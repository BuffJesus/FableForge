#pragma once
// Live link to the running game through ForgeFSE, with no native code: a Lua
// thread (FSE/AtlasLink/atlas_link.lua, started from a tagged Main() hook in a
// host quest script, PartyMode by default) polls FSE/AtlasLink/cmd.lua with
// loadfile() -- the FSE Lua state has no io library, so the command file is a
// Lua chunk returning a table -- executes it through the quest API and answers
// through the FSE log (ATLAS_LINK|... lines) that Atlas tails. A heartbeat line
// carries the hero's map and position every second.
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace albion::livelink {

inline constexpr const char* kHookTag = "-- ATLAS-LIVE-LINK";

// Install / remove the hook and the script. `host` is relative to <root>/FSE.
bool isInstalled(const std::filesystem::path& gameRoot, const std::string& host = "PartyMode/PartyMode.lua");
bool install(const std::filesystem::path& gameRoot, std::string& error, const std::string& host = "PartyMode/PartyMode.lua");
bool remove(const std::filesystem::path& gameRoot, std::string& error, const std::string& host = "PartyMode/PartyMode.lua");

// Commands (world coordinates). Each returns the command id the ack will carry.
uint64_t sendTeleport(const std::filesystem::path& gameRoot, int mapSlot, const std::string& mapName, float x, float y, std::string& error);
uint64_t sendSpawn(const std::filesystem::path& gameRoot, const std::string& definition, float x, float y, const std::string& scriptName, std::string& error);
uint64_t sendPing(const std::filesystem::path& gameRoot, std::string& error);

struct Status {
    bool logSeen = false;        // the FSE log exists
    bool ready = false;          // an ATLAS_LINK|ready line was seen (the thread runs)
    double heartbeatAge = -1;    // seconds since the last heartbeat (-1 = none); the game is live when small
    std::string heroMap;
    float heroX = 0, heroY = 0, heroZ = 0;
    uint64_t lastAckId = 0;
    bool lastAckOk = false;
    std::string lastAckMessage;
    std::vector<std::string> recent;   // the last few ATLAS_LINK lines
};
// Tail the FSE log (cheap; only the last 64 KiB are read).
Status poll(const std::filesystem::path& gameRoot);

} // namespace albion::livelink
