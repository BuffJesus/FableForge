#pragma once
#include "terrainexport.hpp"
#include <map>
#include <memory>
#include <tuple>
#include <algorithm>

namespace albion {
inline size_t texturePayloadBytes(uint32_t width, uint32_t height, bool mipmaps) {
    if (!width || !height) return 0;
    size_t bytes = 0;
    do {
        bytes += size_t(width) * height * 4;
        if (!mipmaps || (width == 1 && height == 1)) break;
        width = std::max(1u, width / 2); height = std::max(1u, height / 2);
    } while (true);
    return bytes;
}
// Render-thread pool. Weak entries never keep an unused GPU allocation alive.
// Compare pixels as well as the name: replacement assets may reuse an identity.
template<class Resource> class TexturePool {
public:
    struct Entry {
        std::vector<uint8_t> pixels;
        std::shared_ptr<Resource> resource;
        size_t bytes = 0;
    };
    struct Stats { size_t allocations = 0, gpuBytes = 0, identityBytes = 0, hits = 0, mipmapped = 0, cutout = 0; };
    static bool valid(const terrainexport::Image& image) {
        return image.width && image.height && image.width <= SIZE_MAX / 4 / image.height &&
            image.rgba.size() == size_t(image.width) * image.height * 4;
    }
    template<class Factory>
    std::shared_ptr<Entry> acquire(const terrainexport::Image& image, int policy, size_t bytes,
                                   bool reuse, Factory create) {
        if (!valid(image)) return {};
        if (++requests_ % 64 == 0) sweep();
        auto& bucket = entries_[{image.name, image.width, image.height, policy}];
        for (auto it = bucket.begin(); it != bucket.end();) {
            if (auto entry = it->lock()) {
                if (reuse && entry->pixels == image.rgba) { ++hits_; return entry; }
                ++it;
            } else it = bucket.erase(it);
        }
        auto resource = create();
        if (!resource) return {};
        auto entry = std::make_shared<Entry>();
        entry->pixels = image.rgba; entry->resource = std::move(resource); entry->bytes = bytes;
        bucket.push_back(entry);
        return entry;
    }
    Stats stats() const {
        Stats result; result.hits = hits_;
        for (const auto& [key, bucket] : entries_) for (const auto& weak : bucket)
            if (auto entry = weak.lock()) {
                ++result.allocations; result.gpuBytes += entry->bytes;
                result.mipmapped += std::get<3>(key) != 0;
                result.cutout += std::get<3>(key) == 2;
                result.identityBytes += entry->pixels.size();
            }
        return result;
    }
    void sweep() {
        for (auto it = entries_.begin(); it != entries_.end();) {
            auto& bucket = it->second;
            std::erase_if(bucket, [](const auto& weak) { return weak.expired(); });
            if (bucket.empty()) it = entries_.erase(it); else ++it;
        }
    }
private:
    using Key = std::tuple<std::string, uint32_t, uint32_t, int>;
    std::map<Key, std::vector<std::weak_ptr<Entry>>> entries_;
    size_t requests_ = 0, hits_ = 0;
};
} // namespace albion
