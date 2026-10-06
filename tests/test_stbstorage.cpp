#include "forge/stbstorage.hpp"
#include "forge/stbbake.hpp"
#include "forge/rangecodec.hpp"
#include <iostream>
#include <stdexcept>
using Bytes=std::vector<uint8_t>;
static void append(Bytes& b,const Bytes& a) {b.insert(b.end(),a.begin(),a.end());}
static void half(Bytes& b,uint16_t v) {b.push_back(uint8_t(v));b.push_back(uint8_t(v>>8));}
static void word(Bytes& b,uint32_t v) {for(unsigned i=0;i<4;++i)b.push_back(uint8_t(v>>(8*i)));}
static void range(Bytes& b,const Bytes& raw) {
    const auto block=forge::rangecodec::encodeRaw(raw);word(b,uint32_t(block.size()));append(b,block);
}
template<class F> static void refuses(F f) {
    try {f();}catch(const std::exception&) {return;}
    throw std::runtime_error("malformed terrain storage accepted");
}
int main() {try {
    using namespace forge::stbstorage;
    forge::stbbake::ForegroundFrame frame;
    forge::stbbake::ForegroundLayer layer;layer.sharedIndexBuffer=true;layer.vertices.resize(3);
    layer.textures[0]=15360;layer.vertices[0].height=15360;frame.layers.push_back(layer);
    auto dry=forge::stbbake::serializeForegroundFrame(frame);validateForeground(dry);
    auto suffix=dry;suffix.push_back(0);refuses([&]{validateForeground(suffix);});
    frame.hasWater=true;frame.waterPayload.resize(12);word(frame.waterPayload,1);
    range(frame.waterPayload,Bytes(289*66));
    const auto wet=forge::stbbake::serializeForegroundFrame(frame);const auto original=wet;
    validateForeground(wet);if(wet!=original)throw std::runtime_error("terrain review changed input");
    for(size_t n:{size_t(0),size_t(1),dry.size()-1,wet.size()-1})
        refuses([&]{validateForeground(Bytes(wet.begin(),wet.begin()+n));});
    frame.waterPayload[12]=9;refuses([&]{validateForeground(forge::stbbake::serializeForegroundFrame(frame));});
    frame.waterPayload[12]=1;frame.waterPayload.push_back(0);
    refuses([&]{validateForeground(forge::stbbake::serializeForegroundFrame(frame));});
    Bytes b;half(b,16);half(b,16);half(b,0);half(b,0);b.push_back(0);b.push_back(0);
    half(b,1);half(b,3);b.push_back(4);b.push_back(4);b.push_back(1);
    forge::stbbake::InlineTexture texture;texture.width=4;texture.height=4;texture.levels=1;
    texture.pixelFormat0=1027;texture.surfacePool=1;texture.mipData.resize(8);
    append(b,forge::stbbake::serializeInlineTexture(texture));
    const auto vb=b.size();range(b,Bytes(48));const auto ib=b.size();range(b,Bytes{0,0,1,0,2,0});
    const auto trailer=b.size();b.resize(b.size()+85);
    validateBackground(b);
    auto shared=b;shared[9]=1;shared.erase(shared.begin()+ib,shared.begin()+trailer);
    validateBackground(shared);
    auto wrongMode=shared;wrongMode[9]=0;refuses([&]{validateBackground(wrongMode);});
    wrongMode=b;wrongMode[9]=1;refuses([&]{validateBackground(wrongMode);});
    for(size_t n=0;n<b.size();++n)refuses([&]{validateBackground(Bytes(b.begin(),b.begin()+n));});
    suffix=b;suffix.push_back(0);refuses([&]{validateBackground(suffix);});
    auto broken=b;broken[8]=2;refuses([&]{validateBackground(broken);});
    broken=b;broken[16]=0;refuses([&]{validateBackground(broken);});
    broken=b;broken[17+5]=0;refuses([&]{validateBackground(broken);});
    broken=b;broken[vb]=50;broken.insert(broken.begin()+vb+4+49,0);
    refuses([&]{validateBackground(broken);});
    broken=b;broken[trailer+4]=2;refuses([&]{validateBackground(broken);});
    broken=b;broken[trailer+5]=255;refuses([&]{validateBackground(broken);});
    // Both native background water layouts, including water-only framing.
    for(uint32_t type:{1u,3u}) {
        Bytes water(10);water[8]=1;water.resize(10+84);water.push_back(1);
        half(water,3);half(water,1);word(water,type);word(water,type==1?56:12);
        range(water,Bytes(3*(type==1?56:12)));range(water,Bytes{0,0,1,0,2,0});
        validateBackground(water);
        water[10+85+8]=99;refuses([&]{validateBackground(water);});
    }
    // Every edge-array variant is range-framed and geometry-only.
    Bytes edges(10);edges[8]=1;
    for(int side=0;side<4;++side) {
        edges.resize(edges.size()+5);
        for(size_t stride:{20,16,60,56}) {word(edges,1);range(edges,Bytes(stride));}
    }
    edges.push_back(0);validateBackground(edges);
    Bytes detail;word(detail,1);word(detail,1);word(detail,15360);word(detail,3);
    word(detail,0);detail.resize(detail.size()+40+52);
    word(detail,1);detail.resize(detail.size()+40);word(detail,1);detail.resize(detail.size()+4+32);
    detail.push_back(1);detail.resize(detail.size()+4*12);detail.push_back(1);detail.push_back(7);
    detail.push_back(1);word(detail,1);detail.resize(detail.size()+76);word(detail,15360); // unused Pad bytes
    const auto spriteKind=detail.size();word(detail,2);detail.resize(detail.size()+40);word(detail,1);
    detail.resize(detail.size()+68);word(detail,15360);detail.resize(detail.size()+12);
    if(validateLocalDetail(detail)!=std::vector<uint32_t>{15360})throw std::runtime_error("local palette index changed");
    for(size_t n=0;n<detail.size();++n)refuses([&]{validateLocalDetail(Bytes(detail.begin(),detail.begin()+n));});
    auto badDetail=detail;badDetail.push_back(0);refuses([&]{validateLocalDetail(badDetail);});
    badDetail=detail;badDetail[spriteKind]=3;refuses([&]{validateLocalDetail(badDetail);});
    badDetail=detail;badDetail[4]=2;refuses([&]{validateLocalDetail(badDetail);});
    badDetail=detail;badDetail[0]=255;refuses([&]{validateLocalDetail(badDetail);});
    if(!validateLocalDetail(Bytes(4)).empty())throw std::runtime_error("empty detail frame changed");
    std::cout<<"terrain storage checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
