#pragma once
#include "forge/stbwater.hpp"
#include "forge/rangecodec.hpp"

namespace forge::stbwater {

inline unsigned backgroundWaterStride(int32_t type) {
    switch(type) {
        case 1:case 2:case 6:case 8:return 56;
        case 3:case 4:case 5:return 12;
        default:throw std::invalid_argument("unsupported background water payload type");
    }
}
struct BackgroundWaterPayload {
    int32_t waterType=0;
    unsigned vertexStride=0;
    std::vector<uint8_t> vertices;
    std::vector<uint16_t> indices;
    size_t consumed=0; // decode only: bytes belonging to this payload, excluding any suffix
};
namespace detail {
inline uint32_t waterU32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1])<<8) | (uint32_t(p[2])<<16) | (uint32_t(p[3])<<24);
}
inline void validateWaterPayload(const BackgroundWaterPayload& p) {
    const auto stride=backgroundWaterStride(p.waterType);
    if(p.vertexStride!=stride || p.vertices.empty() || p.vertices.size()%stride || p.vertices.size()/stride>65535 ||
       p.indices.size()%3 || p.indices.size()/3>65535)
        throw std::invalid_argument("invalid background water counts/stride");
    const auto count=p.vertices.size()/stride;
    for(auto index:p.indices)if(index>=count)throw std::invalid_argument("background water index outside vertex buffer");
    for(size_t at=0;at<p.vertices.size();at+=stride)
        for(size_t field=stride==56?4:0;field<stride;field+=4)
            if(!std::isfinite(std::bit_cast<float>(waterU32(p.vertices.data()+at+field))))
                throw std::invalid_argument("nonfinite background water vertex");
}
}
inline BackgroundWaterPayload backgroundWaterPayload(const BackgroundMesh& mesh) {
    BackgroundWaterPayload out{mesh.waterType,mesh.vertexStride,mesh.drawVertices,mesh.indices};
    detail::validateWaterPayload(out);
    if(mesh.vertices.size()!=out.vertices.size()/out.vertexStride)
        throw std::invalid_argument("background water mesh has inconsistent draw vertices");
    return out;
}
// Native Save02E055E0: u16 vertexCount, u16 TRIANGLE count, s32 type, s32 stride,
// s32 compressedVertexLength, range block; optional s32 indexLength/range block.
// Origins/dimensions belong to the parent patch and are not saved here.
inline std::vector<uint8_t> encodeBackgroundWater(const BackgroundWaterPayload& payload) {
    detail::validateWaterPayload(payload);
    std::vector<uint8_t> out;
    auto half=[&](uint16_t value){out.push_back(uint8_t(value));out.push_back(uint8_t(value>>8));};
    auto word=[&](uint32_t value){half(uint16_t(value));half(uint16_t(value>>16));};
    const auto count=payload.vertices.size()/payload.vertexStride;
    half(uint16_t(count));half(uint16_t(payload.indices.size()/3));
    word(uint32_t(payload.waterType));word(payload.vertexStride);
    auto block=[&](const uint8_t* bytes,size_t n,size_t stride) {
        const auto packed=rangecodec::encodeNative(bytes,n,stride);
        if(packed.empty() || packed.size()>size_t(INT32_MAX))throw std::invalid_argument("invalid water range block length");
        word(uint32_t(packed.size()));out.insert(out.end(),packed.begin(),packed.end());
    };
    block(payload.vertices.data(),count,payload.vertexStride);
    if(!payload.indices.empty()) {
        std::vector<uint8_t> indices;indices.reserve(payload.indices.size()*2);
        for(auto index:payload.indices){indices.push_back(uint8_t(index));indices.push_back(uint8_t(index>>8));}
        block(indices.data(),payload.indices.size(),2);
    }
    return out;
}
inline std::vector<uint8_t> encodeBackgroundWater(const BackgroundMesh& mesh) {
    return encodeBackgroundWater(backgroundWaterPayload(mesh));
}
inline BackgroundWaterPayload decodeBackgroundWater(std::span<const uint8_t> bytes,size_t offset=0) {
    const size_t start=offset;
    auto need=[&](size_t n){if(offset>bytes.size() || n>bytes.size()-offset)throw std::invalid_argument("truncated background water payload");};
    auto half=[&](){need(2);const auto n=uint16_t(uint16_t(bytes[offset]) | (uint16_t(bytes[offset+1])<<8));offset+=2;return n;};
    auto word=[&](){need(4);const auto n=detail::waterU32(bytes.data()+offset);offset+=4;return n;};
    const unsigned count=half(),triangles=half();
    BackgroundWaterPayload out;out.waterType=std::bit_cast<int32_t>(word());out.vertexStride=word();
    if(!count || out.vertexStride!=backgroundWaterStride(out.waterType))
        throw std::invalid_argument("invalid saved background water counts/stride");
    auto block=[&](size_t n,size_t stride) {
        const int32_t length=std::bit_cast<int32_t>(word());
        if(length<1)throw std::invalid_argument("invalid saved water range block length");
        need(size_t(length));auto raw=rangecodec::decodeExact(bytes.data()+offset,size_t(length),n,stride);
        offset+=size_t(length);return raw;
    };
    out.vertices=block(count,out.vertexStride);
    if(triangles) {
        const auto raw=block(size_t(triangles)*3,2);
        for(size_t at=0;at<raw.size();at+=2)out.indices.push_back(uint16_t(uint16_t(raw[at]) | (uint16_t(raw[at+1])<<8)));
    }
    out.consumed=offset-start;detail::validateWaterPayload(out);return out;
}

} // namespace forge::stbwater
