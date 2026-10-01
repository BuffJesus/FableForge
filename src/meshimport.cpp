#include "meshimport.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <tuple>
#include <type_traits>
#include <stdexcept>

#include "nlohmann/json.hpp"

#include "../vendor/embedded_schema.hpp"
#include "backups.hpp"
#include "pendingbanks.hpp"
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

// glTF Y-up metres -> Fable Z-up centimetres: the inverse of the exporter's (x, z, -y) and its
// 0.01 mesh scale (retail's barrel is z 0..132 in its mesh; 1 world unit = 1 m = 100 mesh units)
constexpr float kMetresToMesh = 100.0f;
Vec3 toFable(float x, float y, float z) { return {x * kMetresToMesh, -z * kMetresToMesh, y * kMetresToMesh}; }
Vec3 toFableDir(float x, float y, float z) { return {x, -z, y}; }

// ---------------------------------------------------------------- glTF 2.0

struct GltfBuffers { json doc; std::vector<std::vector<uint8_t>> buffers; };

size_t gltfInteger(const json& value, const char* key) {
    if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<int64_t>() < 0))
        throw std::runtime_error(std::string("invalid nonnegative integer: ") + key);
    const auto number = value.get<uint64_t>();
    if (number > std::numeric_limits<size_t>::max()) throw std::runtime_error(std::string("size overflow: ") + key);
    return size_t(number);
}
size_t gltfSize(const json& object, const char* key, size_t fallback = 0) {
    return object.contains(key) ? gltfInteger(object.at(key), key) : fallback;
}

GltfBuffers openGltf(const fs::path& path) {
    GltfBuffers g;
    const auto bytes = readFile(path);
    std::vector<uint8_t> glbBin;
    const bool glb = bytes.size() >= 4 && std::memcmp(bytes.data(), "glTF", 4) == 0;
    if (glb) {
        // Khronos GLB 2.0: 12-byte header, JSON first, optional BIN second,
        // then unknown extension chunks. Validate ranges before reading them.
        if (bytes.size() < 20) throw std::runtime_error("malformed .glb (incomplete header or JSON chunk header)");
        auto u32 = [&](size_t o) {
            return uint32_t(bytes[o]) | (uint32_t(bytes[o + 1]) << 8) |
                   (uint32_t(bytes[o + 2]) << 16) | (uint32_t(bytes[o + 3]) << 24);
        };
        if (u32(4) != 2) throw std::runtime_error("unsupported .glb container version (expected 2)");
        if (u32(8) != bytes.size()) throw std::runtime_error("malformed .glb (declared length differs from file size)");
        size_t offset = 12, chunkIndex = 0;
        while (offset < bytes.size()) {
            if (bytes.size() - offset < 8) throw std::runtime_error("malformed .glb (incomplete chunk header)");
            const size_t length = u32(offset);
            const uint32_t type = u32(offset + 4);
            offset += 8;
            if (length % 4 || length > bytes.size() - offset)
                throw std::runtime_error("malformed .glb (unaligned or truncated chunk)");
            if (chunkIndex == 0) {
                if (type != 0x4E4F534A) throw std::runtime_error("malformed .glb (first chunk is not JSON)");
                g.doc = json::parse(bytes.begin() + offset, bytes.begin() + offset + length);
            } else if (type == 0x4E4F534A) {
                throw std::runtime_error("malformed .glb (duplicate JSON chunk)");
            } else if (type == 0x004E4942) {
                if (chunkIndex != 1) throw std::runtime_error("malformed .glb (BIN must be the second chunk)");
                glbBin.assign(bytes.begin() + offset, bytes.begin() + offset + length);
            }
            offset += length;
            ++chunkIndex;
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
        const size_t length = gltfSize(b, "byteLength");
        if (!length || length > g.buffers.back().size()) throw std::runtime_error("glTF buffer has an invalid declared length");
        // Padding and extra external-file bytes are not part of the logical buffer.
        g.buffers.back().resize(length);
    }
    return g;
}

// Attributes use floats; triangle indices retain their unsigned integer values.
template<typename Value = float>
std::vector<Value> readAccessor(const GltfBuffers& g, size_t index, int& comps) {
    const auto& acc = g.doc["accessors"].at(size_t(index));
    const std::string type = acc.value("type", "SCALAR");
    comps = type == "SCALAR" ? 1 : type == "VEC2" ? 2 : type == "VEC3" ? 3 : type == "VEC4" ? 4 : 0;
    if (!comps) throw std::runtime_error("unsupported accessor type " + type);
    const size_t count = gltfSize(acc, "count");
    const size_t ct = gltfSize(acc, "componentType", 5126);
    const bool norm = acc.value("normalized", false);
    if constexpr (std::is_same_v<Value, uint32_t>) {
        if (comps != 1 || norm || (ct != 5121 && ct != 5123 && ct != 5125))
            throw std::runtime_error("triangle indices must be unnormalized unsigned SCALAR values");
    }
    if (!count || count > std::vector<Value>().max_size() / size_t(comps))
        throw std::runtime_error("invalid accessor count");
    if (ct != 5120 && ct != 5121 && ct != 5122 && ct != 5123 && ct != 5125 && ct != 5126)
        throw std::runtime_error("unsupported componentType " + std::to_string(ct));
    if (acc.contains("sparse")) throw std::runtime_error("sparse glTF accessors are not supported");
    if (!acc.contains("bufferView")) return std::vector<Value>(count * size_t(comps), Value{});
    const auto& bv = g.doc["bufferViews"].at(gltfSize(acc, "bufferView"));
    const auto& buf = g.buffers.at(gltfSize(bv, "buffer"));
    const size_t csize = ct == 5126 || ct == 5125 ? 4 : ct == 5123 || ct == 5122 ? 2 : 1;
    const size_t element = csize * size_t(comps);
    const size_t stride = gltfSize(bv, "byteStride", element);
    const size_t viewOffset = gltfSize(bv, "byteOffset"), viewLength = gltfSize(bv, "byteLength");
    const size_t accessorOffset = gltfSize(acc, "byteOffset");
    if (!viewLength || viewOffset > buf.size() || viewLength > buf.size() - viewOffset)
        throw std::runtime_error("buffer view reads past its buffer");
    if (stride < element || stride % csize || (bv.contains("byteStride") && (stride < 4 || stride > 252 || stride % 4)))
        throw std::runtime_error("invalid accessor stride");
    if (accessorOffset % csize || viewOffset % csize)
        throw std::runtime_error("unaligned accessor offset");
    if (accessorOffset > viewLength || element > viewLength - accessorOffset ||
        count - 1 > (viewLength - accessorOffset - element) / stride)
        throw std::runtime_error("accessor reads past its buffer view");
    const size_t base = viewOffset + accessorOffset;
    std::vector<Value> out(count * size_t(comps), Value{});
    for (size_t i = 0; i < count; ++i) {
        for (int c = 0; c < comps; ++c) {
            const size_t o = base + i * stride + size_t(c) * csize;
            if (o + csize > buf.size()) throw std::runtime_error("accessor reads past its buffer");
            double v = 0; // All uint32 values are exact here; never route indices through float.
            switch (ct) {
                case 5126: { float f; std::memcpy(&f, buf.data() + o, 4); v = f; break; }
                case 5125: { uint32_t u; std::memcpy(&u, buf.data() + o, 4); v = u; break; }
                case 5123: { uint16_t u; std::memcpy(&u, buf.data() + o, 2); v = norm ? u / 65535.0f : float(u); break; }
                case 5122: { int16_t s; std::memcpy(&s, buf.data() + o, 2); v = norm ? std::max(s / 32767.0f, -1.0f) : float(s); break; }
                case 5121: { v = norm ? buf[o] / 255.0f : float(buf[o]); break; }
                case 5120: { const int8_t s = int8_t(buf[o]); v = norm ? std::max(s / 127.0f, -1.0f) : float(s); break; }
                default: throw std::runtime_error("unsupported componentType " + std::to_string(ct));
            }
            if constexpr (std::is_same_v<Value, uint32_t>) {
                const uint32_t maximum = ct == 5121 ? 255u : ct == 5123 ? 65535u : 0xffffffffu;
                if (v == maximum) throw std::runtime_error("triangle index uses the reserved maximum value");
            }
            out[i * size_t(comps) + size_t(c)] = Value(v);
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
    for (const auto& [key, count] : {std::pair{"matrix", 16u}, {"translation", 3u}, {"scale", 3u}, {"rotation", 4u}}) {
        if (!n.contains(key)) continue;
        const auto& values = n.at(key);
        if (!values.is_array() || values.size() != count) throw std::runtime_error(std::string("invalid node ") + key);
        for (const auto& value : values)
            if (!value.is_number() || !std::isfinite(value.get<float>())) throw std::runtime_error(std::string("invalid node ") + key);
    }
    if (n.contains("matrix") && (n.contains("translation") || n.contains("scale") || n.contains("rotation")))
        throw std::runtime_error("node combines matrix and TRS transforms");
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
    // Traverse the selected scene iteratively: malformed cycles and deep valid
    // hierarchies must not exhaust the process stack. Preserve depth-first order.
    std::vector<std::pair<size_t, Mat4>> instances;
    if (g.doc.contains("scenes") && !g.doc["scenes"].empty()) {
        const size_t sceneIndex = gltfSize(g.doc, "scene");
        if (!g.doc["scenes"].is_array() || sceneIndex >= g.doc["scenes"].size())
            throw std::runtime_error("invalid default scene index");
        const auto& scene = g.doc["scenes"][sceneIndex];
        const auto nodes = g.doc.value("nodes", json::array());
        if (!nodes.is_array()) throw std::runtime_error("nodes is not an array");
        std::vector<bool> visited(nodes.size(), false);
        std::vector<std::pair<size_t, Mat4>> pending;
        auto enqueue = [&](const json& children, const Mat4& parent) {
            if (!children.is_array()) throw std::runtime_error("node list is not an array");
            for (auto it = children.rbegin(); it != children.rend(); ++it)
                pending.emplace_back(gltfInteger(*it, "node index"), parent);
        };
        enqueue(scene.value("nodes", json::array()), identity());
        while (!pending.empty()) {
            const auto [ni, parent] = pending.back(); pending.pop_back();
            if (ni >= nodes.size()) throw std::runtime_error("scene references a missing node");
            const auto& n = nodes[ni];
            if (visited[ni]) throw std::runtime_error("scene contains a cycle or repeated node");
            visited[ni] = true;
            const Mat4 world = mul(parent, nodeMatrix(n));
            if (!std::all_of(world.begin(), world.end(), [](float v) { return std::isfinite(v); }))
                throw std::runtime_error("node world transform is not finite");
            if (n.contains("mesh")) instances.emplace_back(gltfSize(n, "mesh"), world);
            enqueue(n.value("children", json::array()), world);
        }
    } else {
        // With no scenes, import the asset as a mesh library.
        for (size_t mi = 0; mi < g.doc["meshes"].size(); ++mi) instances.emplace_back(mi, identity());
    }

    for (const auto& [mi, world] : instances) {
        const auto& mesh = g.doc["meshes"].at(size_t(mi));
        for (const auto& prim : mesh.value("primitives", json::array())) {
            if (prim.value("mode", 4) != 4) { model.notes.push_back("skipped a non-triangle primitive (mode " + std::to_string(prim.value("mode", 4)) + ")"); continue; }
            const auto& attrs = prim["attributes"];
            if (!attrs.contains("POSITION")) continue;
            int comps = 0;
            const auto pos = readAccessor(g, gltfSize(attrs, "POSITION"), comps);
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
                const auto nrm = readAccessor(g, gltfSize(attrs, "NORMAL"), comps);
                if (comps == 3 && nrm.size() / 3 == nv) {
                    p.normals.reserve(nv);
                    for (size_t i = 0; i < nv; ++i) { float x = nrm[i * 3], y = nrm[i * 3 + 1], z = nrm[i * 3 + 2]; transformPoint(world, x, y, z, true); p.normals.push_back(toFableDir(x, y, z)); }
                }
            }
            if (attrs.contains("TEXCOORD_0")) {
                const auto uv = readAccessor(g, gltfSize(attrs, "TEXCOORD_0"), comps);
                if (comps == 2 && uv.size() / 2 == nv) { p.uvs.reserve(nv); for (size_t i = 0; i < nv; ++i) p.uvs.push_back({uv[i * 2], uv[i * 2 + 1]}); }
            }
            if (prim.contains("indices")) {
                const auto idx = readAccessor<uint32_t>(g, gltfSize(prim, "indices"), comps);
                if (idx.size() % 3) throw std::runtime_error("incomplete indexed triangle");
                for (const auto value : idx)
                    if (value >= nv) throw std::runtime_error("glTF index " + std::to_string(value) + " references a missing vertex");
                for (size_t i = 0; i < idx.size(); i += 3) p.faces.push_back({idx[i], idx[i + 1], idx[i + 2]});
            } else {
                if (nv % 3 || nv > std::numeric_limits<uint32_t>::max()) throw std::runtime_error("invalid non-indexed triangle count");
                for (size_t i = 0; i < nv; i += 3) p.faces.push_back({uint32_t(i), uint32_t(i + 1), uint32_t(i + 2)});
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
    struct Key { size_t p, t, n; bool operator<(const Key& o) const { return std::tie(p, t, n) < std::tie(o.p, o.t, o.n); } };
    constexpr size_t absent = std::numeric_limits<size_t>::max();
    auto index = [](const std::string& text, size_t count) -> size_t {
        size_t used = 0;
        const int value = std::stoi(text, &used);
        if (used != text.size() || value == 0)
            throw std::runtime_error("invalid OBJ face index: " + text);
        const size_t magnitude = size_t(value < 0 ? -int64_t(value) : value);
        if (magnitude > count) throw std::runtime_error("OBJ face references a missing attribute: " + text);
        return value < 0 ? count - magnitude : magnitude - 1;
    };
    auto finite3 = [](const Vec3& value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    };
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
        if (const auto comment = line.find('#'); comment != std::string::npos) line.resize(comment);
        std::istringstream ls(line);
        std::string tag; ls >> tag;
        if (tag == "v" || tag == "vn") {
            float x = 0, y = 0, z = 0;
            if (!(ls >> x >> y >> z)) throw std::runtime_error("incomplete or invalid OBJ " + tag);
            const auto converted = tag == "v" ? toFable(x, y, z) : toFableDir(x, y, z);
            if (!finite3(converted)) throw std::runtime_error("non-finite OBJ " + tag);
            (tag == "v" ? v : vn).push_back(converted);
        }
        else if (tag == "vt") {
            float u = 0, w = 0;
            if (!(ls >> u)) throw std::runtime_error("incomplete or invalid OBJ vt");
            ls >> std::ws;
            if (!ls.eof() && !(ls >> w)) throw std::runtime_error("invalid OBJ vt");
            if (!std::isfinite(u) || !std::isfinite(w)) throw std::runtime_error("non-finite OBJ vt");
            vt.push_back({u, 1.0f - w}); // OBJ v is bottom-up; glTF/Blender convention is top-down
        }
        else if (tag == "usemtl") { std::string n; ls >> n; slot = ensureSlot(n.empty() ? "default" : n); }
        else if (tag == "f") {
            std::vector<uint32_t> poly;
            std::string tok;
            while (ls >> tok) {
                Key k{absent, absent, absent};
                const size_t s1 = tok.find('/');
                k.p = index(tok.substr(0, s1), v.size());
                if (s1 != std::string::npos) {
                    const size_t s2 = tok.find('/', s1 + 1);
                    const std::string ts = tok.substr(s1 + 1, s2 == std::string::npos ? std::string::npos : s2 - s1 - 1);
                    if (!ts.empty()) k.t = index(ts, vt.size());
                    if (s2 != std::string::npos && s2 + 1 < tok.size()) k.n = index(tok.substr(s2 + 1), vn.size());
                }
                auto& prim = prims[slot];
                auto& dd = dedupe[slot];
                auto it = dd.find(k);
                if (it == dd.end()) {
                    it = dd.emplace(k, uint32_t(prim.verts.size())).first;
                    prim.verts.push_back(v[size_t(k.p)]);
                    prim.uvs.push_back(k.t != absent ? vt[k.t] : Vec2{});
                    if (k.n != absent) prim.normals.push_back(vn[k.n]);
                }
                poly.push_back(it->second);
            }
            if (poly.size() < 3) throw std::runtime_error("OBJ face has fewer than three vertices");
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
    return importModel(gameRoot, gameRoot, req, out, error);
}

bool importModel(const fs::path& baseRoot, const fs::path& outRoot, const ImportRequest& req, ImportResult& out, std::string& error) {
    out = ImportResult{};
    std::error_code ec;
    const bool inPlace = fs::equivalent(baseRoot, outRoot, ec) || baseRoot == outRoot;
    if (!validName(req.name)) { error = "name must be A-Z, 0-9 and _ (got '" + req.name + "')"; return false; }
    if (inPlace && backups::gameRunningIn(baseRoot)) { error = "Fable is running from this install; quit to the desktop first"; return false; }
    // read: the output tree's copy when an earlier layer wrote one, else the base; write: the output tree
    auto readPath = [&](const fs::path& rel) { const fs::path o = outRoot / rel; return fs::exists(o, ec) ? o : baseRoot / rel; };
    if (!fs::exists(req.model, ec)) { error = "no such model: " + req.model.string(); return false; }
    if (!req.texturePng.empty() && !fs::exists(req.texturePng, ec)) { error = "no such image: " + req.texturePng.string(); return false; }
    try {
        const fs::path defsRel = fs::path("data") / "CompiledDefs";
        const fs::path namesIn = readPath(defsRel / "names.bin"), gameIn = readPath(defsRel / "game.bin");
        const fs::path texRel = fs::path("data") / "graphics" / "pc" / "textures.big";
        const fs::path texIn = readPath(texRel);
        fs::path gfxRel = fs::path("data") / "graphics" / "graphics.big";
        if (!fs::exists(readPath(gfxRel), ec)) gfxRel = fs::path("data") / "graphics" / "pc" / "graphics.big";
        const fs::path gfxIn = readPath(gfxRel);
        if (!fs::exists(gfxIn, ec)) { error = "no graphics.big under " + (baseRoot / "data" / "graphics").string(); return false; }
        out.meshName = "MESH_" + req.name;
        out.objectName = "OBJECT_" + req.name;

        auto defs = forge::bin::File::open(namesIn, gameIn);
        if (defs.find(out.objectName)) { error = "game.bin already has a def named " + out.objectName; return false; }
        const auto* donor = defs.find(req.donor);
        if (!donor) { error = "no OBJECT named " + req.donor + " to copy from"; return false; }
        if (donor->definition != "OBJECT") { error = req.donor + " is a " + donor->definition + ", not an OBJECT"; return false; }

        // 1. the model, composed
        Model model = loadModel(req.model);
        if (model.prims.empty()) { error = "the model has no triangles"; return false; }
        for (const auto& n : model.notes) out.notes.push_back(n);
        detail::PendingBanks pending(outRoot, ".forge-model-import-");

        // 2. the texture: an appended GBANK_MAIN_PC entry, or the id the caller names
        out.textureId = req.textureId;
        if (!req.texturePng.empty()) {
            const std::string symbol = req.name + "_DIFFUSE";
            {
                const auto tex = forge::big::File::open(texIn);
                const auto* bank = tex.findBank("GBANK_MAIN_PC");
                if (!bank) { error = "textures.big has no GBANK_MAIN_PC"; return false; }
                for (const auto& e : bank->entries) if (e.name == symbol) { error = "textures.big already has an entry named " + symbol; return false; }
            }
            forge::terraintex::ImportRequest ir;
            ir.png = req.texturePng; ir.srcBig = texIn; ir.outBig = pending.prepare(texRel);
            ir.entryName = symbol; ir.subBank = "GBANK_MAIN_PC"; ir.format = "dxt1"; ir.add = true;
            const auto r = forge::terraintex::importPng(ir);
            if (!r.ok) {
                error = "texture import failed for " + req.texturePng.string();
                for (const auto& issue : r.validation.errors) error += ": " + issue;
                if (!r.output.empty()) error += ": " + r.output;
                return false;
            }
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
        // 3a. the collision hull: the next id, so the render mesh's Info can name it
        uint32_t nextId = 0;
        {
            const auto gfx = forge::big::File::open(gfxIn);
            const auto* bank = gfx.findBank("MBANK_ALLMESHES");
            if (!bank || bank->entries.empty()) { error = "graphics.big has no MBANK_ALLMESHES"; return false; }
            for (const auto& e : bank->entries) { nextId = std::max(nextId, e.id); if (e.name == out.meshName) { error = "graphics.big already has a mesh named " + out.meshName; return false; } }
            ++nextId;
        }
        std::vector<uint8_t> physics;
        if (req.collision) { physics = forge::meshcompose::composePhysics(model.prims, out.meshName); out.physicsId = nextId++; }
        const auto composed = forge::meshcompose::composeStatic(out.meshName, model.prims, materials, req.compress, int(out.physicsId));
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
            auto gfx = forge::big::File::open(gfxIn);
            auto* bank = gfx.findBank("MBANK_ALLMESHES");
            if (!bank) { error = "graphics.big has no MBANK_ALLMESHES"; return false; }
            if (bank->entries.empty()) { error = "MBANK_ALLMESHES is empty"; return false; }
            const forge::big::Entry* modelEntry = nullptr;
            for (const auto& e : bank->entries) if (e.type == 1 && !modelEntry) modelEntry = &e;
            if (!modelEntry) modelEntry = &bank->entries.back();
            const uint32_t entryMagic = modelEntry->magic, entryDevFileType = modelEntry->devFileType;
            if (!physics.empty()) {   // the hull first (its id was reserved above)
                forge::big::Entry h;
                h.magic = entryMagic; h.devFileType = entryDevFileType; h.type = 3;
                h.id = out.physicsId;
                h.name = out.meshName + "[PHYSICS]";   // retail's name for a hull
                h.data = physics;
                h.length = uint32_t(physics.size());
                bank->entries.push_back(std::move(h));
            }
            forge::big::Entry e;
            e.magic = entryMagic; e.devFileType = entryDevFileType; e.type = 1;
            e.id = nextId;
            e.name = out.meshName;
            e.subHeader = composed.info;
            e.data = composed.payload;
            e.length = uint32_t(composed.payload.size());
            bank->entries.push_back(std::move(e));
            out.meshId = nextId;
            const auto bytes = gfx.serialize();
            const fs::path tmp = pending.prepare(gfxRel);
            { std::ofstream o(tmp, std::ios::binary | std::ios::trunc); o.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size())); o.close(); if (!o) { error = "cannot write " + tmp.string(); return false; } }
            out.notes.push_back("graphics.big: appended " + out.meshName + " (id " + std::to_string(out.meshId) + ", " + std::to_string(out.vertices) + " vertices, " +
                                std::to_string(out.triangles) + " triangles, " + std::to_string(out.primitives) + " primitive(s), " + std::to_string(composed.payload.size()) + " bytes)" +
                                (physics.empty() ? std::string(", no collision hull") : " + " + out.meshName + "[PHYSICS] (id " + std::to_string(out.physicsId) + ", the model's own triangles as the hull)"));
        }

        // 4. the OBJECT def: the donor's bytes, Graphic.modelId repointed, the mesh size from the bounds
        const size_t index = defs.addEntry("OBJECT", out.objectName, donor->data);
        const auto schema = forge::defschema::Schema::loadText(kEmbeddedDefSchema, "embedded");
        auto graphic = forge::defedit::getFieldBytes(defs, schema, out.objectName, "Graphic");
        if (graphic.size() < 8) { error = "the donor's Graphic field is not a CEngineGraphic"; return false; }
        std::memcpy(graphic.data() + 4, &out.meshId, 4);
        forge::defedit::setFieldBytes(defs, schema, out.objectName, "Graphic", graphic);
        const float height = (composed.bbMax.z - composed.bbMin.z) / kMetresToMesh;   // the def's sizes are world units
        const float radius = 0.5f * std::max(composed.bbMax.x - composed.bbMin.x, composed.bbMax.y - composed.bbMin.y) / kMetresToMesh;
        auto setf = [&](const char* field, float value) {
            try { forge::defedit::setField(defs, schema, out.objectName, field, std::to_string(value)); }
            catch (const std::exception& e) { out.notes.push_back(std::string("game.bin: ") + field + " left as the donor's (" + e.what() + ")"); }
        };
        setf("MeshHeight", height); setf("ApproxMaxMeshHeight", height); setf("MeshRadius", radius);
        const fs::path namesOut = pending.prepare(defsRel / "names.bin"), gameOut = pending.prepare(defsRel / "game.bin");
        defs.save(namesOut, gameOut);
        {
            const auto verified = forge::bin::File::open(namesOut, gameOut);
            const auto* entry = verified.find(out.objectName);
            if (verified.entries().size() != defs.entries().size() || !entry || entry->data != defs.find(out.objectName)->data) {
                error = "prepared definitions did not read back correctly";
                return false;
            }
        }
        if (inPlace && backups::gameRunningIn(baseRoot)) { error = "Fable started during import; quit to the desktop first"; return false; }
        if (!pending.install(inPlace, error)) return false;
        out.defIndex = index;
        out.notes.push_back("game.bin: appended OBJECT " + out.objectName + " (def index " + std::to_string(index) + ", a copy of " + req.donor + " with Graphic.modelId " +
                            std::to_string(out.meshId) + ", height " + std::to_string(height) + ", radius " + std::to_string(radius) + ")");
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

} // namespace albion::meshimport
