#include "forge/stbstorage.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <stdexcept>

namespace forge::stbstorage {
namespace {
void require(bool ok,const char* reason) {
    if(!ok)throw std::runtime_error(std::string("stbstorage: ")+reason);
}
}
BankStorage validateBankStorage(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    require(bool(input),"cannot open bank");
    const auto end=input.tellg();require(end>=32,"truncated bank header");
    const uint64_t size=uint64_t(end);
    auto read=[&](uint64_t offset,size_t count) {
        require(offset<=size && count<=size-offset,"bank read exceeds file");
        std::vector<uint8_t> bytes(count);
        input.seekg(std::streamoff(offset));
        if(count)input.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(count));
        require(bool(input),"bank read failed");return bytes;
    };
    const auto header=read(0,32);
    uint32_t toc=0;for(unsigned i=0;i<4;++i)toc|=uint32_t(header[28+i])<<(8*i);
    require(toc>=32 && toc<=size,"invalid bank table offset");
    // Explicit supported-subset resource cap before allocating untrusted TOC.
    require(size-toc<=64000000,"bank table exceeds supported limit");
    const auto metadata=validateBankMetadata(header,read(toc,size_t(size-toc)),size);
    BankStorage result;result.entries=metadata.entries.size();
    std::map<uint32_t,const BankEntry*> unowned;
    const BankEntry* common=nullptr;
    std::vector<const BankEntry*> physical;
    for(const auto& entry:metadata.entries) {
        unowned.emplace(entry.entry.id,&entry);physical.push_back(&entry);
        if(entry.entry.name=="__STATIC_MAP_COMMON_HEADER__")common=&entry;
    }
    require(common && !common->sea,"missing common entry");
    const auto commonBytes=read(common->entry.offset,common->entry.size);
    const auto maps=validateCommon(commonBytes);unowned.erase(common->entry.id);
    for(const auto& map:maps) {
        const auto it=unowned.find(uint32_t(map.record.info.bankFileIndex));
        require(it!=unowned.end() && !it->second->sea && it->second->entry.name==map.name,
                "common map name/ID does not identify one chunk");
        const auto& entry=it->second->entry;
        const std::vector<uint8_t> record(commonBytes.begin()+map.logicalOffset,
                                         commonBytes.begin()+map.record.info.headerEndPtr);
        try {
            const auto reviewed=validateMapChunk(read(entry.offset,entry.size),record,map.logicalOffset);
            result.chunks.foregroundFrames+=reviewed.foregroundFrames;
            result.chunks.backgroundFrames+=reviewed.backgroundFrames;
            result.chunks.detailFrames+=reviewed.detailFrames;
            result.chunks.decodedBytes+=reviewed.decodedBytes;
            result.chunks.zeroPaddingBytes+=reviewed.zeroPaddingBytes;
        }catch(const std::exception& e){throw std::runtime_error("stbstorage: "+map.name+": "+e.what());}
        ++result.maps;result.paletteRows+=map.record.detailPaletteCount;unowned.erase(it);
    }
    for(const auto& [id,entry]:unowned) {
        require(entry->sea,"bank entry has no runtime owner");
        validateSea(read(entry->entry.offset,entry->entry.size));++result.seas;
    }
    // Metadata review already proves disjoint ordered payload spans. Check
    // gaps in bounded reads, rather than allocating a potentially huge hole.
    std::sort(physical.begin(),physical.end(),[](auto a,auto b){return a->entry.offset<b->entry.offset;});
    uint64_t cursor=32;
    auto gap=[&](uint64_t stop) {
        require(cursor<=stop,"overlapping bank payloads");
        result.zeroBankPaddingBytes+=stop-cursor;
        std::array<char,65536> bytes;
        input.seekg(std::streamoff(cursor));
        while(cursor<stop) {
            const size_t count=size_t(std::min<uint64_t>(bytes.size(),stop-cursor));
            input.read(bytes.data(),std::streamsize(count));
            require(bool(input),"bank gap read failed");
            require(std::all_of(bytes.begin(),bytes.begin()+count,[](char v){return v==0;}),"nonzero unowned bank bytes");
            cursor+=count;
        }
    };
    for(const auto* entry:physical) {
        gap(entry->entry.offset);cursor=uint64_t(entry->entry.offset)+entry->entry.size;
    }
    gap(toc);return result;
}
}
