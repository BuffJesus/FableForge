#pragma once
#include <algorithm>
#include <cmath>

namespace albion::worldview {

// Free flight keeps its orbit focus distance. Derive clipping from actual ground
// clearance so a descent from the overview does not retain a metres-wide blind area.
inline float nearPlane(float groundClearance) {
    return std::clamp(groundClearance * 0.002f, 0.1f, 20.0f);
}

// World free flight is independent of orbit focus distance. Enclose all loaded
// world geometry even after zooming close and then flying up or away.
inline float farPlane(const float* eye, const float* lo, const float* hi) {
    float squared = 0;
    for (int axis = 0; axis < 3; ++axis) {
        const float d = std::max(std::abs(lo[axis] - eye[axis]), std::abs(hi[axis] - eye[axis]));
        squared += d * d;
    }
    return std::max(1000.0f, std::sqrt(squared) + 32.0f);
}

// Row-vector D3D clip space: -w <= x,y <= w and 0 <= z <= w.
inline bool visible(const float* matrix, const float* lo, const float* hi) {
    for (int plane = 0; plane < 6; ++plane) {
        float p[4];
        for (int row = 0; row < 4; ++row) {
            const float* m = matrix + row * 4;
            p[row] = plane == 4 ? m[2] : plane == 5 ? m[3] - m[2]
                : m[3] + (plane % 2 ? -1.0f : 1.0f) * m[plane / 2];
        }
        float farthest = p[3];
        for (int axis = 0; axis < 3; ++axis) farthest += p[axis] * (p[axis] >= 0 ? hi[axis] : lo[axis]);
        if (farthest < -0.001f) return false;
    }
    return true;
}

// Sustained frame pressure lowers detail quickly; spare time restores it slowly.
// A loading stall or focus loss must not be mistaken for steady rendering cost.
struct DetailBudget {
    int maps = 6;
    float slowSeconds = 0, fastSeconds = 0;
    void observe(float dt, int ceiling, bool eligible) {
        maps = std::clamp(maps, 1, ceiling);
        if (!eligible || !std::isfinite(dt) || dt <= 0 || dt > 0.25f) { slowSeconds = fastSeconds = 0; return; }
        slowSeconds = dt > 1.0f / 45 ? slowSeconds + dt : 0;
        fastSeconds = dt < 1.0f / 55 ? fastSeconds + dt : 0;
        if (slowSeconds >= 2) { maps = std::max(1, maps - 1); slowSeconds = fastSeconds = 0; }
        if (fastSeconds >= 5) { maps = std::min(ceiling, maps + 1); slowSeconds = fastSeconds = 0; }
    }
};

} // namespace albion::worldview
