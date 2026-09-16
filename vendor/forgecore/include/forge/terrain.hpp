#pragma once

#include "forge/lev.hpp"

#include <cstddef>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace forge::terrain {

// Editable vertex-height grid for one LEV map. width/height are map extents;
// the stored vertex grid is (width+1) x (height+1).
class Heightfield {
public:
    Heightfield(int width, int height, float initialHeight = 0.0f);
    static Heightfield fromLev(const lev::File& file);

    int width() const { return width_; }
    int height() const { return height_; }
    int cellsX() const { return width_ + 1; }
    int cellsY() const { return height_ + 1; }
    float at(int x, int y) const;
    float& at(int x, int y);
    void writeTo(lev::File& file) const;

private:
    size_t index(int x, int y) const;
    int width_ = 0;
    int height_ = 0;
    std::vector<float> heights_;
};

// Bilinearly sample a normalized raster onto the (mapWidth+1)x(mapHeight+1)
// vertex grid Fable stores for a map. This is the explicit bridge for common
// NxN authored heightmaps (for example 64x64) into an NxN-cell LEV (65x65
// vertices). Output heights are baseHeight + sample*heightScale.
Heightfield fromNormalizedRaster(const std::vector<float>& samples,
                                 int sourceWidth, int sourceHeight,
                                 int mapWidth, int mapHeight,
                                 float baseHeight, float heightScale);

// Resample source-space world heights onto TLC's one-world-unit vertex grid.
// The source extent must be an integer number of target cells; rejecting any
// other extent prevents an implicit crop/stretch policy. Samples retain their
// absolute height, with only the caller-supplied source origin Z added.
Heightfield fromWorldHeightRaster(const std::vector<float>& samples,
                                  int sourceWidth, int sourceHeight,
                                  float sourceSpacing,
                                  float sourceHeightBias = 0.0f);

struct HeightfieldTile {
    int originX = 0;
    int originY = 0;
    Heightfield heightfield{0, 0};
};

// Partition a heightfield using explicit cell widths/heights. Adjacent tiles
// copy the same source border vertex, making every shared seam bit-identical.
// The layout must cover the source exactly; no remainder policy is inferred.
std::vector<HeightfieldTile> splitHeightfield(
    const Heightfield& source,
    const std::vector<int>& tileWidths,
    const std::vector<int>& tileHeights);

enum class BrushMode { RaiseLower, Flatten, Smooth };

struct Brush {
    BrushMode mode = BrushMode::RaiseLower;
    float centerX = 0.0f;
    float centerY = 0.0f;
    float radius = 1.0f;
    // RaiseLower: signed world-height delta at the center.
    // Flatten/Smooth: blend strength, conventionally 0..1.
    float amount = 1.0f;
    float targetHeight = 0.0f;
};

// Applies a circular brush with smooth radial falloff. Returns changed vertices.
size_t applyBrush(Heightfield& field, const Brush& brush);

struct ThemeBlend {
    std::array<uint8_t, 3> indices{};
    std::array<uint8_t, 3> strengths{255, 0, 0};
};

// Deterministically project an arbitrary material-weight vector into TLC's
// three theme slots. `themeIndices` is an explicit caller-supplied material ->
// TLC palette mapping: equal mapped indices are merged before the strongest
// three are selected. Weights are normalized and quantized to an exact byte
// sum of 255 by largest remainder. No semantic mapping is inferred here.
ThemeBlend projectMaterialWeights(const std::vector<float>& weights,
                                  const std::vector<uint8_t>& themeIndices);

// Bilinear row-major sampling of one normalized material raster. World bounds
// are inclusive at both ends, matching the Fable2RE preview export contract.
float sampleMaterialWeight(const std::vector<float>& raster,
                           int rasterWidth, int rasterHeight,
                           float worldX, float worldY,
                           float extentX, float extentY);

// Blend one ground theme toward full coverage while retaining a normalized
// three-slot palette. If absent, the weakest current slot is recycled.
ThemeBlend paintThemeBlend(ThemeBlend current, uint8_t themeIndex, float opacity);

struct ThemeBrush {
    float centerX = 0.0f;
    float centerY = 0.0f;
    float radius = 1.0f;
    float opacity = 1.0f;
    uint8_t themeIndex = 0;
};

size_t applyThemeBrush(lev::File& file, const ThemeBrush& brush);

struct BooleanBrush {
    float centerX = 0.0f;
    float centerY = 0.0f;
    float radius = 1.0f;
    float threshold = 0.5f;
    bool value = true;
};

size_t applyWalkabilityBrush(lev::File& file, const BooleanBrush& brush);
size_t applyPreferredPathBrush(lev::File& file, const BooleanBrush& brush);

struct LayerVertexCoord {
    uint8_t x = 0;
    uint8_t y = 0;
};

struct LayerTopology {
    std::vector<LayerVertexCoord> vertices;
    std::vector<uint16_t> indices;
    bool fullPatch = false;
};

// Retail BuildLayerMesh topology for one 16x16-cell patch. triangleMask is
// X-major: ((x * 16 + y) * 2 + triangle), matching CLayer's 512 flag bytes.
LayerTopology buildLayerTopology(const std::array<bool, 512>& triangleMask);

struct TerrainMaterialTuple {
    std::array<uint32_t, 3> textures{};
    uint32_t textureMaxSize = 0;
    uint32_t bumpMaxSize = 0;
    float selfIllumination = 0.0f;
};

struct TerrainThemeMaterial {
    bool available = false;
    TerrainMaterialTuple base;
    TerrainMaterialTuple cliff;
};

struct TerrainLayerContribution {
    TerrainMaterialTuple material;
    uint8_t mappingDirection = 0;
    uint8_t blend = 0;
};

// Retail ReadThemesAndCreateLayers contribution stage for one LEV vertex.
// Entries retain retail slot/base/cliff direction ordering after tuple merges.
std::vector<TerrainLayerContribution> buildThemeContributions(
    const ThemeBlend& blend, const std::vector<TerrainThemeMaterial>& themes);

struct TilePlacement {
    int worldX = 0;
    int worldY = 0;
};

enum class SharedEdge { None, West, East, North, South };

struct StitchResult {
    SharedEdge edge = SharedEdge::None;
    int overlapStart = 0;
    int overlapEnd = -1;
    size_t boundaryVertices = 0;
    size_t verticesChanged = 0;
};

// Matches target to an adjacent neighbor in world-map coordinates. The shared
// edge is made exact; the edge correction is tapered into target by blendWidth
// vertices. Partial edge overlaps are supported.
StitchResult stitchSharedBoundary(Heightfield& target,
                                  TilePlacement targetPlacement,
                                  const Heightfield& neighbor,
                                  TilePlacement neighborPlacement,
                                  int blendWidth);

const char* edgeName(SharedEdge edge);

} // namespace forge::terrain
