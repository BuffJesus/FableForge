#include "texturebrowse.hpp"
#include "forge/terraintex.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>

using namespace albion;
#define CHECK(x) do { if(!(x)) { std::cerr << "line " << __LINE__ << ": " #x "\n"; std::exit(1); } } while(false)
static void u16(std::vector<uint8_t>& b,size_t p,uint16_t x) { b[p]=uint8_t(x); b[p+1]=uint8_t(x>>8); }
static void u32(std::vector<uint8_t>& b,size_t p,uint32_t x) { for(int n=0;n<4;++n)b[p+n]=uint8_t(x>>(n*8)); }
static std::vector<uint8_t> header(uint16_t aw,uint16_t ah,uint16_t fw,uint16_t fh,uint16_t frames,uint8_t mips=1) {
    std::vector<uint8_t> b(34); u16(b,0,aw);u16(b,2,ah);u16(b,6,fw);u16(b,8,fh);u16(b,10,frames);
    u32(b,12,forge::terraintex::kFormatARGB);b[17]=mips;u32(b,20,aw*ah*4);
    std::copy_n(forge::terraintex::pixelFormatTail(forge::terraintex::kFormatARGB),6,b.data()+28);return b;
}
static void pixel(std::vector<uint8_t>& b,size_t n,uint8_t red,uint8_t green=0,uint8_t blue=0) {
    b[n*4]=blue;b[n*4+1]=green;b[n*4+2]=red;b[n*4+3]=255;
}
int main() {
    texbrowse::SpriteTexture out; std::string error;
    // Single authored 2x1 frame cropped from 4x2 allocation padding.
    auto h=header(4,2,2,1,1);std::vector<uint8_t> bytes(32,211);pixel(bytes,0,10,20,30);pixel(bytes,1,40,50,60);
    CHECK(texbrowse::decodeSpriteFrames(h,bytes,out,error));CHECK(out.frames==1&&out.image.width==2&&out.image.height==1);
    CHECK(out.image.rgba==std::vector<uint8_t>({10,20,30,255,40,50,60,255}));
    // Four frames in a row-major 2x2 sheet become a vertical atlas, not one giant sprite.
    h=header(4,4,2,2,4);bytes.assign(64,0);
    for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)pixel(bytes,y*4+x,uint8_t(10+20*(y/2*2+x/2)));
    CHECK(texbrowse::decodeSpriteFrames(h,bytes,out,error));CHECK(out.frames==4&&out.image.width==2&&out.image.height==8);
    for(size_t f=0;f<4;++f)for(size_t n=0;n<4;++n)CHECK(out.image.rgba[(f*4+n)*4]==10+20*f);
    // Repeated whole mip chains: mip1 must not be mistaken for frame1 mip0.
    h=header(4,2,2,1,2,2);bytes.assign(80,0);
    for(size_t f=0;f<2;++f) { for(size_t n=0;n<8;++n)pixel(bytes,f*10+n,uint8_t(30+f*90)); for(size_t n=8;n<10;++n)pixel(bytes,f*10+n,244); }
    CHECK(texbrowse::decodeSpriteFrames(h,bytes,out,error));CHECK(out.frames==2&&out.image.height==2);
    CHECK(out.image.rgba[0]==30&&out.image.rgba[8]==120);
    // Non-power-of-two allocation uses native rounded stride but crops authored pixels.
    h=header(3,2,3,2,1);u32(h,20,32);bytes.assign(32,0);
    for(size_t n=0;n<8;++n)pixel(bytes,n,uint8_t(n+1));
    CHECK(texbrowse::decodeSpriteFrames(h,bytes,out,error));CHECK(out.image.rgba[12]==5);
    // A compressed stored-chunk sheet is reconstructable through decodeMip0.
    h=header(4,4,2,2,4);u32(h,24,66);bytes.assign(66,0);
    for(size_t n=0;n<16;++n) {bytes[2+n*4+2]=uint8_t(n);bytes[2+n*4+3]=255;}
    CHECK(texbrowse::decodeSpriteFrames(h,bytes,out,error));CHECK(out.image.rgba[0]==0&&out.image.rgba[16]==2&&out.image.rgba[32]==8);
    // Missing array frames cannot silently repeat frame0; failed output is empty.
    h=header(2,2,2,2,3);bytes.assign(16,0);
    CHECK(!texbrowse::decodeSpriteFrames(h,bytes,out,error));CHECK(!error.empty()&&out.frames==0&&out.image.rgba.empty());
    // Compressed independent arrays have no verified per-frame byte offsets.
    h=header(2,2,2,2,2);u32(h,24,18);bytes.assign(36,0);
    CHECK(!texbrowse::decodeSpriteFrames(h,bytes,out,error));CHECK(error.find("Compressed")!=std::string::npos);
    h=header(2,2,2,2,1);bytes.assign(15,0);CHECK(!texbrowse::decodeSpriteFrames(h,bytes,out,error));
    h=header(2,2,0,2,1);bytes.assign(16,0);CHECK(!texbrowse::decodeSpriteFrames(h,bytes,out,error));
    h=header(2,2,2,2,65535);CHECK(!texbrowse::decodeSpriteFrames(h,bytes,out,error));
    h=header(4097,2049,1,1,1);CHECK(!texbrowse::decodeSpriteFrames(h,{},out,error));CHECK(error.find("dimensions")!=std::string::npos);
    h=header(2,2,2,2,1);u16(h,4,2);CHECK(!texbrowse::decodeSpriteFrames(h,bytes,out,error));
    CHECK(!texbrowse::decodeSpriteFrames({},bytes,out,error));
    std::cout << "sprite frame decoding passed\n";
}
