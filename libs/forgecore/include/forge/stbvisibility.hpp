#pragma once

#include "forge/bwd.hpp"
#include "forge/terrain.hpp"
#include <array>
#include <functional>
#include <span>
#include <vector>

namespace forge::stbvisibility {

// Native order: minX,minY,minZ,maxX,maxY,maxZ.
using Bounds = std::array<float, 6>;

// Explicit diagnostic arithmetic modes. Neither asserts the running editor's
// FPU configuration. The native magnitude rounds its squared sum to float.
enum class Precision { Float24, Float53 };
struct Distances { float nearest = 1.0e30f; float furthest = 0.0f; };
Distances observingDistances(const Bounds& query, std::span<const Bounds> observers,
                             Precision precision);

struct ForegroundCull {
    std::vector<Bounds> observers;
    float maximumDistance = 0.0f;
    Precision precision = Precision::Float24;
};
// No policy disables culling. A present policy with no observers culls all
// patches at ordinary finite distances. The native comparison is inclusive.
bool foregroundNeeded(const Bounds& patch, const ForegroundCull* policy);

// Scans only owned cells, excluding the overlap vertex row/column. Heights are
// supplied samples; no guessed LEV quantization or camera-height default.
Bounds cameraBounds(const terrain::Heightfield& heights,
                    const std::array<float, 3>& origin, float cameraHeight);

// Background templates include the trailing border vertices. Their bottom
// follows ground alone; their top encloses both ground and interpolated water.
// Samples are map-local native query results (water interpolation radius 1).
// An absent sampler represents the native missing-game-map fallback, not dry
// water: both local Z bounds become zero. Origin translates all three axes.
using SurfaceSampler = std::function<std::array<float, 2>(int x, int y)>;
Bounds backgroundBounds(int mapX, int mapY, int width, int height,
                        const std::array<float, 3>& origin,
                        const SurfaceSampler& surfaceAt);

struct BackgroundBands {
    uint8_t first = 1, last = 7;
    std::array<bool,8> needed{};
};
// Native visibility-only decisions, before geometry lock success/failure.
// Absent visibility uses nearest=0/furthest=1e30; an empty set uses 1e30/0.
BackgroundBands backgroundBands(Distances distance, float firstLodZ, int lastBand);
struct BackgroundCull {
    std::vector<Bounds> observers;
    Precision precision = Precision::Float24;
    SurfaceSampler surfaceAt; // required, supplying native ground/water samples
};

struct Graph {
    bool enabled = false;
    // Index 0 is unused; all values are compiled BWD slots, not vector indices.
    std::vector<std::vector<int32_t>> visibleFrom;
    std::vector<std::vector<int32_t>> observedBy;
};
// Native last-containing-region assignment, rather than a union of owners.
// Disabled visibility is distinct from an enabled graph with empty observers.
Graph buildGraph(const bwd::File& world, bool enabled);

} // namespace forge::stbvisibility
