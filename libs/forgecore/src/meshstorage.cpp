#include "forge/meshstorage.hpp"
#include "forge/lzo.hpp"
#include <algorithm>
#include <cstring>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace forge::meshstorage {
namespace {
constexpr size_t maxBlock=128*1024*1024;
size_t product(size_t a,size_t b) {
    if(b && a>maxBlock/b)throw std::runtime_error("meshstorage: excessive array size");
    return a*b;
}
void clothProgram(std::span<const uint8_t>);
struct Reader {
    std::span<const uint8_t> b;
    size_t p=0,decodedBytes=0;
    void take(size_t n,const char* =nullptr) {
        if(n>b.size()-p)throw std::runtime_error("meshstorage: truncated field");
        p+=n;
    }
    uint8_t u8(const char* =nullptr) {const auto at=p;take(1);return b[at];}
    uint16_t u16(const char* =nullptr) {const auto at=p;take(2);return uint16_t(b[at])|uint16_t(b[at+1])<<8;}
    uint32_t u32(const char* =nullptr) {
        const auto at=p;take(4);
        return uint32_t(b[at])|uint32_t(b[at+1])<<8|uint32_t(b[at+2])<<16|uint32_t(b[at+3])<<24;
    }
    bool boolean() {
        const auto v=u8();if(v>1)throw std::runtime_error("meshstorage: invalid boolean");return v!=0;
    }
    size_t count(const char* label,size_t maximum=100000) {
        const auto v=u32();if(v>maximum)throw std::runtime_error(std::string("meshstorage: excessive ")+label);return v;
    }
    void string() {
        while(p<b.size())if(!u8())return;
        throw std::runtime_error("meshstorage: unterminated string");
    }
    void block(size_t expected,std::string_view domain) {
        if(!expected)return;
        if(expected<3 || expected>maxBlock || decodedBytes>2*maxBlock-expected)
            throw std::runtime_error("meshstorage: unsupported compressed block size");
        decodedBytes+=expected;
        std::vector<uint8_t> decoded(expected);
        const size_t target=expected-3;size_t total=0;
        while(total<target) {
            size_t n=u16();if(n==0xffff)n=u32();
            if(!n) {
                const auto at=p;take(target-total);
                std::memcpy(decoded.data()+total,b.data()+at,target-total);total=target;
            } else {
                const auto at=p;take(n);
                auto out=forge::lzo::decompressBounded(b.data()+at,n,target-total);
                if(out.empty() || out.size()>target-total || !forge::lzo::tryDecompress(b.data()+at,n,out))
                    throw std::runtime_error("meshstorage: incomplete LZO block");
                std::memcpy(decoded.data()+total,out.data(),out.size());total+=out.size();
            }
        }
        const auto tail=p;take(3);std::memcpy(decoded.data()+target,b.data()+tail,3);
        if(domain=="cloth program")clothProgram(decoded);
        else if(domain=="helper name strings" || domain=="bone name strings") {
            Reader names{decoded};while(names.p<names.b.size())names.string();
        }
    }
    void finish() const {
        if(p!=b.size())throw std::runtime_error("meshstorage: trailing storage bytes");
    }
};
// CParticleProgram::Read 033351A0; simulation 03332C50 and constraint
// execution 03335CB0 establish local particle indices and float parameters.
void clothProgram(std::span<const uint8_t> bytes) {
 Reader c{bytes};
 if(c.u32("cloth version")!=333)throw std::runtime_error("unsupported cloth version");
 const auto constraints=c.count("constraint stream length",128*1024*1024);
 if(constraints>bytes.size()-c.p)throw std::runtime_error("constraint stream exceeds program");
 const size_t constraintEnd=c.p+constraints;size_t loops=0;std::vector<uint32_t> constraintIndices;
 while(c.p<constraintEnd) {
  if(constraintEnd-c.p<12)throw std::runtime_error("truncated constraint header");
  const auto opcode=c.u32("constraint opcode"),count=c.u32("constraint count"),size=c.u32("constraint payload bytes");
  if(size>constraintEnd-c.p)throw std::runtime_error("constraint payload exceeds stream");
  if(opcode<2) {
   if(size)throw std::runtime_error("control constraint has payload");
   if(opcode==0) {if(!count)throw std::runtime_error("zero constraint repeat count");++loops;}
   else {if(!loops||count!=1)throw std::runtime_error("unbalanced constraint loop");--loops;}
  }else {
   if(opcode>5||uint64_t(count)*16!=size)throw std::runtime_error("unknown constraint opcode/record size");
   for(size_t i=0;i<count;++i) {
    const size_t indices=opcode==5?0:opcode==4?3:2;
    for(size_t j=0;j<indices;++j)constraintIndices.push_back(c.u32("local constraint particle index"));
    c.take(16-indices*4,"constraint distance/strength/sphere floats");
   }
  }
 }
 if(loops)throw std::runtime_error("unterminated constraint loop");
 c.take(4,"simulation timestep float");c.u32("simulation timestep-changed boolean");c.take(4,"simulation timestep multiplier float");
 const auto particles=c.count("simulation particle count",1000000);
 for(const auto index:constraintIndices)if(index>=particles)throw std::runtime_error("constraint particle index exceeds simulation");
 c.take(particles*12,"simulation position floats");c.take(particles*4,"simulation alpha floats");
 c.take(8,"gravity/wind floats");c.take(2,"drag boolean flags");c.take(4,"drag float");c.take(1,"acceleration boolean flag");c.take(4,"global damping float");
 const auto triangles=c.count("cloth triangle count",1000000);c.take(triangles*6,"cloth triangle uint16 indices");
 const auto quads=c.count("cloth quad count",1000000);c.take(quads*8,"cloth quad uint16 indices");
 const auto vertices=c.count("cloth render vertex count",1000000);c.take(vertices*4,"cloth position/UV uint16 indices");
 const auto nonSim=c.count("non-simulated particle count",1000000);c.take(nonSim*12,"non-simulated position floats");
 const auto uv=c.count("cloth UV count",1000000);c.take(uv*8,"cloth UV floats");
 const auto particleIndices=c.count("particle index count",1000000);c.take(particleIndices*4,"local particle indices");
 const auto vertexIndices=c.count("vertex index count",1000000);c.take(vertexIndices*4,"local vertex indices");
 c.take(4,"average patch size float");c.take(1,"Bezier boolean flag");
 if(c.count("exported particle count"))throw std::runtime_error("meshstorage: exported cloth particles are unsupported");
 for(int k=0;k<2;++k) {
  const auto groups=c.count("cloth skin group count");for(size_t g=0;g<groups;++g) {
   c.u32("local skin bone index");const auto blends=c.count("skin vertex blend count",1000000);
   for(size_t j=0;j<blends;++j){c.u32("local skin vertex index");c.take(4,"skin blend float");}
  }
 }
 if(c.p!=bytes.size())throw std::runtime_error("cloth program has trailing bytes");
 
}

// Stats LoadBinary 031E53B0 and InitialiseFromFileData 031E2E40: helper
// records are name CRC + position + local bone index (PDB pair size 20).
// Other geometry layouts agree with MeshParser.h and meshpreview.cpp.
void mesh(std::span<const uint8_t> bytes) {
 Reader c{bytes};c.string();c.boolean();c.take(40,"sphere/AABB floats");
 const size_t hp=c.u16("helper count"),dummy=c.u16("dummy count"),names=c.u16("helper names length"),volumes=c.u16("volume count"),generators=c.u16("generator count");
 if(hp)c.block(hp*20,"helper name CRC, position floats and local bone index");if(dummy)c.block(dummy*56,"dummy records");if(names)c.block(names,"helper name strings");
 if(volumes || generators)throw std::runtime_error("meshstorage: volumes/generators are unsupported");
 const auto materials=c.count("material count"),primitives=c.count("primitive count"),bones=c.count("bone count"),boneNames=c.count("bone name bytes",128*1024*1024);
 c.boolean();c.take(4,"total geometry block counts");
 if(bones) {c.take(bones*2,"bone uint16 indices");c.block(boneNames,"bone name strings");c.block(bones*60,"bone records");c.block(bones*48,"bone keyframe floats");c.block(bones*64,"bone matrix floats");}
 c.take(48,"root matrix floats");
 for(size_t i=0;i<materials;++i) {
  c.u32("local material index");c.string();c.take(20,"material texture/decal IDs");c.take(8,"material render flags");for(int j=0;j<4;++j)c.boolean();
  if(c.boolean())for(int j=0;j<4;++j)c.string();
 }
 for(size_t i=0;i<primitives;++i) {
  c.u32("local material index");const auto repeats=c.count("repeat count");c.take(20,"primitive sphere/stretch floats");
  const auto statics=c.count("static block count"),animated=c.count("animated block count"),vertices=c.count("vertex count",10000000);
  c.count("triangle count",30000000);const auto indices=c.count("index count",30000000);const auto format=c.u32("vertex format flags");c.take(8,"duplicate block counts");
  for(size_t j=0;j<statics;++j)c.take(15,"static geometry block counts/indices/flags");
  for(size_t j=0;j<animated;++j){c.take(18,"animated geometry block counts/indices/flags");const auto groups=c.u8("bone palette count");c.take(groups,"bone palette bytes");}
  c.take(32,"vertex compression floats");const auto stride=c.u32("vertex stride");c.u32("vertex buffer type");
  // Restrict accepted vertex domains to the canonical preview's known layouts.
  const std::set<std::pair<uint32_t,uint32_t>> layouts{{12,4},{20,4},{20,6},{20,20},{28,6},{28,20},{28,22},{36,4},{36,22}};
  if(!layouts.contains({stride,format}))throw std::runtime_error("meshstorage: unsupported vertex layout");
  const size_t reps=std::max(size_t(1),repeats);
  if(vertices&&stride)c.block(product(product(stride,vertices),reps),"vertex geometry bytes");
  else if(c.p+2<=bytes.size()&&!bytes[c.p]&&!bytes[c.p+1])c.take(2,"empty vertex buffer marker");
  if(indices)c.block(product(product(indices,2),reps),"triangle uint16 indices");
  else if(c.p+2<=bytes.size()&&!bytes[c.p]&&!bytes[c.p+1])c.take(2,"empty index buffer marker");
  const auto cloth=c.count("cloth primitive count");for(size_t j=0;j<cloth;++j){c.take(8,"cloth primitive/material indices");const auto n=c.count("cloth program length",128*1024*1024);if(n)c.block(n,"cloth program");}
 }
 c.finish();
}

} // namespace
void validateStorage(std::span<const uint8_t> info,std::span<const uint8_t> payload) {
    Reader meta{info};
    meta.u32("physics asset ID");meta.take(40,"bounds floats");
    const auto lods=meta.count("LOD count",8);
    if(!lods)throw std::runtime_error("meshstorage: empty LOD list");
    std::vector<size_t> sizes;
    for(size_t i=0;i<lods;++i) {
        const auto n=meta.u32();if(!n)throw std::runtime_error("meshstorage: empty LOD");sizes.push_back(n);
    }
    meta.take(lods*4,"safe radius and LOD error floats");
    const auto textures=meta.count("texture count");meta.take(textures*4,"texture asset IDs");meta.finish();
    size_t at=0;
    for(const auto n:sizes) {
        if(n>payload.size()-at)throw std::runtime_error("meshstorage: LOD exceeds payload");
        mesh(payload.subspan(at,n));at+=n;
    }
    // Retail stores one optional complete ghost LOD after the counted LODs.
    if(at<payload.size())mesh(payload.subspan(at));
}
} // namespace forge::meshstorage
