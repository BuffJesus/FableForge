#pragma once
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "backups.hpp"
#include "resolvedpath.hpp"
namespace albion::textwrite {
inline std::string loweredPathComponent(std::string value) {
    std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return char(std::tolower(c));});
    return value;
}
inline void requireTextDestinationOffline(const std::filesystem::path& destination) {
    namespace fs=std::filesystem;
    const std::vector<fs::path> candidates={fs::absolute(destination).lexically_normal(),paths::resolvedPath(destination)};
    std::set<fs::path> roots;
    for(const auto& path:candidates) {
        const auto language=path.parent_path(),lang=language.parent_path(),data=lang.parent_path();
        if(loweredPathComponent(path.filename().string())=="text.big" && loweredPathComponent(lang.filename().string())=="lang" &&
           loweredPathComponent(data.filename().string())=="data") roots.insert(data.parent_path());
    }
    for(const auto& root:roots) if(albion::backups::gameRunningIn(root))
        throw std::runtime_error("text write: Fable.exe is running from the destination install; quit it before writing");
}

} // namespace albion::textwrite
