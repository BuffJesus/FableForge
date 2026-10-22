// Scratch-only diagnostic. Logs which code asks CGraphicDataBank for graphic
// extents (GetGraphicFrameWidth/Height, GetGraphicMaxWidth/Height) and from which
// bank, so display-size fixes can target real callers. Never changes a value.
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
HANDLE logFile=INVALID_HANDLE_VALUE;
CRITICAL_SECTION lock;
void log(const char* text) {
    if(logFile!=INVALID_HANDLE_VALUE) {DWORD written;WriteFile(logFile,text,DWORD(std::strlen(text)),&written,nullptr);}
}
bool readMemory(std::uint32_t address,void* out,std::size_t size) {
    __try {std::memcpy(out,reinterpret_cast<const void*>(address),size);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
// Retail CGraphicDataBank extent getters; each opens with mov eax,[ecx+1E0h].
constexpr std::array<std::uint32_t,4> getters{0xa01430,0xa01480,0xa014d0,0xa01540};
constexpr const char* getterNames[]{"FrameWidth","FrameHeight","MaxWidth","MaxHeight"};
constexpr unsigned char entryBytes[]{0x8b,0x81,0xe0,0x01,0x00,0x00};

struct Seen {std::uint32_t getter,caller,bank;};
std::array<Seen,1024> seen{};
unsigned seenCount=0;

void __cdecl census(unsigned getter,std::uint32_t bank,std::uint32_t caller,std::uint32_t index) {
    const auto error=GetLastError();
    EnterCriticalSection(&lock);
    bool known=false;
    bool knownBank=false;
    for(unsigned i=0;i<seenCount;++i) if(seen[i].bank==bank) knownBank=true;
    for(unsigned i=0;i<seenCount;++i)
        if(seen[i].getter==getter && seen[i].caller==caller && seen[i].bank==bank) {known=true;break;}
    if(!known && seenCount<seen.size()) {
        seen[seenCount++]={getter,caller,bank};
        std::array<char,257> handle{};
        std::uint32_t shared=0;std::array<std::uint32_t,2> text{};
        if(!(readMemory(bank+0x88,&shared,4) && shared && readMemory(shared,text.data(),8) &&
             text[1]<256 && readMemory(text[0],handle.data(),text[1]+1) && handle[text[1]]==0))
            std::strcpy(handle.data(),"?");
        char line[384];
        std::snprintf(line,sizeof line,"%s caller=%08x bank=%08x handle=%s index=%u\r\n",
                      getterNames[getter],caller,bank,handle.data(),index);
        log(line);
        if(!knownBank) {
            std::array<unsigned,2> range{};
            if(readMemory(bank+0x1e0,range.data(),8) && range[1]>=range[0] &&
               (range[1]-range[0])%44==0 && (range[1]-range[0])/44<10000) {
                std::snprintf(line,sizeof line,"table bank=%08x handle=%s count=%u\r\n",bank,handle.data(),(range[1]-range[0])/44);log(line);
                for(unsigned i=0;i<(range[1]-range[0])/44;++i) {
                    std::array<unsigned char,44> raw{};
                    if(!readMemory(range[0]+i*44,raw.data(),raw.size())) break;
                    auto n=std::snprintf(line,sizeof line,"record bank=%08x index=%u bytes=",bank,i);
                    for(auto b:raw) n+=std::snprintf(line+n,sizeof(line)-n,"%02x",b);
                    std::snprintf(line+n,sizeof(line)-n,"\r\n");log(line);
                }
            }
        }
    }
    LeaveCriticalSection(&lock);
    SetLastError(error);
}

// pushfd; pushad; push [esp+40] (index); push [esp+40] (return address);
// push ecx; push getter; call census; add esp,16; popad; popfd;
// replay mov eax,[ecx+1E0h]; jmp entry+6.
std::size_t buildStub(unsigned char* code,unsigned getter) {
    std::size_t n=0;
    auto put=[&](std::initializer_list<unsigned char> bytes){for(auto b:bytes) code[n++]=b;};
    auto rel=[&](std::uintptr_t target){
        auto delta=std::uint32_t(target-(reinterpret_cast<std::uintptr_t>(code)+n+4));
        std::memcpy(code+n,&delta,4);n+=4;
    };
    put({0x9c,0x60,0x89,0xe5,0x81,0xec,0x10,0x02,0x00,0x00,0x83,0xe4,0xf0});
    put({0x0f,0xae,0x04,0x24});       // preserve x87/SSE, including live UI intermediates
    put({0xff,0x75,0x28});            // first argument (index)
    put({0xff,0x75,0x24});            // return address
    put({0x51});                      // push ecx (bank)
    put({0x6a,static_cast<unsigned char>(getter)});
    put({0xe8});rel(reinterpret_cast<std::uintptr_t>(&census));
    put({0x83,0xc4,0x10,0x0f,0xae,0x0c,0x24,0x89,0xec,0x61,0x9d});
    for(auto b:entryBytes) code[n++]=b;
    put({0xe9});rel(getters[getter]+sizeof entryBytes);
    return n;
}

bool install() {
    char enabled[8]{};
    if(GetEnvironmentVariableA("FABLE_FORGE_EXTENT_CENSUS",enabled,sizeof enabled)!=1 || enabled[0]!='1') return false;
    char executable[MAX_PATH]{};DWORD size=GetModuleFileNameA(nullptr,executable,MAX_PATH);
    if(!size || size>=MAX_PATH) return false;
    std::string path(executable);for(auto& c:path) if(c=='\\') c='/';
    const std::string root=CENSUS_SCRATCH_ROOT;
    if(_stricmp(path.c_str(),(root+"/Fable.exe").c_str())!=0) return false;
    logFile=CreateFileA((root+"/graphic-extent-census.log").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))!=0x400000) {log("refused: image base\r\n");return false;}
    for(auto getter:getters)
        if(std::memcmp(reinterpret_cast<void*>(getter),entryBytes,sizeof entryBytes)!=0) {log("refused: getter bytes differ\r\n");return false;}
    InitializeCriticalSection(&lock);
    auto stubs=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!stubs) return false;
    std::memset(stubs,0xcc,4096);
    for(unsigned i=0;i<getters.size();++i) buildStub(stubs+i*64,i);
    DWORD old=0;
    if(!VirtualProtect(stubs,4096,PAGE_EXECUTE_READ,&old)) return false;
    FlushInstructionCache(GetCurrentProcess(),stubs,4096);
    // All four getters share page 0xA01000. Startup-only, before the render loop.
    auto page=reinterpret_cast<void*>(0xa01000);
    if(!VirtualProtect(page,4096,PAGE_EXECUTE_READWRITE,&old)) return false;
    for(unsigned i=0;i<getters.size();++i) {
        std::array<unsigned char,6> branch{0xe9,0,0,0,0,0x90};
        auto delta=std::uint32_t(reinterpret_cast<std::uintptr_t>(stubs+i*64)-getters[i]-5);
        std::memcpy(branch.data()+1,&delta,4);std::memcpy(reinterpret_cast<void*>(getters[i]),branch.data(),6);
    }
    DWORD ignored;VirtualProtect(page,4096,old,&ignored);FlushInstructionCache(GetCurrentProcess(),page,4096);
    log("installed four extent getter census hooks\r\n");
    HMODULE pinned=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                       reinterpret_cast<LPCWSTR>(&census),&pinned);
    return true;
}
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        if(!install()) log("not installed\r\n");
    }
    return TRUE;
}
