#include "gtg.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace albion::editor {
namespace fs = std::filesystem;

namespace {
std::string readText(const fs::path& p, std::string& error) {
    std::ifstream in(p, std::ios::binary);
    if (!in) { error = "cannot open " + p.string(); return {}; }
    std::stringstream ss; ss << in.rdbuf();
    return ss.str();
}

bool writeText(const fs::path& p, const std::string& text, std::string& error) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) { error = "cannot write " + p.string(); return false; }
    out << text;
    return bool(out);
}

bool backupOnce(const fs::path& p, std::string& error) {
    const fs::path b = p.string() + ".atlas-orig";
    try { if (fs::exists(p) && !fs::exists(b)) fs::copy_file(p, b); return true; }
    catch (const std::exception& e) { error = e.what(); return false; }
}

std::string fmt(float v) {
    char b[48];
    std::snprintf(b, sizeof b, "%.6f", v);
    std::string s = b;
    // retail prints the shortest form with at least one decimal: "0.0", "33.785801"
    while (s.size() > 1 && s.back() == '0') s.pop_back();
    if (s.back() == '.') s += '0';
    return s;
}

std::string physics(const float pos[3], const float forward[2], const std::string& n) {
    const float fl = std::sqrt(forward[0] * forward[0] + forward[1] * forward[1]);
    const float fx = fl > 1e-6f ? forward[0] / fl : 0.0f, fy = fl > 1e-6f ? forward[1] / fl : 1.0f;
    return "StartCTCPhysicsStandard;" + n + "PositionX " + fmt(pos[0]) + ";" + n + "PositionY " + fmt(pos[1]) + ";" + n + "PositionZ " + fmt(pos[2]) + ";" + n +
           "RHSetForwardX " + fmt(fx) + ";" + n + "RHSetForwardY " + fmt(fy) + ";" + n + "RHSetForwardZ 0.0;" + n + "RHSetUpX 0.0;" + n + "RHSetUpY 0.0;" + n + "RHSetUpZ 1.0;" + n + "EndCTCPhysicsStandard;" + n;
}

std::string entranceBlock(uint64_t uid, const float pos[3], const float forward[2], const std::string& n) {
    return "NewThing Thing;" + n + "Player 4;" + n + "UID " + std::to_string(uid) + ";" + n + "DefinitionType \"REGION_ENTRANCE_POINT\";" + n + "ScriptName NULL;" + n + "ScriptData \"NULL\";" + n +
           "ThingGamePersistent FALSE;" + n + "ThingLevelPersistent FALSE;" + n + physics(pos, forward, n) +
           "StartCTCDRegionEntrance;" + n + "Active TRUE;" + n + "EndCTCDRegionEntrance;" + n + "EndThing;" + n + n;
}

std::string startBlock(uint64_t uid, const std::string& script, const float pos[3], const float forward[2], const std::string& n) {
    return "NewThing Holy Site;" + n + "Player 4;" + n + "UID " + std::to_string(uid) + ";" + n + "DefinitionType \"HOLY_SITE_PLAYER_START\";" + n + "ScriptName " + script + ";" + n + "ScriptData \"NULL\";" + n +
           "ThingGamePersistent FALSE;" + n + "ThingLevelPersistent FALSE;" + n + physics(pos, forward, n) + "Health 100.0;" + n + "EndThing;" + n + n;
}

// [start, end) of the thing block that contains `needle` inside `body`, or npos.
std::pair<size_t, size_t> blockAround(const std::string& body, size_t at) {
    const size_t start = body.rfind("NewThing ", at);
    if (start == std::string::npos) return {std::string::npos, std::string::npos};
    size_t end = body.find("EndThing;", at);
    if (end == std::string::npos) return {std::string::npos, std::string::npos};
    end += 9;
    while (end < body.size() && (body[end] == '\n' || body[end] == '\r')) ++end;   // its line end + the blank line after
    return {start, end};
}
}  // namespace

GtgFile GtgFile::parse(const std::string& text) {
    GtgFile f;
    f.eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    const std::string endmap = "ENDMAP" + f.eol;
    size_t pos = 0;
    while (true) {
        const size_t n = text.find("NEWMAP ", pos);
        if (n == std::string::npos) { f.tail = text.substr(pos); break; }
        const size_t eol = text.find('\n', n);
        if (eol == std::string::npos) { f.tail = text.substr(pos); break; }
        GtgSection s;
        s.slot = std::atoi(text.c_str() + n + 7);
        const size_t end = text.find(endmap, eol + 1);
        if (end == std::string::npos) { f.tail = text.substr(pos); break; }
        s.body = text.substr(eol + 1, end - (eol + 1));
        f.sections.push_back(std::move(s));
        pos = end + endmap.size();
    }
    return f;
}

std::string GtgFile::serialize() const {
    std::string out;
    for (const auto& s : sections) out += "NEWMAP " + std::to_string(s.slot) + eol + s.body + "ENDMAP" + eol;
    out += tail;
    return out;
}

const GtgSection* GtgFile::find(int slot) const {
    for (const auto& s : sections) if (s.slot == slot) return &s;
    return nullptr;
}

GtgSection& GtgFile::sectionFor(int slot) {
    auto it = std::find_if(sections.begin(), sections.end(), [&](const GtgSection& s) { return s.slot >= slot; });
    if (it != sections.end() && it->slot == slot) return *it;
    GtgSection s;
    s.slot = slot;
    s.body = "Version 2;" + eol + eol;
    return *sections.insert(it, std::move(s));
}

uint64_t GtgFile::maxUid() const {
    uint64_t best = 0;
    for (const auto& s : sections) {
        size_t p = 0;
        while ((p = s.body.find("\nUID ", p)) != std::string::npos) {
            best = std::max(best, std::strtoull(s.body.c_str() + p + 5, nullptr, 10));
            p += 5;
        }
    }
    return best;
}

std::optional<RegionEntrance> entranceOf(const fs::path& gameRoot, int slot, std::string& error) {
    const std::string text = readText(gameRoot / "data" / "Levels" / "FinalAlbion.gtg", error);
    if (text.empty()) return std::nullopt;
    const GtgFile f = GtgFile::parse(text);
    const GtgSection* s = f.find(slot);
    if (!s) return std::nullopt;
    const size_t at = s->body.find("DefinitionType \"REGION_ENTRANCE_POINT\"");
    if (at == std::string::npos) return std::nullopt;
    const auto [b0, b1] = blockAround(s->body, at);
    if (b0 == std::string::npos) return std::nullopt;
    const std::string block = s->body.substr(b0, b1 - b0);
    RegionEntrance e;
    auto num = [&](const char* key) { const size_t k = block.find(key); return k == std::string::npos ? 0.0f : float(std::atof(block.c_str() + k + std::strlen(key))); };
    e.pos[0] = num("PositionX "); e.pos[1] = num("PositionY "); e.pos[2] = num("PositionZ ");
    e.forward[0] = num("RHSetForwardX "); e.forward[1] = num("RHSetForwardY ");
    const size_t hsp = s->body.find("DefinitionType \"HOLY_SITE_PLAYER_START\"");
    if (hsp != std::string::npos) {
        const size_t sn = s->body.find("ScriptName ", hsp);
        if (sn != std::string::npos) { const size_t se = s->body.find(';', sn); e.startScript = s->body.substr(sn + 11, se - sn - 11); }
    }
    return e;
}

bool setRegionEntrance(const fs::path& gameRoot, int slot, const std::string& levelName,
                       const float pos[3], const float forward[2], std::vector<std::string>& notes, std::string& error) {
    if (slot <= 0) { error = "unknown WLD map slot"; return false; }
    const fs::path path = gameRoot / "data" / "Levels" / "FinalAlbion.gtg";
    const std::string text = readText(path, error);
    if (text.empty()) return false;
    if (!backupOnce(path, error)) return false;
    GtgFile f = GtgFile::parse(text);
    if (f.serialize() != text) { error = "FinalAlbion.gtg does not round-trip; not touching it"; return false; }
    GtgSection& s = f.sectionFor(slot);
    const std::string script = levelName + "HSP";
    uint64_t uid = f.maxUid();
    std::string things;
    // an entrance Atlas wrote before (its HSP carries our script name): replace both blocks
    const size_t mine = s.body.find("ScriptName " + script + ";");
    if (mine != std::string::npos) {
        const auto [h0, h1] = blockAround(s.body, mine);
        const size_t ent = s.body.rfind("DefinitionType \"REGION_ENTRANCE_POINT\"", h0);
        size_t e0 = std::string::npos, e1 = std::string::npos;
        if (ent != std::string::npos) { const auto be = blockAround(s.body, ent); if (be.second == h0) { e0 = be.first; e1 = be.second; } }
        const size_t cut0 = e0 != std::string::npos ? e0 : h0;
        auto uidIn = [&](size_t a, size_t b) { const size_t u = s.body.find("\nUID ", a); return u != std::string::npos && u < b ? std::strtoull(s.body.c_str() + u + 5, nullptr, 10) : 0ull; };
        const uint64_t uidE = e0 != std::string::npos ? uidIn(e0, e1) : ++uid, uidH = uidIn(h0, h1);
        things = entranceBlock(uidE, pos, forward, f.eol) + startBlock(uidH, script, pos, forward, f.eol);
        s.body.replace(cut0, h1 - cut0, things);
        notes.push_back("FinalAlbion.gtg: moved the region entrance of slot " + std::to_string(slot) + " (" + levelName + ") to (" + fmt(pos[0]) + ", " + fmt(pos[1]) + ", " + fmt(pos[2]) + ")");
    } else {
        const std::string& n = f.eol;
        things = entranceBlock(++uid, pos, forward, n) + startBlock(++uid, script, pos, forward, n);
        const size_t end = s.body.find("XXXSectionEnd;");
        if (end == std::string::npos) s.body = "Version 2;" + n + n + "XXXSectionStart NULL;" + n + n + things + "XXXSectionEnd;" + n + n + n;
        else s.body.insert(end, things);
        notes.push_back("FinalAlbion.gtg: region entrance + " + script + " added for slot " + std::to_string(slot) + " at (" + fmt(pos[0]) + ", " + fmt(pos[1]) + ", " + fmt(pos[2]) + ")");
    }
    return writeText(path, f.serialize(), error);
}

}  // namespace albion::editor
