#pragma once
// Seam stitching between maps that share an edge in the overworld: the retail
// bake left the shared vertex row/column of every adjacent pair identical
// (Greatwood_2 x=96 == Greatwood_1 x=0 to the float). A moved or new map
// breaks that, and the engine draws each map's own heights, so the seam shows
// a step. Stitching averages the shared vertices, feathers the change a few
// cells into each map, writes both LEVs (loose + FinalAlbion.wad) and re-bakes
// both terrain chunks (deployTerrain, one-time backups).
#include <filesystem>
#include <string>
#include <vector>

#include "overworld.hpp"

namespace albion::editor {

struct StitchOptions {
    int feather = -1;         // cells the correction fades over, into each map; 0 = edge only,
                              // <0 = auto: one cell per unit of the largest step (4..32), so the ramp is at most ~45 degrees
    bool deploy = true;       // false = report only (what would change)
};

struct StitchReport {
    std::string a, b;
    int sharedVertices = 0;   // vertices along the shared edge
    float maxStep = 0;        // largest height difference across the seam before stitching
    int feather = 0;          // cells the correction was feathered over (the auto choice when asked for)
    bool stitched = false;    // false when the seam was already tight or no edge is shared
    size_t thingsReseated = 0;   // placed things moved to the new ground (both maps)
};

// The shared edge of two placed boxes as a vertex run: false when they do not
// share one (corner contact or no contact). `horizontal` = the run is along X
// (a top/bottom seam); (x0, y0) is the run's first world vertex, `len` its count.
bool sharedEdge(const WorldMapBox& a, const WorldMapBox& b, bool& horizontal, int& x0, int& y0, int& len);

// Stitch one pair. A seam whose steps are all under 1e-4 is left alone.
bool stitchEdges(const std::filesystem::path& gameRoot, const WorldLayout& layout, const std::string& mapA,
                 const std::string& mapB, const StitchOptions& options, StitchReport& report,
                 std::vector<std::string>& notes, std::string& error);

// Stitch `map` with every edge-sharing neighbour in the layout; every pair is
// attempted, the first failure is returned after the rest ran.
bool stitchNeighbours(const std::filesystem::path& gameRoot, const WorldLayout& layout, const std::string& map,
                      const StitchOptions& options, std::vector<StitchReport>& reports,
                      std::vector<std::string>& notes, std::string& error);

} // namespace albion::editor
