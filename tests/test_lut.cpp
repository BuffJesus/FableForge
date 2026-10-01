#include "forge/lut.hpp"
#include "forge/big.hpp"
#include "forge/lipsync.hpp"
#include "forge/xboxadpcm.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs=std::filesystem;

static void require(bool okay,const char* message) {
    if(!okay) throw std::runtime_error(message);
}
static void put16(std::vector<uint8_t>& out,uint16_t value) {
    out.push_back(uint8_t(value));out.push_back(uint8_t(value>>8));
}
static void put32(std::vector<uint8_t>& out,uint32_t value) {
    for(unsigned shift=0;shift<32;shift+=8) out.push_back(uint8_t(value>>shift));
}
static uint32_t pcmCrc(const std::vector<int16_t>& pcm) {
    uint32_t crc=0xffffffffu;
    for(const int16_t sample:pcm)
        for(const uint8_t byte:{uint8_t(sample),uint8_t(uint16_t(sample)>>8)}) {
            crc^=byte;
            for(int bit=0;bit<8;++bit) crc=(crc>>1)^((crc&1)?0xedb88320u:0);
        }
    return ~crc;
}

int main(int argc,char** argv) {
    try {
        std::vector<uint8_t> fixture;
        const char magic[]="LiOnHeAdLHAudioBankCompData";
        fixture.insert(fixture.end(),magic,magic+sizeof(magic));
        fixture.insert(fixture.end(),12,0);
        put32(fixture,80); // lookup marker at 124, 44 bytes past TocOffset
        require(fixture.size()==44,"fixture header length");
        put32(fixture,1);put32(fixture,0x56220001);put32(fixture,0x19c40);
        put32(fixture,0x01010000);put32(fixture,0x00647f06);
        put32(fixture,std::bit_cast<uint32_t>(1.5f));
        put32(fixture,std::bit_cast<uint32_t>(18.0f));
        put32(fixture,500);put32(fixture,0xffffffff);
        const std::array<uint8_t,44> wave={
            'R','I','F','F',36,0,0,0,'W','A','V','E',
            'f','m','t',' ',16,0,0,0,1,0,1,0,
            0x22,0x56,0,0,0x44,0xac,0,0,2,0,16,0,
            'd','a','t','a',0,0,0,0};
        fixture.insert(fixture.end(),wave.begin(),wave.end());
        require(fixture.size()==124,"fixture clip length");
        const char marker[]="LHAudioBankLookupTable";
        fixture.insert(fixture.end(),marker,marker+sizeof(marker));
        fixture.insert(fixture.end(),32-sizeof(marker),0);
        put32(fixture,20);put32(fixture,500);put32(fixture,1);
        put32(fixture,1);put32(fixture,80);put32(fixture,0);
        fixture.insert(fixture.end(),560,0);
        const auto path=fs::temp_directory_path()/
            ("fableforge_lut_"+std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count())+".lut");
        {
            std::ofstream out(path,std::ios::binary);
            require(bool(out),"cannot create fixture");
            out.write(reinterpret_cast<const char*>(fixture.data()),std::streamsize(fixture.size()));
            require(bool(out),"cannot write fixture");
        }
        const auto parsed=forge::lut::File::open(path);
        require(parsed.clips().size()==1 && parsed.find(1) && !parsed.find(2),"fixture index");
        const auto& clip=*parsed.find(1);
        require(clip.riffOffset==80 && clip.riffLength==44 && clip.formatTag==1 &&
                clip.channels==1 && clip.sampleRate==22050 && clip.dataBytes==0 &&
                parsed.riff(1)==std::vector<uint8_t>(wave.begin(),wave.end()),
                "fixture metadata and RIFF");
        bool unsupported=false;
        try {parsed.pcm16(1);}
        catch(const std::runtime_error&) {unsupported=true;}
        require(unsupported,"PCM fixture decoded as Xbox ADPCM");

        std::vector<uint8_t> mono={0xe8,0x03,20,0};
        for(int i=0;i<8;++i)
            mono.insert(mono.end(),{0x77,0x8f,0x35,0xe2});
        const auto monoPcm=forge::xboxadpcm::decode(mono,1,36);
        require(monoPcm.size()==64 && monoPcm[0]==1000 && monoPcm[1]==1093 &&
                monoPcm[3]==862 && monoPcm[17]==32767 &&
                pcmCrc(monoPcm)==0x709f1364u,"mono ADPCM fixture");
        std::vector<uint8_t> stereo={0,0,0,0,100,0,0,0};
        for(int group=0;group<8;++group) {
            stereo.insert(stereo.end(),4,0x11);
            stereo.insert(stereo.end(),4,0x88);
        }
        const auto stereoPcm=forge::xboxadpcm::decode(stereo,2,72);
        require(stereoPcm.size()==128 && stereoPcm[0]==0 && stereoPcm[1]==100 &&
                stereoPcm[2]==2 && stereoPcm[3]==100 &&
                pcmCrc(stereoPcm)==0xd0430465u,"stereo ADPCM fixture");
        bool partial=false;
        try {forge::xboxadpcm::decode(std::span<const uint8_t>(mono).first(35),1,36);}
        catch(const std::runtime_error&) {partial=true;}
        require(partial,"partial ADPCM block accepted");
        const auto pair=forge::lut::dialoguePair("ScriptDialogue2.lug","English");
        require(pair && pair->lutFilename=="ScriptDialogue2.lut" &&
                pair->lipsyncBank=="LIPSYNC_ENGLISH_SCRIPT_2" &&
                pair->sndsFilename=="scriptdialoguesnds2.bin" &&
                !forge::lut::dialoguePair("Unknown.lug","English") &&
                !forge::lut::dialoguePair("Dialogue.lug","English/Other"),
                "dialogue bank pairing");
        std::error_code cleanupError;
        fs::remove(path,cleanupError);

        if(argc>1) {
            const fs::path root=argv[1];
            struct Pair {const char* filename;size_t count;const char* lipsyncBank;
                         uint32_t probeId;size_t probeSamples;uint32_t probeCrc;};
            const std::array<Pair,4> banks={{{
                "Dialogue.lut",12134,"LIPSYNC_ENGLISH_MAIN",2,23488,0x36d4478eu}, {
                "Dialogue2.lut",1,"LIPSYNC_ENGLISH_MAIN_2",1,66176,0x5d0a8417u}, {
                "ScriptDialogue.lut",5310,"LIPSYNC_ENGLISH_SCRIPT",1,146944,0x56941543u}, {
                "ScriptDialogue2.lut",2769,"LIPSYNC_ENGLISH_SCRIPT_2",3,74624,0x85bab06fu}}};
            const auto lipsync=forge::big::File::openFully(root/"dialogue.big");
            size_t total=0;
            size_t joined=0,largeDelta=0;
            double maxDelta=0;
            for(const auto& pair:banks) {
                const auto resolved=forge::lut::dialoguePair(pair.filename,"English");
                require(resolved && resolved->lipsyncBank==pair.lipsyncBank,
                        "retail bank pairing mismatch");
                const auto audio=forge::lut::File::open(root/pair.filename);
                require(audio.clips().size()==pair.count,"retail LUT count mismatch");
                const auto* lipBank=lipsync.findBank(pair.lipsyncBank);
                require(lipBank,"paired lipsync sub-bank missing");
                std::unordered_map<uint32_t,const forge::big::Entry*> lipById;
                for(const auto& record:lipBank->entries)
                    require(lipById.emplace(record.id,&record).second,"duplicate lipsync ID");
                for(const auto& item:audio.clips()) {
                    require(audio.find(item.index)==&item && item.formatTag==0x69 &&
                            item.channels==1 && item.sampleRate==22050 &&
                            item.blockAlign==36 && item.averageBytesPerSecond==12403 &&
                            item.dataBytes>0,"retail LUT format/index mismatch");
                    const auto it=lipById.find(item.index);
                    require(it!=lipById.end() && it->second->type==1 && it->second->length>0,
                            "audio clip has no paired lipsync entry");
                    const auto curve=forge::lipsync::decode(lipsync.entryData(*it->second),
                                                             it->second->subHeader);
                    const double delta=std::abs(item.durationSeconds()-curve.duration());
                    maxDelta=std::max(maxDelta,delta);
                    if(delta>0.05) ++largeDelta;
                    ++joined;
                }
                require(!audio.riff(1).empty(),"retail RIFF lookup failed");
                const auto probe=audio.pcm16(pair.probeId);
                require(probe.size()==pair.probeSamples && pcmCrc(probe)==pair.probeCrc,
                        "retail PCM differs from independent Python decoder");
                const auto wav=audio.wavPcm16(pair.probeId);
                require(wav.size()==44+probe.size()*2 &&
                        std::equal(wav.begin(),wav.begin()+4,"RIFF") &&
                        std::equal(wav.begin()+8,wav.begin()+12,"WAVE") &&
                        std::equal(wav.begin()+36,wav.begin()+40,"data") &&
                        wav[20]==1 && wav[21]==0 && wav[22]==1 && wav[23]==0 &&
                        wav[24]==0x22 && wav[25]==0x56 &&
                        wav[40]==uint8_t(probe.size()*2) &&
                        wav[41]==uint8_t(probe.size()*2>>8) &&
                        wav[44]==uint8_t(probe.front()) &&
                        wav[45]==uint8_t(uint16_t(probe.front())>>8),
                        "retail PCM WAV header or payload mismatch");
                if(std::string(pair.filename)=="ScriptDialogue2.lut")
                    require(!audio.find(2) && audio.find(3),"retail missing-index check");
                total+=audio.clips().size();
                std::cout << pair.filename << ": " << audio.clips().size() << " clips\n";
            }
            require(total==20214,"retail LUT total mismatch");
            std::cout << "lut: " << joined << " audio/lipsync IDs joined; max duration delta "
                      << maxDelta << " s, " << largeDelta << " over 50 ms\n";
        } else std::cout << "lut: fixture passed (pass language directory for retail banks)\n";
        return 0;
    } catch(const std::exception& ex) {
        std::cerr << "lut: " << ex.what() << '\n';
        return 1;
    }
}
