#ifndef REDIS_CLONE_STORE_TTL_HEAP_H_
#define REDIS_CLONE_STORE_TTL_HEAP_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace redis_clone {
namespace store {

/**
 * Entry tracked in the TTL min-heap.
 * Contains expiry timestamp in milliseconds, key identifier, and monotonic version.
 */
struct TtlEntry {
    uint64_t expire_at_ms{0};
    std::string key;
    uint64_t version{0};

    TtlEntry() = default;
    TtlEntry(uint64_t exp, std::string k, uint64_t v)
        : expire_at_ms(exp), key(std::move(k)), version(v) {}
};

/**
 * CORE LOGIC: understand this file line by line.
 * Hand-written binary min-heap ordering TTL entries by expire_at_ms.
 * Memory layout: contiguous std::vector<TtlEntry> with 0-based indexing.
 * Written from scratch to satisfy systems fundamentals requirements (zero std::priority_queue).
 */
class TtlHeap {
public:
    TtlHeap() = default;
    ~TtlHeap() = default;

    // Movable, non-copyable
    TtlHeap(const TtlHeap&) = delete;
    TtlHeap& operator=(const TtlHeap&) = delete;
    TtlHeap(TtlHeap&&) noexcept = default;
    TtlHeap& operator=(TtlHeap&&) noexcept = default;

    /**
     * Push a new expiration entry into the min-heap.
     * Time Complexity: O(log N).
     */
    void push(std::string key, uint64_t expire_at_ms, uint64_t version);

    /**
     * Peek at the earliest expiring entry without removing it.
     * Returns true if heap is non-empty, false otherwise.
     */
    bool peek_min(TtlEntry& entry) const;

    /**
     * Remove and return the earliest expiring entry.
     * Time Complexity: O(log N).
     */
    bool pop_min(TtlEntry& entry);

    size_t size() const { return heap_.size(); }
    bool empty() const { return heap_.empty(); }
    void clear() { heap_.clear(); }

private:
    std::vector<TtlEntry> heap_;

    void sift_up(size_t index);
    void sift_down(size_t index);
};

}  // namespace store
}  // namespace redis_clone

#endif  // REDIS_CLONE_STORE_TTL_HEAP_H_
