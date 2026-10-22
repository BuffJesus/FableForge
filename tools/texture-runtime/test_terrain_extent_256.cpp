// Executes the exact retail extent-load instructions on a private executable page,
// before and after installing the zero-means-256 hook through the real backend.
#include "local_code_memory.hpp"
#include "terrain_extent_256.hpp"
#include <cstdio>
#include <stdexcept>
#include <vector>

static void require(bool x,const char* what){if(!x)throw std::runtime_error(what);}

// push esi; sub esp,0x70; mov [esp+0x6c],dl; mov esi,ecx; <retail site>; add esp,0x70; pop esi; ret
static std::size_t emitHarness(std::uint8_t* at,std::uint32_t field) {
    std::vector<std::uint8_t> code{0x56,0x83,0xec,0x70,0x88,0x54,0x24,0x6c,0x89,0xce};
    const auto site=terrain_extent::originalSite(field);
    code.insert(code.end(),site.begin(),site.end());
    code.insert(code.end(),{0x83,0xc4,0x70,0x5e,0xc3});
    std::memcpy(at,code.data(),code.size());
    return 10; // site offset
}

using Harness=void(__fastcall*)(std::uint8_t* object,int value);

static void check(Harness a,Harness b,bool hooked) {
    for(int value=0;value<256;++value) {
        std::uint8_t object[0x110]{};
        a(object,value);b(object,value);
        std::uint16_t width=0,height=0;
        std::memcpy(&width,object+0x108,2);std::memcpy(&height,object+0x10a,2);
        const std::uint16_t expected=hooked && value==0 ? 256 : std::uint16_t(value);
        require(width==expected && height==expected,"extent mismatch");
    }
}

int main() {
    auto* page=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,8192,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    require(page!=nullptr,"page allocation");
    std::memset(page,0xcc,8192);
    const auto siteA=emitHarness(page,0x108),siteB=emitHarness(page+64,0x10a);
    std::uint8_t* stubs=page+4096;
    DWORD old=0;
    require(VirtualProtect(page,4096,PAGE_EXECUTE_READ,&old)!=0,"harness protection");
    const auto a=reinterpret_cast<Harness>(page),b=reinterpret_cast<Harness>(page+64);
    const std::array<std::uint32_t,2> sites{std::uint32_t(reinterpret_cast<std::uintptr_t>(page+siteA)),
                                           std::uint32_t(reinterpret_cast<std::uintptr_t>(page+64+siteB))};
    check(a,b,false);

    // A foreign patch at either site is refused without touching anything.
    {
        texture_font::LocalCodeMemory memory;
        const std::array<std::uint32_t,2> wrong{sites[0]+1,sites[1]};
        require(terrain_extent::install(memory,wrong,stubs)==terrain_extent::PatchResult::refused,"misaligned site accepted");
        check(a,b,false);
    }
    texture_font::LocalCodeMemory memory;
    require(terrain_extent::install(memory,sites,stubs)==terrain_extent::PatchResult::applied,"install");
    MEMORY_BASIC_INFORMATION info{};
    require(VirtualQuery(page,&info,sizeof info) && (info.Protect&0xff)==PAGE_EXECUTE_READ,"harness protection not restored");
    check(a,b,true);
    // Installing twice is refused: the sites no longer hold retail bytes.
    texture_font::LocalCodeMemory again;
    require(terrain_extent::install(again,sites,stubs+64)==terrain_extent::PatchResult::refused,"double install accepted");
    check(a,b,true);
    std::puts("PASS terrain extent hook: retail 0..255 unchanged before install, 0->256 and 1..255 unchanged after, foreign/double install refused");
}
