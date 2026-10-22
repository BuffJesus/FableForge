#include "font_glyph_callback.hpp"
#include <vector>
#include <stdexcept>
#include <cstdio>
static void require(bool x){if(!x)throw std::runtime_error("native glyph callback mismatch");}
struct Runtime {
    mutable unsigned calls=0;
    texture_font::GlyphSource result=texture_font::GlyphSource::prepared;
    template<class Reader>
    texture_font::GlyphSource stage(Reader&,unsigned font,std::span<const std::uint8_t> entry,
                                   unsigned slot,std::span<std::uint8_t>,std::span<std::uint8_t>) const {
        require(font==0x20000000 && entry[4]==19 && slot<((entry[15]&128)?128u:512u));
        ++calls;return result;
    }
};
int main() {
    std::array<std::uint8_t,16> entry{};entry[4]=19;
    std::array<std::uint32_t,6> layout{};
    unsigned metadata=0;bool readable=true;
    auto read=[&](unsigned at,void* out,std::size_t size) {
        if(!readable)return false;
        if(at==0x20000100 && size==16){std::memcpy(out,entry.data(),16);return true;}
        if(at==metadata && size==24){std::memcpy(out,layout.data(),24);return true;}
        return false;
    };
    std::vector<std::uint8_t> cache(512*4096,0xa5),dirty(512,0x5a);
    const auto originalCache=cache,originalDirty=dirty;
    Runtime runtime;std::uint8_t global=0;
    auto call=[&](unsigned slot,unsigned meta,unsigned glyph=0x20000100) {
        return texture_font::stageNativeGlyph(runtime,read,0x20000000,glyph,meta,slot,cache,dirty,global);
    };
    unsigned cases=0;
    for(bool large:{false,true}) {
        entry[15]=large?128:0;metadata=large?0x13c7ac8:0x13c5a98;
        layout=large?std::array<std::uint32_t,6>{16,8,128,64,64,4096}:
                     std::array<std::uint32_t,6>{32,16,512,32,32,1024};
        for(unsigned slot=0;slot<layout[2];++slot) {
            global=0;require(call(slot,metadata)==texture_font::GlyphSource::prepared && global==1);++cases;
        }
        const auto calls=runtime.calls;
        global=0;
        require(call(layout[2],metadata)==texture_font::GlyphSource::failed);
        require(call(0,large?0x13c5a98:0x13c7ac8)==texture_font::GlyphSource::failed);
        for(auto& field:layout){field^=1;require(call(0,metadata)==texture_font::GlyphSource::failed);field^=1;}
        readable=false;require(call(0,metadata)==texture_font::GlyphSource::failed);readable=true;
        require(call(0,metadata,0xfffffff8)==texture_font::GlyphSource::failed);
        require(runtime.calls==calls && global==0);
        runtime.result=texture_font::GlyphSource::failed;
        require(call(0,metadata)==texture_font::GlyphSource::failed && global==0);
        runtime.result=texture_font::GlyphSource::original;
        require(call(0,metadata)==texture_font::GlyphSource::original && global==1);
        runtime.result=texture_font::GlyphSource::prepared;
    }
    require(cache==originalCache && dirty==originalDirty);
    std::printf("PASS %u native callback slots; metadata/class/range/read refusals and global-dirty success gating\n",cases);
}
