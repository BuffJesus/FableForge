#include "backups.hpp"

#include "forge/stage.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwctype>
#include <fstream>
#include <map>
#include <stdexcept>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace fs = std::filesystem;

namespace albion::backups {

namespace {

bool endsWith(const std::string& s, const char* suffix) {
    const size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

fs::path targetKey(const fs::path& file) {
    auto key = file.lexically_normal();
#ifdef _WIN32
    auto name = key.wstring();
    for (auto& c : name) c = wchar_t(towlower(c));
    key = name;
#endif
    return key;
}

bool hasCreationMarker(const fs::path& file) {
    bool found = false;
    for (const char* suffix : {kCreatedSuffix, kLegacyCreatedSuffix}) {
        const fs::path marker = file.string() + suffix;
        if (!fs::exists(marker)) continue;
        if (!fs::is_regular_file(marker))
            throw std::runtime_error("creation marker is not a file: " + marker.string());
        found = true;
    }
    return found;
}

bool sameBytes(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    if (!fs::exists(a, ec) || !fs::exists(b, ec)) return false;
    const auto sizeA = fs::file_size(a, ec);
    if (ec) return false;
    const auto sizeB = fs::file_size(b, ec);
    if (ec || sizeA != sizeB) return false;
    std::ifstream fa(a, std::ios::binary), fb(b, std::ios::binary);
    if (!fa || !fb) return false;
    std::vector<char> ba(1 << 20), bb(1 << 20);
    for (;;) {
        fa.read(ba.data(), std::streamsize(ba.size()));
        fb.read(bb.data(), std::streamsize(bb.size()));
        if (fa.bad() || fb.bad()) return false;
        if (fa.gcount() != fb.gcount() || !std::equal(ba.begin(), ba.begin() + fa.gcount(), bb.begin())) return false;
        if (fa.gcount() == 0) return fa.eof() && fb.eof();
    }
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
        auto strip = [&](const char* suffix) { return dir / name.substr(0, name.size() - std::strlen(suffix)); };
        if (endsWith(name, kOrigSuffix) || endsWith(name, kLegacyOrigSuffix)) {
            e.backup = it.path();
            e.file = strip(endsWith(name, kOrigSuffix) ? kOrigSuffix : kLegacyOrigSuffix);
            e.kind = Kind::Original;
            e.differs = !sameBytes(e.file, e.backup);
        } else if (endsWith(name, kCreatedSuffix) || endsWith(name, kLegacyCreatedSuffix)) {
            e.backup = it.path();
            e.file = strip(endsWith(name, kCreatedSuffix) ? kCreatedSuffix : kLegacyCreatedSuffix);
            e.kind = Kind::Created;
            e.created = true;
            e.differs = fs::exists(e.file, ec);
        } else if (endsWith(name, kStagedSuffix)) {
            e.backup = it.path();
            e.file = strip(kStagedSuffix);
            e.kind = Kind::Staged;
            e.differs = !sameBytes(e.file, e.backup);
        } else if (endsWith(name, kOverlaySuffix)) {
            e.backup = it.path();
            e.file = strip(kOverlaySuffix);
            e.kind = Kind::Overlay;
            e.differs = !sameBytes(e.file, e.backup);
        } else continue;
        e.size = fs::exists(e.file, ec) ? fs::file_size(e.file, ec) : 0;
        e.when = stamp(e.backup);
        out.push_back(std::move(e));
    }
}

} // namespace

fs::path originalOf(const fs::path& file) {
    std::error_code ec;
    const fs::path legacy = file.string() + kLegacyOrigSuffix;
    if (fs::exists(legacy, ec)) return legacy;
    return file.string() + kOrigSuffix;
}

bool hasOriginal(const fs::path& file) {
    bool found = false;
    for (const char* suffix : {kOrigSuffix, kLegacyOrigSuffix}) {
        const fs::path original = file.string() + suffix;
        if (!fs::exists(original)) continue;
        if (!fs::is_regular_file(original))
            throw std::runtime_error("original backup is not a file: " + original.string());
        found = true;
    }
    return found;
}

bool backupOnce(const fs::path& file, std::string& error) {
    try {
        const bool created = hasCreationMarker(file);
        if (fs::exists(file) && !hasOriginal(file) && !created)
            fs::copy_file(file, file.string() + kOrigSuffix);
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

void markCreated(const fs::path& file) {
    const bool created = hasCreationMarker(file), original = hasOriginal(file);
    if (created || original) return;
    const fs::path marker = file.string() + kCreatedSuffix;
    std::ofstream out(marker, std::ios::binary);
    out << "created by FableForge\n";
    out.close();
    if (!out) throw std::runtime_error("cannot write creation marker: " + marker.string());
}

std::vector<Entry> scan(const fs::path& gameRoot) {
    std::vector<Entry> out;
    scanDir(gameRoot, out);
    scanDir(gameRoot / "data" / "Levels", out);
    scanDir(gameRoot / "data" / "Levels" / "FinalAlbion", out);
    scanDir(gameRoot / "data" / "CompiledDefs", out);
    scanDir(gameRoot / "data" / "graphics" / "pc", out);
    scanDir(gameRoot / "data" / "Misc" / "pc", out);
    scanDir(gameRoot / "data" / "lang" / "English", out);
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

bool gameRunningIn(const fs::path& gameRoot) {
#ifdef _WIN32
    std::error_code ec;
    fs::path root = fs::weakly_canonical(gameRoot, ec);
    if (ec) root = gameRoot;
    std::wstring rootW = root.wstring();
    for (auto& c : rootW) { if (c == L'/') c = L'\\'; c = wchar_t(towlower(c)); }
    // Match a directory boundary: install-other/Fable.exe is not running from
    // install, while install/Fable.exe and install/bin/Fable.exe both are.
    if (!rootW.empty() && rootW.back() != L'\\') rootW += L'\\';
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe; pe.dwSize = sizeof pe;
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"Fable.exe") != 0) continue;
            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
            if (!h) { found = true; break; }   // cannot tell: assume it is this install
            wchar_t buf[MAX_PATH * 2]; DWORD n = DWORD(sizeof buf / sizeof buf[0]);
            const bool ok = QueryFullProcessImageNameW(h, 0, buf, &n) != 0;
            CloseHandle(h);
            if (!ok) { found = true; break; }
            std::wstring img(buf, n);
            for (auto& c : img) { if (c == L'/') c = L'\\'; c = wchar_t(towlower(c)); }
            if (img.rfind(rootW, 0) == 0) { found = true; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
#else
    (void)gameRoot;
    return false;
#endif
}

bool restore(const Entry& e, bool keepBackup, std::string& error) {
    std::error_code ec;
    if (e.created) {
        fs::remove(e.file, ec);
        if (ec) { error = "cannot delete " + e.file.string() + ": " + ec.message(); return false; }
        fs::remove(e.backup, ec);
        if (ec) { error = "file removed, but cannot remove creation marker " + e.backup.string() + ": " + ec.message(); return false; }
        return true;
    }
    if (!fs::exists(e.backup, ec)) { error = "backup missing: " + e.backup.string(); return false; }
    if (e.kind == Kind::Staged) keepBackup = true;   // the stage manifest still names it
    // copy through a temp file so an interrupted restore never leaves a half file
    const fs::path tmp = e.file.string() + ".forge-restore";
    fs::copy_file(e.backup, tmp, fs::copy_options::overwrite_existing, ec);
    if (ec) { error = "cannot copy " + e.backup.string() + ": " + ec.message(); return false; }
    fs::rename(tmp, e.file, ec);
    if (ec) {
        error = "cannot replace " + e.file.string() + ": " + ec.message();
        std::error_code cleanup;
        fs::remove(tmp, cleanup);
        if (cleanup) error += "; cannot remove temporary file " + tmp.string() + ": " + cleanup.message();
        return false;
    }
    if (!keepBackup) {
        fs::remove(e.backup, ec);
        if (ec) { error = "file restored, but cannot remove backup " + e.backup.string() + ": " + ec.message(); return false; }
    }
    return true;
}

size_t restoreAll(const fs::path& gameRoot, bool keepBackup, std::vector<std::string>& notes, std::string& error) {
    if (gameRunningIn(gameRoot)) { error = "Fable.exe is running; quit the game first (the engine holds these files open)"; return 0; }
    const auto before = scan(gameRoot);
    // Older writers could add an original to a file already marked as created.
    // Neither record establishes which baseline is authoritative. Reject before
    // consuming a stage or changing any target, even if the target is missing.
    std::map<fs::path, fs::path> created, originals;
    for (const auto& e : before) {
        const auto key = targetKey(e.file);
        if (e.kind == Kind::Created) created[key] = e.backup;
        else if (e.kind == Kind::Original || e.kind == Kind::Overlay) originals[key] = e.backup;
    }
    for (const auto& [file, marker] : created) {
        const auto original = originals.find(file);
        if (original == originals.end()) continue;
        error = "conflicting restore records: " + marker.string() + " and " + original->second.string() +
            "; preserve both and resolve which baseline is intended before retrying";
        notes.push_back("failed: " + error);
        return 0;
    }
    size_t n = 0;
    std::error_code ec;
    // an original taken while a stage was live is the staged content, not retail: the stage's
    // older .forgebak is the true original. Prepare those baselines before
    // reverting consumes the staged originals, so a failed rebase is retryable.
    // a staged deploy first, through its manifest (restores and removes what it put there)
    if (fs::exists(forge::stage::manifestPath(gameRoot), ec)) {
        try {
            const auto plan = forge::stage::inspectRecovery(gameRoot);
            std::vector<Entry> rebase;
            for (const auto& staged : plan) {
                const auto key = targetKey(staged.target);
                if (!staged.hadOriginal) {
                    const auto original = originals.find(key);
                    if (original != originals.end())
                        throw std::runtime_error("conflicting restore records: stage created " + staged.target.string() +
                            " but original backup exists: " + original->second.string() +
                            "; preserve recovery data and resolve which baseline is intended before retrying");
                    continue;
                }
                for (const auto& e : before)
                    if (e.kind == Kind::Original && targetKey(e.file) == key &&
                        fs::last_write_time(e.backup) > fs::last_write_time(staged.backup)) {
                        Entry baseline;
                        baseline.file = e.backup;
                        baseline.backup = staged.backup;
                        rebase.push_back(std::move(baseline));
                    }
            }
            for (const auto& baseline : rebase) {
                std::string rebaseError;
                if (!restore(baseline, true, rebaseError))
                    throw std::runtime_error("cannot rebase editor baseline: " + rebaseError);
                notes.push_back("rebased " + baseline.file.filename().string() + " onto the staged original (it was taken on top of the stage)");
            }
            const auto r = forge::stage::revert(gameRoot);
            notes.push_back("reverted the staged deploy: " + std::to_string(r.restored.size()) + " restored, " + std::to_string(r.removed.size()) + " removed");
            n += r.restored.size() + r.removed.size();
        } catch (const std::exception& ex) {
            error = ex.what();
            notes.push_back(std::string("failed: ") + ex.what());
            // The manifest and its backups still describe a pending recovery.
            // Applying/forgetting other originals now could overwrite staged
            // targets and discard the state needed for a safe retry.
            return n;
        }
    }
    for (const auto& e : scan(gameRoot)) {
        if (e.kind == Kind::Staged) {
            if (e.differs) notes.push_back("left " + e.file.string() + " (a .forgebak outside any stage manifest; undeploy or remove it by hand)");
            continue;
        }
        if (!e.differs && !e.created) {
            if (!keepBackup) {
                fs::remove(e.backup, ec);
                if (ec) { error = "cannot remove unchanged backup " + e.backup.string() + ": " + ec.message(); notes.push_back("failed: " + error); }
                else notes.push_back("removed unchanged backup " + e.backup.string());
            }
            continue;
        }
        std::string err;
        if (!restore(e, keepBackup, err)) { error = err; notes.push_back("failed: " + err); continue; }
        notes.push_back((e.created ? "removed " : "restored ") + e.file.string());
        ++n;
    }
    return n;
}

} // namespace albion::backups
