#include "patch_transaction.hpp"
#include <array>
#include <stdexcept>
#include <cstdio>
#include <cstring>
static void require(bool x){if(!x)throw std::runtime_error("patch transaction mismatch");}
struct Memory {
    std::array<std::uint8_t,64> bytes{};
    unsigned writes=0,flushes=0,failWrite=0;
    bool failFlush=false,failRollback=false,corruptWrite=false;
    bool read(std::uint32_t at,std::span<std::uint8_t> out) {
        if(at<0x10000 || std::uint64_t(at)-0x10000+out.size()>bytes.size())return false;
        std::memcpy(out.data(),bytes.data()+at-0x10000,out.size());return true;
    }
    bool write(std::uint32_t at,std::span<const std::uint8_t> in) {
        ++writes;
        if(failRollback && writes>failWrite)return false;
        if(writes==failWrite) {
            bytes[at-0x10000]=in[0]; // partial write still needs restoration
            return corruptWrite; // successful API with incorrect bytes is also caught
        }
        std::memcpy(bytes.data()+at-0x10000,in.data(),in.size());return true;
    }
    bool flush(){++flushes;return !(failFlush && flushes==1);}
};
int main() {
    using namespace texture_font;
    const std::array<std::uint8_t,3> before{1,2,3},after{4,5,6};
    const std::array<CodePatch,3> patches{{{0x10000,before,after},{0x10010,before,after},{0x10020,before,after}}};
    Memory base;
    for(const auto& p:patches)std::memcpy(base.bytes.data()+p.address-0x10000,before.data(),3);
    auto memory=base;require(applyPatches(memory,patches)==PatchResult::applied);
    require(memory.writes==3 && memory.flushes==1);
    for(unsigned fail=1;fail<=3;++fail)for(bool corrupt:{false,true}) {
        memory=base;memory.failWrite=fail;memory.corruptWrite=corrupt;
        require(applyPatches(memory,patches)==PatchResult::rolledBack && memory.bytes==base.bytes);
    }
    memory=base;memory.failFlush=true;
    require(applyPatches(memory,patches)==PatchResult::rolledBack && memory.bytes==base.bytes && memory.flushes==2);
    memory=base;memory.failWrite=2;memory.failRollback=true;
    require(applyPatches(memory,patches)==PatchResult::rollbackFailed);
    memory=base;memory.bytes[0x10]=99;const auto changed=memory.bytes;
    require(applyPatches(memory,patches)==PatchResult::refused && memory.writes==0 && memory.bytes==changed);
    auto overlap=patches;overlap[1].address=0x10001;memory=base;
    require(applyPatches(memory,overlap)==PatchResult::refused && memory.writes==0);
    auto unequal=patches;unequal[1].replacement=std::span(after).first(2);
    require(applyPatches(memory,unequal)==PatchResult::refused && memory.writes==0);
    std::puts("PASS patch preflight, partial/corrupt-write and flush rollback, and explicit rollback-failure reporting");
}
