#pragma once
#include "pinned_alpha.hpp"
#define NOMINMAX
#include <windows.h>

namespace texture_font {
// Startup-only. Holds a read-only share lock for the lifetime of the proof.
// Does not seek/read through, or close, the game's borrowed native handle.
class VerifiedArchive {
    HANDLE guard_=INVALID_HANDLE_VALUE;
    static bool sameFile(HANDLE a,HANDLE b) {
        BY_HANDLE_FILE_INFORMATION x{},y{};
        return GetFileType(a)==FILE_TYPE_DISK && GetFileType(b)==FILE_TYPE_DISK &&
            GetFileInformationByHandle(a,&x) && GetFileInformationByHandle(b,&y) &&
            x.dwVolumeSerialNumber==y.dwVolumeSerialNumber &&
            x.nFileIndexHigh==y.nFileIndexHigh && x.nFileIndexLow==y.nFileIndexLow;
    }
public:
    VerifiedArchive()=default;
    VerifiedArchive(const VerifiedArchive&)=delete;
    VerifiedArchive& operator=(const VerifiedArchive&)=delete;
    ~VerifiedArchive(){reset();}
    void reset() { if(guard_!=INVALID_HANDLE_VALUE) CloseHandle(guard_);guard_=INVALID_HANDLE_VALUE; }
    bool matches(HANDLE native) const {
        return guard_!=INVALID_HANDLE_VALUE && sameFile(native,guard_);
    }
    bool verify(HANDLE native,const std::filesystem::path& path,std::size_t bytes,std::string_view sha) {
        reset();
        guard_=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        if(guard_==INVALID_HANDLE_VALUE) return false;
        std::vector<std::uint8_t> data;
        // Hash the guarded file object, not another pathname lookup (a parent
        // directory/junction could change). Native file position is untouched.
        if(!matches(native) || loadPinnedAlphaHandle(guard_,bytes,sha,data)!=LoadError::none || !matches(native)) {
            reset();return false;
        }
        return true;
    }
};
}
