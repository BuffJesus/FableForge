#include "thingsexport.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>

#include "effects.hpp"
#include "forge/tng.hpp"
#include "forge/wad.hpp"

namespace albion::thingsexport {

namespace fs = std::filesystem;
namespace te = albion::terrainexport;
namespace fe = albion::foliageexport;

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}

std::string unquote(std::string v) {
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
    return v;
}

// Loose file first, then the WAD copy.
std::string readTng(const fs::path& root, const std::string& mapName, std::string& err) {
    const fs::path loose = root / "data" / "Levels" / "FinalAlbion" / (mapName + ".tng");
    if (fs::exists(loose)) {
        std::ifstream f(loose, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
    try {
        const auto wad = forge::wad::Archive::open(root / "data" / "Levels" / "FinalAlbion.wad");
        const std::string want = lower(mapName) + ".tng";
        for (const auto& e : wad.entries())
            if (lower(fs::path(e.name).filename().string()) == want) {
                const auto bytes = wad.read(e);
                return std::string(bytes.begin(), bytes.end());
            }
        err = "no " + mapName + ".tng loose or in FinalAlbion.wad";
    } catch (const std::exception& e) {
        err = e.what();
    }
    return {};
}

float propF(const forge::tng::CtcBlock& b, const char* key, float fallback) {
    for (const auto& p : b.properties)
        if (lower(p.key) == lower(key)) return float(std::atof(p.value.c_str()));
    return fallback;
}

void normalize(float v[3]) {
    const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-9f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

} // namespace

void thingBasis(const float forward[3], const float up[3], float scale, float m[9]) {
    float f[3] = {forward[0], forward[1], forward[2]};
    float u[3] = {up[0], up[1], up[2]};
    normalize(f); normalize(u);
    if (f[0] * f[0] + f[1] * f[1] + f[2] * f[2] < 0.5f) { f[0] = 1; f[1] = 0; f[2] = 0; }
    if (u[0] * u[0] + u[1] * u[1] + u[2] * u[2] < 0.5f) { u[0] = 0; u[1] = 0; u[2] = 1; }
    float r[3] = {f[1] * u[2] - f[2] * u[1], f[2] * u[0] - f[0] * u[2], f[0] * u[1] - f[1] * u[0]};
    normalize(r);
    // Straight from the engine: CEngineInternalPrimitiveMeshBase::CalcObjectMatrix
    // (retail Fable.exe 0x00bebaa0, FableWin.exe 0x02ee3a00) builds the row-vector
    // CMatrix3x4 as rows { -(forward x up), -forward, up, pos } * scale, so
    //   world = pos + lx*(-right) + ly*(-forward) + lz*up.
    // Cross-checked on the Arena: the audience billboards' front face is local -y,
    // and only this frame turns 31/34 of them towards the pit (the sign-flipped
    // guess faced them away; the earlier -lx*forward + ly*right guess was a
    // 90-degree yaw off and turned Oakvale's fence lines into combs).
    for (int k = 0; k < 3; ++k) { m[0 * 3 + k] = -r[k] * scale; m[1 * 3 + k] = -f[k] * scale; m[2 * 3 + k] = u[k] * scale; }
}

fe::Scene load(const std::string& mapName, const Options& options, const te::Context& context, Stats* statsOut) {
    fe::Scene scene;
    scene.mapName = mapName;
    scene.rootName = "Things";
    Stats st;
    auto warn = [&](const std::string& m) { scene.warnings.push_back(m); if (options.log) options.log("warning: " + m); };
    const auto clock0 = std::chrono::steady_clock::now();
    double geoSeconds = 0, texSeconds = 0;
    auto ms = [&]() { return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - clock0).count()) + " ms"; };

    std::string err;
    const std::string text = options.tngText.empty() ? readTng(options.gameRoot, mapName, err) : options.tngText;
    if (text.empty()) { warn(err.empty() ? "empty .tng" : err); if (statsOut) *statsOut = st; return scene; }
    forge::tng::File tng;
    try { tng = forge::tng::File::parseText(text, mapName + ".tng"); }
    catch (const std::exception& e) { warn(std::string("cannot parse .tng: ") + e.what()); if (statsOut) *statsOut = st; return scene; }
    scene.found = true;

    fs::path graphics = options.gameRoot / "data" / "graphics" / "graphics.big";
    if (!fs::exists(graphics)) graphics = options.gameRoot / "data" / "graphics" / "pc" / "graphics.big";
    if (!fe::openMeshBank(graphics, err)) { warn("graphics.big: " + err); if (statsOut) *statsOut = st; return scene; }

    std::map<uint32_t, int> meshIndexById;
    std::map<uint32_t, int> textureToImage;
    std::map<std::string, int> defWarned;
    if (options.particles) {
        std::string ferr;
        if (!effects::openBank(options.gameRoot, ferr)) warn("particle effects unavailable: " + ferr);
    }
    // Mesh for a model id, decoded once per scene; -1 when it cannot be decoded.
    auto acquireMesh = [&](uint32_t modelId, const std::string& def) -> int {
        auto known = meshIndexById.find(modelId);
        if (known != meshIndexById.end()) return known->second;
        std::string merr;
        const auto g0 = std::chrono::steady_clock::now();
        const auto* geo = fe::cachedMesh(modelId, merr);
        geoSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - g0).count();
        if (!geo) { meshIndexById[modelId] = -1; if (!defWarned[def]++) warn(def + ": " + merr); return -1; }
        std::vector<std::string> mw;
        const auto t0 = std::chrono::steady_clock::now();
        fe::Mesh m = fe::makeMesh(modelId, fe::meshName(modelId), def, *geo, options.textures, context, scene.images, textureToImage, mw);
        texSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        for (const auto& w : mw) warn(w);
        scene.meshes.push_back(std::move(m));
        const int idx = int(scene.meshes.size() - 1);
        meshIndexById[modelId] = idx;
        return idx;
    };

    // Proxy meshes for particle sprite systems, keyed by (sprite texture, tint).
    std::map<std::string, int> proxyMeshes;
    auto proxyMesh = [&](const effects::SpriteSystem& sp, const std::string& fxName) -> int {
        char key[64];
        std::snprintf(key, sizeof key, "%d:%02x%02x%02x:%d", sp.sprite, sp.colour[0], sp.colour[1], sp.colour[2], sp.blendMode == 3 ? 1 : 0);
        auto hit = proxyMeshes.find(key);
        if (hit != proxyMeshes.end()) return hit->second;
        int imageIndex = -1;
        if (options.textures && sp.sprite > 0) {
            std::string twarn;
            const te::Image* tex = context.texture(uint32_t(sp.sprite), twarn);
            if (tex) {
                te::Image tinted = *tex;
                tinted.name = std::string("fx_sprite_") + std::to_string(sp.sprite);
                // Additive sprites (blend 3, the fire/glow family) are authored on
                // black with no useful alpha: their brightness IS their coverage.
                const bool additive = sp.blendMode == 3;
                for (size_t i = 0; i + 3 < tinted.rgba.size(); i += 4) {
                    const int lum = std::max({int(tinted.rgba[i]), int(tinted.rgba[i + 1]), int(tinted.rgba[i + 2])});
                    for (int k = 0; k < 3; ++k) tinted.rgba[i + k] = uint8_t(std::min(255, int(tinted.rgba[i + k]) * int(sp.colour[k]) / 255));
                    if (additive) tinted.rgba[i + 3] = uint8_t(std::min(255, lum * 2));
                }
                scene.images.push_back(std::move(tinted));
                imageIndex = int(scene.images.size() - 1);
            }
        }
        // Two crossed vertical quads, 1 x 1 units, base at the origin, in Fable
        // space (z up); the instance scales them to the effect's render size.
        fe::Mesh m;
        m.meshId = 0;
        m.name = "FX_" + fxName + "_" + sp.system;
        m.label = "particle proxy";
        auto& g = m.geometry;
        const float hw = 0.5f;
        auto quad = [&](float ax, float ay, float bx, float by) {
            const uint32_t base = uint32_t(g.vertices.size());
            const float nx = -(by - ay), ny = bx - ax;
            g.vertices.push_back({ax, ay, 0, nx, ny, 0, 0, 1});
            g.vertices.push_back({bx, by, 0, nx, ny, 0, 1, 1});
            g.vertices.push_back({bx, by, 1, nx, ny, 0, 1, 0});
            g.vertices.push_back({ax, ay, 1, nx, ny, 0, 0, 0});
            g.triangles.push_back({base, base + 1, base + 2, 0});
            g.triangles.push_back({base, base + 2, base + 3, 0});
        };
        quad(-hw, 0, hw, 0);
        quad(0, -hw, 0, hw);
        forge::meshpreview::Material mat; mat.id = 0; mat.diffuseTexture = sp.sprite;
        g.materials.push_back(mat);
        fe::SubMesh part;
        part.material = 0; part.diffuseTexture = sp.sprite > 0 ? uint32_t(sp.sprite) : 0; part.image = imageIndex; part.hasAlpha = true;
        for (const auto& t : g.triangles) { part.indices.push_back(t.a); part.indices.push_back(t.b); part.indices.push_back(t.c); }
        m.parts.push_back(std::move(part));
        m.diffuseTexture = m.parts[0].diffuseTexture; m.image = imageIndex; m.hasAlpha = true;
        scene.meshes.push_back(std::move(m));
        const int idx = int(scene.meshes.size() - 1);
        proxyMeshes[key] = idx;
        return idx;
    };
    // One emitter: proxies for its sprite systems, lights for its CPSCLight components.
    auto placeParticle = [&](const std::string& fxName, float x, float y, float z) {
        ++st.particles;
        if (!options.particles) return;
        const effects::Effect* fx = effects::byName(fxName);
        if (!fx) { ++st.particlesUnknown; if (!defWarned["fx:" + fxName]++) warn("particle effect " + fxName + " is not in effects.big"); return; }
        bool any = false;
        for (const auto& sp : fx->sprites) {
            const float size = std::max(std::max(sp.startSize, sp.endSize) * 2.0f, 0.1f);
            const int meshIndex = proxyMesh(sp, fx->name);
            fe::Instance c;
            c.mesh = meshIndex; c.type = -1; c.prim = 0; c.hasMatrix = true;
            c.x = x + sp.offset[0]; c.y = y + sp.offset[1]; c.z = z + sp.offset[2];
            for (int k = 0; k < 9; ++k) c.m[k] = 0;
            c.m[0] = size; c.m[4] = size; c.m[8] = size * 1.5f;   // flames are taller than wide
            c.scale = size; c.yaw = 0;
            c.tag = fx->name;
            scene.meshes[size_t(meshIndex)].instanceCount++;
            scene.instances.push_back(c);
            any = true;
        }
        for (const auto& ms : fx->meshes) {
            // CPSCRenderMesh: the effect draws a bank mesh (sun beams, dust cones,
            // water sheets). Placed once, scaled so its largest extent equals the
            // system's render size -- a static stand-in for an animated system.
            if (ms.mesh <= 0) continue;
            const int meshIndex = acquireMesh(uint32_t(ms.mesh), fx->name);
            if (meshIndex < 0) continue;
            const auto& g = scene.meshes[size_t(meshIndex)].geometry;
            float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
            for (const auto& v : g.vertices) { lo[0] = std::min(lo[0], v.x); hi[0] = std::max(hi[0], v.x); lo[1] = std::min(lo[1], v.y); hi[1] = std::max(hi[1], v.y); lo[2] = std::min(lo[2], v.z); hi[2] = std::max(hi[2], v.z); }
            const float extent = std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2], 1e-3f});
            const float k = std::max(std::max({ms.size[0], ms.size[1], ms.size[2]}), 0.01f) / extent;
            fe::Instance c;
            c.mesh = meshIndex; c.type = -1; c.prim = 0; c.hasMatrix = true;
            c.x = x; c.y = y; c.z = z;
            for (int i = 0; i < 9; ++i) c.m[i] = 0;
            c.m[0] = k; c.m[4] = k; c.m[8] = k;
            c.scale = k; c.yaw = 0; c.tag = fx->name;
            scene.meshes[size_t(meshIndex)].instanceCount++;
            scene.instances.push_back(c);
            any = true;
        }
        for (const auto& l : fx->lights) {
            fe::Light L;
            L.x = x; L.y = y; L.z = z;
            L.r = l.colour[0] / 255.0f; L.g = l.colour[1] / 255.0f; L.b = l.colour[2] / 255.0f;
            L.radius = l.radius; L.name = fx->name + "_light";
            scene.lights.push_back(L);
            ++st.particleLights;
            any = true;
        }
        if (any) ++st.particlesPlaced;
    };

    // Meshes carry 3ds-Max dummy objects whose NAME is an instruction. The
    // engine's CTCMeshAutomaticEntityCreator::CreateChildThings (FableWin 0x02570910)
    // walks every dummy of a thing's mesh and, for "CREATEOBJECT <def>" /
    // "CREATEBUILDING <def>", spawns a child thing of that def at the dummy's
    // transform ("CREATEPARTICLE <fx>" spawns an effect). That is how doors,
    // windows, weathervanes, the Arena's entrances / stand sections and chained
    // interiors (Hall of Heroes, Hobbe cave throne room...) get into the world:
    // none of them are in the .tng. Child = dummy matrix (mesh-local, cm) composed
    // with the parent's world transform; children may carry dummies of their own.
    std::function<void(uint32_t, const fe::Instance&, int)> spawnChildren;
    spawnChildren = [&](uint32_t parentModel, const fe::Instance& parent, int depth) {
        if (depth > 4) return;
        std::string merr;
        const auto* geo = fe::cachedMesh(parentModel, merr);
        if (!geo) return;
        for (const auto& h : geo->helpers) {
            if (h.name.empty()) continue;
            std::string verb, arg;
            {
                size_t i = 0;
                while (i < h.name.size() && !std::isspace((unsigned char)h.name[i])) verb += h.name[i++];
                while (i < h.name.size() && std::isspace((unsigned char)h.name[i])) ++i;
                while (i < h.name.size() && !std::isspace((unsigned char)h.name[i])) arg += h.name[i++];
            }
            if (verb == "CREATEPARTICLE") {
                ++st.childParticles;
                if (arg.empty()) continue;
                const float* d = h.matrix;
                placeParticle(arg,
                              parent.x + d[9] * parent.m[0] + d[10] * parent.m[3] + d[11] * parent.m[6],
                              parent.y + d[9] * parent.m[1] + d[10] * parent.m[4] + d[11] * parent.m[7],
                              parent.z + d[9] * parent.m[2] + d[10] * parent.m[5] + d[11] * parent.m[8]);
                continue;
            }
            if (verb != "CREATEOBJECT" && verb != "CREATEBUILDING") continue;
            if (arg.empty()) continue;
            ++st.childThings;
            uint32_t modelId = 0;
            const int code = context.graphicModelId(arg, modelId);
            if (code == 0) { if (!defWarned[arg]++) warn(arg + " (child of a mesh dummy): not in game.bin"); ++st.noDef; continue; }
            if (code < 0 || modelId == 0) { ++st.noGraphic; continue; }
            const int meshIndex = acquireMesh(modelId, arg);
            if (meshIndex < 0) { ++st.noMesh; continue; }
            // Dummy transform: rows 0..2 = local axes, row 3 = translation (cm),
            // row-vector convention like every CMatrix3x4 -> child = dummy * parent.
            const float* d = h.matrix;
            fe::Instance c;
            c.mesh = meshIndex; c.type = -1; c.prim = 0; c.hasMatrix = true;
            for (int r = 0; r < 3; ++r)
                for (int k = 0; k < 3; ++k)
                    c.m[r * 3 + k] = d[r * 3 + 0] * parent.m[0 * 3 + k] + d[r * 3 + 1] * parent.m[1 * 3 + k] + d[r * 3 + 2] * parent.m[2 * 3 + k];
            c.x = parent.x + d[9] * parent.m[0] + d[10] * parent.m[3] + d[11] * parent.m[6];
            c.y = parent.y + d[9] * parent.m[1] + d[10] * parent.m[4] + d[11] * parent.m[7];
            c.z = parent.z + d[9] * parent.m[2] + d[10] * parent.m[5] + d[11] * parent.m[8];
            c.scale = parent.scale;
            c.yaw = std::atan2(c.m[4], c.m[3]);
            c.tag = "child:" + arg;
            c.thing = parent.thing;
            scene.meshes[size_t(meshIndex)].instanceCount++;
            scene.instances.push_back(c);
            ++st.placed;
            ++st.childPlaced;
            spawnChildren(modelId, c, depth + 1);
        }
    };

    for (size_t thingIndex = 0; thingIndex < tng.things().size(); ++thingIndex) {
        const auto& thing = tng.things()[thingIndex];
        ++st.things;
        const std::string type = lower(thing.type);
        if (type == "marker") { ++st.noGraphic; continue; }
        if (type == "aicreature" && !options.creatures) { ++st.skippedCreatures; continue; }
        const auto* phys = thing.findCtc("CTCPhysicsStandard");
        if (!phys) phys = thing.findCtc("CTCPhysicsNavigator");
        if (!phys) { ++st.noPosition; continue; }

        const std::string def = thing.definitionType();
        if (const auto* pe = thing.findCtc("CTCDParticleEmitter")) {
            std::string fxName;
            for (const auto& p : pe->properties) if (lower(p.key) == "particletypename") fxName = unquote(p.value);
            if (!fxName.empty() && fxName != "NULL") {
                placeParticle(fxName, propF(*phys, "PositionX", 0.0f) + options.originX,
                              propF(*phys, "PositionY", 0.0f) + options.originY, propF(*phys, "PositionZ", 0.0f));
                continue;
            }
        }
        uint32_t modelId = 0;
        std::string overrideName;
        if (const auto ov = thing.find("GraphicOverride")) {
            overrideName = unquote(*ov);
            if (overrideName == "NULL") overrideName.clear();
        }
        if (!overrideName.empty()) modelId = fe::meshIdByName(overrideName);
        if (modelId == 0) {
            const int code = context.graphicModelId(def, modelId);
            if (code == 0) { ++st.noDef; if (!defWarned[def]++) warn(def + ": not in game.bin"); continue; }
            if (code < 0) { ++st.noGraphic; continue; }   // a def type without a Graphic field (cameras, emitters, exits)
            if (modelId == 0) { ++st.noGraphic; continue; }
        }

        const int meshIndex = acquireMesh(modelId, def);
        if (meshIndex < 0) { ++st.noMesh; continue; }

        float objectScale = 1.0f;
        if (const auto sc = thing.find("ObjectScale")) objectScale = float(std::atof(sc->c_str()));
        if (!std::isfinite(objectScale) || objectScale <= 0.0f) objectScale = 1.0f;
        const float s = 0.01f * objectScale;   // mesh centimetres -> world units

        fe::Instance inst;
        inst.mesh = meshIndex;
        inst.type = -1;
        inst.prim = 0;
        inst.x = propF(*phys, "PositionX", 0.0f) + options.originX;
        inst.y = propF(*phys, "PositionY", 0.0f) + options.originY;
        inst.z = propF(*phys, "PositionZ", 0.0f);
        const float fwd[3] = {propF(*phys, "RHSetForwardX", 1.0f), propF(*phys, "RHSetForwardY", 0.0f), propF(*phys, "RHSetForwardZ", 0.0f)};
        const float up[3] = {propF(*phys, "RHSetUpX", 0.0f), propF(*phys, "RHSetUpY", 0.0f), propF(*phys, "RHSetUpZ", 1.0f)};
        inst.hasMatrix = true;
        inst.thing = int(thingIndex);
        thingBasis(fwd, up, s, inst.m);
        inst.scale = s;
        inst.yaw = std::atan2(inst.m[4], inst.m[3]);
        scene.meshes[size_t(meshIndex)].instanceCount++;
        scene.instances.push_back(inst);
        ++st.placed;
        spawnChildren(modelId, inst, 0);
    }
    scene.treeInstances = st.placed;
    if (options.log) {
        size_t hull = 0; int hullMeshes = 0;
        for (const auto& m : scene.meshes) if (m.hullTriangles) { hull += m.hullTriangles; ++hullMeshes; }
        if (hull) options.log("  " + std::to_string(hull) + " collision-hull triangles on texture-less materials dropped from " + std::to_string(hullMeshes) + " meshes" +
                              (std::getenv("ALBION_DEBUG") ? "" : " (ALBION_DEBUG=1 lists them)"));
        if (std::getenv("ALBION_DEBUG"))
            for (const auto& m : scene.meshes) if (m.hullTriangles) {
                size_t kept = 0; for (const auto& part : m.parts) kept += part.indices.size() / 3;
                options.log("    " + m.name + ": dropped " + std::to_string(m.hullTriangles) + ", kept " + std::to_string(kept));
            }
        { char tb[96]; std::snprintf(tb, sizeof tb, "  timing: mesh decode %.0f ms, mesh build + textures %.0f ms, total %s", geoSeconds * 1000, texSeconds * 1000, ms().c_str()); options.log(tb); }
        options.log(std::to_string(st.placed) + " placed objects from " + std::to_string(st.things) + " things (" +
                    std::to_string(scene.meshes.size()) + " meshes, " + std::to_string(scene.triangleCount()) + " triangles); skipped: " +
                    std::to_string(st.noGraphic) + " without a model, " + std::to_string(st.noDef) + " unknown defs, " +
                    std::to_string(st.noMesh) + " missing meshes, " + std::to_string(st.noPosition) + " unplaced, " +
                    std::to_string(st.skippedCreatures) + " creatures; " + std::to_string(st.childPlaced) + " of " +
                    std::to_string(st.childThings) + " mesh-dummy children (doors, windows, building parts) placed, " +
                    (options.particles ? std::to_string(st.particlesPlaced) + " of " + std::to_string(st.particles) + " particle emitters proxied (" +
                                             std::to_string(st.particleLights) + " lights, " + std::to_string(st.particlesUnknown) + " unknown effects)"
                                       : std::to_string(st.particles) + " particle emitters skipped (--particles)"));
        std::vector<const fe::Mesh*> byCount;
        for (const auto& m : scene.meshes) byCount.push_back(&m);
        std::sort(byCount.begin(), byCount.end(), [](const fe::Mesh* a, const fe::Mesh* b) { return a->instanceCount > b->instanceCount; });
        for (size_t i = 0; i < byCount.size() && i < 12; ++i)
            options.log("  " + std::to_string(byCount[i]->instanceCount) + " x " + byCount[i]->label + " -> " + byCount[i]->name);
        if (byCount.size() > 12) options.log("  ... " + std::to_string(byCount.size() - 12) + " more meshes");
    }
    if (statsOut) *statsOut = st;
    return scene;
}

} // namespace albion::thingsexport
