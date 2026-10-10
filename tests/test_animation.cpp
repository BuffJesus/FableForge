#include "forge/animation.hpp"
#include "forge/big.hpp"
#include "forge/lzo.hpp"
#include "forge/lipsync_preset.hpp"

#include <filesystem>
#include <bit>
#include <iostream>
#include <set>
#include <cmath>
#include <stdexcept>

static void require(bool okay,const char* message) {
    if(!okay) throw std::runtime_error(message);
}

struct Crc {
    uint32_t value=0xffffffffu;
    void byte(uint8_t b) {
        value^=b;
        for(int i=0;i<8;++i) value=(value>>1)^((value&1)?0xedb88320u:0);
    }
    void word(uint16_t v) {byte(uint8_t(v));byte(uint8_t(v>>8));}
    void dword(uint32_t v) {for(int i=0;i<32;i+=8) byte(uint8_t(v>>i));}
    void real(float v) {dword(std::bit_cast<uint32_t>(v));}
    void string(const std::string& s) {for(unsigned char c:s) byte(c);byte(0);}
    void track(const forge::animation::Track& t) {
        dword(t.boneIndex);dword(uint32_t(t.parentIndex));string(t.boneName);
        real(t.fps);dword(t.frameCount);real(t.positionFactor);real(t.scalingFactor);
        word(uint16_t(t.rotations.size()));
        for(const auto& q:t.rotations) for(float v:q) real(v);
        word(uint16_t(t.rotationPalette.size()));
        for(uint16_t v:t.rotationPalette) word(v);
        word(uint16_t(t.positions.size()));
        for(const auto& p:t.positions) {word(uint16_t(p.x));word(uint16_t(p.y));word(uint16_t(p.z));}
        word(uint16_t(t.positionPalette.size()));
        for(uint16_t v:t.positionPalette) word(v);
    }
    void animation(const std::string& name,const forge::animation::Animation& a) {
        string(name);string(a.rigName);byte(a.cyclic?1:0);real(a.duration);
        dword(uint32_t(a.tracks.size()));dword(uint32_t(a.helperTracks.size()));
        for(const auto& t:a.tracks) track(t);
        for(const auto& t:a.helperTracks) track(t);
    }
    uint32_t finish() const {return ~value;}
};

using Bytes=std::vector<uint8_t>;
static void word(Bytes& bytes,uint32_t value) {
    for(unsigned i=0;i<4;++i) bytes.push_back(uint8_t(value>>(i*8)));
}
static void append(Bytes& bytes,const Bytes& more) {bytes.insert(bytes.end(),more.begin(),more.end());}
static Bytes chunk(const char* tag,const Bytes& body) {
    Bytes bytes(tag,tag+4);word(bytes,uint32_t(body.size()));append(bytes,body);return bytes;
}
static Bytes trackChunk(const char* name) {
    Bytes bytes;word(bytes,0);word(bytes,UINT32_MAX);
    while(*name)bytes.push_back(uint8_t(*name++));bytes.push_back(0);
    bytes.push_back(0);word(bytes,std::bit_cast<uint32_t>(30.0f));
    word(bytes,0);word(bytes,0);word(bytes,0);word(bytes,0);
    return chunk("XSEQ",bytes);
}
static Bytes image(const Bytes& children) {
    Bytes bytes;word(bytes,0x3e3e3e3e);append(bytes,Bytes{'3','D','A','F'});
    word(bytes,0);word(bytes,0); // empty copyright and alignment
    Bytes root{1};word(root,std::bit_cast<uint32_t>(1.0f));append(root,children);
    append(bytes,chunk("ANRT",root));word(bytes,0);return bytes;
}
static void nativeBoundaries() {
    Bytes object{'R','I','G',0};word(object,UINT32_MAX);
    append(object,chunk("JUNK",trackChunk("DECOY")));
    append(object,trackChunk("REAL"));
    const auto bytes=image(chunk("AOBJ",object));
    const auto parsed=forge::animation::decode(bytes);
    require(parsed.rigName=="RIG" && parsed.tracks.size()==1 &&
            parsed.tracks[0].boneName=="REAL","opaque child signature became a track");
    Bytes children=chunk("JUNK",chunk("AOBJ",object));
    append(children,chunk("AOBJ",object));
    require(forge::animation::decode(image(children)).tracks.size()==1,
            "opaque root child signature became an object");
    bool rejected=false;
    try {forge::animation::decode(image(chunk("AOBJ",Bytes{'R',0})));}
    catch(const std::runtime_error&) {rejected=true;}
    require(rejected,"missing native next-sibling word accepted");
    auto refuses=[](const Bytes& candidate) {
        try {forge::animation::validateStorage(candidate);return false;}
        catch(const std::runtime_error&) {return true;}
    };
    require(refuses(bytes),"strict storage validation accepted opaque child");
    Bytes validObject{'R',0};word(validObject,UINT32_MAX);append(validObject,trackChunk("REAL"));
    Bytes mask;word(mask,2);mask.push_back(0);mask.push_back(1);
    append(validObject,chunk("AMSK",mask));
    Bytes hint;word(hint,1);word(hint,4096);
    Bytes validChildren=chunk("AOBJ",validObject);append(validChildren,chunk("XALO",hint));
    Bytes event{'E',0};word(event,std::bit_cast<uint32_t>(0.5f));
    append(validChildren,chunk("HLPR",chunk("TMEV",event)));
    auto valid=image(validChildren);const auto unchanged=valid;
    forge::animation::validateStorage(valid);
    require(valid==unchanged,"storage validation changed input");
    Bytes compressed;word(compressed,uint32_t(valid.size()));
    append(compressed,forge::lzo::compress(valid.data(),valid.size()));
    forge::animation::validateStorage(compressed);
    compressed.push_back(0);require(refuses(compressed),"compressed trailing data accepted");
    auto noFooter=valid;noFooter.resize(noFooter.size()-4);
    forge::animation::validateStorage(noFooter);
    valid.back()=1;require(refuses(valid),"unknown image footer accepted");
    valid=unchanged;valid.push_back(0);require(refuses(valid),"extra image byte accepted");
    auto shortParent=validChildren;shortParent.push_back(0);
    require(refuses(image(shortParent)),"short parent tail accepted");
    mask.back()=2;require(refuses(image(chunk("AMSK",mask))),"invalid mask boolean accepted");
    mask.pop_back();require(refuses(image(chunk("AMSK",mask))),"short mask accepted");
    hint[0]=2;require(refuses(image(chunk("XALO",hint))),"unknown allocation version accepted");
    event.push_back(0);require(refuses(image(chunk("TMEV",event))),"trailing event byte accepted");
    valid=unchanged;valid[16]='J';require(refuses(valid),"unknown root accepted");
}

int main(int argc,char** argv) {
    try {
        nativeBoundaries();
        bool rejected=false;
        try {forge::animation::decode({});}
        catch(const std::runtime_error&) {rejected=true;}
        require(rejected,"empty animation accepted");
        forge::animation::Track interpolation;
        interpolation.fps=2;interpolation.frameCount=2;
        interpolation.positionFactor=0.5f;
        interpolation.rotations={{{0,0,0,1}},{{0,0,1,0}}};
        interpolation.rotationPalette={0,1};
        interpolation.positions={{0,0,0},{10,0,0}};
        interpolation.positionPalette={0,1};
        const auto midway=forge::animation::evaluate(interpolation,0.25);
        require(midway.hasRotation && midway.hasPosition &&
                std::abs(midway.rotation[2]-0.70710678f)<1e-5f &&
                std::abs(midway.rotation[3]-0.70710678f)<1e-5f &&
                std::abs(midway.position[0]-2.5f)<1e-5f,
                "animation palette interpolation");
        if(argc>1) {
            const auto archive=forge::big::File::open(std::filesystem::path(argv[1]));
            const auto* bank=archive.findBank("MBANK_ALLMESHES");
            require(bank,"graphics mesh bank missing");
            std::set<std::string> names;
            size_t totalTracks=0;
            size_t legacyNames=0,legacyTracks=0;
            Crc canonical,legacy;
            for(const auto& preset:forge::lipsync::headPresets())
                for(const auto& track:preset.tracks) names.insert(track.animation);
            for(const auto& name:names) {
                const forge::big::Entry* entry=nullptr;
                for(const auto& candidate:bank->entries) if(candidate.name==name) {
                    require(!entry,"duplicate animation name");entry=&candidate;
                }
                require(entry && entry->type==9,"phoneme animation missing or wrong type");
                const auto animation=forge::animation::decode(archive.entryData(*entry));
                require(!animation.tracks.empty(),"phoneme animation has no tracks");
                totalTracks+=animation.tracks.size();
                canonical.animation(name,animation);
                if(!name.starts_with("ANIM_ORACLE_")) {
                    ++legacyNames;legacyTracks+=animation.tracks.size();
                    legacy.animation(name,animation);
                }
                std::cout << name << ": " << animation.rigName << ", "
                          << animation.tracks.size() << " tracks\n";
            }
            // Independent parse_anim_xseq.py validation preserves the five-head
            // reference and adds the Oracle's six clips (96 object tracks).
            require(legacyNames==30 && legacyTracks==1518 && legacy.finish()==0x046e7276u,
                    "original retail phoneme tracks differ from independent Python parser");
            if(canonical.finish()!=0xfbeecd06u)
                std::cerr << "animation CRC " << std::hex << canonical.finish()
                          << " expected fbeecd06" << std::dec << '\n';
            require(names.size()==36 && totalTracks==1614 &&
                    canonical.finish()==0xfbeecd06u,
                    "retail phoneme tracks differ from independent Python parser");
            std::cout << "animation: " << names.size() << " unique phoneme assets, "
                      << totalTracks << " tracks, CRC " << std::hex
                      << canonical.finish() << std::dec << '\n';
            if(argc>2 && std::string(argv[2])=="--all") {
                size_t checked=0,empty=0,allTracks=0,allHelpers=0;
                for(const auto& entry:bank->entries) if(entry.type==6 || entry.type==7 || entry.type==9) {
                    try {
                        const auto parsed=forge::animation::decode(archive.entryData(entry));
                        if(parsed.tracks.empty() && parsed.helperTracks.empty()) ++empty;
                        allTracks+=parsed.tracks.size();allHelpers+=parsed.helperTracks.size();
                    } catch(const std::exception& ex) {
                        throw std::runtime_error("animation "+entry.name+": "+ex.what());
                    }
                    ++checked;
                }
                require(checked==3435 && allTracks==210743 && allHelpers==2985 && !empty,
                        "retail animation corpus differs from independent Python reader");
                std::cout << "animation: " << checked << " retail entries parsed, "
                          << empty << " without tracks; " << allTracks << " tracks, "
                          << allHelpers << " helpers\n";
            }
        } else std::cout << "animation: malformed fixture passed\n";
        return 0;
    } catch(const std::exception& ex) {
        std::cerr << "animation: " << ex.what() << '\n';
        return 1;
    }
}
