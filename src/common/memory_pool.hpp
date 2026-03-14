#pragma once

#include <cstdint>
#include <vector>
#include <string>

#include "macros.hpp"

namespace cts {
namespace Common {

    // Fixed-capacity memory pool using placement new.
    // Eliminates heap allocation overhead for frequently created/destroyed objects.
    // Not thread-safe — external synchronization required if used across threads.
    template<typename T>
    class MemPool final {
    public:
        explicit MemPool(std::size_t num_elems) : store_(num_elems, {T(), true}) {
            // Verify object_ is the first member of ObjectBlock so that a pointer
            // to object_ is bitwise identical to a pointer to its containing ObjectBlock.
            // This is required for O(1) deallocate pointer arithmetic.
            ASSERT(
                reinterpret_cast<const ObjectBlock*>(&store_[0].object_) == &store_[0],
                "T object_ must be first member of ObjectBlock for pointer arithmetic to work."
            );
        }

        MemPool()                          = delete;
        MemPool(const MemPool&)            = delete;
        MemPool(MemPool&&)                 = delete;
        MemPool& operator=(const MemPool&) = delete;
        MemPool& operator=(MemPool&&)      = delete;

        // Allocate and construct a T in the next free slot.
        // Returns pointer to constructed object, or fatals if pool is exhausted.
        template<typename... Args>
        T* allocate(Args&&... args) noexcept {
            auto* obj_block = &store_[next_free_idx_];
            ASSERT(
                obj_block->is_free_,
                "Expected free ObjectBlock at index: " + std::to_string(next_free_idx_)
            );

            T* ret = &obj_block->object_;
            ret = new (ret) T(std::forward<Args>(args)...);
            obj_block->is_free_ = false;

            updateNextFreeIdx();
            return ret;
        }

        // Destruct and release a previously allocated object back to the pool.
        // elem must have been returned by this pool's allocate().
        auto deallocate(const T* elem) noexcept -> void {
            const auto elem_index =
                reinterpret_cast<const ObjectBlock*>(elem) - &store_[0];

            ASSERT(
                elem_index >= 0 && static_cast<std::size_t>(elem_index) < store_.size(),
                "Element being deallocated does not belong to this Memory Pool."
            );
            ASSERT(
                !store_[elem_index].is_free_,
                "Expected in-use ObjectBlock at index: " + std::to_string(elem_index)
            );

            // Must explicitly invoke destructor since we used placement new
            store_[elem_index].object_.~T();
            store_[elem_index].is_free_ = true;
        }

    private:
        struct ObjectBlock {
            T    object_;           // must remain first member — see constructor assert
            bool is_free_ = true;
        };

        // Scan forward from current position to find next free slot.
        // Wraps around. Fatals if pool is exhausted.
        auto updateNextFreeIdx() noexcept -> void {
            const auto initial_idx = next_free_idx_;
            while (!store_[next_free_idx_].is_free_) {
                ++next_free_idx_;
                if (UNLIKELY(next_free_idx_ == store_.size())) {
                    next_free_idx_ = 0;  // wrap around
                }
                if (UNLIKELY(next_free_idx_ == initial_idx)) {
                    FATAL("MemPool exhausted — no free blocks available.");
                }
            }
        }

        std::vector<ObjectBlock> store_;
        std::size_t              next_free_idx_{0};
    };

} // namespace data
} // namespace cts