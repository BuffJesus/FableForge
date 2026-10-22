#pragma once

#include "forge/stbwateredges.hpp"
#include "forge/stbwaterio.hpp"

namespace forge::stbwater {

// Four CPatchTesselationEdgeStrip::Save records, followed by the parent patch's
// EBOOL hasWater and optional CEngineWaterBackgroundSubPatch::Save payload.
// This authors understood records; existing opaque trailers remain preserved
// by PatchBody until a caller explicitly requests a fresh water bake.
struct BackgroundEdgeStrip {
    uint16_t start=0,length=0;
    bool vertical=false;
    std::array<std::vector<uint8_t>,4> arrays; // ground20, fan16, water60, waterFan56
};
struct BackgroundWaterTrailer {
    std::array<BackgroundEdgeStrip,4> strips;
    std::optional<BackgroundWaterPayload> water;
    size_t consumed=0;
};
namespace detail {
inline constexpr std::array<size_t,4> edgeStrides={20,16,60,56};
inline uint16_t edgeU16(const std::vector<uint8_t>& bytes,size_t at) {
    return uint16_t(bytes.at(at)|(unsigned(bytes.at(at+1))<<8));
}
inline void validateWaterTrailer(const BackgroundWaterTrailer& trailer) {
    size_t waterCount=0;
    if(trailer.water) {
        validateWaterPayload(*trailer.water);
        waterCount=trailer.water->vertices.size()/trailer.water->vertexStride;
    }
    for(const auto& strip:trailer.strips) {
        std::array<size_t,4> counts{};
        for(size_t i=0;i<4;++i) {
            if(strip.arrays[i].size()%edgeStrides[i])
                throw std::invalid_argument("misaligned background edge array");
            counts[i]=strip.arrays[i].size()/edgeStrides[i];
            if(counts[i]>size_t(strip.length)+1)
                throw std::invalid_argument("background edge count exceeds strip extent");
        }
        if(counts[1]>counts[0] || counts[2]>counts[0] || counts[3]>counts[2] ||
           (!trailer.water && (counts[2] || counts[3])))
            throw std::invalid_argument("inconsistent background edge array counts");
        for(size_t i=0;i<counts[0];++i) {
            const auto fan=edgeU16(strip.arrays[0],i*20+16);
            if(fan!=65535 && fan>=counts[1])
                throw std::invalid_argument("background ground edge fan is absent");
        }
        for(size_t i=0;i<counts[2];++i) {
            const auto& bytes=strip.arrays[2];
            const auto fan=edgeU16(bytes,i*60+56);
            const auto index=edgeU16(bytes,i*60+54);
            if(edgeU16(bytes,i*60+58)>=counts[0] ||
               (fan!=65535 && (fan>=counts[3] || edgeU16(bytes,i*60+52)>=waterCount ||
                   (index!=65535 && index>=trailer.water->indices.size()))))
                throw std::invalid_argument("background water edge reference is invalid");
            for(size_t field=0;field<52;field+=4)
                if(!std::isfinite(std::bit_cast<float>(waterU32(bytes.data()+i*60+field))))
                    throw std::invalid_argument("nonfinite background water edge");
        }
        for(size_t i=0;i<counts[3];++i) {
            const auto& bytes=strip.arrays[3];
            const auto vertex=edgeU16(bytes,i*56+52);
            if(vertex==65535)continue; // native leaves height/shore unwritten
            if(vertex>=waterCount)throw std::invalid_argument("background water fan vertex is absent");
            for(size_t field=0;field<52;field+=4)
                if(!std::isfinite(std::bit_cast<float>(waterU32(bytes.data()+i*56+field))))
                    throw std::invalid_argument("nonfinite background water fan");
        }
    }
}
}

inline std::vector<uint8_t> encodeBackgroundWaterTrailer(const BackgroundWaterTrailer& trailer) {
    detail::validateWaterTrailer(trailer);
    std::vector<uint8_t> bytes;
    auto half=[&](uint16_t value){bytes.push_back(uint8_t(value));bytes.push_back(uint8_t(value>>8));};
    auto word=[&](uint32_t value){half(uint16_t(value));half(uint16_t(value>>16));};
    for(const auto& strip:trailer.strips) {
        half(strip.start);half(strip.length);bytes.push_back(uint8_t(strip.vertical));
        for(size_t i=0;i<4;++i) {
            const auto count=strip.arrays[i].size()/detail::edgeStrides[i];
            word(uint32_t(count));
            if(!count)continue;
            const auto block=rangecodec::encodeNative(strip.arrays[i].data(),count,detail::edgeStrides[i]);
            if(block.empty() || block.size()>size_t(INT32_MAX))
                throw std::invalid_argument("invalid background edge compressed length");
            word(uint32_t(block.size()));bytes.insert(bytes.end(),block.begin(),block.end());
        }
    }
    bytes.push_back(uint8_t(trailer.water.has_value()));
    if(trailer.water) {
        const auto payload=encodeBackgroundWater(*trailer.water);
        bytes.insert(bytes.end(),payload.begin(),payload.end());
    }
    return bytes;
}

inline BackgroundWaterTrailer decodeBackgroundWaterTrailer(std::span<const uint8_t> bytes,size_t offset=0) {
    const size_t start=offset;
    auto need=[&](size_t n){if(offset>bytes.size() || n>bytes.size()-offset)
        throw std::invalid_argument("truncated background water trailer");};
    auto half=[&](){need(2);const auto value=uint16_t(bytes[offset]|(unsigned(bytes[offset+1])<<8));offset+=2;return value;};
    auto word=[&](){need(4);const auto value=detail::waterU32(bytes.data()+offset);offset+=4;return value;};
    auto flag=[&](){need(1);const auto value=bytes[offset++];if(value>1)
        throw std::invalid_argument("invalid background edge/water flag");return value!=0;};
    BackgroundWaterTrailer result;
    for(auto& strip:result.strips) {
        strip.start=half();strip.length=half();strip.vertical=flag();
        for(size_t i=0;i<4;++i) {
            const auto count=word();
            if(count>size_t(strip.length)+1)throw std::invalid_argument("invalid background edge count");
            if(!count)continue;
            const auto length=std::bit_cast<int32_t>(word());
            if(length<1)throw std::invalid_argument("invalid background edge compressed length");
            need(size_t(length));
            strip.arrays[i]=rangecodec::decodeExact(bytes.data()+offset,size_t(length),count,detail::edgeStrides[i]);
            offset+=size_t(length);
        }
    }
    if(flag()) {
        result.water=decodeBackgroundWater(bytes,offset);
        offset+=result.water->consumed;
    }
    result.consumed=offset-start;detail::validateWaterTrailer(result);return result;
}

// Fresh authoring only: preserve every decoded ground-edge/fan byte, then
// append the verified water seam arrays and payload. Compression may change.
// Parent texture/VB/IB and all other patch bytes are outside this function.
inline std::vector<uint8_t> addBackgroundWaterToDryTrailer(std::span<const uint8_t> dry,
    const BackgroundMesh& mesh,bool areaHasWater) {
    auto trailer=decodeBackgroundWaterTrailer(dry);
    if(trailer.consumed!=dry.size() || trailer.water)
        throw std::invalid_argument("background water requires one exact dry trailer");
    for(auto& strip:trailer.strips) {
        auto water=buildBackgroundWaterEdges(strip.arrays[0],strip.arrays[1],mesh,areaHasWater);
        strip.arrays[2]=std::move(water.vertices);strip.arrays[3]=std::move(water.fans);
    }
    if(!mesh.vertices.empty())trailer.water=backgroundWaterPayload(mesh);
    return encodeBackgroundWaterTrailer(trailer);
}

} // namespace forge::stbwater
