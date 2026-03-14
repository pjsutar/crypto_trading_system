#pragma once

#include "logger.hpp"
#include "time_utils.hpp"

namespace cts {
namespace data {

    class LatencyLogger {
    public:
        explicit LatencyLogger(const std::string& file_path)
            : logger_(file_path) {}

        // exchange_timestamp_ms  — the 'E' field from Binance JSON (milliseconds)
        // book_updated_ns        — steady_clock timestamp after book update completes
        auto log(int64_t exchange_timestamp_ms,
                 int64_t book_updated_ns) noexcept -> void {

            const int64_t exchange_ns  = exchange_timestamp_ms * 1'000'000LL;
            const int64_t latency_us   = (book_updated_ns - exchange_ns) / 1'000LL;
            const int64_t wall_time_ns = Common::getCurrentNanos();

            // Format: <wall_time_ns> <latency_us> us
            logger_.log("% % us\n", wall_time_ns, latency_us);
        }

    private:
        Common::Logger logger_;
    };

} // namespace data
} // namespace cts