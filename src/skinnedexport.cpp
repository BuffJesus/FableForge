#include "skinnedexport.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

#include "exportfiles.hpp"
#include "glbwriter.hpp"

namespace albion::skinnedexport {
namespace {
namespace fs = std::filesystem;
using json = nlohmann::json;
using M4 = std::array<double, 16>; // row-major, column-vector points

[[noreturn]] void fail(const std::string& reason) { throw std::runtime_error("rigged export: " + reason); }

M4 mul(const M4& a, const M4& b) {
    M4 r{};
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j)
        for (int k = 0; k < 4; ++k) r[i * 4 + j] += a[i * 4 + k] * b[k * 4 + j];
    return r;
}
// Affine inverse (upper 3x3 by cofactors, then translation).
M4 inverseAffine(const M4& m) {
    const double a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    const double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (!std::isfinite(det) || std::abs(det) < 1e-12) fail("bind matrix is not invertible");
    M4 r{};
    r[0] = (e * i - f * h) / det; r[1] = (c * h - b * i) / det; r[2] = (b * f - c * e) / det;
    r[4] = (f * g - d * i) / det; r[5] = (a * i - c * g) / det; r[6] = (c * d - a * f) / det;
    r[8] = (d * h - e * g) / det; r[9] = (b * g - a * h) / det; r[10] = (a * e - b * d) / det;
    for (int row = 0; row < 3; ++row)
        r[row * 4 + 3] = -(r[row * 4] * m[3] + r[row * 4 + 1] * m[7] + r[row * 4 + 2] * m[11]);
    r[15] = 1;
    return r;
}
// glTF metres -> Fable centimetres and back (meshimport's toFable / inverseBindInFable).
constexpr M4 kToFable{100, 0, 0, 0, 0, 0, -100, 0, 0, 100, 0, 0, 0, 0, 0, 1};
constexpr M4 kToGltf{0.01, 0, 0, 0, 0, 0, 0.01, 0, 0, -0.01, 0, 0, 0, 0, 0, 1};
std::vector<float> columnMajor(const M4& m) {
    std::vector<float> out(16);
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) out[size_t(c * 4 + r)] = float(m[size_t(r * 4 + c)]);
    return out;
}
// Largest-remainder rounding: positive weights become bytes summing to 255.
std::array<uint8_t, 4> weightBytes(const std::array<float, 4>& w) {
    double total = 0;
    for (float x : w) total += std::max(0.0f, x);
    if (!(total > 0)) fail("vertex without positive weights");
    std::array<int, 4> bytes{};
    std::array<double, 4> rest{};
    int used = 0;
    for (size_t k = 0; k < 4; ++k) {
        const double exact = std::max(0.0f, w[k]) / total * 255.0;
        bytes[k] = int(std::floor(exact)); rest[k] = exact - bytes[k]; used += bytes[k];
    }
    while (used < 255) {
        const auto k = size_t(std::max_element(rest.begin(), rest.end()) - rest.begin());
        ++bytes[k]; rest[k] = -1; ++used;
    }
    return {uint8_t(bytes[0]), uint8_t(bytes[1]), uint8_t(bytes[2]), uint8_t(bytes[3])};
}
} // namespace

std::vector<uint8_t> build(const foliageexport::Mesh& mesh, const std::vector<terrainexport::Image>& images) {
    const auto& g = mesh.geometry;
    if (g.bones.empty()) fail("mesh has no bones");
    std::set<std::string> names;
    for (const auto& bone : g.bones)
        if (bone.name.empty() || !names.insert(bone.name).second) fail("bone names must be unique and non-empty");
    for (size_t i = 0; i < g.bones.size(); ++i)
        if (g.bones[i].parent >= int32_t(g.bones.size()) || g.bones[i].parent == int32_t(i)) fail("invalid bone parent");

    glb::Builder b;
    // Skeleton: global bind in glTF space = C^-1 * inverse(inverseBind) * C.
    std::vector<M4> inverseBinds, globals;
    for (const auto& bone : g.bones) {
        M4 ib{};
        for (size_t k = 0; k < 16; ++k) ib[k] = bone.inverseBind[k];
        ib[12] = ib[13] = ib[14] = 0; ib[15] = 1;
        const M4 gltfInverse = mul(mul(kToGltf, ib), kToFable);
        inverseBinds.push_back(gltfInverse);
        globals.push_back(inverseAffine(gltfInverse));
    }
    const int firstJoint = int(b.nodes.size());
    std::vector<std::vector<int>> children(g.bones.size());
    std::vector<int> rootJoints;
    for (size_t i = 0; i < g.bones.size(); ++i) {
        const int parent = g.bones[i].parent;
        if (parent < 0) rootJoints.push_back(firstJoint + int(i));
        else children[size_t(parent)].push_back(firstJoint + int(i));
    }
    for (size_t i = 0; i < g.bones.size(); ++i) {
        const int parent = g.bones[i].parent;
        const M4 local = parent < 0 ? globals[i] : mul(inverseAffine(globals[size_t(parent)]), globals[i]);
        json node = {{"name", g.bones[i].name}, {"matrix", columnMajor(local)}};
        if (!children[i].empty()) node["children"] = children[i];
        b.node(node);
    }
    std::vector<float> ibm;
    for (const auto& m : inverseBinds) { const auto c = columnMajor(m); ibm.insert(ibm.end(), c.begin(), c.end()); }
    const int ibmAccessor = b.accessor(b.view(ibm.data(), ibm.size() * 4), 5126, "MAT4", inverseBinds.size());
    std::vector<int> joints;
    for (size_t i = 0; i < g.bones.size(); ++i) joints.push_back(firstJoint + int(i));

    // Geometry in glTF metres; normals rotate only.
    const size_t n = g.vertices.size();
    std::vector<float> pos(n * 3), nrm(n * 3), uv(n * 2);
    std::vector<uint16_t> jointData(n * 4);
    std::vector<uint8_t> weightData(n * 4);
    for (size_t i = 0; i < n; ++i) {
        const auto& v = g.vertices[i];
        if (!v.skinned) fail("vertex " + std::to_string(i) + " is not skinned");
        pos[i * 3] = v.x * 0.01f; pos[i * 3 + 1] = v.z * 0.01f; pos[i * 3 + 2] = -v.y * 0.01f;
        nrm[i * 3] = v.nx; nrm[i * 3 + 1] = v.nz; nrm[i * 3 + 2] = -v.ny;
        uv[i * 2] = v.u; uv[i * 2 + 1] = v.v;
        // Merge repeated slots so no joint carries two positive weights.
        std::array<uint16_t, 4> slot{};
        std::array<float, 4> weight{};
        size_t used = 0;
        for (size_t k = 0; k < 4; ++k) {
            if (v.weights[k] <= 0) continue;
            if (v.joints[k] >= g.bones.size()) fail("joint slot out of range");
            size_t at = 0;
            while (at < used && slot[at] != v.joints[k]) ++at;
            if (at == used) { slot[used] = v.joints[k]; ++used; }
            weight[at] += v.weights[k];
        }
        const auto bytes = weightBytes(weight);
        for (size_t k = 0; k < 4; ++k) {
            jointData[i * 4 + k] = bytes[k] ? slot[k] : 0;
            weightData[i * 4 + k] = bytes[k];
        }
    }
    json attributes = {{"POSITION", b.positions(pos)}, {"NORMAL", b.vec3(nrm)}, {"TEXCOORD_0", b.vec2(uv)},
                       {"JOINTS_0", b.accessor(b.view(jointData.data(), jointData.size() * 2, 34962), 5123, "VEC4", n)},
                       {"WEIGHTS_0", b.accessor(b.view(weightData.data(), weightData.size(), 34962), 5121, "VEC4", n,
                                                {{"normalized", true}})}};
    json primitives = json::array();
    std::map<int, int> imageToTexture;
    for (const auto& part : mesh.parts) {
        if (part.indices.empty()) continue;
        json material = {{"name", mesh.name + "_" + std::to_string(part.material)}, {"doubleSided", true},
                         {"pbrMetallicRoughness", {{"metallicFactor", 0.0}, {"roughnessFactor", 1.0}}}};
        if (part.image >= 0 && size_t(part.image) < images.size()) {
            auto t = imageToTexture.find(part.image);
            if (t == imageToTexture.end())
                t = imageToTexture.emplace(part.image, b.texture(images[size_t(part.image)], images[size_t(part.image)].name, true)).first;
            material["pbrMetallicRoughness"]["baseColorTexture"] = {{"index", t->second}};
            if (part.hasAlpha) { material["alphaMode"] = "MASK"; material["alphaCutoff"] = 0.5; }
        }
        primitives.push_back({{"attributes", attributes}, {"indices", b.indices(part.indices)},
                              {"material", b.material(material)}, {"mode", 4}});
    }
    if (primitives.empty()) fail("mesh has no drawable triangles");
    const int meshIndex = b.mesh({{"name", mesh.name}, {"extras", {{"mesh_id", mesh.meshId}}}, {"primitives", primitives}});
    const int meshNode = b.node({{"name", mesh.name}, {"mesh", meshIndex}, {"skin", 0}});
    std::vector<int> roots = rootJoints;
    roots.push_back(meshNode);
    // Builder has no skins member: add the single skin to the finished JSON chunk.
    auto glbBytes = b.finish(mesh.name, roots, "FableForge");
    const uint32_t jsonLength = uint32_t(glbBytes[12]) | uint32_t(glbBytes[13]) << 8 |
                                uint32_t(glbBytes[14]) << 16 | uint32_t(glbBytes[15]) << 24;
    auto doc = json::parse(glbBytes.begin() + 20, glbBytes.begin() + 20 + jsonLength);
    doc["skins"] = json::array({{{"name", mesh.name + "_skeleton"}, {"joints", joints},
                                 {"inverseBindMatrices", ibmAccessor}}});
    if (rootJoints.size() == 1) doc["skins"][0]["skeleton"] = rootJoints.front();
    std::string js = doc.dump();
    while (js.size() % 4) js.push_back(' ');
    const std::vector<uint8_t> binChunk(glbBytes.begin() + 20 + jsonLength, glbBytes.end());
    std::vector<uint8_t> out;
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i))); };
    u32(0x46546C67); u32(2); u32(uint32_t(12 + 8 + js.size() + binChunk.size()));
    u32(uint32_t(js.size())); u32(0x4E4F534A);
    out.insert(out.end(), js.begin(), js.end());
    out.insert(out.end(), binChunk.begin(), binChunk.end());
    return out;
}

std::vector<fs::path> write(const foliageexport::Mesh& mesh, const std::vector<terrainexport::Image>& images,
                            const fs::path& out) {
    return detail::publishExport(out, [&](const fs::path& out) {
        const auto bytes = build(mesh, images);
        if (!out.parent_path().empty()) fs::create_directories(out.parent_path());
        detail::writeExportBytes(out, bytes);
        return std::vector<fs::path>{out};
    });
}

} // namespace albion::skinnedexport
