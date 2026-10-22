#include "archive_stream_patches_generated.hpp"
#include "archive_pack_stub_generated.hpp"
#include <map>
#include <cstdio>
#include <cstring>
#include <stdexcept>

struct Memory {
    std::map<std::uint32_t,std::uint8_t> bytes;
    unsigned writes=0,failWrite=0;
    bool read(std::uint32_t at,std::span<std::uint8_t> out) {
        for(auto& value:out) {auto it=bytes.find(at++);if(it==bytes.end())return false;value=it->second;}return true;
    }
    bool write(std::uint32_t at,std::span<const std::uint8_t> input) {
        ++writes;
        if(writes==failWrite) {bytes[at]=input[0];return false;}
        for(auto value:input)bytes[at++]=value;return true;
    }
    bool flush() {return true;}
};
static void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
int main() try {
    const std::uint8_t before[]{1,2,3,4},after[]{5,6,7,8};
    std::vector<texture_font::CodePatch> list{{0x99ac1c,before,after},{0x99acb0,before,after},
        {0x99ad01,before,after},{0x143fde8,before,after}};
    const auto metadata=texture_archive::streamPatches();list.insert(list.end(),metadata.begin(),metadata.end());
    const std::array<std::uint8_t,9> jump{0xe9,1,2,3,4,0x90,0x90,0x90,0x90};
    list.push_back({0xa62d4a,texture_archive::packCarryOriginal,jump});
    require(list.size()==23,"complete archive patch transaction");
    Memory baseline;
    for(const auto& patch:list)for(unsigned i=0;i<patch.original.size();++i)baseline.bytes[patch.address+i]=patch.original[i];
    auto success=baseline;
    require(texture_font::applyPatches(success,list)==texture_font::PatchResult::applied,"complete installation");
    for(const auto& patch:list)for(unsigned i=0;i<patch.replacement.size();++i)
        require(success.bytes[patch.address+i]==patch.replacement[i],"replacement bytes");
    for(unsigned i=0;i<list.size();++i) {
        auto missing=baseline;missing.bytes.erase(list[i].address);
        const auto snapshot=missing.bytes;
        require(texture_font::applyPatches(missing,list)==texture_font::PatchResult::refused &&
            missing.writes==0 && missing.bytes==snapshot,"every site mandatory before any mutation");
        auto failure=baseline;failure.failWrite=i+1;
        require(texture_font::applyPatches(failure,list)==texture_font::PatchResult::rolledBack &&
            failure.bytes==baseline.bytes,"partial write restores all 23 regions");
    }
    std::puts("PASS 23-site archive transaction: success, 23 mandatory omissions, 23 partial-write rollbacks");
} catch(const std::exception& error) {std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
