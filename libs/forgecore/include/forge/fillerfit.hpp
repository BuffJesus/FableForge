#pragma once
// The vanilla editor's "Fit Neighbours" (FableWin CEditWorldMap::EditFitFillerMap
// 0x0297dab0, opened from the world-map popup), ported from its code, not
// re-invented. It rebuilds a whole FILLER map as a ridge that meets every
// neighbour at the seam:
//   1. a ring round the map's border (top L->R, right T->B, bottom R->L, left
//      B->T) takes the heights of every touching map's shared edge; the rest is
//      the sentinel -200
//   2. every sentinel run is filled with a cardinal spline (tension = Tens) over
//      {before, mean, after}, parameterised by WrapDifference along the ring
//   3. the ring is written onto the map's border
//   4. each interior column becomes a 5-point cardinal spline from its top to its
//      bottom height: edge, PHt*TStp + edge, max(edges) + PHt, PHt*TStp + edge,
//      edge (each inner point + GFRandom(trunc(LoNs)))
//   5. each interior row the same from its left to its right height, blended with
//      the column pass by the distance to the nearest edge, + GFRandom(trunc(HiNs))
//   6. one in-place 3x3 mean over the interior (x outer, y inner)
// Helpers ported with it: CCardinalSpline::GetSplinePosition 0x0324b730,
// GFGetHermiteSplinePosition 0x0324b560, WrapDifference 0x017c0137, GFRandom
// 0x018cf370 (LCG *0x24a1 +0x24df, ror 13; the fit's seed starts at 0x346780).
//
// One adaptation: vanilla indexes the box's w cells per side; FableForge's LEVs
// carry w + 1 vertices with neighbours SHARING their edge vertex row (the stitch
// convention, checked in game), so the fit runs over that vertex grid with the
// shared rows copied inclusively -- the seam then closes exactly.
// Float32 steps mirror the x87 code.

#include <cstdint>
#include <string>
#include <vector>

namespace forge::fillerfit {

// The world-map popup's fields and their start values.
struct Params {
    float peakHeight = 0.0f;   // "PHt": how far the ridge rises above the higher edge
    float step = 1.75f;        // "TStp": the shoulders sit at edge + PHt * TStp
    float tension = 0.4f;      // "Tens": cardinal-spline tension (0.5 = Catmull-Rom)
    float lowNoise = 1.5f;     // "LoNs": each shoulder / peak point + GFRandom(trunc(LoNs)) whole units
    float highNoise = 1.5f;    // "HiNs": each interior vertex + GFRandom(trunc(HiNs)) whole units
};

// A touching map: its box in world cells (x1 = x0 + cellsX - 1) and its heights.
struct Neighbour {
    std::string name;
    int x0 = 0, y0 = 0, cellsX = 0, cellsY = 0;
    std::vector<float> heights;   // cellsX * cellsY, row-major
};

struct Report {
    int sidesFound = 0;           // border vertices taken from a neighbour
    int sidesFilled = 0;          // border vertices the ring spline filled
    std::vector<std::string> north, east, south, west;   // neighbour names per side
};

constexpr float kNoNeighbour = -200.0f;   // the ring's sentinel

// The border ring (2W + 2H) for a map of cellsX x cellsY vertices at (x0, y0).
std::vector<float> edgeRing(int x0, int y0, int cellsX, int cellsY, const std::vector<Neighbour>& neighbours, Report* report = nullptr);

// Fit `heights` (cellsX * cellsY) in place. False (untouched) when no neighbour
// touches the map -- vanilla would write the sentinel onto the border then.
bool fit(std::vector<float>& heights, int x0, int y0, int cellsX, int cellsY,
         const std::vector<Neighbour>& neighbours, const Params& params, Report* report = nullptr);

// The pieces, exposed for tests.
float cardinalSpline(const std::vector<float>& points, float tension, float s);
uint32_t gfRandom(uint32_t range, uint32_t& seed);

} // namespace forge::fillerfit
