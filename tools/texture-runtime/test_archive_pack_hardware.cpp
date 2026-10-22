// Execute the exact generated packed-carry correction on the host x86 CPU.
#define NOMINMAX
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include "archive_pack_stub_generated.hpp"

static void* entry;
static std::uint32_t afterEax,afterEcx,afterEdx,afterEdi,afterEbp,afterFlags;
__declspec(naked) static void continuation() {__asm {ret}}
__declspec(naked) static void exercise(unsigned,unsigned,unsigned) {
    __asm {
        push ebp
        push edi
        mov eax,[esp+12]
        mov ecx,[esp+16]
        mov edx,[esp+20]
        mov ebp,0x12345678
        mov edi,0x99aabbcc
        call entry
        mov afterEax,eax
        mov afterEcx,ecx
        mov afterEdx,edx
        mov afterEdi,edi
        mov afterEbp,ebp
        pushfd
        pop afterFlags
        pop edi
        pop ebp
        ret
    }
}
static void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}

int main() try {
    entry=VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    require(entry!=nullptr,"allocate packed-carry stub");
    const auto stub=texture_archive::packCarryStub(reinterpret_cast<std::uint32_t>(entry),reinterpret_cast<std::uint32_t>(&continuation));
    std::memcpy(entry,stub.data(),stub.size());DWORD old=0;
    require(VirtualProtect(entry,4096,PAGE_EXECUTE_READ,&old)!=0,"protect packed-carry stub");
    require(FlushInstructionCache(GetCurrentProcess(),entry,stub.size())!=0,"flush packed-carry stub");
    unsigned cases=0;
    for(unsigned width=1;width<=32;++width)for(unsigned previous=0;previous<32;++previous) {
        const auto total=width+previous;if(total<32)continue;
        const auto shift=32-previous;
        const auto mask=std::uint32_t((std::uint64_t(1)<<width)-1);
        const std::array<std::uint32_t,7> values{0,1,mask,mask-1,0x55555555u&mask,0xaaaaaaaau&mask,1u<<(width-1)};
        for(auto value:values) {
            exercise(total,shift,value);
            const auto carry=std::uint32_t(std::uint64_t(value)>>shift);
            require(afterEax==total-32 && afterEcx==shift && afterEdx==value,"native packed input/remainder registers");
            require(afterEdi==carry && afterEbp==carry,"native packed carry result");
            require(!(afterFlags&0x81u) && bool(afterFlags&0x40u)==(total==32),"native subtraction flags");
            ++cases;
        }
    }
    require(VirtualFree(entry,0,MEM_RELEASE)!=0,"free packed-carry stub");
    std::printf("PASS packed-carry hardware: %u width/boundary/register/flag cases\n",cases);
} catch(const std::exception& error) {std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
