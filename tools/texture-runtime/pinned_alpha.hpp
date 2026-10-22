#pragma once
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>
namespace texture_font {
enum class LoadError { none, parameters, open, size, read, crypto, hash };
// Startup-only loader: output changes only after the complete loaded bytes match
// their compiled identity. Never call from a render callback or DllMain.
LoadError loadPinnedAlpha(const std::filesystem::path& path, std::size_t expectedSize,
                          std::string_view sha256, std::vector<std::uint8_t>& output);
// Reopens the same OS file object with an independent file position. Borrowed
// handle remains owned by the caller; no pathname lookup is used.
LoadError loadPinnedAlphaHandle(void* handle,std::size_t expectedSize,
                                std::string_view sha256,std::vector<std::uint8_t>& output);
}
