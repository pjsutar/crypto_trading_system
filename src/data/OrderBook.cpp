#include "OrderBook.hpp"
#include <stdexcept>
#include <iostream>

namespace cts {
namespace data {

    // Snapshot

    void OrderBook::apply_snapshot(
        uint64_t last_update_id,
        const std::vector<PriceLevel>& bids,
        const std::vector<PriceLevel>& asks
    ) {
        // Wipe any existing state before loading fresh data
        bids_.clear();
        asks_.clear();

        // Load all bid levels from the snapshot
        for (const auto& level : bids) {
            if (level.quantity > 0.0) {
                bids_[level.price] = level.quantity;
            }
        }

        // Load all ask levels from the snapshot
        for (const auto& level : asks) {
            if (level.quantity > 0.0) {
                asks_[level.price] = level.quantity;
            }
        }

        last_update_id_ = last_update_id;
        initialized_ = true;
    }

    // Diff application

    bool OrderBook::apply_diff(
        uint64_t first_update_id,
        uint64_t last_update_id,
        const std::vector<PriceLevel>& bid_updates,
        const std::vector<PriceLevel>& ask_updates
    ) {
        // Drop events already covered by the snapshot
        if (last_update_id <= last_update_id_) {
            return true;
        }

        // Validate the first event after the snapshot.
        // Binance requires: U <= lastUpdateId+1 <= u
        if (!initialized_) {
            std::cerr << "[OrderBook] apply_diff called before snapshot\n";
            return false;
        }

        // After the first valid event, every subsequent event must
        // arrive with no gaps: U must equal previous u + 1
        if (first_update_id != last_update_id_ + 1) {
            std::cerr << "[OrderBook] sequence gap detected: "
                      << "expected U=" << last_update_id_ + 1
                      << " got U="     << first_update_id  << "\n";
            initialized_ = false;   // force caller to restart
            return false;
        }

        apply_updates(bids_, bid_updates);
        apply_updates(asks_, ask_updates);

        last_update_id_ = last_update_id;
        return true;
    }

    // Update one side of the book

    template<typename MapType>
    void OrderBook::apply_updates(
        MapType& side,
        const std::vector<PriceLevel>& updates
    ) {
        for (const auto& level : updates) {
            if (level.quantity == 0.0) {
                // qty == 0 means Binance removed this price level
                side.erase(level.price);
            } else {
                // Insert new level or update existing quantity
                side[level.price] = level.quantity;
            }
        }
    }

    // Snapshot for feature engine

    BookSnapshot OrderBook::get_snapshot(int64_t timestamp_ns) const {
        BookSnapshot snap{};
        snap.timestamp_ns = timestamp_ns;

        // bids_ is sorted descending — first 5 are the best bids
        std::size_t i = 0;
        for (auto it = bids_.begin();
             it != bids_.end() && i < 5;
             ++it, ++i)
        {
            snap.bids[i] = {it->first, it->second};
        }

        // asks_ is sorted ascending — first 5 are the best asks
        i = 0;
        for (auto it = asks_.begin();
             it != asks_.end() && i < 5;
             ++it, ++i)
        {
            snap.asks[i] = {it->first, it->second};
        }

        // Derived values — only valid when both sides are non-empty
        if (!bids_.empty() && !asks_.empty()) {
            snap.best_bid  = bids_.begin()->first;
            snap.best_ask  = asks_.begin()->first;
            snap.mid_price = (snap.best_bid + snap.best_ask) / 2.0;
        }

        return snap;
    }

    // Explicit instantiations — needed because the template is
    // defined in .cpp, not in the header
    template void OrderBook::apply_updates(
        std::map<double, double, std::greater<double>>&,
        const std::vector<PriceLevel>&
    );
    template void OrderBook::apply_updates(
        std::map<double, double>&,
        const std::vector<PriceLevel>&
    );

} // namespace data
} // namespace cts