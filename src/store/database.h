#ifndef REDIS_CLONE_STORE_DATABASE_H_
#define REDIS_CLONE_STORE_DATABASE_H_

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

#include "store/hash_table.h"
#include "store/ttl_heap.h"

namespace redis_clone {
namespace store {

/**
 * CORE LOGIC: understand this file line by line.
 * Database unifies the separate-chaining HashTable (with incremental rehashing)
 * and the TtlHeap (with active expiry and lazy eviction).
 *
 * Expiry Model:
 * 1. Lazy expiry: On every key access (get, contains, ttl), expired keys are deleted immediately.
 * 2. Active expiry: Bounded batch eviction in step() pops expired entries from the min-heap.
 * 3. Stale resolution: Monotonic per-key version numbers discard stale entries from
 * overwrites/deletes.
 */
class Database {
public:
    using Clock = std::function<uint64_t()>;

    explicit Database(size_t initial_capacity = HashTable::kDefaultInitialCapacity,
                      double max_load_factor = HashTable::kDefaultMaxLoadFactor);
    ~Database() = default;

    // Movable, non-copyable
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&&) noexcept = default;
    Database& operator=(Database&&) noexcept = default;

    /**
     * Store a key-value pair, optionally with an absolute expiration timestamp in milliseconds.
     * Overwriting resets the key's TTL if expire_at_ms is 0, and increments its monotonic version.
     * Returns true if key was newly inserted, false if updated.
     */
    bool set(const std::string& key, const std::string& value, uint64_t expire_at_ms = 0);

    /**
     * Retrieve value for key. Executes lazy expiry check first.
     * Returns true if key exists and is unexpired, false otherwise.
     */
    bool get(const std::string& key, std::string& value);

    /**
     * Delete a key. Any active TTL entries become stale via version mismatch.
     * Returns true if key was deleted, false if non-existent.
     */
    bool del(const std::string& key);

    /**
     * Check existence with lazy expiry check.
     */
    bool contains(const std::string& key);

    /**
     * Set a relative TTL in seconds on an existing key.
     * Returns true on success, false if key does not exist or was expired.
     */
    bool expire(const std::string& key, uint64_t seconds);

    /**
     * Set a relative TTL in milliseconds on an existing key.
     * Returns true on success, false if key does not exist or was expired.
     */
    bool pexpire(const std::string& key, uint64_t milliseconds);

    /**
     * Get remaining TTL in seconds.
     * Returns:
     *   -2: key does not exist (or expired)
     *   -1: key exists but has no associated expire
     *  >=0: remaining time in seconds
     */
    int64_t ttl(const std::string& key);

    /**
     * Get remaining TTL in milliseconds.
     * Returns:
     *   -2: key does not exist (or expired)
     *   -1: key exists but has no associated expire
     *  >=0: remaining time in milliseconds
     */
    int64_t pttl(const std::string& key);

    /**
     * Active eviction pass: pops and validates up to max_keys expired entries.
     * Guarantees event loop latency is bounded even during mass-expiry events.
     * Returns number of keys actually evicted.
     */
    size_t active_expire_step(size_t max_keys = 100);

    /**
     * Step incremental rehashing in HashTable.
     */
    bool rehash_step(size_t max_buckets = 8, size_t max_empty = 80);

    /**
     * Single iteration maintenance called on every event loop tick.
     * Executes both bounded incremental rehashing and bounded active expiry.
     */
    void step();

    /**
     * Deallocate all keys and clear TTL heap.
     */
    void clear();

    // Inspectors
    size_t size();
    size_t raw_size() const { return hash_table_.size(); }
    size_t heap_size() const { return ttl_heap_.size(); }
    bool is_rehashing() const { return hash_table_.is_rehashing(); }

    HashTable& hash_table() { return hash_table_; }
    const HashTable& hash_table() const { return hash_table_; }
    TtlHeap& ttl_heap() { return ttl_heap_; }
    const TtlHeap& ttl_heap() const { return ttl_heap_; }

    // Mock clock injection for deterministic unit testing
    void set_clock(Clock clock) { clock_ = std::move(clock); }
    void reset_clock() { clock_ = nullptr; }
    uint64_t now_ms() const { return clock_ ? clock_() : system_now_ms(); }

private:
    static uint64_t system_now_ms();

    HashTable hash_table_;
    TtlHeap ttl_heap_;
    Clock clock_;
};

}  // namespace store
}  // namespace redis_clone

#endif  // REDIS_CLONE_STORE_DATABASE_H_
