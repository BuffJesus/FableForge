#pragma once
// albion::stbterrain -- which terrain cells the engine actually renders
// (DIAGNOSTIC; `AlbionAtlas coverage <map>`).
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

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

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

// Builds the mask for a retail map. `found` false (and an all-present mask) when
// the STB has no entry, so callers can always index `present`.
CellMask load(const std::filesystem::path& gameRoot, const std::string& mapName,
              int mapWidth, int mapHeight);

} // namespace albion::stbterrain
