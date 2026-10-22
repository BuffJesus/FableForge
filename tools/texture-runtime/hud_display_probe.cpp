// Scratch-only main-bank HUD layout correction. Texture/UV callers stay native.
#define NOMINMAX
#include "local_code_memory.hpp"
#include "hud_extent_hook.hpp"
#include "hud_geometry.hpp"
#include "generated_hud_records.hpp"
#include <bcrypt.h>
#include <cstdio>
#include <string>

namespace {
HANDLE logFile=INVALID_HANDLE_VALUE;
void log(const char* text) {
    if(logFile!=INVALID_HANDLE_VALUE) {DWORD written;WriteFile(logFile,text,DWORD(std::strlen(text)),&written,nullptr);}
}
bool readMemory(std::uint32_t address,void* out,std::size_t size) {
    __try {std::memcpy(out,reinterpret_cast<const void*>(address),size);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool hashMatches(const std::string& path,const char* expected) {
    HANDLE file=CreateFileA(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    bool ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
    DWORD objectSize=0,used=0;
    if(ok)ok=BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&objectSize),4,&used,0)>=0;
    std::vector<unsigned char> object(objectSize),buffer(1024*1024);
    if(ok)ok=BCryptCreateHash(algorithm,&hash,object.data(),objectSize,nullptr,0,0)>=0;
    while(ok) {
        DWORD bytes=0;if(!ReadFile(file,buffer.data(),DWORD(buffer.size()),&bytes,nullptr)){ok=false;break;}
        if(!bytes)break;
        ok=BCryptHashData(hash,buffer.data(),bytes,0)>=0;
    }
    std::array<unsigned char,32> digest{};
    if(ok)ok=BCryptFinishHash(hash,digest.data(),DWORD(digest.size()),0)>=0;
    if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);CloseHandle(file);
    char hex[65]{};for(unsigned i=0;i<32;++i)std::snprintf(hex+i*2,3,"%02x",digest[i]);
    return ok && std::strcmp(hex,expected)==0;
}
unsigned __cdecl resolve(unsigned getter,unsigned bank,unsigned caller,unsigned index) noexcept {
    const auto error=GetLastError();
    unsigned result=texture_hud::unchanged;
    if(texture_hud::layoutCaller(getter,caller)) {
        std::array<unsigned,2> range{};
        std::array<unsigned char,44> graphic{};
        if(readMemory(bank+0x1e0,range.data(),8) && range[1]>=range[0] &&
           range[1]-range[0]==texture_display::mainRecordCount*44 && index<texture_display::mainRecordCount &&
           readMemory(range[0]+index*44,graphic.data(),graphic.size())) {
            const unsigned axis=getter%2;
            std::uint16_t native=0;std::memcpy(&native,graphic.data()+6+axis*2,2);
            bool valid=true;
            if(getter>=2) {
                // Max getters traverse per-frame headers. Only proven single-frame
                // entries are eligible; animated/irregular maxima retain native code.
                std::uint16_t frames=0;std::memcpy(&frames,graphic.data()+10,2);
                unsigned vector=0,frame=0;std::uint16_t maximum=0;
                std::memcpy(&vector,graphic.data()+40,4);
                valid=frames==1 && vector && readMemory(vector+12,&frame,4) && frame &&
                    readMemory(frame+6+axis*2,&maximum,2) && maximum==native;
            }
            if(valid) {
                texture_display::Resolver resolver(&readMemory,std::span<const texture_display::Record>{});
                const auto match=resolver.indexedExtent(bank,index,axis,native,"GBANK_MAIN",
                    texture_display::mainRecordCount,texture_display::mainRecords);
                if(match.reason==texture_display::Reason::matched) {
                    result=match.value;
                    static volatile LONG counts[std::size(texture_hud::callers)]{};
                    unsigned slot=0;
                    while(texture_hud::callers[slot].address!=caller || texture_hud::callers[slot].getter!=getter)++slot;
                    if(InterlockedIncrement(&counts[slot])<=4) {
                        char line[180];std::snprintf(line,sizeof line,"HUD caller=%08x getter=%u index=%u native=%u result=%u\r\n",caller,getter,index,native,result);log(line);
                    }
                }
            }
        }
    }
    SetLastError(error);return result;
}
void __cdecl updateGeometry(unsigned kind,unsigned internal,unsigned primitive) noexcept {
    const auto error=GetLastError();
    unsigned engine=0,bank=0,index=0;
    if(readMemory(0x1436e98,&engine,4) && engine && readMemory(engine+0x954,&bank,4) && bank &&
       readMemory(internal+0x5c,&index,4) && index<texture_display::mainRecordCount) {
        auto box=texture_hud::correctedBox(&readMemory,bank,internal,primitive,kind,
            texture_display::mainRecordCount,texture_display::mainRecords);
        if(box) {
            SIZE_T written=0;
            if(WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(internal+0x48),box->data(),sizeof(*box),&written) && written==sizeof(*box)) {
                static volatile LONG seen[2][texture_display::mainRecordCount]{};
                if(InterlockedIncrement(&seen[kind][index])==1) {
                    char line[180];std::snprintf(line,sizeof line,"geometry kind=%u index=%u box=%.1f,%.1f,%.1f,%.1f\r\n",kind,index,(*box)[0],(*box)[1],(*box)[2],(*box)[3]);log(line);
                }
            }
        }
    }
    SetLastError(error);
}
bool install() {
    char enabled[8]{};
    if(GetEnvironmentVariableA("FABLE_FORGE_DISPLAY_PROBE",enabled,sizeof enabled)!=1 || enabled[0]!='1')return false;
    char executable[MAX_PATH]{};auto size=GetModuleFileNameA(nullptr,executable,MAX_PATH);
    if(!size || size>=MAX_PATH)return false;
    std::string path(executable);for(auto& c:path)if(c=='\\')c='/';
    const std::string root=HUD_SCRATCH_ROOT;
    if(_stricmp(path.c_str(),(root+"/Fable.exe").c_str())!=0)return false;
    logFile=CreateFileA((root+"/hud-display-probe.log").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))!=0x400000 ||
       !hashMatches(path,"41dc91090ae853715ac06d2e9fc96e5d545381d197ed55d624c642f34509ac10") ||
       !hashMatches(root+"/data/graphics/pc/textures.big",texture_display::mainArchiveSha256)) {
        log("refused: executable or main archive pin differs\r\n");return false;
    }
    auto stubs=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!stubs)return false;
    std::array<std::array<std::uint8_t,8>,6> branches{};
    std::array<texture_font::CodePatch,6> patches{};
    for(unsigned i=0;i<4;++i) {
        texture_hud::buildStub(stubs+i*128,i,reinterpret_cast<std::uintptr_t>(&resolve));
        auto& branch=branches[i];branch={0xe9,0,0,0,0,0x90,0x90,0x90};
        auto delta=std::uint32_t(reinterpret_cast<std::uintptr_t>(stubs+i*128)-texture_hud::getters[i]-5);
        std::memcpy(branch.data()+1,&delta,4);
        patches[i]={texture_hud::getters[i],texture_hud::entryBytes,std::span<const std::uint8_t>(branch.data(),6)};
    }
    for(unsigned kind=0;kind<2;++kind) {
        auto code=stubs+512+kind*512;
        const auto original=kind?std::span<const unsigned char>(texture_hud::clockUpdateBytes):std::span<const unsigned char>(texture_hud::spriteUpdateBytes);
        std::memcpy(code+256,original.data(),original.size());
        code[256+original.size()]=0xe9;
        auto delta=std::uint32_t(texture_hud::updateSites[kind]+original.size()-reinterpret_cast<std::uintptr_t>(code+256+original.size()+5));
        std::memcpy(code+256+original.size()+1,&delta,4);
        texture_hud::buildUpdateStub(code,kind,reinterpret_cast<std::uintptr_t>(&updateGeometry),reinterpret_cast<std::uintptr_t>(code+256));
        auto& branch=branches[4+kind];branch={0xe9,0,0,0,0,0x90,0x90,0x90};
        delta=std::uint32_t(reinterpret_cast<std::uintptr_t>(code)-texture_hud::updateSites[kind]-5);
        std::memcpy(branch.data()+1,&delta,4);
        patches[4+kind]={texture_hud::updateSites[kind],original,std::span<const std::uint8_t>(branch.data(),original.size())};
    }
    DWORD old=0;
    if(!VirtualProtect(stubs,4096,PAGE_EXECUTE_READ,&old) || !FlushInstructionCache(GetCurrentProcess(),stubs,4096)) {
        VirtualFree(stubs,0,MEM_RELEASE);return false;
    }
    texture_font::LocalCodeMemory memory;
    if(!memory.prepare(patches)) {VirtualFree(stubs,0,MEM_RELEASE);return false;}
    auto status=texture_font::applyPatches(memory,patches);
    if(status==texture_font::PatchResult::rollbackFailed) {log("fatal: rollback failed\r\n");ExitProcess(0xe000f257);}
    if(status!=texture_font::PatchResult::applied) {VirtualFree(stubs,0,MEM_RELEASE);return false;}
    HMODULE pinned=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&resolve),&pinned);
    log("installed four caller-filtered HUD extent hooks and two sprite geometry hooks\r\n");return true;
}
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        try {if(!install())log("not installed\r\n");}catch(...) {log("startup exception\r\n");}
    }
    return TRUE;
}
