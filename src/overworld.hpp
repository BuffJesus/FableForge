#pragma once
// Overworld editing: where every map sits in the world. The layout is the
// join of the compiled BWD (box per map slot), the text WLD (MapX/MapY, region
// ownership) and the STB info blocks (the origin the terrain chunk was baked
// for). Moving a map rewrites all three: WLD MapX/MapY, BWD box (root +
// data/Levels + data/Levels/FinalAlbion mirrors), STB info block origin +
// camera bounds and the terrain chunk re-baked for the new origin (vertex grid
// coordinates and the background-LOD quad directory are absolute). Neighbours
// whose shared edge changed are re-baked as well so seam samples stay right.
// Thing positions (.tng) are map-local and untouched. Debug-editor model:
// CEditWorldMap; the engine's own rule for what counts as a neighbour is
// "owned or seen by one of my regions AND touching", kept here.

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace albion::editor {

struct WorldMapBox {
    int slot = 0;              // BWD/WLD map slot (1-based)
    std::string name;          // level stem, e.g. "LookoutPoint"
    std::string levelName;     // WLD spelling: FinalAlbion\LookoutPoint.lev
    int x = 0, y = 0;          // MapX/MapY (BWD left/top), world units (cells)
    int w = 0, h = 0;          // BWD box size
    std::string region;        // owning region (first ContainsMap owner); "" if unowned
    int regionSlot = 0;        // WLD region index of the owner (0 = none)
    bool isSea = false;
    bool loadedOnProximity = false;
    bool inStb = false;        // has a terrain chunk in FinalAlbion_RT.stb
    int stbX = 0, stbY = 0;    // origin the chunk was baked for (info block); differs from x/y only when broken
};

struct WorldLayout {
    std::vector<WorldMapBox> maps;
    std::vector<std::string> regions;   // WLD order (index = slot - 1)
    int minX = 0, minY = 0, maxX = 0, maxY = 0;   // bounds of every box
    const WorldMapBox* find(const std::string& name) const;
    // Boxes touching `box` if it stood at (x, y): edge- or corner-adjacent or overlapping.
    std::vector<const WorldMapBox*> touching(const WorldMapBox& box, int x, int y) const;
};

bool loadWorldLayout(const std::filesystem::path& gameRoot, WorldLayout& out, std::string& error);

struct MapMove { std::string name; int x = 0, y = 0; };

// The rules a move must pass (also what the canvas shows while dragging):
// 32-aligned, inside the u16 patch grid, no overlap with any box that is not
// itself part of `moves`.
bool checkMove(const WorldLayout& layout, const std::vector<MapMove>& moves, const MapMove& move, std::string& why);

// Apply every move in one go. One-time .atlas-orig backups of the four
// containers. Notes list what was re-baked; the STB is rewritten in place when
// every re-baked chunk kept its size (the usual case) and re-laid otherwise.
bool applyMoves(const std::filesystem::path& gameRoot, const std::vector<MapMove>& moves,
                std::vector<std::string>& notes, std::string& error);

} // namespace albion::editor
