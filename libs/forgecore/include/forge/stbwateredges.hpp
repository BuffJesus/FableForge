#pragma once

#include "forge/stbwater.hpp"
#include <span>

namespace forge::stbwater {

// CLandscapeBackgroundPatch::BuildEdgeStrips (02CE3970), water portion only.
// Inputs are the existing native 20-byte ground-edge and 16-byte ground-fan
// records. The water generator's raw vertices are used even for draw types
// which subsequently convert their GPU vertices to float XYZ.
struct BackgroundWaterEdges {
    std::vector<uint8_t> vertices; // 60-byte CWaterVertex records
    std::vector<uint8_t> fans;     // 56-byte CWaterFanBaseVertex records
};

inline BackgroundWaterEdges buildBackgroundWaterEdges(
    std::span<const uint8_t> groundEdges, std::span<const uint8_t> groundFans,
    const BackgroundMesh& water, bool areaHasWater) {
    if(groundEdges.size()%20 || groundFans.size()%16 ||
       groundEdges.size()/20>65535 || groundFans.size()/16>65535 ||
       water.vertices.size()>65535)
        throw std::invalid_argument("invalid background water edge storage");
    BackgroundWaterEdges out;
    if(!areaHasWater)return out;
    auto word=[](std::span<const uint8_t> bytes,size_t at) {
        return uint16_t(bytes[at]|(unsigned(bytes[at+1])<<8));
    };
    auto mappedVertex=[&](uint16_t ground)->int32_t {
        if(ground>=water.groundToWaterVertex.size())
            throw std::invalid_argument("background water edge ground vertex is absent");
        const auto id=water.groundToWaterVertex[ground];
        if(id < -1 || (id>=0 && size_t(id)>=water.vertices.size()))
            throw std::invalid_argument("background water edge vertex mapping is invalid");
        return id;
    };
    auto putWord=[](std::vector<uint8_t>& bytes,size_t at,uint16_t value) {
        bytes[at]=uint8_t(value);bytes[at+1]=uint8_t(value>>8);
    };
    auto attributes=[](std::vector<uint8_t>& bytes,size_t at,const WaterVertex& vertex) {
        auto putFloat=[&](size_t offset,float value) {
            if(!std::isfinite(value))
                throw std::invalid_argument("background water edge attribute is nonfinite");
            const auto bits=std::bit_cast<uint32_t>(value);
            for(unsigned i=0;i<4;++i)bytes[offset+i]=uint8_t(bits>>(8*i));
        };
        putFloat(at,vertex.height);
        for(size_t i=0;i<12;++i)putFloat(at+4+4*i,vertex.shore[i]);
    };
    for(size_t edge=0;edge<groundEdges.size()/20;++edge) {
        const auto ground=word(groundEdges,edge*20+12);
        const auto vertex=mappedVertex(ground);
        if(vertex<0)continue;
        const auto start=out.vertices.size();
        // Authored zero policy for bytes native construction leaves unwritten:
        // edge52..55 when no fan, fan54..55 always, fan0..51 when mapped=-1.
        // This is not a claim about the native allocator's initial contents.
        out.vertices.resize(start+60,0);
        attributes(out.vertices,start,water.vertices[size_t(vertex)]);
        putWord(out.vertices,start+58,uint16_t(edge));
        const auto groundFan=word(groundEdges,edge*20+16);
        if(groundFan==0xffff) {
            putWord(out.vertices,start+56,0xffff);
            continue;
        }
        if(size_t(groundFan)>=groundFans.size()/16)
            throw std::invalid_argument("background water edge fan is absent");
        const auto index=word(groundEdges,edge*20+14);
        if(index>=water.groundToWaterIndex.size() || water.groundToWaterIndex[index]<-1)
            throw std::invalid_argument("background water edge index mapping is invalid");
        putWord(out.vertices,start+52,uint16_t(vertex));
        // Native reads the LOW UWORD of the signed32 mapping, including -1.
        putWord(out.vertices,start+54,uint16_t(water.groundToWaterIndex[index]));
        putWord(out.vertices,start+56,uint16_t(out.fans.size()/56));
        const auto fan=out.fans.size();out.fans.resize(fan+56,0);
        const auto fanVertex=mappedVertex(word(groundFans,size_t(groundFan)*16+12));
        putWord(out.fans,fan+52,uint16_t(fanVertex));
        if(fanVertex>=0)attributes(out.fans,fan,water.vertices[size_t(fanVertex)]);
    }
    return out;
}

} // namespace forge::stbwater
