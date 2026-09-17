#pragma once
// Distant-LOD textures for authored terrain chunks: the map's top-down albedo
// (LEV themes through the ENGINE_THEME textures, 4 texels per cell like the
// retail 64x64-per-16-cell patches) sliced per background-tree node into the
// 64x64 DXT1 single-mip inline texture CLandscapeBackgroundPatch carries.
#include <filesystem>
#include <functional>
#include <string>

#include "forge/lev.hpp"
#include "forge/stbbake.hpp"
#include "terrainexport.hpp"

namespace albion::editor {

struct LodAlbedo {
    terrainexport::Image image;   // texelsPerCell texels per LEV cell, row 0 = map y 0
    int texelsPerCell = 16;      // baked fine, box-filtered down to the tile (retail tiles are smooth)
    bool textured = false;        // false = flat theme colours (no textures.big)
};

// Bake the top-down albedo of `lev` (needs the install's game.bin + textures.big
// for real texels; falls back to flat colours). `gain` matches the in-game look.
LodAlbedo bakeLodAlbedo(const std::filesystem::path& gameRoot, const forge::lev::File& lev, float gain = 1.0f);

// A provider for stbbake::buildTerrainChunk64 / bakeHeightfield: the node rect
// (map cells) is resampled (box filter) to the wanted texture size (64x64 when
// the caller leaves it open) and DXT1-encoded. Texture row 0 = the rect's
// lowest map Y (pinned against retail chunks with `lod-check`: Greatwood_1
// 0.65 vs 0.49 flipped, HookCoast 0.58 vs 0.38).
forge::stbbake::BackgroundTextureProvider lodTextureProvider(const LodAlbedo& albedo);

// RGBA tile of the albedo over a cell rect (the provider's input to DXT1).
terrainexport::Image lodTile(const LodAlbedo& albedo, int x, int y, int w, int h, bool flipY, int tw = 64, int th = 64);

} // namespace albion::editor
