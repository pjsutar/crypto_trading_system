#pragma once

#include <iostream>
#include <atomic>
#include <thread>
#include <pthread.h>

namespace cts {
namespace Common {

    // Pin the calling thread to a specific CPU core.
    // Returns true on success, false on failure.
    // Pass coreId = -1 to skip affinity setting.
    inline auto setThreadCore(int core_id) noexcept -> bool {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core_id, &cpuset);
        return (pthread_setaffinity_np(
            pthread_self(), sizeof(cpu_set_t), &cpuset) == 0
        );
    }

    // Creates and starts a thread, optionally pinning it to a CPU core.
    // core_id = -1 means no affinity constraint.
    // Blocks until the thread has started (or failed) before returning.
    // Returns nullptr if thread failed to start.
    template<typename T, typename... A>
    inline auto createAndStartThread(
        int core_id,
        const std::string& name,
        T&& func,
        A&&... args
    ) noexcept -> std::thread* {

        std::atomic<bool> running{false};
        std::atomic<bool> failed{false};

        auto thread_body = [&] {
            // Only set affinity if a valid core was requested
            if (core_id >= 0 && !setThreadCore(core_id)) {
                std::cerr << "Failed to set core affinity for "
                          << name << " tid=" << pthread_self()
                          << " to core " << core_id << "\n";
                failed.store(true, std::memory_order_release);
                return;
            }

            if (core_id >= 0) {
                std::cout << "Set core affinity for "
                          << name << " tid=" << pthread_self()
                          << " to core " << core_id << "\n";
            }

            running.store(true, std::memory_order_release);
            std::forward<T>(func)(std::forward<A>(args)...);
        };

        auto* t = new std::thread(thread_body);

        // Spin until thread confirms startup or failure
        while (!running.load(std::memory_order_acquire) &&
               !failed.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        if (failed.load(std::memory_order_acquire)) {
            t->join();
            delete t;
            return nullptr;
        }

        return t;
    }

} // namespace Common
} // namespace cts