#pragma once
// A custom static mesh into the game (ROADMAP 0.17 "Custom static meshes"): a .glb / .gltf /
// .obj model becomes a compiled MESH_<NAME> entry appended to graphics.big's MBANK_ALLMESHES
// (forge::meshcompose, the in-game-proven compose_mesh grammar), its diffuse texture a
// GBANK_MAIN_PC entry (the 0.14 importer), and an OBJECT_<NAME> def -- a copy of a donor
// OBJECT with Graphic.modelId repointed and the mesh height/radius from the bounds -- so the
// object shows in *Add an object* and places like any retail prop. One-time .forge-orig
// backups; refused while the game runs. Model space: glTF Y-up metres are turned into Fable Z-up
// centimetres (x, -z, y) * 100, the exporter's inverse; OBJ is read as Y-up metres too. Untested in-game so far:
// physics (PhysicsIndex 0 = the mesh has no hull; the object may be walk-through).
#include <array>
#include <filesystem>
#include <string>
#include <vector>

#include "forge/meshcompose.hpp"

namespace albion::meshimport {

struct Model {
    std::vector<forge::meshcompose::Primitive> prims;   // one per glTF primitive / OBJ material group, in Fable space
    std::vector<std::string> materialNames;             // per slot
    std::vector<std::string> notes;
};

// .glb / .gltf (embedded or sidecar buffers; POSITION, NORMAL, TEXCOORD_0, indices, any component types)
// or .obj (v / vt / vn / f, usemtl groups). Throws std::runtime_error with a reason.
Model loadModel(const std::filesystem::path& path);

struct ImportRequest {
    std::filesystem::path model;
    std::string name;                 // A-Z 0-9 _ ; becomes MESH_<name> / OBJECT_<name> / <name>_DIFFUSE
    std::filesystem::path texturePng; // the diffuse for every material (optional)
    uint32_t textureId = 0;           // ... or an existing textures.big id
    std::string donor = "OBJECT_BARREL_UNBREAKABLE";   // the OBJECT def copied for the new object
    bool compress = true;
    bool collision = true;            // a type-3 physics entry from the model's own triangles (PhysicsIndex); false = walk-through
};

struct ImportResult {
    uint32_t meshId = 0;              // the new MBANK_ALLMESHES id (Graphic.modelId)
    uint32_t physicsId = 0;           // the type-3 collision entry (0 = none)
    uint32_t textureId = 0;
    size_t defIndex = 0;
    size_t vertices = 0, triangles = 0, primitives = 0;
    std::string meshName, objectName;
    std::vector<std::string> notes;
};

bool importModel(const std::filesystem::path& gameRoot, const ImportRequest& req, ImportResult& out, std::string& error);

} // namespace albion::meshimport
