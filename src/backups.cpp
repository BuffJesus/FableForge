#include "backups.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace fs = std::filesystem;

namespace albion::backups {

namespace {

const char* kOrig = ".atlas-orig";
const char* kCreated = ".atlas-created";

bool endsWith(const std::string& s, const char* suffix) {
    const size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

bool sameBytes(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    if (!fs::exists(a, ec) || !fs::exists(b, ec)) return false;
    if (fs::file_size(a, ec) != fs::file_size(b, ec)) return false;
    std::ifstream fa(a, std::ios::binary), fb(b, std::ios::binary);
    std::vector<char> ba(1 << 20), bb(1 << 20);
    while (fa && fb) {
        fa.read(ba.data(), std::streamsize(ba.size()));
        fb.read(bb.data(), std::streamsize(bb.size()));
        if (fa.gcount() != fb.gcount() || !std::equal(ba.begin(), ba.begin() + fa.gcount(), bb.begin())) return false;
        if (fa.gcount() == 0) break;
    }
    return true;
}

std::string stamp(const fs::path& p) {
    std::error_code ec;
    const auto t = fs::last_write_time(p, ec);
    if (ec) return "";
    const auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(t - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    const std::time_t tt = std::chrono::system_clock::to_time_t(sys);
    char buf[32];
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tm);
    return buf;
}

void scanDir(const fs::path& dir, std::vector<Entry>& out) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return;
    for (const auto& it : fs::directory_iterator(dir, ec)) {
        if (!it.is_regular_file(ec)) continue;
        const std::string name = it.path().filename().string();
        Entry e;
        if (endsWith(name, kOrig)) {
            e.backup = it.path();
            e.file = dir / name.substr(0, name.size() - std::strlen(kOrig));
            e.differs = !sameBytes(e.file, e.backup);
        } else if (endsWith(name, kCreated)) {
            e.backup = it.path();
            e.file = dir / name.substr(0, name.size() - std::strlen(kCreated));
            e.created = true;
            e.differs = fs::exists(e.file, ec);
        } else continue;
        e.size = fs::exists(e.file, ec) ? fs::file_size(e.file, ec) : 0;
        e.when = stamp(e.backup);
        out.push_back(std::move(e));
    }
}

} // namespace

std::vector<Entry> scan(const fs::path& gameRoot) {
    std::vector<Entry> out;
    scanDir(gameRoot, out);
    scanDir(gameRoot / "data" / "Levels", out);
    scanDir(gameRoot / "data" / "Levels" / "FinalAlbion", out);
    scanDir(gameRoot / "data" / "CompiledDefs", out);
    scanDir(gameRoot / "data" / "graphics" / "pc", out);
    scanDir(gameRoot / "data" / "Misc" / "pc", out);
    scanDir(gameRoot / "FSE", out);
    scanDir(gameRoot / "FSE" / "PartyMode", out);
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a.file < b.file; });
    return out;
}

bool gameRunning() {
#ifdef _WIN32
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe; pe.dwSize = sizeof pe;
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"Fable.exe") == 0) { found = true; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
#else
    return false;
#endif
}

bool restore(const Entry& e, bool keepBackup, std::string& error) {
    std::error_code ec;
    if (e.created) {
        fs::remove(e.file, ec);
        if (ec) { error = "cannot delete " + e.file.string() + ": " + ec.message(); return false; }
        fs::remove(e.backup, ec);
        return true;
    }
    if (!fs::exists(e.backup, ec)) { error = "backup missing: " + e.backup.string(); return false; }
    // copy through a temp file so an interrupted restore never leaves a half file
    const fs::path tmp = e.file.string() + ".atlas-restore";
    fs::copy_file(e.backup, tmp, fs::copy_options::overwrite_existing, ec);
    if (ec) { error = "cannot copy " + e.backup.string() + ": " + ec.message(); return false; }
    fs::rename(tmp, e.file, ec);
    if (ec) { fs::remove(tmp, ec); error = "cannot replace " + e.file.string() + ": " + ec.message(); return false; }
    if (!keepBackup) fs::remove(e.backup, ec);
    return true;
}

size_t restoreAll(const fs::path& gameRoot, bool keepBackup, std::vector<std::string>& notes, std::string& error) {
    if (gameRunning()) { error = "Fable.exe is running; quit the game first (the engine holds these files open)"; return 0; }
    size_t n = 0;
    for (const auto& e : scan(gameRoot)) {
        if (!e.differs) continue;
        std::string err;
        if (!restore(e, keepBackup, err)) { error = err; notes.push_back("failed: " + err); continue; }
        notes.push_back((e.created ? "removed " : "restored ") + e.file.string());
        ++n;
    }
    return n;
}

} // namespace albion::backups
