#include "thingsexport.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>

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

    std::string err;
    const std::string text = readTng(options.gameRoot, mapName, err);
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

    // Mesh for a model id, decoded once per scene; -1 when it cannot be decoded.
    auto acquireMesh = [&](uint32_t modelId, const std::string& def) -> int {
        auto known = meshIndexById.find(modelId);
        if (known != meshIndexById.end()) return known->second;
        std::string merr;
        const auto* geo = fe::cachedMesh(modelId, merr);
        if (!geo) { meshIndexById[modelId] = -1; if (!defWarned[def]++) warn(def + ": " + merr); return -1; }
        std::vector<std::string> mw;
        fe::Mesh m = fe::makeMesh(modelId, fe::meshName(modelId), def, *geo, options.textures, context, scene.images, textureToImage, mw);
        for (const auto& w : mw) warn(w);
        scene.meshes.push_back(std::move(m));
        const int idx = int(scene.meshes.size() - 1);
        meshIndexById[modelId] = idx;
        return idx;
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
            if (verb == "CREATEPARTICLE") { ++st.childParticles; continue; }
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
            scene.meshes[size_t(meshIndex)].instanceCount++;
            scene.instances.push_back(c);
            ++st.placed;
            ++st.childPlaced;
            spawnChildren(modelId, c, depth + 1);
        }
    };

    for (const auto& thing : tng.things()) {
        ++st.things;
        const std::string type = lower(thing.type);
        if (type == "marker") { ++st.noGraphic; continue; }
        if (type == "aicreature" && !options.creatures) { ++st.skippedCreatures; continue; }
        const auto* phys = thing.findCtc("CTCPhysicsStandard");
        if (!phys) phys = thing.findCtc("CTCPhysicsNavigator");
        if (!phys) { ++st.noPosition; continue; }

        const std::string def = thing.definitionType();
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
        options.log(std::to_string(st.placed) + " placed objects from " + std::to_string(st.things) + " things (" +
                    std::to_string(scene.meshes.size()) + " meshes, " + std::to_string(scene.triangleCount()) + " triangles); skipped: " +
                    std::to_string(st.noGraphic) + " without a model, " + std::to_string(st.noDef) + " unknown defs, " +
                    std::to_string(st.noMesh) + " missing meshes, " + std::to_string(st.noPosition) + " unplaced, " +
                    std::to_string(st.skippedCreatures) + " creatures; " + std::to_string(st.childPlaced) + " of " +
                    std::to_string(st.childThings) + " mesh-dummy children (doors, windows, building parts) placed, " +
                    std::to_string(st.childParticles) + " particle dummies ignored");
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
