#pragma once
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace albion::paths {
inline std::filesystem::path resolvedPath(const std::filesystem::path& destination) {
    namespace fs=std::filesystem;
#ifdef _WIN32
    auto cursor=fs::absolute(destination).lexically_normal();
    std::vector<fs::path> missing;
    for(;;) {
        HANDLE handle=CreateFileW(cursor.c_str(),0,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
            nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
        if(handle!=INVALID_HANDLE_VALUE) {
            struct ClosePathHandle { HANDLE value;~ClosePathHandle(){CloseHandle(value);} } owner{handle};
            const DWORD flags=FILE_NAME_NORMALIZED|VOLUME_NAME_DOS;
            const DWORD capacity=GetFinalPathNameByHandleW(handle,nullptr,0,flags);
            if(!capacity) throw std::runtime_error("cannot resolve filesystem path");
            std::wstring buffer(capacity,L'\0');
            const DWORD length=GetFinalPathNameByHandleW(handle,buffer.data(),capacity,flags);
            if(!length || length>=capacity) throw std::runtime_error("cannot resolve filesystem path");
            buffer.resize(length);
            if(buffer.rfind(L"\\\\?\\UNC\\",0)==0) buffer=L"\\\\"+buffer.substr(8);
            else if(buffer.rfind(L"\\\\?\\",0)==0) buffer=buffer.substr(4);
            fs::path resolved(buffer);
            for(auto it=missing.rbegin();it!=missing.rend();++it) resolved/=*it;
            return resolved.lexically_normal();
        }
        const auto error=GetLastError();
        if((error!=ERROR_FILE_NOT_FOUND && error!=ERROR_PATH_NOT_FOUND) || cursor==cursor.parent_path())
            throw std::runtime_error("cannot resolve filesystem path (Windows error "+std::to_string(error)+")");
        missing.push_back(cursor.filename());cursor=cursor.parent_path();
    }
#else
    return fs::weakly_canonical(fs::absolute(destination));
#endif
}

} // namespace albion::paths
