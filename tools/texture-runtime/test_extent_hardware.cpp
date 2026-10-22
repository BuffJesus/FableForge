// Execute the SAME generated stub bytes as the Unicorn probe on Windows x86.
// Only this test process is modified; no game process or game file is touched.
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include "extent_stub_generated.hpp"

static unsigned expectedAxis,expectedNative,corrected,calls;
static bool argumentsOk;
static unsigned frontendMode;
static unsigned char uiObject[512]{};
unsigned __cdecl callback(unsigned bank,unsigned index,unsigned axis,unsigned native) {
    argumentsOk &= bank==0x456789ab && index==73 && axis==expectedAxis && native==expectedNative;
    ++calls;
    __asm {
        fninit
        pxor xmm0,xmm0
        pxor xmm1,xmm1
        pxor xmm2,xmm2
        pxor xmm3,xmm3
        pxor xmm4,xmm4
        pxor xmm5,xmm5
        pxor xmm6,xmm6
        pxor xmm7,xmm7
    }
    return corrected;
}

// Keep the original MOV [ESP+18],EAX inside our reserved stack area.
__declspec(naked) unsigned invoke(void* entry,unsigned native,void* before,void* after) {
    __asm {
        push ebp
        mov ebp,esp
        push ebx
        push esi
        push edi
        sub esp,64
        fninit
        fld1
        fldpi
        pcmpeqd xmm0,xmm0
        pcmpeqd xmm1,xmm1
        pcmpeqd xmm2,xmm2
        pcmpeqd xmm3,xmm3
        pcmpeqd xmm4,xmm4
        pcmpeqd xmm5,xmm5
        pcmpeqd xmm6,xmm6
        pcmpeqd xmm7,xmm7
        mov edx,[ebp+16]
        fxsave [edx]
        mov esi,456789abh
        mov edi,73
        cmp frontendMode,0
        je ready
        mov esi,offset uiObject
        mov ecx,456789abh
    ready:
        mov eax,[ebp+12]
        call dword ptr [ebp+8]
        mov edx,[ebp+20]
        fxsave [edx]
        fninit
        add esp,64
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}

int main() {
    static_assert(sizeof(void*)==4);
    auto code=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if (!code) return 2;
    unsigned cases=0;
    unsigned index=73;std::memcpy(uiObject+0x178,&index,4);
    for(frontendMode=0;frontendMode<2;++frontendMode)
    for (unsigned axis=0;axis<2;++axis) for (auto pair:std::array<std::array<unsigned,2>,5>{{
            {64,32},{128,32},{32,32},{0,0},{65535,32767}}}) {
        DWORD old;
        if(!VirtualProtect(code,4096,PAGE_READWRITE,&old)) return 3;
        auto stubSize=frontendMode?sizeof frontendStubTemplate:sizeof extentStubTemplate;
        std::memcpy(code,frontendMode?frontendStubTemplate:extentStubTemplate,stubSize);
        code[extentAxisOffset]=static_cast<unsigned char>(axis);
        auto rel=[&](unsigned offset,std::uintptr_t target) {
            auto delta=static_cast<std::uint32_t>(target-reinterpret_cast<std::uintptr_t>(code+offset+5));
            std::memcpy(code+offset+1,&delta,4);
        };
        rel(frontendMode?frontendCallOffset:extentCallOffset,reinterpret_cast<std::uintptr_t>(&callback));
        rel(unsigned(stubSize)-5,reinterpret_cast<std::uintptr_t>(code+512));
        code[512]=0xc3; // return after the replayed TEST/MOV
        if(!VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old) || !FlushInstructionCache(GetCurrentProcess(),code,4096)) return 4;
        alignas(16) std::array<unsigned char,512> before{},after{};
        expectedAxis=axis;expectedNative=pair[0];corrected=pair[1];calls=0;argumentsOk=true;
        auto result=invoke(code,pair[0],before.data(),after.data());
        if(result!=corrected || calls!=1 || !argumentsOk) return 5;
        // Includes the instruction/data pointer fields excluded in Unicorn.
        if(before!=after) {
            for(unsigned i=0;i<512;++i) if(before[i]!=after[i])
                std::cerr<<"FXSAVE mismatch "<<i<<" "<<unsigned(before[i])<<" "<<unsigned(after[i])<<"\n";
            return 6;
        }
        ++cases;
    }
    VirtualFree(code,0,MEM_RELEASE);
    std::cout<<"PASS "<<cases<<" Windows x86 callback cases; complete FXSAVE images match\n";
}
