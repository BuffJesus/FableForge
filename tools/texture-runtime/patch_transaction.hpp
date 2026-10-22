#pragma once
#include <cstdint>
#include <span>
#include <vector>
#include <algorithm>

namespace texture_font {
struct CodePatch {
    std::uint32_t address;
    std::span<const std::uint8_t> original,replacement;
};
enum class PatchResult { applied, refused, rolledBack, rollbackFailed };
// Startup-only, while no other thread can execute or mutate these sites. The
// backend owns page protection and instruction-cache flushing. A failed write
// may have changed bytes, so rollback includes that site as well.
template<class Memory>
PatchResult applyPatches(Memory& memory,std::span<const CodePatch> patches) {
    if(patches.empty()) return PatchResult::refused;
    for(std::size_t i=0;i<patches.size();++i) {
        const auto& p=patches[i];
        const auto end=std::uint64_t(p.address)+p.original.size();
        if(p.address<0x10000 || p.original.empty() || p.original.size()!=p.replacement.size() || end>0x100000000ull)
            return PatchResult::refused;
        for(std::size_t j=0;j<i;++j)
            if(p.address<std::uint64_t(patches[j].address)+patches[j].original.size() && patches[j].address<end)
                return PatchResult::refused;
    }
    std::size_t capacity=0;
    for(const auto& p:patches) capacity=std::max(capacity,p.original.size());
    // Allocate all scratch before touching executable bytes.
    std::vector<std::uint8_t> scratch(capacity);
    auto matches=[&](const CodePatch& p,std::span<const std::uint8_t> expected) {
        auto bytes=std::span<std::uint8_t>(scratch.data(),expected.size());
        return memory.read(p.address,bytes) && std::equal(bytes.begin(),bytes.end(),expected.begin());
    };
    for(const auto& p:patches) if(!matches(p,p.original)) return PatchResult::refused;
    auto rollback=[&](std::size_t attempted) {
        bool ok=true;
        for(std::size_t i=attempted;i>0;--i) {
            const auto& p=patches[i-1];
            const bool wrote=memory.write(p.address,p.original);
            const bool restored=matches(p,p.original);
            ok=wrote && restored && ok;
        }
        const bool flushed=memory.flush();
        return ok && flushed ? PatchResult::rolledBack : PatchResult::rollbackFailed;
    };
    for(std::size_t i=0;i<patches.size();++i) {
        const auto& p=patches[i];
        if(!memory.write(p.address,p.replacement) || !matches(p,p.replacement)) return rollback(i+1);
    }
    if(!memory.flush()) return rollback(patches.size());
    return PatchResult::applied;
}
}
