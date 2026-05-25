#pragma once

#include <cstddef>
#include <list>
#include <unordered_map>
#include <utility>

namespace SandBox {

template <typename Key, typename Value, typename Hash = std::hash<Key>>
class LruCache {
public:
    explicit LruCache(size_t capacity = 128)
        : capacity_(capacity)
    {
    }

    void setCapacity(size_t capacity)
    {
        capacity_ = capacity;
        trim();
    }

    [[nodiscard]] size_t size() const { return items_.size(); }
    [[nodiscard]] bool empty() const { return items_.empty(); }

    Value* find(const Key& key)
    {
        auto it = lookup_.find(key);
        if (it == lookup_.end()) {
            return nullptr;
        }

        touch(it->second);
        return &it->second->value;
    }

    const Value* find(const Key& key) const
    {
        auto it = lookup_.find(key);
        if (it == lookup_.end()) {
            return nullptr;
        }

        return &it->second->value;
    }

    void put(Key key, Value value)
    {
        auto it = lookup_.find(key);
        if (it != lookup_.end()) {
            it->second->value = std::move(value);
            touch(it->second);
            return;
        }

        items_.push_front(Entry { std::move(key), std::move(value) });
        lookup_[items_.front().key] = items_.begin();
        trim();
    }

    void touch(const Key& key)
    {
        auto it = lookup_.find(key);
        if (it != lookup_.end()) {
            touch(it->second);
        }
    }

private:
    struct Entry {
        Key key;
        Value value;
    };

    using ListIterator = typename std::list<Entry>::iterator;

    void touch(ListIterator it)
    {
        if (it != items_.begin()) {
            items_.splice(items_.begin(), items_, it);
        }
    }

    void trim()
    {
        while (items_.size() > capacity_) {
            lookup_.erase(items_.back().key);
            items_.pop_back();
        }
    }

    size_t capacity_ = 128;
    std::list<Entry> items_;
    std::unordered_map<Key, ListIterator, Hash> lookup_;
};

} // namespace SandBox
