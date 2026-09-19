#pragma once
// Read-only LOD0 geometry decoding for compiled MBANK_ALLMESHES entries.
// The supported layouts mirror FableMod.Gfx.Integration SUBM::GetVertices;
// it is intentionally rendering-agnostic so GUI previews and thumbnails share it.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace forge::meshpreview {

struct Vertex { float x = 0, y = 0, z = 0; float nx = 0, ny = 0, nz = 1; float u = 0, v = 0; };
struct Triangle { uint32_t a = 0, b = 0, c = 0; int32_t material = -1; };
struct Material {
    int32_t id = -1;
    int32_t diffuseTexture = 0;
    int32_t bumpTexture = 0;
    int32_t reflectionTexture = 0;
    int32_t alphaMapTexture = 0;
    uint32_t textureFlags = 0;
    uint32_t glowStrength = 0;
    uint8_t unknown40 = 0;
    bool alphaEnabled = false;
    uint8_t unknown42 = 0;
    uint16_t unknown43 = 0;
};
struct Helper {
    uint32_t nameCrc = 0;
    std::string name;
    // Row-major affine 3x4 exactly as HDMY::GetMatrix expands it.
    float matrix[12] = {};
    uint32_t bone = 0;
};
struct PrimitiveInfo {
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    uint32_t vertexStride = 0;
    uint32_t vertexFormat = 0;
    uint32_t repeatCount = 1;
    uint32_t oddStartStripBlocks = 0;
    uint32_t staticMinimumIndex = 0;
};
struct Geometry {
    std::vector<Vertex> vertices;
    std::vector<Triangle> triangles;
    std::vector<Material> materials;
    std::vector<Helper> helpers;
    std::vector<PrimitiveInfo> primitives;
    uint32_t boneCount = 0;
    uint32_t primitiveCount = 0;
    bool empty() const { return vertices.empty() || triangles.empty(); }
};

// Decode one compiled mesh payload. meshType is BIG entry type (1/2/4/5).
// Throws std::runtime_error for malformed or unsupported data.
Geometry decodeLod0(const std::vector<uint8_t>& payload, uint32_t meshType);

// Locate a dense mesh id in MBANK_ALLMESHES and decode its first LOD.
Geometry readLod0(const std::filesystem::path& graphicsBig, uint32_t meshId);

} // namespace forge::meshpreview
