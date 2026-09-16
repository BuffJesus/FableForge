#include "forge/meshpreview.hpp"

#include "forge/big.hpp"
#include "forge/lzo.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace forge::meshpreview {
namespace {

struct Cursor {
    const std::vector<uint8_t>& b;
    size_t p = 0;
    void need(size_t n) const { if (p + n > b.size()) throw std::runtime_error("meshpreview: truncated payload"); }
    void skip(size_t n) { need(n); p += n; }
    uint8_t u8() { need(1); return b[p++]; }
    uint16_t u16() { need(2); uint16_t v; std::memcpy(&v,b.data()+p,2); p+=2; return v; }
    uint32_t u32() { need(4); uint32_t v; std::memcpy(&v,b.data()+p,4); p+=4; return v; }
    int32_t i32() { return static_cast<int32_t>(u32()); }
    std::string str() {
        const size_t s=p; while (p<b.size() && b[p]) ++p;
        if (p==b.size()) throw std::runtime_error("meshpreview: unterminated string");
        std::string r(reinterpret_cast<const char*>(b.data()+s),p-s); ++p; return r;
    }
};

std::vector<uint8_t> fableLzo(Cursor& c, size_t expected) {
    std::vector<uint8_t> out(expected);
    size_t total=0, target=expected>3?expected-3:0;
    while (total<target) {
        uint32_t comp=c.u16(); if (comp==0xffff) comp=c.u32();
        if (comp==0) {
            const size_t take=target-total; c.need(take);
            std::memcpy(out.data()+total,c.b.data()+c.p,take); c.p+=take; total+=take;
        } else {
            c.need(comp);
            auto dec=forge::lzo::decompressBounded(c.b.data()+c.p,comp,expected-total);
            if (dec.empty() || total+dec.size()>expected)
                throw std::runtime_error("meshpreview: invalid chunked LZO output");
            std::memcpy(out.data()+total,dec.data(),dec.size());
            c.p+=comp; total+=dec.size();
        }
    }
    if (expected>=3) { c.need(3); std::memcpy(out.data()+expected-3,c.b.data()+c.p,3); c.p+=3; }
    return out;
}

Vertex unpackPosition(uint32_t packed, const float* scale, const float* off) {
    int ix=int(packed&0x7ff), iy=int((packed>>11)&0x7ff), iz=int(packed>>22);
    if(ix&0x400)ix-=0x800; if(iy&0x400)iy-=0x800; if(iz&0x200)iz-=0x400;
    return {ix*(1.0f/1023.0f)*scale[0]+off[0],
            iy*(1.0f/1023.0f)*scale[1]+off[1],
            iz*(1.0f/511.0f)*scale[2]+off[2]};
}

struct VertexLayout {
    bool packedPosition;
    size_t normalOffset;
    bool packedNormal;
    size_t uvOffset;
    bool packedUv;
};

VertexLayout vertexLayout(uint32_t stride, uint32_t format) {
    // FableMod.Gfx.Integration SUBM::GetVertices dispatches exclusively on
    // these (vertex size, vertex format) pairs and throws for all others.
    if (stride == 12 && format == 4)  return {true,   4, true,   8, true};
    if (stride == 20 && format == 4)  return {true,  12, true,  16, true};
    if (stride == 20 && format == 6)  return {true,   4, true,   8, true};
    if (stride == 20 && format == 20) return {false, 12, true,  16, true};
    if (stride == 28 && format == 6)  return {true,  12, true,  16, true};
    if (stride == 28 && format == 20) return {false, 20, true,  24, true};
    if (stride == 28 && format == 22) return {false, 12, true,  16, true};
    if (stride == 36 && format == 4)  return {false, 12, false, 24, false};
    if (stride == 36 && format == 22) return {false, 20, true,  24, true};
    throw std::runtime_error("meshpreview: unknown compiled-model vertex format");
}

uint32_t nameCrc(std::string_view text) {
    // Engine: -1 - zlib::crc32(0xffffffff, bytes). zlib XORs the incoming
    // value on entry and the result on exit, so this is the raw polynomial
    // recurrence starting at zero with no final complement.
    uint32_t crc=0;
    for(unsigned char ch:text) {
        crc^=ch;
        for(int bit=0;bit<8;++bit) crc=(crc>>1)^(0xedb88320u&uint32_t(-(int32_t(crc&1))));
    }
    return crc;
}

struct Block { uint32_t count=0,start=0; bool strip=false; int32_t material=-1; };
void emit(const std::vector<uint16_t>& ib, const Block& b, std::vector<Triangle>& out) {
    if (b.strip) {
        uint32_t parity=b.start&1u;
        for(uint32_t k=0;k<b.count && size_t(b.start)+k+2<ib.size();++k) {
            uint32_t a=ib[b.start+k], d=ib[b.start+k+1], e=ib[b.start+k+2];
            if(a==d||d==e||a==e){++parity;continue;}
            Triangle t = parity%2 ? Triangle{a,d,e,b.material} : Triangle{a,e,d,b.material};
            ++parity;out.push_back(t);
        }
    } else {
        for(uint32_t k=0;k<b.count*3 && size_t(b.start)+k+2<ib.size();k+=3) {
            out.push_back({uint32_t(ib[b.start+k]),uint32_t(ib[b.start+k+2]),
                           uint32_t(ib[b.start+k+1]),b.material});
        }
    }
}

void appendFaces(std::vector<Triangle>& destination, std::vector<Triangle> faces,
                 uint32_t base, uint32_t vertexCount, bool normalizeMinimum) {
    uint32_t minimum=0;
    if(normalizeMinimum&&!faces.empty()) {
        minimum=std::numeric_limits<uint32_t>::max();
        for(const auto& t:faces)minimum=std::min({minimum,t.a,t.b,t.c});
    }
    for(auto& t:faces) {
        t.a-=minimum;t.b-=minimum;t.c-=minimum;
        if(t.a<vertexCount&&t.b<vertexCount&&t.c<vertexCount) {
            t.a+=base;t.b+=base;t.c+=base;destination.push_back(t);
        }
    }
}
} // namespace

Geometry decodeLod0(const std::vector<uint8_t>& payload, uint32_t meshType) {
    Cursor c{payload}; Geometry result;
    c.str(); c.u8(); c.skip(40);
    const uint16_t hp=c.u16(), hdmy=c.u16();
    const uint32_t names=c.u32(); const uint16_t headerTail=c.u16();
    if(hp) fableLzo(c,size_t(hp)*20);
    const auto helperBytes=hdmy?fableLzo(c,size_t(hdmy)*56):std::vector<uint8_t>{};
    const auto nameBytes=names?fableLzo(c,names):std::vector<uint8_t>{};
    (void)headerTail;
    std::vector<std::string> helperNames;
    if(nameBytes.size()>=2) {
        uint16_t boundary=0;std::memcpy(&boundary,nameBytes.data(),2);
        if(boundary>nameBytes.size())throw std::runtime_error("meshpreview: invalid helper-name boundary");
        size_t hpTerminators=0,hdmyTerminators=0;
        for(size_t p=2;p<nameBytes.size();++p)if(nameBytes[p]==0) {
            if(p<boundary)++hpTerminators;else ++hdmyTerminators;
        }
        const size_t hpNameCount=hpTerminators?hpTerminators-1:0;
        const size_t hdmyNameCount=hdmyTerminators?hdmyTerminators-1:0;
        size_t p=2;
        for(size_t i=0;i<hpNameCount;++i) {
            while(p<nameBytes.size()&&nameBytes[p])++p;
            if(p<nameBytes.size())++p;
        }
        if(p<nameBytes.size())++p;
        for(size_t i=0;i<hdmyNameCount&&p<nameBytes.size();++i) {
            size_t e=p;while(e<nameBytes.size()&&nameBytes[e])++e;
            if(e>p)helperNames.emplace_back(reinterpret_cast<const char*>(nameBytes.data()+p),e-p);
            p=e+1;
        }
    }
    for(uint16_t i=0;i<hdmy&&size_t(i+1)*56<=helperBytes.size();++i) {
        Helper h;const uint8_t* r=helperBytes.data()+size_t(i)*56;
        std::memcpy(&h.nameCrc,r,4);std::memcpy(h.matrix,r+4,48);std::memcpy(&h.bone,r+52,4);
        for(const auto& n:helperNames) if(nameCrc(n)==h.nameCrc){h.name=n;break;}
        result.helpers.push_back(std::move(h));
    }
    const int32_t materials=c.i32(), primitives=c.i32(), bones=c.i32(), boneNames=c.i32();
    if(materials<0||materials>10000||primitives<0||primitives>10000||bones<0||bones>10000||boneNames<0)
        throw std::runtime_error("meshpreview: unreasonable counts");
    result.boneCount=uint32_t(bones);result.primitiveCount=uint32_t(primitives);
    c.u8();c.u16();c.u16();
    if(bones){c.skip(size_t(bones)*2);fableLzo(c,boneNames);fableLzo(c,size_t(bones)*60);
              fableLzo(c,size_t(bones)*48);fableLzo(c,size_t(bones)*64);}
    c.skip(48);
    for(int32_t i=0;i<materials;++i){
        Material m; m.id=c.i32(); c.str(); c.i32(); m.diffuseTexture=c.i32();
        m.bumpTexture=c.i32(); m.reflectionTexture=c.i32(); m.alphaMapTexture=c.i32();
        m.textureFlags=c.u32(); m.glowStrength=c.u32(); m.unknown40=c.u8();
        m.alphaEnabled=c.u8()!=0; m.unknown42=c.u8();
        const uint8_t unknown43=c.u8(), unknown44=c.u8();
        m.unknown43=uint16_t(unknown43)|(uint16_t(unknown44)<<8);
        if(unknown44)for(int k=0;k<4;++k)c.str(); result.materials.push_back(m);
    }
    for(int32_t pi=0;pi<primitives;++pi) {
        const int32_t primitiveMaterialId=c.i32(); const int32_t repeated=c.i32(); c.skip(20);
        int32_t primitiveMaterial=-1;
        for(size_t materialIndex=0;materialIndex<result.materials.size();++materialIndex)
            if(result.materials[materialIndex].id==primitiveMaterialId) {
                primitiveMaterial=static_cast<int32_t>(materialIndex);break;
            }
        const uint32_t sbc=c.u32(),abc=c.u32(),vc=c.u32(),tc=c.u32(),ic=c.u32(),format=c.u32(); c.skip(8);
        if(sbc>100000||abc>100000||vc>10000000||ic>30000000)throw std::runtime_error("meshpreview: unreasonable primitive");
        std::vector<Block> staticBlocks,animatedBlocks;staticBlocks.reserve(sbc);animatedBlocks.reserve(abc);
        // A static block carries its OWN material index (EgoCore
        // GltfExporter uses CStaticBlock.MaterialIndex); the primitive's material is
        // only the fallback. Without this, multi-material meshes (roof + walls)
        // render entirely with material 0.
        for(uint32_t i=0;i<sbc;++i){Block b{c.u32(),c.u32(),c.u8()!=0,primitiveMaterial};c.skip(2);const int32_t bm=c.i32();if(bm>=0)b.material=bm;staticBlocks.push_back(b);}
        for(uint32_t i=0;i<abc;++i){Block b{c.u32(),c.u32(),c.u8()!=0,primitiveMaterial};c.skip(2);c.u32();c.u16();c.u8();uint8_t gc=c.u8();c.skip(gc);animatedBlocks.push_back(b);}
        float scale[4],off[4]; c.need(32);std::memcpy(scale,c.b.data()+c.p,16);std::memcpy(off,c.b.data()+c.p+16,16);c.p+=32;
        const uint32_t stride=c.u32();c.u32();
        const VertexLayout layout=vertexLayout(stride,format);
        const uint32_t reps=repeated>1?uint32_t(repeated):1;
        result.primitives.push_back({vc,ic,stride,format,reps});
        auto vb=vc&&stride?fableLzo(c,size_t(vc)*stride*reps):std::vector<uint8_t>{};
        auto ibraw=ic?fableLzo(c,size_t(ic)*2*reps):std::vector<uint8_t>{};
        std::vector<uint16_t> ib(ibraw.size()/2);if(!ib.empty())std::memcpy(ib.data(),ibraw.data(),ib.size()*2);
        const uint32_t base=uint32_t(result.vertices.size());
        // SUBM::GetVertices returns exactly VertexCount records.  A non-zero
        // repeat count enlarges the serialized buffers, but those additional
        // streams are not additional spatial vertices in the decoded mesh.
        for(uint32_t v=0;v<vc;++v){size_t o=size_t(v)*stride;if(o+(layout.packedPosition?4:12)>vb.size())break;
            Vertex p;if(layout.packedPosition){uint32_t q;std::memcpy(&q,vb.data()+o,4);p=unpackPosition(q,scale,off);}
            else std::memcpy(&p,vb.data()+o,12);
            if(o+layout.normalOffset+(layout.packedNormal?4:12)<=vb.size()){
                if(layout.packedNormal){uint32_t q;std::memcpy(&q,vb.data()+o+layout.normalOffset,4);int nx=int(q&0x7ff),ny=int((q>>11)&0x7ff),nz=int(q>>22);if(nx&0x400)nx-=0x800;if(ny&0x400)ny-=0x800;if(nz&0x200)nz-=0x400;p.nx=nx/1023.f;p.ny=ny/1023.f;p.nz=nz/511.f;}
                else std::memcpy(&p.nx,vb.data()+o+layout.normalOffset,12);
            }
            if(o+layout.uvOffset+(layout.packedUv?4:8)<=vb.size()){
                if(layout.packedUv){int16_t uv[2];std::memcpy(uv,vb.data()+o+layout.uvOffset,4);p.u=uv[0]/2048.f;p.v=uv[1]/2048.f;}
                else {float uv[2];std::memcpy(uv,vb.data()+o+layout.uvOffset,8);p.u=uv[0];p.v=uv[1];}
            } result.vertices.push_back(p);}
        std::vector<Triangle> staticFaces,animatedFaces;
        for(const auto& b:staticBlocks)emit(ib,b,staticFaces);
        auto& primitiveInfo=result.primitives.back();
        for(const auto& b:staticBlocks)primitiveInfo.oddStartStripBlocks+=b.strip&&(b.start&1u);
        for(const auto& b:animatedBlocks)primitiveInfo.oddStartStripBlocks+=b.strip&&(b.start&1u);
        if(!staticFaces.empty()){
            primitiveInfo.staticMinimumIndex=std::numeric_limits<uint32_t>::max();
            for(const auto& t:staticFaces)primitiveInfo.staticMinimumIndex=
                std::min({primitiveInfo.staticMinimumIndex,t.a,t.b,t.c});
        }
        appendFaces(result.triangles,std::move(staticFaces),base,vc,true);
        for(const auto& b:animatedBlocks)emit(ib,b,animatedFaces);
        appendFaces(result.triangles,std::move(animatedFaces),base,vc,false);
        const uint32_t cloth=c.u32();for(uint32_t i=0;i<cloth;++i){c.u32();c.u32();uint32_t n=c.u32();if(n)fableLzo(c,n);}
        (void)meshType;
    }
    return result;
}

Geometry readLod0(const std::filesystem::path& graphicsBig, uint32_t meshId) {
    const auto file=forge::big::File::open(graphicsBig);const auto* bank=file.findBank("MBANK_ALLMESHES");
    if(!bank)throw std::runtime_error("meshpreview: MBANK_ALLMESHES not found");
    for(const auto& e:bank->entries)if(e.id==meshId)return decodeLod0(file.entryData(e),e.type);
    throw std::runtime_error("meshpreview: mesh id not found");
}
} // namespace forge::meshpreview
