#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace albion::process {

#ifdef _WIN32
inline std::string utf8(std::wstring_view value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!size) throw std::runtime_error("cannot convert Windows argument to UTF-8");
    std::string result(size, '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), size, nullptr, nullptr))
        throw std::runtime_error("cannot convert Windows argument to UTF-8");
    return result;
}

inline std::wstring wide(std::string_view value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (!size) throw std::runtime_error("cannot convert UTF-8 process argument");
    std::wstring result(size, L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), size))
        throw std::runtime_error("cannot convert UTF-8 process argument");
    return result;
}

inline std::filesystem::path executableDirectory() {
    std::wstring buffer(32768, L'\0');
    const DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!size || size >= buffer.size()) throw std::runtime_error("cannot locate running executable");
    buffer.resize(size);
    return std::filesystem::path(buffer).parent_path();
}
#endif

// The narrow Windows CRT argv uses the active ANSI code page. All path-bearing
// commands in Forge consume UTF-8, as does the GUI's wide entry point.
inline std::vector<std::string> arguments(int argc, char** argv) {
#ifdef _WIN32
    int count = 0;
    wchar_t** values = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!values) throw std::runtime_error("cannot read Windows command line");
    struct Arguments { wchar_t** values; ~Arguments() { LocalFree(values); } } owner{values};
    std::vector<std::string> result;
    for (int i = 1; i < count; ++i) result.push_back(utf8(values[i]));
    return result;
#else
    return {argv + 1, argv + argc};
#endif
}

} // namespace albion::process
