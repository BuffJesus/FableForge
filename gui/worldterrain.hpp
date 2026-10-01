#pragma once
#include "renderer.hpp"
#include "terrainlod.hpp"

namespace albion::gui {
// Call after overview height/normal assignment. A failed validation leaves the
// caller's original geometry intact. Successful results reuse all vertices.
inline terrainlod::Result prepareWorldTerrainLods(const Renderer::PreparedBatch& batch) {
    if (!batch.terrainMorph || batch.water || batch.alpha) return {};
    return terrainlod::build(batch.vertices, batch.indices);
}
}
