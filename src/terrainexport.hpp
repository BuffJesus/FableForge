#pragma once
// albion::terrainexport -- turn one Fable TLC map into a portable 3D asset
// (.glb, or .obj + .mtl + PNG) that Blender / Unreal / three.js open directly.
//
// What goes in, and where each byte comes from:
//   * geometry  : the .lev cell grid (forge::lev::File) -- (width+1) x (height+1)
//                 vertices one world unit apart, Z = raw * 2048.
//   * texture   : the per-vertex ground-theme blend (3 palette slots + strengths,
//                 lev cell +10..+14) resolved through forge::terraintex::ThemeLibrary
//                 to the ENGINE_THEME def's base/cliff texture triples, whose
//                 entries are read out of textures.big GBANK_MAIN_PC and
//                 DXT-decoded here. The exporter BAKES one albedo image per map by
//                 blending those textures with the vertex weights.
//   * layers    : optionally the raw splat data too -- _THEME_INDEX/_THEME_WEIGHT
//                 vertex attributes plus one PNG per palette slot -- for people
//                 who want to rebuild the blend as a real shader.
//
// Honest boundaries (documented, not hidden):
//   * Texture tiling is the engine's: the landscape vertex shader maps u = x/8,
//     v = y/8 (CEngineLandscapePatch::PositionToTextureUVTransformU/V, +-0.125),
//     cliffs along one horizontal axis with v = -z/8. `Options::tileSize` (8)
//     overrides the period for people who want a different look.
//   * Cliff mapping direction is chosen here by slope, not by the engine's
//     per-layer mappingDirection bake (1..4) -- an approximation; `layers`
//     mode gives the exact inputs.
//   * Grid topology only: every cell becomes two triangles. The engine's masked
//     16x16 patches (holes/caves) live in the STB and are not consulted yet.
//   * Textures come from the user's own install; nothing retail is embedded.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "forge/lev.hpp"

namespace albion::terrainexport {

// Which axis is "up" in the exported file. Fable is Z-up right-handed; glTF is
// Y-up right-handed, so `Y` maps (x, y, z) -> (x, z, -y). `Z` writes Fable
// coordinates untouched.
enum class UpAxis { Y, Z };

struct Options {
    // Texturing. When `textures` is set, `gameRoot` (for data/CompiledDefs) and
    // `texturesBig` must point at a Fable TLC install. The def schema is embedded.
    bool textures = true;
    std::filesystem::path gameRoot;
    std::filesystem::path texturesBig;
    int texelsPerCell = 8;         // baked albedo resolution per cell edge
    float tileSize = 8.0f;         // world units per texture repeat: the engine's vertex shader
                                   // uses u = x/8, v = y/8 (cliffs: along/8, height/8) -- see header
    // Albedo gain. Fable's base textures are authored dark and the engine's
    // lighting brightens them by an unpinned factor; 1.0 keeps raw texels.
    float gain = 1.0f;
    float cliffStartSlope = 0.55f; // tan(angle) where cliff blending begins (~29 deg)
    float cliffFullSlope = 1.4f;   // tan(angle) where cliff fully replaces base (~54 deg)
    bool layers = false;           // also emit splat attributes + per-slot PNGs
    bool water = true;             // water surface from the theme blend (needs textures/defs)
    // Bake the ground from the engine's own STB foreground passes (per-patch texture
    // layers with mapping direction and per-vertex blend) instead of the LEV theme
    // blend + slope heuristic. Falls back to the LEV bake when the map has no STB entry.
    bool engineLayers = true;
    std::string mapName;           // STB lookup key (level stem); set by the CLI/GUI
    bool walkableColor = false;    // COLOR_0 = walkable (white) / blocked (red)
    UpAxis up = UpAxis::Y;
    // Map-local -> world offset (Fable-space), for stitching maps by WLD placement.
    float originX = 0.0f;
    float originY = 0.0f;
    // Optional cell presence mask (mapWidth*mapHeight, 1 = rendered). Absent
    // cells get no triangles: cave ceilings and cut-outs open up. Built by
    // stbterrain::load from the map's STB foreground frames.
    const std::vector<uint8_t>* cellMask = nullptr;
    std::function<void(const std::string&)> log; // optional progress/warning sink
};

struct Vertex {
    float px = 0, py = 0, pz = 0;   // already in the requested up-axis space
    float nx = 0, ny = 0, nz = 1;
    float u = 0, v = 0;             // glTF convention: v=0 is the TOP image row
    uint8_t themeIndex[3] = {0, 0, 0};
    uint8_t themeWeight[3] = {255, 0, 0};
    bool walkable = true;
};

struct Image {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> rgba;      // row 0 = map y 0 (matches v = y / height)
    std::string name;
};

struct ThemeLayer {
    int slot = -1;                  // LEV palette slot
    std::string name;               // palette name (== def name when sane)
    uint32_t defIndex = 0;
    uint32_t baseTexture = 0;       // textures.big GBANK_MAIN_PC id (0 = none)
    uint32_t cliffTexture = 0;
    bool resolved = false;
    size_t vertexReferences = 0;
    int baseImage = -1;             // index into Scene::layerImages, or -1
    int cliffImage = -1;
    float waterHeight = 0;          // ENGINE_THEME WaterHeight: water depth above the ground
    int waterType = 0;              // ENGINE_THEME WaterType: 0 = no water
};

// The water surface (lakes, rivers, sea) as the engine's water patches draw it:
// per vertex, ground + (theme blend * ENGINE_THEME WaterHeight) where any slot has
// WaterType != 0, smoothed over the 5x5 window (and extended 2 cells onto the bank),
// placed 0.1 below that level, with the in-game depth fade (0..2 units) kept per
// vertex. Cells whose sheet is entirely below the ground are not emitted.
struct WaterMesh {
    std::vector<float> positions;     // xyz, already in the requested up-axis space
    std::vector<uint8_t> ice;         // per vertex: 1 = frozen (EWaterType 8, Hook Coast ice)
    std::vector<float> fade;          // per vertex: engine depth fade, 0 (shore) .. 1 (2+ units deep)
    std::vector<uint32_t> indices;    // liquid water triangles, CCW seen from above
    std::vector<uint32_t> iceIndices; // frozen water triangles
    int wetVertices = 0;
    bool empty() const { return indices.empty() && iceIndices.empty(); }
};

struct Scene {
    std::string sourceName;
    int mapWidth = 0, mapHeight = 0;
    uint64_t uid = 0;
    float minHeight = 0, maxHeight = 0;
    UpAxis up = UpAxis::Y;
    std::vector<Vertex> vertices;   // row-major, index = y * (mapWidth+1) + x
    std::vector<uint32_t> indices;  // triangle list, CCW seen from above
    bool hasAlbedo = false;
    Image albedo;
    std::vector<ThemeLayer> themes;
    std::vector<Image> layerImages; // per-slot decoded textures (layers mode)
    std::vector<std::string> warnings;
    int hiddenCells = 0;            // cells omitted by the presence mask
    int unresolvedThemes = 0;       // palette slots no ENGINE_THEME could be found for
    int nameResolvedThemes = 0;     // slots resolved by NAME because the stored def index was stale
    bool walkableColor = false;
    bool layers = false;
    WaterMesh water;
    bool engineBake = false;        // albedo came from the STB foreground passes
    int enginePasses = 0;           // texture passes composited
};

// The map's world origin from data/Levels/FinalAlbion.wld (MapX/MapY); false
// when the WLD or the map is missing. World = origin + map-local.
bool worldOrigin(const std::filesystem::path& gameRoot, const std::string& mapName, float& x, float& y);

// Region membership from the WLD: region name for a map ("" when unknown) and
// the maps of a region, in WLD order.
struct RegionIndex {
    std::map<std::string, std::string> regionOfMap;               // map stem -> region name
    std::map<std::string, std::vector<std::string>> mapsOfRegion;  // region name -> map stems
    std::map<std::string, std::pair<float, float>> originOfMap;    // map stem -> MapX, MapY
    bool loaded = false;
};
RegionIndex loadRegionIndex(const std::filesystem::path& gameRoot);

// PNG encoding (used by the GLB builder and the OBJ/layer writers). Cached per
// process by image content; prewarmPng encodes a set in parallel first.
std::vector<uint8_t> encodePng(const Image& image);
void prewarmPng(const std::vector<const Image*>& images);

// Geometry only (positions/normals/UVs/walkable/theme slots), no textures.
Scene buildMesh(const forge::lev::File& level, const Options& options);

// Water surface from a scene whose `themes` carry waterHeight/waterType
// (buildScene calls this; exposed for tests). slotToLayer: LEV palette slot -> Scene::themes index.
void buildWater(const forge::lev::File& level, Scene& scene, const std::map<int, size_t>& slotToLayer, const Options& options);

// Install-wide texture state that is expensive to build (ENGINE_THEME library
// from game.bin, the textures.big index, decoded textures). Load once, reuse
// for every map; thread-safe for concurrent buildScene calls after load().
class Context {
public:
    Context();
    // Returns false and fills `error` when the install cannot be read.
    bool load(const std::filesystem::path& gameRoot,
              const std::filesystem::path& texturesBig, std::string& error);
    bool ready() const;
    std::filesystem::path gameRoot() const;
    // Decoded GBANK_MAIN_PC texture by id (cached; owned by the context). nullptr
    // with `warning` set when the id is unknown or undecodable.
    const Image* texture(uint32_t id, std::string& warning) const;
    // game.bin lookup for placed things: the definition's Graphic model id
    // (MBANK_ALLMESHES). Result codes: 1 = found (modelId set, may be 0 = no
    // model), 0 = definition not in game.bin, -1 = def type not decodable.
    int graphicModelId(const std::string& definitionName, uint32_t& modelId) const;
    // Names of every game.bin definition whose type is one of `types` (e.g.
    // {"OBJECT", "BUILDING"}), as (name, type); the editor's placement palette.
    std::vector<std::pair<std::string, std::string>> definitions(const std::vector<std::string>& types) const;
    struct Impl;
    Impl& impl() const { return *impl_; }
private:
    std::shared_ptr<Impl> impl_;
};

// Full pipeline: buildMesh + theme resolution + albedo bake (+ layer images).
// Texture failures degrade to warnings and an untextured scene, never throw,
// so the plain heightmap always comes out. Without a ready `context`, one is
// built from `options.gameRoot` / `options.texturesBig` for this call.
Scene buildScene(const forge::lev::File& level, const Options& options,
                 const Context* context = nullptr);

// GPU-format mip -> straight RGBA8. Exposed for tests and the GUI preview.
std::vector<uint8_t> decodeBc1ToRgba(const uint8_t* blocks, uint32_t w, uint32_t h);
std::vector<uint8_t> decodeBc2ToRgba(const uint8_t* blocks, uint32_t w, uint32_t h);
std::vector<uint8_t> bgra8ToRgba(const uint8_t* pixels, uint32_t w, uint32_t h);

// RGBA8 -> PNG bytes (miniz).


// Serialize a scene as a self-contained GLB (PNG images embedded) in memory.
std::vector<uint8_t> buildGlb(const Scene& scene);

// Writers. writeGlb writes one .glb. writeObj writes <stem>.obj + <stem>.mtl +
// <stem>_albedo.png. In `layers` mode both also write
// <stem>_themes/<slot>_<name>_{base,cliff}.png and <stem>.themes.json.
// Returns every path written.
std::vector<std::filesystem::path> writeGlb(const Scene& scene,
                                            const std::filesystem::path& out);
std::vector<std::filesystem::path> writeObj(const Scene& scene,
                                            const std::filesystem::path& out);

} // namespace albion::terrainexport
