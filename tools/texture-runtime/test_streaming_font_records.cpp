#include "generated_streaming_records.hpp"
#include "pinned_alpha.hpp"
#include "streaming_font_pixels.hpp"
#include "prepared_font_runtime.hpp"
#include "font_glyph_callback.hpp"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <vector>
#include <cstdio>
#include <stdexcept>
static void require(bool x) { if(!x) throw std::runtime_error("real streaming glyph staging mismatch"); }
int main(int argc,char** argv) {
    require(argc==4);
    std::size_t bytes=0;
    for(const auto& r:streamingGlyphRecords) if(r.present) bytes=std::max(bytes,std::size_t(r.offset)+r.pitch*r.pitch);
    std::vector<std::uint8_t> planes;
    require(texture_font::loadPinnedAlpha(argv[1],bytes,streamingAlphaSha256,planes)==texture_font::LoadError::none);
    std::vector<std::uint8_t> original;
    require(texture_font::loadPinnedAlpha(argv[2],std::filesystem::file_size(argv[2]),streamingOriginalSha256,original)==texture_font::LoadError::none);
    require(original.size()>=20);
    const unsigned dataStart=unsigned(original[8])|(unsigned(original[9])<<8)|(unsigned(original[10])<<16)|(unsigned(original[11])<<24);
    require(dataStart<=original.size());
    struct Handle {HANDLE value;~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
    Handle native{CreateFileW(std::filesystem::path(argv[3]).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr)};
    require(native.value!=INVALID_HANDLE_VALUE);
    std::array<std::uint8_t,0x4000> memory{};
    constexpr unsigned font=0x20000000,bank=0x20001000,disk=0x20003000,bankEntry=0x20002000;
    auto put=[&](unsigned at,unsigned value){std::memcpy(memory.data()+at-font,&value,4);};
    auto memoryRead=[&](unsigned at,void* out,std::size_t size){
        if((at==0x13c5a98 || at==0x13c7ac8) && size==24) {
            const std::array<unsigned,6> layout=at==0x13c5a98?
                std::array<unsigned,6>{32,16,512,32,32,1024}:
                std::array<unsigned,6>{16,8,128,64,64,4096};
            std::memcpy(out,layout.data(),24);return true;
        }
        if(at<font || std::uint64_t(at)+size>font+memory.size()) return false;
        std::memcpy(out,memory.data()+at-font,size);return true;
    };
    put(font+0x10,bank);put(bank,0x129d5cc);put(bank+0x188,bankEntry);
    put(bankEntry,0x129b880);put(bankEntry+4,bank);put(bankEntry+8,streamingEntryId);
    put(bank+0x7c,disk);put(disk,0x122d06c);put(disk+0x14,1);
    put(disk+0x10,static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(native.value)));
    texture_font::PreparedFontRuntime runtime;
    require(runtime.initialize(memoryRead,font,{bank,bankEntry,streamingEntryId,true},
        {argv[3],streamingArchiveBytes,streamingArchiveSha256},
        {argv[2],streamingOriginalBytes,streamingOriginalSha256},
        {argv[1],bytes,streamingAlphaSha256},streamingGlyphRecords));
    texture_font::VerifiedGlyphPixels pixels;
    require(pixels.load(argv[2],original.size(),streamingOriginalSha256));
    unsigned reads=0;
    auto read=[&](std::uint32_t at,std::span<std::uint8_t> dst) {
        if(!pixels.read(true,at,dst)) return false;
        ++reads;return true;
    };
    std::vector<std::uint8_t> cache(512*4096),dirty(512);
    unsigned present=0,empty=0;
    for(unsigned i=0;i<std::size(streamingGlyphRecords);++i) {
        const auto& r=streamingGlyphRecords[i];
        std::array<std::uint8_t,16> entry{};
        unsigned index=i+1;entry[4]=index&255;entry[5]=(index>>8)&255;entry[6]=(index>>16)&255;entry[7]=i%5;
        std::copy(r.originalMetric.begin(),r.originalMetric.end(),entry.begin()+8);
        const bool large=(entry[15]&128)!=0;const unsigned cells=large?2:1,slot=i%(large?128:512);
        std::fill(dirty.begin(),dirty.end(),0);
        std::memcpy(memory.data()+0x400,entry.data(),entry.size());
        std::uint8_t globalDirty=0;
        require(texture_font::stageNativeGlyph(runtime,memoryRead,font,font+0x400,
            large?0x13c7ac8:0x13c5a98,slot,cache,dirty,globalDirty)==texture_font::GlyphSource::prepared);
        require(globalDirty==1);
        const unsigned ox=(slot%(32/cells))*cells*64,oy=(slot/(32/cells))*cells*64;
        require(std::count(dirty.begin(),dirty.end(),1)==cells*cells);
        for(unsigned y=0;y<r.pitch;++y) for(unsigned x=0;x<r.pitch;++x) {
            const unsigned ax=ox+x,ay=oy+y,tile=(ay/64)*32+ax/64;
            const auto value=cache[tile*4096+(ay%64)*64+ax%64];
            require(value==(r.present?planes[r.offset+y*r.pitch+x]:0));
            require(dirty[tile]==1);
        }
        if(r.present)++present;else++empty;
        std::fill(dirty.begin(),dirty.end(),0);
        require(texture_font::stageOriginalGlyph(entry,slot,read,cache,dirty));
        // A changed logical offset disqualifies the prepared record, but the
        // verified original pixel source still supplies a complete fallback.
        entry[8]^=1;
        require(runtime.stage(memoryRead,font,entry,slot,cache,dirty)==texture_font::GlyphSource::original);
        entry[8]^=1;
        const unsigned offset=unsigned(entry[12])|(unsigned(entry[13])<<8)|((unsigned(entry[14])&0x3f)<<16);
        for(unsigned y=0;y<r.pitch;++y) for(unsigned x=0;x<r.pitch;++x) {
            const unsigned ax=ox+x,ay=oy+y,tile=(ay/64)*32+ax/64;
            const auto value=cache[tile*4096+(ay%64)*64+ax%64];
            require(value==(r.present?original[dataStart+std::size_t(offset)*1024+(y/2)*(r.pitch/2)+x/2]:0));
            require(dirty[tile]==1);
        }
    }
    require(reads==present);
    auto savedCache=cache,savedDirty=dirty;
    std::array<std::uint8_t,16> rejectedEntry{};
    put(bankEntry+4,bank+0x100);
    require(runtime.stage(memoryRead,font,rejectedEntry,0,cache,dirty)==texture_font::GlyphSource::failed);
    require(cache==savedCache && dirty==savedDirty);
    put(bankEntry+4,bank);runtime.close(bank);
    require(runtime.stage(memoryRead,font,rejectedEntry,0,cache,dirty)==texture_font::GlyphSource::failed);
    require(cache==savedCache && dirty==savedDirty);
    std::array<std::uint8_t,1024> refused;refused.fill(0x5a);
    const auto unchanged=refused;
    require(!pixels.read(false,0,refused));
    require(!pixels.read(true,0xffffffffu,refused));
    require(!pixels.read(true,static_cast<std::uint32_t>(original.size()-dataStart-1),refused));
    require(refused==unchanged);
    require(!pixels.load(argv[2],original.size()-1,streamingOriginalSha256));
    require(!pixels.read(true,0,refused) && refused==unchanged);
    std::printf("PASS %zu native glyph identities: %u present, %u empty; staged and original-alpha fallback placements match\n",
                std::size(streamingGlyphRecords),present,empty);
}
