#include "forge/lipsync.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace forge::lipsync {
namespace {

uint32_t read32(std::span<const uint8_t> bytes, size_t& pos) {
    if (pos > bytes.size() || bytes.size() - pos < 4)
        throw std::runtime_error("lipsync: truncated u32");
    const uint32_t value = uint32_t(bytes[pos]) | uint32_t(bytes[pos+1]) << 8 |
                           uint32_t(bytes[pos+2]) << 16 | uint32_t(bytes[pos+3]) << 24;
    pos += 4;
    return value;
}

void write32(std::vector<uint8_t>& out, uint32_t value) {
    for (unsigned shift=0;shift<32;shift+=8) out.push_back(uint8_t(value>>shift));
}

} // namespace

Entry decode(std::span<const uint8_t> raw, std::span<const uint8_t> info) {
    if (info.size()!=4) throw std::runtime_error("lipsync: Info must be one f32");
    Entry entry;
    size_t infoPos=0;
    entry.durationBits=read32(info,infoPos);

    size_t pos=0;
    const uint32_t count=read32(raw,pos);
    if (count>(raw.size()-pos)/2)
        throw std::runtime_error("lipsync: truncated viseme dictionary");
    entry.dictionary.reserve(count);
    for (uint32_t i=0;i<count;++i) {
        const uint8_t id=raw[pos++];
        const size_t start=pos;
        while (pos<raw.size() && raw[pos]!=0) ++pos;
        if (pos==raw.size()) throw std::runtime_error("lipsync: unterminated viseme symbol");
        entry.dictionary.push_back({id,std::string(reinterpret_cast<const char*>(raw.data()+start),pos-start)});
        ++pos;
    }
    entry.fps=read32(raw,pos);
    const uint32_t frameCount=read32(raw,pos);
    if (frameCount>raw.size()-pos)
        throw std::runtime_error("lipsync: truncated frame table");
    entry.frames.reserve(frameCount);
    for (uint32_t i=0;i<frameCount;++i) {
        const uint8_t keyCount=raw[pos++];
        if (size_t(keyCount)>(raw.size()-pos)/2)
            throw std::runtime_error("lipsync: truncated frame keys");
        Frame frame;
        frame.reserve(keyCount);
        for (uint32_t k=0;k<keyCount;++k) {
            frame.push_back({raw[pos],raw[pos+1]});
            pos+=2;
        }
        entry.frames.push_back(std::move(frame));
    }
    if (pos!=raw.size()) throw std::runtime_error("lipsync: trailing payload bytes");
    return entry;
}

Pose sample(const Entry& entry, double seconds) {
    Pose pose;
    if(entry.dictionary.empty()) return pose;
    std::array<float,256> weights{};
    if(!entry.frames.empty() && entry.fps && std::isfinite(seconds)) {
        const double position=std::clamp(seconds*double(entry.fps),0.0,
                                         double(entry.frames.size()-1));
        pose.frame=size_t(position);
        const size_t next=std::min(pose.frame+1,entry.frames.size()-1);
        const float alpha=float(position-double(pose.frame));
        for(const auto& key:entry.frames[pose.frame])
            weights[key.id]+=float(key.weight)/255.0f*(1.0f-alpha);
        for(const auto& key:entry.frames[next])
            weights[key.id]+=float(key.weight)/255.0f*alpha;
    }
    float total=0;
    pose.visemes.reserve(entry.dictionary.size());
    for(const auto& viseme:entry.dictionary) {
        const float weight=weights[viseme.id];
        pose.visemes.push_back({viseme.id,viseme.symbol,weight});
        total+=weight;
        weights[viseme.id]=0; // a repeated dictionary ID must not double count
    }
    if(total>1.0f)
        for(auto& viseme:pose.visemes) viseme.weight/=total;
    pose.restWeight=std::clamp(1.0f-total,0.0f,1.0f);
    return pose;
}

uint8_t ensureViseme(Entry& entry,const std::string& symbol) {
    static constexpr std::array<const char*,6> allowed={"AH","EE","MM","OH","SZ","WW"};
    if(std::find_if(allowed.begin(),allowed.end(),[&](const char* name) {
        return symbol==name;
    })==allowed.end()) throw std::runtime_error("lipsync: unsupported phoneme symbol");
    std::array<bool,256> used{};
    for(const auto& viseme:entry.dictionary) {
        if(viseme.symbol==symbol) return viseme.id;
        used[viseme.id]=true;
    }
    for(size_t id=0;id<used.size();++id) if(!used[id]) {
        entry.dictionary.push_back({uint8_t(id),symbol});
        return uint8_t(id);
    }
    throw std::runtime_error("lipsync: no free phoneme ID");
}

void setWeight(Entry& entry,size_t frame,const std::string& symbol,uint8_t weight) {
    if(frame>=entry.frames.size()) throw std::runtime_error("lipsync: frame out of range");
    auto& keys=entry.frames[frame];
    if(keys.size()>=4) {
        const auto known=std::find_if(entry.dictionary.begin(),entry.dictionary.end(),
            [&](const Viseme& viseme){return viseme.symbol==symbol;});
        if(known==entry.dictionary.end() ||
           std::none_of(keys.begin(),keys.end(),
                [&](const Key& key){return key.id==known->id;}))
            throw std::runtime_error("lipsync: frame already has four phonemes");
    }
    const auto id=ensureViseme(entry,symbol);
    for(auto& key:keys) if(key.id==id) {key.weight=weight;return;}
    if(keys.size()>=4) throw std::runtime_error("lipsync: frame already has four phonemes");
    keys.push_back({id,weight});
}

void removeWeight(Entry& entry,size_t frame,const std::string& symbol) {
    if(frame>=entry.frames.size()) throw std::runtime_error("lipsync: frame out of range");
    const auto it=std::find_if(entry.dictionary.begin(),entry.dictionary.end(),
        [&](const Viseme& viseme){return viseme.symbol==symbol;});
    if(it==entry.dictionary.end()) return;
    auto& keys=entry.frames[frame];
    keys.erase(std::remove_if(keys.begin(),keys.end(),
        [&](const Key& key){return key.id==it->id;}),keys.end());
}

void insertFrameAfter(Entry& entry,size_t frame) {
    if(frame>=entry.frames.size()) throw std::runtime_error("lipsync: frame out of range");
    if(!entry.fps || entry.frames.size()>=std::numeric_limits<uint32_t>::max())
        throw std::runtime_error("lipsync: invalid frame count or fps");
    entry.frames.insert(entry.frames.begin()+frame+1,Frame{});
    entry.setDuration(float(entry.frames.size())/entry.fps);
}

void eraseFrame(Entry& entry,size_t frame) {
    if(frame>=entry.frames.size()) throw std::runtime_error("lipsync: frame out of range");
    if(entry.frames.size()<=1 || !entry.fps)
        throw std::runtime_error("lipsync: cannot remove the final frame");
    entry.frames.erase(entry.frames.begin()+frame);
    entry.setDuration(float(entry.frames.size())/entry.fps);
}

std::vector<uint8_t> encode(const Entry& entry) {
    if (entry.dictionary.size()>std::numeric_limits<uint32_t>::max() ||
        entry.frames.size()>std::numeric_limits<uint32_t>::max())
        throw std::runtime_error("lipsync: too many visemes or frames");
    std::vector<uint8_t> out;
    write32(out,uint32_t(entry.dictionary.size()));
    for (const auto& viseme:entry.dictionary) {
        if (viseme.symbol.find('\0')!=std::string::npos)
            throw std::runtime_error("lipsync: symbol contains NUL");
        out.push_back(viseme.id);
        out.insert(out.end(),viseme.symbol.begin(),viseme.symbol.end());
        out.push_back(0);
    }
    write32(out,entry.fps);
    write32(out,uint32_t(entry.frames.size()));
    for (const auto& frame:entry.frames) {
        if (frame.size()>255) throw std::runtime_error("lipsync: more than 255 keys in a frame");
        out.push_back(uint8_t(frame.size()));
        for (const auto& key:frame) {out.push_back(key.id);out.push_back(key.weight);}
    }
    return out;
}

std::vector<uint8_t> encodeInfo(const Entry& entry) {
    std::vector<uint8_t> out;
    write32(out,entry.durationBits);
    return out;
}

UpsertResult upsert(big::File& file, const std::string& bankName,
                    uint32_t soundId, const Entry& value) {
    if (bankName.rfind("LIPSYNC_",0)!=0 || soundId==0)
        throw std::runtime_error("lipsync: choose a LIPSYNC sub-bank and nonzero sound ID");
    big::Bank* bank=file.findBank(bankName);
    if (!bank) throw std::runtime_error("lipsync: sub-bank not found: "+bankName);
    const auto payload=encode(value);
    const auto info=encodeInfo(value);

    big::Entry* existing=nullptr;
    for (auto& record:bank->entries) if (record.id==soundId) {
        if (existing) throw std::runtime_error("lipsync: duplicate sound ID in sub-bank");
        existing=&record;
    }
    if (existing) {
        if (existing->type!=1) throw std::runtime_error("lipsync: existing record is not type 1");
        existing->data=payload;
        existing->length=uint32_t(payload.size());
        existing->dataOffset=0;
        existing->subHeader=info;
        return {soundId,existing->name,bankName,false};
    }

    const big::Entry* donor=nullptr;
    std::string prefix;
    for (const auto& record:bank->entries) {
        if (record.type!=1 || record.length==0 || record.subHeader.size()!=4) continue;
        const size_t underscore=record.name.rfind('_');
        if (underscore==std::string::npos || record.name.substr(underscore+1)!=std::to_string(record.id)) continue;
        donor=&record;
        prefix=record.name.substr(0,underscore+1);
        break;
    }
    if (!donor) throw std::runtime_error("lipsync: no same-bank type-1 donor");
    const std::string name=prefix+std::to_string(soundId);
    for (const auto& record:bank->entries)
        if (record.name==name) throw std::runtime_error("lipsync: generated name already exists");
    big::Entry added=*donor;
    added.id=soundId;
    added.name=name;
    added.data=payload;
    added.length=uint32_t(payload.size());
    added.dataOffset=0;
    added.subHeader=info;
    bank->entries.push_back(std::move(added));
    return {soundId,name,bankName,true};
}

void writeScratchArchive(const std::filesystem::path& source,
                         const std::filesystem::path& destination,
                         std::span<const ArchiveEdit> edits) {
    namespace fs=std::filesystem;
    if(!fs::is_regular_file(source)) throw std::runtime_error("lipsync: source archive missing");
    if(destination.empty() || destination.filename().empty())
        throw std::runtime_error("lipsync: choose a destination file");
    if(edits.empty()) throw std::runtime_error("lipsync: no staged edits to export");
    if(fs::exists(destination))
        throw std::runtime_error("lipsync: destination already exists");
    const auto archive=big::File::openFully(source);
    auto edited=archive;
    for(const auto& edit:edits)
        upsert(edited,edit.bankName,edit.soundId,edit.value);
    const auto bytes=edited.serialize();
    if(!destination.parent_path().empty())
        fs::create_directories(destination.parent_path());
    fs::path temporary=destination;
    temporary+=std::string(".part.")+std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    try {
        {
            std::ofstream stream(temporary,std::ios::binary|std::ios::trunc);
            if(!stream) throw std::runtime_error("lipsync: cannot open scratch output");
            stream.write(reinterpret_cast<const char*>(bytes.data()),
                         std::streamsize(bytes.size()));
            if(!stream) throw std::runtime_error("lipsync: cannot write scratch output");
        }
        const auto checked=big::File::open(temporary);
        if(checked.banks().size()!=archive.banks().size())
            throw std::runtime_error("lipsync: scratch archive verification failed");
        for(const auto& edit:edits) {
            const auto* bank=checked.findBank(edit.bankName);
            if(!bank) throw std::runtime_error("lipsync: scratch bank verification failed");
            const big::Entry* match=nullptr;
            for(const auto& record:bank->entries) if(record.id==edit.soundId) {
                if(match) throw std::runtime_error("lipsync: duplicate ID in scratch archive");
                match=&record;
            }
            if(!match || checked.entryData(*match)!=encode(edit.value) ||
               match->subHeader!=encodeInfo(edit.value))
                throw std::runtime_error("lipsync: scratch entry verification failed");
        }
        if(fs::exists(destination))
            throw std::runtime_error("lipsync: destination appeared while writing");
        fs::rename(temporary,destination);
    } catch(...) {
        std::error_code ignored;
        fs::remove(temporary,ignored);
        throw;
    }
}

} // namespace forge::lipsync
