#pragma once
// albion::foliageexport -- the baked "local detail" scenery of one map (grass,
// flowers, ferns, bushes, trees) as mesh instances.
//
// Where it comes from: Fable's foliage is not hand-placed; the editor scatters it
// from ground themes and BAKES the result into data/Levels/FinalAlbion_RT.stb.
// Per map that bank holds (a) a common-header record with the map's world
// origin and the scenery TYPE palette (mesh ids + fade distances), and (b) a
// terrain chunk whose LZO frames contain the per-instance placements
// [x y z scale] + [cos*scale sin*scale 0 0]. forge::foliage recovers both.
// Meshes are MBANK_ALLMESHES entries in graphics.big (LOD0 via forge::meshpreview),
// their diffuse textures GBANK_MAIN_PC entries in textures.big.
//
// Honest boundaries:
//   * Instances are read by GRAMMAR from every LZO frame in the chunk that
//     parses as a CObjectCacheGroupCollection (the layout FableForge's STB baker
//     writes and the engine's Load reads): type-1 RepeatedMesh batches (grass:
//     16-byte A/B arrays) and type-0 Mesh primitives (trees/props: a full 3x4
//     matrix) and type-2 ZSpriteBatch records (distant trees: the same 3x4
//     matrix per object, grammar from CLocalDetailPrimitiveMeshZSpriteBatch::Load
//     in the debug build). A z-sprite object that coincides with a type-0 object
//     of the same type is its far-LOD twin and is dropped.
//   * Only LOD0 geometry; z-sprite objects are exported as the full mesh, not as
//     the impostor sprite the engine draws at distance.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "forge/meshpreview.hpp"
#include "terrainexport.hpp"

namespace albion::foliageexport {

struct Options {
    std::filesystem::path gameRoot;     // for FinalAlbion_RT.stb, graphics.big, textures.big
    bool textures = true;
    terrainexport::UpAxis up = terrainexport::UpAxis::Y;
    // Instances are stored in WORLD coordinates. By default they are shifted by
    // the map's own STB origin so they land on a map-local terrain export; pass
    // false to keep world coordinates (when the terrain was exported with --origin).
    bool mapLocal = true;
    std::function<void(const std::string&)> log;
};

// One material's share of a mesh (trees are leaves + trunk, each its own texture).
struct SubMesh {
    int material = -1;                   // index into geometry.materials (-1 = none)
    uint32_t diffuseTexture = 0;         // GBANK_MAIN_PC id (0 = none)
    int image = -1;                      // index into Scene::images
    bool hasAlpha = false;               // texture has an alpha channel -> cutout material
    std::vector<uint32_t> indices;       // triangle list into geometry.vertices
};

struct Mesh {
    uint32_t meshId = 0;                 // MBANK_ALLMESHES entry id
    std::string name;                    // e.g. MESH_DANDELIONFLOWERS_01
    std::string label;                   // human label from the palette catalog, if known
    forge::meshpreview::Geometry geometry;   // Fable-space LOD0 (UVs normalised into [0,1)-based range)
    std::vector<SubMesh> parts;          // by material; every triangle is in exactly one part
    uint32_t diffuseTexture = 0;         // first part's texture (convenience)
    int image = -1;                      // first part's image (convenience)
    bool hasAlpha = false;
    size_t instanceCount = 0;
};

struct Instance {
    int mesh = -1;          // index into Scene::meshes
    int type = -1;          // palette type#
    int prim = 1;           // 0 = single mesh (tree/prop), 1 = repeated mesh (grass)
    float x = 0, y = 0, z = 0;   // Fable-space, map-local unless Options::mapLocal == false
    float yaw = 0;          // radians about Fable +Z (type-1, or derived for type-0)
    float scale = 1;        // uniform scale (type-1) / |column| average (type-0)
    // Type-0 primitives carry a full rotation*scale 3x3 (row-major, rows are the
    // transformed basis vectors as the engine stores them). `hasMatrix` says it
    // is authoritative; `yaw`/`scale` are then derived conveniences.
    bool hasMatrix = false;
    float m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
};

struct Scene {
    std::string mapName;
    std::string rootName = "Foliage"; // glTF root node / OBJ object suffix
    bool found = false;               // the STB knows this map
    int worldX = 0, worldY = 0;       // STB origin (Fable units)
    std::vector<Mesh> meshes;
    std::vector<terrainexport::Image> images;
    std::vector<Instance> instances;
    int paletteTypes = 0;
    int unboundInstances = 0;         // instances whose type# had no palette entry / mesh
    int rejectedInstances = 0;        // implausible scale (parse false positives)
    int framesDecoded = 0;            // LZO frames decoded in the chunk
    int groupFrames = 0;              // frames that parsed as cache-group collections
    int zspriteInstances = 0;         // type-2 (distant impostor) placements decoded
    int zspriteDuplicates = 0;        // ...of which coincide with a type-0 placement and were dropped
    int treeInstances = 0;            // type-0/2 (single-mesh) instances placed
    std::vector<std::string> warnings;
    size_t triangleCount() const;
};

// --- shared mesh bank access (process-wide cache over MBANK_ALLMESHES) ------
bool openMeshBank(const std::filesystem::path& graphicsBig, std::string& err);
const forge::meshpreview::Geometry* cachedMesh(uint32_t id, std::string& err);
std::string meshName(uint32_t id);
uint32_t meshIdByName(const std::string& name);   // 0 when unknown
// Build a Mesh (parts split by material, textures resolved through `context`,
// UVs normalised) from a decoded geometry. `images` / `textureToImage` are the
// scene's shared image table.
Mesh makeMesh(uint32_t meshId, const std::string& name, const std::string& label,
              const forge::meshpreview::Geometry& geo, bool textures,
              const terrainexport::Context& context, std::vector<terrainexport::Image>& images,
              std::map<uint32_t, int>& textureToImage, std::vector<std::string>& warnings);

// Basis images of the instance's local axes in Fable space (col[k] = image of
// local axis k, scale included): world = pos + lx*col[0] + ly*col[1] + lz*col[2].
void instanceBasis(const Instance& i, float col[3][3]);

// Loads everything for one map. Never throws for "no foliage": `found` is false
// and `warnings` says why. `context` must be a ready terrainexport::Context
// (textures.big is shared with it); graphics.big and the STB are opened here.
Scene load(const std::string& mapName, const Options& options,
           const terrainexport::Context& context);

// Writers: terrain plus any number of instance scenes (foliage, things...).
// GLB: each scene becomes a root node (Scene::rootName) with one child per
// instance. OBJ: each scene is baked into world-space triangles as an extra
// object group (one material per texture) in the same .obj/.mtl.
std::vector<uint8_t> buildGlbWith(const terrainexport::Scene& terrain, const std::vector<const Scene*>& layers);
std::vector<std::filesystem::path> writeGlbWith(const terrainexport::Scene& terrain,
                                                const std::vector<const Scene*>& layers,
                                                const std::filesystem::path& out);
std::vector<std::filesystem::path> writeObjWith(const terrainexport::Scene& terrain,
                                                const std::vector<const Scene*>& layers,
                                                const std::filesystem::path& out);
// Single-layer conveniences.
std::vector<uint8_t> buildGlbWithFoliage(const terrainexport::Scene& terrain, const Scene& foliage);
std::vector<std::filesystem::path> writeGlbWithFoliage(const terrainexport::Scene& terrain,
                                                       const Scene& foliage,
                                                       const std::filesystem::path& out);
std::vector<std::filesystem::path> writeObjWithFoliage(const terrainexport::Scene& terrain,
                                                       const Scene& foliage,
                                                       const std::filesystem::path& out);

} // namespace albion::foliageexport
