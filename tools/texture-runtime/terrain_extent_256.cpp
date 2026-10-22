// TerrainExtent256.dll: lets STB background patches allocate 256-pixel terrain
// textures (stored extent byte 0 = 256). Loaded through Mods.ini like the 2K
// texture-limit DLL. Non-zero extents keep retail behavior exactly.
#include "local_code_memory.hpp"
#include "terrain_extent_256.hpp"
#include <cstdio>

namespace {
HMODULE self=nullptr;

void log(const char* message) noexcept {
    wchar_t path[MAX_PATH];
    const DWORD length=GetModuleFileNameW(self,path,MAX_PATH);
    if(length==0 || length>=MAX_PATH-8)return;
    wchar_t* dot=wcsrchr(path,L'.');
    if(!dot)return;
    wcscpy_s(dot,MAX_PATH-(dot-path),L".log");
    const HANDLE file=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return;
    DWORD written=0;
    WriteFile(file,message,DWORD(std::strlen(message)),&written,nullptr);
    CloseHandle(file);
}

bool retailExecutable() noexcept {
    const auto base=reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
    if(reinterpret_cast<std::uintptr_t>(base)!=0x400000)return false;
    const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE)return false;
    const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    // Fable.exe 25.7.13.149 (Steam TLC), the build the site addresses come from.
    return nt->Signature==IMAGE_NT_SIGNATURE && nt->FileHeader.TimeDateStamp==0x42d7dbdf;
}

void installHooks() noexcept {
    if(!retailExecutable()) {log("refused: not the audited Fable.exe build; terrain extents unchanged\r\n");return;}
    auto* stubs=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!stubs) {log("refused: stub allocation failed\r\n");return;}
    texture_font::LocalCodeMemory memory;
    std::memset(stubs,0xcc,4096);
    DWORD old=0;
    if(!VirtualProtect(stubs,4096,PAGE_EXECUTE_READWRITE,&old)) {VirtualFree(stubs,0,MEM_RELEASE);log("refused: stub protection failed\r\n");return;}
    const auto result=terrain_extent::install(memory,terrain_extent::retailSites,stubs);
    if(result!=terrain_extent::PatchResult::applied) {
        if(result==terrain_extent::PatchResult::rollbackFailed) {
            // A half-patched loader cannot be left running; the stubs stay mapped.
            log("fatal: terrain extent patch rollback failed\r\n");
            ExitProcess(0xe000f256);
        }
        VirtualFree(stubs,0,MEM_RELEASE);
        log(result==terrain_extent::PatchResult::refused
            ? "refused: terrain extent sites differ from retail (another patch?); terrain extents unchanged\r\n"
            : "refused: terrain extent patch rolled back; terrain extents unchanged\r\n");
        return;
    }
    // The stubs are live from here on and are never released.
    if(!VirtualProtect(stubs,4096,PAGE_EXECUTE_READ,&old))log("warning: stub page left writable\r\n");
    FlushInstructionCache(GetCurrentProcess(),stubs,4096);
    char line[128];
    std::snprintf(line,sizeof line,"applied: zero terrain extents allocate 256 (sites 0x%08x 0x%08x)\r\n",
                  terrain_extent::retailSites[0],terrain_extent::retailSites[1]);
    log(line);
}
}

BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        self=module;
        DisableThreadLibraryCalls(module);
        // Terrain patches load long after mod DLLs; no thread executes these sites yet.
        installHooks();
        // Pin the module: the patched sites jump into stubs this DLL owns.
        HMODULE pinned=nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&installHooks),&pinned);
    }
    return TRUE;
}
