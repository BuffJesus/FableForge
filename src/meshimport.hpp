#pragma once
// glTF triangle lists, strips and fans become static triangle lists.
// Unsupported required glTF extensions refuse; ignored optional extensions are reported in notes.
// Active glTF morph weights refuse; bake the desired shape into the static mesh first.
// A custom static mesh into the game (ROADMAP 0.17 "Custom static meshes"): a .glb / .gltf /
// .obj model becomes a compiled MESH_<NAME> entry appended to graphics.big's MBANK_ALLMESHES
// (forge::meshcompose, the in-game-proven compose_mesh grammar), its diffuse texture a
// GBANK_MAIN_PC entry (the 0.14 importer), and an OBJECT_<NAME> def -- a copy of a donor
// OBJECT with Graphic.modelId repointed and the mesh height/radius from the bounds -- so the
// object shows in *Add an object* and places like any retail prop. One-time .forge-orig
// backups; refused while the game runs. Model space: glTF Y-up metres are turned into Fable Z-up
// centimetres (x, -z, y) * 100, the exporter's inverse; OBJ is read as Y-up metres too.
// Collision builds a type-3 hull from the triangles and links PhysicsIndex;
// disabling collision leaves PhysicsIndex 0.
#include <array>
#include <filesystem>
#include <string>
#include <vector>
#include <optional>

#include "forge/meshcompose.hpp"
namespace forge::meshpreview { struct Bone; }

namespace albion::meshimport {

struct SkinJoint {
    size_t sourceNode=0;
    std::optional<size_t> parentNode;
    std::string name;
    // Original glTF coordinates/metres, column-major. Not donor transforms.
    std::array<float,16> world{},inverseBind{};
    std::optional<size_t> parentJoint; // nearest ancestor in skin.joints, through non-joint nodes
};
struct SkinBinding {
    size_t sourceSkin=0,meshNode=0;
    std::optional<size_t> skeletonRoot;
    std::vector<SkinJoint> joints; // skin.joints order; influence indices refer here
    std::vector<std::vector<forge::meshcompose::Influence>> influences;
};
struct Model {
    std::vector<forge::meshcompose::Primitive> prims;   // one per glTF primitive / OBJ material group, in Fable space
    std::vector<std::string> materialNames;             // per slot
    std::vector<std::string> notes;
    std::vector<SkinBinding> skins; // loadSkinnedModel only, parallel to prims
};

// .glb / .gltf (embedded or sidecar buffers; POSITION, NORMAL, TEXCOORD_0, indices;
// core position/normal/UV component encodings, interleaved bases and sparse replacements)
// or .obj (v / vt / vn / f, usemtl groups). Throws std::runtime_error with a reason.
// OBJ polygons up to 1024 corners are triangulated; simple planar concave faces
// use ear clipping. Self intersections and non-planar concave faces refuse with
// a request to triangulate in the modelling tool. Valid convex fans retain order.

// Notes identify texture coordinates that will clip in the static writer.
Model loadModel(const std::filesystem::path& path);
// Selected-scene glTF/GLB skinned instances only. Retains every positive weight
// set and joint/bind metadata for subsequent donor compatibility validation.
// Does not apply pose or skinned mesh-node transforms to the authored vertices,
// map joints to a donor, compose bytes or publish banks. Static loader unchanged.
Model loadSkinnedModel(const std::filesystem::path& path);
// Coordinate-only conversion for donor bind comparison: C * M * inverse(C),
// C maps glTF metres (x,y,z) to Fable centimetres (x,-z,y)*100.
// Returns a row-major affine matrix. Does not establish donor compatibility.
std::array<double,16> inverseBindInFable(const std::array<float,16>& gltfMatrix);
// Read-only donor remapping. Exact unique names for positive-weight joints and
// donor ancestors; checks nearest-joint parent chains and converted inverse binds.
// Bind tolerance: 1e-5 linear / 0.001 cm translation absolute + 1e-5 relative.
// Retains geometry and every positive weight; does not compose/publish or prove
// animation/runtime compatibility. Donor roots may be partial source subtrees.
std::vector<forge::meshcompose::SkinnedPrimitive> mapSkinToDonor(
    const Model& model,const std::vector<forge::meshpreview::Bone>& donor);

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
    std::vector<std::string> modelNotes; // conversion notes for the import card; notes also contains bank-write logs
};

// Prepare all banks before replacement. A reported commit failure rolls back
// prior replacements; if rollback itself fails, error names retained recovery files.
bool importModel(const std::filesystem::path& gameRoot, const ImportRequest& req, ImportResult& out, std::string& error);
// The same into an output tree: every file is read from outRoot when it is there
// (an earlier mod layer wrote it) else from baseRoot, and written to outRoot, with
// no backups -- the mod composer's recipe step (forge_pack.json). With outRoot ==
// baseRoot this is the in-place import above (one-time .forge-orig backups).
bool importModel(const std::filesystem::path& baseRoot, const std::filesystem::path& outRoot, const ImportRequest& req, ImportResult& out, std::string& error);

struct ReplaceRequest {
    std::filesystem::path model;
    std::string target; // exact mesh name, or unsigned decimal bank ID
    bool compress = true;
    std::string expectedName; // optional stale-selection guard for ID targets
};
struct ReplaceResult {
    uint32_t meshId=0,physicsId=0;
    std::string meshName;
    size_t vertices=0,triangles=0,primitives=0;
    std::vector<std::string> notes;
};
// Replace only the geometry/geometry descriptor of a helper-free type-1 mesh
// with one LOD, canonical ordinary material IDs/root matrix and zero safe-radius
// override. Original material records, entry
// identity and collision link/payload remain; imported material slots map by
// ordinal and must match the source's ordinary material count. Other banks and
// definitions remain untouched. Prepare/read back before committing one bank.
bool replaceModel(const std::filesystem::path& gameRoot,const ReplaceRequest& req,ReplaceResult& out,std::string& error);
bool replaceModel(const std::filesystem::path& baseRoot,const std::filesystem::path& outRoot,
                  const ReplaceRequest& req,ReplaceResult& out,std::string& error);

} // namespace albion::meshimport
