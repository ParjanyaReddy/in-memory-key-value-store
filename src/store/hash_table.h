#ifndef REDIS_CLONE_STORE_HASH_TABLE_H_
#define REDIS_CLONE_STORE_HASH_TABLE_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace redis_clone {
namespace store {

/**
 * Node structure for separate-chaining linked lists.
 * Ownership: HashTable uniquely owns all HashNode pointers in its buckets array.
 * Allocation is performed in set(); deallocation in del(), clear(), and ~HashTable().
 */
struct HashNode {
    std::string key;
    std::string value;
    uint64_t hash_code{0};
    HashNode* next{nullptr};

    HashNode(std::string k, std::string v, uint64_t h, HashNode* n = nullptr)
        : key(std::move(k)), value(std::move(v)), hash_code(h), next(n) {}
};

/**
 * Internal bucket array table structure used for dual-table incremental rehashing.
 */
struct Table {
    std::vector<HashNode*> buckets;
    size_t capacity{0};
    size_t size{0};

    Table() = default;
    explicit Table(size_t cap) : buckets(cap, nullptr), capacity(cap), size(0) {}
};

/**
 * CORE LOGIC: understand this file line by line.
 * Separate-chaining hash table with power-of-two capacity, amortized incremental rehashing
 * (dual-table migration with empty bucket visit cap), and selectable stop-the-world resizing.
 */
class HashTable {
public:
    static constexpr size_t kDefaultInitialCapacity = 16;
    static constexpr double kDefaultMaxLoadFactor = 1.0;
    static constexpr size_t kDefaultMigrationBatchSize = 8;
    static constexpr size_t kDefaultMaxEmptyBucketsToVisit = 80;

    explicit HashTable(size_t initial_capacity = kDefaultInitialCapacity,
                       double max_load_factor = kDefaultMaxLoadFactor);
    ~HashTable();

    // Disable copy semantics to prevent accidental expensive node duplications
    HashTable(const HashTable&) = delete;
    HashTable& operator=(const HashTable&) = delete;

    // Enable move semantics
    HashTable(HashTable&& other) noexcept;
    HashTable& operator=(HashTable&& other) noexcept;

    /**
     * Insert or update a key-value pair.
     * If incremental rehashing is active, executes a migration step and directs
     * new writes to table_new_ while removing stale duplicates in table_old_.
     * Returns true if key was newly inserted, false if an existing key was updated.
     */
    bool set(const std::string& key, const std::string& value);

    /**
     * Retrieve value for key. Non-const overload steps incremental rehashing.
     * Checks table_new_ first, then table_old_. Returns true if found.
     */
    bool get(const std::string& key, std::string& value);

    /**
     * Const retrieve value for key without stepping incremental rehashing.
     */
    bool get(const std::string& key, std::string& value) const;

    /**
     * Delete key from the hash table, checking both tables.
     * Steps incremental rehashing and returns true if key was found and deleted.
     */
    bool del(const std::string& key);

    /**
     * Check if key exists.
     */
    bool contains(const std::string& key);
    bool contains(const std::string& key) const;

    /**
     * Deallocate all nodes across both tables and reset to empty state.
     */
    void clear();

    /**
     * Initiate incremental rehashing to new_capacity (rounded to power of two).
     * Allocates table_new_ and resets rehash_index_ to 0.
     * Returns true if rehashing started, false if already rehashing or invalid size.
     */
    bool start_incremental_rehash(size_t new_capacity);

    /**
     * Perform a bounded batch migration step from table_old_ to table_new_.
     * Migrates up to max_buckets non-empty buckets and visits at most max_empty buckets.
     * Returns true if rehashing is still in progress, false if completed or inactive.
     */
    bool rehash_step(size_t max_buckets = kDefaultMigrationBatchSize,
                     size_t max_empty = kDefaultMaxEmptyBucketsToVisit);

    /**
     * Stop-the-world resize: doubles the bucket array and migrates all nodes in one pass.
     * Kept as a selectable baseline to benchmark against incremental rehashing.
     */
    void resize_stop_the_world(size_t new_capacity);

    // Inspector methods
    size_t size() const;
    size_t capacity() const;
    double load_factor() const;

    bool is_rehashing() const { return is_rehashing_; }
    size_t rehash_index() const { return rehash_index_; }
    size_t table_old_size() const { return table_old_.size; }
    size_t table_new_size() const { return is_rehashing_ ? table_new_.size : 0; }
    size_t table_old_capacity() const { return table_old_.capacity; }
    size_t table_new_capacity() const { return is_rehashing_ ? table_new_.capacity : 0; }

    void set_auto_resize(bool enable) { auto_resize_enabled_ = enable; }
    void set_incremental_rehash(bool enable) { incremental_rehash_enabled_ = enable; }
    bool is_incremental_rehash_enabled() const { return incremental_rehash_enabled_; }
    void set_max_load_factor(double mlf) { max_load_factor_ = mlf; }
    void set_migration_batch_size(size_t bs) { migration_batch_size_ = bs; }
    void set_max_empty_buckets_to_visit(size_t me) { max_empty_buckets_to_visit_ = me; }

private:
    static size_t round_up_power_of_two(size_t n);
    void finish_rehash();

    Table table_old_;
    Table table_new_;
    bool is_rehashing_{false};
    size_t rehash_index_{0};

    double max_load_factor_{kDefaultMaxLoadFactor};
    bool auto_resize_enabled_{true};
    bool incremental_rehash_enabled_{true};
    size_t migration_batch_size_{kDefaultMigrationBatchSize};
    size_t max_empty_buckets_to_visit_{kDefaultMaxEmptyBucketsToVisit};
};

}  // namespace store
}  // namespace redis_clone

#endif  // REDIS_CLONE_STORE_HASH_TABLE_H_
