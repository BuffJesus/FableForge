#pragma once
// Zero-means-256 terrain allocation extents.
//
// STB background patches store their texture allocation width/height as one byte
// each (patch body offsets 14/15). CLandscapeBackgroundPatch::Load (0x00be7d70)
// zero-extends them into the 16-bit TextureWidthNeeded/TextureHeightNeeded fields
// (+0x108/+0x10a); their only reader is the CEngineLandscapeMemoryManager::
// AllocateTexture call at 0x00be81b1, which creates one surface per patch through
// CTexture::Initialise with no smaller size cap. Retail never stores a zero extent
// (all 64,428 retail patches audited), so zero can encode 256 without changing any
// retail patch. Each site:
//     66 0f b6 4c 24 6c        movzx cx, byte ptr [esp+0x6c]
//     66 89 8e <field>         mov word ptr [esi+field], cx
// The first instruction becomes a jump to a stub that repeats it, maps 0 to 0x100
// with flags preserved, and returns to the store.
#include "patch_transaction.hpp"
#include <array>
#include <cstring>

namespace terrain_extent {
using texture_font::CodePatch;
using texture_font::PatchResult;

inline constexpr std::array<std::uint32_t,2> retailSites{0x00be80d5,0x00be812c};
inline constexpr std::array<std::uint32_t,2> retailFields{0x108,0x10a};
inline constexpr std::size_t siteLength=13,patchLength=6,stubLength=22;

inline std::array<std::uint8_t,siteLength> originalSite(std::uint32_t field) {
    std::array<std::uint8_t,siteLength> bytes{0x66,0x0f,0xb6,0x4c,0x24,0x6c,0x66,0x89,0x8e};
    std::memcpy(bytes.data()+9,&field,4);
    return bytes;
}

// movzx cx,[esp+0x6c]; pushfd; test cx,cx; jne +4; mov cx,0x100; popfd; jmp site+6
inline std::array<std::uint8_t,stubLength> stub(std::uint32_t stubAddress,std::uint32_t site) {
    std::array<std::uint8_t,stubLength> code{0x66,0x0f,0xb6,0x4c,0x24,0x6c,0x9c,0x66,0x85,0xc9,0x75,0x04,
                                             0x66,0xb9,0x00,0x01,0x9d,0xe9};
    const auto delta=std::uint32_t(site+patchLength-(stubAddress+stubLength));
    std::memcpy(code.data()+18,&delta,4);
    return code;
}

inline std::array<std::uint8_t,patchLength> jump(std::uint32_t site,std::uint32_t stubAddress) {
    std::array<std::uint8_t,patchLength> code{0xe9,0,0,0,0,0x90};
    const auto delta=std::uint32_t(stubAddress-(site+5));
    std::memcpy(code.data()+1,&delta,4);
    return code;
}

// Writes both stubs into stubMemory (2*stubLength bytes, executable and owned by
// the caller), then patches both sites in one transaction. Returns refused when
// either site does not hold the exact retail bytes; nothing is changed then.
template<class Memory>
PatchResult install(Memory& memory,const std::array<std::uint32_t,2>& sites,std::uint8_t* stubMemory) {
    std::array<std::array<std::uint8_t,siteLength>,2> originals;
    std::array<std::array<std::uint8_t,patchLength>,2> jumps;
    std::array<CodePatch,2> patches;
    for(unsigned i=0;i<2;++i) {
        originals[i]=originalSite(retailFields[i]);
        std::array<std::uint8_t,siteLength> live{};
        if(!memory.read(sites[i],live)) return PatchResult::refused;
        if(live!=originals[i]) return PatchResult::refused;
        const auto stubAddress=std::uint32_t(reinterpret_cast<std::uintptr_t>(stubMemory+i*stubLength));
        const auto code=stub(stubAddress,sites[i]);
        std::memcpy(stubMemory+i*stubLength,code.data(),code.size());
        jumps[i]=jump(sites[i],stubAddress);
        patches[i]={sites[i],std::span<const std::uint8_t>(originals[i].data(),patchLength),jumps[i]};
    }
    const std::span<const CodePatch> plan(patches);
    if constexpr(requires {memory.prepare(plan);})
        if(!memory.prepare(plan)) return PatchResult::refused;
    return texture_font::applyPatches(memory,plan);
}
}
