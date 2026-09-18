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
    std::string name;          // level stem, e.g. "LookoutPoint" (what the WLD/WAD/STB key on)
    std::string scriptName;    // BWD script name; differs from the stem for ten retail maps
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

struct WorldRegion {
    int slot = 0;
    std::string name;
    std::vector<std::string> contains;  // map stems (owned)
    std::vector<std::string> sees;      // map stems (loaded/visible from this region)
    bool sees_(const std::string& stem) const;
    bool owns(const std::string& stem) const;
};

struct WorldLayout {
    std::vector<WorldMapBox> maps;
    std::vector<std::string> regions;   // WLD order (index = slot - 1)
    std::vector<WorldRegion> regionInfo;
    const WorldRegion* region(const std::string& name) const;
    int minX = 0, minY = 0, maxX = 0, maxY = 0;   // bounds of every box
    const WorldMapBox* find(const std::string& name) const;
    // Boxes touching `box` if it stood at (x, y): edge- or corner-adjacent or overlapping.
    std::vector<const WorldMapBox*> touching(const WorldMapBox& box, int x, int y) const;
};

bool loadWorldLayout(const std::filesystem::path& gameRoot, WorldLayout& out, std::string& error);

struct MapMove { std::string name; int x = 0, y = 0; };

// The engine's world placement grid: CWorld::Init builds CWorldMap over the
// box (0,0)-(8192,8192) in 32-unit cells; a map placed past it indexes outside
// the grid (retail's maps end at 5216 x 8160).
constexpr int kWorldExtent = 8192;

// The rules a move must pass (also what the canvas shows while dragging):
// 32-aligned, inside the u16 patch grid, no overlap with any box that is not
// itself part of `moves`.
bool checkMove(const WorldLayout& layout, const std::vector<MapMove>& moves, const MapMove& move, std::string& why);

// Region edits: which region owns a map (exactly one: the retail partition)
// and which regions see it (a region loads/draws every map it sees while the
// player is in it; a moved map wants its new neighbours' regions to see it and
// its own region to see them).
struct OwnerEdit { std::string map, region; };
struct SeesEdit { std::string region, map; bool sees = true; };

// Apply moves and region edits in one go. One-time .atlas-orig backups of the
// four containers. The WLD is edited line-precisely; the BWD is compiled from
// the edited WLD (forgecore's compileFromWld, checked byte-exact against the
// current BWD before anything is written; a mismatch falls back to box edits
// and refuses region edits). Moved maps get their terrain chunk translated in
// FinalAlbion_RT.stb; touching neighbours re-bake best-effort.
bool applyWorldEdits(const std::filesystem::path& gameRoot, const std::vector<MapMove>& moves,
                     const std::vector<OwnerEdit>& owners, const std::vector<SeesEdit>& sees,
                     std::vector<std::string>& notes, std::string& error);
// Region properties the engine reads from the BWD record (mirrored into the
// WLD text so the two stay in step): the REGION_* def (atmosphere, name text,
// creature generation -- an empty one loads but names the region "" and draws
// the ground unlit white), the MINIMAP_* graphic and the world-map flag.
// Empty strings leave a field alone.
struct RegionProps {
    std::string regionDef;        // e.g. REGION_GREATWOOD_TELEPORT
    std::string minimapGraphic;   // e.g. MINIMAP_GREATWOOD
    std::string displayName;      // TXT_... key or plain text
    int onWorldMap = -1;          // 0/1, -1 = keep
};
bool setRegionProperties(const std::filesystem::path& gameRoot, const std::string& region, const RegionProps& props,
                         std::vector<std::string>& notes, std::string& error);

inline bool applyMoves(const std::filesystem::path& gameRoot, const std::vector<MapMove>& moves,
                       std::vector<std::string>& notes, std::string& error) {
    return applyWorldEdits(gameRoot, moves, {}, {}, notes, error);
}

} // namespace albion::editor
