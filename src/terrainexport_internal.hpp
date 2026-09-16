#pragma once
// Internal seams shared by the terrain and foliage writers.
#include <filesystem>
#include <vector>
#include "glbwriter.hpp"
#include "terrainexport.hpp"

namespace albion::terrainexport {
// Adds the terrain mesh/material/node to `b`; returns the node index.
int appendTerrain(glb::Builder& b, const Scene& scene);
// Layer PNGs + themes.json next to `out` (no-op unless scene.layers).
std::vector<std::filesystem::path> writeLayerSidecars(const Scene& scene, const std::filesystem::path& out);
} // namespace albion::terrainexport
