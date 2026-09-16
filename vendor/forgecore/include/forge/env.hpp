#pragma once
// forge::env — the shared Install Environment. One auto-detected source of truth
// for the Fable TLC install path, the save folder, the data files, and the FSE
// install, so every tool/panel points at the same place instead of hardcoding.
//
// Detection order: (1) a persisted user override, (2) Steam — registry SteamPath
// then libraryfolders.vdf for app 174430, (3) known candidate paths. The result
// is validated (Fable.exe / data present) and can be overridden + re-detected.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace forge::env {

struct SaveFile {
    std::string name;               // "Manual - Save2", "Auto Save", …
    std::filesystem::path path;
    uint64_t size = 0;
    int64_t mtime = 0;              // unix seconds
    bool looksValid = false;        // 307200 bytes + "FableSave!" magic
};

struct Profile {
    std::string name;               // save-profile folder name (e.g. "Cornelio")
    std::filesystem::path dir;
    std::vector<SaveFile> saves;
};

struct Environment {
    // --- install ---
    std::filesystem::path installDir;   // Fable TLC root
    std::filesystem::path dataDir;      // installDir/data
    bool installValid = false;
    std::string detectSource;           // "override" | "registry" | "libraryfolders" | "candidate" | "none"

    // --- resolved key files (empty if absent) ---
    std::filesystem::path fableExe;
    std::filesystem::path gameBin, namesBin;
    std::filesystem::path wld, bwd, wad, stb;
    std::filesystem::path ingameLug, textBig, graphicsBig, texturesBig;

    // --- FSE ---
    std::filesystem::path fseDir, fseLauncher, fseMasterLua, fseLog;
    bool fseInstalled = false;

    // --- saves ---
    std::filesystem::path savesRoot;    // Documents/My Games/Fable/Saves
    std::vector<Profile> profiles;

    // Detect the environment. Applies a persisted override if present (unless
    // `overrideInstall` is given, which takes precedence and is persisted).
    static Environment detect(const std::filesystem::path& overrideInstall = {});

    // Recompute key-file + FSE paths from `installDir`.
    void resolveKeyFiles();
    // (Re)enumerate `savesRoot` profiles + save files.
    void enumerateSaves();

    // --- persisted config (%APPDATA%/FableForge/config.ini) ---
    static std::filesystem::path configPath();
    // Returns the persisted install override if any (empty otherwise).
    static std::filesystem::path loadOverride();
    // Persist `installDir` as the override.
    void saveOverride() const;

    // Convenience: total valid saves across profiles.
    size_t totalSaves() const;
};

// Locate the Steam install (SteamPath) and, via libraryfolders.vdf, the Fable TLC
// dir (app 174430). Empty path if not found. Sets `source` to how it was found.
std::filesystem::path detectSteamInstall(std::string& source);

// Documents/My Games/Fable/Saves (may not exist). Empty if the home dir is unknown.
std::filesystem::path defaultSavesRoot();

} // namespace forge::env
