// Execute pinned retail disk routines in this test process against one temporary
// sparse file. Does not load Fable, touch an installation, or create game windows.
#include "archive_seek.hpp"
#include "archive_move_stub_generated.hpp"
#include "local_code_memory.hpp"
#include "pinned_alpha.hpp"
#include <winioctl.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace {
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
struct Allocation {
    void* pointer=nullptr;
    Allocation(void* at,SIZE_T size) {
        pointer=VirtualAlloc(at,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        require(pointer && (!at || at==pointer),"reserve native fixture address");
    }
    ~Allocation() {if(pointer)VirtualFree(pointer,0,MEM_RELEASE);}
};
struct File {
    HANDLE handle=INVALID_HANDLE_VALUE;
    File() {
        wchar_t folder[MAX_PATH]{},path[MAX_PATH]{};
        auto n=GetTempPathW(MAX_PATH,folder);
        require(n && n<MAX_PATH && GetTempFileNameW(folder,L"a4i",0,path),"temporary sparse file");
        handle=CreateFileW(path,GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,
            FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,nullptr);
        if(handle==INVALID_HANDLE_VALUE) {DeleteFileW(path);throw std::runtime_error("open temporary file");}
    }
    ~File() {if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
};
LONGLONG position(HANDLE file) {
    LARGE_INTEGER zero{},result{};
    require(SetFilePointerEx(file,zero,&result,FILE_CURRENT)!=0,"query file position");
    return result.QuadPart;
}
void putSlot(unsigned address,void* value) {std::memcpy(reinterpret_cast<void*>(address),&value,4);}
unsigned seekFailures=0;
DWORD WINAPI retailSeek(HANDLE file,LONG distance,PLONG high,DWORD method) {
    SetLastError(NO_ERROR);
    const auto result=SetFilePointer(file,distance,high,method);
    if(result==INVALID_SET_FILE_POINTER && GetLastError()!=NO_ERROR)++seekFailures;
    return result;
}
DWORD WINAPI patchedSeek(HANDLE file,LONG distance,PLONG high,DWORD method) {
    return texture_archive::seek(retailSeek,file,distance,high,method);
}
struct Disk {std::uint32_t vptr=0,access=0,position=0,length=0xffffffff;HANDLE handle;};
using SetPosition=void(__thiscall*)(Disk*,long);
using MovePosition=void(__thiscall*)(Disk*,long);
using Read=void(__thiscall*)(Disk*,void*,long,void*);
}

int wmain(int argc,wchar_t** argv) try {
    require(argc==2,"usage: test_archive_native_io <pinned disk-routine fixture>");
    std::vector<std::uint8_t> bytes;
    require(texture_font::loadPinnedAlpha(argv[1],0x414,
        "a6ae8b7208cc0f791f7190eafa0f77f08f47c98666297949df8c9a2421cd2217",bytes)==
        texture_font::LoadError::none,"retail code fixture size/hash");
    // Relocate only the five audited import operands; intra-function branches
    // keep their original displacements. No preferred address must be vacant.
    Allocation native(nullptr,0x10000);
    Allocation imports(nullptr,4096);
    Allocation stub(nullptr,4096);
    const auto codeBase=reinterpret_cast<std::uint32_t>(native.pointer);
    const auto importsBase=reinterpret_cast<std::uint32_t>(imports.pointer);
    auto at=[&](std::uint32_t address) {return codeBase+address-0x99a960u;};
    std::memset(native.pointer,0xcc,0x10000);
    std::memcpy(native.pointer,bytes.data(),bytes.size());
    struct Import {unsigned instruction,original,slot;};
    for(const auto& row:std::array<Import,5>{{{0x99aa1d,0x143fed0,8},
        {0x99ac0a,0x143fde8,0},{0x99ac75,0x143fda8,4},
        {0x99ac94,0x143fde8,0},{0x99ad59,0x143fda8,4}}}) {
        std::uint32_t operand=0;
        std::memcpy(&operand,reinterpret_cast<void*>(at(row.instruction)+2),4);
        require(operand==row.original,"audited original import operand");
        operand=importsBase+row.slot;
        std::memcpy(reinterpret_cast<void*>(at(row.instruction)+2),&operand,4);
    }
    putSlot(importsBase,reinterpret_cast<void*>(&patchedSeek));
    putSlot(importsBase+4,reinterpret_cast<void*>(&FlushFileBuffers));
    putSlot(importsBase+8,reinterpret_cast<void*>(&ReadFile));
    const auto origin=reinterpret_cast<std::uint32_t>(stub.pointer);
    const auto generated=texture_archive::moveStub(origin,at(0x99acfe),at(0x99acb7));
    std::memcpy(stub.pointer,generated.data(),generated.size());
    DWORD old=0;
    require(VirtualProtect(stub.pointer,4096,PAGE_EXECUTE_READ,&old)!=0,"protect move stub");
    require(FlushInstructionCache(GetCurrentProcess(),stub.pointer,generated.size())!=0,"flush move stub");
    const std::uint8_t jle[]{0x7e,0x47},jbe[]{0x76,0x47};
    const std::uint8_t move[]{0x8d,0x04,0x11,0x85,0xc0,0x7d,0x47};
    std::uint8_t jump[]{0xe9,0,0,0,0,0x90,0x90};
    const auto relative=origin-at(0x99acb5);std::memcpy(jump+1,&relative,4);
    const texture_font::CodePatch patches[]{
        {at(0x99ac1c),jle,jbe},{at(0x99acb0),move,jump},{at(0x99ad01),jle,jbe}};
    texture_font::LocalCodeMemory memory;
    require(memory.prepare(patches) && texture_font::applyPatches(memory,patches)==
        texture_font::PatchResult::applied,"apply audited position patches to local fixture");
    require(VirtualProtect(native.pointer,0x10000,PAGE_EXECUTE_READ,&old)!=0,"protect retail code");

    File file;DWORD got=0;
    require(DeviceIoControl(file.handle,FSCTL_SET_SPARSE,nullptr,0,nullptr,0,&got,nullptr)!=0,"sparse-file support required");
    LARGE_INTEGER end{};end.QuadPart=0xffffffffll;
    require(SetFilePointerEx(file.handle,end,nullptr,FILE_BEGIN) && SetEndOfFile(file.handle),"set sparse logical size");
    std::array<std::uint8_t,32> marker{};
    for(unsigned i=0;i<marker.size();++i)marker[i]=std::uint8_t(0x41+i);
    constexpr std::array<std::uint32_t,4> offsets{0x1000,0x7ffffff0,0x80001000,0xfffffff0};
    for(auto offset:offsets) {
        LARGE_INTEGER at{};at.QuadPart=offset;
        const DWORD size=offset==offsets.back()?15:DWORD(marker.size());
        require(SetFilePointerEx(file.handle,at,nullptr,FILE_BEGIN)!=0,"position marker");
        require(WriteFile(file.handle,marker.data(),size,&got,nullptr) && got==size,"write marker");
    }
    FILE_STANDARD_INFO info{};
    require(FlushFileBuffers(file.handle)!=0,"commit sparse markers before allocation query");
    require(GetFileInformationByHandleEx(file.handle,FileStandardInfo,&info,sizeof info)!=0,"file allocation info");
    require(info.EndOfFile.QuadPart==0xffffffffll && info.AllocationSize.QuadPart<=1024*1024,"sparse allocation stays bounded");
    const auto set=reinterpret_cast<SetPosition>(at(0x99abf0));
    const auto movePosition=reinterpret_cast<MovePosition>(at(0x99ac90));
    const auto read=reinterpret_cast<Read>(at(0x99a960));
    Disk disk{};disk.handle=file.handle;
    std::array<std::uint8_t,32> output{};
    auto reset=[&] {
        LARGE_INTEGER zero{};require(SetFilePointerEx(file.handle,zero,nullptr,FILE_BEGIN)!=0,"reset position");
        disk.position=0;output.fill(0);
    };
    // Negative control: original import fails even with unsigned branch patches.
    reset();putSlot(importsBase,reinterpret_cast<void*>(&retailSeek));
    set(&disk,long(0x80001000u));read(&disk,output.data(),32,nullptr);
    require(seekFailures==1 && output!=marker && position(file.handle)==32,"original import must fail high-offset read");
    putSlot(importsBase,reinterpret_cast<void*>(&patchedSeek));
    for(auto offset:offsets) {
        reset();set(&disk,long(offset));
        require(disk.position==offset && position(file.handle)==offset,"native cached/physical seek position");
        read(&disk,output.data(),32,nullptr);
        const unsigned count=offset==offsets.back()?15:32;
        require(std::memcmp(output.data(),marker.data(),count)==0,"native read marker");
        require(disk.position==offset+count && position(file.handle)==std::uint64_t(offset)+count,"native read cached/physical advancement");
        if(count<32)require(output[count]==0,"short read preserves unwritten destination");
    }
    // Real signed relative addition followed by real Win32 ReadFile.
    reset();set(&disk,long(0x80001008u));movePosition(&disk,-8);read(&disk,output.data(),32,nullptr);
    require(output==marker && disk.position==0x80001020u,"negative relative high-offset read");
    // The retail zero-size read at EOF must remain a no-op.
    set(&disk,-1);read(&disk,output.data(),0,nullptr);
    require(disk.position==0xffffffff && position(file.handle)==0xffffffffll,"zero-size EOF read");
    require(seekFailures==1,"patched seek must introduce no additional Windows seek failures");
    std::printf("PASS native retail sparse I/O: 4 absolute markers, relative read, EOF no-op; negative control fails; logical=%llu allocated=%llu\n",
        static_cast<unsigned long long>(info.EndOfFile.QuadPart),static_cast<unsigned long long>(info.AllocationSize.QuadPart));
} catch(const std::exception& error) {
    std::fprintf(stderr,"FAIL: %s (Windows error %lu)\n",error.what(),GetLastError());return 1;
}
