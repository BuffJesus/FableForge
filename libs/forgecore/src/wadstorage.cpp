#include "forge/wadstorage.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <set>
#include <stdexcept>

namespace forge::wadstorage {
namespace {
void require(bool ok,const char* message) {
    if(!ok)throw std::runtime_error(std::string("wadstorage: ")+message);
}
struct Reader {
    std::span<const uint8_t> bytes;
    size_t at=0;
    std::span<const uint8_t> take(size_t n) {
        require(n<=bytes.size()-at,"truncated field");
        const auto result=bytes.subspan(at,n);at+=n;return result;
    }
    uint32_t word() {
        const auto b=take(4);
        return uint32_t(b[0])|uint32_t(b[1])<<8|uint32_t(b[2])<<16|uint32_t(b[3])<<24;
    }
    std::string name(bool empty) {
        auto b=take(word());
        if(!b.empty() && !b.back())b=b.first(b.size()-1);
        require((empty || !b.empty()) && std::find(b.begin(),b.end(),0)==b.end(),"invalid name");
        return std::string(b.begin(),b.end());
    }
    void finish()const {require(at==bytes.size(),"trailing metadata");}
};
}
Metadata validateMetadata(std::span<const uint8_t> header,
                          std::span<const uint8_t> table,uint64_t fileSize) {
    // CBankFile::ReadFileInformation 02F99600 / ReadEntryList 02F9A180;
    // CWADBank::GetDataFormatVersion 01C808C0 returns 2.
    Reader h{header};
    require(h.word()==0x42424242u && h.word()==115 && h.word()==2 && h.word()==1,
            "unsupported header");
    Metadata result;result.alignment=h.word();result.maximumId=h.word();
    const auto count=h.word();result.tableOffset=h.word();h.finish();
    const uint64_t toc=result.tableOffset;
    require(result.alignment && !(result.alignment&(result.alignment-1)) &&
            toc>=32 && toc%result.alignment==0 && toc<=fileSize && fileSize-toc==table.size(),
            "invalid table span or alignment");
    Reader c{table};
    require(c.word()==1 && c.word()==0 && c.word()==count,"unsupported histogram");
    require(count<=(table.size()-c.at)/132,"entry count exceeds table");
    std::set<uint32_t> ids;std::set<std::string> names;
    result.entries.reserve(count);
    for(uint32_t i=0;i<count;++i) {
        require(c.word()==42,"invalid entry magic");
        wad::Entry e;e.id=c.word();e.type=c.word();e.size=c.word();e.offset=c.word();e.crc=c.word();
        e.name=c.name(false);e.timestamp=c.word();
        require(c.word()==1,"unsupported filename count");
        e.dependencies.push_back(c.name(true));
        require(c.word()==88,"unsupported file information size");
        const auto info=c.take(88);e.info.assign(info.begin(),info.end());
        // CWADBank::AddFile 031E0FE0 / CWADFile::GetFileInformation
        // 031E01C0 save/read 0x58 raw bytes. PDB CFileInformation: three
        // CDateAndTime[28], ReadOnly at 84, then three raw padding bytes.
        require(info[84]<=1,"invalid read-only flag");
        require(e.id && e.id<=result.maximumId && !e.type && ids.insert(e.id).second &&
                names.insert(e.name).second,"invalid or duplicate entry identity");
        require(e.size && e.offset>=32 && e.offset%result.alignment==0 &&
                uint64_t(e.offset)+e.size<=toc,"invalid payload span");
        result.entries.push_back(std::move(e));
    }
    c.finish();
    std::vector<const wad::Entry*> order;
    for(const auto& e:result.entries)order.push_back(&e);
    std::sort(order.begin(),order.end(),[](auto a,auto b){return a->offset<b->offset;});
    uint64_t end=32;
    for(const auto* e:order) {
        require(e->offset>=end,"overlapping payloads");end=uint64_t(e->offset)+e->size;
    }
    return result;
}
Storage validateEnvelope(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    require(bool(input),"cannot open archive");
    const auto end=input.tellg();require(end>=32,"truncated header");const uint64_t size=uint64_t(end);
    auto read=[&](uint64_t offset,size_t count) {
        require(offset<=size && count<=size-offset,"read exceeds archive");
        std::vector<uint8_t> bytes(count);input.seekg(std::streamoff(offset));
        if(count)input.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(count));
        require(bool(input),"read failed");return bytes;
    };
    const auto header=read(0,32);Reader h{header};h.take(28);const uint64_t toc=h.word();
    require(toc>=32 && toc<=size && size-toc<=64000000,"unsupported table span");
    Storage result;result.metadata=validateMetadata(header,read(toc,size_t(size-toc)),size);
    std::vector<const wad::Entry*> order;
    for(const auto& e:result.metadata.entries)order.push_back(&e);
    std::sort(order.begin(),order.end(),[](auto a,auto b){return a->offset<b->offset;});
    uint64_t cursor=32;
    auto gap=[&](uint64_t stop) {
        require(cursor<=stop,"overlapping payloads");result.zeroPaddingBytes+=stop-cursor;
        input.seekg(std::streamoff(cursor));std::array<char,65536> bytes;
        while(cursor<stop) {
            const auto count=size_t(std::min<uint64_t>(bytes.size(),stop-cursor));
            input.read(bytes.data(),std::streamsize(count));require(bool(input),"gap read failed");
            require(std::all_of(bytes.begin(),bytes.begin()+count,[](char v){return !v;}),"nonzero unowned bytes");
            cursor+=count;
        }
    };
    for(const auto* e:order){gap(e->offset);cursor=uint64_t(e->offset)+e->size;}
    gap(toc);return result;
}
}
