#include "forge/lut.hpp"
#include "forge/xboxadpcm.hpp"

#include <array>
#include <bit>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace forge::lut {
std::optional<DialoguePair> dialoguePair(std::string_view speechBank,
                                         std::string_view language) {
    if(language.empty()) return std::nullopt;
    std::string upperLanguage;
    for(unsigned char c:language) {
        if(!std::isalpha(c)) return std::nullopt;
        upperLanguage.push_back(char(std::toupper(c)));
    }
    std::string key;
    for(unsigned char c:speechBank) key.push_back(char(std::tolower(c)));
    struct Row {const char* key;const char* lut;const char* suffix;const char* snds;};
    constexpr Row rows[]={
        {"dialogue.lug","Dialogue.lut","MAIN","dialoguesnds.bin"},
        {"dialogue2.lug","Dialogue2.lut","MAIN_2","dialoguesnds2.bin"},
        {"scriptdialogue.lug","ScriptDialogue.lut","SCRIPT","scriptdialoguesnds.bin"},
        {"scriptdialogue2.lug","ScriptDialogue2.lut","SCRIPT_2","scriptdialoguesnds2.bin"},
        {"dialogue.lut","Dialogue.lut","MAIN","dialoguesnds.bin"},
        {"dialogue2.lut","Dialogue2.lut","MAIN_2","dialoguesnds2.bin"},
        {"scriptdialogue.lut","ScriptDialogue.lut","SCRIPT","scriptdialoguesnds.bin"},
        {"scriptdialogue2.lut","ScriptDialogue2.lut","SCRIPT_2","scriptdialoguesnds2.bin"},
    };
    for(const auto& row:rows)
        if(key==row.key)
            return DialoguePair{row.lut,"LIPSYNC_"+upperLanguage+"_"+row.suffix,row.snds};
    return std::nullopt;
}

namespace {

uint16_t u16(const uint8_t* p) {return uint16_t(p[0]) | uint16_t(p[1])<<8;}
uint32_t u32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24;
}

void readAt(std::ifstream& stream,uint64_t fileSize,uint64_t offset,uint8_t* out,size_t size) {
    if(offset>fileSize || uint64_t(size)>fileSize-offset ||
       offset>uint64_t(std::numeric_limits<std::streamoff>::max()))
        throw std::runtime_error("lut: read outside file");
    stream.clear();
    stream.seekg(std::streamoff(offset),std::ios::beg);
    stream.read(reinterpret_cast<char*>(out),std::streamsize(size));
    if(!stream) throw std::runtime_error("lut: short read");
}

void readWave(std::ifstream& stream,uint64_t fileSize,Clip& clip) {
    const uint64_t end=clip.riffOffset+clip.riffLength;
    bool haveFormat=false,haveData=false;
    uint64_t cursor=clip.riffOffset+12;
    while(cursor+8<=end) {
        std::array<uint8_t,8> chunk{};
        readAt(stream,fileSize,cursor,chunk.data(),chunk.size());
        const uint32_t length=u32(chunk.data()+4);
        const uint64_t data=cursor+8;
        if(uint64_t(length)>end-data) throw std::runtime_error("lut: RIFF chunk out of bounds");
        if(std::memcmp(chunk.data(),"fmt ",4)==0) {
            if(length<16) throw std::runtime_error("lut: short fmt chunk");
            std::array<uint8_t,16> fmt{};
            readAt(stream,fileSize,data,fmt.data(),fmt.size());
            clip.formatTag=u16(fmt.data());
            clip.channels=u16(fmt.data()+2);
            clip.sampleRate=u32(fmt.data()+4);
            clip.averageBytesPerSecond=u32(fmt.data()+8);
            clip.blockAlign=u16(fmt.data()+12);
            clip.bitsPerSample=u16(fmt.data()+14);
            haveFormat=true;
        } else if(std::memcmp(chunk.data(),"data",4)==0) {
            clip.dataOffset=data;
            clip.dataBytes=length;
            haveData=true;
        }
        const uint64_t next=data+length+(length&1u);
        if(next>end) throw std::runtime_error("lut: missing RIFF chunk padding");
        cursor=next;
    }
    if(cursor!=end || !haveFormat || !haveData || !clip.channels || !clip.sampleRate ||
       !clip.averageBytesPerSecond || !clip.blockAlign)
        throw std::runtime_error("lut: incomplete RIFF/WAVE");
}

} // namespace

File File::open(const std::filesystem::path& path) {
    std::ifstream stream(path,std::ios::binary|std::ios::ate);
    if(!stream) throw std::runtime_error("lut: cannot open "+path.string());
    File file;
    file.path_=path;
    file.fileSize_=uint64_t(stream.tellg());
    std::array<uint8_t,44> header{};
    readAt(stream,file.fileSize_,0,header.data(),header.size());
    constexpr char magic[]="LiOnHeAdLHAudioBankCompData";
    if(std::memcmp(header.data(),magic,sizeof(magic))!=0)
        throw std::runtime_error("lut: bad magic");
    const uint64_t tocOffset=u32(header.data()+40);
    const uint64_t markerOffset=tocOffset+44;
    if(markerOffset>file.fileSize_ || markerOffset<44)
        throw std::runtime_error("lut: bad lookup offset");

    uint64_t cursor=44;
    while(cursor<markerOffset) {
        if(markerOffset-cursor<48) throw std::runtime_error("lut: truncated clip record");
        std::array<uint8_t,48> record{};
        readAt(stream,file.fileSize_,cursor,record.data(),record.size());
        if(std::memcmp(record.data()+36,"RIFF",4)!=0 ||
           std::memcmp(record.data()+44,"WAVE",4)!=0)
            throw std::runtime_error("lut: clip has no RIFF/WAVE");
        const uint64_t riffLength=uint64_t(u32(record.data()+40))+8;
        if(riffLength<12 || riffLength>UINT32_MAX || riffLength>markerOffset-(cursor+36))
            throw std::runtime_error("lut: clip RIFF exceeds audio section");
        Clip clip;
        clip.index=u32(record.data());
        clip.recordOffset=cursor;
        clip.riffOffset=cursor+36;
        clip.riffLength=uint32_t(riffLength);
        clip.flags2=u32(record.data()+16);
        clip.minDistance=std::bit_cast<float>(u32(record.data()+20));
        clip.maxDistance=std::bit_cast<float>(u32(record.data()+24));
        clip.priority=u32(record.data()+28);
        readWave(stream,file.fileSize_,clip);
        if(!clip.index || !file.byIndex_.emplace(clip.index,file.clips_.size()).second)
            throw std::runtime_error("lut: zero or duplicate clip index");
        file.clips_.push_back(clip);
        cursor+=36+riffLength;
    }
    if(cursor!=markerOffset) throw std::runtime_error("lut: clip walk missed lookup table");

    std::array<uint8_t,56> table{};
    readAt(stream,file.fileSize_,markerOffset,table.data(),table.size());
    if(std::memcmp(table.data(),"LHAudioBankLookupTable",22)!=0)
        throw std::runtime_error("lut: lookup table marker missing");
    const uint32_t dataSize=u32(table.data()+32);
    const uint32_t count=u32(table.data()+40);
    if(count!=file.clips_.size() || u32(table.data()+44)!=1 ||
       dataSize!=20+(count?count-1:0)*12)
        throw std::runtime_error("lut: lookup table count/size mismatch");
    const uint32_t firstLookup=count>1 ? uint32_t(file.clips_[1].recordOffset-44) : uint32_t(tocOffset);
    if(u32(table.data()+48)!=firstLookup)
        throw std::runtime_error("lut: lookup table first offset mismatch");
    const uint64_t recordsStart=markerOffset+56;
    if(uint64_t(count?count-1:0)*12>file.fileSize_-recordsStart)
        throw std::runtime_error("lut: lookup table exceeds file");
    for(uint32_t i=1;i<count;++i) {
        std::array<uint8_t,12> record{};
        readAt(stream,file.fileSize_,recordsStart+uint64_t(i-1)*12,record.data(),record.size());
        const Clip& clip=file.clips_[i];
        if(u32(record.data())!=clip.index || u32(record.data()+4)!=clip.riffLength+36 ||
           u32(record.data()+8)!=clip.recordOffset-44)
            throw std::runtime_error("lut: lookup record disagrees with audio clip");
    }
    return file;
}

const Clip* File::find(uint32_t index) const {
    const auto it=byIndex_.find(index);
    return it==byIndex_.end()?nullptr:&clips_[it->second];
}

std::vector<uint8_t> File::riff(uint32_t index) const {
    const Clip* clip=find(index);
    if(!clip) return {};
    std::ifstream stream(path_,std::ios::binary);
    if(!stream) throw std::runtime_error("lut: cannot reopen "+path_.string());
    std::vector<uint8_t> bytes(clip->riffLength);
    readAt(stream,fileSize_,clip->riffOffset,bytes.data(),bytes.size());
    return bytes;
}

std::vector<int16_t> File::pcm16(uint32_t index) const {
    const Clip* clip=find(index);
    if(!clip) return {};
    if(clip->formatTag!=0x69) throw std::runtime_error("lut: expected Xbox ADPCM clip");
    std::ifstream stream(path_,std::ios::binary);
    if(!stream) throw std::runtime_error("lut: cannot reopen "+path_.string());
    std::vector<uint8_t> bytes(clip->dataBytes);
    readAt(stream,fileSize_,clip->dataOffset,bytes.data(),bytes.size());
    return xboxadpcm::decode(bytes,clip->channels,clip->blockAlign);
}

std::vector<uint8_t> File::wavPcm16(uint32_t index) const {
    const Clip* clip=find(index);
    if(!clip) return {};
    const auto pcm=pcm16(index);
    if(pcm.size()>(UINT32_MAX-36u)/2 ||
       clip->sampleRate>UINT32_MAX/(2*clip->channels))
        throw std::runtime_error("lut: PCM WAV too large");
    std::vector<uint8_t> wav;
    wav.reserve(44+pcm.size()*2);
    const auto put16=[&](uint16_t value) {
        wav.push_back(uint8_t(value));wav.push_back(uint8_t(value>>8));
    };
    const auto put32=[&](uint32_t value) {
        for(unsigned shift=0;shift<32;shift+=8) wav.push_back(uint8_t(value>>shift));
    };
    wav.insert(wav.end(),{'R','I','F','F'});
    put32(uint32_t(36+pcm.size()*2));
    wav.insert(wav.end(),{'W','A','V','E','f','m','t',' '});
    put32(16);put16(1);put16(clip->channels);
    put32(clip->sampleRate);put32(clip->sampleRate*clip->channels*2);
    put16(uint16_t(clip->channels*2));put16(16);
    wav.insert(wav.end(),{'d','a','t','a'});
    put32(uint32_t(pcm.size()*2));
    for(const int16_t sample:pcm) put16(uint16_t(sample));
    return wav;
}

} // namespace forge::lut
