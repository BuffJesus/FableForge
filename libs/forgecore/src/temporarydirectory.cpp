#include "forge/temporarydirectory.hpp"
#include <string_view>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace forge::detail {
#ifdef _WIN32
    // libstdc++ remove_all can loop indefinitely below MAX_PATH. Enumerate
    // native extended paths instead, and never follow junctions or symlinks.
static void removeOwnedTree(const std::wstring& path) {
        const DWORD attributes=GetFileAttributesW(path.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES)return;
        if(!(attributes&FILE_ATTRIBUTE_DIRECTORY)){DeleteFileW(path.c_str());return;}
        if(!(attributes&FILE_ATTRIBUTE_REPARSE_POINT)) {
            WIN32_FIND_DATAW data{};
            HANDLE search=FindFirstFileW((path+L"\\*").c_str(),&data);
            if(search!=INVALID_HANDLE_VALUE) {
                struct Close {HANDLE handle;~Close(){FindClose(handle);}} close{search};
                do {
                    if(std::wstring_view(data.cFileName)==L"." || std::wstring_view(data.cFileName)==L"..")continue;
                    removeOwnedTree(path+L"\\"+data.cFileName);
                }while(FindNextFileW(search,&data));
            }
        }
        RemoveDirectoryW(path.c_str());
    }
#endif
void removeOwnedTemporaryDirectory(const std::filesystem::path& owned) noexcept {
    try {
#ifdef _WIN32
        auto path=owned;path.make_preferred();auto native=path.native();
        if(!native.starts_with(L"\\\\?\\"))native=native.starts_with(L"\\\\")?L"\\\\?\\UNC\\"+native.substr(2):L"\\\\?\\"+native;
        removeOwnedTree(native);
#else
        std::error_code error;std::filesystem::remove_all(owned,error);
#endif
    }catch(...) {} // Cleanup must not mask an original exception.
}
}
