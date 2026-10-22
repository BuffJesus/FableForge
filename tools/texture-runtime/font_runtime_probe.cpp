// Experimental scratch-only font hook. Not a distributable mod.
#include "prepared_font_runtime.hpp"
#include "font_glyph_callback.hpp"
#include "font_texture_create.hpp"
#include "local_code_memory.hpp"
#include "generated_streaming_records.hpp"
#include "generated_font_patches.hpp"
#include "generated_font_hooks.hpp"
#include <array>
#include <cstdio>
#include <string>

namespace {
using namespace texture_font;
HANDLE logFile=INVALID_HANDLE_VALUE;
PreparedFontRuntime* runtime=nullptr; // constructed outside the loader lock
std::uint8_t* stubs=nullptr;
bool active=false,verifiedExe=false;
DWORD ownerThread=0;
struct LastError {DWORD value=GetLastError();~LastError(){SetLastError(value);}};
void log(const char* message) noexcept {
    if(logFile!=INVALID_HANDLE_VALUE){DWORD written=0;WriteFile(logFile,message,DWORD(std::strlen(message)),&written,nullptr);}
}
[[noreturn]] void fatal() noexcept {
    log("fatal: incomplete patch rollback; refusing native initialization\r\n");
    RaiseFailFastException(nullptr,nullptr,0);ExitProcess(0xe000f017);
}
bool readMemory(std::uint32_t at,void* out,std::size_t size) noexcept {
    SIZE_T read=0;
    return at>=0x10000 && std::uint64_t(at)+size<=0x100000000ull &&
        ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(std::uintptr_t(at)),out,size,&read) && read==size;
}
bool writable(std::uint32_t at,std::size_t size) noexcept {
    std::uint64_t cursor=at,end=std::uint64_t(at)+size;
    if(at<0x10000 || end>0x100000000ull)return false;
    while(cursor<end) {
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(reinterpret_cast<void*>(std::uintptr_t(cursor)),&info,sizeof info) || info.State!=MEM_COMMIT || info.Protect&PAGE_GUARD)return false;
        const auto p=info.Protect&0xff;
        if(p!=PAGE_READWRITE && p!=PAGE_WRITECOPY && p!=PAGE_EXECUTE_READWRITE && p!=PAGE_EXECUTE_WRITECOPY)return false;
        const auto next=std::uint64_t(reinterpret_cast<std::uintptr_t>(info.BaseAddress))+info.RegionSize;
        if(next<=cursor)return false;cursor=next;
    }
    return true;
}
std::vector<std::uint8_t> branch(std::uint32_t site,std::uintptr_t target,std::size_t length,bool call=false) {
    std::vector<std::uint8_t> code(length,0x90);code[0]=call?0xe8:0xe9;
    const auto delta=std::uint32_t(target-site-5);std::memcpy(code.data()+1,&delta,4);return code;
}
PatchResult apply(std::span<const CodePatch> patches) {
    LocalCodeMemory memory;
    if(!memory.prepare(patches))return PatchResult::refused;
    return applyPatches(memory,patches);
}
bool __fastcall createTexture(void* texture,void*,const TextureExtent* extent,std::int32_t levels,
                             const void* format,std::uint32_t flags,std::uint32_t pool,bool dynamic,bool mipmaps) {
    const auto result=createFontCacheTexture(reinterpret_cast<NativeTextureCreate>(0x9fa280),texture,extent,
        levels,format,flags,pool,dynamic,mipmaps,active);
    if(!result)log("font cache texture creation failed\r\n");
    return result;
}
bool activate() {
    std::vector<CodePatch> patches(std::begin(texture_font_plan::immediates),std::end(texture_font_plan::immediates));
    std::array<std::vector<std::uint8_t>,6> replacement;
    const std::array<std::span<const std::uint8_t>,3> calls{texture_font_plan::textureCall0,texture_font_plan::textureCall1,texture_font_plan::textureCall2};
    for(unsigned i=0;i<3;++i) {
        const auto site=texture_font_plan::textureCallSites[i];
        replacement[i]=branch(site,reinterpret_cast<std::uintptr_t>(&createTexture),5,true);
        patches.push_back({site,calls[i],replacement[i]});
    }
    const std::array<const texture_font_hooks::Template*,3> hooks{&texture_font_hooks::glyph,&texture_font_hooks::bank,&texture_font_hooks::cache};
    for(unsigned i=0;i<3;++i) {
        const auto& h=*hooks[i];replacement[i+3]=branch(h.site,reinterpret_cast<std::uintptr_t>(stubs+(i+1)*256),h.original.size());
        patches.push_back({h.site,h.original,replacement[i+3]});
    }
    const auto result=apply(patches);
    if(result==PatchResult::rollbackFailed)fatal();
    return result==PatchResult::applied;
}
void __cdecl startup(std::uint32_t font) noexcept {
    LastError error;
    try {
        if(logFile==INVALID_HANDLE_VALUE)logFile=CreateFileW(std::filesystem::path(FONT_SCRATCH_ROOT L"/font-runtime-probe.log").c_str(),
            GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,0,nullptr);
        if(ownerThread && ownerThread!=GetCurrentThreadId()){log("startup thread differs; refused\r\n");return;}
        ownerThread=GetCurrentThreadId();
        if(!verifiedExe) {
            std::vector<std::uint8_t> exe;
            if(loadPinnedAlpha(FONT_SCRATCH_ROOT L"/Fable.exe",16666624,
                "41dc91090ae853715ac06d2e9fc96e5d545381d197ed55d624c642f34509ac10",exe)!=LoadError::none) {
                log("executable pin refused\r\n");return;
            }
            verifiedExe=true;
        }
        std::uint32_t cache=0,bank=0,entry=0;
        if(!readMemory(0x13bda24,&cache,4) || (!active && cache)) {log("existing stock cache; refused\r\n");return;}
        if(!readMemory(font+0x10,&bank,4) || !readMemory(bank+0x188,&entry,4)) {log("font identity unreadable\r\n");return;}
        if(!runtime)runtime=new PreparedFontRuntime;
        std::size_t alphaBytes=0;
        for(const auto& r:streamingGlyphRecords)if(r.present)alphaBytes=std::max(alphaBytes,std::size_t(r.offset)+r.pitch*r.pitch);
        if(!runtime->initialize(readMemory,font,{bank,entry,streamingEntryId,true},
            {FONT_SCRATCH_ROOT L"/data/lang/English/fonts.big",streamingArchiveBytes,streamingArchiveSha256},
            {FONT_ORIGINAL_PATH,streamingOriginalBytes,streamingOriginalSha256},
            {FONT_ALPHA_PATH,alphaBytes,streamingAlphaSha256},streamingGlyphRecords)) {
            log("font source verification refused\r\n");return;
        }
        if(!active) {
            if(!activate()){runtime->invalidate();log("cache patch activation refused or rolled back\r\n");return;}
            active=true;
        }
        log("verified font source active; enlarged cache patches installed\r\n");
    } catch(...) {if(runtime)runtime->invalidate();log("startup exception; source unavailable\r\n");}
}
bool __cdecl glyph(std::uint32_t font,std::uint32_t entry,std::uint32_t metadata,unsigned slot) noexcept {
    LastError error;
    if(!active || !runtime || ownerThread!=GetCurrentThreadId())return false;
    std::uint32_t cache=0,bytes=0;
    if(!readMemory(0x13bda24,&cache,4) || !readMemory(0x13bda28,&bytes,4) || bytes!=2097152 ||
       !writable(cache,bytes) || !writable(0x13bd808,512) || !writable(0x13c5a70,1))return false;
    return stageNativeGlyph(*runtime,readMemory,font,entry,metadata,slot,
        {reinterpret_cast<std::uint8_t*>(cache),bytes},{reinterpret_cast<std::uint8_t*>(0x13bd808),512},
        *reinterpret_cast<std::uint8_t*>(0x13c5a70))!=GlyphSource::failed;
}
void __cdecl closeBank(std::uint32_t bank) noexcept {
    LastError error;
    if(runtime && ownerThread==GetCurrentThreadId()){runtime->close(bank);log("bank source retired\r\n");}
}
void __cdecl closeCache(std::uint32_t) noexcept {
    LastError error;
    if(runtime && ownerThread==GetCurrentThreadId()){runtime->invalidate();log("shared cache source retired\r\n");}
}
bool bootstrap() {
    wchar_t enabled[8]{},path[MAX_PATH]{};
    if(GetEnvironmentVariableW(L"FABLE_FORGE_FONT_PROBE",enabled,8)!=1 || enabled[0]!=L'1')return false;
    const auto size=GetModuleFileNameW(nullptr,path,MAX_PATH);
    if(!size || size>=MAX_PATH || reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))!=0x400000)return false;
    std::wstring normalized(path);std::replace(normalized.begin(),normalized.end(),L'\\',L'/');
    if(_wcsicmp(normalized.c_str(),FONT_SCRATCH_ROOT L"/Fable.exe"))return false;
    // Only a small constructor-call hook is installed here; file hashing,
    // asset allocation and cache patch activation run in startup outside DllMain.
    const std::array<const texture_font_hooks::Template*,4> hooks{&texture_font_hooks::startup,&texture_font_hooks::glyph,&texture_font_hooks::bank,&texture_font_hooks::cache};
    const std::array<std::uintptr_t,4> callbacks{reinterpret_cast<std::uintptr_t>(&startup),reinterpret_cast<std::uintptr_t>(&glyph),reinterpret_cast<std::uintptr_t>(&closeBank),reinterpret_cast<std::uintptr_t>(&closeCache)};
    stubs=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!stubs)return false;
    for(unsigned i=0;i<4;++i) {
        const auto& h=*hooks[i];auto* code=stubs+i*256;
        std::memcpy(code,h.code.data(),h.code.size());
        for(const auto& r:h.relocations) {
            const auto target=r.target?std::uintptr_t(r.target):callbacks[i];
            const auto relative=std::uint32_t(target-reinterpret_cast<std::uintptr_t>(code+r.offset+4));
            std::memcpy(code+r.offset,&relative,4);
        }
    }
    DWORD old=0;
    if(!VirtualProtect(stubs,4096,PAGE_EXECUTE_READ,&old) || !FlushInstructionCache(GetCurrentProcess(),stubs,4096))return false;
    const auto& h=texture_font_hooks::startup;
    auto replacement=branch(h.site,reinterpret_cast<std::uintptr_t>(stubs),h.original.size(),true);
    const CodePatch patch{h.site,h.original,replacement};
    const auto result=apply({&patch,1});
    if(result==PatchResult::rollbackFailed)fatal();
    if(result!=PatchResult::applied){VirtualFree(stubs,0,MEM_RELEASE);stubs=nullptr;return false;}
    return true;
}
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH){DisableThreadLibraryCalls(module);try{bootstrap();}catch(...){}}
    // Hooks remain until process exit. Do not unload this experimental module.
    return TRUE;
}
