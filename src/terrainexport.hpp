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
//   * The foreground texture TILING scale (world units per texture repeat) is not
//     yet pinned from the engine; `Options::tileSize` is a user knob (default 4).
//   * Cliff textures are applied by slope, not by the engine's four projection
//     directions (mappingDirection 1..4 in the STB layer bake) -- a bake
//     approximation; `layers` mode gives the exact inputs.
//   * Grid topology only: every cell becomes two triangles. The engine's masked
//     16x16 patches (holes/caves) live in the STB and are not consulted yet.
//   * Textures come from the user's own install; nothing retail is embedded.

#include <cstdint>
#include <filesystem>
#include <functional>
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
    float tileSize = 4.0f;         // world units per texture repeat (see header)
    float cliffStartSlope = 0.55f; // tan(angle) where cliff blending begins (~29 deg)
    float cliffFullSlope = 1.4f;   // tan(angle) where cliff fully replaces base (~54 deg)
    bool layers = false;           // also emit splat attributes + per-slot PNGs
    bool walkableColor = false;    // COLOR_0 = walkable (white) / blocked (red)
    UpAxis up = UpAxis::Y;
    // Map-local -> world offset (Fable-space), for stitching maps by WLD placement.
    float originX = 0.0f;
    float originY = 0.0f;
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
    int unresolvedThemes = 0;       // palette slots no ENGINE_THEME could be found for
    int nameResolvedThemes = 0;     // slots resolved by NAME because the stored def index was stale
    bool walkableColor = false;
    bool layers = false;
};

// Geometry only (positions/normals/UVs/walkable/theme slots), no textures.
Scene buildMesh(const forge::lev::File& level, const Options& options);

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
std::vector<uint8_t> encodePng(const Image& image);

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
