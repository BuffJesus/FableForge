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
//   * The instance scanner is heuristic (maximal 16-byte-stride runs of plausible
//     world coordinates in every decodable LZO frame). Instances it cannot bind
//     to a scenery type are reported as `unboundInstances` and skipped.
//   * Tree-type instances live in a separate pass in the engine; coverage of
//     those depends on the scanner and is reported, not assumed.
//   * Only LOD0 geometry; fade/impostor (ZSprite) data is not exported.

#include <cstdint>
#include <filesystem>
#include <functional>
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

struct Mesh {
    uint32_t meshId = 0;                 // MBANK_ALLMESHES entry id
    std::string name;                    // e.g. MESH_DANDELIONFLOWERS_01
    std::string label;                   // human label from the palette catalog, if known
    forge::meshpreview::Geometry geometry;   // Fable-space LOD0
    uint32_t diffuseTexture = 0;         // GBANK_MAIN_PC id (0 = none)
    int image = -1;                      // index into Scene::images
    bool hasAlpha = false;               // texture carries an alpha channel -> cutout material
    size_t instanceCount = 0;
};

struct Instance {
    int mesh = -1;          // index into Scene::meshes
    int type = -1;          // palette type#
    float x = 0, y = 0, z = 0;   // Fable-space, map-local unless Options::mapLocal == false
    float yaw = 0;          // radians about Fable +Z
    float scale = 1;
};

struct Scene {
    std::string mapName;
    bool found = false;               // the STB knows this map
    int worldX = 0, worldY = 0;       // STB origin (Fable units)
    std::vector<Mesh> meshes;
    std::vector<terrainexport::Image> images;
    std::vector<Instance> instances;
    int paletteTypes = 0;
    int unboundInstances = 0;
    int rejectedInstances = 0;        // implausible scale (scanner false positives)
    int framesDecoded = 0;
    std::vector<std::string> warnings;
    size_t triangleCount() const;
};

// Loads everything for one map. Never throws for "no foliage": `found` is false
// and `warnings` says why. `context` must be a ready terrainexport::Context
// (textures.big is shared with it); graphics.big and the STB are opened here.
Scene load(const std::string& mapName, const Options& options,
           const terrainexport::Context& context);

// Appends foliage to a GLB document under construction. Used by
// terrainexport::buildGlb via the combined writer below.
std::vector<uint8_t> buildGlbWithFoliage(const terrainexport::Scene& terrain, const Scene& foliage);
std::vector<std::filesystem::path> writeGlbWithFoliage(const terrainexport::Scene& terrain,
                                                       const Scene& foliage,
                                                       const std::filesystem::path& out);
// OBJ: foliage instances are baked into world-space triangles in a second object
// group (one material per texture), written into the same .obj/.mtl.
std::vector<std::filesystem::path> writeObjWithFoliage(const terrainexport::Scene& terrain,
                                                       const Scene& foliage,
                                                       const std::filesystem::path& out);

} // namespace albion::foliageexport
