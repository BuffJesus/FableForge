#pragma once
#include "forge/stbwaterio.hpp"

namespace forge::stbwater {

struct ForegroundWaterVertex {
    uint16_t x=0,y=0;
    float height=0;
    int16_t sinWave=0,cosWave=0,depth=0;
    float shoreU=0;
    std::array<float,12> shore=noBodyShoreLookup();
    bool operator==(const ForegroundWaterVertex&) const=default;
};
struct ForegroundWaterPayload {
    int32_t offsetX=0,offsetY=0;
    float waterBodySpan=0;
    int32_t waterType=1;
    std::array<ForegroundWaterVertex,289> vertices{};
    size_t consumed=0;
};
namespace detail {
inline void validateForegroundWater(const ForegroundWaterPayload& p) {
    if(p.waterType<1 || p.waterType>8 || p.waterType==7 ||
       !std::isfinite(p.waterBodySpan) || p.waterBodySpan<0)
        throw std::invalid_argument("invalid foreground water type/span");
    for(const auto& v:p.vertices) {
        if(!std::isfinite(v.height) || !std::isfinite(v.shoreU) || v.depth<0)
            throw std::invalid_argument("invalid foreground water vertex");
        for(float distance:v.shore)if(!std::isfinite(distance))
            throw std::invalid_argument("nonfinite foreground shore distance");
    }
}
}

// Save02E68BF0 writes offsets, span, type, compressed byte length and one range
// block with289 records of66 bytes. Runtime vertices use68 bytes: the saved
// signed-short depth replaces the runtime float at+12; shoreU is saved at+14.
// The parent's hasWater byte is deliberately outside this payload.
inline std::vector<uint8_t> encodeForegroundWater(const ForegroundWaterPayload& p) {
    detail::validateForegroundWater(p);
    std::vector<uint8_t> out,raw;raw.reserve(289*66);
    auto half=[](std::vector<uint8_t>& b,uint16_t v){b.push_back(uint8_t(v));b.push_back(uint8_t(v>>8));};
    auto word=[&](std::vector<uint8_t>& b,uint32_t v){half(b,uint16_t(v));half(b,uint16_t(v>>16));};
    word(out,uint32_t(p.offsetX));word(out,uint32_t(p.offsetY));
    word(out,std::bit_cast<uint32_t>(p.waterBodySpan));word(out,uint32_t(p.waterType));
    for(const auto& v:p.vertices) {
        half(raw,v.x);half(raw,v.y);word(raw,std::bit_cast<uint32_t>(v.height));
        half(raw,uint16_t(v.sinWave));half(raw,uint16_t(v.cosWave));half(raw,uint16_t(v.depth));
        word(raw,std::bit_cast<uint32_t>(v.shoreU));
        for(float distance:v.shore)word(raw,std::bit_cast<uint32_t>(distance));
    }
    const auto block=rangecodec::encodeNative(raw.data(),289,66);
    if(block.empty() || block.size()>size_t(INT32_MAX))throw std::invalid_argument("invalid foreground water range length");
    word(out,uint32_t(block.size()));out.insert(out.end(),block.begin(),block.end());return out;
}

inline ForegroundWaterPayload decodeForegroundWater(std::span<const uint8_t> bytes,size_t offset=0) {
    const size_t start=offset;
    auto need=[&](size_t n){if(offset>bytes.size() || n>bytes.size()-offset)
        throw std::invalid_argument("truncated foreground water payload");};
    auto word=[&](){need(4);const auto value=detail::waterU32(bytes.data()+offset);offset+=4;return value;};
    ForegroundWaterPayload out;
    out.offsetX=std::bit_cast<int32_t>(word());out.offsetY=std::bit_cast<int32_t>(word());
    out.waterBodySpan=std::bit_cast<float>(word());out.waterType=std::bit_cast<int32_t>(word());
    const int32_t length=std::bit_cast<int32_t>(word());
    if(length<1)throw std::invalid_argument("invalid foreground water range length");
    need(size_t(length));const auto raw=rangecodec::decodeExact(bytes.data()+offset,size_t(length),289,66);
    offset+=size_t(length);size_t at=0;
    auto half=[&](){const auto v=uint16_t(uint16_t(raw[at]) | (uint16_t(raw[at+1])<<8));at+=2;return v;};
    auto rawWord=[&](){const auto v=detail::waterU32(raw.data()+at);at+=4;return v;};
    for(auto& v:out.vertices) {
        v.x=half();v.y=half();v.height=std::bit_cast<float>(rawWord());
        v.sinWave=std::bit_cast<int16_t>(half());v.cosWave=std::bit_cast<int16_t>(half());
        v.depth=std::bit_cast<int16_t>(half());v.shoreU=std::bit_cast<float>(rawWord());
        for(auto& distance:v.shore)distance=std::bit_cast<float>(rawWord());
    }
    out.consumed=offset-start;detail::validateForegroundWater(out);return out;
}

} // namespace forge::stbwater
