#include "effects.hpp"

#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>

#include "forge/big.hpp"

namespace albion::effects {

namespace fs = std::filesystem;

namespace {

std::string upper(std::string s) { for (char& c : s) c = char(std::toupper((unsigned char)c)); return s; }

struct Bank {
    forge::big::File file;
    std::map<std::string, const forge::big::Entry*> byName;
    std::map<std::string, std::unique_ptr<Effect>> parsed;
    size_t count = 0;
};
std::unique_ptr<Bank> g_bank;
std::mutex g_mutex;

struct Stream {
    const std::vector<uint8_t>& b;
    size_t p = 0;
    explicit Stream(const std::vector<uint8_t>& buf) : b(buf) {}
    void need(size_t n) const { if (p + n > b.size()) throw std::runtime_error("effect payload truncated"); }
    uint32_t u32() { need(4); uint32_t v; std::memcpy(&v, b.data() + p, 4); p += 4; return v; }
    int32_t i32() { return int32_t(u32()); }
    float f32() { need(4); float v; std::memcpy(&v, b.data() + p, 4); p += 4; return v; }
    bool ebool() { need(1); return b[p++] != 0; }
    uint8_t byte() { need(1); return b[p++]; }
    void colour(uint8_t out[4]) { need(4); out[2] = b[p]; out[1] = b[p + 1]; out[0] = b[p + 2]; out[3] = b[p + 3]; p += 4; }   // stored B,G,R,A
    void vec3(float out[3]) { out[0] = f32(); out[1] = f32(); out[2] = f32(); }
    void skipVec3() { float t[3]; vec3(t); }
    std::string cstr() {
        size_t e = p;
        while (e < b.size() && b[e]) ++e;
        if (e >= b.size()) throw std::runtime_error("effect string unterminated");
        std::string s(reinterpret_cast<const char*>(b.data() + p), e - p);
        p = e + 1;
        return s;
    }
    float q(float maxq, float scale, float bias = 0.0f) { return float(u32()) / maxq * scale - bias; }
    void terminator(uint8_t expect) { if (byte() != expect) throw std::runtime_error("effect terminator mismatch"); }
};

// ---- component grammars (parse_effects.py / EgoCore ParticleParser.h) ----

void splineBase(Stream& s) {
    const uint32_t n = s.u32();
    for (uint32_t i = 0; i < n; ++i) s.skipVec3();
    if (s.ebool()) {
        s.ebool(); s.ebool();
        const uint32_t m = s.u32();
        for (uint32_t i = 0; i < m; ++i) { s.skipVec3(); s.skipVec3(); }
    }
    if (s.ebool()) {
        s.ebool();
        const uint32_t m = s.u32();
        for (uint32_t i = 0; i < m; ++i) s.u32();
    }
}

void renderSprite(Stream& s, SpriteSystem& out) {
    out.sprite = s.i32(); s.i32();
    s.colour(out.colour);
    uint8_t mid[4], end[4]; s.colour(mid); s.colour(end);
    out.blendMode = int(s.u32());
    for (int k = 0; k < 10; ++k) s.u32();   // TrailBlendMode .. NoCrossedSprites
    out.startSize = s.q(2047, 20);
    s.q(127, 1);
    out.endSize = s.q(2047, 20);
    s.q(255, 2, 1);
    s.u32();            // AnimationTimeSecs raw
    s.q(127, 1); s.q(1023, 10); s.q(4095, 30);
    for (int k = 0; k < 7; ++k) s.ebool();
}

void emitterGeneric(Stream& s, SpriteSystem& out) {
    for (int k = 0; k < 5; ++k) s.u32();
    for (int k = 0; k < 10; ++k) s.ebool();
    s.u32();
    s.u32();                                  // custom dir x
    out.perSecond = s.q(16383, 100);
    s.q(1023, 10); s.q(1023, 10, 5); s.q(1023, 10);
    s.ebool(); s.ebool();
    s.u32(); s.u32();                         // custom dir y, z
    s.q(32767, 30);
    s.ebool();
    s.q(32767, 300); s.q(32767, 300); s.q(1023, 10);
    for (int k = 0; k < 3; ++k) s.q(2047, 20, 10);
    if (s.ebool()) { s.f32(); const uint32_t n = s.u32(); for (uint32_t i = 0; i < n; ++i) s.skipVec3(); }
}

void updateNormal(Stream& s, SpriteSystem& out) {
    s.u32(); s.u32();
    for (int k = 0; k < 18; ++k) s.ebool();
    s.cstr();                                 // DecalEmitterName
    s.f32();                                  // SystemLifeSecs
    out.lifeSecs = s.f32();                   // ParticleLifeSecs
    for (int k = 0; k < 13; ++k) s.f32();
    s.vec3(out.offset);                       // ParticleSystemOffset
    for (int k = 0; k < 4; ++k) s.skipVec3();
    s.u32(); s.u32();
}

void decalRenderer(Stream& s) {
    s.f32(); s.i32();
    uint8_t c[4]; s.colour(c); s.colour(c); s.colour(c);
    for (int k = 0; k < 10; ++k) s.u32();
    for (int k = 0; k < 6; ++k) s.ebool();
    for (int k = 0; k < 4; ++k) s.u32();
    for (int k = 0; k < 5; ++k) s.ebool();
    s.f32(); s.f32(); s.f32();
    s.ebool();
    s.f32(); s.f32();
}

void spline(Stream& s) { s.ebool(); s.ebool(); s.f32(); splineBase(s); }

void singleSprite(Stream& s, SpriteSystem& out) {
    out.sprite = s.i32();
    s.colour(out.colour);
    uint8_t c[4]; s.colour(c); s.colour(c);
    s.f32();                                  // AnimationTimeSecs
    out.startSize = s.f32(); out.endSize = s.f32();
    s.i32(); s.i32(); s.f32(); s.i32(); s.i32(); s.f32(); s.f32(); s.u32();
    out.blendMode = s.i32();
    s.i32(); s.i32(); s.i32(); s.i32(); s.f32(); s.i32(); s.i32();
    for (int k = 0; k < 14; ++k) s.ebool();
    splineBase(s);
    out.single = true;
}

void orbit(Stream& s) {
    s.u32();
    for (int o = 0; o < 3; ++o) { s.i32(); s.u32(); s.f32(); s.f32(); s.u32(); for (int k = 0; k < 7; ++k) s.f32(); }
}

void attractor(Stream& s) {
    s.ebool(); s.ebool(); s.i32(); s.u32(); s.u32(); s.f32(); s.f32();
    const uint32_t n = s.u32();
    for (uint32_t i = 0; i < n; ++i) s.skipVec3();
}

void light(Stream& s, LightSystem& out) {
    s.u32();
    s.f32(); s.f32(); s.f32(); s.f32();
    out.radius = s.f32(); s.f32();           // start / end world radius
    s.i32(); s.i32(); s.i32();
    s.f32();
    uint8_t c[4]; s.colour(c);
    for (int k = 0; k < 11; ++k) s.ebool();
    s.colour(out.colour);                     // LightStartColour
    s.colour(c); s.colour(c);
}

void renderMesh(Stream& s, MeshSystem& out) {
    out.mesh = s.i32(); s.i32();
    uint8_t c[4]; s.colour(c); s.colour(c); s.colour(c);
    s.colour(out.colour); s.colour(c); s.colour(c);
    for (int k = 0; k < 9; ++k) s.u32();
    for (int k = 0; k < 3; ++k) out.size[k] = s.q(2047, 20);
    s.ebool(); s.ebool();
    for (int k = 0; k < 3; ++k) s.q(2047, 20);
    for (int k = 0; k < 6; ++k) s.ebool();
    s.q(255, 2, 1); s.q(4095, 30); s.q(127, 1);
    s.ebool(); s.q(127, 1); s.ebool(); s.q(1023, 10); s.ebool(); s.u32();
}

std::unique_ptr<Effect> parse(const forge::big::Entry& e, const std::vector<uint8_t>& buf) {
    auto fx = std::make_unique<Effect>();
    fx->id = e.id; fx->name = e.name;
    Stream s(buf);
    try {
        s.u32();                                  // magic 0x64
        fx->displayName = s.cstr();
        for (int k = 0; k < 7; ++k) s.ebool();
        for (int k = 0; k < 5; ++k) s.f32();
        s.i32();
        for (int k = 0; k < 5; ++k) s.ebool();
        const uint32_t systems = s.u32();
        if (systems > 64) throw std::runtime_error("implausible system count");
        fx->systems = int(systems);
        for (uint32_t si = 0; si < systems; ++si) {
            const std::string sysName = s.cstr();
            s.ebool();
            const bool scaleParticles = s.ebool();
            float scale[3]; s.vec3(scale);
            const uint32_t components = s.u32();
            if (components > 64) throw std::runtime_error("implausible component count");
            SpriteSystem sprite; sprite.system = sysName; bool hasSprite = false;
            for (uint32_t ci = 0; ci < components; ++ci) {
                const std::string cls = s.cstr();
                s.u32(); s.ebool();
                if (cls == "CPSCRenderSprite") { renderSprite(s, sprite); hasSprite = true; }
                else if (cls == "CPSCUpdateNormal") updateNormal(s, sprite);
                else if (cls == "CPSCEmitterGeneric") emitterGeneric(s, sprite);
                else if (cls == "CPSCSpline") spline(s);
                else if (cls == "CPSCSingleSprite") { singleSprite(s, sprite); hasSprite = true; }
                else if (cls == "CPSCRenderMesh") { MeshSystem m; m.system = sysName; renderMesh(s, m); fx->meshes.push_back(m); }
                else if (cls == "CPSCLight") { LightSystem l; l.system = sysName; light(s, l); fx->lights.push_back(l); }
                else if (cls == "CPSCAttractor") attractor(s);
                else if (cls == "CPSCOrbit") orbit(s);
                else if (cls == "CPSCDecalRenderer") decalRenderer(s);
                else throw std::runtime_error("unknown particle component " + cls);
                s.terminator(0x7B);
            }
            s.terminator(0x26);
            if (hasSprite && sprite.sprite >= 0) {
                if (scaleParticles) { sprite.startSize *= scale[0]; sprite.endSize *= scale[0]; }
                fx->sprites.push_back(sprite);
            }
        }
        fx->parsedFully = true;
    } catch (const std::exception&) {
        // keep what was decoded before the walk failed
    }
    return fx;
}

} // namespace

bool openBank(const fs::path& gameRoot, std::string& err) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_bank) return true;
    fs::path p = gameRoot / "data" / "Misc" / "pc" / "effects.big";
    if (!fs::exists(p)) { err = "no effects.big under data/Misc/pc"; return false; }
    try {
        auto bank = std::make_unique<Bank>();
        bank->file = forge::big::File::open(p);
        for (const auto& b : bank->file.banks())
            for (const auto& e : b.entries) { bank->byName[upper(e.name)] = &e; ++bank->count; }
        g_bank = std::move(bank);
        return true;
    } catch (const std::exception& e) {
        err = std::string("effects.big: ") + e.what();
        return false;
    }
}

bool bankOpen() { std::lock_guard<std::mutex> lock(g_mutex); return g_bank != nullptr; }
size_t entryCount() { std::lock_guard<std::mutex> lock(g_mutex); return g_bank ? g_bank->count : 0; }

const Effect* byName(const std::string& name) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_bank) return nullptr;
    const std::string key = upper(name);
    auto hit = g_bank->parsed.find(key);
    if (hit != g_bank->parsed.end()) return hit->second.get();
    auto it = g_bank->byName.find(key);
    if (it == g_bank->byName.end()) { g_bank->parsed[key] = nullptr; return nullptr; }
    const auto data = g_bank->file.entryData(*it->second);
    auto fx = parse(*it->second, data);
    const Effect* raw = fx.get();
    g_bank->parsed[key] = std::move(fx);
    return raw;
}

} // namespace albion::effects
