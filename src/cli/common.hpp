// forge CLI: shared helpers and the per-family entry points. Each family returns the exit
// code when `cmd` is one of its commands, nullopt otherwise (main tries them in order).
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace albion::cli {

int usage();
std::string lower(std::string s);

struct Install {
    std::filesystem::path root;
    bool valid = false;
    std::string how;
};

Install findInstall(const std::string& override);

// Resolve the user's level argument to a .lev path on disk. WAD-resident maps are extracted
// to a temp file (the LEV reader is path-based); `tempOut` names it so the caller can delete it.
std::filesystem::path resolveLevel(const std::string& arg, const Install& install, std::filesystem::path& tempOut);

using Args = std::vector<std::string>;
std::optional<int> runLevels(const std::string& cmd, const Args& args);   // new levels and region entrances
std::optional<int> runTextures(const std::string& cmd, const Args& args);   // textures.big and ground themes
std::optional<int> runInstall(const std::string& cmd, const Args& args);   // backups and restore
std::optional<int> runWorld(const std::string& cmd, const Args& args);   // the overworld (WLD/BWD) and regions
std::optional<int> runChunks(const std::string& cmd, const Args& args);   // STB terrain chunk diagnostics and bakes
int runExport(const std::string& cmd, const Args& args);   // list / mesh / info / effects / export (the default)

}  // namespace albion::cli
