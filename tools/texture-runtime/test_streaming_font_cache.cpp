#include "streaming_font_cache.hpp"
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <vector>

static void require(bool ok) { if (!ok) throw std::runtime_error("font cache placement failed"); }
int main() {
    unsigned cases = 0;
    for (unsigned scale : {1u,2u}) for (bool large : {false,true}) {
        const unsigned tile = 32 * scale, pitch = tile * (large ? 2 : 1);
        std::vector<std::uint8_t> alpha(pitch*pitch), cache(512*tile*tile), dirty(512);
        for (unsigned y=0;y<pitch;++y) for (unsigned x=0;x<pitch;++x) alpha[y*pitch+x] = std::uint8_t(y*13+x*37);
        for (unsigned slot=0;slot<(large?128u:512u);++slot) {
            std::fill(cache.begin(),cache.end(),0x5a); std::fill(dirty.begin(),dirty.end(),0);
            require(texture_font::scatterGlyph(alpha,large,slot,scale,cache,dirty));
            // Independently interpret the CPU cache as the native uploader's
            // 32-column atlas, then compare the entire atlas against one glyph.
            const unsigned ox = (slot % (1024*scale/pitch))*pitch;
            const unsigned oy = (slot / (1024*scale/pitch))*pitch;
            for (unsigned i=0;i<512;++i) {
                const unsigned tx=(i%32)*tile,ty=(i/32)*tile;
                const bool touched=tx>=ox && tx<ox+pitch && ty>=oy && ty<oy+pitch;
                require(dirty[i]==unsigned(touched));
                for(unsigned y=0;y<tile;++y) for(unsigned x=0;x<tile;++x)
                    require(cache[i*tile*tile+y*tile+x]==(touched?alpha[(ty+y-oy)*pitch+tx+x-ox]:0x5a));
            }
            ++cases;
        }
        auto before=cache;auto flags=dirty;
        require(!texture_font::scatterGlyph(alpha,large,large?128:512,scale,cache,dirty));
        require(!texture_font::scatterGlyph(alpha,large,0,3,cache,dirty));
        require(!texture_font::scatterGlyph(std::span<const std::uint8_t>(cache.data(),alpha.size()),large,0,scale,cache,dirty));
        require(!texture_font::scatterGlyph(alpha,large,0,scale,std::span<std::uint8_t>(cache.data(),cache.size()-1),dirty));
        require(cache==before && dirty==flags);
    }
    {
        std::vector<std::uint8_t> cache(512*64*64,0x5a),dirty(512),planes(128*128,0x85);
        std::array<std::uint8_t,16> entry{};
        entry[4]=1;entry[7]=3;entry[10]=11;entry[11]=33;entry[15]=0x80;
        texture_font::GlyphRecord record{};
        std::copy(entry.begin()+8,entry.end(),record.originalMetric.begin());
        record.pitch=128;record.present=true;
        const auto records=std::span<const texture_font::GlyphRecord>(&record,1);
        require(texture_font::stageCachedGlyph(entry,records,planes,127,cache,dirty));
        require(dirty[478] && dirty[479] && dirty[510] && dirty[511]);
        require(std::count(dirty.begin(),dirty.end(),1)==4);
        auto before=cache;auto flags=dirty;
        entry[4]=0;require(!texture_font::stageCachedGlyph(entry,records,planes,127,cache,dirty));entry[4]=2;
        require(!texture_font::stageCachedGlyph(entry,records,planes,127,cache,dirty));entry[4]=1;
        entry[8]=1;require(!texture_font::stageCachedGlyph(entry,records,planes,127,cache,dirty));entry[8]=0;
        record.offset=1;require(!texture_font::stageCachedGlyph(entry,records,planes,127,cache,dirty));record.offset=0;
        record.pitch=64;require(!texture_font::stageCachedGlyph(entry,records,planes,127,cache,dirty));record.pitch=128;
        record.present=false;require(!texture_font::stageCachedGlyph(entry,records,planes,127,cache,dirty));
        require(cache==before && dirty==flags);
        auto failedRead=[](std::uint32_t,std::span<std::uint8_t> bytes){bytes[0]=99;return false;};
        require(!texture_font::stageOriginalGlyph(entry,127,failedRead,cache,dirty));
        require(cache==before && dirty==flags);
        // A true empty sentinel clears the whole cell, including stale alpha.
        entry[12]=0xff;entry[13]=0xff;entry[14]=0x3f;
        std::copy(entry.begin()+8,entry.end(),record.originalMetric.begin());
        require(texture_font::stageCachedGlyph(entry,records,{},127,cache,dirty));
        bool called=false;
        require(texture_font::stageOriginalGlyph(entry,127,[&](auto,auto){called=true;return false;},cache,dirty));
        require(!called);
        for (unsigned tile : {478u,479u,510u,511u})
            require(std::all_of(cache.begin()+tile*4096,cache.begin()+(tile+1)*4096,[](auto x){return x==0;}));
    }
    {
        using texture_font::GlyphSource;
        std::vector<std::uint8_t> cache(512*4096,0x5a),dirty(512),planes(4096,0xaa);
        std::array<std::uint8_t,16> entry{};entry[4]=1;entry[10]=2;entry[11]=2;
        texture_font::GlyphRecord record{};record.pitch=64;record.present=true;
        std::copy(entry.begin()+8,entry.end(),record.originalMetric.begin());
        const auto records=std::span<const texture_font::GlyphRecord>(&record,1);
        unsigned reads=0;
        auto read=[&](std::uint32_t offset,std::span<std::uint8_t> dest) {
            require(offset==0 && dest.size()==1024);++reads;
            std::fill(dest.begin(),dest.end(),0x55);return true;
        };
        require(texture_font::dispatchGlyph(true,entry,records,planes,0,read,cache,dirty)==GlyphSource::prepared);
        require(reads==0 && cache[0]==0xaa);
        // Same ID and all eight metric bytes, but a different live bank.
        require(texture_font::dispatchGlyph(false,entry,records,planes,0,read,cache,dirty)==GlyphSource::original);
        require(reads==1 && cache[0]==0x55);
        // Changed metric in a recognized bank must also use its original pixels.
        entry[8]=1;
        require(texture_font::dispatchGlyph(true,entry,records,planes,0,read,cache,dirty)==GlyphSource::original);
        require(reads==2);
        auto before=cache;auto flags=dirty;
        require(texture_font::dispatchGlyph(false,entry,records,planes,0,[](auto,auto){return false;},cache,dirty)==GlyphSource::failed);
        require(cache==before && dirty==flags);
    }
    std::printf("PASS %u font cache slots, alpha placement, bank-aware dispatch and failed-read preservation\n",cases);
}
