#pragma once
#include "display_identity.hpp"
#include <optional>

namespace texture_hud {
using Box=std::array<float,4>;
template<class Reader>
std::optional<Box> correctedBox(Reader read,unsigned bank,unsigned internal,unsigned primitive,
                               unsigned kind,unsigned count,std::span<const texture_display::IndexedRecord> rows) {
    if(kind>1)return {};
    unsigned char automatic=0;unsigned flags=0,index=0,frame=0;
    Box box{};
    if(!read(primitive+(kind?0x44:0x38),&automatic,1) || !automatic ||
       !read(primitive+(kind?0x48:0x3c),&flags,4) || !read(primitive+0xc,box.data(),sizeof box) ||
       !read(internal+0x5c,&index,4) || !index || !read(internal+0x60,&frame,4))return {};
    auto dimensions=[&](unsigned id,unsigned which,std::array<unsigned,2>& logical) {
        if(id>=count || which!=0)return false;
        std::array<unsigned,2> range{},frameHeader{};
        if(!read(bank+0x1e0,range.data(),8) || range[1]<range[0] || range[1]-range[0]!=count*44)return false;
        std::array<unsigned char,44> graphic{};
        if(!read(range[0]+id*44,graphic.data(),graphic.size()))return false;
        unsigned vector=0,header=0;std::uint16_t frames=0;
        std::memcpy(&vector,graphic.data()+40,4);std::memcpy(&frames,graphic.data()+10,2);
        std::array<unsigned char,28> live{};
        if(frames!=1 || !vector || !read(vector+12,&header,4) || !header ||
           !read(header,live.data(),live.size()) || std::memcmp(live.data(),graphic.data(),28)!=0)return false;
        texture_display::Resolver resolver(read,std::span<const texture_display::Record>{});
        for(unsigned axis=0;axis<2;++axis) {
            std::uint16_t native=0;std::memcpy(&native,live.data()+6+axis*2,2);
            const auto match=resolver.indexedExtent(bank,id,axis,native,"GBANK_MAIN",count,rows);
            if(match.reason!=texture_display::Reason::matched)return false;
            logical[axis]=match.value;
        }
        return true;
    };
    std::array<unsigned,2> logical{};
    if(!dimensions(index,frame,logical))return {};
    if(kind) {
        unsigned overlay=0,overlayFrame=0;
        if(!read(internal+0x68,&overlay,4) || !read(internal+0x6c,&overlayFrame,4))return {};
        if(overlay) {
            std::array<unsigned,2> extra{};
            if(!dimensions(overlay,overlayFrame,extra))return {};
            for(unsigned axis=0;axis<2;++axis)if(extra[axis]>logical[axis])logical[axis]=extra[axis];
        }
    }
    if(flags&1) {
        for(unsigned axis=0;axis<2;++axis) {
            const auto half=float(logical[axis]>>1);
            box[axis]-=half;box[axis+2]+=half;
        }
    } else {
        for(unsigned axis=0;axis<2;++axis)box[axis+2]+=float(logical[axis]);
    }
    return box;
}
}
