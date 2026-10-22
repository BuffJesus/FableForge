// Execute the generated checked-add stub on the host x86 CPU, with local tails.
#define NOMINMAX
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include "archive_move_stub_generated.hpp"

static unsigned accepted;
__declspec(naked) static void acceptTarget() {
    __asm {mov accepted,1}
    __asm {ret}
}
__declspec(naked) static void rejectTarget() {
    __asm {mov accepted,0}
    __asm {ret}
}
static void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}

int main() try {
    const auto code=VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    require(code!=nullptr,"allocate code");
    const auto stub=texture_archive::moveStub(reinterpret_cast<std::uint32_t>(code),
        reinterpret_cast<std::uint32_t>(&acceptTarget),reinterpret_cast<std::uint32_t>(&rejectTarget));
    std::memcpy(code,stub.data(),stub.size());
    DWORD old=0;
    require(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old)!=0,"protect code");
    require(FlushInstructionCache(GetCurrentProcess(),code,stub.size())!=0,"flush code");
    constexpr std::array<std::uint32_t,10> positions{0,1,2,0x7ffffffe,0x7fffffff,
        0x80000000,0x80000001,0xfffffffd,0xfffffffe,0xffffffff};
    constexpr std::array<std::int32_t,9> deltas{INT32_MIN,INT32_MIN+1,-0x7ffffffe,-2,-1,0,1,2,INT32_MAX};
    unsigned cases=0;
    for(auto current:positions)for(auto delta:deltas) {
        std::uint32_t result=0,afterCurrent=0,afterDelta=0;
        accepted=2;
        __asm {
            mov ecx,current
            mov edx,delta
            call code
            mov result,eax
            mov afterCurrent,ecx
            mov afterDelta,edx
        }
        const auto full=std::int64_t(current)+delta;
        require(accepted==unsigned(full>=0 && full<=UINT32_MAX),"wrong checked-add branch");
        require(result==std::uint32_t(full),"wrong target bits");
        require(afterCurrent==current && afterDelta==std::uint32_t(delta),"input registers changed");
        ++cases;
    }
    require(VirtualFree(code,0,MEM_RELEASE)!=0,"free code");
    std::printf("PASS Archive4GB checked-add hardware: %u underflow/overflow/register cases\n",cases);
} catch(const std::exception& error) {
    std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;
}
