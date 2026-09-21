#pragma once
// forge::meshcompose -- compose a NEW compiled static mesh (BIG entry type 1) from arbitrary
// geometry: the payload MBANK_ALLMESHES stores (LOD0 + the retail ghost LOD) and its entry
// Info blob. A port of FableTLC's `mesh_rw.compose_mesh` static path (in-game proven for the
// custom-NPC pipeline; EgoCore MeshCompiler.h / GltfMeshImporter.h are the C++ oracle):
//   * one CStaticBlock per primitive, triangle LISTS, InitFlags 0x14 / stride 20 (FLOAT3
//     position + 11/11/10 packed normal + s11e4 int16 UV) -- the "float" layout retail ships too;
//   * materials + retail's trailing 'DegenerateTriangles' sentinel;
//   * vertex / index buffers in Fable chunk framing (LZO, or stored);
//   * Info = PhysicsIndex, bounds, LODCount=1, LODSizes[0]=len(LOD0), SafeBoundingRadius=0, texture ids
//     (EgoCore C3DMeshContent::SerializeEntryMetadata; validated field-for-field against retail).
// Geometry is in Fable space (Z up, 1 unit = 1 world unit), faces CCW as our decoder yields them.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace forge::meshcompose {

struct Vec3 { float x = 0, y = 0, z = 0; };
struct Vec2 { float u = 0, v = 0; };

struct Primitive {
    std::vector<Vec3> verts;                 // required
    std::vector<std::array<uint32_t, 3>> faces;   // required, CCW
    std::vector<Vec2> uvs;                   // per vertex; empty = (0,0). v is glTF/Blender-convention (stored as 1-v)
    std::vector<Vec3> normals;               // per vertex; empty = accumulated face normals
    int material = 0;                        // slot into Composed::materials
};

struct Material {
    std::string name;                        // empty = <mesh>_mat<slot>
    int32_t diffuseId = 0;                   // textures.big entry ids (0 = none)
    int32_t bumpId = 0, reflectId = 0, illumId = 0, decalId = 0;
    int32_t selfIllum = 0;
    bool twoSided = false, transparent = false, booleanAlpha = false;
};

struct Composed {
    std::vector<uint8_t> payload;            // LOD0 + ghost LOD
    std::vector<uint8_t> info;               // the entry subheader
    Vec3 bbMin, bbMax, sphereCentre;
    float sphereRadius = 0;
    size_t vertices = 0, triangles = 0;
};

// `name` must start with "MESH_" (the payload classifier). Throws std::invalid_argument on bad input
// (no prims / materials, > 65535 vertices in one primitive, a face index out of range).
Composed composeStatic(const std::string& name, const std::vector<Primitive>& prims,
                       const std::vector<Material>& materials, bool compress = true, int physicsIndex = 0);

// EgoCore GltfMeshImporter.h PackNormal / CompressUV, exposed for tests.
uint32_t packNormal(Vec3 n);
int16_t compressUv(float v);

} // namespace forge::meshcompose
