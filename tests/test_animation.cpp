#include "forge/animation.hpp"
#include "forge/big.hpp"
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
    uint32_t finish() const {return ~value;}
};

int main(int argc,char** argv) {
    try {
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
            Crc canonical;
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
                canonical.string(name);
                canonical.string(animation.rigName);
                canonical.byte(animation.cyclic?1:0);
                canonical.real(animation.duration);
                canonical.dword(uint32_t(animation.tracks.size()));
                canonical.dword(uint32_t(animation.helperTracks.size()));
                for(const auto& track:animation.tracks) canonical.track(track);
                for(const auto& track:animation.helperTracks) canonical.track(track);
                std::cout << name << ": " << animation.rigName << ", "
                          << animation.tracks.size() << " tracks\n";
            }
            if(canonical.finish()!=0x046e7276u)
                std::cerr << "animation CRC " << std::hex << canonical.finish()
                          << " expected 46e7276" << std::dec << '\n';
            require(names.size()==30 && totalTracks==1518 &&
                    canonical.finish()==0x046e7276u,
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
