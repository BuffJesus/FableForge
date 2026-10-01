#include "backups.hpp"
#include "livelink.hpp"
#include "pendingbanks.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace albion::livelink {

namespace {

const char* kScript = R"LUA(-- FableForge live link (installed from the editor; remove it there or delete the ATLAS-LIVE-LINK hook).
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
                    elseif t.cmd == "reload" then
                        -- a retail region transition into the hero's own map re-streams
                        -- the region: edited .lev/.wad/.stb data comes in
                        local z = 0
                        pcall(function() z = Q:GetGroundHeightAt(t.x, t.y) end)
                        Q:GoToMapSlotRetailTransition(t.slot, t.x, t.y, z + 0.5)
                        return "reloading slot " .. tostring(t.slot)
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
    if (!in) throw std::runtime_error("cannot read " + p.string());
    std::string result((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) throw std::runtime_error("cannot read " + p.string());
    return result;
}

bool writeAll(const fs::path& p, const std::string& s, std::string& error) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) { error = "cannot write " + p.string(); return false; }
    out.write(s.data(), std::streamsize(s.size()));
    out.close();
    if (!out) { error = "cannot finish writing " + p.string(); return false; }
    return true;
}

fs::path linkDir(const fs::path& root) { return root / "FSE" / "AtlasLink"; }
fs::path cmdPath(const fs::path& root) { return linkDir(root) / "cmd.lua"; }
fs::path logPath(const fs::path& root) { return root / "FSE" / "FableScriptExtender.log"; }

std::string hookText(const fs::path& script) {
    std::string posix = script.string();
    for (char& c : posix) if (c == '\\') c = '/';
    return std::string("\n") + kHookTag + " (installed by FableForge; the editor's Live link card removes it)\n"
           "local _atlasLinkMain = Main\n"
           "function Main(quest)\n"
           "    pcall(function()\n"
           "        local f, err = loadfile([[" + posix + "]])\n"
           "        if f then f(); quest:CreateThread(\"AtlasLink\", {}) else quest:Log(\"ATLAS_LINK|error|\" .. tostring(err)) end\n"
           "    end)\n"
           "    return _atlasLinkMain(quest)\n"
           "end\n";
}

// Match the complete generated legacy hook, allowing CRLF and an old install path.
// Byte offsets preserve all surrounding text without reserializing the user's script.
bool stripHook(std::string& text) {
    std::string normalized;
    std::vector<size_t> offsets;
    for (size_t i = 0; i < text.size(); ++i) {
        offsets.push_back(i);
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ++i;
        normalized += text[i];
    }
    offsets.push_back(text.size());
    const auto tag = normalized.find(kHookTag);
    if (tag == std::string::npos) return false;
    const auto refuse = [] { throw std::runtime_error("live-link hook is modified or ambiguous; host script was not changed"); };
    if (tag == 0 || normalized[tag - 1] != '\n' || normalized.find(kHookTag, tag + 1) != std::string::npos) refuse();
    const std::string prefix = "        local f, err = loadfile([[";
    const auto pathAt = normalized.find(prefix, tag);
    if (pathAt == std::string::npos) refuse();
    const auto pathEnd = normalized.find("]])\n", pathAt + prefix.size());
    if (pathEnd == std::string::npos) refuse();
    const auto expected = hookText(fs::path(normalized.substr(pathAt + prefix.size(), pathEnd - pathAt - prefix.size())));
    const auto start = tag - 1;
    if (normalized.compare(start, expected.size(), expected) != 0) refuse();
    const auto byteStart = offsets[start];
    text.erase(byteStart, offsets[start + expected.size()] - byteStart);
    return true;
}

fs::path hostRelative(const std::string& host) {
    const fs::path path(host);
    if (path.empty() || path.has_root_path()) throw std::runtime_error("host quest script must be relative to FSE");
    for (const auto& part : path)
        if (part == "..") throw std::runtime_error("host quest script must stay inside FSE");
    const auto normalized = path.lexically_normal();
    auto first = normalized.begin()->string();
    for (auto& c : first) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    if (first == "atlaslink") throw std::runtime_error("host quest script cannot be inside the live-link worker directory");
    return fs::path("FSE") / normalized;
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
    try { return readAll(root / hostRelative(host)).find(kHookTag) != std::string::npos; }
    catch (const std::exception&) { return false; }
}

bool install(const fs::path& root, std::string& error, const std::string& host) {
    try {
        const auto relative = hostRelative(host);
        const auto master = root / relative;
        const auto original = readAll(master);
        auto withoutHook = original;
        const bool installed = stripHook(withoutHook);
        const fs::path script = linkDir(root) / "atlas_link.lua";
        std::string body = kScript;
        std::string cmd = cmdPath(root).generic_string();
        body.replace(body.find("%CMD%"), 5, cmd);
        detail::PendingBanks pending(root, ".forge-link-install-");
        if (!writeAll(pending.prepare("FSE/AtlasLink/atlas_link.lua"), body, error)) return false;
        if (!installed) {
            if (!backups::backupOnce(master, error)) return false;
            if (!writeAll(pending.prepare(relative), original + hookText(script), error)) return false;
        }
        return pending.install(false, error);
    } catch (const std::exception& e) { error = e.what(); return false; }
}

bool remove(const fs::path& root, std::string& error, const std::string& host) {
    try {
        const auto relative = hostRelative(host);
        const auto master = root / relative;
        if (!fs::exists(master)) return true;
        auto text = readAll(master);
        const bool installed = stripHook(text);
        detail::PendingBanks pending(root, ".forge-link-remove-");
        if (installed && !writeAll(pending.prepare(relative), text, error)) return false;
        pending.remove("FSE/AtlasLink/cmd.lua");
        return pending.install(false, error);
    } catch (const std::exception& e) { error = e.what(); return false; }
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

uint64_t sendReload(const fs::path& root, int mapSlot, float x, float y, std::string& error) {
    char buf[160];
    std::snprintf(buf, sizeof buf, "cmd = \"reload\", slot = %d, x = %.3f, y = %.3f", mapSlot, x, y);
    return send(root, buf, error);
}

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
