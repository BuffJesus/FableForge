#include "forge/fonttexture.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace forge::fonttexture {
namespace {
using Bytes=std::vector<uint8_t>;
void need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class T>T get(const Bytes& b,size_t at){
    need(at<=b.size() && sizeof(T)<=b.size()-at,"Truncated static font");
    T value;std::memcpy(&value,b.data()+at,sizeof(value));return value;
}
template<class T>void put(Bytes& b,size_t at,T value){
    need(at<=b.size() && sizeof(T)<=b.size()-at,"Static font write exceeds payload");
    std::memcpy(b.data()+at,&value,sizeof(value));
}
}
Bytes replaceStaticAtlas(const Bytes& original,const texturewrite::Image& image){
    const auto zero=std::find(original.begin(),original.end(),0);
    need(zero!=original.end(),"Unterminated static font name");
    const size_t header=size_t(zero-original.begin())+1;
    const auto w=get<uint32_t>(original,header+13),h=get<uint32_t>(original,header+17);
    need(w>1 && h>1 && w<=8192 && h<=8192,"Invalid static atlas dimensions");
    need(image.width>1 && image.height>1 && image.width<=8192 && image.height<=8192,
         "Invalid replacement static atlas dimensions");
    const uint32_t nw=uint32_t(image.width),nh=uint32_t(image.height);
    need(nw*h==nh*w,"Static atlas aspect ratio must be retained");
    need(image.rgba.size()==size_t(nw)*nh*4,"Static atlas requires RGBA8 pixels");
    const auto banks=get<uint32_t>(original,header+29);
    need(banks<=1024,"Excessive static glyph banks");
    size_t p=header+33;std::vector<size_t> glyphs;
    for(uint32_t bank=0;bank<banks;++bank){
        const auto count=get<uint32_t>(original,p+8);p+=12;
        need(count<=65536 && p<=original.size() && size_t(count)*22<=original.size()-p,
             "Invalid static glyph span");
        for(uint32_t n=0;n<count;++n,p+=22){
            float uv[4];for(unsigned i=0;i<4;++i){uv[i]=get<float>(original,p+i*4);need(std::isfinite(uv[i]),"Invalid glyph UV");}
            // Retail Arial 18/12 contain UVs beyond 1; retain sampler behavior.
            need(uv[0]<=uv[2] && uv[1]<=uv[3],"Inverted glyph UV");glyphs.push_back(p);
        }
    }
    const size_t sizeAt=p,tga=p+4;
    const auto tgaSize=get<uint32_t>(original,p);
    need(tga<=original.size() && tgaSize==original.size()-tga,"Invalid static atlas payload size");
    need(tgaSize>=18,"Truncated static atlas TGA");
    need(original[tga+1]==0 && original[tga+2]==2 &&
         (original[tga+16]==24 || original[tga+16]==32) && !(original[tga+17]&0xc0),
         "Static atlas requires uncompressed true-colour TGA");
    need(get<uint16_t>(original,tga+12)==w && get<uint16_t>(original,tga+14)==h,
         "Static atlas TGA dimensions disagree");
    const size_t pixelStart=tga+18+original[tga],channels=original[tga+16]/8;
    need(pixelStart<=original.size() && size_t(w)*h*channels<=original.size()-pixelStart,
         "Truncated static atlas pixels");
    const size_t end=pixelStart+size_t(w)*h*channels;
    const Bytes footer(original.begin()+end,original.end());
    const char signature[]="TRUEVISION-XFILE.";
    need(footer.empty() || (footer.size()==26 && std::all_of(footer.begin(),footer.begin()+8,[](auto b){return b==0;}) &&
         std::memcmp(footer.data()+8,signature,18)==0),"Unsupported TGA trailing data");
    if(channels==3)for(size_t i=3;i<image.rgba.size();i+=4)
        need(image.rgba[i]==255,"24-bit static atlas cannot preserve transparency");
    Bytes result(original.begin(),original.begin()+pixelStart);
    put(result,header+13,nw);put(result,header+17,nh);
    if(w!=nw || h!=nh)for(auto at:glyphs)for(unsigned i=0;i<4;++i){
        const double dim=(i%2)?h:w,next=(i%2)?nh:nw;
        const float uv=float(double(get<float>(original,at+i*4))*(dim-1)/dim*next/(next-1));
        need(std::isfinite(uv),"Resized glyph UV overflow");put(result,at+i*4,uv);
    }
    put(result,tga+12,uint16_t(nw));put(result,tga+14,uint16_t(nh));
    result.resize(pixelStart+size_t(nw)*nh*channels);
    for(uint32_t y=0;y<nh;++y)for(uint32_t x=0;x<nw;++x){
        const auto sx=(original[tga+17]&0x10)?nw-1-x:x;
        const auto sy=(original[tga+17]&0x20)?y:nh-1-y;
        const size_t src=(size_t(sy)*nw+sx)*4,dst=pixelStart+(size_t(y)*nw+x)*channels;
        result[dst]=image.rgba[src+2];result[dst+1]=image.rgba[src+1];result[dst+2]=image.rgba[src];
        if(channels==4)result[dst+3]=image.rgba[src+3];
    }
    result.insert(result.end(),footer.begin(),footer.end());
    put(result,sizeAt,uint32_t(result.size()-tga));return result;
}
}
