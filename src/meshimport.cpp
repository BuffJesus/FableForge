#include "meshimport.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <tuple>
#include <stdexcept>

#include "nlohmann/json.hpp"

#include "../vendor/embedded_schema.hpp"
#include "backups.hpp"
#include "forge/big.hpp"
#include "forge/bin.hpp"
#include "forge/defedit.hpp"
#include "forge/defschema.hpp"
#include "forge/meshpreview.hpp"
#include "forge/terraintex.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;
using forge::meshcompose::Primitive;
using forge::meshcompose::Vec2;
using forge::meshcompose::Vec3;

namespace albion::meshimport {

namespace {

std::vector<uint8_t> readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + p.string());
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// glTF Y-up -> Fable Z-up: the inverse of the exporter's (x, z, -y)
Vec3 toFable(float x, float y, float z) { return {x, -z, y}; }

// ---------------------------------------------------------------- glTF 2.0

struct GltfBuffers { json doc; std::vector<std::vector<uint8_t>> buffers; };

GltfBuffers openGltf(const fs::path& path) {
    GltfBuffers g;
    const auto bytes = readFile(path);
    std::vector<uint8_t> glbBin;
    bool glb = bytes.size() >= 12 && std::memcmp(bytes.data(), "glTF", 4) == 0;
    if (glb) {
        auto u32 = [&](size_t o) { uint32_t v; std::memcpy(&v, bytes.data() + o, 4); return v; };
        const uint32_t jl = u32(12);
        if (20 + jl > bytes.size() || u32(16) != 0x4E4F534A) throw std::runtime_error("malformed .glb (JSON chunk)");
        g.doc = json::parse(bytes.begin() + 20, bytes.begin() + 20 + jl);
        const size_t bo = 20 + jl;
        if (bo + 8 <= bytes.size() && u32(bo + 4) == 0x004E4942) {
            const uint32_t bl = u32(bo);
            glbBin.assign(bytes.begin() + bo + 8, bytes.begin() + std::min(bytes.size(), bo + 8 + size_t(bl)));
        }
    } else {
        g.doc = json::parse(bytes.begin(), bytes.end());
    }
    for (const auto& b : g.doc.value("buffers", json::array())) {
        if (b.contains("uri")) {
            const std::string uri = b["uri"].get<std::string>();
            if (uri.rfind("data:", 0) == 0) {
                const size_t comma = uri.find(',');
                if (comma == std::string::npos) throw std::runtime_error("bad data: URI in glTF");
                // base64
                static const std::string tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
                std::vector<uint8_t> out; int val = 0, bits = -8;
                for (size_t i = comma + 1; i < uri.size(); ++i) {
                    const char ch = uri[i];
                    if (ch == '=') break;
                    const size_t pos = tbl.find(ch);
                    if (pos == std::string::npos) continue;
                    val = (val << 6) + int(pos); bits += 6;
                    if (bits >= 0) { out.push_back(uint8_t((val >> bits) & 0xFF)); bits -= 8; }
                }
                g.buffers.push_back(std::move(out));
            } else {
                g.buffers.push_back(readFile(path.parent_path() / uri));
            }
        } else {
            g.buffers.push_back(glbBin);
        }
    }
    return g;
}

// one accessor as floats (any component type, normalised or not), `comps` per element
std::vector<float> readAccessor(const GltfBuffers& g, int index, int& comps) {
    const auto& acc = g.doc["accessors"].at(size_t(index));
    const std::string type = acc.value("type", "SCALAR");
    comps = type == "SCALAR" ? 1 : type == "VEC2" ? 2 : type == "VEC3" ? 3 : type == "VEC4" ? 4 : 0;
    if (!comps) throw std::runtime_error("unsupported accessor type " + type);
    const size_t count = acc.value("count", 0);
    const int ct = acc.value("componentType", 5126);
    const bool norm = acc.value("normalized", false);
    std::vector<float> out(count * size_t(comps), 0.0f);
    if (!acc.contains("bufferView")) return out;   // all zeros (sparse not supported)
    const auto& bv = g.doc["bufferViews"].at(acc["bufferView"].get<size_t>());
    const auto& buf = g.buffers.at(bv.value("buffer", 0));
    const size_t csize = ct == 5126 || ct == 5125 ? 4 : ct == 5123 || ct == 5122 ? 2 : 1;
    const size_t stride = bv.value("byteStride", size_t(0)) ? bv.value("byteStride", size_t(0)) : csize * size_t(comps);
    size_t base = bv.value("byteOffset", size_t(0)) + acc.value("byteOffset", size_t(0));
    for (size_t i = 0; i < count; ++i) {
        for (int c = 0; c < comps; ++c) {
            const size_t o = base + i * stride + size_t(c) * csize;
            if (o + csize > buf.size()) throw std::runtime_error("accessor reads past its buffer");
            float v = 0;
            switch (ct) {
                case 5126: { float f; std::memcpy(&f, buf.data() + o, 4); v = f; break; }
                case 5125: { uint32_t u; std::memcpy(&u, buf.data() + o, 4); v = float(u); break; }
                case 5123: { uint16_t u; std::memcpy(&u, buf.data() + o, 2); v = norm ? u / 65535.0f : float(u); break; }
                case 5122: { int16_t s; std::memcpy(&s, buf.data() + o, 2); v = norm ? std::max(s / 32767.0f, -1.0f) : float(s); break; }
                case 5121: { v = norm ? buf[o] / 255.0f : float(buf[o]); break; }
                case 5120: { const int8_t s = int8_t(buf[o]); v = norm ? std::max(s / 127.0f, -1.0f) : float(s); break; }
                default: throw std::runtime_error("unsupported componentType " + std::to_string(ct));
            }
            out[i * size_t(comps) + size_t(c)] = v;
        }
    }
    return out;
}

// node transforms: apply the scene's world matrix to each mesh instance (column-major 4x4)
using Mat4 = std::array<float, 16>;
Mat4 identity() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }
Mat4 mul(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c = 0; c < 4; ++c) for (int rr = 0; rr < 4; ++rr) { float s = 0; for (int k = 0; k < 4; ++k) s += a[size_t(k * 4 + rr)] * b[size_t(c * 4 + k)]; r[size_t(c * 4 + rr)] = s; }
    return r;
}
Mat4 nodeMatrix(const json& n) {
    if (n.contains("matrix")) { Mat4 m; for (size_t i = 0; i < 16; ++i) m[i] = n["matrix"][i].get<float>(); return m; }
    Mat4 t = identity(), r = identity(), s = identity();
    if (n.contains("translation")) { t[12] = n["translation"][0]; t[13] = n["translation"][1]; t[14] = n["translation"][2]; }
    if (n.contains("scale")) { s[0] = n["scale"][0]; s[5] = n["scale"][1]; s[10] = n["scale"][2]; }
    if (n.contains("rotation")) {
        const float x = n["rotation"][0], y = n["rotation"][1], z = n["rotation"][2], w = n["rotation"][3];
        r = {1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w), 0,
             2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w), 0,
             2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y), 0,
             0, 0, 0, 1};
    }
    return mul(mul(t, r), s);
}
void transformPoint(const Mat4& m, float& x, float& y, float& z, bool direction) {
    const float nx = m[0] * x + m[4] * y + m[8] * z + (direction ? 0 : m[12]);
    const float ny = m[1] * x + m[5] * y + m[9] * z + (direction ? 0 : m[13]);
    const float nz = m[2] * x + m[6] * y + m[10] * z + (direction ? 0 : m[14]);
    x = nx; y = ny; z = nz;
}

Model loadGltf(const fs::path& path) {
    Model model;
    const GltfBuffers g = openGltf(path);
    if (!g.doc.contains("meshes")) throw std::runtime_error("glTF has no meshes");
    std::map<std::string, int> slotOf;
    auto slotFor = [&](int matIndex) {
        std::string name = matIndex >= 0 && g.doc.contains("materials") && size_t(matIndex) < g.doc["materials"].size()
                               ? g.doc["materials"][size_t(matIndex)].value("name", "material" + std::to_string(matIndex)) : "default";
        auto it = slotOf.find(name);
        if (it != slotOf.end()) return it->second;
        const int slot = int(model.materialNames.size());
        model.materialNames.push_back(name); slotOf[name] = slot;
        return slot;
    };
    // every mesh instance in the default scene (else every node), with its world transform
    std::vector<std::pair<int, Mat4>> instances;
    std::function<void(int, const Mat4&)> visit = [&](int ni, const Mat4& parent) {
        const auto& n = g.doc["nodes"].at(size_t(ni));
        const Mat4 world = mul(parent, nodeMatrix(n));
        if (n.contains("mesh")) instances.push_back({n["mesh"].get<int>(), world});
        for (const auto& c : n.value("children", json::array())) visit(c.get<int>(), world);
    };
    if (g.doc.contains("scenes") && !g.doc["scenes"].empty()) {
        const auto& scene = g.doc["scenes"].at(size_t(g.doc.value("scene", 0)));
        for (const auto& r : scene.value("nodes", json::array())) visit(r.get<int>(), identity());
    }
    if (instances.empty()) for (size_t mi = 0; mi < g.doc["meshes"].size(); ++mi) instances.push_back({int(mi), identity()});

    for (const auto& [mi, world] : instances) {
        const auto& mesh = g.doc["meshes"].at(size_t(mi));
        for (const auto& prim : mesh.value("primitives", json::array())) {
            if (prim.value("mode", 4) != 4) { model.notes.push_back("skipped a non-triangle primitive (mode " + std::to_string(prim.value("mode", 4)) + ")"); continue; }
            const auto& attrs = prim["attributes"];
            if (!attrs.contains("POSITION")) continue;
            int comps = 0;
            const auto pos = readAccessor(g, attrs["POSITION"].get<int>(), comps);
            if (comps != 3) throw std::runtime_error("POSITION is not VEC3");
            const size_t nv = pos.size() / 3;
            Primitive p;
            p.material = slotFor(prim.value("material", -1));
            p.verts.reserve(nv);
            for (size_t i = 0; i < nv; ++i) {
                float x = pos[i * 3], y = pos[i * 3 + 1], z = pos[i * 3 + 2];
                transformPoint(world, x, y, z, false);
                p.verts.push_back(toFable(x, y, z));
            }
            if (attrs.contains("NORMAL")) {
                const auto nrm = readAccessor(g, attrs["NORMAL"].get<int>(), comps);
                if (comps == 3 && nrm.size() / 3 == nv) {
                    p.normals.reserve(nv);
                    for (size_t i = 0; i < nv; ++i) { float x = nrm[i * 3], y = nrm[i * 3 + 1], z = nrm[i * 3 + 2]; transformPoint(world, x, y, z, true); p.normals.push_back(toFable(x, y, z)); }
                }
            }
            if (attrs.contains("TEXCOORD_0")) {
                const auto uv = readAccessor(g, attrs["TEXCOORD_0"].get<int>(), comps);
                if (comps == 2 && uv.size() / 2 == nv) { p.uvs.reserve(nv); for (size_t i = 0; i < nv; ++i) p.uvs.push_back({uv[i * 2], uv[i * 2 + 1]}); }
            }
            if (prim.contains("indices")) {
                const auto idx = readAccessor(g, prim["indices"].get<int>(), comps);
                for (size_t i = 0; i + 2 < idx.size(); i += 3) p.faces.push_back({uint32_t(idx[i]), uint32_t(idx[i + 1]), uint32_t(idx[i + 2])});
            } else {
                for (uint32_t i = 0; i + 2 < nv; i += 3) p.faces.push_back({i, i + 1, i + 2});
            }
            // a mirrored node transform flips the winding
            const float det = world[0] * (world[5] * world[10] - world[9] * world[6]) - world[4] * (world[1] * world[10] - world[9] * world[2]) + world[8] * (world[1] * world[6] - world[5] * world[2]);
            if (det < 0) for (auto& f : p.faces) std::swap(f[1], f[2]);
            if (!p.faces.empty()) model.prims.push_back(std::move(p));
        }
    }
    if (model.materialNames.empty()) model.materialNames.push_back("default");
    return model;
}

// ---------------------------------------------------------------- OBJ

Model loadObj(const fs::path& path) {
    Model model;
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot read " + path.string());
    std::vector<Vec3> v, vn; std::vector<Vec2> vt;
    struct Key { int p, t, n; bool operator<(const Key& o) const { return std::tie(p, t, n) < std::tie(o.p, o.t, o.n); } };
    std::map<std::string, int> slotOf;
    std::map<int, Primitive> prims;          // slot -> primitive
    std::map<int, std::map<Key, uint32_t>> dedupe;
    int slot = 0;
    auto ensureSlot = [&](const std::string& name) {
        auto it = slotOf.find(name);
        if (it != slotOf.end()) return it->second;
        const int s = int(model.materialNames.size());
        model.materialNames.push_back(name); slotOf[name] = s;
        return s;
    };
    slot = ensureSlot("default");
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream ls(line);
        std::string tag; ls >> tag;
        if (tag == "v") { float x, y, z; ls >> x >> y >> z; v.push_back(toFable(x, y, z)); }
        else if (tag == "vt") { float u, w; ls >> u >> w; vt.push_back({u, 1.0f - w}); }   // OBJ v is bottom-up; glTF/Blender convention is top-down
        else if (tag == "vn") { float x, y, z; ls >> x >> y >> z; vn.push_back(toFable(x, y, z)); }
        else if (tag == "usemtl") { std::string n; ls >> n; slot = ensureSlot(n.empty() ? "default" : n); }
        else if (tag == "f") {
            std::vector<uint32_t> poly;
            std::string tok;
            while (ls >> tok) {
                int p = 0, t = 0, n = 0;
                const size_t s1 = tok.find('/');
                p = std::stoi(tok.substr(0, s1));
                if (s1 != std::string::npos) {
                    const size_t s2 = tok.find('/', s1 + 1);
                    const std::string ts = tok.substr(s1 + 1, s2 == std::string::npos ? std::string::npos : s2 - s1 - 1);
                    if (!ts.empty()) t = std::stoi(ts);
                    if (s2 != std::string::npos && s2 + 1 < tok.size()) n = std::stoi(tok.substr(s2 + 1));
                }
                auto fix = [](int i, size_t count) { return i < 0 ? int(count) + i : i - 1; };
                const Key k{fix(p, v.size()), t ? fix(t, vt.size()) : -1, n ? fix(n, vn.size()) : -1};
                if (k.p < 0 || size_t(k.p) >= v.size()) throw std::runtime_error("OBJ face references a missing vertex");
                auto& prim = prims[slot];
                auto& dd = dedupe[slot];
                auto it = dd.find(k);
                if (it == dd.end()) {
                    it = dd.emplace(k, uint32_t(prim.verts.size())).first;
                    prim.verts.push_back(v[size_t(k.p)]);
                    prim.uvs.push_back(k.t >= 0 && size_t(k.t) < vt.size() ? vt[size_t(k.t)] : Vec2{});
                    if (k.n >= 0 && size_t(k.n) < vn.size()) prim.normals.push_back(vn[size_t(k.n)]);
                }
                poly.push_back(it->second);
            }
            auto& prim = prims[slot];
            for (size_t i = 1; i + 1 < poly.size(); ++i) prim.faces.push_back({poly[0], poly[i], poly[i + 1]});   // fan
        }
    }
    for (auto& [s, prim] : prims) {
        if (prim.faces.empty()) continue;
        if (prim.normals.size() != prim.verts.size()) prim.normals.clear();   // partial normals: recompute
        prim.material = s;
        model.prims.push_back(std::move(prim));
    }
    return model;
}

bool validName(const std::string& n) {
    if (n.empty() || n.size() >= 80) return false;
    for (char c : n) if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

} // namespace

Model loadModel(const fs::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (ext == ".glb" || ext == ".gltf") return loadGltf(path);
    if (ext == ".obj") return loadObj(path);
    throw std::runtime_error("unsupported model format " + ext + " (.glb, .gltf or .obj)");
}

bool importModel(const fs::path& gameRoot, const ImportRequest& req, ImportResult& out, std::string& error) {
    out = ImportResult{};
    if (!validName(req.name)) { error = "name must be A-Z, 0-9 and _ (got '" + req.name + "')"; return false; }
    if (backups::gameRunningIn(gameRoot)) { error = "Fable is running from this install; quit to the desktop first"; return false; }
    std::error_code ec;
    if (!fs::exists(req.model, ec)) { error = "no such model: " + req.model.string(); return false; }
    if (!req.texturePng.empty() && !fs::exists(req.texturePng, ec)) { error = "no such image: " + req.texturePng.string(); return false; }
    try {
        const fs::path defsDir = gameRoot / "data" / "CompiledDefs";
        const fs::path namesBin = defsDir / "names.bin", gameBin = defsDir / "game.bin";
        const fs::path texBig = gameRoot / "data" / "graphics" / "pc" / "textures.big";
        fs::path gfxBig = gameRoot / "data" / "graphics" / "graphics.big";
        if (!fs::exists(gfxBig, ec)) gfxBig = gameRoot / "data" / "graphics" / "pc" / "graphics.big";
        if (!fs::exists(gfxBig, ec)) { error = "no graphics.big under " + (gameRoot / "data" / "graphics").string(); return false; }
        out.meshName = "MESH_" + req.name;
        out.objectName = "OBJECT_" + req.name;

        auto defs = forge::bin::File::open(namesBin, gameBin);
        if (defs.find(out.objectName)) { error = "game.bin already has a def named " + out.objectName; return false; }
        const auto* donor = defs.find(req.donor);
        if (!donor) { error = "no OBJECT named " + req.donor + " to copy from"; return false; }
        if (donor->definition != "OBJECT") { error = req.donor + " is a " + donor->definition + ", not an OBJECT"; return false; }

        // 1. the model, composed
        Model model = loadModel(req.model);
        if (model.prims.empty()) { error = "the model has no triangles"; return false; }
        for (const auto& n : model.notes) out.notes.push_back(n);

        // 2. the texture: an appended GBANK_MAIN_PC entry, or the id the caller names
        out.textureId = req.textureId;
        if (!req.texturePng.empty()) {
            const std::string symbol = req.name + "_DIFFUSE";
            {
                const auto tex = forge::big::File::open(texBig);
                const auto* bank = tex.findBank("GBANK_MAIN_PC");
                if (!bank) { error = "textures.big has no GBANK_MAIN_PC"; return false; }
                for (const auto& e : bank->entries) if (e.name == symbol) { error = "textures.big already has an entry named " + symbol; return false; }
            }
            if (!backups::backupOnce(texBig, error)) return false;
            forge::terraintex::ImportRequest ir;
            ir.png = req.texturePng; ir.srcBig = texBig; ir.outBig = texBig.string() + ".forge-tmp";
            ir.entryName = symbol; ir.subBank = "GBANK_MAIN_PC"; ir.format = "dxt1"; ir.add = true;
            const auto r = forge::terraintex::importPng(ir);
            if (!r.ok) { error = "texture import failed: " + r.output; fs::remove(ir.outBig, ec); return false; }
            fs::rename(ir.outBig, texBig);
            out.textureId = uint32_t(r.entryId);
            out.notes.push_back("textures.big: appended " + symbol + " (id " + std::to_string(out.textureId) + ", DXT1) from " + req.texturePng.string());
        }
        std::vector<forge::meshcompose::Material> materials;
        for (const auto& n : model.materialNames) {
            forge::meshcompose::Material m;
            m.name = out.meshName + "_" + n;
            m.diffuseId = int32_t(out.textureId);
            materials.push_back(m);
        }
        const auto composed = forge::meshcompose::composeStatic(out.meshName, model.prims, materials, req.compress, 0);
        out.vertices = composed.vertices; out.triangles = composed.triangles; out.primitives = model.prims.size();
        // the decoder must read back what we wrote (the same reader the preview and thumbnails use)
        const auto check = forge::meshpreview::decodeLod0(composed.payload, 1);
        if (check.vertices.size() != composed.vertices || check.triangles.size() != composed.triangles) {
            error = "composed mesh does not decode back (" + std::to_string(check.vertices.size()) + "/" + std::to_string(check.triangles.size()) + " of " +
                    std::to_string(composed.vertices) + "/" + std::to_string(composed.triangles) + ")";
            return false;
        }

        // 3. graphics.big: appended MBANK_ALLMESHES entry with the next id
        {
            auto gfx = forge::big::File::open(gfxBig);
            auto* bank = gfx.findBank("MBANK_ALLMESHES");
            if (!bank) { error = "graphics.big has no MBANK_ALLMESHES"; return false; }
            if (bank->entries.empty()) { error = "MBANK_ALLMESHES is empty"; return false; }
            for (const auto& e : bank->entries) if (e.name == out.meshName) { error = "graphics.big already has a mesh named " + out.meshName; return false; }
            uint32_t maxId = 0;
            const forge::big::Entry* modelEntry = nullptr;
            for (const auto& e : bank->entries) { maxId = std::max(maxId, e.id); if (e.type == 1 && !modelEntry) modelEntry = &e; }
            if (!modelEntry) modelEntry = &bank->entries.back();
            forge::big::Entry e;
            e.magic = modelEntry->magic; e.devFileType = modelEntry->devFileType; e.type = 1;
            e.id = maxId + 1;
            e.name = out.meshName;
            e.subHeader = composed.info;
            e.data = composed.payload;
            e.length = uint32_t(composed.payload.size());
            bank->entries.push_back(std::move(e));
            out.meshId = maxId + 1;
            if (!backups::backupOnce(gfxBig, error)) return false;
            const auto bytes = gfx.serialize();
            const fs::path tmp = gfxBig.string() + ".forge-tmp";
            { std::ofstream o(tmp, std::ios::binary | std::ios::trunc); o.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size())); if (!o) { error = "cannot write " + tmp.string(); return false; } }
            fs::rename(tmp, gfxBig);
            out.notes.push_back("graphics.big: appended " + out.meshName + " (id " + std::to_string(out.meshId) + ", " + std::to_string(out.vertices) + " vertices, " +
                                std::to_string(out.triangles) + " triangles, " + std::to_string(out.primitives) + " primitive(s), " + std::to_string(composed.payload.size()) + " bytes)");
        }

        // 4. the OBJECT def: the donor's bytes, Graphic.modelId repointed, the mesh size from the bounds
        const size_t index = defs.addEntry("OBJECT", out.objectName, donor->data);
        const auto schema = forge::defschema::Schema::loadText(kEmbeddedDefSchema, "embedded");
        auto graphic = forge::defedit::getFieldBytes(defs, schema, out.objectName, "Graphic");
        if (graphic.size() < 8) { error = "the donor's Graphic field is not a CEngineGraphic"; return false; }
        std::memcpy(graphic.data() + 4, &out.meshId, 4);
        forge::defedit::setFieldBytes(defs, schema, out.objectName, "Graphic", graphic);
        const float height = composed.bbMax.z - composed.bbMin.z;
        const float radius = 0.5f * std::max(composed.bbMax.x - composed.bbMin.x, composed.bbMax.y - composed.bbMin.y);
        auto setf = [&](const char* field, float value) {
            try { forge::defedit::setField(defs, schema, out.objectName, field, std::to_string(value)); }
            catch (const std::exception& e) { out.notes.push_back(std::string("game.bin: ") + field + " left as the donor's (" + e.what() + ")"); }
        };
        setf("MeshHeight", height); setf("ApproxMaxMeshHeight", height); setf("MeshRadius", radius);
        if (!backups::backupOnce(namesBin, error) || !backups::backupOnce(gameBin, error)) return false;
        defs.save(namesBin, gameBin);
        out.defIndex = index;
        out.notes.push_back("game.bin: appended OBJECT " + out.objectName + " (def index " + std::to_string(index) + ", a copy of " + req.donor + " with Graphic.modelId " +
                            std::to_string(out.meshId) + ", height " + std::to_string(height) + ", radius " + std::to_string(radius) + ")");
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

} // namespace albion::meshimport
