#include "thingsexport.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
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
    // Mesh-local frame: +x = right, +y = forward, +z = up, so
    // world = pos + lx*right + ly*forward + lz*up   (rows = images of local axes).
    // Verified on the Arena: the oval pit's long axis and its two N/S entrance
    // corridors only line up with the audience ring, the gate things and
    // MINIMAP_ARENA this way (the previous -lx*forward + ly*right guess was a
    // 90-degree yaw off, which turned Oakvale's fence lines into combs).
    for (int k = 0; k < 3; ++k) { m[0 * 3 + k] = r[k] * scale; m[1 * 3 + k] = f[k] * scale; m[2 * 3 + k] = u[k] * scale; }
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

        int meshIndex = -1;
        auto known = meshIndexById.find(modelId);
        if (known != meshIndexById.end()) meshIndex = known->second;
        else {
            std::string merr;
            const auto* geo = fe::cachedMesh(modelId, merr);
            if (!geo) { meshIndexById[modelId] = -1; ++st.noMesh; if (!defWarned[def]++) warn(def + ": " + merr); continue; }
            std::vector<std::string> mw;
            fe::Mesh m = fe::makeMesh(modelId, fe::meshName(modelId), def, *geo, options.textures, context, scene.images, textureToImage, mw);
            for (const auto& w : mw) warn(w);
            scene.meshes.push_back(std::move(m));
            meshIndex = int(scene.meshes.size() - 1);
            meshIndexById[modelId] = meshIndex;
        }
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
    }
    scene.treeInstances = st.placed;
    if (options.log) {
        options.log(std::to_string(st.placed) + " placed objects from " + std::to_string(st.things) + " things (" +
                    std::to_string(scene.meshes.size()) + " meshes, " + std::to_string(scene.triangleCount()) + " triangles); skipped: " +
                    std::to_string(st.noGraphic) + " without a model, " + std::to_string(st.noDef) + " unknown defs, " +
                    std::to_string(st.noMesh) + " missing meshes, " + std::to_string(st.noPosition) + " unplaced, " +
                    std::to_string(st.skippedCreatures) + " creatures");
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
