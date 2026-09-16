#include "leveledit.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <stdexcept>

#include "forge/wad.hpp"

namespace fs = std::filesystem;

namespace albion::editor {

namespace {

constexpr size_t kUndoDepth = 128;

std::string lower(std::string s) { for (auto& c : s) c = char(std::tolower(uint8_t(c))); return s; }

std::string unquote(std::string v) {
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
    return v;
}

float propF(const forge::tng::CtcBlock& b, const char* key, float fallback) {
    for (const auto& p : b.properties)
        if (lower(p.key) == lower(key)) return float(std::atof(p.value.c_str()));
    return fallback;
}

const forge::tng::CtcBlock* physicsOf(const forge::tng::Thing& t) {
    const auto* phys = t.findCtc("CTCPhysicsStandard");
    if (!phys) phys = t.findCtc("CTCPhysicsNavigator");
    return phys;
}

bool parseUid(const std::string& raw, uint64_t& out) {
    const std::string s = unquote(raw);
    if (s.empty()) return false;
    char* end = nullptr;
    out = std::strtoull(s.c_str(), &end, 10);
    return end && *end == 0;
}

void normalise3(float v[3]) {
    const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-12f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

std::string readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

} // namespace

std::string formatFloat(float v) { return forge::thingplacer::formatFloat(v); }

void frameToMatrix(const Frame& f, float m[16]) {
    float fw[3] = {f.forward[0], f.forward[1], f.forward[2]};
    float up[3] = {f.up[0], f.up[1], f.up[2]};
    normalise3(fw); normalise3(up);
    if (fw[0] * fw[0] + fw[1] * fw[1] + fw[2] * fw[2] < 0.5f) { fw[0] = 1; fw[1] = 0; fw[2] = 0; }
    if (up[0] * up[0] + up[1] * up[1] + up[2] * up[2] < 0.5f) { up[0] = 0; up[1] = 0; up[2] = 1; }
    float r[3] = {fw[1] * up[2] - fw[2] * up[1], fw[2] * up[0] - fw[0] * up[2], fw[0] * up[1] - fw[1] * up[0]};
    normalise3(r);
    const float s = 0.01f * (f.scale > 0 ? f.scale : 1.0f);
    // CalcObjectMatrix rows: { -(forward x up), -forward, up, pos } * scale
    m[0] = -r[0] * s;  m[1] = -r[1] * s;  m[2] = -r[2] * s;  m[3] = 0;
    m[4] = -fw[0] * s; m[5] = -fw[1] * s; m[6] = -fw[2] * s; m[7] = 0;
    m[8] = up[0] * s;  m[9] = up[1] * s;  m[10] = up[2] * s; m[11] = 0;
    m[12] = f.pos[0];  m[13] = f.pos[1];  m[14] = f.pos[2];  m[15] = 1;
}

bool matrixToFrame(const float m[16], Frame& f) {
    const float l1 = std::sqrt(m[4] * m[4] + m[5] * m[5] + m[6] * m[6]);
    const float l2 = std::sqrt(m[8] * m[8] + m[9] * m[9] + m[10] * m[10]);
    const float l0 = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
    if (l1 < 1e-9f || l2 < 1e-9f || l0 < 1e-9f) return false;
    f.forward[0] = -m[4] / l1; f.forward[1] = -m[5] / l1; f.forward[2] = -m[6] / l1;
    f.up[0] = m[8] / l2; f.up[1] = m[9] / l2; f.up[2] = m[10] / l2;
    f.pos[0] = m[12]; f.pos[1] = m[13]; f.pos[2] = m[14];
    f.scale = ((l0 + l1 + l2) / 3.0f) / 0.01f;
    return true;
}

void multiply(const float a[16], const float b[16], float out[16]) {
    float r[16];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[i * 4 + k] * b[k * 4 + j];
            r[i * 4 + j] = s;
        }
    std::memcpy(out, r, sizeof r);
}

bool invert(const float m[16], float out[16]) {
    // general 4x4 inverse (cofactors); matrices here are affine but this keeps it simple
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (std::fabs(det) < 1e-20f) return false;
    for (int i = 0; i < 16; ++i) out[i] = inv[i] / det;
    return true;
}

// ---------------------------------------------------------------- Document

bool Document::open(const fs::path& gameRoot, const std::string& mapName, const fs::path& levPath, std::string& error) {
    std::string text;
    fromWad_ = false;
    loosePath_ = gameRoot / "data" / "Levels" / "FinalAlbion" / (mapName + ".tng");
    if (fs::exists(loosePath_)) {
        text = readFile(loosePath_);
    } else {
        try {
            const auto wad = forge::wad::Archive::open(gameRoot / "data" / "Levels" / "FinalAlbion.wad");
            const std::string want = lower(mapName) + ".tng";
            for (const auto& e : wad.entries())
                if (lower(fs::path(e.name).filename().string()) == want) {
                    const auto bytes = wad.read(e);
                    text.assign(bytes.begin(), bytes.end());
                    fromWad_ = true;
                    break;
                }
        } catch (const std::exception& e) { error = e.what(); return false; }
        if (text.empty()) { error = "no " + mapName + ".tng loose or in FinalAlbion.wad"; return false; }
    }
    if (!openText(mapName, std::move(text), error)) return false;
    if (!levPath.empty()) {
        try { level_ = std::make_shared<forge::lev::File>(forge::lev::File::open(levPath)); }
        catch (const std::exception& e) { level_.reset(); error = std::string("level heights unavailable: ") + e.what(); }
    }
    return true;
}

bool Document::openText(const std::string& mapName, std::string tngText, std::string& error) {
    try { file_ = forge::tng::File::parseText(std::move(tngText), mapName + ".tng"); }
    catch (const std::exception& e) { error = std::string("cannot parse .tng: ") + e.what(); return false; }
    mapName_ = mapName;
    original_ = file_.serialize();
    undo_.clear(); redo_.clear();
    ++revision_;
    return true;
}

ThingSummary Document::summary(size_t index) const {
    ThingSummary s;
    s.index = index;
    if (index >= file_.things().size()) return s;
    const auto& t = file_.things()[index];
    s.type = t.type;
    s.definition = t.definitionType();
    s.scriptName = t.scriptName();
    if (s.scriptName == "NULL") s.scriptName.clear();
    s.uid = uidOf(index);
    s.hasFrame = physicsOf(t) != nullptr;
    return s;
}

uint64_t Document::uidOf(size_t index) const {
    if (index >= file_.things().size()) return 0;
    uint64_t uid = 0;
    if (const auto raw = file_.things()[index].find("UID")) parseUid(*raw, uid);
    return uid;
}

std::optional<size_t> Document::indexOfUid(uint64_t uid) const {
    for (size_t i = 0; i < file_.things().size(); ++i)
        if (uidOf(i) == uid) return i;
    return std::nullopt;
}

bool Document::frameOf(size_t index, Frame& out) const {
    if (index >= file_.things().size()) return false;
    const auto& t = file_.things()[index];
    const auto* phys = physicsOf(t);
    if (!phys) return false;
    out.pos[0] = propF(*phys, "PositionX", 0.0f);
    out.pos[1] = propF(*phys, "PositionY", 0.0f);
    out.pos[2] = propF(*phys, "PositionZ", 0.0f);
    out.forward[0] = propF(*phys, "RHSetForwardX", 1.0f);
    out.forward[1] = propF(*phys, "RHSetForwardY", 0.0f);
    out.forward[2] = propF(*phys, "RHSetForwardZ", 0.0f);
    out.up[0] = propF(*phys, "RHSetUpX", 0.0f);
    out.up[1] = propF(*phys, "RHSetUpY", 0.0f);
    out.up[2] = propF(*phys, "RHSetUpZ", 1.0f);
    out.scale = 1.0f;
    if (const auto sc = t.find("ObjectScale")) {
        const float v = float(std::atof(sc->c_str()));
        if (std::isfinite(v) && v > 0) out.scale = v;
    }
    return true;
}

std::optional<float> Document::groundHeight(float x, float y) const {
    if (!level_) return std::nullopt;
    try { return forge::thingplacer::terrainHeightAt(*level_, x, y); }
    catch (...) { return std::nullopt; }
}

void Document::pushUndo() {
    undo_.push_back(file_.serialize());
    if (undo_.size() > kUndoDepth) undo_.erase(undo_.begin());
    redo_.clear();
}

void Document::restore(const std::string& text) {
    file_ = forge::tng::File::parseText(text, mapName_ + ".tng");
    ++revision_;
}

void Document::setFrame(size_t index, const Frame& frame) {
    if (index >= file_.things().size()) throw std::out_of_range("setFrame: bad thing index");
    const auto& t = file_.things()[index];
    const auto* phys = physicsOf(t);
    if (!phys) throw std::runtime_error("setFrame: thing has no physics block");
    const std::string ctc = phys->name;
    pushUndo();
    file_.setCtcProperty(index, ctc, "PositionX", formatFloat(frame.pos[0]));
    file_.setCtcProperty(index, ctc, "PositionY", formatFloat(frame.pos[1]));
    file_.setCtcProperty(index, ctc, "PositionZ", formatFloat(frame.pos[2]));
    file_.setCtcProperty(index, ctc, "RHSetForwardX", formatFloat(frame.forward[0]));
    file_.setCtcProperty(index, ctc, "RHSetForwardY", formatFloat(frame.forward[1]));
    file_.setCtcProperty(index, ctc, "RHSetForwardZ", formatFloat(frame.forward[2]));
    file_.setCtcProperty(index, ctc, "RHSetUpX", formatFloat(frame.up[0]));
    file_.setCtcProperty(index, ctc, "RHSetUpY", formatFloat(frame.up[1]));
    file_.setCtcProperty(index, ctc, "RHSetUpZ", formatFloat(frame.up[2]));
    const bool hasScale = file_.things()[index].find("ObjectScale").has_value();
    if (std::fabs(frame.scale - 1.0f) > 1e-6f) file_.setThingProperty(index, "ObjectScale", formatFloat(frame.scale));
    else if (hasScale) file_.removeThingProperty(index, "ObjectScale");
    ++revision_;
}

void Document::setProperty(size_t index, const std::string& key, const std::string& value) {
    if (index >= file_.things().size()) throw std::out_of_range("setProperty: bad thing index");
    pushUndo();
    file_.setThingProperty(index, key, value);
    ++revision_;
}

size_t Document::duplicate(size_t index) {
    if (index >= file_.things().size()) throw std::out_of_range("duplicate: bad thing index");
    pushUndo();
    std::string block = file_.thingBlockText(index);
    const size_t newIndex = file_.insertThingBlockBefore(index + 1, block);
    file_.setThingProperty(newIndex, "UID", std::to_string(forge::thingplacer::nextUid(file_)));
    if (file_.things()[newIndex].find("ScriptName")) file_.setThingProperty(newIndex, "ScriptName", "NULL");
    ++revision_;
    return newIndex;
}

size_t Document::place(forge::thingplacer::Placement placement) {
    pushUndo();
    try {
        const auto r = forge::thingplacer::place(file_, std::move(placement));
        ++revision_;
        return r.thingIndex;
    } catch (...) {
        restore(undo_.back()); undo_.pop_back();
        throw;
    }
}

void Document::remove(size_t index) {
    if (index >= file_.things().size()) throw std::out_of_range("remove: bad thing index");
    pushUndo();
    file_.removeThing(index);
    ++revision_;
}

bool Document::undo() {
    if (undo_.empty()) return false;
    redo_.push_back(file_.serialize());
    restore(undo_.back());
    undo_.pop_back();
    return true;
}

bool Document::redo() {
    if (redo_.empty()) return false;
    undo_.push_back(file_.serialize());
    restore(redo_.back());
    redo_.pop_back();
    return true;
}

bool Document::dirty() const {
    if (dirtyRev_ != revision_) { dirtyValue_ = file_.serialize() != original_; dirtyRev_ = revision_; }
    return dirtyValue_;
}

std::vector<std::string> Document::changes() const {
    std::vector<std::string> out;
    forge::tng::File before;
    try { before = forge::tng::File::parseText(original_, mapName_ + ".tng"); } catch (...) { return out; }
    auto label = [](const forge::tng::Thing& t, uint64_t uid) {
        std::string s = t.definitionType();
        const std::string sn = t.scriptName();
        if (!sn.empty() && sn != "NULL") s += " " + sn;
        return s + " (uid " + std::to_string(uid) + ")";
    };
    std::map<uint64_t, const forge::tng::Thing*> was, now;
    auto uidOfThing = [](const forge::tng::Thing& t) { uint64_t u = 0; if (const auto r = t.find("UID")) parseUid(*r, u); return u; };
    for (const auto& t : before.things()) was[uidOfThing(t)] = &t;
    for (const auto& t : file_.things()) now[uidOfThing(t)] = &t;
    for (const auto& [uid, t] : was)
        if (!now.count(uid)) out.push_back("removed " + label(*t, uid));
    for (const auto& [uid, t] : now) {
        auto hit = was.find(uid);
        if (hit == was.end()) { out.push_back("added " + label(*t, uid)); continue; }
        const auto* a = physicsOf(*hit->second);
        const auto* b = physicsOf(*t);
        bool moved = false;
        if (a && b) {
            static const char* keys[] = {"PositionX", "PositionY", "PositionZ", "RHSetForwardX", "RHSetForwardY", "RHSetForwardZ", "RHSetUpX", "RHSetUpY", "RHSetUpZ"};
            for (const char* k : keys) if (std::fabs(propF(*a, k, 0) - propF(*b, k, 0)) > 1e-6f) { moved = true; break; }
        }
        // any other line difference
        std::string ta, tb;
        for (const auto& p : hit->second->properties) ta += p.key + "=" + p.value + ";";
        for (const auto& p : t->properties) tb += p.key + "=" + p.value + ";";
        for (const auto& c : hit->second->ctcBlocks) { ta += c.name + "{"; for (const auto& p : c.properties) ta += p.key + "=" + p.value + ";"; ta += "}"; }
        for (const auto& c : t->ctcBlocks) { tb += c.name + "{"; for (const auto& p : c.properties) tb += p.key + "=" + p.value + ";"; tb += "}"; }
        if (moved) out.push_back("moved " + label(*t, uid));
        else if (ta != tb) out.push_back("changed " + label(*t, uid));
    }
    return out;
}

bool Document::saveLoose(const fs::path& gameRoot, std::string& error) {
    const fs::path path = gameRoot / "data" / "Levels" / "FinalAlbion" / (mapName_ + ".tng");
    try {
        fs::create_directories(path.parent_path());
        const std::string text = file_.serialize();
        if (fs::exists(path) && !fs::exists(path.string() + ".atlas-orig")) fs::copy_file(path, path.string() + ".atlas-orig");
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) { error = "cannot write " + path.string(); return false; }
        f.write(text.data(), std::streamsize(text.size()));
        loosePath_ = path;
        original_ = text; dirtyRev_ = ~0ull;
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

bool Document::deployWad(const fs::path& gameRoot, std::string& error) {
    const fs::path wad = gameRoot / "data" / "Levels" / "FinalAlbion.wad";
    const fs::path backup = wad.string() + ".atlas-orig";
    const fs::path temp = wad.string() + ".atlas-tmp";
    try {
        if (!fs::exists(wad)) { error = "no " + wad.string(); return false; }
        const auto archive = forge::wad::Archive::open(wad);
        std::string entryName;
        const std::string want = lower(mapName_) + ".tng";
        for (const auto& e : archive.entries())
            if (lower(fs::path(e.name).filename().string()) == want) { entryName = e.name; break; }
        if (entryName.empty()) { error = mapName_ + ".tng is not in FinalAlbion.wad"; return false; }
        if (!fs::exists(backup)) fs::copy_file(wad, backup);
        const std::string text = file_.serialize();
        std::map<std::string, std::vector<uint8_t>> rep;
        rep[entryName] = std::vector<uint8_t>(text.begin(), text.end());
        forge::wad::repack(wad, rep, temp);
        fs::rename(temp, wad);
        original_ = text; dirtyRev_ = ~0ull;
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        std::error_code ec; fs::remove(temp, ec);
        return false;
    }
}

} // namespace albion::editor
