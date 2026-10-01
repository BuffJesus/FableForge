#include "livelink.hpp"
#include <forge/temporarydirectory.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <limits>
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
        const auto cmd = root / "FSE/AtlasLink/cmd.lua";
        const std::string longDefinition = "CREATURE_" + std::string(600, 'D');
        const std::string longName = "Script_" + std::string(600, 'N');
        check(albion::livelink::sendSpawn(root, longDefinition, 12.5f, -7.25f, longName, error) != 0, "long spawn send failed");
        check(read(cmd).find(longDefinition) != std::string::npos && read(cmd).find(longName) != std::string::npos,
              "spawn command silently truncated string fields");
        const std::string longMap = "Map_" + std::string(600, 'M');
        check(albion::livelink::sendTeleport(root, 4, longMap, 12.5f, -7.25f, error) != 0 && read(cmd).find(longMap) != std::string::npos,
              "teleport command truncated map");
        write(cmd.string() + ".tmp", "unrelated temporary file");
        check(albion::livelink::sendPing(root, error) != 0 && read(cmd.string() + ".tmp") == "unrelated temporary file",
              "send consumed an unowned temporary file");
        const auto beforeInvalid = read(cmd);
        check(albion::livelink::sendReload(root, 4, std::numeric_limits<float>::infinity(), 0, error) == 0 && read(cmd) == beforeInvalid,
              "non-finite reload changed command");
        check(albion::livelink::sendTeleport(root, 4, "Map", 0, std::numeric_limits<float>::quiet_NaN(), error) == 0 && read(cmd) == beforeInvalid,
              "non-finite teleport changed command");
        check(albion::livelink::sendSpawn(root, "CREATURE", -std::numeric_limits<float>::infinity(), 0, "Probe", error) == 0 && read(cmd) == beforeInvalid,
              "non-finite spawn changed command");
        const std::string special = std::string("quote\"slash\\line\nreturn\rtab\t") + '\0' + "7";
        check(albion::livelink::sendSpawn(root, special, 12.5f, -7.25f, special, error) != 0, "escaped spawn failed");
        const auto escaped = read(cmd);
        const std::string literal = R"("quote\"slash\\line\010return\013tab\009\0007")";
        check(escaped.find("def = " + literal) != std::string::npos && escaped.find("name = " + literal) != std::string::npos,
              "Lua string escapes did not preserve input bytes");
        check(escaped.find("x = 12.500, y = -7.250") != std::string::npos, "coordinate formatting changed");
#ifdef _WIN32
        {
            Lock lock(cmd, FILE_SHARE_READ);
            check(albion::livelink::sendPing(root, error) == 0 && read(cmd) == escaped, "failed send changed pending command");
        }
#endif
        check(albion::livelink::sendPing(root, error) != 0 && read(cmd).find("cmd = \"ping\"") != std::string::npos, "send retry failed");
        for (const auto& entry : fs::directory_iterator(root))
            check(entry.path().filename().string().find(".forge-link-") != 0, "owned live-link workspace leaked");
        const auto log = root / "FSE/FableScriptExtender.log";
        write(log, "prefix ATLAS_LINK|ready\r\nprefix ATLAS_LINK|hero|2|Greatwood_1|12.5|-7.25|9.0\r\n");
        fs::last_write_time(log, fs::file_time_type::clock::now() - std::chrono::seconds(60));
        auto status = albion::livelink::poll(root);
        check(status.ready && status.heroMap == "Greatwood_1" && status.heroX == 12.5f && status.heroY == -7.25f && status.heroZ == 9,
              "valid heartbeat parse failed");
        check(status.heartbeatAge >= 55, "old log was reported freshly live");
        const auto otherRoot = root / "other-install";
        write(otherRoot / "FSE/FableScriptExtender.log", read(log));
        const auto otherStatus = albion::livelink::poll(otherRoot);
        check(otherStatus.heartbeatAge >= 0 && otherStatus.heartbeatAge < 5, "another install inherited stale heartbeat timing");
        write(log, read(log) + "unrelated new log line\n");
        check(albion::livelink::poll(root).heartbeatAge >= 55, "unrelated log activity refreshed an old heartbeat");
        write(log, "ATLAS_LINK|hero|4|Greatwood_1|12.5|-7.25|9.0\nATLAS_LINK|ack|123|true|message|with|pipes\r\n");
        status = albion::livelink::poll(root);
        check(status.heartbeatAge >= 0 && status.heartbeatAge < 5 && status.lastAckId == 123 && status.lastAckOk && status.lastAckMessage == "message|with|pipes",
              "fresh heartbeat or complete acknowledgement parse failed");
        for (const std::string& invalid : {
                "ATLAS_LINK|hero|6|Map|nan|2|3\n", "ATLAS_LINK|hero|6|Map|1|inf|3\n",
                "ATLAS_LINK|hero|6|Map|1oops|2|3\n", "ATLAS_LINK|hero|6|Map|1|2|",
                "ATLAS_LINK|hero|oops|Map|1|2|3\n", "ATLAS_LINK|hero|6||1|2|3\n"}) {
            write(log, invalid);
            status = albion::livelink::poll(root);
            check(status.heartbeatAge < 0 && status.heroMap.empty(), "malformed or partial heartbeat accepted");
        }
        write(log, "ATLAS_LINK|ack|123oops|true|bad\nATLAS_LINK|ack|124|maybe|bad\nATLAS_LINK|ack|125|true|partial");
        check(albion::livelink::poll(root).lastAckId == 0, "malformed or partial acknowledgement accepted");
        write(log, std::string(70000, 'x') + "ATLAS_LINK|hero|6|Clipped|1|2|3\nATLAS_LINK|ack|126|false|failed\n");
        status = albion::livelink::poll(root);
        check(status.heartbeatAge < 0 && status.lastAckId == 126 && !status.lastAckOk && status.lastAckMessage == "failed",
              "clipped first tail line accepted or complete tail line lost");
        write(log, "ATLAS_LINK|hero|8|Valid|1|2|3\nATLAS_LINK|hero|10|Invalid|nan|2|3\n");
        status = albion::livelink::poll(root);
        check(status.heroMap == "Valid" && status.heroX == 1 && status.heroY == 2 && status.heroZ == 3,
              "invalid heartbeat displaced the last valid position");
#ifdef _WIN32
        {
            Lock lock(log, 0);
            check(albion::livelink::poll(root).heartbeatAge < 0, "unreadable log reported live");
        }
#endif
        std::cout << "Live-link hook, command and status checks passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
