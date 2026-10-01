#include "effects.hpp"

#include <cstring>
#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <cwctype>
#include <stdexcept>

#include "forge/big.hpp"

namespace albion::effects {

namespace fs = std::filesystem;

namespace {

std::string upper(std::string s) { for (char& c : s) c = char(std::toupper((unsigned char)c)); return s; }

struct Bank {
    forge::big::File file;
    std::map<std::string, const forge::big::Entry*> byName;
    std::map<std::string, std::shared_ptr<const Effect>> parsed;
    size_t count = 0;
    fs::file_time_type modified{};
    uintmax_t bytes = 0;
};
std::map<fs::path,std::unique_ptr<Bank>> g_banks;
Bank* g_bank = nullptr; // legacy single-bank callers; workers use explicit roots
std::mutex g_mutex;
fs::path bankPath(const fs::path& root) {
    return fs::absolute(root / "data" / "Misc" / "pc" / "effects.big").lexically_normal();
}
fs::path bankKey(const fs::path& root) {
    auto path=bankPath(root);
#ifdef _WIN32
    auto value=path.wstring();
    for (auto& c:value) c=wchar_t(std::towlower(c));
    return fs::path(value);
#else
    return path;
#endif
}
Bank* rootBank(const fs::path& root) {
    const auto found=g_banks.find(bankKey(root));
    return found==g_banks.end()?nullptr:found->second.get();
}

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
// Newly retained field names/quantization cross-checked against EgoCore commit
// 55bdc10 (2026-09-29), EgoCore/Particles/ParticleParser.h. Forge retains its own
// checked stream reader and terminator validation; no unchecked parser is used.
// Reference attribution for adapted component layouts:
// MIT License, Copyright (c) 2026 AeoN (AlbionSecrets).
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions: The above copyright notice and this
// permission notice shall be included in all copies or substantial portions of
// the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO
// EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES
// OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

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
    auto& r=out.render;
    out.sprite = s.i32(); r.trailTexture=s.i32();
    s.colour(out.colour);
    s.colour(r.midColour); s.colour(r.endColour);
    out.blendMode = int(s.u32());
    r.trailBlendMode=s.u32(); r.blendOp=s.u32(); r.trailBlendOp=s.u32();
    r.flags=s.u32(); r.fadeInEnd=s.u32(); r.fadeOutBegin=s.u32(); r.trailLength=s.u32();
    r.flickerMinAlpha=s.u32(); r.flickerMinSize=s.u32(); r.crossedSprites=s.u32();
    out.startSize = s.q(2047, 20);
    r.alphaMinimum=s.q(127, 1);
    out.endSize = s.q(2047, 20);
    r.flickerBias=s.q(255, 2, 1);
    r.animationSecs=s.q(16383,99.9f)+0.1f;
    r.sizeMinimum=s.q(127, 1); r.trailWidth=s.q(1023, 10); r.flickerSpeed=s.q(4095, 30);
    r.useStartColour=s.ebool(); r.useMidColour=s.ebool(); r.useEndColour=s.ebool();
    r.alphaFade=s.ebool(); r.sizeFade=s.ebool(); r.flicker=s.ebool(); r.forceAnimation=s.ebool();
}

void emitterGeneric(Stream& s, SpriteSystem& out) {
    auto& e=out.emitter; e.present=true;
    e.positionParam=s.u32(); e.directionParam=s.u32(); e.startCount=s.u32(); e.startCountRandom=s.u32(); e.type=s.u32();
    e.solid=s.ebool(); e.useLife=s.ebool(); e.useTimeline=s.ebool();
    e.orientationXY=s.ebool(); e.orientationXZ=s.ebool(); e.orientationYZ=s.ebool();
    e.useCustomDirection=s.ebool(); e.useOutwardDirection=s.ebool(); e.useParamDirection=s.ebool(); e.oppositeDirection=s.ebool();
    e.angularPerturbation=s.u32();
    auto direction=[&] { return float(s.u32()&255)/255.f*2-1; };
    e.customDirection[0]=direction();
    out.perSecond = s.q(16383, 100);
    e.size=s.q(1023, 10); e.radialBias=s.q(1023, 10, 5); e.minSpeed=s.q(1023, 10);
    e.useForwardDirection=s.ebool(); e.useRandom2DDirection=s.ebool();
    e.customDirection[1]=direction(); e.customDirection[2]=direction();
    e.timelineSecs=s.q(32767, 30); e.useRandom3DDirection=s.ebool();
    e.lifeSecs=s.q(32767, 300); e.startTime=s.q(32767, 300); e.maxSpeed=s.q(1023, 10);
    for (float& value:e.nonUniformScale) value=s.q(2047, 20, 10);
    e.hasSpline=s.ebool();
    if (e.hasSpline) { s.f32(); const uint32_t n = s.u32(); for (uint32_t i = 0; i < n; ++i) s.skipVec3(); }
}

void updateNormal(Stream& s, SpriteSystem& out) {
    auto& u=out.update; u.present=true;
    u.fadeInEnd=s.u32(); u.fadeOutBegin=s.u32();
    u.useParticleLife=s.ebool(); u.randomRotationAxis=s.ebool(); u.randomInitialRotation=s.ebool(); u.useAccelerationParam=s.ebool();
    u.stayWithEmitter=s.ebool(); u.useSystemLife=s.ebool(); u.systemAlphaFade=s.ebool(); u.emissionFade=s.ebool();
    u.collideGround=s.ebool(); u.collideAnything=s.ebool(); u.dieOnCollision=s.ebool(); u.useAttractors=s.ebool();
    u.randomisePosition=s.ebool(); u.randomiseDistance=s.ebool(); u.setOrientationDirection=s.ebool(); u.setOrientationGame=s.ebool();
    u.createDecal=s.ebool(); u.createDecalEmitter=s.ebool();
    u.decalEmitter=s.cstr();
    u.systemLife=s.f32();
    out.lifeSecs = s.f32();                   // ParticleLifeSecs
    u.wind=s.f32(); u.gravity=s.f32(); u.drag=s.f32(); u.accelerationScale=s.f32();
    s.vec3(u.initialRotation); u.rotationMinSpeed=s.f32(); u.rotationMaxSpeed=s.f32(); u.bounce=s.f32();
    u.systemAlphaMinimum=s.f32(); u.randomiseDistanceScale=s.f32(); u.emissionMinimum=s.f32();
    s.vec3(out.offset);                       // ParticleSystemOffset
    s.vec3(u.acceleration); s.vec3(u.rotationAxis); s.vec3(u.randomiseSpeed); s.vec3(u.randomiseScale);
    u.accelerationParam=s.u32(); u.orientationParam=s.u32();
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
    auto& r=out.render;
    out.sprite = s.i32();
    s.colour(out.colour);
    s.colour(r.midColour); s.colour(r.endColour);
    r.animationSecs=s.f32();
    out.startSize = s.f32(); out.endSize = s.f32();
    r.alignment=s.i32(); r.crossedSprites=uint32_t(s.i32()); r.initialAngle=s.f32();
    r.fadeInEnd=uint32_t(s.i32()); r.fadeOutBegin=uint32_t(s.i32());
    r.alphaMinimum=s.f32(); r.sizeMinimum=s.f32(); r.positionParam=s.u32();
    out.blendMode = s.i32();
    r.blendOp=uint32_t(s.i32()); r.trailBlendMode=uint32_t(s.i32()); r.trailBlendOp=uint32_t(s.i32());
    r.trailTexture=s.i32(); r.trailWidth=s.f32(); r.trailLength=uint32_t(s.i32()); r.stayWithEmitterFactor=s.i32();
    r.useStartColour=s.ebool(); r.useMidColour=s.ebool(); r.useEndColour=s.ebool();
    r.selfIlluminating=s.ebool(); r.alphaFade=s.ebool(); r.rotateAroundCentre=s.ebool();
    r.faceMe2D=s.ebool(); r.faceMe3D=s.ebool(); r.crossed=s.ebool();
    r.sizeFade=s.ebool(); r.forceAnimation=s.ebool(); r.stayWithEmitter=s.ebool();
    r.usePosition=s.ebool(); r.useSplinePoints=s.ebool();
    splineBase(s);
    out.single = true;
}

void orbit(Stream& s, SpriteSystem& out) {
    OrbitParams value; value.centreParam=s.u32();
    for (auto& axis:value.axes) {
        axis.type=s.i32(); axis.enabled=s.u32()!=0;
        axis.radius=s.f32(); axis.expand=s.f32(); axis.cycle=s.u32()!=0;
        axis.cycleTime=s.f32(); axis.squeezeScale=s.f32(); axis.squeezeAngle=s.f32();
        axis.rotateSpeed=s.f32(); axis.rotateStart=s.f32();
        axis.rotateSpeedRandom=s.f32(); axis.rotateStartRandom=s.f32();
    }
    out.orbits.push_back(std::move(value));
}

void attractor(Stream& s, SpriteSystem& out) {
    AttractorParams value;
    value.enabled=s.ebool(); value.useParamPosition=s.ebool(); value.falloff=s.i32();
    value.positionParam=s.u32(); value.positionParamName=s.u32();
    value.radius=s.f32(); value.force=s.f32();
    const uint32_t n = s.u32();
    // Bound allocation by the remaining checked payload, including disabled components.
    if (n>(s.b.size()-s.p)/12) throw std::runtime_error("attractor points truncated");
    value.points.resize(n);
    for (auto& point:value.points) s.vec3(point.data());
    out.attractors.push_back(std::move(value));
}

void light(Stream& s, LightSystem& out) {
    out.positionParam=s.u32();
    out.lifeSecs=s.f32();out.respawnDelaySecs=s.f32();out.startTime=s.f32();out.timelineSecs=s.f32();
    out.radius=s.f32();out.endRadius=s.f32();
    out.attenuationFactor=s.i32();out.fadeInEnd=s.i32();out.fadeOutBegin=s.i32();
    out.radiusMinimum=s.f32();s.colour(out.colourMinimum);
    out.radiusFade=s.ebool();out.useLife=s.ebool();out.enabled=s.ebool();out.respawns=s.ebool();
    out.colourFade=s.ebool();out.useStartColour=s.ebool();out.useMidColour=s.ebool();out.useEndColour=s.ebool();
    out.useFadeColour=s.ebool();out.useTimeline=s.ebool();out.hasInitializedTime=s.ebool();
    s.colour(out.colour);s.colour(out.midColour);s.colour(out.endColour);
}

void renderMesh(Stream& s, MeshSystem& out) {
    auto& r=out.render;
    out.mesh = s.i32(); r.trailTexture=s.i32();
    s.colour(out.trailStartColour);s.colour(out.trailMidColour);s.colour(out.trailEndColour);
    s.colour(out.colour);s.colour(r.midColour);s.colour(r.endColour);
    out.blendMode=int(s.u32());r.trailBlendMode=s.u32();r.blendOp=s.u32();r.trailBlendOp=s.u32();
    r.fadeInEnd=s.u32();r.fadeOutBegin=s.u32();r.trailLength=s.u32();r.flickerMinAlpha=s.u32();r.flickerMinSize=s.u32();
    for (int k = 0; k < 3; ++k) out.size[k] = s.q(2047, 20);
    out.centredOnPosition=s.ebool();r.alphaFade=s.ebool();
    for(float& size:out.endSize)size=s.q(2047,20);
    r.sizeFade=s.ebool();r.flicker=s.ebool();out.trailUseStartColour=s.ebool();out.trailUseMidColour=s.ebool();out.trailUseEndColour=s.ebool();out.useRenderSizeParam=s.ebool();
    r.flickerBias=s.q(255,2,1);r.flickerSpeed=s.q(4095,30);r.alphaMinimum=s.q(127,1);
    r.useStartColour=s.ebool();r.sizeMinimum=s.q(127,1);r.useMidColour=s.ebool();r.trailWidth=s.q(1023,10);r.useEndColour=s.ebool();out.renderSizeParam=s.u32();
}

std::unique_ptr<Effect> parse(const forge::big::Entry& e, const std::vector<uint8_t>& buf) {
    auto fx = std::make_unique<Effect>();
    fx->id = e.id; fx->name = e.name;
    Stream s(buf);
    try {
        s.u32();                                  // magic 0x64
        fx->displayName = s.cstr();
        fx->emitter2D=s.ebool(); fx->preWater=s.ebool(); fx->water=s.ebool(); fx->zWrite=s.ebool();
        fx->continuous=s.ebool(); fx->screenDisplacement=s.ebool(); fx->readZ=s.ebool();
        fx->maxSpawnDistance=s.f32(); fx->maxDrawDistance=s.f32(); fx->fadeOutStart=s.f32(); fx->fadeInEnd=s.f32(); fx->fadeInStart=s.f32();
        fx->priority=s.i32();
        fx->dieOffscreen=s.ebool(); fx->offscreenUpdate=s.ebool(); fx->weatherMask=s.ebool(); fx->dithering=s.ebool(); fx->boundsOnce=s.ebool();
        const uint32_t systems = s.u32();
        if (systems > 64) throw std::runtime_error("implausible system count");
        fx->systems = int(systems);
        for (uint32_t si = 0; si < systems; ++si) {
            const std::string sysName = s.cstr();
            const bool systemEnabled = s.ebool();
            const bool scaleParticles = s.ebool();
            float scale[3]; s.vec3(scale);
            const uint32_t components = s.u32();
            if (components > 64) throw std::runtime_error("implausible component count");
            SpriteSystem sprite; sprite.system = sysName; bool hasSprite = false;
            sprite.systemIndex=si;
            const size_t meshBegin=fx->meshes.size();
            sprite.scaleParticles=scaleParticles;
            std::copy(std::begin(scale),std::end(scale),sprite.systemScale);
            for (uint32_t ci = 0; ci < components; ++ci) {
                const std::string cls = s.cstr();
                s.u32(); const bool enabled = s.ebool() && systemEnabled;
                // Disabled payloads still occupy the stream, but cannot overwrite
                // the active renderer/update state or contribute visible proxies.
                SpriteSystem candidate = sprite;
                if (enabled && cls!="CPSCRenderSprite" && cls!="CPSCUpdateNormal" && cls!="CPSCEmitterGeneric" && cls!="CPSCSingleSprite" && cls!="CPSCOrbit" && cls!="CPSCAttractor" && cls!="CPSCRenderMesh" && cls!="CPSCLight") {
                    fx->previewUnsupported.push_back(sysName+": "+cls);
                    if (cls!="CPSCRenderMesh" && cls!="CPSCLight" && cls!="CPSCDecalRenderer")
                        candidate.previewUnsupported.push_back(cls);
                }
                if (cls == "CPSCRenderSprite") { renderSprite(s, candidate); if (enabled) hasSprite = true; }
                else if (cls == "CPSCUpdateNormal") updateNormal(s, candidate);
                else if (cls == "CPSCEmitterGeneric") emitterGeneric(s, candidate);
                else if (cls == "CPSCSpline") spline(s);
                else if (cls == "CPSCSingleSprite") { singleSprite(s, candidate); if (enabled) hasSprite = true; }
                else if (cls == "CPSCRenderMesh") { MeshSystem m; m.system = sysName; renderMesh(s, m); if (enabled) fx->meshes.push_back(m); }
                else if (cls == "CPSCLight") { LightSystem l; l.system = sysName;l.systemIndex=si;l.scaleParticles=scaleParticles;std::copy(std::begin(scale),std::end(scale),l.systemScale); light(s, l); if (enabled) fx->lights.push_back(l); }
                else if (cls == "CPSCAttractor") attractor(s, candidate);
                else if (cls == "CPSCOrbit") orbit(s, candidate);
                else if (cls == "CPSCDecalRenderer") decalRenderer(s);
                else throw std::runtime_error("unknown particle component " + cls);
                s.terminator(0x7B);
                if (enabled) sprite = std::move(candidate);
            }
            s.terminator(0x26);
            for(size_t mi=meshBegin;mi<fx->meshes.size();++mi) {
                fx->meshes[mi].systemIndex=si;
                fx->meshes[mi].config=sprite;
            }
            if (hasSprite && sprite.sprite >= 0) {
                sprite.authoredStartSize=sprite.startSize;
                sprite.authoredEndSize=sprite.endSize;
                sprite.hasAuthoredSizes=true;
                if (scaleParticles) { sprite.startSize *= scale[0]; sprite.endSize *= scale[0]; }
                fx->sprites.push_back(sprite);
            }
        }
        fx->parsedFully = true;
    } catch (const std::exception& error) {
        // keep what was decoded before the walk failed
        fx->previewUnsupported.push_back(std::string("Incomplete decode: ")+error.what());
    }
    return fx;
}

} // namespace

Effect decode(const std::vector<uint8_t>& payload) {
    return *parse(forge::big::Entry{}, payload);
}

bool openBank(const fs::path& gameRoot, std::string& err, bool reload) {
    std::lock_guard<std::mutex> lock(g_mutex);
    try {
        const auto p=bankPath(gameRoot), key=bankKey(gameRoot);
        if (!fs::exists(p)) { err="no effects.big under data/Misc/pc"; return false; }
        const auto modified=fs::last_write_time(p);
        const auto bytes=fs::file_size(p);
        const auto found=g_banks.find(key);
        if (!reload && found!=g_banks.end() && found->second->modified==modified && found->second->bytes==bytes) {
            g_bank=found->second.get(); err.clear(); return true;
        }
        auto bank = std::make_unique<Bank>();
        // A retained snapshot must not lazily reread changed bytes from disk.
        bank->file = forge::big::File::openFully(p);
        bank->modified=modified; bank->bytes=bytes;
        for (const auto& b : bank->file.banks())
            for (const auto& e : b.entries) { bank->byName[upper(e.name)] = &e; ++bank->count; }
        g_bank=bank.get(); g_banks[key]=std::move(bank); err.clear();
        return true;
    } catch (const std::exception& e) {
        err = std::string("effects.big: ") + e.what();
        return false;
    }
}

bool bankOpen() { std::lock_guard<std::mutex> lock(g_mutex); return g_bank != nullptr; }
bool bankOpen(const fs::path& root) { std::lock_guard<std::mutex> lock(g_mutex); return rootBank(root)!=nullptr; }
size_t entryCount() { std::lock_guard<std::mutex> lock(g_mutex); return g_bank ? g_bank->count : 0; }

std::vector<std::string> entryNames() {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<std::string> v;
    if (!g_bank) return v;
    for (const auto& [name, e] : g_bank->byName) v.push_back(name);
    return v;   // a std::map: already sorted
}
std::vector<std::string> entryNames(const fs::path& root) {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<std::string> names;
    if (const auto* bank=rootBank(root)) for (const auto& [name,entry]:bank->byName) names.push_back(name);
    return names;
}

namespace {
std::shared_ptr<const Effect> lookup(Bank* bank,const std::string& name) {
    if (!bank) return nullptr;
    const std::string key = upper(name);
    auto hit = bank->parsed.find(key);
    if (hit != bank->parsed.end()) return hit->second;
    auto it = bank->byName.find(key);
    if (it == bank->byName.end()) { bank->parsed[key] = nullptr; return nullptr; }
    const auto data = bank->file.entryData(*it->second);
    std::shared_ptr<const Effect> fx = parse(*it->second, data);
    bank->parsed[key] = fx;
    return fx;
}
}
std::shared_ptr<const Effect> byName(const std::string& name) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return lookup(g_bank,name);
}
std::shared_ptr<const Effect> byName(const fs::path& root,const std::string& name) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return lookup(rootBank(root),name);
}

} // namespace albion::effects
