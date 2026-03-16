#pragma once

#include <array>
#include <cstdint>

namespace cts {
namespace data {

    // One price level: a price and the total quantity resting there
    struct PriceLevel {
        double price    {0.0};
        double quantity {0.0};
    };

    // Top-5 view of the book passed to the feature engine.
    // Plain data — no methods, no logic.
    struct BookSnapshot {
        // bids[0] is best bid, bids[1] is second best, and so on
        std::array<PriceLevel, 5> bids{};

        // asks[0] is best ask, asks[1] is second best, and so on
        std::array<PriceLevel, 5> asks{};

        double best_bid{0.0};
        double best_ask{0.0};
        double mid_price{0.0};

        // Nanoseconds since epoch when this snapshot was taken
        int64_t timestamp_ns{0};
    };

} // namespace data
} // namespace cts