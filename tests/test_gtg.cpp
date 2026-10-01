#include "gtg.hpp"
#include "forge/temporarydirectory.hpp"
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <cmath>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace fs = std::filesystem;
static void check(bool good, const char* message) { if (!good) throw std::runtime_error(message); }
static void write(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary); out << s; out.close(); check(bool(out), "fixture write failed");
}
static std::string read(const fs::path& p) { std::ifstream in(p, std::ios::binary); return {std::istreambuf_iterator<char>(in), {}}; }
#ifdef _WIN32
struct Lock {
    HANDLE handle;
    Lock(const fs::path& p, DWORD share) : handle(CreateFileW(p.c_str(), GENERIC_READ, share,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)) { check(handle != INVALID_HANDLE_VALUE, "fixture lock failed"); }
    ~Lock() { CloseHandle(handle); }
};
#endif
int main() {
    try {
        forge::TemporaryDirectory scratch(fs::current_path(), "gtg-test-");
        const auto root = scratch.path();
        const auto file = root / "data/Levels/FinalAlbion.gtg";
        const std::string empty = "NEWMAP 1\nVersion 2;\n\nENDMAP\n";
        const float pos[3] = {1, 2, 3}, forward[2] = {0, 1};
        const std::vector<std::string> invalid = {
            "NEWMAP 1\nVersion 2;\n",
            "NEWMAP 1\nVersion 2;\nNEWMAP 2\nVersion 2;\nENDMAP\n",
            empty + empty,
            "NEWMAP 2\nVersion 2;\nENDMAP\n" + empty,
            "NEWMAP 1\nVersion 2;\nXXXSectionStart NULL;\nNewThing Object;\nENDMAP\n",
            "NEWMAP 1\nVersion 2;\nXXXSectionStart NULL;\nNewThing Object;\nEndThing;\nENDMAP\n",
            "NEWMAP 1\nVersion 2;\nXXXSectionStart NULL;\nNewThing Object;\nStartCTCPhysicsStandard;\nEndThing;\nXXXSectionEnd;\nENDMAP\n",
            "NEWMAP 1\nVersion 2;\nXXXSectionStart NULL;\nNewThing Object;\nUID 18446744073709551615;\nEndThing;\nXXXSectionEnd;\nENDMAP\n",
        };
        std::string error;
        std::vector<std::string> notes;
        for (const auto& input : invalid) {
            write(file, input);
            check(!albion::editor::setRegionEntrance(root, 1, "Probe", pos, forward, notes, error), "malformed GTG accepted");
            check(read(file) == input && !fs::exists(file.string() + ".forge-orig") && notes.empty(), "refused GTG changed files or success notes");
        }
        write(file, empty);
        const float invalidPos[3] = {1, std::numeric_limits<float>::quiet_NaN(), 3};
        check(!albion::editor::setRegionEntrance(root, 1, "Probe", invalidPos, forward, notes, error), "non-finite position accepted");
        check(!albion::editor::setRegionEntrance(root, 1, "Probe;\nInjected", pos, forward, notes, error), "invalid script name accepted");
        check(read(file) == empty && !fs::exists(file.string() + ".forge-orig") && notes.empty(), "invalid entrance changed GTG");
        const std::string commented = "NEWMAP 1\nVersion 2;\n\n// retain this comment\nENDMAP\n";
        write(file, commented);
        check(albion::editor::setRegionEntrance(root, 1, "Probe", pos, forward, notes, error), "valid entrance failed");
        check(read(file).find("// retain this comment\n") != std::string::npos && read(file.string() + ".forge-orig") == commented,
              "entrance creation discarded prior section text or backup");
        const auto once = read(file);
        check(albion::editor::setRegionEntrance(root, 1, "Probe", pos, forward, notes, error) && read(file) == once, "same entrance update was not idempotent");
        notes.clear();
#ifdef _WIN32
        {
            Lock lock(file, FILE_SHARE_READ);
            check(!albion::editor::setRegionEntrance(root, 2, "Second", pos, forward, notes, error), "locked GTG replacement succeeded");
            check(read(file) == once && notes.empty(), "failed replacement changed GTG or success notes");
        }
        {
            Lock lock(file, 0);
            check(!albion::editor::setRegionEntrance(root, 2, "Second", pos, forward, notes, error) && !error.empty(), "unreadable GTG write succeeded");
        }
#endif
        const float largeForward[2] = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
        check(albion::editor::setRegionEntrance(root, 2, "Second", pos, largeForward, notes, error), "entrance retry failed");
        const auto entrance = albion::editor::entranceOf(root, 2, error);
        check(entrance && std::abs(entrance->forward[0] - 0.707107f) < 0.00001f && std::abs(entrance->forward[1] - 0.707107f) < 0.00001f,
              "large finite direction did not normalize");
        std::cout << "GTG entrance failure checks passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
