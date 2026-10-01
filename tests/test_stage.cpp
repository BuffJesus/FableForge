#include "forge/stage.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace fs = std::filesystem;
static void check(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
static void put(const fs::path& p, const std::string& text) { std::ofstream(p, std::ios::binary) << text; }
static std::string get(const fs::path& p) { std::ifstream f(p, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}}; }
int main() {
    const auto root = fs::temp_directory_path() / ("forge-stage-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        check(fs::create_directory(root), "scratch collision");
        put(root / "a", "mod a"); put(root / "a.forgebak", "original a"); put(root / "b", "mod b");
        const std::string manifest = R"({"files":[{"path":"a","had_original":true},{"path":"b","had_original":true}]})";
        put(forge::stage::manifestPath(root), manifest);
        bool failed = false;
        try { forge::stage::revert(root); } catch (const std::exception&) { failed = true; }
        check(failed, "missing original backup was accepted");
        check(get(root / "a") == "mod a" && get(root / "b") == "mod b", "missing-backup preflight changed targets");
        check(get(root / "a.forgebak") == "original a" && get(forge::stage::manifestPath(root)) == manifest, "missing backup discarded recovery data");
        put(root / "b.forgebak", "original b");
#ifdef _WIN32
        HANDLE locked = CreateFileW((root / "b").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(locked != INVALID_HANDLE_VALUE, "could not lock second target");
        failed = false;
        try { forge::stage::revert(root); } catch (const std::exception&) { failed = true; }
        CloseHandle(locked);
        check(failed, "locked second target was accepted");
        check(get(root / "a.forgebak") == "original a" && get(root / "b.forgebak") == "original b", "failed restore consumed an original backup");
        check(get(forge::stage::manifestPath(root)) == manifest, "failed restore lost manifest");
#endif
        const auto result = forge::stage::revert(root);
        check(result.restored.size() == 2 && result.removed.empty(), "retry did not restore both originals");
        check(get(root / "a") == "original a" && get(root / "b") == "original b", "retry deleted or changed originals");
        check(!fs::exists(forge::stage::manifestPath(root)) && !fs::exists(root / "a.forgebak"), "successful restore retained recovery state");
        put(root / "new", "new mod file");
        put(forge::stage::manifestPath(root), R"({"files":[{"path":"new","had_original":false}]})");
        check(forge::stage::revert(root).removed.size() == 1 && !fs::exists(root / "new"), "new file was not removed");
        put(forge::stage::manifestPath(root), R"({"files":[{"path":"../escape","had_original":false}]})");
        failed = false;
        try { forge::stage::revert(root); } catch (const std::exception&) { failed = true; }
        check(failed && fs::exists(forge::stage::manifestPath(root)), "escaping manifest path was accepted");
        fs::remove(forge::stage::manifestPath(root));
        const auto mod = root / "input";
        fs::create_directory(mod);
        put(mod / "a", "replacement a"); put(mod / "b", "replacement b");
#ifdef _WIN32
        locked = CreateFileW((root / "b").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(locked != INVALID_HANDLE_VALUE, "could not lock staging target");
        failed = false;
        try { forge::stage::apply(root, mod); } catch (const std::exception&) { failed = true; }
        CloseHandle(locked);
        check(failed, "locked staging target was accepted");
        check(fs::exists(forge::stage::manifestPath(root)), "failed stage left no recovery manifest");
        forge::stage::revert(root);
        check(get(root / "a") == "original a" && get(root / "b") == "original b", "failed-stage recovery changed originals");
#endif
#ifdef _WIN32
        locked = CreateFileW((root / "b").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(locked != INVALID_HANDLE_VALUE, "could not lock original against backup reads");
        failed = false;
        try { forge::stage::apply(root, mod); } catch (const std::exception&) { failed = true; }
        CloseHandle(locked);
        check(failed, "unreadable original was accepted");
        check(get(root / "a") == "original a" && get(root / "b") == "original b", "backup failure changed targets");
        check(!fs::exists(forge::stage::manifestPath(root)) && !fs::exists(root / "a.forgebak") && !fs::exists(root / "b.forgebak"), "backup failure leaked recovery state");
#endif
        put(root / "b.forgebak", "stale backup");
        failed = false;
        try { forge::stage::apply(root, mod); } catch (const std::exception&) { failed = true; }
        check(failed, "stage reused an unowned backup");
        check(get(root / "a") == "original a" && get(root / "b") == "original b", "stale-backup preflight changed targets");
        check(get(root / "b.forgebak") == "stale backup", "stage changed an unowned backup");
        fs::remove(root / "b.forgebak");
        put(mod / "new", "new stage file");
        check(forge::stage::apply(root, mod).staged.size() == 3, "successful stage missed files");
        check(get(root / "a") == "replacement a" && get(root / "b") == "replacement b", "stage omitted replacement bytes");
        forge::stage::revert(root);
        check(get(root / "a") == "original a" && get(root / "b") == "original b" && !fs::exists(root / "new"), "stage round trip changed originals");
        for (const auto* reserved : {"a.FORGEBak", "FORGE_STAGE_MANIFEST.JSON"}) {
            put(mod / reserved, "must not overwrite recovery data");
            failed = false;
            try { forge::stage::apply(root, mod); } catch (const std::exception&) { failed = true; }
            check(failed, "case alias of recovery file was accepted");
            check(get(root / "a") == "original a" && get(root / "b") == "original b", "reserved-path preflight changed originals");
            check(!fs::exists(forge::stage::manifestPath(root)), "reserved-path preflight created a manifest");
            fs::remove(mod / reserved);
        }
#ifdef _WIN32
        put(root / "a.forgebak", "original a");
        put(forge::stage::manifestPath(root), R"({"files":[{"path":"a","had_original":true},{"path":"A","had_original":true}]})");
        failed = false;
        try { forge::stage::revert(root); } catch (const std::exception&) { failed = true; }
        check(failed && get(root / "a") == "original a" && fs::exists(root / "a.forgebak"), "restore accepted duplicate Windows target aliases");
#endif
        fs::remove_all(root);
        std::cout << "stage restore checks passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << " (scratch retained: " << root << ")\n"; return 1; }
}
