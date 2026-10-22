#include "store/ttl_heap.h"

#include <utility>

namespace redis_clone {
namespace store {

void TtlHeap::push(std::string key, uint64_t expire_at_ms, uint64_t version) {
    heap_.emplace_back(expire_at_ms, std::move(key), version);
    sift_up(heap_.size() - 1);
}

bool TtlHeap::peek_min(TtlEntry& entry) const {
    if (heap_.empty()) {
        return false;
    }
    entry = heap_[0];
    return true;
}

bool TtlHeap::pop_min(TtlEntry& entry) {
    if (heap_.empty()) {
        return false;
    }
    entry = std::move(heap_[0]);
    if (heap_.size() > 1) {
        heap_[0] = std::move(heap_.back());
        heap_.pop_back();
        sift_down(0);
    } else {
        heap_.pop_back();
    }
    return true;
}

void TtlHeap::sift_up(size_t index) {
    while (index > 0) {
        size_t parent = (index - 1) / 2;
        if (heap_[index].expire_at_ms < heap_[parent].expire_at_ms) {
            std::swap(heap_[index], heap_[parent]);
            index = parent;
        } else {
            break;
        }
    }
}

void TtlHeap::sift_down(size_t index) {
    size_t n = heap_.size();
    while (true) {
        size_t smallest = index;
        size_t left = 2 * index + 1;
        size_t right = 2 * index + 2;

        if (left < n && heap_[left].expire_at_ms < heap_[smallest].expire_at_ms) {
            smallest = left;
        }
        if (right < n && heap_[right].expire_at_ms < heap_[smallest].expire_at_ms) {
            smallest = right;
        }
        if (smallest != index) {
            std::swap(heap_[index], heap_[smallest]);
            index = smallest;
        } else {
            break;
        }
    }
}

}  // namespace store
}  // namespace redis_clone
