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
 * CORE LOGIC: understand this file line by line.
 * Separate-chaining hash table written from scratch with power-of-two capacity
 * and selectable stop-the-world resizing for benchmark comparison.
 */
class HashTable {
public:
    static constexpr size_t kDefaultInitialCapacity = 16;
    static constexpr double kDefaultMaxLoadFactor = 1.0;

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
     * Returns true if key was newly inserted, false if an existing key was updated.
     */
    bool set(const std::string& key, const std::string& value);

    /**
     * Retrieve value for key. Returns true if found, false otherwise.
     */
    bool get(const std::string& key, std::string& value) const;

    /**
     * Delete key from the hash table. Returns true if key existed and was deleted.
     */
    bool del(const std::string& key);

    /**
     * Check if key exists.
     */
    bool contains(const std::string& key) const;

    /**
     * Deallocate all nodes and reset table to empty state.
     */
    void clear();

    /**
     * Stop-the-world resize: doubles the bucket array and migrates all nodes in one pass.
     * Kept as a selectable baseline to benchmark against incremental rehashing.
     */
    void resize_stop_the_world(size_t new_capacity);

    // Inspector methods
    size_t size() const { return size_; }
    size_t capacity() const { return capacity_; }
    double load_factor() const;

    void set_auto_resize(bool enable) { auto_resize_enabled_ = enable; }
    void set_max_load_factor(double mlf) { max_load_factor_ = mlf; }

private:
    static size_t round_up_power_of_two(size_t n);

    size_t capacity_{kDefaultInitialCapacity};
    size_t size_{0};
    double max_load_factor_{kDefaultMaxLoadFactor};
    bool auto_resize_enabled_{true};
    std::vector<HashNode*> buckets_;
};

}  // namespace store
}  // namespace redis_clone

#endif  // REDIS_CLONE_STORE_HASH_TABLE_H_
