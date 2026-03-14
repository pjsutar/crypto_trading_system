#pragma once

#include <string>
#include <chrono>
#include <ctime>

namespace cts {
namespace Common {
    // Nanosecond typedef used throughout the project
    typedef int64_t Nanos;

    constexpr Nanos NANOS_TO_MICROS  = 1000;
    constexpr Nanos MICROS_TO_MILLIS = 1000;
    constexpr Nanos MILLIS_TO_SECS   = 1000;
    constexpr Nanos NANOS_TO_MILLIS  = NANOS_TO_MICROS  * MICROS_TO_MILLIS;
    constexpr Nanos NANOS_TO_SECS    = NANOS_TO_MILLIS  * MILLIS_TO_SECS;

    // Wall-clock nanoseconds — for timestamping log entries (absolute time)
    inline auto getCurrentNanos() noexcept {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }

    // Monotonic nanoseconds — use this for latency measurement intervals ONLY
    inline auto getSteadyNanos() noexcept {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    }

    // Human-readable wall time string for stderr diagnostics
    inline auto getCurrentTimeStr(std::string* time_str) noexcept {
        const auto time = std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::now()
        );
        time_str->assign(ctime(&time));
        // ctime appends '\n' — replace with null terminator
        if (!time_str->empty()) {
            time_str->back() = '\0';
        }
        return *time_str;
    }

} // namespace Common
} // namespace cts