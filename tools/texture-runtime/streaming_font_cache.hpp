#pragma once
// Experimental alpha-cache placement only; not installed in a running game.
// Retail upload 0x9a23e0 consumes 512 tile-contiguous cells in a 32x16 grid.
#include <cstdint>
#include <cstring>
#include <span>
#include <array>

namespace texture_font {
struct GlyphRecord {
    std::array<std::uint8_t,8> originalMetric;
    std::uint32_t offset;
    std::uint16_t pitch;
    bool present;
};
inline bool scatterGlyph(std::span<const std::uint8_t> alpha,
                         bool large, unsigned slot, unsigned scale,
                         std::span<std::uint8_t> cache,
                         std::span<std::uint8_t> dirty) {
    if ((scale != 1 && scale != 2) || slot >= (large ? 128u : 512u)) return false;
    const unsigned tile = 32 * scale, cells = large ? 2 : 1;
    const unsigned pitch = tile * cells, columns = 32 / cells;
    if (alpha.size() != pitch * pitch || cache.size() != 512 * tile * tile || dirty.size() != 512) return false;
    const auto overlaps = [](const auto& a, const auto& b) {
        const auto x = reinterpret_cast<std::uintptr_t>(a.data());
        const auto y = reinterpret_cast<std::uintptr_t>(b.data());
        return x <= y ? y - x < a.size() : x - y < b.size();
    };
    if (overlaps(alpha,cache) || overlaps(alpha,dirty) || overlaps(cache,dirty)) return false;
    const unsigned column = (slot % columns) * cells, row = (slot / columns) * cells;
    for (unsigned cy = 0; cy < cells; ++cy) {
        for (unsigned cx = 0; cx < cells; ++cx) {
            const unsigned index = (row + cy) * 32 + column + cx;
            auto* dst = cache.data() + index * tile * tile;
            for (unsigned y = 0; y < tile; ++y)
                std::memcpy(dst + y * tile, alpha.data() + (cy * tile + y) * pitch + cx * tile, tile);
            dirty[index] = 1;
        }
    }
    return true;
}

// The cached retail entry contains the packed character reference at +4 and
// the original eight metric bytes at +8. Bank identity must also be pinned by
// the caller before it enables an override; entry IDs alone are not sufficient.
inline bool stageCachedGlyph(std::span<const std::uint8_t> entry,
                             std::span<const GlyphRecord> records,
                             std::span<const std::uint8_t> planes, unsigned slot,
                             std::span<std::uint8_t> cache,
                             std::span<std::uint8_t> dirty) {
    if (entry.size()!=16) return false;
    const auto index=std::uint32_t(entry[4])|(std::uint32_t(entry[5])<<8)|(std::uint32_t(entry[6])<<16);
    if (index==0 || index>records.size()) return false;
    const auto& record=records[index-1];
    if (std::memcmp(entry.data()+8,record.originalMetric.data(),8)!=0) return false;
    const bool large=(entry[15]&0x80)!=0;
    const unsigned pitch=large?128:64;
    if (record.pitch!=pitch) return false;
    const auto originalOffset=std::uint32_t(entry[12])|(std::uint32_t(entry[13])<<8)|((std::uint32_t(entry[14])&0x3f)<<16);
    const bool present=entry[10]!=0 && entry[11]!=0 && originalOffset!=0x3fffff;
    if (record.present!=present || entry[10]>pitch/2 || entry[11]>pitch/2) return false;
    if (!record.present) {
        // Spaces/missing glyphs retain identity and layout but must not leave
        // another glyph's cached alpha visible in their allocated slot.
        const std::array<std::uint8_t,128*128> empty{};
        return scatterGlyph(std::span<const std::uint8_t>(empty.data(),pitch*pitch),large,slot,2,cache,dirty);
    }
    const auto size=std::size_t(pitch)*pitch;
    if (record.offset>planes.size() || size>planes.size()-record.offset) return false;
    return scatterGlyph(planes.subspan(record.offset,size),large,slot,2,cache,dirty);
}

// Enlarged-cache fallback. Reader must perform a bounded synchronous read from
// the CURRENT bank's GlyphData pixel region, returning false on a short read.
// This does not invoke the old native cache writer or alter logical metrics.
template<class Reader>
bool stageOriginalGlyph(std::span<const std::uint8_t> entry,unsigned slot,
                        Reader&& reader,std::span<std::uint8_t> cache,
                        std::span<std::uint8_t> dirty) {
    if(entry.size()!=16) return false;
    const bool large=(entry[15]&0x80)!=0;
    const unsigned nativePitch=large?64:32,pitch=nativePitch*2;
    if(slot>=(large?128u:512u) || cache.size()!=512*4096 || dirty.size()!=512 ||
       entry[10]>nativePitch || entry[11]>nativePitch) return false;
    const auto offset=std::uint32_t(entry[12])|(std::uint32_t(entry[13])<<8)|((std::uint32_t(entry[14])&0x3f)<<16);
    std::array<std::uint8_t,128*128> doubled{};
    if(entry[10] && entry[11] && offset!=0x3fffff) {
        const std::uint64_t start=std::uint64_t(offset)*1024,size=nativePitch*nativePitch;
        if(start+size>0x100000000ull) return false;
        std::array<std::uint8_t,64*64> original{};
        if(!reader(std::uint32_t(start),std::span<std::uint8_t>(original.data(),std::size_t(size)))) return false;
        for(unsigned y=0;y<pitch;++y) for(unsigned x=0;x<pitch;++x)
            doubled[y*pitch+x]=original[(y/2)*nativePitch+x/2];
    }
    return scatterGlyph(std::span<const std::uint8_t>(doubled.data(),pitch*pitch),large,slot,2,cache,dirty);
}

enum class GlyphSource { prepared, original, failed };
// bankMatches must represent a verified live bank identity, not just its name
// or matching metric numbers. Reader remains scoped to the actual current bank.
template<class Reader>
GlyphSource dispatchGlyph(bool bankMatches,std::span<const std::uint8_t> entry,
                          std::span<const GlyphRecord> records,
                          std::span<const std::uint8_t> planes,unsigned slot,
                          Reader&& reader,std::span<std::uint8_t> cache,
                          std::span<std::uint8_t> dirty) {
    if(bankMatches && stageCachedGlyph(entry,records,planes,slot,cache,dirty)) return GlyphSource::prepared;
    if(stageOriginalGlyph(entry,slot,reader,cache,dirty)) return GlyphSource::original;
    return GlyphSource::failed;
}
}
