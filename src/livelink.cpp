#include "backups.hpp"
#include "livelink.hpp"
#include "pendingbanks.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <unordered_map>

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
    try {
        const uint64_t id = nextId();
        const std::string chunk = "return {id = " + std::to_string(id) + ", " + body + "}\n";
        detail::PendingBanks pending(root, ".forge-link-command-");
        if (!writeAll(pending.prepare("FSE/AtlasLink/cmd.lua"), chunk, error)) return 0;
        return pending.install(false, error) ? id : 0;
    } catch (const std::exception& e) { error = e.what(); return 0; }
}

std::string luaString(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        if (c < 32 || c == 127) {
            // Three digits keep a following decimal digit out of this escape (Lua 5.0).
            out += '\\';
            out += char('0' + c / 100);
            out += char('0' + (c / 10) % 10);
            out += char('0' + c % 10);
        } else {
            if (c == '"' || c == '\\') out += '\\';
            out += char(c);
        }
    }
    return out + "\"";
}

bool coordinates(float x, float y, std::string& result, std::string& error) {
    if (!std::isfinite(x) || !std::isfinite(y)) {
        error = "live-link coordinates must be finite";
        return false;
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(3) << ", x = " << x << ", y = " << y;
    result = out.str();
    return true;
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
    std::string position;
    if (!coordinates(x, y, position, error)) return 0;
    return send(root, "cmd = \"teleport\", slot = " + std::to_string(mapSlot) + ", map = " + luaString(mapName) + position, error);
}

uint64_t sendSpawn(const fs::path& root, const std::string& definition, float x, float y, const std::string& scriptName, std::string& error) {
    std::string position;
    if (!coordinates(x, y, position, error)) return 0;
    return send(root, "cmd = \"spawn\", def = " + luaString(definition) + position + ", name = " +
                luaString(scriptName.empty() ? "AtlasSpawn" : scriptName), error);
}

uint64_t sendPing(const fs::path& root, std::string& error) { return send(root, "cmd = \"ping\"", error); }

uint64_t sendReload(const fs::path& root, int mapSlot, float x, float y, std::string& error) {
    std::string position;
    if (!coordinates(x, y, position, error)) return 0;
    return send(root, "cmd = \"reload\", slot = " + std::to_string(mapSlot) + position, error);
}

Status poll(const fs::path& root) {
    struct Observation {
        std::string beat;
        std::chrono::steady_clock::time_point seenAt;
        double initialAge = 0;
    };
    static std::unordered_map<std::string, Observation> observations;
    Status s;
    const fs::path log = logPath(root);
    std::error_code ec;
    if (!fs::exists(log, ec)) return s;
    s.logSeen = true;
    const auto modified = fs::last_write_time(log, ec);
    if (ec) return s;
    auto key = fs::absolute(log, ec).lexically_normal().generic_string();
    if (ec) return s;
#ifdef _WIN32
    for (auto& c : key) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
#endif
    std::ifstream in(log, std::ios::binary);
    if (!in) return s;
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0) return s;
    const std::streamoff from = size > 65536 ? size - 65536 : 0;
    in.seekg(from);
    std::string tail((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) return s;
    // A concurrently written final line and the first clipped tail line are incomplete.
    if (from) {
        const auto newline = tail.find('\n');
        if (newline == std::string::npos) return s;
        tail.erase(0, newline + 1);
    }
    const auto lastNewline = tail.rfind('\n');
    if (lastNewline == std::string::npos) return s;
    tail.resize(lastNewline + 1);
    std::istringstream lines(tail);
    std::string line, beat;
    const auto number = [](const std::string& text, auto& result) {
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
        return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
    };
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t at = line.find("ATLAS_LINK|");
        if (at == std::string::npos) continue;
        const std::string msg = line.substr(at + 11);
        s.recent.push_back(msg);
        if (s.recent.size() > 8) s.recent.erase(s.recent.begin());
        if (msg == "ready") s.ready = true;
        else if (msg.rfind("hero|", 0) == 0) {
            std::vector<std::string> f; std::string cur;
            for (char c : msg) { if (c == '|') { f.push_back(cur); cur.clear(); } else cur += c; }
            f.push_back(cur);
            uint64_t sequence = 0;
            float x = 0, y = 0, z = 0;
            if (f.size() != 6 || !number(f[1], sequence) || !sequence || f[2].empty() ||
                !number(f[3], x) || !number(f[4], y) || !number(f[5], z) ||
                !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) continue;
            beat = msg;
            s.heroMap = f[2]; s.heroX = x; s.heroY = y; s.heroZ = z;
        } else if (msg.rfind("ack|", 0) == 0) {
            const auto idEnd = msg.find('|', 4);
            if (idEnd == std::string::npos) continue;
            const auto okEnd = msg.find('|', idEnd + 1);
            if (okEnd == std::string::npos) continue;
            uint64_t id = 0;
            const auto ok = msg.substr(idEnd + 1, okEnd - idEnd - 1);
            if (!number(msg.substr(4, idEnd - 4), id) || !id || (ok != "true" && ok != "false")) continue;
            s.lastAckId = id; s.lastAckOk = ok == "true"; s.lastAckMessage = msg.substr(okEnd + 1);
        }
    }
    if (!beat.empty()) {
        const auto now = std::chrono::steady_clock::now();
        const auto fileAge = std::max(0.0, std::chrono::duration<double>(fs::file_time_type::clock::now() - modified).count());
        auto& observation = observations[key];
        if (beat != observation.beat) observation = {beat, now, fileAge};
        s.heartbeatAge = std::max(fileAge, observation.initialAge + std::chrono::duration<double>(now - observation.seenAt).count());
    }
    return s;
}

} // namespace albion::livelink
