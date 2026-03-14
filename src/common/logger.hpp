#pragma once

#include <string>
#include <fstream>
#include <atomic>
#include <thread>

#include "macros.hpp"
#include "time_utils.hpp"
#include "thread_utils.hpp"
#include "lockfree_queue.hpp"

namespace cts {
namespace Common {

    constexpr std::size_t LOG_QUEUE_SIZE = 8 * 1024 * 1024;

    enum class LogType : int8_t {
        CHAR = 0,
        INTEGER,
        LONG_INTEGER,
        LONG_LONG_INTEGER,
        UNSIGNED_INTEGER,
        UNSIGNED_LONG_INTEGER,
        UNSIGNED_LONG_LONG_INTEGER,
        FLOAT,
        DOUBLE
    };

    struct LogElement {
        LogType type_ = LogType::CHAR;
        union {
            char               c;
            int                i;
            long               l;
            long long          ll;
            unsigned           u;
            unsigned long      ul;
            unsigned long long ull;
            float              f;
            double             d;
        } u_;
    };

    class Logger final {
    public:
        explicit Logger(const std::string& file_name)
            : file_name_(file_name), queue_(LOG_QUEUE_SIZE)
        {
            file_.open(file_name);
            ASSERT(file_.is_open(), "Could not open log file: " + file_name);

            // Pin to no specific core (-1), name thread "Logger"
            logger_thread_ = Common::createAndStartThread(
                -1, "Logger", [this]{ flushQueue(); }
            );
            ASSERT(logger_thread_ != nullptr, "Failed to start Logger thread.");
        }

        // Non-copyable, non-movable — owns file handle and thread
        Logger(const Logger&)            = delete;
        Logger& operator=(const Logger&) = delete;
        Logger(Logger&&)                 = delete;
        Logger& operator=(Logger&&)      = delete;

        ~Logger() noexcept {
            std::string time_str;
            std::cerr << Common::getCurrentTimeStr(&time_str)
                      << " Flushing and closing Logger for "
                      << file_name_ << "\n";

            // Signal logger thread to exit after draining
            running_.store(false, std::memory_order_release);

            if (logger_thread_ && logger_thread_->joinable()) {
                logger_thread_->join();
            }
            delete logger_thread_;

            file_.close();
            std::cerr << Common::getCurrentTimeStr(&time_str)
                      << " Logger for " << file_name_ << " exiting.\n";
        }

        // Called by logger thread — drains queue to file
        auto flushQueue() noexcept -> void {
            while (running_.load(std::memory_order_acquire) || queue_.size()) {
                const LogElement* next = queue_.getNextToRead();
                if (next) {
                    writeElement(*next);
                    queue_.updateReadIdx();
                } else {
                    // Queue empty — sleep to avoid spinning at 100% CPU
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }
            // Final flush to disk before thread exits
            file_.flush();
        }

        // ── Push overloads — called by producer thread ────────────
        auto pushValue(const LogElement& log_element) noexcept -> void {
            *queue_.getNextToWriteTo() = log_element;
            queue_.updateWriteIdx();
        }

        auto pushValue(char value) noexcept -> void {
            LogElement elem;
            elem.type_ = LogType::CHAR;
            elem.u_.c  = value;
            pushValue(elem);
        }

        auto pushValue(int value) noexcept -> void {
            LogElement elem;
            elem.type_ = LogType::INTEGER;
            elem.u_.i  = value;
            pushValue(elem);
        }

        auto pushValue(long value) noexcept -> void {
            LogElement elem;
            elem.type_ = LogType::LONG_INTEGER;
            elem.u_.l  = value;
            pushValue(elem);
        }

        auto pushValue(long long value) noexcept -> void {
            LogElement elem;
            elem.type_ = LogType::LONG_LONG_INTEGER;
            elem.u_.ll = value;
            pushValue(elem);
        }

        auto pushValue(unsigned value) noexcept -> void {
            LogElement elem;
            elem.type_ = LogType::UNSIGNED_INTEGER;
            elem.u_.u  = value;
            pushValue(elem);
        }

        auto pushValue(unsigned long value) noexcept -> void {
            LogElement elem;
            elem.type_ = LogType::UNSIGNED_LONG_INTEGER;
            elem.u_.ul = value;
            pushValue(elem);
        }

        auto pushValue(unsigned long long value) noexcept -> void {
            LogElement elem;
            elem.type_ = LogType::UNSIGNED_LONG_LONG_INTEGER;
            elem.u_.ull = value;
            pushValue(elem);
        }

        auto pushValue(float value) noexcept -> void {
            LogElement elem;
            elem.type_ = LogType::FLOAT;
            elem.u_.f  = value;
            pushValue(elem);
        }

        auto pushValue(double value) noexcept -> void {
            LogElement elem;
            elem.type_ = LogType::DOUBLE;
            elem.u_.d  = value;
            pushValue(elem);
        }

        auto pushValue(const char* value) noexcept -> void {
            while (*value) {
                pushValue(*value++);
            }
        }

        auto pushValue(const std::string& value) noexcept -> void {
            pushValue(value.c_str());
        }

        // Printf-style log: log("price=% qty=%", price, qty)
        template<typename T, typename... A>
        auto log(const char* s, const T& value, A... args) noexcept -> void {
            while (*s) {
                if (*s == '%') {
                    if (UNLIKELY(*(s + 1) == '%')) {
                        ++s;
                    } else {
                        pushValue(value);
                        log(s + 1, args...);
                        return;
                    }
                }
                pushValue(*s++);
            }
            FATAL("extra arguments provided to log()");
        }

        auto log(const char* s) noexcept -> void {
            while (*s) {
                if (*s == '%') {
                    if (UNLIKELY(*(s + 1) == '%')) {
                        ++s;
                    } else {
                        FATAL("missing arguments to log()");
                    }
                }
                pushValue(*s++);
            }
        }

    private:
        // Write a single LogElement to the file — called by logger thread only
        auto writeElement(const LogElement& elem) noexcept -> void {
            switch (elem.type_) {
                case LogType::CHAR:                    file_ << elem.u_.c;   break;
                case LogType::INTEGER:                 file_ << elem.u_.i;   break;
                case LogType::LONG_INTEGER:            file_ << elem.u_.l;   break;
                case LogType::LONG_LONG_INTEGER:       file_ << elem.u_.ll;  break;
                case LogType::UNSIGNED_INTEGER:        file_ << elem.u_.u;   break;
                case LogType::UNSIGNED_LONG_INTEGER:   file_ << elem.u_.ul;  break;
                case LogType::UNSIGNED_LONG_LONG_INTEGER: file_ << elem.u_.ull; break;
                case LogType::FLOAT:                   file_ << elem.u_.f;   break;
                case LogType::DOUBLE:                  file_ << elem.u_.d;   break;
            }
        }

        const std::string file_name_;
        std::ofstream file_;
        cts::data::LFQueue<LogElement> queue_;
        std::atomic<bool> running_{true};
        std::thread* logger_thread_{nullptr};
    };

} // namespace data
} // namespace cts