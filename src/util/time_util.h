#ifndef REDIS_CLONE_UTIL_TIME_UTIL_H_
#define REDIS_CLONE_UTIL_TIME_UTIL_H_

#include <chrono>
#include <cstdint>

namespace redis_clone {
namespace util {

/**
 * Returns current monotonic timestamp in milliseconds since steady_clock epoch.
 * Immune to wall-clock time adjustments (NTP shifts, leap seconds, daylight saving).
 */
inline uint64_t current_time_ms() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

}  // namespace util
}  // namespace redis_clone

#endif  // REDIS_CLONE_UTIL_TIME_UTIL_H_
