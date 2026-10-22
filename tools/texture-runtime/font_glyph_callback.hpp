#pragma once
#include "streaming_font_cache.hpp"

namespace texture_font {
// Adapter for the four arguments captured at retail 9A21BE. Cache/dirty spans
// must already be validated writable by the hook owner. This returns a source
// enum, never permission to replay the stock writer in an enlarged cache.
template<class Runtime,class Reader>
GlyphSource stageNativeGlyph(const Runtime& runtime,Reader&& read,std::uint32_t font,
                             std::uint32_t glyph,std::uint32_t metadata,unsigned slot,
                             std::span<std::uint8_t> cache,std::span<std::uint8_t> dirty,
                             std::uint8_t& globalDirty) {
    if(metadata!=0x13c5a98 && metadata!=0x13c7ac8) return GlyphSource::failed;
    if(glyph<0x10000 || std::uint64_t(glyph)+16>0x100000000ull) return GlyphSource::failed;
    std::array<std::uint8_t,16> entry{};
    std::array<std::uint32_t,6> layout{};
    if(!read(glyph,entry.data(),entry.size()) || !read(metadata,layout.data(),sizeof(layout))) return GlyphSource::failed;
    const bool large=(entry[15]&0x80)!=0;
    const std::array<std::uint32_t,6> expected=large ?
        std::array<std::uint32_t,6>{16,8,128,64,64,4096} :
        std::array<std::uint32_t,6>{32,16,512,32,32,1024};
    if(metadata!=(large?0x13c7ac8u:0x13c5a98u) || layout!=expected || slot>=expected[2] ||
       cache.size()!=512*4096 || dirty.size()!=512) return GlyphSource::failed;
    const auto source=runtime.stage(read,font,entry,slot,cache,dirty);
    if(source!=GlyphSource::failed) globalDirty=1;
    return source;
}
}
