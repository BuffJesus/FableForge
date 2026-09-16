#pragma once
// Internal: minimal GLB (glTF 2.0 binary) document builder shared by the
// terrain and foliage writers. Not part of the public API.

#include <cstdint>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"
#include "terrainexport.hpp"

namespace albion::glb {

using json = nlohmann::json;

struct Builder {
    std::vector<uint8_t> bytes;
    json bufferViews = json::array();
    json accessors = json::array();
    json images = json::array();
    json textures = json::array();
    json samplers = json::array();
    json materials = json::array();
    json meshes = json::array();
    json nodes = json::array();

    void pad() { while (bytes.size() % 4) bytes.push_back(0); }

    int view(const void* data, size_t len, int target = 0, int stride = 0) {
        pad();
        json v = {{"buffer", 0}, {"byteOffset", bytes.size()}, {"byteLength", len}};
        if (target) v["target"] = target;
        if (stride) v["byteStride"] = stride;
        const auto* p = static_cast<const uint8_t*>(data);
        bytes.insert(bytes.end(), p, p + len);
        bufferViews.push_back(v);
        return int(bufferViews.size() - 1);
    }

    int accessor(int view, int compType, const char* type, size_t count, json extra = json::object()) {
        json a = {{"bufferView", view}, {"componentType", compType}, {"count", count}, {"type", type}};
        for (auto& [k, val] : extra.items()) a[k] = val;
        accessors.push_back(a);
        return int(accessors.size() - 1);
    }

    // float VEC3 with min/max (required for POSITION).
    int positions(const std::vector<float>& xyz) {
        const size_t n = xyz.size() / 3;
        float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
        for (size_t i = 0; i < n; ++i)
            for (int k = 0; k < 3; ++k) { mn[k] = std::min(mn[k], xyz[i * 3 + k]); mx[k] = std::max(mx[k], xyz[i * 3 + k]); }
        if (n == 0) for (int k = 0; k < 3; ++k) mn[k] = mx[k] = 0;
        return accessor(view(xyz.data(), xyz.size() * 4, 34962), 5126, "VEC3", n,
                        {{"min", {mn[0], mn[1], mn[2]}}, {"max", {mx[0], mx[1], mx[2]}}});
    }
    int vec3(const std::vector<float>& v) { return accessor(view(v.data(), v.size() * 4, 34962), 5126, "VEC3", v.size() / 3); }
    int vec2(const std::vector<float>& v) { return accessor(view(v.data(), v.size() * 4, 34962), 5126, "VEC2", v.size() / 2); }
    int indices(const std::vector<uint32_t>& idx) { return accessor(view(idx.data(), idx.size() * 4, 34963), 5125, "SCALAR", idx.size()); }

    // Embeds a PNG and returns the glTF texture index.
    int texture(const terrainexport::Image& img, const std::string& name, bool repeat) {
        const auto png = terrainexport::encodePng(img);
        images.push_back({{"name", name}, {"mimeType", "image/png"}, {"bufferView", view(png.data(), png.size())}});
        const int wrap = repeat ? 10497 : 33071;
        samplers.push_back({{"magFilter", 9729}, {"minFilter", 9987}, {"wrapS", wrap}, {"wrapT", wrap}});
        textures.push_back({{"source", int(images.size() - 1)}, {"sampler", int(samplers.size() - 1)}});
        return int(textures.size() - 1);
    }

    int material(json m) { materials.push_back(std::move(m)); return int(materials.size() - 1); }
    int mesh(json m) { meshes.push_back(std::move(m)); return int(meshes.size() - 1); }
    int node(json n) { nodes.push_back(std::move(n)); return int(nodes.size() - 1); }

    std::vector<uint8_t> finish(const std::string& sceneName, const std::vector<int>& rootNodes,
                                const std::string& generator) {
        json doc = {
            {"asset", {{"version", "2.0"}, {"generator", generator}}},
            {"scene", 0},
            {"scenes", {{{"name", sceneName}, {"nodes", rootNodes}}}},
            {"nodes", nodes},
            {"meshes", meshes},
            {"materials", materials},
            {"accessors", accessors},
            {"bufferViews", bufferViews},
        };
        if (!images.empty()) { doc["images"] = images; doc["textures"] = textures; doc["samplers"] = samplers; }
        pad();
        doc["buffers"] = {{{"byteLength", bytes.size()}}};
        std::string js = doc.dump();
        while (js.size() % 4) js.push_back(' ');
        std::vector<uint8_t> out;
        auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i))); };
        u32(0x46546C67); u32(2); u32(12 + 8 + uint32_t(js.size()) + 8 + uint32_t(bytes.size()));
        u32(uint32_t(js.size())); u32(0x4E4F534A);
        out.insert(out.end(), js.begin(), js.end());
        u32(uint32_t(bytes.size())); u32(0x004E4942);
        out.insert(out.end(), bytes.begin(), bytes.end());
        return out;
    }
};

} // namespace albion::glb
