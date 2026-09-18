#include "livelink.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace albion::livelink {

namespace {

const char* kScript = R"LUA(-- Albion Atlas live link (installed from the editor; remove it there or delete the ATLAS-LIVE-LINK hook).
-- Polls FSE/AtlasLink/cmd.lua (a Lua chunk returning {id=, cmd=, ...}) and answers in the FSE log.
ATLAS_LINK_CMD = [[%CMD%]]

function AtlasLink(questObject)
    local Q = questObject
    Q:Log("ATLAS_LINK|ready")
    local lastId = 0
    local beat = 0
    local hero = nil
    while true do
        Q:Pause(0.5)
        if not Q:NewScriptFrame() then return end
        if hero == nil then
            local ok, h = pcall(function() return Q:GetHero() end)
            if ok and h ~= nil then hero = h end
        end
        beat = beat + 1
        if hero ~= nil and beat % 2 == 0 then
            local mok, mname = pcall(function() return hero:GetCurrentMapName() end)
            local pok, p = pcall(function() return hero:GetPos() end)
            if mok and pok and p then
                Q:Log(string.format("ATLAS_LINK|hero|%d|%s|%.3f|%.3f|%.3f", beat, tostring(mname), p.x or 0, p.y or 0, p.z or 0))
            end
        end
        local f = loadfile(ATLAS_LINK_CMD)
        if f then
            local ok, t = pcall(f)
            if ok and type(t) == "table" and t.id and t.id ~= lastId then
                lastId = t.id
                local rok, rerr = pcall(function()
                    if t.cmd == "ping" then
                        return "pong"
                    elseif t.cmd == "teleport" then
                        local z = 0
                        pcall(function() z = Q:GetGroundHeightAt(t.x, t.y) end)
                        local mname = nil
                        if hero ~= nil then pcall(function() mname = hero:GetCurrentMapName() end) end
                        if hero ~= nil and mname == t.map then
                            Q:EntityTeleportToPosition(hero, {x = t.x, y = t.y, z = z + 0.5}, 0.0)
                            return "teleported within " .. tostring(mname)
                        else
                            Q:GoToMapSlotRetailTransition(t.slot, t.x, t.y, z + 0.5)
                            return "transition to slot " .. tostring(t.slot)
                        end
                    elseif t.cmd == "spawn" then
                        local z = 0
                        pcall(function() z = Q:GetGroundHeightAt(t.x, t.y) end)
                        local c = Q:CreateCreature(t.def, {x = t.x, y = t.y, z = z + 0.5}, t.name or "AtlasSpawn")
                        return "spawned " .. tostring(t.def)
                    end
                    return "unknown command " .. tostring(t.cmd)
                end)
                Q:Log("ATLAS_LINK|ack|" .. tostring(t.id) .. "|" .. tostring(rok) .. "|" .. tostring(rerr))
            end
        end
    end
end
)LUA";

std::string readAll(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool writeAll(const fs::path& p, const std::string& s, std::string& error) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) { error = "cannot write " + p.string(); return false; }
    out.write(s.data(), std::streamsize(s.size()));
    return bool(out);
}

fs::path linkDir(const fs::path& root) { return root / "FSE" / "AtlasLink"; }
fs::path cmdPath(const fs::path& root) { return linkDir(root) / "cmd.lua"; }
fs::path logPath(const fs::path& root) { return root / "FSE" / "FableScriptExtender.log"; }

std::string hookText(const fs::path& script) {
    std::string posix = script.string();
    for (char& c : posix) if (c == '\\') c = '/';
    return std::string("\n") + kHookTag + " (installed by Albion Atlas; the editor's Live link card removes it)\n"
           "local _atlasLinkMain = Main\n"
           "function Main(quest)\n"
           "    pcall(function()\n"
           "        local f, err = loadfile([[" + posix + "]])\n"
           "        if f then f(); quest:CreateThread(\"AtlasLink\", {}) else quest:Log(\"ATLAS_LINK|error|\" .. tostring(err)) end\n"
           "    end)\n"
           "    return _atlasLinkMain(quest)\n"
           "end\n";
}

uint64_t nextId() {
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

uint64_t send(const fs::path& root, const std::string& body, std::string& error) {
    std::error_code ec;
    fs::create_directories(linkDir(root), ec);
    const uint64_t id = nextId();
    const std::string chunk = "return {id = " + std::to_string(id) + ", " + body + "}\n";
    const fs::path tmp = cmdPath(root).string() + ".tmp";
    if (!writeAll(tmp, chunk, error)) return 0;
    fs::rename(tmp, cmdPath(root), ec);
    if (ec) { error = "cannot replace " + cmdPath(root).string() + ": " + ec.message(); return 0; }
    return id;
}

std::string luaString(const std::string& s) {
    std::string out = "\"";
    for (char c : s) { if (c == '"' || c == '\\') out += '\\'; out += c; }
    return out + "\"";
}

} // namespace

bool isInstalled(const fs::path& root, const std::string& host) {
    const fs::path master = root / "FSE" / host;
    if (!fs::exists(master)) return false;
    return readAll(master).find(kHookTag) != std::string::npos;
}

bool install(const fs::path& root, std::string& error, const std::string& host) {
    const fs::path master = root / "FSE" / host;
    if (!fs::exists(master)) { error = "no host quest script " + master.string() + " (is ForgeFSE installed?)"; return false; }
    std::error_code ec;
    fs::create_directories(linkDir(root), ec);
    const fs::path script = linkDir(root) / "atlas_link.lua";
    std::string body = kScript;
    std::string cmd = cmdPath(root).string();
    for (char& c : cmd) if (c == '\\') c = '/';
    const size_t at = body.find("%CMD%");
    if (at != std::string::npos) body.replace(at, 5, cmd);
    if (!writeAll(script, body, error)) return false;
    if (isInstalled(root, host)) return true;
    const fs::path backup = master.string() + ".atlas-orig";
    if (!fs::exists(backup)) fs::copy_file(master, backup, ec);
    return writeAll(master, readAll(master) + hookText(script), error);
}

bool remove(const fs::path& root, std::string& error, const std::string& host) {
    const fs::path master = root / "FSE" / host;
    if (!fs::exists(master)) return true;
    std::string text = readAll(master);
    const size_t at = text.find(std::string("\n") + kHookTag);
    if (at != std::string::npos) {
        text.erase(at);
        if (!writeAll(master, text, error)) return false;
    }
    std::error_code ec;
    fs::remove(cmdPath(root), ec);
    return true;
}

uint64_t sendTeleport(const fs::path& root, int mapSlot, const std::string& mapName, float x, float y, std::string& error) {
    char buf[256];
    std::snprintf(buf, sizeof buf, "cmd = \"teleport\", slot = %d, map = %s, x = %.3f, y = %.3f", mapSlot, luaString(mapName).c_str(), x, y);
    return send(root, buf, error);
}

uint64_t sendSpawn(const fs::path& root, const std::string& definition, float x, float y, const std::string& scriptName, std::string& error) {
    char buf[512];
    std::snprintf(buf, sizeof buf, "cmd = \"spawn\", def = %s, x = %.3f, y = %.3f, name = %s", luaString(definition).c_str(), x, y,
                  luaString(scriptName.empty() ? "AtlasSpawn" : scriptName).c_str());
    return send(root, buf, error);
}

uint64_t sendPing(const fs::path& root, std::string& error) { return send(root, "cmd = \"ping\"", error); }

Status poll(const fs::path& root) {
    static std::string lastBeat;
    static std::chrono::steady_clock::time_point lastBeatAt;
    Status s;
    const fs::path log = logPath(root);
    std::error_code ec;
    if (!fs::exists(log, ec)) return s;
    s.logSeen = true;
    std::ifstream in(log, std::ios::binary);
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    const std::streamoff from = size > 65536 ? size - 65536 : 0;
    in.seekg(from);
    std::string tail((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::istringstream lines(tail);
    std::string line, beat;
    while (std::getline(lines, line)) {
        const size_t at = line.find("ATLAS_LINK|");
        if (at == std::string::npos) continue;
        const std::string msg = line.substr(at + 11);
        if (!msg.empty() && msg.back() == '\r') { /* keep as is */ }
        s.recent.push_back(msg);
        if (s.recent.size() > 8) s.recent.erase(s.recent.begin());
        if (msg.rfind("ready", 0) == 0) s.ready = true;
        else if (msg.rfind("hero|", 0) == 0) {
            beat = msg;
            // hero|beat|map|x|y|z
            std::vector<std::string> f; std::string cur;
            for (char c : msg) { if (c == '|') { f.push_back(cur); cur.clear(); } else cur += c; }
            f.push_back(cur);
            if (f.size() >= 6) { s.heroMap = f[2]; s.heroX = std::strtof(f[3].c_str(), nullptr); s.heroY = std::strtof(f[4].c_str(), nullptr); s.heroZ = std::strtof(f[5].c_str(), nullptr); }
        } else if (msg.rfind("ack|", 0) == 0) {
            std::vector<std::string> f; std::string cur;
            for (char c : msg) { if (c == '|') { f.push_back(cur); cur.clear(); } else cur += c; }
            f.push_back(cur);
            if (f.size() >= 4) { s.lastAckId = std::strtoull(f[1].c_str(), nullptr, 10); s.lastAckOk = f[2] == "true"; s.lastAckMessage = f[3]; }
        }
    }
    if (!beat.empty()) {
        if (beat != lastBeat) { lastBeat = beat; lastBeatAt = std::chrono::steady_clock::now(); }
        s.heartbeatAge = std::chrono::duration<double>(std::chrono::steady_clock::now() - lastBeatAt).count();
    }
    return s;
}

} // namespace albion::livelink
