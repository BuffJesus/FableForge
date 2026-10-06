#include "forge/stbstorage.hpp"
#include "forge/stbbake.hpp"
#include "forge/rangecodec.hpp"
#include <span>
#include <stdexcept>

namespace forge::stbstorage {
namespace {
struct Reader {
    std::span<const uint8_t> b;
    size_t p=0;
    size_t take(size_t n) {
        if(n>b.size()-p)throw std::runtime_error("stbstorage: truncated field");
        const auto at=p;p+=n;return at;
    }
    uint8_t byte() {return b[take(1)];}
    uint16_t half() {const auto at=take(2);return uint16_t(b[at])|uint16_t(b[at+1])<<8;}
    uint32_t word() {
        const auto at=take(4);
        return uint32_t(b[at])|uint32_t(b[at+1])<<8|uint32_t(b[at+2])<<16|uint32_t(b[at+3])<<24;
    }
    bool flag() {
        const auto v=byte();if(v>1)throw std::runtime_error("stbstorage: invalid boolean");return v!=0;
    }
    void range(size_t count,size_t stride) {
        const size_t n=word(),at=take(n);
        if(count>1000000 || stride>128 || count*stride>64000000)
            throw std::runtime_error("stbstorage: excessive range array");
        (void)rangecodec::decodeExact(b.data()+at,n,count,stride);
    }
    void finish() const {
        if(p!=b.size())throw std::runtime_error("stbstorage: trailing frame bytes");
    }
    void foregroundWater() {
        // CEngineWaterSubPatch::Load 02E673B0: coords, height, type and
        // 17x17 packed water vertices (PDB stride 66, numeric wave/depth fields).
        take(12);if(word()>8)throw std::runtime_error("stbstorage: unknown foreground water type");
        range(289,66);
    }
    void backgroundTrailer() {
        // CPatchTesselationEdgeStrip::Load 02E03980 and array helpers
        // 02E045F0/02E046F0/02E047F0/02E048F0.
        for(int side=0;side<4;++side) {
            take(4);flag();
            for(const size_t stride:{20,16,60,56}) {
                const auto count=word();if(count)range(count,stride);
            }
        }
        if(flag()) {
            const auto vertices=half(),triangles=half();
            const auto type=word(),stride=word();
            // Background water Load/Save 02E052B0/02E055E0.
            const size_t expected=(type==1||type==2||type==6||type==8)?56:(type>=3&&type<=5)?12:0;
            if(!expected || stride!=expected)throw std::runtime_error("stbstorage: unknown background water layout");
            range(vertices,stride);if(triangles)range(size_t(triangles)*3,2);
        }
    }
};
}
void validateForeground(const std::vector<uint8_t>& body) {
    const auto frame=stbbake::parseForegroundFrame(body);
    if(stbbake::serializeForegroundFrame(frame)!=body)
        throw std::runtime_error("stbstorage: foreground storage is not exact");
    Reader water{frame.waterPayload};
    if(frame.hasWater)water.foregroundWater();
    water.finish();
}
void validateBackground(const std::vector<uint8_t>& body) {
    // Native Load/Save 02CDF860/02CE3220. After loading vertices, Load
    // sets SharedIndexBuffer to (detailMode == 1), overriding the constructor.
    Reader c{body};c.take(8);const bool waterOnly=c.flag();const auto detailMode=c.byte();
    if(!waterOnly) {
        const auto triangles=c.half(),vertices=c.half();c.take(2);
        if(!c.flag())throw std::runtime_error("stbstorage: unsupported uncompressed inline texture");
        const auto textureStart=c.p;
        const auto width=c.half(),height=c.half();
        if(!width || !height || width%4 || height%4)
            throw std::runtime_error("stbstorage: unsupported inline texture dimensions");
        c.take(15+size_t(width)*height/2);
        const std::vector<uint8_t> bytes(body.begin()+textureStart,body.begin()+c.p);
        const auto texture=stbbake::parseInlineTexture(bytes);
        if(texture.levels!=1 || texture.pixelFormat0!=1027 || texture.pixelFormat1 ||
           texture.usage || texture.surfacePool!=1 || stbbake::serializeInlineTexture(texture)!=bytes)
            throw std::runtime_error("stbstorage: unsupported inline texture layout");
        c.range(vertices,16);if(detailMode!=1)c.range(size_t(triangles)*3,2);
    }
    c.backgroundTrailer();c.finish();
}
std::vector<uint32_t> validateLocalDetail(const std::vector<uint8_t>& body) {
    Reader c{body};
    auto count=[&](size_t minimum) {
        const auto n=c.word();
        if(n>(body.size()-c.p)/minimum)throw std::runtime_error("stbstorage: local-detail count exceeds frame");
        return size_t(n);
    };
    const auto collections=count(12);std::vector<uint32_t> paletteIndices;
    paletteIndices.reserve(collections);
    // Collection Load 02E35E70; primitives 02EDD230, 02EDDEB0, 02EDF3D0.
    for(size_t collection=0;collection<collections;++collection) {
        if(c.word()>1)throw std::runtime_error("stbstorage: invalid render-update flag");
        paletteIndices.push_back(c.word());
        const auto primitives=count(44);
        for(size_t primitive=0;primitive<primitives;++primitive) {
            const auto kind=c.word();c.take(40); // AABB and bounding sphere
            if(kind==0) {c.take(52);continue;} // matrix and scale floats
            if(kind>2)throw std::runtime_error("stbstorage: unknown local-detail primitive");
            const auto objects=count(16);
            if(!objects)throw std::runtime_error("stbstorage: empty repeated object array");
            if(kind==2) {
                c.take(objects*68); // CObject matrix, scale, bounding sphere
                // Native allocates CObjectEntry in NEnginePrimitiveManagerRepeatedZSprites:
                // PDB size 16: WorldPos C3DVector + WorldSphereRadius float.
                c.take(objects*16);continue;
            }
            c.take(4+objects*32); // maximum scale and two vector arrays
            if(c.flag())c.take(((objects+3)&~size_t(3))*12); // padded normals
            if(c.flag())c.take(objects); // wind delays
            if(c.flag()) {
                const auto sections=count(80);
                if(!sections)throw std::runtime_error("stbstorage: empty subsection array");
                // Native Save 02EE1D70 writes CSubsectionElement[sections]. PDB:
                // 64 sphere-float bytes, 12 local count/index bytes, four Pad bytes.
                c.take(sections*80);
            }
        }
    }
    c.finish();return paletteIndices;
}
}
