// Scratch-only startup probe. Not a redistributable mod or production hook.
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <cstdio>
#include <string>
#include <vector>
#include "generated_display_records.hpp"
#include "generated_frontend_records.hpp"
#include "extent_stub_generated.hpp"

namespace {
HANDLE logFile=INVALID_HANDLE_VALUE;
void log(const char* text) {
    if(logFile!=INVALID_HANDLE_VALUE) { DWORD written;WriteFile(logFile,text,DWORD(std::strlen(text)),&written,nullptr); }
}
bool readMemory(std::uint32_t address,void* out,std::size_t size) {
    __try { std::memcpy(out,reinterpret_cast<const void*>(address),size);return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void dump(const char* label,unsigned address,unsigned size) {
    std::array<unsigned char,544> bytes{};
    char line[1200];
    if(size>bytes.size() || !readMemory(address,bytes.data(),size)) {
        std::snprintf(line,sizeof line,"dump %s %08x unreadable\r\n",label,address);log(line);return;
    }
    auto used=std::snprintf(line,sizeof line,"dump %s %08x ",label,address);
    for(unsigned i=0;i<size;++i) used+=std::snprintf(line+used,sizeof(line)-used,"%02x",bytes[i]);
    std::snprintf(line+used,sizeof(line)-used,"\r\n");log(line);
}
bool hashMatches(const std::string& path,const char* expected) {
    HANDLE file=CreateFileA(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE algorithm=nullptr; BCRYPT_HASH_HANDLE hash=nullptr;
    bool ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
    DWORD objectSize=0,used=0;
    if(ok) ok=BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&objectSize),4,&used,0)>=0;
    std::vector<unsigned char> object(objectSize),buffer(1024*1024);
    if(ok) ok=BCryptCreateHash(algorithm,&hash,object.data(),objectSize,nullptr,0,0)>=0;
    while(ok) {
        DWORD bytes=0;if(!ReadFile(file,buffer.data(),DWORD(buffer.size()),&bytes,nullptr)) {ok=false;break;}
        if(!bytes) break;
        ok=BCryptHashData(hash,buffer.data(),bytes,0)>=0;
    }
    std::array<unsigned char,32> digest{};
    if(ok) ok=BCryptFinishHash(hash,digest.data(),DWORD(digest.size()),0)>=0;
    if(hash) BCryptDestroyHash(hash);if(algorithm) BCryptCloseAlgorithmProvider(algorithm,0);
    CloseHandle(file);
    char hex[65]{};for(unsigned i=0;i<32;++i) std::snprintf(hex+i*2,3,"%02x",digest[i]);
    return ok && std::strcmp(hex,expected)==0;
}
unsigned __cdecl resolve(unsigned bank,unsigned index,unsigned axis,unsigned native) {
    const auto error=GetLastError();
    texture_display::Resolver resolver(&readMemory,std::span<const texture_display::Record>(texture_display::displayRecords));
    auto result=resolver.extent(bank,index,axis,native);
    if(result.reason!=texture_display::Reason::matched)
        result=resolver.frontendExtent(bank,index,axis,native,texture_display::frontendRecords);
    static unsigned lastBank=0,lastIndex=~0u,unique=0;
    if((bank!=lastBank || index!=lastIndex) && unique++<96) {
        if(bank!=lastBank) {
            dump("bank",bank,544);
            std::array<unsigned,2> range{};
            if(readMemory(bank+0x1e0,range.data(),8) && range[1]>=range[0] &&
               (range[1]-range[0])%44==0 && (range[1]-range[0])/44<=10000) {
                for(unsigned i=0;i<(range[1]-range[0])/44;++i) {
                    char label[48];std::snprintf(label,sizeof label,"graphic-all-%u",i);
                    dump(label,range[0]+i*44,44);
                }
            }
        }
        lastBank=bank;lastIndex=index;
        char line[256];std::snprintf(line,sizeof line,"lookup bank=%08x index=%u axis=%u native=%u result=%u reason=%u\r\n",
            bank,index,axis,native,result.value,unsigned(result.reason));log(line);
        // Capture exact runtime handle spelling without dereferencing unchecked pointers.
        unsigned shared=0;std::array<unsigned,2> data{};std::array<char,257> text{};
        if(readMemory(bank+0x88,&shared,4) && shared && readMemory(shared,data.data(),8) &&
           data[1]<256 && readMemory(data[0],text.data(),data[1]+1) && text[data[1]]==0) {
            log("handle=");log(text.data());log("\r\n");
        }
        std::array<unsigned,2> symbols{},graphics{};
        if(readMemory(bank+8,symbols.data(),8) && index<1000000) {
            dump("symbol-vector",bank+8,12);
            unsigned ptr=0;
            if(readMemory(symbols[0]+index*4,&ptr,4)) {
                dump("symbol-shared",ptr,20);
                if(readMemory(ptr,data.data(),8) && data[1]<256) dump("symbol-text",data[0],data[1]+1);
            }
        }
        if(readMemory(bank+0x1e0,graphics.data(),8) && index<1000000) {
            dump("graphics-vector",bank+0x1e0,12);
            dump("graphic",graphics[0]+index*44,44);
        }
    }
    SetLastError(error);return result.value;
}
bool install() {
    char enabled[8]{};
    if(GetEnvironmentVariableA("FABLE_FORGE_DISPLAY_PROBE",enabled,sizeof enabled)!=1 || enabled[0]!='1') return false;
    char executable[MAX_PATH]{};DWORD size=GetModuleFileNameA(nullptr,executable,MAX_PATH);
    if(!size || size>=MAX_PATH) return false;
    std::string path(executable);for(auto& c:path) if(c=='\\') c='/';
    const std::string root=DISPLAY_SCRATCH_ROOT;
    if(_stricmp(path.c_str(),(root+"/Fable.exe").c_str())!=0) return false;
    logFile=CreateFileA((root+"/texture-display-probe.log").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    log("startup verification\r\n");
    if(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))!=0x400000 ||
       !hashMatches(path,"41dc91090ae853715ac06d2e9fc96e5d545381d197ed55d624c642f34509ac10")) return false;
    for(const auto& pin:texture_display::archivePins)
        if(!hashMatches(root+"/"+pin.path,pin.sha256)) {log("archive hash mismatch\r\n");return false;}
    constexpr std::array<unsigned,2> sites{0x41a8fc,0x41a94d};
    constexpr unsigned char expected[]{0x85,0xc0,0x89,0x44,0x24,0x08};
    for(auto site:sites) if(std::memcmp(reinterpret_cast<void*>(site),expected,6)!=0) return false;
    auto stubs=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!stubs) return false;
    for(unsigned i=0;i<sites.size();++i) {
        auto code=stubs+i*128;
        std::memcpy(code,frontendStubTemplate,sizeof frontendStubTemplate);code[extentAxisOffset]=i;
        auto relative=[&](unsigned offset,std::uintptr_t target) {
            auto delta=std::uint32_t(target-reinterpret_cast<std::uintptr_t>(code+offset+5));
            std::memcpy(code+offset+1,&delta,4);
        };
        relative(frontendCallOffset,reinterpret_cast<std::uintptr_t>(&resolve));
        relative(sizeof frontendStubTemplate-5,sites[i]+6);
    }
    DWORD old=0;
    if(!VirtualProtect(stubs,4096,PAGE_EXECUTE_READ,&old) || !FlushInstructionCache(GetCurrentProcess(),stubs,4096)) {
        VirtualFree(stubs,0,MEM_RELEASE);return false;
    }
    // Both audited cache sites share this page. Startup-only; never hot-install.
    auto page=reinterpret_cast<void*>(0x41a000);
    if(!VirtualProtect(page,4096,PAGE_EXECUTE_READWRITE,&old)) {VirtualFree(stubs,0,MEM_RELEASE);return false;}
    for(unsigned i=0;i<sites.size();++i) {
        std::array<unsigned char,6> branch{0xe9,0,0,0,0,0x90};
        auto delta=std::uint32_t(reinterpret_cast<std::uintptr_t>(stubs+i*128)-sites[i]-5);
        std::memcpy(branch.data()+1,&delta,4);std::memcpy(reinterpret_cast<void*>(sites[i]),branch.data(),6);
    }
    DWORD ignored;VirtualProtect(page,4096,old,&ignored);FlushInstructionCache(GetCurrentProcess(),page,4096);
    log("installed two frontend cache hooks\r\n");return true;
}
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        try {if(!install()) log("not installed\r\n");}
        catch(...) {log("startup exception; not installed\r\n");}
    }
    return TRUE;
}
