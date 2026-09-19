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

struct HeightfieldBakeOptions {
    std::vector<HeightfieldNeighbor> neighbors;
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
};

// Throws std::runtime_error / std::invalid_argument on any gate failure.
HeightfieldBakeResult bakeHeightfield(const std::vector<uint8_t>& chunkBytes,
                                      const lev::File& lev, int worldX, int worldY,
                                      const HeightfieldBakeOptions& options);

} // namespace forge::stbbake
