#include "local_code_memory.hpp"
#include <array>
#include <cstdio>
#include <stdexcept>
#include <cstring>
static void require(bool x){if(!x)throw std::runtime_error("local patch memory failed");}
int main() {
    struct Page {void* p;~Page(){if(p)VirtualFree(p,0,MEM_RELEASE);}} page{VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE)};
    require(page.p!=nullptr);
    const std::array<std::uint8_t,3> before{0x90,0x90,0xc3},after{0x31,0xc0,0xc3};
    std::memcpy(page.p,before.data(),before.size());DWORD old=0;
    require(VirtualProtect(page.p,4096,PAGE_EXECUTE_READ,&old)!=0);
    auto address=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(page.p));
    std::array<texture_font::CodePatch,1> patches{{{address,before,after}}};
    texture_font::LocalCodeMemory memory;require(memory.prepare(patches));
    require(texture_font::applyPatches(memory,patches)==texture_font::PatchResult::applied);
    require(std::memcmp(page.p,after.data(),3)==0);
    MEMORY_BASIC_INFORMATION info{};require(VirtualQuery(page.p,&info,sizeof info)!=0 && info.Protect==PAGE_EXECUTE_READ);
    patches[0]={address,after,before};require(memory.prepare(patches));
    require(texture_font::applyPatches(memory,patches)==texture_font::PatchResult::applied);
    require(std::memcmp(page.p,before.data(),3)==0);
    patches[0]={address+4095,before,after};require(!memory.prepare(patches));
    std::puts("PASS private executable-page patch/restore, exact RX protection and cross-page refusal");
}
