#include "verified_archive.hpp"
#include <fstream>
#include <stdexcept>
#include <cstdio>
static void require(bool value) {if(!value) throw std::runtime_error("archive identity mismatch");}
int main() {
    wchar_t folder[MAX_PATH],first[MAX_PATH],second[MAX_PATH];
    require(GetTempPathW(MAX_PATH,folder)!=0 && GetTempFileNameW(folder,L"fva",0,first)!=0 &&
        GetTempFileNameW(folder,L"fva",0,second)!=0);
    struct Cleanup {std::filesystem::path a,b;~Cleanup(){std::error_code ec;std::filesystem::remove(a,ec);std::filesystem::remove(b,ec);}} files{first,second};
    for(const auto& p:{files.a,files.b}) {std::ofstream f(p,std::ios::binary);f<<"abc";}
    constexpr auto hash="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    struct Handle {HANDLE h;~Handle(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}};
    Handle native{CreateFileW(first,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr)};
    require(native.h!=INVALID_HANDLE_VALUE);
    require(SetFilePointer(native.h,1,nullptr,FILE_BEGIN)==1);
    texture_font::VerifiedArchive archive;
    require(!archive.verify(native.h,second,3,hash)); // same bytes, different file
    require(!archive.verify(native.h,first,4,hash));
    require(!archive.verify(native.h,first,3,std::string(64,'0')));
    require(archive.verify(native.h,first,3,hash) && archive.matches(native.h));
    require(SetFilePointer(native.h,0,nullptr,FILE_CURRENT)==1);
    Handle writer{CreateFileW(first,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr)};
    require(writer.h==INVALID_HANDLE_VALUE);
    require(!DeleteFileW(first));
    require(!archive.matches(INVALID_HANDLE_VALUE));
    archive.reset();require(!archive.matches(native.h));
    Handle writable{CreateFileW(first,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr)};
    require(writable.h!=INVALID_HANDLE_VALUE);
    // Existing writable access prevents acquisition of the proof's share lock.
    require(!archive.verify(native.h,first,3,hash));
    std::puts("PASS archive handle/content identity, wrong-file/hash/size refusals, write/delete exclusion and native file-position preservation");
}
