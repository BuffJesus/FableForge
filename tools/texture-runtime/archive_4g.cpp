// Archive4GB.dll: lets Fable read .big archives between 2 and 4 GiB.
// The engine's CAFile API uses signed 32-bit positions. CDiskFileWin32 compares
// positions against the file length with signed branches, and seeks with
// SetFilePointer(h, pos, NULL, FILE_BEGIN), which Windows treats as signed.
// BIG entry offsets are unsigned 32-bit, so 4 GiB is the format's own ceiling.
//  - SetPosition 0x99AC1C / MovePosition 0x99AD01: jle (pos <= length) -> jbe.
//  - MovePosition 0x99ACB0: checked signed-delta addition rejects wraparound,
//    then the unsigned length check still rejects positions beyond the file.
//  - SetFilePointer import: FILE_BEGIN with no high word seeks unsigned.
//  - BIG directory/entry-list input reads: unsigned 32-bit stream positions.
//  - Packed 32-bit offsets: a shift-by-32 carry must be zero, not the value.
// Async reads (ReadFileEx) already pass unsigned OVERLAPPED offsets.
// Valid reads below 2 GiB retain the native path.
#include "local_code_memory.hpp"
#include "archive_seek.hpp"
#include "archive_move_stub_generated.hpp"
#include "archive_stream_patches_generated.hpp"
#include "archive_pack_stub_generated.hpp"
#include <cstdio>

namespace {
HMODULE self=nullptr;
using SetFilePointerFn=DWORD(WINAPI*)(HANDLE,LONG,PLONG,DWORD);
SetFilePointerFn previous=nullptr;
constexpr std::uint32_t setFilePointerSlot=0x143fde8;

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

DWORD WINAPI setFilePointer(HANDLE file,LONG distance,PLONG high,DWORD method) {
    return texture_archive::seek(previous,file,distance,high,method);
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

void install() noexcept {
    if(!retailExecutable()) {log("refused: not the audited Fable.exe build; archive limit unchanged\r\n");return;}
    static constexpr std::uint8_t setJle[]{0x7e,0x47},setJbe[]{0x76,0x47};
    static constexpr std::uint8_t moveOriginal[]{0x8d,0x04,0x11,0x85,0xc0,0x7d,0x47};
    // Prepare executable code before touching the audited engine sites. This
    // allocation stays alive with the pinned DLL after a successful install.
    void* moveCode=VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!moveCode) {log("refused: relative-seek stub allocation failed\r\n");return;}
    const auto origin=reinterpret_cast<std::uint32_t>(moveCode);
    const auto stub=texture_archive::moveStub(origin);
    std::memcpy(moveCode,stub.data(),stub.size());
    const auto packOrigin=origin+256u;
    const auto packStub=texture_archive::packCarryStub(packOrigin);
    std::memcpy(static_cast<std::uint8_t*>(moveCode)+256,packStub.data(),packStub.size());
    DWORD oldProtection=0;
    if(!VirtualProtect(moveCode,4096,PAGE_EXECUTE_READ,&oldProtection) ||
       !FlushInstructionCache(GetCurrentProcess(),moveCode,256+packStub.size())) {
        VirtualFree(moveCode,0,MEM_RELEASE);
        log("refused: relative-seek stub protection failed\r\n");return;
    }
    std::uint8_t moveReplacement[]{0xe9,0,0,0,0,0x90,0x90};
    const auto relative=origin-(0x99acb0u+5u);
    std::memcpy(moveReplacement+1,&relative,4);
    std::array<std::uint8_t,9> packReplacement{0xe9,0,0,0,0,0x90,0x90,0x90,0x90};
    const auto packRelative=packOrigin-(0xa62d4au+5u);
    std::memcpy(packReplacement.data()+1,&packRelative,4);
    static std::uint8_t slotOriginal[4]{},slotReplacement[4]{};
    previous=*reinterpret_cast<SetFilePointerFn*>(std::uintptr_t(setFilePointerSlot));
    const auto hook=reinterpret_cast<std::uint32_t>(&setFilePointer);
    std::memcpy(slotOriginal,&previous,4);std::memcpy(slotReplacement,&hook,4);
    std::vector<texture_font::CodePatch> patches{
        {0x99ac1c,setJle,setJbe},     // SetPosition: position vs length
        {0x99acb0,moveOriginal,moveReplacement}, // MovePosition: checked target
        {0x99ad01,setJle,setJbe},     // MovePosition: target vs length
        {setFilePointerSlot,slotOriginal,slotReplacement},
        {0xa62d4a,texture_archive::packCarryOriginal,packReplacement},
    };
    const auto metadata=texture_archive::streamPatches();
    patches.insert(patches.end(),metadata.begin(),metadata.end());
    texture_font::LocalCodeMemory memory;
    if(!memory.prepare(patches)) {
        VirtualFree(moveCode,0,MEM_RELEASE);
        log("refused: patch pages unavailable; archive limit unchanged\r\n");return;
    }
    const auto result=texture_font::applyPatches(memory,std::span<const texture_font::CodePatch>(patches));
    if(result==texture_font::PatchResult::rollbackFailed) {log("fatal: archive patch rollback failed\r\n");ExitProcess(0xe000f400);}
    if(result!=texture_font::PatchResult::applied) {
        VirtualFree(moveCode,0,MEM_RELEASE);
        log(result==texture_font::PatchResult::refused
            ? "refused: patch sites differ from retail (another patch?); archive limit unchanged\r\n"
            : "refused: archive patch rolled back; archive limit unchanged\r\n");
        return;
    }
    // The import slot now points into this DLL: never unload it.
    HMODULE pinned=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&setFilePointer),&pinned);
    char line[240];
    std::snprintf(line,sizeof line,"applied: archives below 4 GiB (unsigned bounds, checked relative addition, packed 32-bit carry, %u metadata guards, SetFilePointer slot %08x)\r\n",unsigned(metadata.size()),setFilePointerSlot);
    log(line);
}
}

BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        self=module;
        DisableThreadLibraryCalls(module);
        install();
    }
    return TRUE;
}
