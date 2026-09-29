#pragma once
// A whole-world overview (the vanilla editor's world map + 3D engine view over every map):
// each map as a small tile -- a decimated height grid and a top-down ground picture -- placed
// at its WLD MapX / MapY. Tiles include compact persistent water and are cached on disk,
// keyed by the source level and bake dependencies, to avoid rebuilding on the next opening.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "foliageexport.hpp"
#include "terrainexport.hpp"

namespace forge::lev { class File; }

namespace albion::worldtiles {

struct Tile {
    std::string name;
    int cellsX = 0, cellsY = 0;        // the map's vertex grid (width + 1, height + 1)
    int stride = 1;                    // vertices kept: every `stride`-th, plus the far edge
    int gw = 0, gh = 0;                // the decimated grid
    std::vector<float> heights;        // gw * gh, row-major (row 0 = map y 0), Fable z
    float minH = 0, maxH = 0;
    terrainexport::Image ground;       // top-down colour, row 0 = map y 0
    terrainexport::WaterMesh water;    // persistent water, map-local Z up
};

// The decimation for a map: about `target` quads along its longer side (at least 1 unit).
int strideFor(int cellsX, int cellsY, int target = 40);

// Merge only flat, identically shaded unit quads; retain shores, holes and ice transitions.
// Unsupported topology is returned unchanged. No height/fade approximation is made.
terrainexport::WaterMesh compactWater(const terrainexport::WaterMesh& water, int cellsX, int cellsY);

// A tile from a .lev. With a texture context the ground picture is the baked albedo at one
// texel per cell (capped at `maxTexels` on the longer side); without one it is a height tint.
Tile buildTile(const forge::lev::File& level, const std::string& name, const terrainexport::Context* context,
               const std::filesystem::path& gameRoot, float gain, int maxTexels = 256);

// Grid position i of `count` samples along `cells` vertices at `stride`: i * stride, the last
// one pinned to the far edge so neighbouring tiles meet.
int sampleIndex(int i, int count, int cells, int stride);

// Bilinear height at a map-local point (clamped to the tile).
float heightAt(const Tile& tile, float localX, float localY);
// Height on the actual overview triangles, used for a hole-free geometry transition.
float meshHeightAt(const Tile& tile, float localX, float localY);

// The tile as one mesh in map-local Fable axes (Z up), textured with image 0 of `scene`.
void appendMesh(const Tile& tile, foliageexport::Scene& scene, float worldX, float worldY);

// Disk cache: `key` identifies sources/settings; a stale, malformed or foreign file fails.
bool saveTile(const std::filesystem::path& file, const std::string& key, const Tile& tile);
bool loadTile(const std::filesystem::path& file, const std::string& key, Tile& tile);

} // namespace albion::worldtiles
