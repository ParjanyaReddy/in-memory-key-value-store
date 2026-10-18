#include "store/hash_table.h"

#include <utility>

#include "util/hash.h"

namespace redis_clone {
namespace store {

size_t HashTable::round_up_power_of_two(size_t n) {
    if (n < 16)
        return 16;
    size_t power = 1;
    while (power < n) {
        power <<= 1;
    }
    return power;
}

HashTable::HashTable(size_t initial_capacity, double max_load_factor)
    : capacity_(round_up_power_of_two(initial_capacity)),
      size_(0),
      max_load_factor_(max_load_factor),
      auto_resize_enabled_(true),
      buckets_(capacity_, nullptr) {}

HashTable::~HashTable() {
    clear();
}

HashTable::HashTable(HashTable&& other) noexcept
    : capacity_(other.capacity_),
      size_(other.size_),
      max_load_factor_(other.max_load_factor_),
      auto_resize_enabled_(other.auto_resize_enabled_),
      buckets_(std::move(other.buckets_)) {
    other.capacity_ = 0;
    other.size_ = 0;
}

HashTable& HashTable::operator=(HashTable&& other) noexcept {
    if (this != &other) {
        clear();
        capacity_ = other.capacity_;
        size_ = other.size_;
        max_load_factor_ = other.max_load_factor_;
        auto_resize_enabled_ = other.auto_resize_enabled_;
        buckets_ = std::move(other.buckets_);

        other.capacity_ = 0;
        other.size_ = 0;
    }
    return *this;
}

void HashTable::clear() {
    for (size_t i = 0; i < capacity_; ++i) {
        HashNode* current = buckets_[i];
        while (current != nullptr) {
            HashNode* next_node = current->next;
            delete current;
            current = next_node;
        }
        buckets_[i] = nullptr;
    }
    size_ = 0;
}

double HashTable::load_factor() const {
    if (capacity_ == 0)
        return 0.0;
    return static_cast<double>(size_) / static_cast<double>(capacity_);
}

bool HashTable::set(const std::string& key, const std::string& value) {
    uint64_t hash = util::fnv1a_hash(key);
    size_t index = hash & (capacity_ - 1);

    // Search for existing key in bucket chain
    HashNode* current = buckets_[index];
    while (current != nullptr) {
        if (current->hash_code == hash && current->key == key) {
            current->value = value;
            return false;  // Updated existing key
        }
        current = current->next;
    }

    // Insert new node at bucket chain head
    buckets_[index] = new HashNode(key, value, hash, buckets_[index]);
    size_++;

    // Trigger stop-the-world resize if load factor threshold exceeded
    if (auto_resize_enabled_ && load_factor() > max_load_factor_) {
        resize_stop_the_world(capacity_ * 2);
    }
    return true;  // Inserted new key
}

bool HashTable::get(const std::string& key, std::string& value) const {
    if (capacity_ == 0 || size_ == 0)
        return false;

    uint64_t hash = util::fnv1a_hash(key);
    size_t index = hash & (capacity_ - 1);

    HashNode* current = buckets_[index];
    while (current != nullptr) {
        if (current->hash_code == hash && current->key == key) {
            value = current->value;
            return true;
        }
        current = current->next;
    }
    return false;
}

bool HashTable::contains(const std::string& key) const {
    std::string dummy;
    return get(key, dummy);
}

bool HashTable::del(const std::string& key) {
    if (capacity_ == 0 || size_ == 0)
        return false;

    uint64_t hash = util::fnv1a_hash(key);
    size_t index = hash & (capacity_ - 1);

    HashNode* current = buckets_[index];
    HashNode* prev = nullptr;

    while (current != nullptr) {
        if (current->hash_code == hash && current->key == key) {
            if (prev != nullptr) {
                prev->next = current->next;
            } else {
                buckets_[index] = current->next;
            }
            delete current;
            size_--;
            return true;
        }
        prev = current;
        current = current->next;
    }
    return false;
}

void HashTable::resize_stop_the_world(size_t new_capacity) {
    new_capacity = round_up_power_of_two(new_capacity);
    if (new_capacity <= capacity_)
        return;

    std::vector<HashNode*> new_buckets(new_capacity, nullptr);

    // Rehash all existing nodes into new bucket array without reallocating strings
    for (size_t i = 0; i < capacity_; ++i) {
        HashNode* current = buckets_[i];
        while (current != nullptr) {
            HashNode* next_node = current->next;
            size_t new_index = current->hash_code & (new_capacity - 1);

            current->next = new_buckets[new_index];
            new_buckets[new_index] = current;
            current = next_node;
        }
    }

    buckets_ = std::move(new_buckets);
    capacity_ = new_capacity;
}

}  // namespace store
}  // namespace redis_clone
