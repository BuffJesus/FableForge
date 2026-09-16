#include "foliageexport.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <mutex>

#include "forge/big.hpp"
#include "forge/foliage.hpp"
#include "forge/lzo.hpp"
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

} // namespace

bool openMeshBank(const fs::path& graphicsBig, std::string& err) { return meshCache().open(graphicsBig, err); }
const forge::meshpreview::Geometry* cachedMesh(uint32_t id, std::string& err) { return meshCache().get(id, err); }
std::string meshName(uint32_t id) {
    std::lock_guard<std::mutex> lock(meshCache().mutex);
    auto it = meshCache().names.find(id);
    return it == meshCache().names.end() ? std::string() : it->second;
}
uint32_t meshIdByName(const std::string& name) {
    std::lock_guard<std::mutex> lock(meshCache().mutex);
    for (const auto& [id, n] : meshCache().names) if (lower(n) == lower(name)) return id;
    return 0;
}

Mesh makeMesh(uint32_t meshId, const std::string& name, const std::string& label,
              const forge::meshpreview::Geometry& geo, bool textures,
              const te::Context& context, std::vector<te::Image>& images,
              std::map<uint32_t, int>& textureToImage, std::vector<std::string>& warnings) {
    Mesh m;
    m.meshId = meshId;
    m.name = name;
    m.label = label;
    m.geometry = geo;
    // Compressed vertex formats decode UVs with an integer offset (8.x);
    // wrap-equivalent, but normalise so exported UVs read as 0..1.
    if (!m.geometry.vertices.empty()) {
        float minU = 1e30f, minV = 1e30f;
        for (const auto& v : m.geometry.vertices) { minU = std::min(minU, v.u); minV = std::min(minV, v.v); }
        const float du = std::floor(minU), dv = std::floor(minV);
        if (std::isfinite(du) && std::isfinite(dv) && (du != 0 || dv != 0))
            for (auto& v : m.geometry.vertices) { v.u -= du; v.v -= dv; }
    }
    std::map<int, size_t> partByMaterial;
    for (const auto& t : geo.triangles) {
        const int mat = (t.material >= 0 && size_t(t.material) < geo.materials.size()) ? int(t.material) : -1;
        auto it = partByMaterial.find(mat);
        if (it == partByMaterial.end()) {
            SubMesh part;
            part.material = mat;
            if (mat >= 0) part.diffuseTexture = geo.materials[size_t(mat)].diffuseTexture > 0 ? uint32_t(geo.materials[size_t(mat)].diffuseTexture) : 0;
            if (textures && part.diffuseTexture) {
                auto img = textureToImage.find(part.diffuseTexture);
                if (img == textureToImage.end()) {
                    std::string twarn;
                    const te::Image* tex = context.texture(part.diffuseTexture, twarn);
                    if (tex) {
                        images.push_back(*tex);
                        img = textureToImage.emplace(part.diffuseTexture, int(images.size() - 1)).first;
                    } else {
                        warnings.push_back(name + ": " + twarn);
                        img = textureToImage.emplace(part.diffuseTexture, -1).first;
                    }
                }
                part.image = img->second;
                if (part.image >= 0) {
                    const auto& px = images[size_t(part.image)].rgba;
                    for (size_t i = 3; i < px.size(); i += 4) if (px[i] < 250) { part.hasAlpha = true; break; }
                }
            }
            it = partByMaterial.emplace(mat, m.parts.size()).first;
            m.parts.push_back(std::move(part));
        }
        auto& idx = m.parts[it->second].indices;
        idx.push_back(t.a); idx.push_back(t.b); idx.push_back(t.c);
    }
    if (!m.parts.empty()) { m.diffuseTexture = m.parts[0].diffuseTexture; m.image = m.parts[0].image; m.hasAlpha = m.parts[0].hasAlpha; }
    for (const auto& part : m.parts) if (part.hasAlpha) m.hasAlpha = true;
    return m;
}

namespace {

void toUp(te::UpAxis up, float x, float y, float z, float& ox, float& oy, float& oz) {
    if (up == te::UpAxis::Y) { ox = x; oy = z; oz = -y; }
    else                     { ox = x; oy = y; oz = z; }
}

// ---------------------------------------------------------------- frames
// Every standard-LZO1X frame in a bank chunk: [u32 uncompressed][u32 compressed]
// then the body. Frames are only recognised when they decode to exactly the
// declared length, the same honest gate FableForge's chunk model uses.
template <typename Fn>
void forEachFrame(const std::vector<uint8_t>& d, int& framesDecoded, Fn&& onFrame) {
    auto u32 = [&](size_t o) { uint32_t v; std::memcpy(&v, d.data() + o, 4); return v; };
    const size_t n = d.size();
    for (size_t off = 0; off + 8 < n; off += 4) {
        const uint32_t a = u32(off), b = u32(off + 4);
        const uint32_t pairs[2][2] = {{a, b}, {b, a}};
        for (int k = 0; k < 2; ++k) {
            const uint32_t unc = pairs[k][0], comp = pairs[k][1];
            if (comp < 32 || comp > 400000 || unc < 64 || unc > 4000000 || comp > unc) continue;
            if (off + 8 + size_t(comp) > n) continue;
            std::vector<uint8_t> out;
            try { out = forge::lzo::decompress(d.data() + off + 8, comp, unc); }
            catch (const std::exception&) { continue; }
            ++framesDecoded;
            onFrame(out);
            off = ((off + 8 + comp + 3) & ~size_t(3)) - 4;
            break;
        }
    }
}

// ------------------------------------------------- cache-group grammar
// CObjectCacheGroupCollection::SaveContents payload:
//   u32 collectionCount
//   per collection: u32 needsRenderUpdate(0/1), u32 typeIndex, u32 primCount
//     per primitive: u32 primType
//       bbox 6*f32, sphere 4*f32
//       type 1: u32 N, f32 maxScale, N*(cos*s, sin*s, 0, 0), N*(x, y, z, scale),
//               u8 normals? [3*P f32, P=(N+3)&~3], u8 wind? [N bytes],
//               u8 subsections? [u32 count, count*0x50]
//       type 0: 12*f32 (3x3 rotation*scale rows + translation), f32 1.0
//       type 2: not decoded (parsing stops; counted)
struct RawPlacement {
    int type = -1, prim = 1;
    float x = 0, y = 0, z = 0, yaw = 0, scale = 1;
    bool hasMatrix = false;
    float m[9] = {};
};

struct ParseStats { int zsprite = 0; };

// World = R * local (+ translation) for an instance, in FABLE space. Type-1:
// yaw about +Z times uniform scale. Type-0: the stored 3x3, whose ROWS are the
// images of the local x/y/z axes (the engine writes row-vector matrices), so
// world = local.x*row0 + local.y*row1 + local.z*row2.
} // namespace

void instanceBasis(const Instance& i, float col[3][3]) {  // col[k] = image of local axis k
    if (i.hasMatrix) {
        for (int k = 0; k < 3; ++k) for (int c = 0; c < 3; ++c) col[k][c] = i.m[k * 3 + c];
    } else {
        const float cs = std::cos(i.yaw) * i.scale, sn = std::sin(i.yaw) * i.scale;
        col[0][0] = cs; col[0][1] = sn; col[0][2] = 0;
        col[1][0] = -sn; col[1][1] = cs; col[1][2] = 0;
        col[2][0] = 0; col[2][1] = 0; col[2][2] = i.scale;
    }
}

namespace {

// Rotation matrix (columns = basis images, orthonormal) -> quaternion (x,y,z,w).
void quatFromColumns(const float c0[3], const float c1[3], const float c2[3], float q[4]) {
    // m[r][c] with columns c0,c1,c2
    const float m00 = c0[0], m10 = c0[1], m20 = c0[2];
    const float m01 = c1[0], m11 = c1[1], m21 = c1[2];
    const float m02 = c2[0], m12 = c2[1], m22 = c2[2];
    const float tr = m00 + m11 + m22;
    if (tr > 0) {
        const float s = std::sqrt(tr + 1.0f) * 2;
        q[3] = 0.25f * s; q[0] = (m21 - m12) / s; q[1] = (m02 - m20) / s; q[2] = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2;
        q[3] = (m21 - m12) / s; q[0] = 0.25f * s; q[1] = (m01 + m10) / s; q[2] = (m02 + m20) / s;
    } else if (m11 > m22) {
        const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2;
        q[3] = (m02 - m20) / s; q[0] = (m01 + m10) / s; q[1] = 0.25f * s; q[2] = (m12 + m21) / s;
    } else {
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2;
        q[3] = (m10 - m01) / s; q[0] = (m02 + m20) / s; q[1] = (m12 + m21) / s; q[2] = 0.25f * s;
    }
    const float len = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (int i = 0; i < 4; ++i) q[i] /= len;
}

struct WorldBounds { float xlo, xhi, ylo, yhi, zlo, zhi; };

bool parseGroupFrame(const std::vector<uint8_t>& d, const WorldBounds& wb,
                     std::vector<RawPlacement>& out, ParseStats& st) {
    size_t p = 0;
    const size_t n = d.size();
    auto have = [&](size_t k) { return p + k <= n; };
    auto u32 = [&]() { uint32_t v; std::memcpy(&v, d.data() + p, 4); p += 4; return v; };
    auto f32 = [&]() { float v; std::memcpy(&v, d.data() + p, 4); p += 4; return v; };
    auto sane = [](float v, float lo, float hi) { return std::isfinite(v) && v >= lo && v <= hi; };
    auto inWorld = [&](float x, float y, float z) {
        return sane(x, wb.xlo, wb.xhi) && sane(y, wb.ylo, wb.yhi) && sane(z, wb.zlo, wb.zhi);
    };
    if (!have(4)) return false;
    const uint32_t collections = u32();
    if (collections == 0 || collections > 128) return false;
    const size_t before = out.size();
    for (uint32_t c = 0; c < collections; ++c) {
        if (!have(12)) break;
        const uint32_t flag = u32(), typeIndex = u32(), primCount = u32();
        if (flag > 1 || typeIndex > 255 || primCount == 0 || primCount > 65536) { out.resize(before); return false; }
        for (uint32_t pi = 0; pi < primCount; ++pi) {
            if (!have(4 + 40)) { out.resize(before); return false; }
            const uint32_t primType = u32();
            if (primType > 2) { out.resize(before); return false; }
            float bb[6], sp[4];
            for (float& v : bb) v = f32();
            for (float& v : sp) v = f32();
            if (!inWorld(bb[0], bb[1], bb[2]) || !inWorld(bb[3], bb[4], bb[5]) || !sane(sp[3], 0.0f, 100000.0f)) {
                out.resize(before); return false;
            }
            if (primType == 2) { ++st.zsprite; return out.size() > before; }   // stop here, keep what parsed
            if (primType == 1) {
                if (!have(8)) { out.resize(before); return false; }
                const uint32_t N = u32();
                const float maxScale = f32();
                if (N == 0 || N > 4096 || !sane(maxScale, 0.0f, 10000.0f)) { out.resize(before); return false; }
                if (!have(size_t(N) * 32 + 1)) { out.resize(before); return false; }
                const size_t aStart = p;
                p += size_t(N) * 16;
                for (uint32_t i = 0; i < N; ++i) {
                    RawPlacement r; r.type = int(typeIndex); r.prim = 1;
                    r.x = f32(); r.y = f32(); r.z = f32(); r.scale = f32();
                    float ax, ay; std::memcpy(&ax, d.data() + aStart + size_t(i) * 16, 4); std::memcpy(&ay, d.data() + aStart + size_t(i) * 16 + 4, 4);
                    r.yaw = std::atan2(ay, ax);
                    if (!inWorld(r.x, r.y, r.z) || !sane(r.scale, 0.0f, 10000.0f)) { out.resize(before); return false; }
                    out.push_back(r);
                }
                if (!have(1)) { out.resize(before); return false; }
                const uint8_t normals = d[p++];
                if (normals > 1) { out.resize(before); return false; }
                if (normals) { const size_t P = (size_t(N) + 3) & ~size_t(3); if (!have(P * 12)) { out.resize(before); return false; } p += P * 12; }
                if (!have(1)) { out.resize(before); return false; }
                const uint8_t wind = d[p++];
                if (wind > 1) { out.resize(before); return false; }
                if (wind) { if (!have(N)) { out.resize(before); return false; } p += N; }
                if (!have(1)) { out.resize(before); return false; }
                const uint8_t sub = d[p++];
                if (sub > 1) { out.resize(before); return false; }
                if (sub) {
                    if (!have(4)) { out.resize(before); return false; }
                    const uint32_t cnt = u32();
                    if (cnt > 4096 || !have(size_t(cnt) * 0x50)) { out.resize(before); return false; }
                    p += size_t(cnt) * 0x50;
                }
            } else {   // type 0: single mesh with a full matrix
                if (!have(13 * 4)) { out.resize(before); return false; }
                float mtx[12];
                for (float& v : mtx) v = f32();
                const float one = f32();
                RawPlacement r; r.type = int(typeIndex); r.prim = 0; r.hasMatrix = true;
                for (int i = 0; i < 9; ++i) r.m[i] = mtx[i];
                r.x = mtx[9]; r.y = mtx[10]; r.z = mtx[11];
                bool finite = std::isfinite(one);
                for (float v : mtx) finite = finite && std::isfinite(v) && std::fabs(v) < 1e5f;
                if (!finite || !inWorld(r.x, r.y, r.z)) { out.resize(before); return false; }
                const float sx = std::sqrt(mtx[0] * mtx[0] + mtx[1] * mtx[1] + mtx[2] * mtx[2]);
                const float sy = std::sqrt(mtx[3] * mtx[3] + mtx[4] * mtx[4] + mtx[5] * mtx[5]);
                const float sz = std::sqrt(mtx[6] * mtx[6] + mtx[7] * mtx[7] + mtx[8] * mtx[8]);
                r.scale = (sx + sy + sz) / 3.0f;
                r.yaw = std::atan2(mtx[1], mtx[0]);
                if (!sane(r.scale, 1e-5f, 10000.0f)) { out.resize(before); return false; }
                out.push_back(r);
            }
        }
    }
    // A genuine group frame is consumed (nearly) to the end; anything else is
    // some other record type that happened to start with a small integer.
    if (n - p > 64) { out.resize(before); return false; }
    return out.size() > before;
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
    std::vector<RawPlacement> raw;
    ParseStats pst;
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
        // Generous world window: the map's own extent padded by a cell block,
        // heights anywhere an Albion map can plausibly reach.
        const WorldBounds wb{float(info.worldX) - 16, float(info.worldX + info.mapWidth) + 16,
                             float(info.worldY) - 16, float(info.worldY + info.mapHeight) + 16, -500.0f, 2000.0f};
        int frames = 0;
        forEachFrame(bytes, frames, [&](const std::vector<uint8_t>& frame) {
            if (parseGroupFrame(frame, wb, raw, pst)) ++scene.groupFrames;
        });
        scene.framesDecoded = frames;
        scene.zspriteSkipped = pst.zsprite;
        scene.found = true;
    } catch (const std::exception& e) {
        warn(options, scene, std::string("reading foliage: ") + e.what());
        return scene;
    }
    if (options.log) options.log(std::to_string(raw.size()) + " baked placements in " + std::to_string(scene.groupFrames) +
                                 " cache-group frames (" + std::to_string(scene.framesDecoded) + " frames decoded), " +
                                 std::to_string(palette.entries.size()) + " scenery types" +
                                 (pst.zsprite ? ", " + std::to_string(pst.zsprite) + " z-sprite batch(es) skipped" : ""));

    // Meshes for every palette type that has instances.
    fs::path graphics = options.gameRoot / "data" / "graphics" / "graphics.big";
    if (!fs::exists(graphics)) graphics = options.gameRoot / "data" / "graphics" / "pc" / "graphics.big";
    std::string err;
    if (!meshCache().open(graphics, err)) { warn(options, scene, "graphics.big: " + err); return scene; }

    std::map<int, int> typeToMesh;      // palette type# -> Scene::meshes index
    std::map<uint32_t, int> meshIdToIndex;
    std::map<uint32_t, int> textureToImage;
    for (const auto& inst : raw) {
        if (inst.type < 0 || inst.type >= int(palette.entries.size())) { ++scene.unboundInstances; continue; }
        if (!std::isfinite(inst.scale) || inst.scale <= 0.0f) continue;
        if (!typeToMesh.count(inst.type)) {
            const auto& type = palette.entries[size_t(inst.type)];
            int meshIndex = -1;
            auto known = meshIdToIndex.find(type.meshIdx);
            if (known != meshIdToIndex.end()) meshIndex = known->second;
            else {
                std::string merr;
                const auto* geo = meshCache().get(type.meshIdx, merr);
                if (!geo) { warn(options, scene, merr); meshIdToIndex[type.meshIdx] = -1; typeToMesh[inst.type] = -1; continue; }
                std::vector<std::string> mw;
                const std::string bankName = meshName(type.meshIdx);
                Mesh m = makeMesh(type.meshIdx, bankName.empty() ? type.meshName : bankName, type.label,
                                  *geo, options.textures, context, scene.images, textureToImage, mw);
                for (const auto& w : mw) warn(options, scene, w);
                scene.meshes.push_back(std::move(m));
                meshIndex = int(scene.meshes.size() - 1);
                meshIdToIndex[type.meshIdx] = meshIndex;
            }
            typeToMesh[inst.type] = meshIndex;
        }
        const int meshIndex = typeToMesh[inst.type];
        if (meshIndex < 0) { ++scene.unboundInstances; continue; }
        Instance out;
        out.mesh = meshIndex;
        out.type = inst.type;
        out.prim = inst.prim;
        out.x = inst.x - (options.mapLocal ? float(scene.worldX) : 0.0f);
        out.y = inst.y - (options.mapLocal ? float(scene.worldY) : 0.0f);
        out.z = inst.z;
        out.yaw = inst.yaw;
        out.scale = inst.scale;
        out.hasMatrix = inst.hasMatrix;
        std::memcpy(out.m, inst.m, sizeof out.m);
        if (inst.prim == 0) ++scene.treeInstances;
        scene.meshes[size_t(meshIndex)].instanceCount++;
        scene.instances.push_back(out);
    }
    // Scanner false positives show up as absurd scales (a 10x birch in a map
    // corner). Reject per mesh: outside [median/16, median*4].
    {
        std::map<int, std::vector<float>> scales;
        for (const auto& i : scene.instances) if (!i.hasMatrix) scales[i.mesh].push_back(i.scale);
        std::map<int, std::pair<float, float>> range;
        for (auto& [mesh, v] : scales) {
            std::sort(v.begin(), v.end());
            const float med = v[v.size() / 2];
            range[mesh] = {med / 16.0f, med * 4.0f};
        }
        std::vector<Instance> kept;
        kept.reserve(scene.instances.size());
        for (const auto& i : scene.instances) {
            if (i.hasMatrix || !range.count(i.mesh)) { kept.push_back(i); continue; }
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
    if (options.log) options.log("  " + std::to_string(scene.treeInstances) + " single-mesh (tree/prop) placements, " +
                                 std::to_string(scene.unboundInstances) + " unbound");
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
        json attributes = {{"POSITION", b.positions(pos)}, {"NORMAL", b.vec3(nrm)}, {"TEXCOORD_0", b.vec2(uv)}};
        json primitives = json::array();
        for (const SubMesh& part : m.parts) {
            if (part.indices.empty()) continue;
            json material = {{"name", m.name + (m.parts.size() > 1 ? "_" + std::to_string(part.material) : "")}, {"doubleSided", true},
                             {"pbrMetallicRoughness", {{"metallicFactor", 0.0}, {"roughnessFactor", 1.0}}}};
            if (part.image >= 0) {
                auto t = imageToTexture.find(part.image);
                if (t == imageToTexture.end())
                    t = imageToTexture.emplace(part.image, b.texture(f.images[size_t(part.image)], f.images[size_t(part.image)].name, true)).first;
                material["pbrMetallicRoughness"]["baseColorTexture"] = {{"index", t->second}};
                if (part.hasAlpha) { material["alphaMode"] = "MASK"; material["alphaCutoff"] = 0.5; }
            } else {
                material["pbrMetallicRoughness"]["baseColorFactor"] = {0.35, 0.55, 0.3, 1.0};
            }
            primitives.push_back({{"attributes", attributes}, {"indices", b.indices(part.indices)}, {"material", b.material(material)}, {"mode", 4}});
        }
        if (primitives.empty()) continue;
        glMesh[mi] = b.mesh({{"name", m.name},
                             {"extras", {{"mesh_id", m.meshId}, {"label", m.label}, {"instances", m.instanceCount}}},
                             {"primitives", primitives}});
    }
    std::vector<int> children;
    children.reserve(f.instances.size());
    for (const Instance& inst : f.instances) {
        if (inst.mesh < 0 || glMesh[size_t(inst.mesh)] < 0) continue;
        float tx, ty, tz;
        toUp(up, inst.x, inst.y, inst.z, tx, ty, tz);
        // Basis images in Fable space -> output space, then split into scale + rotation.
        float col[3][3]; instanceBasis(inst, col);
        float oc[3][3], sc[3];
        for (int k = 0; k < 3; ++k) {
            toUp(up, col[k][0], col[k][1], col[k][2], oc[k][0], oc[k][1], oc[k][2]);
            sc[k] = std::sqrt(oc[k][0] * oc[k][0] + oc[k][1] * oc[k][1] + oc[k][2] * oc[k][2]);
            if (sc[k] > 1e-12f) for (float& v : oc[k]) v /= sc[k];
        }
        // The local axes themselves were converted by toUp too (mesh vertices went
        // through the same map), so the local->output basis order is (x, z, -y):
        // output local axis 0 = Fable x, 1 = Fable z, 2 = -Fable y.
        float q[4];
        float sx = sc[0], sy = sc[2], sz = sc[1];
        float cy[3] = {-oc[1][0], -oc[1][1], -oc[1][2]};
        if (up == te::UpAxis::Y) quatFromColumns(oc[0], oc[2], cy, q);
        else { quatFromColumns(oc[0], oc[1], oc[2], q); sx = sc[0]; sy = sc[1]; sz = sc[2]; }
        children.push_back(b.node({{"mesh", glMesh[size_t(inst.mesh)]},
                                   {"translation", {tx, ty, tz}}, {"rotation", {q[0], q[1], q[2], q[3]}},
                                   {"scale", {sx, sy, sz}}}));
    }
    roots.push_back(b.node({{"name", f.rootName}, {"children", children},
                            {"extras", {{"instances", children.size()}, {"scenery_types", f.paletteTypes},
                                        {"unbound_instances", f.unboundInstances},
                                        {"note", "baked local-detail from FinalAlbion_RT.stb; LOD0 meshes"}}}}));
}

} // namespace

std::vector<uint8_t> buildGlbWith(const te::Scene& terrain, const std::vector<const Scene*>& layers) {
    glb::Builder b;
    std::vector<int> roots{te::appendTerrain(b, terrain)};
    for (const Scene* layer : layers) if (layer) appendFoliage(b, *layer, terrain.up, roots);
    return b.finish(terrain.sourceName, roots, "Albion Atlas");
}

std::vector<fs::path> writeGlbWith(const te::Scene& terrain, const std::vector<const Scene*>& layers, const fs::path& out) {
    const auto glb = buildGlbWith(terrain, layers);
    if (!out.parent_path().empty()) fs::create_directories(out.parent_path());
    std::ofstream f(out, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write " + out.string());
    f.write(reinterpret_cast<const char*>(glb.data()), std::streamsize(glb.size()));
    std::vector<fs::path> written{out};
    for (auto& p : te::writeLayerSidecars(terrain, out)) written.push_back(p);
    return written;
}

std::vector<uint8_t> buildGlbWithFoliage(const te::Scene& terrain, const Scene& foliage) { return buildGlbWith(terrain, {&foliage}); }
std::vector<fs::path> writeGlbWithFoliage(const te::Scene& terrain, const Scene& foliage, const fs::path& out) { return writeGlbWith(terrain, {&foliage}, out); }

namespace {

void appendObjLayer(const te::Scene& terrain, const Scene& foliage, const fs::path& out,
                    uint32_t& base, std::vector<fs::path>& written) {
    if (foliage.instances.empty()) return;
    const std::string stem = out.stem().string();
    const fs::path mtlPath = out.parent_path() / (stem + ".mtl");
    std::ofstream obj(out, std::ios::app), mtl(mtlPath, std::ios::app);
    obj << "o " << stem << "_" << foliage.rootName << "\n";
    // One material per texture image.
    std::map<int, std::string> imageMaterial;
    for (const Mesh& m : foliage.meshes) {
        if (m.instanceCount == 0) continue;
        for (const SubMesh& part : m.parts) {
            if (part.image < 0 || imageMaterial.count(part.image)) continue;
            const std::string matName = lower(foliage.rootName) + "_" + std::to_string(part.diffuseTexture);
            const fs::path png = out.parent_path() / (stem + "_" + matName + ".png");
            const auto bytes = te::encodePng(foliage.images[size_t(part.image)]);
            std::ofstream(png, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
            written.push_back(png);
            mtl << "newmtl " << matName << "\nKa 1 1 1\nKd 1 1 1\nKs 0 0 0\nd 1\nillum 1\nmap_Kd " << png.filename().string() << "\n";
            if (part.hasAlpha) mtl << "map_d " << png.filename().string() << "\n";
            imageMaterial[part.image] = matName;
        }
    }
    mtl << "newmtl " << lower(foliage.rootName) << "_untextured\nKd 0.35 0.55 0.3\n";
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
        float col[3][3]; instanceBasis(inst, col);
        for (const auto& v : m.geometry.vertices) {
            const float wx = inst.x + v.x * col[0][0] + v.y * col[1][0] + v.z * col[2][0];
            const float wy = inst.y + v.x * col[0][1] + v.y * col[1][1] + v.z * col[2][1];
            const float wz = inst.z + v.x * col[0][2] + v.y * col[1][2] + v.z * col[2][2];
            float px, py, pz, nx, ny, nz;
            toUp(terrain.up, wx, wy, wz, px, py, pz);
            toUp(terrain.up, v.nx * col[0][0] + v.ny * col[1][0] + v.nz * col[2][0],
                 v.nx * col[0][1] + v.ny * col[1][1] + v.nz * col[2][1],
                 v.nx * col[0][2] + v.ny * col[1][2] + v.nz * col[2][2], nx, ny, nz);
            std::snprintf(line, sizeof line, "v %.4f %.4f %.4f\nvt %.5f %.5f\nvn %.4f %.4f %.4f\n", px, py, pz, v.u, 1.0f - v.v, nx, ny, nz);
            obj << line;
        }
        for (const SubMesh& part : m.parts) {
            if (part.image != currentImage) {
                currentImage = part.image;
                obj << "usemtl " << (part.image >= 0 ? imageMaterial[part.image] : lower(foliage.rootName) + "_untextured") << "\n";
            }
            for (size_t k = 0; k + 2 < part.indices.size(); k += 3) {
                const uint32_t a = base + part.indices[k] + 1, bb = base + part.indices[k + 1] + 1, c = base + part.indices[k + 2] + 1;
                std::snprintf(line, sizeof line, "f %u/%u/%u %u/%u/%u %u/%u/%u\n", a, a, a, bb, bb, bb, c, c, c);
                obj << line;
            }
        }
        base += uint32_t(m.geometry.vertices.size());
    }
}

} // namespace

std::vector<fs::path> writeObjWith(const te::Scene& terrain, const std::vector<const Scene*>& layers, const fs::path& out) {
    auto written = te::writeObj(terrain, out);
    uint32_t base = uint32_t(terrain.vertices.size());
    for (const Scene* layer : layers) if (layer) appendObjLayer(terrain, *layer, out, base, written);
    return written;
}

std::vector<fs::path> writeObjWithFoliage(const te::Scene& terrain, const Scene& foliage, const fs::path& out) {
    return writeObjWith(terrain, {&foliage}, out);
}

} // namespace albion::foliageexport
