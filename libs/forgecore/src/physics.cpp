#include "forge/physics.hpp"
#include "forge/lzo.hpp"
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace forge::physics {
namespace {
struct Reader {
    const std::vector<uint8_t>& bytes;
    size_t pos,end;
    size_t remaining() const {return end-pos;}
    size_t take(size_t n) {
        if(n>remaining())throw std::runtime_error("physics: truncated field");
        const auto at=pos;pos+=n;return at;
    }
    uint32_t word() {
        const auto at=take(4);
        return uint32_t(bytes[at]) | uint32_t(bytes[at+1])<<8 |
            uint32_t(bytes[at+2])<<16 | uint32_t(bytes[at+3])<<24;
    }
    void boolean() {if(bytes[take(1)]>1)throw std::runtime_error("physics: invalid boolean");}
    void string() {
        while(remaining())if(bytes[take(1)]==0)return;
        throw std::runtime_error("physics: unterminated string");
    }
    std::string_view tag() {
        const auto at=take(4);
        return {reinterpret_cast<const char*>(bytes.data()+at),4};
    }
    void array(size_t stride) {
        const auto count=word();
        if(count>remaining()/stride)throw std::runtime_error("physics: array exceeds chunk");
        take(size_t(count)*stride);
    }
};

void chunks(Reader& stream,unsigned depth) {
    if(depth>16)throw std::runtime_error("physics: excessive chunk nesting");
    while(stream.remaining()) {
        const auto tag=stream.tag();
        const auto size=stream.word();
        if(size>stream.remaining())throw std::runtime_error("physics: chunk exceeds parent");
        Reader c{stream.bytes,stream.pos,stream.pos+size};
        bool children=false;
        if(tag=="3DRT" || tag=="MTLS" || tag=="HLPR")children=true;
        else if(tag=="SUBM") {c.string();c.take(16);children=true;} // local hierarchy
        else if(tag=="MTRL") {
            // Native 0347CC20: name/index, two-sided, three colors, three floats.
            c.string();c.take(4);c.boolean();c.take(24);children=true;
        } else if(tag=="MTLE") {
            // Native 0347D1B0: ignored word and self-illumination color.
            c.take(8);
        } else if(tag=="MMAP") {
            // Native 0347D720: map-type enum, description and filename.
            c.take(4);c.string();c.string();children=true;
        } else if(tag=="HPNT" || tag=="HDMY") {
            if(tag=="HDMY") {
                const auto version=c.word();
                if(version<1 || version>3)throw std::runtime_error("physics: unsupported dummy version");
                c.take(12); // extents
                if(version>=2)c.take(48); // matrix
                if(version>=3)c.boolean();
            }
            // Native 033CEAC0/033CF1A0: position, local submesh/bone IDs, name.
            c.take(20);c.string();children=true;
        } else if(tag=="BONE") {
            // Native 033D93D0: four local hierarchy IDs, matrix, optional name.
            c.take(64);if(c.remaining())c.string();children=true;
        } else if(tag=="PRIM") {c.take(4);children=true;} // local material index
        else if(tag=="VGRP") {c.take(4);c.array(6);children=true;} // bone, u16 vertex + float weight
        else if(tag=="HCVL") {
            if(c.word()!=1)throw std::runtime_error("physics: unsupported convex version");
            c.string();c.array(16); // plane normal/distance floats
        } else if(tag=="VERT")c.array(32); // position/normal/UV floats
        else if(tag=="TRIS")c.array(6); // three local u16 vertex indices
        else if(tag=="UNIV")c.array(4); // local unique-vertex indices
        else if(tag=="SMTH") {
            c.array(4); // per-face smoothing bits
            // Native writer 033DCAA0 emits an optional zero count after masks.
            if(c.remaining()==4 && c.word()!=0)
                throw std::runtime_error("physics: nonzero smoothing tail");
        } else if(tag=="TRFM")c.take(48);
        else throw std::runtime_error("physics: unsupported chunk "+std::string(tag));
        if(children)chunks(c,depth+1);
        if(c.remaining())throw std::runtime_error("physics: trailing chunk data");
        stream.pos=c.end;
    }
}
}

void validateStorage(std::span<const uint8_t> payload) {
    if(payload.size()<4)throw std::runtime_error("physics: short payload");
    const auto first=uint32_t(payload[0]) | uint32_t(payload[1])<<8 |
        uint32_t(payload[2])<<16 | uint32_t(payload[3])<<24;
    std::vector<uint8_t> image;
    if(first==0x3e3e3e3e)image.assign(payload.begin(),payload.end());
    else {
        if(first<16 || first>256u*1024u*1024u)
            throw std::runtime_error("physics: unreasonable image length");
        image=lzo::decompress(payload.data()+4,payload.size()-4,first);
    }
    Reader c{image,0,image.size()};
    if(c.word()!=0x3e3e3e3e || c.tag()!="3DMF")
        throw std::runtime_error("physics: invalid image header");
    c.word();c.string(); // file version/comment, then alignment
    while(c.pos%4)if(image[c.take(1)]!=0)
        throw std::runtime_error("physics: unsupported header padding");
    const auto root=c.pos;
    if(c.tag()!="3DRT")throw std::runtime_error("physics: missing root chunk");
    const auto rootSize=c.word();
    c.take(rootSize);
    if(c.remaining() && (c.remaining()!=4 || c.word()!=0))
        throw std::runtime_error("physics: unsupported image footer");
    Reader body{image,root,root+8+rootSize};
    chunks(body,0);
}
} // namespace forge::physics
