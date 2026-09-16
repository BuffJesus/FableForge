#include "forge/env.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace forge::env {
namespace fs = std::filesystem;
namespace {

std::string envVar(const char* name) {
#ifdef _WIN32
    char buf[32768];
    DWORD n = GetEnvironmentVariableA(name, buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf)) return std::string(buf, n);
    return {};
#else
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
#endif
}

bool pathExists(const fs::path& p) {
    std::error_code ec;
    return !p.empty() && fs::exists(p, ec);
}

fs::path firstExisting(const fs::path& dir, std::initializer_list<const char*> rels) {
    for (const char* r : rels) {
        fs::path p = dir / r;
        if (pathExists(p)) return p;
    }
    return {};
}

#ifdef _WIN32
// Read a string value from HKCU\Software\Valve\Steam (SteamPath / InstallPath).
std::string readSteamPathReg() {
    for (const char* val : {"SteamPath", "InstallPath"}) {
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            char buf[MAX_PATH];
            DWORD sz = sizeof(buf), type = 0;
            LSTATUS s = RegQueryValueExA(hKey, val, nullptr, &type, (LPBYTE)buf, &sz);
            RegCloseKey(hKey);
            if (s == ERROR_SUCCESS && type == REG_SZ) return std::string(buf, sz ? sz - 1 : 0);
        }
    }
    // 64-bit machine key fallback.
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Valve\\Steam", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char buf[MAX_PATH];
        DWORD sz = sizeof(buf), type = 0;
        LSTATUS s = RegQueryValueExA(hKey, "InstallPath", nullptr, &type, (LPBYTE)buf, &sz);
        RegCloseKey(hKey);
        if (s == ERROR_SUCCESS && type == REG_SZ) return std::string(buf, sz ? sz - 1 : 0);
    }
    return {};
}
#endif

// Very small KeyValues scan: pull every "path" "<value>" pair from a
// libraryfolders.vdf. Enough to enumerate Steam library roots without a full
// VDF parser.
std::vector<fs::path> parseLibraryPaths(const fs::path& vdf) {
    std::vector<fs::path> out;
    std::ifstream f(vdf);
    if (!f) return out;
    std::string line;
    while (std::getline(f, line)) {
        auto key = line.find("\"path\"");
        if (key == std::string::npos) continue;
        // value is the second quoted token on the line
        size_t q1 = line.find('"', key + 6);
        if (q1 == std::string::npos) continue;
        size_t q2 = line.find('"', q1 + 1);
        if (q2 == std::string::npos) continue;
        std::string val = line.substr(q1 + 1, q2 - q1 - 1);
        // VDF escapes backslashes; unescape "\\" -> "\"
        std::string clean;
        for (size_t i = 0; i < val.size(); ++i) {
            if (val[i] == '\\' && i + 1 < val.size() && val[i + 1] == '\\') { clean += '\\'; ++i; }
            else clean += val[i];
        }
        if (!clean.empty()) out.emplace_back(clean);
    }
    return out;
}

bool looksLikeFable(const fs::path& dir) {
    return pathExists(dir / "Fable.exe") || pathExists(dir / "data");
}

} // namespace

fs::path detectSteamInstall(std::string& source) {
    source.clear();
#ifdef _WIN32
    std::string steam = readSteamPathReg();
    if (!steam.empty()) {
        fs::path steamDir = steam;
        // Steam's own library + any extra libraries in libraryfolders.vdf.
        std::vector<fs::path> libs{steamDir};
        for (const char* rel : {"steamapps/libraryfolders.vdf", "config/libraryfolders.vdf"}) {
            for (auto& p : parseLibraryPaths(steamDir / rel)) libs.push_back(p);
        }
        const char* names[] = {"Fable The Lost Chapters", "Fable - The Lost Chapters"};
        for (auto& lib : libs) {
            for (const char* nm : names) {
                fs::path cand = lib / "steamapps" / "common" / nm;
                if (looksLikeFable(cand)) { source = "libraryfolders"; return cand; }
            }
        }
    }
#endif
    return {};
}

fs::path defaultSavesRoot() {
    std::string home = envVar("USERPROFILE");
    if (home.empty()) home = envVar("HOME");
    if (home.empty()) return {};
    return fs::path(home) / "Documents" / "My Games" / "Fable" / "Saves";
}

fs::path Environment::configPath() {
    std::string appdata = envVar("APPDATA");
    if (appdata.empty()) appdata = envVar("HOME");
    if (appdata.empty()) return {};
    return fs::path(appdata) / "FableForge" / "config.ini";
}

fs::path Environment::loadOverride() {
    fs::path cfg = configPath();
    if (!pathExists(cfg)) return {};
    std::ifstream f(cfg);
    std::string line;
    while (std::getline(f, line)) {
        const std::string key = "install=";
        if (line.rfind(key, 0) == 0) {
            std::string v = line.substr(key.size());
            while (!v.empty() && (v.back() == '\r' || v.back() == '\n' || v.back() == ' ')) v.pop_back();
            return v;
        }
    }
    return {};
}

void Environment::saveOverride() const {
    fs::path cfg = configPath();
    if (cfg.empty()) return;
    std::error_code ec;
    fs::create_directories(cfg.parent_path(), ec);
    std::ofstream f(cfg, std::ios::trunc);
    f << "install=" << installDir.string() << "\n";
}

void Environment::resolveKeyFiles() {
    dataDir = installDir / "data";
    fableExe = firstExisting(installDir, {"Fable.exe"});
    gameBin = firstExisting(dataDir, {"CompiledDefs/game.bin", "Defs/game.bin"});
    namesBin = firstExisting(dataDir, {"CompiledDefs/names.bin", "Defs/names.bin"});
    fs::path lev = dataDir / "Levels";
    wld = firstExisting(lev, {"FinalAlbion.wld"});
    bwd = firstExisting(lev, {"FinalAlbion.bwd"});
    wad = firstExisting(lev, {"FinalAlbion.wad"});
    stb = firstExisting(lev, {"FinalAlbion_RT.stb"});
    ingameLug = firstExisting(dataDir, {"Sound/Ingame.lug"});
    textBig = firstExisting(dataDir, {"lang/English/text.big"});
    graphicsBig = firstExisting(dataDir, {"graphics/graphics.big"});
    texturesBig = firstExisting(dataDir, {"graphics/pc/textures.big"});

    fseDir = installDir / "FSE";
    fseInstalled = pathExists(fseDir);
    fseLauncher = firstExisting(installDir, {"FSE_Launcher.exe"});
    fseMasterLua = firstExisting(fseDir, {"Master/FSE_Master.lua"});
    fseLog = firstExisting(fseDir, {"FableScriptExtender.log"});
}

void Environment::enumerateSaves() {
    profiles.clear();
    if (savesRoot.empty()) savesRoot = defaultSavesRoot();
    if (!pathExists(savesRoot)) return;
    std::error_code ec;
    for (auto& entry : fs::directory_iterator(savesRoot, ec)) {
        if (!entry.is_directory()) continue;
        Profile prof;
        prof.name = entry.path().filename().string();
        prof.dir = entry.path();
        for (auto& sf : fs::directory_iterator(entry.path(), ec)) {
            if (!sf.is_regular_file()) continue;
            fs::path p = sf.path();
            // skip our own backups (.bak/.pre-*), keep bare saves
            std::string ext = p.extension().string();
            if (ext == ".bak") continue;
            SaveFile s;
            s.name = p.filename().string();
            s.path = p;
            s.size = (uint64_t)fs::file_size(p, ec);
            auto t = fs::last_write_time(p, ec);
            s.mtime = (int64_t)std::chrono::duration_cast<std::chrono::seconds>(
                          t.time_since_epoch()).count();
            // FableSave! magic + fixed 307200 size
            if (s.size == 307200) {
                std::ifstream in(p, std::ios::binary);
                char magic[10] = {0};
                in.read(magic, 10);
                s.looksValid = (std::string(magic, 10) == "FableSave!");
            }
            prof.saves.push_back(std::move(s));
        }
        std::sort(prof.saves.begin(), prof.saves.end(),
                  [](const SaveFile& a, const SaveFile& b) { return a.name < b.name; });
        profiles.push_back(std::move(prof));
    }
    std::sort(profiles.begin(), profiles.end(),
              [](const Profile& a, const Profile& b) { return a.name < b.name; });
}

Environment Environment::detect(const fs::path& overrideInstall) {
    Environment e;

    // 1) explicit override (persist it), or a previously-saved override.
    fs::path ovr = overrideInstall;
    std::string src = "override";
    if (ovr.empty()) { ovr = loadOverride(); src = "override"; }
    if (!ovr.empty() && looksLikeFable(ovr)) {
        e.installDir = ovr;
        e.detectSource = src;
    } else {
        // 2) Steam.
        std::string steamSrc;
        fs::path steam = detectSteamInstall(steamSrc);
        if (!steam.empty()) {
            e.installDir = steam;
            e.detectSource = steamSrc;
        } else {
            // 3) candidate paths.
            const char* cands[] = {
                "C:\\Programs\\Steam\\steamapps\\common\\Fable The Lost Chapters",
                "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Fable The Lost Chapters",
                "C:\\Program Files\\Steam\\steamapps\\common\\Fable The Lost Chapters",
            };
            for (const char* c : cands) {
                if (looksLikeFable(c)) { e.installDir = c; e.detectSource = "candidate"; break; }
            }
        }
    }

    if (overrideInstall.empty() == false && looksLikeFable(overrideInstall)) {
        e.saveOverride();  // persist a freshly-supplied override
    }

    e.installValid = !e.installDir.empty() && looksLikeFable(e.installDir);
    if (e.detectSource.empty()) e.detectSource = "none";
    if (e.installValid) e.resolveKeyFiles();
    e.savesRoot = defaultSavesRoot();
    e.enumerateSaves();
    return e;
}

size_t Environment::totalSaves() const {
    size_t n = 0;
    for (auto& p : profiles) for (auto& s : p.saves) if (s.looksValid) ++n;
    return n;
}

} // namespace forge::env
