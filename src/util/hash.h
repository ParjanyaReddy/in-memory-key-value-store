#ifndef REDIS_CLONE_UTIL_HASH_H_
#define REDIS_CLONE_UTIL_HASH_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace redis_clone {
namespace util {

/**
 * 64-bit FNV-1a (Fowler-Noll-Vo) hash function.
 * Fast, non-cryptographic hash with uniform bit dispersion across power-of-two buckets.
 */
inline uint64_t fnv1a_hash(const char* data, size_t length) {
    constexpr uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ULL;
    constexpr uint64_t kFnvPrime = 0x100000001b3ULL;

    uint64_t hash = kFnvOffsetBasis;
    for (size_t i = 0; i < length; ++i) {
        hash ^= static_cast<uint8_t>(data[i]);
        hash *= kFnvPrime;
    }
    return hash;
}

inline uint64_t fnv1a_hash(std::string_view str) {
    return fnv1a_hash(str.data(), str.size());
}

}  // namespace util
}  // namespace redis_clone

#endif  // REDIS_CLONE_UTIL_HASH_H_
