#pragma once
#include <array>
#include <cstdint>
#include <cstring>

namespace texture_hud {
inline constexpr std::array<unsigned,4> getters{0xa01430,0xa01480,0xa014d0,0xa01540};
inline constexpr std::array<unsigned char,6> entryBytes{0x8b,0x81,0xe0,0x01,0x00,0x00};
inline constexpr unsigned unchanged=0xffffffffu;
struct Caller {unsigned address,getter;};
// Retail disassembly: quad bounds, CSprite screen-size caches, text/stat box
// borders, health-bar geometry, minimap pixel positions/border and marker sizes.
// This list deliberately excludes every unobserved caller (including UV paths).
inline constexpr Caller callers[]{
    {0x41a8fc,0},{0x41a94d,1},{0x41ad05,0},{0x41ad55,1},{0x41b58a,0},{0x41b5da,1},
    {0x48e1a9,2},{0x48e1c9,2},{0x48e1ef,3},{0x48e299,3},{0x48e2a6,3},
    {0x48e347,3},{0x48e367,3},{0x48e3a3,2},{0x48e45a,2},{0x48e592,3},{0x48e647,2},
    {0x48e9a2,2},{0x5fbc6f,0},{0x5fbcb5,1},
    {0x645abd,2},{0x645ace,2},{0x645aef,3},{0x645b30,2},{0x645d8a,2},
    {0x645dc0,3},{0x645dcd,2},{0x6461f5,2},{0x6462cc,2},
    {0x647fb5,1},{0x647fd8,0},{0x648179,1},{0x650a48,0},{0x650a67,1},{0x654482,3},
    {0x82a75f,0},{0x82a786,1},{0x9dcadf,0},{0x9dcb0e,0},{0x9dcb34,1},{0x9dcb6c,1}
};
inline bool layoutCaller(unsigned getter,unsigned address) {
    for(auto c:callers) if(c.getter==getter && c.address==address) return true;
    return false;
}

// Entry callback returns an override or unchanged. Preserve all integer/FP state;
// fallback executes the exact displaced instruction then the original getter.
// Frame getters clean TWO arguments; Max getters clean ONE.
inline std::size_t buildStub(unsigned char* code,unsigned getter,std::uintptr_t callback,
                             std::uintptr_t fallback=0) {
    std::size_t n=0;
    auto put=[&](std::initializer_list<unsigned char> bytes){for(auto b:bytes)code[n++]=b;};
    auto rel=[&](std::uintptr_t target){
        auto delta=std::uint32_t(target-reinterpret_cast<std::uintptr_t>(code+n+4));
        std::memcpy(code+n,&delta,4);n+=4;
    };
    put({0x9c,0x60,0x89,0xe5,0x81,0xec,0x10,0x02,0,0,0x83,0xe4,0xf0});
    put({0x0f,0xae,0x04,0x24});
    put({0xff,0x75,0x28,0xff,0x75,0x24,0x51,0x6a,static_cast<unsigned char>(getter)});
    put({0xe8});rel(callback);
    put({0x83,0xc4,0x10,0x89,0x45,0x1c,0x0f,0xae,0x0c,0x24,0x89,0xec,0x61,0x9d});
    put({0x83,0xf8,0xff,0x74,0x03,0xc2,static_cast<unsigned char>(getter<2?8:4),0});
    for(auto b:entryBytes)code[n++]=b;
    put({0xe9});rel(fallback?fallback:getters[getter]+entryBytes.size());
    return n;
}
inline constexpr std::array<unsigned,2> updateSites{0xbad8a0,0xbadb40};
inline constexpr std::array<unsigned char,5> spriteUpdateBytes{0x83,0xec,0x08,0x53,0x56};
// Clock entry needs its complete MOV ECX,[ESP+10] (8 bytes total).
inline constexpr std::array<unsigned char,8> clockUpdateBytes{0x53,0x56,0x8b,0xf1,0x8b,0x4c,0x24,0x10};
inline std::size_t buildUpdateStub(unsigned char* code,unsigned kind,std::uintptr_t callback,
                                  std::uintptr_t original) {
    std::size_t n=0;
    auto put=[&](std::initializer_list<unsigned char> bytes){for(auto b:bytes)code[n++]=b;};
    auto rel=[&](std::uintptr_t target){
        auto delta=std::uint32_t(target-reinterpret_cast<std::uintptr_t>(code+n+4));
        std::memcpy(code+n,&delta,4);n+=4;
    };
    put({0x55,0x89,0xe5,0x51,0xff,0x75,0x0c,0xff,0x75,0x08,0xe8});rel(original);
    put({0x9c,0x60,0x89,0xee,0x89,0xe5,0x81,0xec,0x10,0x02,0,0,0x83,0xe4,0xf0});
    put({0x0f,0xae,0x04,0x24,0xff,0x76,0x08,0xff,0x76,0xfc,0x6a,static_cast<unsigned char>(kind)});
    put({0xe8});rel(callback);
    put({0x83,0xc4,0x0c,0x0f,0xae,0x0c,0x24,0x89,0xec,0x61,0x9d,0x83,0xc4,0x04,0x5d,0xc2,0x08,0});
    return n;
}
}
