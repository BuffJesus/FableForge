// Real Win32 file positions, with no game, installed files, or large allocation.
#include "archive_seek.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <stdexcept>

namespace {
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
unsigned delegated=0;
DWORD WINAPI original(HANDLE file,LONG distance,PLONG high,DWORD method) {
    ++delegated;
    return SetFilePointer(file,distance,high,method);
}
struct File {
    HANDLE handle=INVALID_HANDLE_VALUE;
    File() {
        wchar_t temp[MAX_PATH]{},path[MAX_PATH]{};
        const auto n=GetTempPathW(MAX_PATH,temp);
        require(n && n<MAX_PATH,"temporary directory");
        require(GetTempFileNameW(temp,L"a4g",0,path)!=0,"temporary file");
        handle=CreateFileW(path,GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,
                           FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,nullptr);
        if(handle==INVALID_HANDLE_VALUE) {DeleteFileW(path);throw std::runtime_error("open temporary file");}
    }
    ~File() {if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
};
LONGLONG position(HANDLE file) {
    LARGE_INTEGER zero{},result{};
    require(SetFilePointerEx(file,zero,&result,FILE_CURRENT)!=0,"query position");
    return result.QuadPart;
}
}

int main() try {
    File file;
    constexpr std::array<DWORD,8> offsets{0,1,0x7ffffffe,0x7fffffff,0x80000000,
                                          0x80000001,0xfffffffe,0xffffffff};
    for(const auto offset:offsets) {
        SetLastError(ERROR_ACCESS_DENIED);
        const auto result=texture_archive::seek(original,file.handle,static_cast<LONG>(offset),nullptr,FILE_BEGIN);
        const auto error=GetLastError();
        require(result==offset && error==NO_ERROR,"unsigned absolute seek including sentinel");
        require(position(file.handle)==offset,"full 64-bit absolute position");
        require(delegated==0,"absolute unsigned seek must bypass original");
    }
    // Relative distances stay signed; a null high word keeps Windows' 4 GiB bound.
    require(texture_archive::seek(original,file.handle,-1,nullptr,FILE_CURRENT)==0xfffffffe,"relative backward");
    require(position(file.handle)==0xfffffffe,"relative backward position");
    SetLastError(NO_ERROR);
    require(texture_archive::seek(original,file.handle,2,nullptr,FILE_CURRENT)==INVALID_SET_FILE_POINTER &&
            GetLastError()==ERROR_INVALID_PARAMETER,"relative crossing 4 GiB retains Windows refusal");
    require(position(file.handle)==0xfffffffe,"failed out-of-range relative seek leaves position unchanged");
    LONG high=1;
    require(texture_archive::seek(original,file.handle,7,&high,FILE_BEGIN)==7 && high==1,"explicit high word");
    require(position(file.handle)==0x100000007ll,"explicit high position");
    require(texture_archive::seek(original,file.handle,0,nullptr,FILE_END)==0,"seek to empty end");
    SetLastError(NO_ERROR);
    require(texture_archive::seek(original,file.handle,-1,nullptr,FILE_CURRENT)==INVALID_SET_FILE_POINTER &&
            GetLastError()==ERROR_NEGATIVE_SEEK,"negative relative seek refused");
    require(position(file.handle)==0,"failed relative seek leaves position unchanged");
    high=-1;
    SetLastError(NO_ERROR);
    require(texture_archive::seek(original,file.handle,-1,&high,FILE_BEGIN)==INVALID_SET_FILE_POINTER &&
            GetLastError()==ERROR_NEGATIVE_SEEK,"explicit negative absolute seek refused");
    require(delegated==6,"all explicit-high and relative requests delegated");
    SetLastError(NO_ERROR);
    require(texture_archive::seek(original,INVALID_HANDLE_VALUE,0,nullptr,FILE_BEGIN)==INVALID_SET_FILE_POINTER &&
            GetLastError()==ERROR_INVALID_HANDLE,"unsigned seek preserves Windows failure");
    LARGE_INTEGER size{};
    require(GetFileSizeEx(file.handle,&size) && size.QuadPart==0,"seeks never allocate or extend the file");
    std::puts("PASS Archive4GB: 8 unsigned boundaries, 6 delegated seeks, invalid handle; zero-byte file");
} catch(const std::exception& error) {
    std::fprintf(stderr,"FAIL: %s (Windows error %lu)\n",error.what(),GetLastError());
    return 1;
}
