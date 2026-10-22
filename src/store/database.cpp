#include "store/database.h"

#include <utility>

namespace redis_clone {
namespace store {

uint64_t Database::system_now_ms() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

Database::Database(size_t initial_capacity, double max_load_factor)
    : hash_table_(initial_capacity, max_load_factor), ttl_heap_(), clock_(nullptr) {}

bool Database::set(const std::string& key, const std::string& value, uint64_t expire_at_ms) {
    bool newly_inserted = hash_table_.set(key, value);
    HashNode* node = hash_table_.lookup_node(key);
    if (node != nullptr) {
        node->expiry_version++;
        node->expire_at_ms = expire_at_ms;
        if (expire_at_ms > 0) {
            ttl_heap_.push(key, expire_at_ms, node->expiry_version);
        }
    }
    return newly_inserted;
}

bool Database::get(const std::string& key, std::string& value) {
    uint64_t now = now_ms();
    HashNode* node = hash_table_.lookup_node(key);
    if (node == nullptr) {
        return false;
    }
    if (node->expire_at_ms > 0 && now >= node->expire_at_ms) {
        hash_table_.del(key);  // Lazy expiry eviction
        return false;
    }
    return hash_table_.get(key, value);
}

bool Database::del(const std::string& key) {
    return hash_table_.del(key);
}

bool Database::contains(const std::string& key) {
    std::string dummy;
    return get(key, dummy);
}

bool Database::expire(const std::string& key, uint64_t seconds) {
    return pexpire(key, seconds * 1000);
}

bool Database::pexpire(const std::string& key, uint64_t milliseconds) {
    uint64_t now = now_ms();
    HashNode* node = hash_table_.lookup_node(key);
    if (node == nullptr) {
        return false;
    }
    if (node->expire_at_ms > 0 && now >= node->expire_at_ms) {
        hash_table_.del(key);
        return false;
    }

    uint64_t expire_at_ms = now + milliseconds;
    node->expiry_version++;
    node->expire_at_ms = expire_at_ms;
    ttl_heap_.push(key, expire_at_ms, node->expiry_version);
    return true;
}

int64_t Database::ttl(const std::string& key) {
    int64_t p = pttl(key);
    if (p < 0) {
        return p;
    }
    return (p + 999) / 1000;
}

int64_t Database::pttl(const std::string& key) {
    uint64_t now = now_ms();
    HashNode* node = hash_table_.lookup_node(key);
    if (node == nullptr) {
        return -2;
    }
    if (node->expire_at_ms == 0) {
        return -1;
    }
    if (now >= node->expire_at_ms) {
        hash_table_.del(key);  // Lazy expiry
        return -2;
    }
    return static_cast<int64_t>(node->expire_at_ms - now);
}

size_t Database::active_expire_step(size_t max_keys) {
    uint64_t now = now_ms();
    size_t evicted = 0;

    while (!ttl_heap_.empty() && evicted < max_keys) {
        TtlEntry entry;
        if (!ttl_heap_.peek_min(entry)) {
            break;
        }
        if (entry.expire_at_ms > now) {
            break;  // Earliest entry is still in the future
        }
        ttl_heap_.pop_min(entry);

        HashNode* node = hash_table_.lookup_node(entry.key);
        if (node == nullptr) {
            continue;  // Key deleted before expiry
        }
        if (node->expiry_version != entry.version) {
            continue;  // Overwritten or updated with new TTL
        }
        if (node->expire_at_ms == 0 || node->expire_at_ms > now) {
            continue;  // TTL was cleared or extended
        }

        hash_table_.del(entry.key);
        evicted++;
    }
    return evicted;
}

bool Database::rehash_step(size_t max_buckets, size_t max_empty) {
    return hash_table_.rehash_step(max_buckets, max_empty);
}

void Database::step() {
    if (hash_table_.is_rehashing()) {
        hash_table_.rehash_step(8, 80);
    }
    active_expire_step(100);
}

void Database::clear() {
    hash_table_.clear();
    ttl_heap_.clear();
}

size_t Database::size() {
    active_expire_step(100);
    return hash_table_.size();
}

}  // namespace store
}  // namespace redis_clone
