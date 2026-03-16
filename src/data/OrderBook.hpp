#pragma once

#include <map>
#include <vector>
#include <functional>
#include <cstdint>

#include "BookSnapshot.hpp"

namespace cts {
namespace data {

    class OrderBook {
    public:
        // Initialize book from REST snapshot.
        // Must be called before any diffs are applied.
        void apply_snapshot(
            uint64_t last_update_id,
            const std::vector<PriceLevel>& bids,
            const std::vector<PriceLevel>& asks
        );

        // Apply one diff event from the WebSocket stream.
        // Returns false if a sequence gap is detected —
        // caller must discard the book and restart.
        bool apply_diff(
            uint64_t first_update_id,
            uint64_t last_update_id,
            const std::vector<PriceLevel>& bid_updates,
            const std::vector<PriceLevel>& ask_updates
        );

        // Build a top-5 snapshot for the feature engine.
        BookSnapshot get_snapshot(int64_t timestamp_ns) const;

        bool is_initialized() const { return initialized_; }

    private:
        // Shared update logic for bids and asks.
        // qty == 0 means remove the level; otherwise insert or update.
        template<typename MapType>
        void apply_updates(
            MapType& side,
            const std::vector<PriceLevel>& updates
        );

        // Bids sorted descending — best bid is begin().
        // Double key is safe: Binance enforces fixed tick size (0.01 USDT),
        // so the same price string always parses to the same bit pattern.
        std::map<double, double, std::greater<double>> bids_;

        // Asks sorted ascending — best ask is begin().
        std::map<double, double> asks_;

        uint64_t last_update_id_{0};
        bool initialized_{false};
    };

} // namespace data
} // namespace cts