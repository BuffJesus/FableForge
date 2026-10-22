#pragma once
#include "patch_transaction.hpp"
#define NOMINMAX
#include <windows.h>

namespace texture_font {
// Current process only. Prepare while execution at these sites is excluded.
// Single-page instructions are required so every protection restoration is exact.
class LocalCodeMemory {
    struct Page {void* address;SIZE_T size;DWORD protection;};
    std::vector<Page> pages_;
public:
    bool prepare(std::span<const CodePatch> patches) {
        pages_.clear();SYSTEM_INFO system{};GetSystemInfo(&system);
        for(const auto& p:patches) {
            const auto page=std::uintptr_t(p.address)/system.dwPageSize*system.dwPageSize;
            if(p.original.empty() || p.original.size()>system.dwPageSize-(p.address-page))return false;
            if(std::any_of(pages_.begin(),pages_.end(),[&](const auto& q){return std::uintptr_t(q.address)==page;}))continue;
            MEMORY_BASIC_INFORMATION info{};
            if(!VirtualQuery(reinterpret_cast<void*>(page),&info,sizeof info) || info.State!=MEM_COMMIT ||
               (info.Protect&PAGE_GUARD) || (info.Protect&0xff)==PAGE_NOACCESS)return false;
            pages_.push_back({reinterpret_cast<void*>(page),system.dwPageSize,info.Protect});
        }
        return !pages_.empty();
    }
    bool read(std::uint32_t at,std::span<std::uint8_t> out) noexcept {
        SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(std::uintptr_t(at)),out.data(),out.size(),&got) && got==out.size();
    }
    bool write(std::uint32_t at,std::span<const std::uint8_t> bytes) noexcept {
        const auto page=std::find_if(pages_.begin(),pages_.end(),[&](const auto& p) {
            const auto start=std::uintptr_t(p.address);
            return at>=start && std::uint64_t(at)+bytes.size()<=start+p.size;
        });
        if(page==pages_.end())return false;
        DWORD old=0,ignored=0;
        if(!VirtualProtect(page->address,page->size,PAGE_EXECUTE_READWRITE,&old))return false;
        SIZE_T written=0;
        const bool copied=WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(std::uintptr_t(at)),bytes.data(),bytes.size(),&written) && written==bytes.size();
        const bool restored=VirtualProtect(page->address,page->size,page->protection,&ignored)!=0;
        return copied && restored;
    }
    bool flush() noexcept {
        bool ok=true;
        // Retry restoration even when a prior write reported a protection error.
        for(const auto& p:pages_) {DWORD ignored=0;if(!VirtualProtect(p.address,p.size,p.protection,&ignored))ok=false;}
        const bool flushed=FlushInstructionCache(GetCurrentProcess(),nullptr,0)!=0;
        return ok && flushed;
    }
};
}
