#include "forge/physics.hpp"
#include "forge/lzo.hpp"
#include <charconv>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
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
    std::string_view string() {
        const auto start=pos;
        while(remaining())if(bytes[take(1)]==0)
            return {reinterpret_cast<const char*>(bytes.data()+start),pos-start-1};
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

// Native CParticleScriptParser::Parse 033388E0 and symbol lookup 0333CFB0.
// This deliberately supports a bounded subset with explicit parameter sets.
// Numbers are floats, booleans or local texture-coordinate indices; symbolic
// particle/vertex references resolve only within the current program.
void particleText(std::string_view text) {
    std::set<std::string> particles,vertices;
    size_t texCoords=0,lineNumber=0;
    std::istringstream lines{std::string(text)};
    std::string line;
    while(std::getline(lines,line)) {
        ++lineNumber;
        auto fail=[&](const std::string& why) {
            throw std::runtime_error("physics: cloth line "+std::to_string(lineNumber)+": "+why);
        };
        std::istringstream tokens(line);
        std::string command,token;
        if(!(tokens>>command))continue;
        std::map<std::string,std::string> parameters;
        while(tokens>>token) {
            const auto eq=token.find('=');
            if(eq==std::string::npos || eq==0 || eq+1==token.size() ||
               token.find('=',eq+1)!=std::string::npos)fail("unsupported parameter syntax");
            if(!parameters.emplace(token.substr(0,eq),token.substr(eq+1)).second)
                fail("duplicate parameter");
        }
        auto get=[&](const char* key) {
            const auto found=parameters.find(key);
            if(found==parameters.end())fail(std::string("missing ")+key);
            const auto value=found->second;parameters.erase(found);return value;
        };
        auto real=[&](const char* key) {
            const auto value=get(key);float number=0;
            const auto parsed=std::from_chars(value.data(),value.data()+value.size(),number);
            if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size() || !std::isfinite(number))
                fail(std::string("invalid float ")+key);
        };
        auto boolean=[&](const char* key) {
            const auto value=get(key);if(value!="0" && value!="1")fail(std::string("invalid boolean ")+key);
        };
        auto reference=[&](const char* key,bool particleOnly) {
            const auto name=get(key);
            if(!particles.contains(name) && (particleOnly || !vertices.contains(name)))
                fail("unknown local symbol "+name);
        };
        if(command=="Particle" || command=="Vertex") {
            const auto name=get("name");
            for(const unsigned char ch:name)
                if(!((ch>='A'&&ch<='Z') || (ch>='a'&&ch<='z') || (ch>='0'&&ch<='9') || ch=='_'))
                    fail("unsupported symbol spelling");
            if(particles.contains(name) || vertices.contains(name))fail("duplicate local symbol");
            real("x");real("y");real("z");
            if(command=="Particle") {real("alpha");particles.insert(name);}
            else vertices.insert(name);
        } else if(command=="TexCoord") {real("u");real("v");++texCoords;}
        else if(command=="Triangle") {
            for(const char* key:{"a","b","c"})reference(key,false);
            for(const char* key:{"ta","tb","tc"}) {
                const auto value=get(key);uint32_t index=0;
                const auto parsed=std::from_chars(value.data(),value.data()+value.size(),index);
                if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size() || index>=texCoords)
                    fail("texture coordinate index exceeds local table");
            }
        } else if(command=="Distance") {
            reference("a",true);reference("b",true);real("dist");real("strength");
        } else if(command=="SetRenderMode")boolean("bezier");
        else if(command=="SetDragging") {boolean("enable");boolean("rotational");real("strength");}
        else if(command=="SetAcceleration") {boolean("enable");real("damping");}
        else if(command=="SetWindStrength" || command=="SetGravityStrength")real("strength");
        else fail("unsupported command "+command);
        if(!parameters.empty())fail("unsupported parameter "+parameters.begin()->first);
    }
}

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
        } else if(tag=="CLTH") {
            // Native 034BD940: version, name, local material, source and compiled flag.
            if(c.word()!=2)throw std::runtime_error("physics: unsupported cloth version");
            c.string();c.take(4);const auto source=c.string();
            if(c.bytes[c.take(1)]!=0)throw std::runtime_error("physics: compiled cloth is unsupported");
            particleText(source);children=true;
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
