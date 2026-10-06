#include "forge/terraintex.hpp"
#include "forge/lzo.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
namespace {
int checks=0;
#define CHECK(x) do { ++checks; if(!(x)) throw std::runtime_error("line " + std::to_string(__LINE__) + ": " #x); } while(false)
void u32(std::vector<uint8_t>& bytes,size_t p,uint32_t value) {
    for(size_t i=0;i<4;++i) bytes.at(p+i)=uint8_t(value>>(i*8));
}
std::vector<uint8_t> header(uint32_t format) {
    std::vector<uint8_t> info(34);
    info[0]=8; info[2]=8; info[6]=8; info[8]=8; info[10]=1;
    u32(info,12,format); info[16]=format==forge::terraintex::kFormatDXT5?3:1;
    info[17]=4; u32(info,20,64);
    std::copy_n(forge::terraintex::pixelFormatTail(format),6,info.begin()+28);
    return info;
}
}
int main() {
    try {
        using namespace forge::terraintex;
        auto info=header(kFormatDXT5);
        std::vector<uint8_t> pixels(85);
        for(size_t i=0;i<pixels.size();++i) pixels[i]=uint8_t(i*19);
        const auto original=pixels, originalInfo=info;
        auto result=validateTextureEntry(info,pixels);
        CHECK(result.ok && result.consumed==85);
        CHECK(result.mipRawSizes==std::vector<size_t>({64,16,4,1}));
        CHECK(pixels==original && info==originalInfo);
        CHECK(mipRawLength(kFormatDXT5,2,2)==16); // GPU allocation remains block-sized.
        CHECK(std::string(pixelFormatName(kFormatDXT5))=="DXT5");
        auto preview=decodeMip0(info,pixels);
        CHECK(!preview.ok && preview.bytes.empty() && preview.error.find("DXT5")!=std::string::npos);
        auto shortPixels=pixels; shortPixels.pop_back();
        CHECK(!validateTextureEntry(info,shortPixels).ok);
        auto extraPixels=pixels; extraPixels.push_back(0);
        CHECK(!validateTextureEntry(info,extraPixels).ok);
        auto wrongTail=info; wrongTail[28]=2;
        CHECK(!validateTextureEntry(wrongTail,pixels).ok);
        auto wrongSize=info; u32(wrongSize,20,65);
        CHECK(!validateTextureEntry(wrongSize,pixels).ok);
        // Real framing: compressed raw-3 bytes, three literal tail bytes, then raw mips.
        auto compressed=forge::lzo::compress(pixels.data(),61);
        CHECK(compressed.size()<65535);
        std::vector<uint8_t> payload{uint8_t(compressed.size()),uint8_t(compressed.size()>>8)};
        payload.insert(payload.end(),compressed.begin(),compressed.end());
        payload.insert(payload.end(),pixels.begin()+61,pixels.end());
        u32(info,24,uint32_t(compressed.size()+5));
        result=validateTextureEntry(info,payload);
        CHECK(result.ok && result.consumed==payload.size() && result.chunkCount==1);
        CHECK(result.mip0RegionSize==compressed.size()+5);
        CHECK(result.mipRawSizes==std::vector<size_t>({64,16,4,1}));
        auto badRegion=info; u32(badRegion,24,uint32_t(compressed.size()+6));
        CHECK(!validateTextureEntry(badRegion,payload).ok);
        payload.pop_back(); CHECK(!validateTextureEntry(info,payload).ok);
        // Existing BC2 validation/preview behavior remains separate from DXT5 storage.
        info=header(kFormatDXT3); pixels.assign(112,0);
        result=validateTextureEntry(info,pixels);
        CHECK(result.ok && result.consumed==112);
        CHECK(result.mipRawSizes==std::vector<size_t>({64,16,16,16}));
        preview=decodeMip0(info,pixels);
        CHECK(preview.ok && preview.format==PreviewFormat::BC2 && preview.bytes.size()==64);
        u32(info,12,99);
        result=validateTextureEntry(info,pixels);
        CHECK(!result.ok && result.errors.at(0)=="unsupported pixel format 99");
        std::cout<<checks<<" texture storage checks passed\n";
        return 0;
    }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
