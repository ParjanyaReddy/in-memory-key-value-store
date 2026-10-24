#ifndef REDIS_CLONE_UTIL_LOGGING_H_
#define REDIS_CLONE_UTIL_LOGGING_H_

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace redis_clone {
namespace util {

enum class LogLevel { DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3, NONE = 4 };

class Logger {
public:
    static Logger& instance() {
        static Logger instance_;
        return instance_;
    }

    void set_level(LogLevel level) { current_level_ = level; }
    LogLevel level() const { return current_level_; }

    template <typename... Args>
    void log(LogLevel level, const char* file, int line, Args&&... args) {
        if (level < current_level_) {
            return;
        }

        auto now = std::chrono::system_clock::now();
        auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        std::time_t timer = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf{};
#ifdef _WIN32
        localtime_s(&tm_buf, &timer);
#else
        localtime_r(&timer, &tm_buf);
#endif

        std::ostringstream ss;
        ss << "[" << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S") << "." << std::setfill('0')
           << std::setw(3) << ms.count() << "] [" << level_to_string(level) << "] ";

        (ss << ... << std::forward<Args>(args));

        if (level == LogLevel::DEBUG || level == LogLevel::ERROR) {
            ss << " (" << file << ":" << line << ")";
        }
        ss << "\n";

        if (level == LogLevel::ERROR) {
            std::cerr << ss.str();
        } else {
            std::cout << ss.str();
        }
    }

private:
    Logger() : current_level_(LogLevel::INFO) {}

    static const char* level_to_string(LogLevel level) {
        switch (level) {
            case LogLevel::DEBUG:
                return "DEBUG";
            case LogLevel::INFO:
                return "INFO ";
            case LogLevel::WARN:
                return "WARN ";
            case LogLevel::ERROR:
                return "ERROR";
            default:
                return "UNKNOWN";
        }
    }

    LogLevel current_level_{LogLevel::INFO};
};

}  // namespace util
}  // namespace redis_clone

#define LOG_DEBUG(...)                                                                      \
    redis_clone::util::Logger::instance().log(redis_clone::util::LogLevel::DEBUG, __FILE__, \
                                              __LINE__, __VA_ARGS__)
#define LOG_INFO(...)                                                                      \
    redis_clone::util::Logger::instance().log(redis_clone::util::LogLevel::INFO, __FILE__, \
                                              __LINE__, __VA_ARGS__)
#define LOG_WARN(...)                                                                      \
    redis_clone::util::Logger::instance().log(redis_clone::util::LogLevel::WARN, __FILE__, \
                                              __LINE__, __VA_ARGS__)
#define LOG_ERROR(...)                                                                      \
    redis_clone::util::Logger::instance().log(redis_clone::util::LogLevel::ERROR, __FILE__, \
                                              __LINE__, __VA_ARGS__)

#endif  // REDIS_CLONE_UTIL_LOGGING_H_
