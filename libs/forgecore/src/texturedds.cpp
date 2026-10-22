#include "forge/texturewrite.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace forge::texturewrite {
// DDS layout: Microsoft DDS_HEADER / DDS_PIXELFORMAT. Only single-surface
// legacy 2D colour formats are accepted; signed bump channels are not colour.
Image loadDds(FILE* input) {
    auto require=[](bool yes,const char* why){if(!yes) throw std::runtime_error(why);};
    std::array<uint8_t,128> h{};
    require(std::fread(h.data(),1,h.size(),input)==h.size(),"DDS header is truncated");
    auto word=[&](size_t at){uint32_t n=0;for(unsigned i=0;i<4;++i)n|=uint32_t(h[at+i])<<(i*8);return n;};
    require(std::memcmp(h.data(),"DDS ",4)==0 && word(4)==124 && word(76)==32,"Invalid DDS header");
    const auto height=word(12),width=word(16),flags=word(80),fourcc=word(84);
    require(width && height && width<=8192 && height<=8192,"DDS dimensions must be between 1 and 8192 pixels");
    require(word(24)<=1 && !(word(8)&0x800000) && !(word(112)&0x20fe00),"DDS cubemaps and volume textures are not supported; export a single 2D image");
    unsigned format=0;
    if(flags&4) {
        if(fourcc==0x31545844) format=1;
        else if(fourcc==0x33545844) format=2;
        else if(fourcc==0x35545844) format=3;
        else if(fourcc==0x30315844) throw std::runtime_error("DX10 DDS is not supported; export legacy DXT1, DXT3, DXT5 or 32-bit RGBA DDS");
        else throw std::runtime_error("Unsupported DDS compression; use legacy DXT1, DXT3, DXT5 or 32-bit RGBA (bump-map formats need a separate workflow)");
    } else {
        require((flags&0x40) && word(88)==32 && word(96)==0xff00 &&
            ((word(92)==0xff0000 && word(100)==0xff) || (word(92)==0xff && word(100)==0xff0000)) &&
            (word(104)==0 || word(104)==0xff000000),"Unsupported DDS pixel masks; export 32-bit RGBA or BGRA");
        require(!(flags&1) || word(104)==0xff000000,"DDS alpha flag has no supported alpha mask");
    }
    const uint64_t rowBytes=uint64_t(width)*4;
    const uint64_t pitch=!format && (word(8)&8)?word(20):rowBytes;
    require(format || (pitch>=rowBytes && pitch<=rowBytes+65536),"Invalid DDS row pitch");
    const auto mipBytes=[&](uint32_t w,uint32_t h){return format?uint64_t((w+3)/4)*((h+3)/4)*(format==1?8:16):uint64_t(w)*h*4;};
    const uint64_t topBytes=format?mipBytes(width,height):pitch*height;
    uint32_t maxMips=1;for(auto extent=std::max(width,height);extent>1;extent>>=1) ++maxMips;
    const auto mips=std::max(1u,word(28));require(mips<=maxMips,"DDS mip count exceeds its dimensions");
    uint64_t total=topBytes;auto w=width,hh=height;
    for(uint32_t level=1;level<mips;++level) {w=std::max(1u,w/2);hh=std::max(1u,hh/2);total+=mipBytes(w,hh);}
    require(std::fseek(input,0,SEEK_END)==0,"Cannot inspect DDS file size");
    const auto length=std::ftell(input);
    require(length>=128 && uint64_t(length)-128>=total,"DDS pixel data or declared mip chain is truncated");
    require(total<=512ull*1024*1024,"DDS surface and mip chain are too large");
    require(std::fseek(input,128,SEEK_SET)==0,"Cannot read DDS pixels");
    std::vector<uint8_t> bytes(static_cast<size_t>(total));
    require(std::fread(bytes.data(),1,bytes.size(),input)==bytes.size(),"DDS pixels changed or became unreadable");
    Image image;image.width=int(width);image.height=int(height);image.rgba.resize(size_t(width)*height*4);
    image.fromDds=true;image.sourceFormat=format==1?kFormatDXT1:format==2?kFormatDXT3:format==3?0x23:kFormatARGB;
    size_t mipOffset=0;w=width;hh=height;
    for(uint32_t level=0;level<mips;++level) {
        const auto stride=level==0?pitch:uint64_t(w)*4;
        const auto size=format?mipBytes(w,hh):stride*hh;
        const auto* source=bytes.data()+mipOffset;
        std::vector<uint8_t> raw;
        if(format) raw.assign(source,source+size);
        else {
            raw.resize(size_t(w)*hh*4);const bool bgra=word(92)==0xff0000,alpha=(flags&1)!=0;
            for(uint32_t y=0;y<hh;++y) for(uint32_t x=0;x<w;++x) {
                const auto* src=source+size_t(y*stride+x*4);auto* dst=raw.data()+(size_t(y)*w+x)*4;
                dst[0]=src[bgra?0:2];dst[1]=src[1];dst[2]=src[bgra?2:0];dst[3]=alpha?src[3]:255;
            }
        }
        image.sourceMips.push_back(std::move(raw));mipOffset+=size;w=std::max(1u,w/2);hh=std::max(1u,hh/2);
    }
    if(!format) {
        const bool bgra=word(92)==0xff0000,alpha=(flags&1)!=0;
        for(uint32_t y=0;y<height;++y) for(uint32_t x=0;x<width;++x) {
            const auto* src=bytes.data()+size_t(y*pitch+x*4);auto* dst=image.rgba.data()+(size_t(y)*width+x)*4;
            dst[0]=src[bgra?2:0];dst[1]=src[1];dst[2]=src[bgra?0:2];dst[3]=alpha?src[3]:255;
        }
        return image;
    }
    auto rgb=[](uint16_t c){return std::array<unsigned,4>{unsigned((c>>11)*255/31),unsigned(((c>>5)&63)*255/63),unsigned((c&31)*255/31),255};};
    size_t offset=0;
    for(uint32_t by=0;by<height;by+=4) for(uint32_t bx=0;bx<width;bx+=4) {
        const auto* block=bytes.data()+offset;offset+=format==1?8:16;
        const auto* color=block+(format==1?0:8);
        const uint16_t c0=uint16_t(color[0])|(uint16_t(color[1])<<8),c1=uint16_t(color[2])|(uint16_t(color[3])<<8);
        std::array<std::array<unsigned,4>,4> palette{rgb(c0),rgb(c1),{}, {}};
        for(unsigned channel=0;channel<3;++channel) {
            if(format!=1 || c0>c1) {palette[2][channel]=(2*palette[0][channel]+palette[1][channel])/3;palette[3][channel]=(palette[0][channel]+2*palette[1][channel])/3;}
            else {palette[2][channel]=(palette[0][channel]+palette[1][channel])/2;palette[3][channel]=0;}
        }
        palette[2][3]=255;palette[3][3]=format==1 && c0<=c1?0:255;
        uint32_t indices=0;for(unsigned i=0;i<4;++i) indices|=uint32_t(color[4+i])<<(8*i);
        std::array<unsigned,8> alpha{};uint64_t alphaBits=0;
        if(format==3) {
            alpha[0]=block[0];alpha[1]=block[1];
            if(alpha[0]>alpha[1]) for(unsigned i=2;i<8;++i) alpha[i]=((8-i)*alpha[0]+(i-1)*alpha[1])/7;
            else {for(unsigned i=2;i<6;++i) alpha[i]=((6-i)*alpha[0]+(i-1)*alpha[1])/5;alpha[6]=0;alpha[7]=255;}
            for(unsigned i=0;i<6;++i) alphaBits|=uint64_t(block[2+i])<<(8*i);
        }
        for(unsigned i=0;i<16;++i) {
            const auto x=bx+i%4,y=by+i/4;if(x>=width || y>=height) continue;
            auto rgba=palette[(indices>>(2*i))&3];
            if(format==2) rgba[3]=((block[i/2]>>((i%2)*4))&15)*17;
            if(format==3) rgba[3]=alpha[(alphaBits>>(3*i))&7];
            auto* dst=image.rgba.data()+(size_t(y)*width+x)*4;for(unsigned c=0;c<4;++c) dst[c]=uint8_t(rgba[c]);
        }
    }
    return image;
}
std::vector<uint8_t> encodeDds(const Image& image,uint32_t format,unsigned mipLevels,bool cutoutAlpha) {
    auto require=[](bool yes,const char* why){if(!yes) throw std::invalid_argument(why);};
    require(image.width>0 && image.height>0 && image.width<=8192 && image.height<=8192,
            "DDS dimensions must be between 1 and 8192 pixels");
    require(image.rgba.size()==size_t(image.width)*size_t(image.height)*4,"Invalid DDS image pixels");
    require(format==kFormatDXT1 || format==kFormatDXT3 || format==kFormatDXT5 || format==kFormatARGB,
            "DDS output supports DXT1, DXT3, DXT5 and ARGB only");
    require(!cutoutAlpha || format==kFormatDXT1,"DDS cutout mode requires DXT1");
    unsigned maximum=1;
    for(unsigned extent=unsigned(std::max(image.width,image.height));extent>1;extent>>=1) ++maximum;
    if(!mipLevels) mipLevels=maximum;
    require(mipLevels<=maximum,"DDS mip count exceeds its dimensions");
    std::vector<uint8_t> bytes(128,0);
    auto word=[&](size_t at,uint32_t n){for(unsigned i=0;i<4;++i) bytes.at(at+i)=uint8_t(n>>(i*8));};
    const bool argb=format==kFormatARGB;
    word(0,0x20534444);word(4,124);
    word(8,0x1007|(argb?8:0x80000)|(mipLevels>1?0x20000:0));
    word(12,uint32_t(image.height));word(16,uint32_t(image.width));
    word(20,uint32_t(argb?size_t(image.width)*4:mipRawLength(format,image.width,image.height)));
    word(28,mipLevels);word(76,32);
    if(argb) {
        word(80,0x41);word(88,32);word(92,0xff0000);word(96,0xff00);word(100,0xff);word(104,0xff000000);
    } else {
        word(80,4);word(84,format==kFormatDXT1?0x31545844:format==kFormatDXT3?0x33545844:0x35545844);
    }
    word(108,0x1000|(mipLevels>1?0x400008:0));
    auto mip=image;
    for(unsigned level=0;level<mipLevels;++level) {
        const auto raw=format==kFormatDXT1?encodeDxt1(mip,cutoutAlpha):encodeMip(format,mip);
        bytes.insert(bytes.end(),raw.begin(),raw.end());
        if(level+1<mipLevels) mip=downsample(mip);
    }
    return bytes;
}
} // namespace forge::texturewrite
