#pragma once
#include "cutoutmips.hpp"
#include <list>
#include <iterator>

namespace albion::cutoutmips {
// Accessed by the serial detail worker only. Returned immutable chains may be
// held by an upload/retirement payload after their cache entry is evicted.
class Cache {
public:
    struct Stats { size_t hits=0, builds=0, bytes=0, entries=0; };
    explicit Cache(size_t budget=32u*1024u*1024u, size_t maxEntries=256)
        : budget_(budget), maxEntries_(maxEntries) {}
    Cache(const Cache&)=delete;
    Cache& operator=(const Cache&)=delete;
    std::shared_ptr<const Chain> acquire(const terrainexport::Image& image, bool reuse=true) {
        if (!reuse) clear();
        if (!TexturePool<int>::valid(image)) return std::make_shared<const Chain>();
        const Key key{image.name,image.width,image.height};
        auto found=index_.find(key);
        if (found!=index_.end()) {
            auto entry=found->second;
            if (entry->pixels==image.rgba) {
                entries_.splice(entries_.end(),entries_,entry);
                ++hits_;
                return entry->chain;
            }
            // A replacement may reuse the same name and dimensions.
            erase(entry);
        }
        ++builds_;
        auto chain=std::make_shared<const Chain>(build(image));
        size_t bytes=image.rgba.size();
        for (const auto& level:*chain) bytes+=level.rgba.size();
        if (reuse && maxEntries_ && bytes<=budget_) {
            while (!entries_.empty() && (bytes_>budget_-bytes || entries_.size()>=maxEntries_))
                erase(entries_.begin());
            entries_.push_back({key,image.rgba,chain,bytes});
            try { index_.emplace(key,std::prev(entries_.end())); }
            catch (...) { entries_.pop_back(); throw; }
            bytes_+=bytes;
        }
        return chain;
    }
    Stats stats() const { return {hits_,builds_,bytes_,entries_.size()}; }
    void clear() { index_.clear(); entries_.clear(); bytes_=0; }
private:
    using Key=std::tuple<std::string,uint32_t,uint32_t>;
    struct Entry { Key key; std::vector<uint8_t> pixels; std::shared_ptr<const Chain> chain; size_t bytes; };
    using Entries=std::list<Entry>;
    void erase(Entries::iterator entry) {
        bytes_-=entry->bytes; index_.erase(entry->key); entries_.erase(entry);
    }
    size_t budget_,maxEntries_,bytes_=0,hits_=0,builds_=0;
    Entries entries_;
    std::map<Key,Entries::iterator> index_;
};
} // namespace albion::cutoutmips
