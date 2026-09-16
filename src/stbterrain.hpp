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

namespace albion::stbterrain {

struct CellMask {
    int width = 0, height = 0;           // map cells
    std::vector<uint8_t> present;        // width*height, 1 = rendered by the engine
    int frames = 0;                      // foreground frames parsed
    int presentCells = 0;
    bool found = false;                  // an STB entry with foreground frames existed
    std::string note;
};

// Builds the mask for a retail map. `found` false (and an all-present mask) when
// the STB has no entry, so callers can always index `present`.
CellMask load(const std::filesystem::path& gameRoot, const std::string& mapName,
              int mapWidth, int mapHeight);

} // namespace albion::stbterrain
