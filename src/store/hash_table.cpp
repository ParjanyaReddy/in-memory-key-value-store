#include "store/hash_table.h"

#include <utility>

#include "util/hash.h"

namespace redis_clone {
namespace store {

size_t HashTable::round_up_power_of_two(size_t n) {
    if (n < 16) {
        return 16;
    }
    size_t power = 1;
    while (power < n) {
        power <<= 1;
    }
    return power;
}

HashTable::HashTable(size_t initial_capacity, double max_load_factor)
    : table_old_(round_up_power_of_two(initial_capacity)),
      table_new_(),
      is_rehashing_(false),
      rehash_index_(0),
      max_load_factor_(max_load_factor),
      auto_resize_enabled_(true),
      incremental_rehash_enabled_(true),
      migration_batch_size_(kDefaultMigrationBatchSize),
      max_empty_buckets_to_visit_(kDefaultMaxEmptyBucketsToVisit) {}

HashTable::~HashTable() {
    clear();
}

HashTable::HashTable(HashTable&& other) noexcept
    : table_old_(std::move(other.table_old_)),
      table_new_(std::move(other.table_new_)),
      is_rehashing_(other.is_rehashing_),
      rehash_index_(other.rehash_index_),
      max_load_factor_(other.max_load_factor_),
      auto_resize_enabled_(other.auto_resize_enabled_),
      incremental_rehash_enabled_(other.incremental_rehash_enabled_),
      migration_batch_size_(other.migration_batch_size_),
      max_empty_buckets_to_visit_(other.max_empty_buckets_to_visit_) {
    other.is_rehashing_ = false;
    other.rehash_index_ = 0;
}

HashTable& HashTable::operator=(HashTable&& other) noexcept {
    if (this != &other) {
        clear();
        table_old_ = std::move(other.table_old_);
        table_new_ = std::move(other.table_new_);
        is_rehashing_ = other.is_rehashing_;
        rehash_index_ = other.rehash_index_;
        max_load_factor_ = other.max_load_factor_;
        auto_resize_enabled_ = other.auto_resize_enabled_;
        incremental_rehash_enabled_ = other.incremental_rehash_enabled_;
        migration_batch_size_ = other.migration_batch_size_;
        max_empty_buckets_to_visit_ = other.max_empty_buckets_to_visit_;

        other.is_rehashing_ = false;
        other.rehash_index_ = 0;
    }
    return *this;
}

void HashTable::finish_rehash() {
    table_old_.buckets.clear();
    table_old_ = std::move(table_new_);
    table_new_ = Table{};
    is_rehashing_ = false;
    rehash_index_ = 0;
}

void HashTable::clear() {
    auto clear_table = [](Table& t) {
        for (size_t i = 0; i < t.capacity; ++i) {
            HashNode* current = t.buckets[i];
            while (current != nullptr) {
                HashNode* next_node = current->next;
                delete current;
                current = next_node;
            }
            t.buckets[i] = nullptr;
        }
        t.size = 0;
    };

    clear_table(table_old_);
    if (is_rehashing_) {
        clear_table(table_new_);
        table_new_ = Table{};
        is_rehashing_ = false;
        rehash_index_ = 0;
    }
}

size_t HashTable::size() const {
    return table_old_.size + (is_rehashing_ ? table_new_.size : 0);
}

size_t HashTable::capacity() const {
    return is_rehashing_ ? table_new_.capacity : table_old_.capacity;
}

double HashTable::load_factor() const {
    size_t cap = capacity();
    if (cap == 0) {
        return 0.0;
    }
    return static_cast<double>(size()) / static_cast<double>(cap);
}

bool HashTable::start_incremental_rehash(size_t new_capacity) {
    if (is_rehashing_) {
        return false;
    }

    new_capacity = round_up_power_of_two(new_capacity);
    if (new_capacity <= table_old_.capacity) {
        return false;
    }

    if (table_old_.size == 0) {
        table_old_.buckets.assign(new_capacity, nullptr);
        table_old_.capacity = new_capacity;
        return true;
    }

    table_new_.buckets.assign(new_capacity, nullptr);
    table_new_.capacity = new_capacity;
    table_new_.size = 0;
    is_rehashing_ = true;
    rehash_index_ = 0;
    return true;
}

bool HashTable::rehash_step(size_t max_buckets, size_t max_empty) {
    if (!is_rehashing_) {
        return false;
    }

    size_t empty_visited = 0;
    size_t buckets_migrated = 0;

    while (rehash_index_ < table_old_.capacity && buckets_migrated < max_buckets) {
        if (table_old_.buckets[rehash_index_] == nullptr) {
            empty_visited++;
            rehash_index_++;
            if (empty_visited >= max_empty) {
                break;
            }
            continue;
        }

        // Migrate non-empty bucket chain
        HashNode* current = table_old_.buckets[rehash_index_];
        while (current != nullptr) {
            HashNode* next_node = current->next;
            size_t new_index = current->hash_code & (table_new_.capacity - 1);

            current->next = table_new_.buckets[new_index];
            table_new_.buckets[new_index] = current;

            table_old_.size--;
            table_new_.size++;
            current = next_node;
        }

        table_old_.buckets[rehash_index_] = nullptr;
        rehash_index_++;
        buckets_migrated++;
    }

    if (rehash_index_ >= table_old_.capacity || table_old_.size == 0) {
        finish_rehash();
        return false;
    }

    return true;
}

bool HashTable::set(const std::string& key, const std::string& value) {
    if (is_rehashing_) {
        rehash_step(migration_batch_size_, max_empty_buckets_to_visit_);
    }

    uint64_t hash = util::fnv1a_hash(key);

    if (is_rehashing_) {
        // 1. Search table_new_
        size_t new_index = hash & (table_new_.capacity - 1);
        HashNode* cur = table_new_.buckets[new_index];
        while (cur != nullptr) {
            if (cur->hash_code == hash && cur->key == key) {
                cur->value = value;
                return false;  // Updated existing key in table_new_
            }
            cur = cur->next;
        }

        // 2. Search table_old_
        size_t old_index = hash & (table_old_.capacity - 1);
        HashNode* prev = nullptr;
        cur = table_old_.buckets[old_index];
        bool existed_in_old = false;
        while (cur != nullptr) {
            if (cur->hash_code == hash && cur->key == key) {
                if (prev != nullptr) {
                    prev->next = cur->next;
                } else {
                    table_old_.buckets[old_index] = cur->next;
                }
                // Rewire directly into table_new_ without reallocating
                cur->value = value;
                cur->next = table_new_.buckets[new_index];
                table_new_.buckets[new_index] = cur;
                table_old_.size--;
                table_new_.size++;
                existed_in_old = true;
                break;
            }
            prev = cur;
            cur = cur->next;
        }

        if (existed_in_old) {
            if (table_old_.size == 0) {
                finish_rehash();
            }
            return false;
        }

        // 3. Key does not exist in either table: insert fresh node into table_new_
        table_new_.buckets[new_index] =
            new HashNode(key, value, hash, table_new_.buckets[new_index]);
        table_new_.size++;
        return true;
    }

    // Single-table mode (not rehashing)
    size_t index = hash & (table_old_.capacity - 1);
    HashNode* cur = table_old_.buckets[index];
    while (cur != nullptr) {
        if (cur->hash_code == hash && cur->key == key) {
            cur->value = value;
            return false;
        }
        cur = cur->next;
    }

    table_old_.buckets[index] = new HashNode(key, value, hash, table_old_.buckets[index]);
    table_old_.size++;

    if (auto_resize_enabled_ && load_factor() > max_load_factor_) {
        if (incremental_rehash_enabled_) {
            start_incremental_rehash(table_old_.capacity * 2);
        } else {
            resize_stop_the_world(table_old_.capacity * 2);
        }
    }
    return true;
}

bool HashTable::get(const std::string& key, std::string& value) {
    if (is_rehashing_) {
        rehash_step(migration_batch_size_, max_empty_buckets_to_visit_);
    }
    return static_cast<const HashTable*>(this)->get(key, value);
}

bool HashTable::get(const std::string& key, std::string& value) const {
    uint64_t hash = util::fnv1a_hash(key);

    if (is_rehashing_) {
        size_t new_index = hash & (table_new_.capacity - 1);
        HashNode* cur = table_new_.buckets[new_index];
        while (cur != nullptr) {
            if (cur->hash_code == hash && cur->key == key) {
                value = cur->value;
                return true;
            }
            cur = cur->next;
        }
    }

    if (table_old_.capacity > 0) {
        size_t old_index = hash & (table_old_.capacity - 1);
        HashNode* cur = table_old_.buckets[old_index];
        while (cur != nullptr) {
            if (cur->hash_code == hash && cur->key == key) {
                value = cur->value;
                return true;
            }
            cur = cur->next;
        }
    }

    return false;
}

bool HashTable::contains(const std::string& key) {
    std::string dummy;
    return get(key, dummy);
}

bool HashTable::contains(const std::string& key) const {
    std::string dummy;
    return get(key, dummy);
}

bool HashTable::del(const std::string& key) {
    if (is_rehashing_) {
        rehash_step(migration_batch_size_, max_empty_buckets_to_visit_);
    }

    uint64_t hash = util::fnv1a_hash(key);

    if (is_rehashing_) {
        // Check table_new_ first
        size_t new_index = hash & (table_new_.capacity - 1);
        HashNode* cur = table_new_.buckets[new_index];
        HashNode* prev = nullptr;
        while (cur != nullptr) {
            if (cur->hash_code == hash && cur->key == key) {
                if (prev != nullptr) {
                    prev->next = cur->next;
                } else {
                    table_new_.buckets[new_index] = cur->next;
                }
                delete cur;
                table_new_.size--;
                if (table_old_.size == 0) {
                    finish_rehash();
                }
                return true;
            }
            prev = cur;
            cur = cur->next;
        }

        // Check table_old_
        size_t old_index = hash & (table_old_.capacity - 1);
        cur = table_old_.buckets[old_index];
        prev = nullptr;
        while (cur != nullptr) {
            if (cur->hash_code == hash && cur->key == key) {
                if (prev != nullptr) {
                    prev->next = cur->next;
                } else {
                    table_old_.buckets[old_index] = cur->next;
                }
                delete cur;
                table_old_.size--;
                if (table_old_.size == 0) {
                    finish_rehash();
                }
                return true;
            }
            prev = cur;
            cur = cur->next;
        }
        return false;
    }

    if (table_old_.capacity == 0 || table_old_.size == 0) {
        return false;
    }

    size_t index = hash & (table_old_.capacity - 1);
    HashNode* cur = table_old_.buckets[index];
    HashNode* prev = nullptr;

    while (cur != nullptr) {
        if (cur->hash_code == hash && cur->key == key) {
            if (prev != nullptr) {
                prev->next = cur->next;
            } else {
                table_old_.buckets[index] = cur->next;
            }
            delete cur;
            table_old_.size--;
            return true;
        }
        prev = cur;
        cur = cur->next;
    }
    return false;
}

void HashTable::resize_stop_the_world(size_t new_capacity) {
    new_capacity = round_up_power_of_two(new_capacity);
    if (new_capacity <= capacity()) {
        return;
    }

    std::vector<HashNode*> new_buckets(new_capacity, nullptr);
    size_t total_migrated = 0;

    for (size_t i = 0; i < table_old_.capacity; ++i) {
        HashNode* current = table_old_.buckets[i];
        while (current != nullptr) {
            HashNode* next_node = current->next;
            size_t new_index = current->hash_code & (new_capacity - 1);

            current->next = new_buckets[new_index];
            new_buckets[new_index] = current;
            total_migrated++;
            current = next_node;
        }
        table_old_.buckets[i] = nullptr;
    }

    if (is_rehashing_) {
        for (size_t i = 0; i < table_new_.capacity; ++i) {
            HashNode* current = table_new_.buckets[i];
            while (current != nullptr) {
                HashNode* next_node = current->next;
                size_t new_index = current->hash_code & (new_capacity - 1);

                current->next = new_buckets[new_index];
                new_buckets[new_index] = current;
                total_migrated++;
                current = next_node;
            }
            table_new_.buckets[i] = nullptr;
        }
    }

    table_old_.buckets = std::move(new_buckets);
    table_old_.capacity = new_capacity;
    table_old_.size = total_migrated;

    table_new_ = Table{};
    is_rehashing_ = false;
    rehash_index_ = 0;
}

}  // namespace store
}  // namespace redis_clone
