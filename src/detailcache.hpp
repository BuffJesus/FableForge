#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <vector>

namespace albion::worldview {

// Heuristic allowance for inactive resources, not a cap on total GPU usage.
// Sample once per second: reduce immediately, restore only after sustained room.
struct CacheMemoryBudget {
    static constexpr uint64_t MiB = 1024u * 1024u;
    static constexpr size_t ceiling = 128u * MiB;
    size_t limit = ceiling;
    unsigned healthySamples = 0;
    size_t observe(bool valid, uint64_t budget, uint64_t usage, uint64_t inactiveBytes) {
        if (!valid) { healthySamples = 0; return limit; }
        const uint64_t nonCache = usage - std::min(usage, inactiveBytes);
        const uint64_t reserve = std::min(budget / 2, std::max(64 * MiB, budget / 10));
        const uint64_t available = budget - std::min(budget, nonCache);
        const size_t desired = size_t(std::min({uint64_t(ceiling), budget / 16,
            available - std::min(available, reserve)}));
        if (desired <= limit) { limit = desired; healthySamples = 0; }
        else if (++healthySamples >= 5) { limit = std::min(desired, limit + size_t(16 * MiB)); healthySamples = 0; }
        return limit;
    }
};

// Inactive GPU maps only. The renderer owns resources and releases every name
// returned by retain() or setBudget(). Active maps never compete for this allowance.
class DetailCache {
public:
    explicit DetailCache(size_t budget = 128u * 1024u * 1024u, size_t maps = 6)
        : budget_(budget), maps_(maps) {}
    bool take(const std::string& name) {
        for (auto it = entries_.begin(); it != entries_.end(); ++it) if (it->name == name) {
            bytes_ -= it->bytes;
            entries_.erase(it);
            return true;
        }
        return false;
    }
    std::vector<std::string> retain(const std::string& name, size_t bytes) {
        take(name);
        std::vector<std::string> evicted;
        if (!maps_ || !bytes || bytes > budget_) { evicted.push_back(name); return evicted; }
        while (!entries_.empty() && (bytes_ > budget_ - bytes || entries_.size() >= maps_)) {
            evicted.push_back(entries_.front().name);
            bytes_ -= entries_.front().bytes;
            entries_.pop_front();
        }
        entries_.push_back({name, bytes});
        bytes_ += bytes;
        return evicted;
    }
    void clear() { entries_.clear(); bytes_ = 0; }
    std::vector<std::string> setBudget(size_t budget) {
        budget_ = budget;
        std::vector<std::string> evicted;
        while (!entries_.empty() && bytes_ > budget_) {
            evicted.push_back(entries_.front().name);
            bytes_ -= entries_.front().bytes;
            entries_.pop_front();
        }
        return evicted;
    }
    std::vector<std::string> names() const {
        std::vector<std::string> result;
        for (const auto& entry : entries_) result.push_back(entry.name);
        return result;
    }
    size_t bytes() const { return bytes_; }
    size_t size() const { return entries_.size(); }
    size_t budget() const { return budget_; }
private:
    struct Entry { std::string name; size_t bytes; };
    std::list<Entry> entries_;
    size_t budget_, maps_, bytes_ = 0;
};

} // namespace albion::worldview
