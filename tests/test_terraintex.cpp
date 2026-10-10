#include "forge/terraintex.hpp"
#include "forge/lzo.hpp"
#include "forge/texturewrite.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
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
        CHECK(forge::texturewrite::fullMipCount(1024,512,forge::texturewrite::kFormatDXT1)==8);
        CHECK(forge::texturewrite::fullMipCount(4,64,forge::texturewrite::kFormatDXT3)==1);
        CHECK(forge::texturewrite::fullMipCount(64,8,forge::texturewrite::kFormatDXT1)==2);
        CHECK(forge::texturewrite::fullMipCount(32,32,forge::texturewrite::kFormatDXT1)==4);
        CHECK(forge::texturewrite::fullMipCount(1,4,forge::texturewrite::kFormatARGB)==1);
        CHECK(forge::texturewrite::fullMipCount(16,8,forge::texturewrite::kFormatARGB)==4);
        for (const auto& test : std::array<std::array<int,4>,4>{{
            {{2,4,int(forge::texturewrite::kFormatDXT1),1}},
            {{4,2,int(forge::texturewrite::kFormatDXT3),1}},
            {{8,4,int(forge::texturewrite::kFormatDXT1),2}},
            {{4,4,int(forge::texturewrite::kFormatARGB),4}}
        }}) {
            forge::texturewrite::Image image{test[0],test[1],std::vector<uint8_t>(size_t(test[0]*test[1])*4,255)};
            bool refused=false;
            try { (void)forge::texturewrite::buildEntry(image,uint32_t(test[2]),0,0,false,test[3]); }
            catch(const std::invalid_argument&) { refused=true; }
            CHECK(refused);
        }
        // A one-pixel-wide mip still needs to average along its long axis.
        // Alternating rows/columns must survive as their average, not disappear.
        for (bool vertical : {false,true}) {
            forge::texturewrite::Image image;
            image.width=vertical?1:4; image.height=vertical?4:1;
            image.rgba={0,20,40,60, 200,220,240,180,
                        0,20,40,60, 200,220,240,180};
            const auto original=image.rgba;
            const auto half=forge::texturewrite::downsample(image);
            CHECK(half.width==(vertical?1:2) && half.height==(vertical?2:1));
            CHECK(half.rgba==std::vector<uint8_t>({100,120,140,120,100,120,140,120}));
            const auto last=forge::texturewrite::downsample(half);
            CHECK(last.width==1 && last.height==1);
            CHECK(last.rgba==std::vector<uint8_t>({100,120,140,120}));
            CHECK(forge::texturewrite::downsample(last).rgba==last.rgba);
            const auto built=forge::texturewrite::buildEntry(image,forge::texturewrite::kFormatARGB,0,0,false);
            CHECK(built.mips==1 && built.payload.size()==16);
            const auto averaged=forge::texturewrite::buildEntry(half,forge::texturewrite::kFormatARGB,0,0,false);
            CHECK(averaged.mips==1 && averaged.payload==
                  std::vector<uint8_t>({140,120,100,120,140,120,100,120}));
            CHECK(forge::terraintex::validateTextureEntry(averaged.info,averaged.payload).ok);
            CHECK(forge::terraintex::validateTextureEntry(built.info,built.payload).ok);
            CHECK(image.rgba==original);
        }
        using namespace forge::terraintex;
        // Decode BC1 alpha independently from its endpoint ordering and indices.
        auto alphaAt=[](const std::vector<uint8_t>& bytes,size_t block,int pixel) {
            const size_t offset=block*8;
            const int c0=int(bytes.at(offset))|(int(bytes.at(offset+1))<<8);
            const int c1=int(bytes.at(offset+2))|(int(bytes.at(offset+3))<<8);
            uint32_t indices=0;
            for(int i=0;i<4;++i) indices|=uint32_t(bytes.at(offset+4+size_t(i)))<<(8*i);
            return c0<=c1 && ((indices>>(2*pixel))&3)==3 ? 0 : 255;
        };
        for (int kind=0;kind<3;++kind) {
            forge::texturewrite::Image image{4,4,std::vector<uint8_t>(64,255)};
            for(int i=0;i<16;++i) {
                image.rgba[size_t(i)*4+3]=kind==0?255:kind==1?0:std::array<uint8_t,4>{0,127,128,255}[size_t(i)%4];
            }
            const auto before=image.rgba;
            const auto encoded=forge::texturewrite::encodeDxt1(image,true);
            CHECK(encoded.size()==8 && image.rgba==before);
            for(int i=0;i<16;++i) CHECK(alphaAt(encoded,0,i)==(image.rgba[size_t(i)*4+3]<128?0:255));
            if(kind==0) CHECK(encoded==forge::texturewrite::encodeDxt1(image));
        }
        forge::texturewrite::Image cutout{8,8,std::vector<uint8_t>(256,255)};
        for(int y=0;y<8;++y) for(int x=0;x<4;++x) cutout.rgba[size_t(y*8+x)*4+3]=0;
        const auto cutoutEntry=forge::texturewrite::buildEntry(cutout,forge::texturewrite::kFormatDXT1,0,0,false,0,true);
        CHECK(cutoutEntry.info[16]==2 && cutoutEntry.mips==2 && cutoutEntry.payload.size()==40);
        CHECK(validateTextureEntry(cutoutEntry.info,cutoutEntry.payload).ok);
        for(int i=0;i<16;++i) {
            CHECK(alphaAt(cutoutEntry.payload,0,i)==0 && alphaAt(cutoutEntry.payload,1,i)==255);
            CHECK(alphaAt(cutoutEntry.payload,4,i)==(i%4<2?0:255));
        }
        bool invalidCutout=false;
        try { (void)forge::texturewrite::buildEntry(cutout,forge::texturewrite::kFormatARGB,0,0,false,0,true); }
        catch(const std::invalid_argument&) { invalidCutout=true; }
        CHECK(invalidCutout);
        // BC2 expands a four-bit alpha to multiples of 17. Every input must
        // choose its nearest representable opacity (at most eight away).
        for (int first=0;first<256;first+=16) {
            forge::texturewrite::Image image{4,4,std::vector<uint8_t>(64,255)};
            for (int i=0;i<16;++i) image.rgba[size_t(i)*4+3]=uint8_t(first+i);
            const auto encoded=forge::texturewrite::encodeDxt3(image);
            CHECK(encoded.size()==16);
            for (int i=0;i<16;++i) {
                const int decoded=((encoded[size_t(i)/2]>>((i%2)*4))&15)*17;
                CHECK(std::abs(decoded-(first+i))<=8);
            }
        }
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
        info=header(kFormatV8U8);info[16]=0;u32(info,20,128);pixels.assign(170,0);
        result=validateTextureEntry(info,pixels);
        CHECK(result.ok && result.consumed==170);
        CHECK(result.mipRawSizes==std::vector<size_t>({128,32,8,2}));
        CHECK(mipRawLength(kFormatV8U8,1,1)==2);
        preview=decodeMip0(info,pixels);
        CHECK(!preview.ok && preview.bytes.empty() && preview.error.find("V8U8")!=std::string::npos);
        compressed=forge::lzo::compress(pixels.data(),125);
        payload={uint8_t(compressed.size()),uint8_t(compressed.size()>>8)};
        payload.insert(payload.end(),compressed.begin(),compressed.end());
        payload.insert(payload.end(),pixels.begin()+125,pixels.end());
        u32(info,24,uint32_t(compressed.size()+5));
        CHECK(validateTextureEntry(info,payload).ok);
        payload.pop_back();CHECK(!validateTextureEntry(info,payload).ok);
        u32(info,24,0);info[30]=0;result=validateTextureEntry(info,pixels);
        CHECK(!result.ok && result.errors.at(0).find("CPixelFormatInit tail")!=std::string::npos);
        std::cout<<checks<<" texture storage checks passed\n";
        return 0;
    }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
