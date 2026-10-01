#include "livelink.hpp"
#include <forge/temporarydirectory.hpp>
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
static void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static void write(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary); out << s; out.close(); check(bool(out), "fixture write failed");
}
static std::string read(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
#ifdef _WIN32
struct Lock {
    HANDLE handle;
    Lock(const fs::path& p, DWORD share) : handle(CreateFileW(p.c_str(), GENERIC_READ, share,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)) {
        check(handle != INVALID_HANDLE_VALUE, "cannot lock fixture");
    }
    ~Lock() { CloseHandle(handle); }
};
#endif
int main() {
    try {
        forge::TemporaryDirectory scratch(fs::current_path(), "livelink-test-");
        const auto root = scratch.path();
        const auto host = root / "FSE/PartyMode/PartyMode.lua";
        const std::string original = "function Main(quest)\r\n    return 42\r\nend\r\n";
        const std::string tail = "\n-- user addition\nfunction Extra() return 7 end\n";
        std::string error;
        write(host, original);
        check(albion::livelink::install(root, error), "install failed");
        const auto installed = read(host);
        check(albion::livelink::install(root, error) && read(host) == installed, "reinstall duplicated hook");
        write(host, installed + tail);
        check(albion::livelink::remove(root, error), "remove failed");
        check(read(host) == original + tail, "removal deleted subsequent user script");
        const auto hook = installed.substr(original.size());
        std::string crlf;
        for (char c : hook) { if (c == '\n') crlf += '\r'; crlf += c; }
        write(host, original + crlf + "\r\n" + tail);
        check(albion::livelink::remove(root, error) && read(host) == original + "\r\n" + tail,
              "CRLF removal changed surrounding bytes");
        auto moved = hook;
        const auto pathStart = moved.find("loadfile([[") + std::string("loadfile([[").size();
        moved.replace(pathStart, moved.find("]])", pathStart) - pathStart, "C:/Old Install/FSE/AtlasLink/atlas_link.lua");
        write(host, original + moved + tail);
        check(albion::livelink::remove(root, error) && read(host) == original + tail, "old install path not removable");
        write(host, installed + hook + tail);
        check(!albion::livelink::remove(root, error) && read(host) == installed + hook + tail, "duplicate hook changed");
        write(host, installed + tail);
        fs::create_directory(root / "FSE/AtlasLink/cmd.lua");
        check(!albion::livelink::remove(root, error) && read(host) == installed + tail, "command preflight failure changed host");
        fs::remove(root / "FSE/AtlasLink/cmd.lua");
        check(!albion::livelink::install(root, error, "../outside.lua"), "escaping host accepted");
        check(!albion::livelink::install(root, error, "./atlaslink/atlas_link.lua"), "worker accepted as host");
        write(host, installed);
        auto modified = installed;
        modified.replace(modified.find("return _atlasLinkMain"), 6, "-- return");
        write(host, modified + tail);
        check(!albion::livelink::remove(root, error), "modified hook accepted");
        check(read(host) == modified + tail, "modified hook changed on refusal");
        write(host, original);
        fs::remove(host.string() + ".forge-orig");
        fs::create_directory(host.string() + ".forge-orig");
        check(!albion::livelink::install(root, error), "backup failure ignored");
        check(read(host) == original, "host changed after backup failure");
        fs::remove(host.string() + ".forge-orig");
#ifdef _WIN32
        const auto script = root / "FSE/AtlasLink/atlas_link.lua";
        write(script, "old worker script");
        {
            Lock lock(host, FILE_SHARE_READ);
            check(!albion::livelink::install(root, error), "locked host install succeeded");
            check(read(host) == original && read(script) == "old worker script", "install rollback lost original files");
        }
        check(albion::livelink::install(root, error), "install retry failed");
        const auto hooked = read(host);
        const auto command = root / "FSE/AtlasLink/cmd.lua";
        write(command, "pending command");
        {
            Lock lock(command, FILE_SHARE_READ);
            check(!albion::livelink::remove(root, error), "locked command removal succeeded");
            check(read(host) == hooked && read(command) == "pending command", "remove rollback lost original files");
        }
        {
            Lock lock(host, 0);
            check(!albion::livelink::install(root, error), "unreadable host install succeeded");
            check(!albion::livelink::remove(root, error), "unreadable host removal succeeded");
            check(!albion::livelink::isInstalled(root), "unreadable host reported installed");
        }
        check(albion::livelink::remove(root, error) && read(host) == original && !fs::exists(command), "remove retry failed");
#endif
        std::cout << "Live-link hook checks passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
