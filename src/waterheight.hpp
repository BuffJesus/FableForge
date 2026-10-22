#pragma once

#include <algorithm>

namespace albion::terrainexport::waterheight {

struct WaterHeightPoint {
    float ground = 0.0f, depth = 0.0f;
    bool ice = false;
    bool waterTheme = false; // type presence, independent of depth/blend
};

struct WaterHeightResult {
    float height = 0.0f;
    int samples = 0, iceSamples = 0;
};

// The local-map path of CEngineMap::PeekInterpolatedWaterHeight
// (FableWin 02D5DB50). Both native loops visit X first, then Y. In particular,
// the first water-themed point selected for a dry vertex changes the window.
// Missing neighboring maps are outside this preview kernel's input contract.
template<class Sample>
WaterHeightResult interpolateWaterHeight(int width, int height, int x, int y,
                                         Sample&& sample, int radius = 2) {
    int centerX = x, centerY = y;
    const auto here = sample(x, y);
    if (!here.waterTheme || here.depth <= 0.001f) {
        bool found = false;
        for (int i = std::max(0, x - radius); i <= std::min(width - 1, x + radius) && !found; ++i)
            for (int j = std::max(0, y - radius); j <= std::min(height - 1, y + radius) && !found; ++j)
                // PeekHasWaterFast checks the theme type, even at zero blend.
                if (sample(i, j).waterTheme) {
                    centerX = i; centerY = j; found = true;
                }
        if (!found) return {};
    }
    WaterHeightResult result;
    float sum = 0.0f;
    for (int i = std::max(0, centerX - radius); i <= std::min(width - 1, centerX + radius); ++i)
        for (int j = std::max(0, centerY - radius); j <= std::min(height - 1, centerY + radius); ++j) {
            const auto point = sample(i, j);
            if (point.depth > 0.001f) {
                // Retain the preview's float arithmetic. Native execution is
                // precision-dependent; the oracle sets its x87 mode explicitly.
                sum += point.ground + point.depth;
                ++result.samples;
                result.iceSamples += point.ice;
            }
        }
    if (result.samples) result.height = sum / float(result.samples);
    return result;
}

} // namespace albion::terrainexport::waterheight
