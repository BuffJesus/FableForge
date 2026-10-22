#include "pinned_alpha.hpp"
#include <fstream>
#include <cstdio>
#include <stdexcept>
#include <windows.h>
static void require(bool ok) { if(!ok) throw std::runtime_error("pinned alpha loader failed"); }
int main() {
    wchar_t folder[MAX_PATH],name[MAX_PATH];
    require(GetTempPathW(MAX_PATH,folder)!=0 && GetTempFileNameW(folder,L"ffa",0,name)!=0);
    struct Cleanup { std::filesystem::path path; ~Cleanup(){std::error_code ec;std::filesystem::remove(path,ec);} } cleanup{name};
    { std::ofstream f(cleanup.path,std::ios::binary);f<<"abc"; }
    constexpr auto hash="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    using texture_font::LoadError;using texture_font::loadPinnedAlpha;
    std::vector<std::uint8_t> output{7,8,9};const auto before=output;
    require(loadPinnedAlpha(cleanup.path,4,hash,output)==LoadError::size && output==before);
    require(loadPinnedAlpha(cleanup.path,3,std::string(64,'0'),output)==LoadError::hash && output==before);
    require(loadPinnedAlpha(cleanup.path,3,std::string(64,'z'),output)==LoadError::parameters && output==before);
    require(loadPinnedAlpha(cleanup.path,0,hash,output)==LoadError::parameters && output==before);
    require(loadPinnedAlpha(cleanup.path.wstring()+L".missing",3,hash,output)==LoadError::open && output==before);
    require(loadPinnedAlpha(cleanup.path,3,hash,output)==LoadError::none && output==std::vector<std::uint8_t>({'a','b','c'}));
    { std::ofstream f(cleanup.path,std::ios::binary);f<<"abd"; }
    require(loadPinnedAlpha(cleanup.path,3,hash,output)==LoadError::hash && output==std::vector<std::uint8_t>({'a','b','c'}));
    // Failed opens must not leak a handle or prevent subsequent replacement.
    require(DeleteFileW(cleanup.path.c_str())!=0);
    std::puts("PASS pinned alpha load; size/hash/metadata/missing-file refusals preserve output and release handles");
}
