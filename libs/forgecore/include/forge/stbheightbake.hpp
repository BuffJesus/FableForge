#pragma once
// Same-topology heightfield bake of one retail STB terrain chunk from an edited
// LEV: every composed background patch and foreground layer mesh gets its
// heights/normals resampled from the LEV, foreground frames are re-encoded
// (lzo1x_999 class) into the donor's page-aligned slots, the quadtree
// directory is re-wired and Z-fitted. Lifted from `forge stb bake-heightfield`
// so editors can call it; behaviour and gates are unchanged.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "forge/lev.hpp"
#include "forge/stbbake.hpp"
#include "forge/terrain.hpp"

namespace forge::stbbake {

struct HeightfieldNeighbor {
    const lev::File* lev = nullptr;   // adjacent map used to resolve edge samples
    int worldX = 0, worldY = 0;       // its WLD placement
};

// The visible water surface per 16x16 patch: the bytes CWaterPatchMesh::Save writes after
// the foreground frame's hasWater flag (header + range-compressed 289 x 66 B block), or empty
// for a dry patch. Patch coordinates are map-local cells (multiples of 16). When set, it
// decides for every re-encoded frame; when unset, donor water blocks are kept verbatim.
// `ground` is the (width+1) x (height+1) vertex height grid the bake writes into the layer
// meshes (shared edges sampled from the neighbours), row-major; the water records must be
// built against it, not the LEV alone, or the depth fade and the audit disagree on the rim.
using WaterPatchProvider = std::function<std::vector<uint8_t>(int mapPatchX, int mapPatchY, const std::vector<float>& ground)>;

struct HeightfieldBakeOptions {
    std::vector<HeightfieldNeighbor> neighbors;
    WaterPatchProvider waterPatches;
    // Water blocks rarely fit the donor's foreground allocation. With this set the re-encoded
    // foreground frames may move to a 2048-aligned region appended to the chunk (the
    // quad-directory is re-pointed either way); the chunk then grows, and the caller must
    // write it back through the variable-size STB path (replaceStaticMapsRelayout).
    bool allowForegroundGrowth = false;
    // Optional WLD-driven neighbour discovery (all three or none): loose LEVs
    // under levelsRoot named like the WLD entries.
    std::string worldPath, levelsRoot, levelName;
    bool rebuildDirectionMask = false;
    bool rebuildTopology = false;                       // regenerate layer meshes from LEV themes
    std::vector<terrain::TerrainThemeMaterial> themes;  // 256 slots; required by rebuildTopology
    // Set false to skip the 32/64-cell gate the CLI enforces (retail maps are
    // up to 224 cells); the caller then owns the in-game validation.
    bool requireCanonicalSize = true;
    // Replace every re-baked background patch's inline texture with the
    // provider's (same dimensions/format as the one it replaces, so the patch
    // keeps its slot): the distant-LOD bake for edited levels.
    BackgroundTextureProvider backgroundTextures;
};

struct HeightfieldBakeResult {
    std::vector<uint8_t> chunk;
    std::vector<std::string> notes;
    size_t patches = 0, foregroundFrames = 0;
    size_t waterPatches = 0;              // frames given a water block by the provider
    bool foregroundMoved = false;         // frames live in an appended region (chunk grew)
};

// Throws std::runtime_error / std::invalid_argument on any gate failure.
HeightfieldBakeResult bakeHeightfield(const std::vector<uint8_t>& chunkBytes,
                                      const lev::File& lev, int worldX, int worldY,
                                      const HeightfieldBakeOptions& options);

} // namespace forge::stbbake
