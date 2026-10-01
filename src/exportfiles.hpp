#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <span>
#include <stdexcept>
#include <vector>

#include "pendingbanks.hpp"

namespace albion::detail {

inline std::ofstream exportStream(const std::filesystem::path& path,
                                  std::ios::openmode mode = std::ios::out) {
    std::ofstream stream;
    stream.exceptions(std::ios::failbit | std::ios::badbit);
    stream.open(path, mode);
    return stream;
}

inline void writeExportBytes(const std::filesystem::path& path, std::span<const uint8_t> bytes) {
    if (bytes.empty()) throw std::runtime_error("empty export output: " + path.string());
    auto stream = exportStream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    stream.close();
}

// The writer uses the final basename inside an owned directory, so material and
// image references stay valid. Only its declared files are published together.
template<class Writer>
std::vector<std::filesystem::path> publishExport(const std::filesystem::path& out, Writer&& writer) {
    namespace fs = std::filesystem;
    const auto target = fs::absolute(out).lexically_normal();
    if (target.filename().empty()) throw std::runtime_error("export needs a filename");
    PendingBanks pending(target.parent_path(), ".forge-model-export-");
    const auto prepared = pending.prepare(target.filename());
    const auto files = writer(prepared);
    std::set<fs::path> registered;
    std::vector<fs::path> written;
    for (const auto& file : files) {
        const auto relative = file.lexically_normal().lexically_relative(prepared.parent_path());
        if (relative.empty() || relative.is_absolute() ||
            std::find(relative.begin(), relative.end(), fs::path("..")) != relative.end())
            throw std::runtime_error("export produced a path outside its workspace: " + file.string());
        if (!registered.insert(relative).second) continue;
        if (relative != target.filename()) pending.prepare(relative);
        written.push_back(out.parent_path() / relative);
    }
    if (!registered.contains(target.filename())) throw std::runtime_error("export omitted its primary file");
    std::string error;
    if (!pending.install(false, error)) throw std::runtime_error("export: " + error);
    return written;
}

} // namespace albion::detail
