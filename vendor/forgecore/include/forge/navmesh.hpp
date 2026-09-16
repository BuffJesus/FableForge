#pragma once

#include "forge/lev.hpp"
#include "forge/tng.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace forge::navmesh {

struct GenerateResult {
    std::vector<uint8_t> levBytes;
    size_t sections = 0;
    size_t recordsPerSection = 0;
    size_t internalNodes = 0;
    size_t navigableLeaves = 0;
    size_t blockedRootRecords = 0;
    size_t regions = 0; // includes region 0 (none), matching the file header
    size_t walkableCells = 0;
    size_t islandCellsRemoved = 0;
    size_t anchorCount = 0;
};

// Rebuild a terrain-only, single-layer CNavQuadTree suffix. Existing section
// names and CNavigationPosition records are retained. TNG seeds/entrances/exits
// select reachable walkable components; without anchors, the largest component
// survives. Object collision, switchable blockers, and stacked layers are not
// inferred by this generator.
GenerateResult generateTerrain(const lev::File& file,
                               const tng::File* tngFile = nullptr);

} // namespace forge::navmesh
