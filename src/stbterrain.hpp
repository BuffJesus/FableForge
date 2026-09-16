#pragma once
// albion::stbterrain -- which terrain cells the engine actually renders.
//
// The .lev grid is a full rectangle, but the engine draws terrain from the
// baked STB "foreground" frames: per 16x16 patch, per theme layer, an explicit
// vertex list + triangle strip. Cells nobody strips are holes: cave ceilings,
// map corners, the inside of buildings. This module parses those frames
// (CLandscapeForegroundPatch grammar, same layout FableForge's baker emits) and
// marks every cell touched by a real triangle. Cells never touched are absent
// from the export.

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
