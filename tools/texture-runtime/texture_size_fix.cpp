// TextureSizeFix.dll: upscaled interface textures keep their retail on-screen size.
// Production form of TextureDisplayProbe + HudDisplayProbe: the same audited
// getter hooks (41 layout callers) and sprite Update geometry hooks, but the
// records come from TextureSizes.dat beside the DLL (build_texture_size_data.py),
// so any rebuilt bank works. Identity is the live 28-byte header plus owner/index
// of every graphic; a texture whose live header differs keeps its native size.
// Texture creation, UVs and sampling are never touched.
#define NOMINMAX
#include "local_code_memory.hpp"
#include "hud_extent_hook.hpp"
#include "hud_geometry.hpp"
#include <cstdio>
#include <string>
#include <vector>

namespace {
HMODULE self=nullptr;
HANDLE logFile=INVALID_HANDLE_VALUE;
void log(const char* text) {
    if(logFile!=INVALID_HANDLE_VALUE) {DWORD written;WriteFile(logFile,text,DWORD(std::strlen(text)),&written,nullptr);}
}
bool readMemory(std::uint32_t address,void* out,std::size_t size) {
    __try {std::memcpy(out,reinterpret_cast<const void*>(address),size);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

struct Table {
    std::string handle;
    unsigned slots=0;
    std::vector<texture_display::IndexedRecord> rows;
};
Table mainTable,frontendTable;

std::wstring besideModule(const wchar_t* name) {
    wchar_t path[MAX_PATH];
    const DWORD length=GetModuleFileNameW(self,path,MAX_PATH);
    if(!length || length>=MAX_PATH) return {};
    std::wstring result(path,length);
    return result.substr(0,result.find_last_of(L"\\/")+1)+name;
}

// TextureSizes.dat: 'FTSZ' u32 version=1, u32 banks; per bank: char handle[32],
// u32 slots, u32 rows, rows x {u32 index, u8 header[28], u16 width, u16 height}.
bool loadTables() {
    const HANDLE file=CreateFileW(besideModule(L"TextureSizes.dat").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) {log("refused: TextureSizes.dat not found beside the DLL\r\n");return false;}
    LARGE_INTEGER size{};
    std::vector<unsigned char> data;
    bool read=GetFileSizeEx(file,&size) && size.QuadPart>=12 && size.QuadPart<64*1024*1024;
    if(read) {
        data.resize(std::size_t(size.QuadPart));DWORD got=0;
        read=ReadFile(file,data.data(),DWORD(data.size()),&got,nullptr) && got==data.size();
    }
    CloseHandle(file);
    std::size_t at=0;
    auto take=[&](void* out,std::size_t n){if(at+n>data.size())return false;std::memcpy(out,data.data()+at,n);at+=n;return true;};
    std::uint32_t version=0,banks=0;char magic[4]{};
    if(!read || !take(magic,4) || std::memcmp(magic,"FTSZ",4)!=0 || !take(&version,4) || version!=1 || !take(&banks,4) || banks>8) {
        log("refused: TextureSizes.dat is not a version 1 size table\r\n");return false;
    }
    for(std::uint32_t b=0;b<banks;++b) {
        char handle[33]{};Table table;std::uint32_t count=0;
        if(!take(handle,32) || !take(&table.slots,4) || !take(&count,4) || count>table.slots) {log("refused: truncated bank table\r\n");return false;}
        table.handle=handle;table.rows.resize(count);
        for(auto& row:table.rows)
            if(!take(&row.index,4) || !take(row.header.data(),28) || !take(row.original.data(),4) ||
               row.index>=table.slots || !row.original[0] || !row.original[1]) {log("refused: invalid size record\r\n");return false;}
        for(std::size_t i=1;i<table.rows.size();++i)
            if(table.rows[i-1].index>=table.rows[i].index) {log("refused: size records out of order\r\n");return false;}
        if(table.handle=="GBANK_MAIN") mainTable=std::move(table);
        else if(table.handle=="GBANK_FRONT_END") frontendTable=std::move(table);
    }
    if(at!=data.size()) {log("refused: trailing bytes in TextureSizes.dat\r\n");return false;}
    char line[160];
    std::snprintf(line,sizeof line,"loaded %u main-bank and %u frontend size records\r\n",
                  unsigned(mainTable.rows.size()),unsigned(frontendTable.rows.size()));
    log(line);
    return !mainTable.rows.empty() || !frontendTable.rows.empty();
}

unsigned __cdecl resolve(unsigned getter,unsigned bank,unsigned caller,unsigned index) noexcept {
    const auto error=GetLastError();
    unsigned result=texture_hud::unchanged;
    std::array<unsigned,2> range{};
    std::array<unsigned char,44> graphic{};
    if(texture_hud::layoutCaller(getter,caller) && readMemory(bank+0x1e0,range.data(),8) && range[1]>=range[0]) {
        const Table* table=nullptr;
        for(const Table* candidate:{&mainTable,&frontendTable})
            if(candidate->slots && range[1]-range[0]==candidate->slots*44) table=candidate;
        if(table && index<table->slots && readMemory(range[0]+index*44,graphic.data(),graphic.size())) {
            const unsigned axis=getter%2;
            std::uint16_t native=0;std::memcpy(&native,graphic.data()+6+axis*2,2);
            bool valid=true;
            if(getter>=2) {
                // Max getters traverse per-frame headers: single-frame entries only.
                std::uint16_t frames=0;std::memcpy(&frames,graphic.data()+10,2);
                unsigned vector=0,frame=0;std::uint16_t maximum=0;
                std::memcpy(&vector,graphic.data()+40,4);
                valid=frames==1 && vector && readMemory(vector+12,&frame,4) && frame &&
                    readMemory(frame+6+axis*2,&maximum,2) && maximum==native;
            }
            if(valid) {
                texture_display::Resolver resolver(&readMemory,std::span<const texture_display::Record>{});
                const auto match=resolver.indexedExtent(bank,index,axis,native,table->handle,table->slots,table->rows);
                if(match.reason==texture_display::Reason::matched) result=match.value;
            }
        }
    }
    SetLastError(error);return result;
}

void __cdecl updateGeometry(unsigned kind,unsigned internal,unsigned primitive) noexcept {
    const auto error=GetLastError();
    unsigned engine=0,bank=0;
    // Sprites draw from the engine's main graphics bank (engine+954h).
    if(mainTable.slots && readMemory(0x1436e98,&engine,4) && engine && readMemory(engine+0x954,&bank,4) && bank) {
        auto box=texture_hud::correctedBox(&readMemory,bank,internal,primitive,kind,mainTable.slots,
            std::span<const texture_display::IndexedRecord>(mainTable.rows));
        if(box) {
            SIZE_T written=0;
            WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(internal+0x48),box->data(),sizeof(*box),&written);
        }
    }
    SetLastError(error);
}

bool retailExecutable() {
    const auto base=reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
    if(reinterpret_cast<std::uintptr_t>(base)!=0x400000) return false;
    const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE) return false;
    const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    // Fable.exe 25.7.13.149 (Steam TLC), the build every hook address comes from.
    return nt->Signature==IMAGE_NT_SIGNATURE && nt->FileHeader.TimeDateStamp==0x42d7dbdf;
}

bool install() {
    logFile=CreateFileW(besideModule(L"TextureSizeFix.log").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(!retailExecutable()) {log("refused: not the Steam Fable.exe these hooks were audited on\r\n");return false;}
    if(!loadTables()) return false;
    auto stubs=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!stubs) return false;
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
    if(!memory.prepare(patches)) {VirtualFree(stubs,0,MEM_RELEASE);log("refused: hook sites unavailable\r\n");return false;}
    const auto status=texture_font::applyPatches(memory,patches);
    if(status==texture_font::PatchResult::rollbackFailed) {log("fatal: rollback failed\r\n");ExitProcess(0xe000f258);}
    if(status!=texture_font::PatchResult::applied) {
        VirtualFree(stubs,0,MEM_RELEASE);
        log("refused: hook sites differ from retail (another patch?); sizes unchanged\r\n");return false;
    }
    HMODULE pinned=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&resolve),&pinned);
    log("applied: 4 extent getter hooks (41 layout callers) and 2 sprite geometry hooks\r\n");
    return true;
}
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        self=module;
        DisableThreadLibraryCalls(module);
        try {install();} catch(...) {log("startup exception; sizes unchanged\r\n");}
    }
    return TRUE;
}
