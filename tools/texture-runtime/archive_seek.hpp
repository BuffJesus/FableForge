#pragma once
#define NOMINMAX
#include <windows.h>

namespace texture_archive {
using SetFilePointerFn=DWORD(WINAPI*)(HANDLE,LONG,PLONG,DWORD);

// Only absolute, low-word-only positions use the engine's unsigned BIG offsets.
// Relative distances and explicit high words retain the Windows API contract.
inline DWORD seek(SetFilePointerFn previous,HANDLE file,LONG distance,PLONG high,DWORD method) {
    if(high || method!=FILE_BEGIN)return previous(file,distance,high,method);
    LARGE_INTEGER target{},result{};
    target.QuadPart=static_cast<LONGLONG>(static_cast<DWORD>(distance));
    if(!SetFilePointerEx(file,target,&result,FILE_BEGIN))return INVALID_SET_FILE_POINTER;
    // 0xffffffff is a valid unsigned position: callers disambiguate via LastError.
    SetLastError(NO_ERROR);
    return result.LowPart;
}
}
