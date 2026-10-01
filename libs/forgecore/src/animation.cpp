#include "forge/animation.hpp"
#include "forge/lzo.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace forge::animation {
namespace {

uint16_t u16(const uint8_t* p) {return uint16_t(p[0])|uint16_t(p[1])<<8;}
uint32_t u32(const uint8_t* p) {
    return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;
}

struct Cursor {
    const std::vector<uint8_t>& bytes;
    size_t p,end;
    void need(size_t n) const {
        if(p>end || n>end-p) throw std::runtime_error("animation: truncated track");
    }
    uint8_t byte() {need(1);return bytes[p++];}
    uint16_t word() {need(2);const auto v=u16(bytes.data()+p);p+=2;return v;}
    uint32_t dword() {need(4);const auto v=u32(bytes.data()+p);p+=4;return v;}
    int16_t signedWord() {return std::bit_cast<int16_t>(word());}
    float real() {return std::bit_cast<float>(dword());}
    std::string string() {
        const size_t start=p;
        while(p<end && bytes[p]) ++p;
        if(p==end) throw std::runtime_error("animation: unterminated bone name");
        std::string result(reinterpret_cast<const char*>(bytes.data()+start),p-start);
        ++p;
        return result;
    }
};

Track parseTrack(const std::vector<uint8_t>& image,size_t start,size_t end) {
    Cursor c{image,start,end};
    Track track;
    track.boneIndex=c.dword();
    track.parentIndex=std::bit_cast<int32_t>(c.dword());
    track.boneName=c.string();
    c.byte(); // pre-FPS flag
    track.fps=c.real();
    track.frameCount=c.dword();
    c.need(4);c.p+=4; // channel flags
    track.positionFactor=c.real();
    track.scalingFactor=c.real();
    if(c.p<end) {
        const uint16_t count=c.word();
        if(size_t(count)>(end-c.p)/16) throw std::runtime_error("animation: short rotation pool");
        track.rotations.reserve(count);
        for(uint16_t i=0;i<count;++i)
            track.rotations.push_back({c.real(),c.real(),c.real(),c.real()});
    }
    if(c.p<end) {
        const uint16_t count=c.word();
        const bool wide=track.rotations.size()>255;
        if(size_t(count)>(end-c.p)/(wide?2:1))
            throw std::runtime_error("animation: short rotation palette");
        track.rotationPalette.reserve(count);
        for(uint16_t i=0;i<count;++i) {
            const uint16_t index=wide?c.word():c.byte();
            if(index>=track.rotations.size())
                throw std::runtime_error("animation: rotation palette out of bounds");
            track.rotationPalette.push_back(index);
        }
    }
    if(c.p<end) {
        const uint16_t count=c.word();
        if(size_t(count)>(end-c.p)/6) throw std::runtime_error("animation: short position pool");
        track.positions.reserve(count);
        for(uint16_t i=0;i<count;++i)
            track.positions.push_back({c.signedWord(),c.signedWord(),c.signedWord()});
    }
    if(c.p<end) {
        const uint16_t count=c.word();
        const bool wide=track.positions.size()>255;
        if(size_t(count)>(end-c.p)/(wide?2:1))
            throw std::runtime_error("animation: short position palette");
        track.positionPalette.reserve(count);
        for(uint16_t i=0;i<count;++i) {
            const uint16_t index=wide?c.word():c.byte();
            if(index>=track.positions.size())
                throw std::runtime_error("animation: position palette out of bounds");
            track.positionPalette.push_back(index);
        }
    }
    if(c.p!=end) throw std::runtime_error("animation: trailing track bytes");
    if((!track.rotationPalette.empty() && track.rotationPalette.size()!=track.frameCount) ||
       (!track.positionPalette.empty() && track.positionPalette.size()!=track.frameCount))
        throw std::runtime_error("animation: palette/frame count mismatch");
    return track;
}

bool signature(const uint8_t* p,const char* s) {return std::memcmp(p,s,4)==0;}

size_t findChild(const std::vector<uint8_t>& image,size_t from,size_t end,bool object) {
    for(size_t p=from;p+8<=end;++p) {
        const uint8_t* at=image.data()+p;
        const bool known=object ?
            (signature(at,"XSEQ")||signature(at,"SEQ0")||signature(at,"AMSK")) :
            (signature(at,"HLPR")||signature(at,"AOBJ")||signature(at,"XALO"));
        if(known && u32(at+4)<=end-(p+8)) return p;
    }
    return end;
}

void chunks(const std::vector<uint8_t>& image,size_t start,size_t end,
            Animation& animation,bool helper,int depth) {
    if(depth>16) throw std::runtime_error("animation: excessive chunk nesting");
    size_t cursor=start;
    while(cursor+8<=end) {
        const uint8_t* at=image.data()+cursor;
        const uint32_t size=u32(at+4);
        if(size>end-(cursor+8)) {
            // Retail images have a short raw footer; a recognized chunk with
            // a bad size is corrupt, while other tail bytes are not chunks.
            if(signature(at,"ANRT")||signature(at,"AOBJ")||signature(at,"XSEQ")||
               signature(at,"SEQ0")||signature(at,"HLPR")||signature(at,"MVEC"))
                throw std::runtime_error("animation: chunk exceeds parent");
            break;
        }
        const size_t body=cursor+8,next=body+size;
        if(signature(at,"ANRT")) {
            if(size<5) throw std::runtime_error("animation: short ANRT");
            animation.cyclic=image[body]!=0;
            animation.duration=std::bit_cast<float>(u32(image.data()+body+1));
            const size_t child=findChild(image,body+5,next,false);
            if(child<next) chunks(image,child,next,animation,helper,depth+1);
        } else if(signature(at,"AOBJ")) {
            Cursor c{image,body,next};
            animation.rigName=c.string();
            const size_t child=findChild(image,c.p,next,true);
            if(child<next) chunks(image,child,next,animation,false,depth+1);
        } else if(signature(at,"HLPR")) {
            chunks(image,body,next,animation,true,depth+1);
        } else if(signature(at,"MVEC")) {
            if(size<12) throw std::runtime_error("animation: short MVEC");
            chunks(image,body+12,next,animation,helper,depth+1);
        } else if(signature(at,"XSEQ")||signature(at,"SEQ0")) {
            auto track=parseTrack(image,body,next);
            (helper?animation.helperTracks:animation.tracks).push_back(std::move(track));
        }
        cursor=next;
    }
}

template<class Pool>
size_t poolIndex(const Pool& pool,const std::vector<uint16_t>& palette,size_t frame) {
    if(pool.empty()) return 0;
    return palette.empty()?std::min(frame,pool.size()-1):palette[std::min(frame,palette.size()-1)];
}

} // namespace

Animation decode(std::span<const uint8_t> payload) {
    if(payload.size()<8) throw std::runtime_error("animation: short payload");
    std::vector<uint8_t> image;
    const uint32_t first=u32(payload.data());
    if(first==0x3e3e3e3eu) image.assign(payload.begin(),payload.end());
    else {
        if(first<12 || first>256u*1024u*1024u)
            throw std::runtime_error("animation: unreasonable image length");
        image=forge::lzo::decompress(payload.data()+4,payload.size()-4,first);
    }
    if(image.size()<16 || u32(image.data())!=0x3e3e3e3eu ||
       !signature(image.data()+4,"3DAF"))
        throw std::runtime_error("animation: missing 3DAF image header");
    size_t cursor=12;
    while(cursor<image.size() && image[cursor]) ++cursor;
    if(cursor==image.size()) throw std::runtime_error("animation: unterminated copyright");
    cursor=(cursor+4)&~size_t(3); // NUL plus 4-byte alignment
    if(cursor>image.size()) throw std::runtime_error("animation: short header padding");
    Animation animation;
    chunks(image,cursor,image.size(),animation,false,0);
    return animation;
}

Transform evaluate(const Track& track,double seconds) {
    Transform out;
    const double last=track.frameCount?double(track.frameCount-1):0.0;
    const double frame=std::clamp(std::isfinite(seconds) && std::isfinite(track.fps)?
                                  seconds*track.fps:0.0,0.0,last);
    const size_t first=size_t(frame),second=std::min(first+1,size_t(last));
    const float alpha=float(frame-first);
    if(!track.rotations.empty()) {
        auto a=track.rotations[poolIndex(track.rotations,track.rotationPalette,first)];
        auto b=track.rotations[poolIndex(track.rotations,track.rotationPalette,second)];
        float dot=0;for(size_t i=0;i<4;++i) dot+=a[i]*b[i];
        if(dot<0) for(float& value:b) value=-value;
        float norm=0;
        for(size_t i=0;i<4;++i) {
            out.rotation[i]=a[i]*(1-alpha)+b[i]*alpha;
            norm+=out.rotation[i]*out.rotation[i];
        }
        if(norm>0) {
            for(float& value:out.rotation) value/=std::sqrt(norm);
            out.hasRotation=true;
        } else out.rotation={0,0,0,1};
    }
    if(!track.positions.empty()) {
        const auto& a=track.positions[poolIndex(track.positions,track.positionPalette,first)];
        const auto& b=track.positions[poolIndex(track.positions,track.positionPalette,second)];
        const int16_t av[]={a.x,a.y,a.z},bv[]={b.x,b.y,b.z};
        for(size_t i=0;i<3;++i)
            out.position[i]=(float(av[i])*(1-alpha)+float(bv[i])*alpha)*track.positionFactor;
        out.hasPosition=true;
    }
    return out;
}

} // namespace forge::animation
