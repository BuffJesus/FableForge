#pragma once

#include "forge/lev.hpp"
#include "forge/tng.hpp"
#include "forge/navpatch.hpp"

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

struct Line { float x0 = 0, y0 = 0, x1 = 0, y1 = 0; };
struct DetailArea { float x0 = 0, y0 = 0, x1 = 0, y1 = 0; };
struct SwitchableLines { uint64_t uid = 0; std::vector<Line> lines; };
struct GroundGeometry {
    std::vector<Line> blockingLines;
    std::vector<DetailArea> detailedAreas;
    std::vector<SwitchableLines> switchableLines;
};
struct GroundResult {
    RetailSection section;
    size_t leavesRemoved = 0;
    size_t anchorsUsed = 0;
};

// Experimental ground-layer builder from explicit geometry and a section's
// navigation positions. Rejects stacked layers; no existing section/file is
// mutated. Retains the supplied name and raw positions. Without valid anchors
// normal islands are removed (unlike generateTerrain's largest-island fallback),
// while unvisited switchable leaves survive closed in region zero.
// Caller owns quest/layer selection and thing UID recovery; missing inputs are
// not inferred from the retail tree. Use emitNavigation for a scratch LEV.
GroundResult generateGround(const lev::File& file, const RetailSection& source,
                            const GroundGeometry& geometry);

} // namespace forge::navmesh
