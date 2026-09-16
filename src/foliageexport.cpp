#include "foliageexport.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <mutex>

#include "forge/big.hpp"
#include "forge/foliage.hpp"
#include "forge/stb.hpp"
#include "forge/stbinfo.hpp"
#include "glbwriter.hpp"
#include "terrainexport_internal.hpp"

namespace albion::foliageexport {

namespace fs = std::filesystem;
namespace te = albion::terrainexport;
using json = nlohmann::json;

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}

void warn(const Options& o, Scene& s, const std::string& msg) {
    s.warnings.push_back(msg);
    if (o.log) o.log("warning: " + msg);
}

// LOD0 geometry cache across maps (graphics.big is 100+ MB; decoding is cheap
// but the BIG index is not, so keep the file open per process).
struct MeshCache {
    std::mutex mutex;
    fs::path path;
    std::unique_ptr<forge::big::File> big;
    std::map<uint32_t, const forge::big::Entry*> byId;
    std::map<uint32_t, forge::meshpreview::Geometry> decoded;
    std::map<uint32_t, std::string> names;

    bool open(const fs::path& graphics, std::string& err) {
        std::lock_guard<std::mutex> lock(mutex);
        if (big && path == graphics) return true;
        try {
            auto f = std::make_unique<forge::big::File>(forge::big::File::open(graphics));
            const auto* bank = f->findBank("MBANK_ALLMESHES");
            if (!bank) { err = "no MBANK_ALLMESHES bank in " + graphics.string(); return false; }
            byId.clear(); decoded.clear(); names.clear();
            for (const auto& e : bank->entries) { byId[e.id] = &e; names[e.id] = e.name; }
            big = std::move(f);
            path = graphics;
            return true;
        } catch (const std::exception& e) {
            err = e.what();
            return false;
        }
    }

    const forge::meshpreview::Geometry* get(uint32_t id, std::string& err) {
        std::lock_guard<std::mutex> lock(mutex);
        auto hit = decoded.find(id);
        if (hit != decoded.end()) return &hit->second;
        auto e = byId.find(id);
        if (e == byId.end()) { err = "mesh id " + std::to_string(id) + " not in MBANK_ALLMESHES"; return nullptr; }
        try {
            auto g = forge::meshpreview::decodeLod0(big->entryData(*e->second), e->second->type);
            if (g.empty()) { err = "mesh " + e->second->name + " has no LOD0 geometry"; return nullptr; }
            return &(decoded[id] = std::move(g));
        } catch (const std::exception& ex) {
            err = std::string("mesh ") + e->second->name + ": " + ex.what();
            return nullptr;
        }
    }
};

MeshCache& meshCache() { static MeshCache c; return c; }

void toUp(te::UpAxis up, float x, float y, float z, float& ox, float& oy, float& oz) {
    if (up == te::UpAxis::Y) { ox = x; oy = z; oz = -y; }
    else                     { ox = x; oy = y; oz = z; }
}

} // namespace

size_t Scene::triangleCount() const {
    size_t n = 0;
    for (const auto& i : instances)
        if (i.mesh >= 0 && size_t(i.mesh) < meshes.size()) n += meshes[size_t(i.mesh)].geometry.triangles.size();
    return n;
}

Scene load(const std::string& mapName, const Options& options, const te::Context& context) {
    Scene scene;
    scene.mapName = mapName;

    const fs::path stbPath = options.gameRoot / "data" / "Levels" / "FinalAlbion_RT.stb";
    if (!fs::exists(stbPath)) { warn(options, scene, "no FinalAlbion_RT.stb in this install"); return scene; }

    forge::foliage::Palette palette;
    forge::foliage::InstanceScan scan;
    try {
        const auto archive = forge::stb::Archive::open(stbPath);
        const std::string stem = lower(mapName);
        const forge::stb::StaticMap* map = nullptr;
        for (const auto& c : archive.staticMaps())
            if (lower(fs::path(c.levelName).stem().string()) == stem) { map = &c; break; }
        if (!map) { warn(options, scene, "the static-map bank has no entry for " + mapName + " (no baked foliage)"); return scene; }
        const auto record = archive.readStaticMapRecord(*map);
        if (record.size() < forge::stbinfo::kInfoBlockSize) { warn(options, scene, "static-map record too short"); return scene; }
        const auto info = forge::stbinfo::readInfoBlock(record.data());
        scene.worldX = info.worldX; scene.worldY = info.worldY;
        palette = forge::foliage::readPalette(record.data(), record.size());
        scene.paletteTypes = int(palette.entries.size());
        const forge::stb::Entry* chunk = nullptr;
        for (const auto& e : archive.entries())
            if (int32_t(e.id) == info.bankFileIndex) { chunk = &e; break; }
        if (!chunk) { warn(options, scene, "static-map chunk missing for " + mapName); return scene; }
        const auto bytes = archive.read(*chunk);
        forge::foliage::ScanBounds bounds{float(info.worldX), float(info.worldX + info.mapWidth),
                                          float(info.worldY), float(info.worldY + info.mapHeight)};
        scan = forge::foliage::scanInstances(bytes, chunk->name, bounds);
        scene.framesDecoded = scan.framesDecoded;
        scene.unboundInstances = scan.unboundInstances;
        scene.found = true;
    } catch (const std::exception& e) {
        warn(options, scene, std::string("reading foliage: ") + e.what());
        return scene;
    }
    if (options.log) options.log(std::to_string(scan.instances.size()) + " baked foliage instances, " +
                                 std::to_string(palette.entries.size()) + " scenery types");

    // Meshes for every palette type that has instances.
    fs::path graphics = options.gameRoot / "data" / "graphics" / "graphics.big";
    if (!fs::exists(graphics)) graphics = options.gameRoot / "data" / "graphics" / "pc" / "graphics.big";
    std::string err;
    if (!meshCache().open(graphics, err)) { warn(options, scene, "graphics.big: " + err); return scene; }

    std::map<int, int> typeToMesh;      // palette type# -> Scene::meshes index
    std::map<uint32_t, int> meshIdToIndex;
    std::map<uint32_t, int> textureToImage;
    for (const auto& inst : scan.instances) {
        if (inst.paletteIndex < 0 || inst.paletteIndex >= int(palette.entries.size())) continue;
        if (!std::isfinite(inst.scale) || inst.scale <= 0.0f) continue;
        if (!typeToMesh.count(inst.paletteIndex)) {
            const auto& type = palette.entries[size_t(inst.paletteIndex)];
            int meshIndex = -1;
            auto known = meshIdToIndex.find(type.meshIdx);
            if (known != meshIdToIndex.end()) meshIndex = known->second;
            else {
                std::string merr;
                const auto* geo = meshCache().get(type.meshIdx, merr);
                if (!geo) { warn(options, scene, merr); meshIdToIndex[type.meshIdx] = -1; typeToMesh[inst.paletteIndex] = -1; continue; }
                Mesh m;
                m.meshId = type.meshIdx;
                m.name = type.meshName.empty() ? meshCache().names[type.meshIdx] : type.meshName;
                m.label = type.label;
                m.geometry = *geo;
                for (const auto& mat : geo->materials)
                    if (mat.diffuseTexture > 0) { m.diffuseTexture = uint32_t(mat.diffuseTexture); break; }
                if (options.textures && m.diffuseTexture) {
                    auto img = textureToImage.find(m.diffuseTexture);
                    if (img == textureToImage.end()) {
                        std::string twarn;
                        const te::Image* tex = context.texture(m.diffuseTexture, twarn);
                        if (tex) {
                            scene.images.push_back(*tex);
                            scene.images.back().name = m.name;
                            img = textureToImage.emplace(m.diffuseTexture, int(scene.images.size() - 1)).first;
                        } else {
                            warn(options, scene, m.name + ": " + twarn);
                            img = textureToImage.emplace(m.diffuseTexture, -1).first;
                        }
                    }
                    m.image = img->second;
                    if (m.image >= 0) {
                        const auto& px = scene.images[size_t(m.image)].rgba;
                        for (size_t i = 3; i < px.size(); i += 4) if (px[i] < 250) { m.hasAlpha = true; break; }
                    }
                }
                scene.meshes.push_back(std::move(m));
                meshIndex = int(scene.meshes.size() - 1);
                meshIdToIndex[type.meshIdx] = meshIndex;
            }
            typeToMesh[inst.paletteIndex] = meshIndex;
        }
        const int meshIndex = typeToMesh[inst.paletteIndex];
        if (meshIndex < 0) continue;
        Instance out;
        out.mesh = meshIndex;
        out.type = inst.paletteIndex;
        out.x = inst.x - (options.mapLocal ? float(scene.worldX) : 0.0f);
        out.y = inst.y - (options.mapLocal ? float(scene.worldY) : 0.0f);
        out.z = inst.z;
        out.yaw = inst.hasRotation ? inst.yawRadians : 0.0f;
        out.scale = inst.scale;
        scene.meshes[size_t(meshIndex)].instanceCount++;
        scene.instances.push_back(out);
    }
    // Scanner false positives show up as absurd scales (a 10x birch in a map
    // corner). Reject per mesh: outside [median/16, median*4].
    {
        std::map<int, std::vector<float>> scales;
        for (const auto& i : scene.instances) scales[i.mesh].push_back(i.scale);
        std::map<int, std::pair<float, float>> range;
        for (auto& [mesh, v] : scales) {
            std::sort(v.begin(), v.end());
            const float med = v[v.size() / 2];
            range[mesh] = {med / 16.0f, med * 4.0f};
        }
        std::vector<Instance> kept;
        kept.reserve(scene.instances.size());
        for (const auto& i : scene.instances) {
            const auto [lo, hi] = range[i.mesh];
            if (i.scale < lo || i.scale > hi) {
                ++scene.rejectedInstances;
                scene.meshes[size_t(i.mesh)].instanceCount--;
                continue;
            }
            kept.push_back(i);
        }
        scene.instances = std::move(kept);
        if (scene.rejectedInstances && options.log)
            options.log("  rejected " + std::to_string(scene.rejectedInstances) + " instance(s) with implausible scale (scanner false positives)");
    }
    if (options.log && !scene.instances.empty()) {
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
        for (const auto& i : scene.instances) {
            const float v[3] = {i.x, i.y, i.z};
            for (int k = 0; k < 3; ++k) { lo[k] = std::min(lo[k], v[k]); hi[k] = std::max(hi[k], v[k]); }
        }
        char b[160];
        std::snprintf(b, sizeof b, "  instance bounds (map-local): x %.1f..%.1f  y %.1f..%.1f  z %.1f..%.1f", lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]);
        options.log(b);
    }
    if (options.log)
        for (const auto& m : scene.meshes)
            options.log("  " + std::to_string(m.instanceCount) + " x " + m.name + (m.label.empty() ? "" : " (" + m.label + ")") +
                        (m.image >= 0 ? "" : " [no texture]") + (m.hasAlpha ? " [cutout]" : ""));
    if (options.log) options.log(std::to_string(scene.instances.size()) + " instances placed across " +
                                 std::to_string(scene.meshes.size()) + " meshes (" +
                                 std::to_string(scene.triangleCount()) + " triangles)");
    return scene;
}

// ------------------------------------------------------------------- writers

namespace {

void appendFoliage(glb::Builder& b, const Scene& f, te::UpAxis up, std::vector<int>& roots) {
    if (f.instances.empty()) return;
    std::vector<int> glMesh(f.meshes.size(), -1);
    std::map<int, int> imageToTexture;
    for (size_t mi = 0; mi < f.meshes.size(); ++mi) {
        const Mesh& m = f.meshes[mi];
        if (m.instanceCount == 0) continue;
        const auto& g = m.geometry;
        std::vector<float> pos(g.vertices.size() * 3), nrm(g.vertices.size() * 3), uv(g.vertices.size() * 2);
        for (size_t i = 0; i < g.vertices.size(); ++i) {
            const auto& v = g.vertices[i];
            toUp(up, v.x, v.y, v.z, pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2]);
            toUp(up, v.nx, v.ny, v.nz, nrm[i * 3], nrm[i * 3 + 1], nrm[i * 3 + 2]);
            uv[i * 2] = v.u; uv[i * 2 + 1] = v.v;
        }
        std::vector<uint32_t> idx;
        idx.reserve(g.triangles.size() * 3);
        for (const auto& t : g.triangles) { idx.push_back(t.a); idx.push_back(t.b); idx.push_back(t.c); }

        json material = {{"name", m.name}, {"doubleSided", true},
                         {"pbrMetallicRoughness", {{"metallicFactor", 0.0}, {"roughnessFactor", 1.0}}}};
        if (m.image >= 0) {
            auto t = imageToTexture.find(m.image);
            if (t == imageToTexture.end())
                t = imageToTexture.emplace(m.image, b.texture(f.images[size_t(m.image)], f.images[size_t(m.image)].name, true)).first;
            material["pbrMetallicRoughness"]["baseColorTexture"] = {{"index", t->second}};
            if (m.hasAlpha) { material["alphaMode"] = "MASK"; material["alphaCutoff"] = 0.5; }
        } else {
            material["pbrMetallicRoughness"]["baseColorFactor"] = {0.35, 0.55, 0.3, 1.0};
        }
        const int mat = b.material(material);
        json attributes = {{"POSITION", b.positions(pos)}, {"NORMAL", b.vec3(nrm)}, {"TEXCOORD_0", b.vec2(uv)}};
        glMesh[mi] = b.mesh({{"name", m.name},
                             {"extras", {{"mesh_id", m.meshId}, {"label", m.label}, {"instances", m.instanceCount}}},
                             {"primitives", {{{"attributes", attributes}, {"indices", b.indices(idx)}, {"material", mat}, {"mode", 4}}}}});
    }
    std::vector<int> children;
    children.reserve(f.instances.size());
    for (const Instance& inst : f.instances) {
        if (inst.mesh < 0 || glMesh[size_t(inst.mesh)] < 0) continue;
        float tx, ty, tz;
        toUp(up, inst.x, inst.y, inst.z, tx, ty, tz);
        const float hs = std::sin(inst.yaw * 0.5f), hc = std::cos(inst.yaw * 0.5f);
        // Yaw about Fable +Z becomes a rotation about glTF +Y under (x, z, -y).
        json rot = up == te::UpAxis::Y ? json{0.0f, hs, 0.0f, hc} : json{0.0f, 0.0f, hs, hc};
        children.push_back(b.node({{"mesh", glMesh[size_t(inst.mesh)]},
                                   {"translation", {tx, ty, tz}}, {"rotation", rot},
                                   {"scale", {inst.scale, inst.scale, inst.scale}}}));
    }
    roots.push_back(b.node({{"name", "Foliage"}, {"children", children},
                            {"extras", {{"instances", children.size()}, {"scenery_types", f.paletteTypes},
                                        {"unbound_instances", f.unboundInstances},
                                        {"note", "baked local-detail from FinalAlbion_RT.stb; LOD0 meshes"}}}}));
}

} // namespace

std::vector<uint8_t> buildGlbWithFoliage(const te::Scene& terrain, const Scene& foliage) {
    glb::Builder b;
    std::vector<int> roots{te::appendTerrain(b, terrain)};
    appendFoliage(b, foliage, terrain.up, roots);
    return b.finish(terrain.sourceName, roots, "AlbionTerrain terrain exporter");
}

std::vector<fs::path> writeGlbWithFoliage(const te::Scene& terrain, const Scene& foliage, const fs::path& out) {
    const auto glb = buildGlbWithFoliage(terrain, foliage);
    if (!out.parent_path().empty()) fs::create_directories(out.parent_path());
    std::ofstream f(out, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write " + out.string());
    f.write(reinterpret_cast<const char*>(glb.data()), std::streamsize(glb.size()));
    std::vector<fs::path> written{out};
    for (auto& p : te::writeLayerSidecars(terrain, out)) written.push_back(p);
    return written;
}

std::vector<fs::path> writeObjWithFoliage(const te::Scene& terrain, const Scene& foliage, const fs::path& out) {
    auto written = te::writeObj(terrain, out);
    if (foliage.instances.empty()) return written;
    const std::string stem = out.stem().string();
    const fs::path mtlPath = out.parent_path() / (stem + ".mtl");
    std::ofstream obj(out, std::ios::app), mtl(mtlPath, std::ios::app);
    uint32_t base = uint32_t(terrain.vertices.size());
    obj << "o " << stem << "_foliage\n";
    // One material per texture image.
    std::map<int, std::string> imageMaterial;
    for (const Mesh& m : foliage.meshes) {
        if (m.instanceCount == 0 || m.image < 0 || imageMaterial.count(m.image)) continue;
        const std::string matName = "foliage_" + std::to_string(m.diffuseTexture);
        const fs::path png = out.parent_path() / (stem + "_" + matName + ".png");
        const auto bytes = te::encodePng(foliage.images[size_t(m.image)]);
        std::ofstream(png, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        written.push_back(png);
        mtl << "newmtl " << matName << "\nKa 1 1 1\nKd 1 1 1\nKs 0 0 0\nd 1\nillum 1\nmap_Kd " << png.filename().string() << "\n";
        if (m.hasAlpha) mtl << "map_d " << png.filename().string() << "\n";
        imageMaterial[m.image] = matName;
    }
    mtl << "newmtl foliage_untextured\nKd 0.35 0.55 0.3\n";
    char line[160];
    // Bake instances: group by material to minimise usemtl switches.
    std::vector<size_t> order(foliage.instances.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t c) {
        return foliage.meshes[size_t(foliage.instances[a].mesh)].image < foliage.meshes[size_t(foliage.instances[c].mesh)].image;
    });
    int currentImage = -2;
    for (size_t oi : order) {
        const Instance& inst = foliage.instances[oi];
        if (inst.mesh < 0) continue;
        const Mesh& m = foliage.meshes[size_t(inst.mesh)];
        if (m.image != currentImage) {
            currentImage = m.image;
            obj << "usemtl " << (m.image >= 0 ? imageMaterial[m.image] : "foliage_untextured") << "\n";
        }
        const float cs = std::cos(inst.yaw), sn = std::sin(inst.yaw);
        for (const auto& v : m.geometry.vertices) {
            const float lx = v.x * inst.scale, ly = v.y * inst.scale, lz = v.z * inst.scale;
            const float wx = inst.x + lx * cs - ly * sn, wy = inst.y + lx * sn + ly * cs, wz = inst.z + lz;
            float px, py, pz, nx, ny, nz;
            toUp(terrain.up, wx, wy, wz, px, py, pz);
            toUp(terrain.up, v.nx * cs - v.ny * sn, v.nx * sn + v.ny * cs, v.nz, nx, ny, nz);
            std::snprintf(line, sizeof line, "v %.4f %.4f %.4f\nvt %.5f %.5f\nvn %.4f %.4f %.4f\n", px, py, pz, v.u, 1.0f - v.v, nx, ny, nz);
            obj << line;
        }
        for (const auto& t : m.geometry.triangles) {
            const uint32_t a = base + t.a + 1, bb = base + t.b + 1, c = base + t.c + 1;
            std::snprintf(line, sizeof line, "f %u/%u/%u %u/%u/%u %u/%u/%u\n", a, a, a, bb, bb, bb, c, c, c);
            obj << line;
        }
        base += uint32_t(m.geometry.vertices.size());
    }
    return written;
}

} // namespace albion::foliageexport
