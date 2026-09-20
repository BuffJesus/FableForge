#pragma once
// albion::stbterrain -- which terrain cells the engine actually renders
// (DIAGNOSTIC; `forge coverage <map>`).
//
// The engine draws terrain from the baked STB "foreground" frames: per 16x16
// patch, per theme layer, a vertex list + either a triangle strip or the shared
// full-grid index buffer. This module parses those frames (the
// CLandscapeForegroundPatch grammar FableForge's baker emits) and marks every
// cell a triangle touches.
//
// FINDING (2026-09-15, measured on Arena, Bowerstone, Greatwood, GuildWoods,
// LookoutPoint, HobbeCaveEntranceTunnel): every cell of every map is drawn.
// Fable terrain has no masked-out cells; caves are heightfield floors enclosed
// by mesh walls/ceilings. The exporter therefore emits the full grid and this
// module is kept only as a parser check.

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "forge/stbbake.hpp"
#include "terrainexport.hpp"

namespace albion::stbterrain {

struct CellMask {
    int width = 0, height = 0;           // map cells
    std::vector<uint8_t> present;        // width*height, 1 = rendered by the engine
    int frames = 0;                      // foreground frames parsed
    int presentCells = 0;
    bool found = false;                  // an STB entry with foreground frames existed
    std::string note;
};

// The engine's own low-resolution ground colour: every background patch frame
// carries a 64x64 DXT1 texture of the blended themes for its 16x16 cells (what
// the game draws at distance). Assembled into one image at 4 texels per cell,
// row 0 = map y 0. `found` false when the STB has no usable frames.
struct BackgroundAlbedo {
    bool found = false;
    int texelsPerCell = 4;
    terrainexport::Image image;
    int patches = 0;
    std::string note;
};
BackgroundAlbedo backgroundAlbedo(const std::filesystem::path& gameRoot, const std::string& mapName,
                                  int mapWidth, int mapHeight);

// The engine's foreground layer bake, exactly as it draws it: per 16x16 patch a
// list of texture passes, each with a mapping direction (0 flat: u=x/8 v=y/8;
// 1..4 cliff projections: u = -x/8, +x/8, +y/8, -y/8 and v = -z/8), a texture
// id, and the vertices it covers with three per-vertex bytes (blend, and two
// more the STB baker calls cliffU/cliffV). Vertices are map-local.
struct LayerVertex { int x = 0, y = 0; float height = 0; uint8_t blend = 0, b1 = 0, b2 = 0; };
struct ForegroundLayer {
    int patchIndex = 0;                  // frame index in the chunk
    int layerIndex = 0;                  // position within the frame (draw order)
    uint8_t direction = 0;
    uint32_t texture = 0, backgroundTexture = 0, bumpTexture = 0;
    bool sharedIndexBuffer = false;
    float selfIllumination = 0;
    std::vector<LayerVertex> vertices;
    std::vector<uint16_t> strip;         // empty when the shared 16x16 index buffer is used
};
struct ForegroundLayers {
    bool found = false;
    int frames = 0;
    std::vector<ForegroundLayer> layers;
    std::string note;
};
ForegroundLayers loadLayers(const std::filesystem::path& gameRoot, const std::string& mapName,
                            int mapWidth, int mapHeight);

// Builds the mask for a retail map. `found` false (and an all-present mask) when
// the STB has no entry, so callers can always index `present`.
CellMask load(const std::filesystem::path& gameRoot, const std::string& mapName,
              int mapWidth, int mapHeight);

// The baked water surface: CWaterPatchMesh::Save per foreground frame that has water
// (FableTLC docs/engine/WATER_RE.md). Header i32 Offset.x/y (patch origin in map cells),
// f32 WaterBodySpan, i32 WaterType, then a CRangeCompressor block of 289 x 66-byte records
// (the 17x17 vertex grid): u16 world x/y, f32 z, i16 wave sin/cos, i16 depth, f32
// distToShore, 12 x f32 shore look-up. The block is kept so a writer can be compared
// byte for byte against it.
struct WaterPatchRecord {
    uint16_t x = 0, y = 0;
    float z = 0;
    int16_t waveS = 0, waveC = 0, depth = 0;
    float distToShore = 0;
    float shore[12] = {};
};
struct WaterPatch {
    int frameIndex = 0;                  // foreground frame number (0-based, same order as loadLayers)
    int32_t offsetX = 0, offsetY = 0;    // patch origin, map cells
    float span = 0;
    int32_t waterType = 0;
    std::vector<uint8_t> block;          // the range-compressed block as stored
    std::vector<WaterPatchRecord> records;   // 289, decoded
};
struct WaterPatches {
    bool found = false;                  // an STB entry with foreground frames existed
    int frames = 0;                      // foreground frames seen
    int worldX = 0, worldY = 0;
    std::vector<WaterPatch> patches;
    std::string note;
};
WaterPatches loadWaterPatches(const std::filesystem::path& gameRoot, const std::string& mapName);
constexpr size_t kWaterRecordSize = 0x42;
constexpr size_t kWaterRecordCount = 17 * 17;
// One record in its on-disk form.
void packWaterRecord(const WaterPatchRecord& r, uint8_t* out);
WaterPatchRecord unpackWaterRecord(const uint8_t* in);

// The distant water: CEngineWaterBackgroundSubPatch::Save at the end of a background patch's
// trailer (after the four CPatchTesselationEdgeStrips and the water flag). Built by
// CWaterGenerator::BuildStaticMapBackgroundBuffers from the patch's own landscape mesh: every
// triangle with a vertex near water contributes its vertices, z = the interpolated water height.
//   u16 vertexCount, u16 polyCount, i32 WaterType, i32 stride (0x38 lake/river/ice: u16 x, u16 y,
//   f32 z, f32 shore[12]; 0x0c sea: f32 x, f32 y, f32 z), i32 len + VB block, [i32 len + IB block]
struct BackgroundWaterVertex { float x = 0, y = 0, z = 0; float shore[12] = {}; };
struct BackgroundWaterPatch {
    int frameIndex = 0;
    int coordX = 0, coordY = 0, pw = 0, ph = 0;   // the background patch (map-local cells)
    int patchVertices = 0;                        // the landscape mesh's vertex count
    int32_t waterType = 0, stride = 0;
    std::vector<BackgroundWaterVertex> vertices;  // world coordinates as stored
    std::vector<uint16_t> indices;                // 3 per triangle
    size_t trailerWaterOffset = 0;                // where the water flag sits in the trailer
    forge::stbbake::PatchHeader header;           // the patch, for a writer comparison
    std::vector<forge::stbbake::PatchVertex> meshVertices;
    std::vector<std::array<uint16_t, 3>> meshTriangles;
};
struct BackgroundWater {
    bool found = false;
    int patches = 0;                              // background patches seen
    int worldX = 0, worldY = 0;
    std::vector<BackgroundWaterPatch> water;      // those with a sub-patch
    std::string note;
};
BackgroundWater loadBackgroundWater(const std::filesystem::path& gameRoot, const std::string& mapName);
// Offset of the water flag byte inside a background patch trailer (past the four edge strips),
// or npos when the trailer does not parse.
size_t trailerWaterFlagOffset(const std::vector<uint8_t>& trailer);

} // namespace albion::stbterrain
