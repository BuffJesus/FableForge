#define NOMINMAX
#include <windows.h>
#include "hud_extent_hook.hpp"
#include <iostream>

static unsigned expectedGetter,overrideValue,calls;
static bool argumentsOk;
static unsigned bank[128]{};
static unsigned originalCalls;
static bool originalArgsOk;
unsigned __cdecl callback(unsigned getter,unsigned owner,unsigned caller,unsigned index) {
    argumentsOk &= getter==expectedGetter && owner==reinterpret_cast<unsigned>(bank) && caller!=0 && index==73;
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
    return overrideValue;
}
__declspec(naked) void originalUpdate() {
    __asm {
        inc originalCalls
        cmp dword ptr [esp+4],73
        jne badArgs
        cmp dword ptr [esp+8],19
        jne badArgs
        mov eax,offset bank
        cmp ecx,eax
        je done
    badArgs:
        mov originalArgsOk,0
    done:
        ret 8
    }
}
void __cdecl updateCallback(unsigned kind,unsigned internal,unsigned primitive) {
    argumentsOk &= kind==expectedGetter && internal==reinterpret_cast<unsigned>(bank) && primitive==73;
    callback(expectedGetter,reinterpret_cast<unsigned>(bank),1,73);
}
__declspec(naked) unsigned invoke(void* entry,unsigned getter,void* before,void* after) {
    __asm {
        push ebp
        mov ebp,esp
        push ebx
        push esi
        push edi
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
        mov ecx,offset bank
        cmp dword ptr [ebp+12],2
        jae maxGetter
        push 19
    maxGetter:
        push 73
        call dword ptr [ebp+8]
        lea edx,[ebp-12]
        cmp esp,edx
        jne badStack
        mov edx,[ebp+20]
        fxsave [edx]
        fninit
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    badStack:
        int 3
    }
}
int main() {
    static_assert(sizeof(void*)==4);
    auto code=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!code)return 2;
    unsigned cases=0;
    bank[0x1e0/4]=128;
    for(unsigned getter=0;getter<4;++getter)for(unsigned value:{32u,128u,texture_hud::unchanged}) {
        DWORD old=0;
        if(!VirtualProtect(code,4096,PAGE_READWRITE,&old))return 3;
        texture_hud::buildStub(code,getter,reinterpret_cast<std::uintptr_t>(&callback),reinterpret_cast<std::uintptr_t>(code+512));
        code[512]=0xc2;code[513]=getter<2?8:4;code[514]=0;
        if(!VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old) || !FlushInstructionCache(GetCurrentProcess(),code,4096))return 4;
        expectedGetter=getter;overrideValue=value;calls=0;argumentsOk=true;
        alignas(16) std::array<unsigned char,512> before{},after{};
        auto result=invoke(code,getter,before.data(),after.data());
        if(result!=(value==texture_hud::unchanged?128:value) || calls!=1 || !argumentsOk)return 5;
        if(before!=after)return 6;
        ++cases;
    }
    if(texture_hud::layoutCaller(0,0x5fbc6f)==false || texture_hud::layoutCaller(1,0x5fbc6f) ||
       texture_hud::layoutCaller(0,0xdeadbeef))return 7;
    for(unsigned kind=0;kind<2;++kind) {
        DWORD old=0;
        if(!VirtualProtect(code,4096,PAGE_READWRITE,&old))return 8;
        texture_hud::buildUpdateStub(code,kind,reinterpret_cast<std::uintptr_t>(&updateCallback),reinterpret_cast<std::uintptr_t>(&originalUpdate));
        if(!VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old) || !FlushInstructionCache(GetCurrentProcess(),code,4096))return 9;
        expectedGetter=kind;calls=0;originalCalls=0;argumentsOk=true;originalArgsOk=true;
        alignas(16) std::array<unsigned char,512> before{},after{};
        invoke(code,kind,before.data(),after.data());
        if(calls!=1 || originalCalls!=1 || !argumentsOk || !originalArgsOk || before!=after)return 10;
        ++cases;
    }
    VirtualFree(code,0,MEM_RELEASE);
    std::cout<<"PASS "<<cases<<" HUD x86 override/fallback cases; RET 4/8 and complete FXSAVE preserved\n";
}
