#pragma once

#include <vector>
#include <atomic>
#include "macros.hpp"

namespace cts {
namespace data {

    // Single-Producer Single-Consumer lock-free queue.
    // Producer owns write_idx_, consumer owns read_idx_.
    // Capacity must be a power of 2 — enforced by assertion.
    // No shared counter — full/empty derived from idx difference.
    template<typename T>
    class LFQueue final {
    public:
        explicit LFQueue(std::size_t capacity) : store_(capacity, T()) {
            // Capacity must be power of 2 for bitmask indexing
            ASSERT(
                capacity > 0 && (capacity & (capacity - 1)) == 0,
                "LFQueue capacity must be a power of 2, got: " +
                std::to_string(capacity)
            );
        }

        LFQueue()                            = delete;
        LFQueue(const LFQueue&)              = delete;
        LFQueue(LFQueue&&)                   = delete;
        LFQueue& operator=(const LFQueue&)   = delete;
        LFQueue& operator=(LFQueue&&)        = delete;

        // Called by producer thread only
        auto getNextToWriteTo() noexcept -> T* {
            return &store_[write_idx_.load(std::memory_order_relaxed) & index_mask_];
        }

        // Called by producer thread only — must be called after writing the slot
        auto updateWriteIdx() noexcept -> void {
            write_idx_.fetch_add(1, std::memory_order_release);
        }

        // Called by consumer thread only
        // Returns nullptr if queue is empty
        auto getNextToRead() noexcept -> const T* {
            const auto write = write_idx_.load(std::memory_order_acquire);
            const auto read  = read_idx_.load(std::memory_order_relaxed);
            if (write == read) return nullptr;  // empty
            return &store_[read & index_mask_];
        }

        // Called by consumer thread only — must be called after consuming the slot
        auto updateReadIdx() noexcept -> void {
            read_idx_.fetch_add(1, std::memory_order_release);
        }

        // Approximate size — safe to call from either thread
        auto size() const noexcept -> std::size_t {
            const auto write = write_idx_.load(std::memory_order_acquire);
            const auto read  = read_idx_.load(std::memory_order_acquire);
            return (write >= read) ? (write - read) : 0;
        }

    private:
        std::vector<T>  store_;
        const std::size_t index_mask_ = store_.size() - 1;

        // Producer owns write_idx_ — pad to separate cache lines
        alignas(64) std::atomic<std::size_t> write_idx_{0};

        // Consumer owns read_idx_ — pad to separate cache lines
        alignas(64) std::atomic<std::size_t> read_idx_{0};
    };

} // namespace data
} // namespace cts