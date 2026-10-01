#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

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

// Keep room for the editor and other GPU clients. Resource accounting excludes
// driver overhead, so reserve 50% above the largest observed map.
// Missing telemetry never authorizes expansion beyond the conservative six maps.
inline int memoryMapCeiling(bool valid, uint64_t budget, uint64_t usage,
                            uint64_t detailBytes,
                            uint64_t largestMapBytes, int userCeiling) {
    userCeiling = std::clamp(userCeiling, 1, 32);
    if (!valid || !budget) return std::min(6, userCeiling);
    constexpr uint64_t MiB = 1024u * 1024u;
    const uint64_t nonDetail = usage - std::min(usage, detailBytes);
    const uint64_t reserve = std::max(256 * MiB, budget / 5);
    const uint64_t room = budget - std::min(budget, nonDetail);
    const uint64_t usable = room - std::min(room, reserve);
    // Inactive cache memory remains part of usage and cannot fund expansion.
    const uint64_t perMap = std::max(64 * MiB, largestMapBytes + largestMapBytes / 2);
    return int(std::clamp<uint64_t>(usable / perMap, 1, uint64_t(userCeiling)));
}

// A resident neighbour gets hysteresis, but cannot starve the map under the eye.
// Other maps keep their distance ordering independent of viewing direction.
inline float detailPriority(float distance, float radius, bool resident, bool underfoot) {
    return distance - radius * (underfoot ? 0.2f : resident ? 0.1f : 0.0f);
}

// Terrain settles quickly; objects ease in/out over a longer interval. A single
// reversible progress value keeps camera reversals continuous and bounds retirement.
struct DetailFade {
    static constexpr float seconds = 0.6f;
    static float terrain(float progress) { return std::clamp(progress * (seconds / 0.25f), 0.0f, 1.0f); }
    static float objects(float progress) {
        const float t = std::clamp(progress, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }
};

// Authored object LODs are selected by projected bounding-sphere diameter.
// Distance and projection are world-camera values, independent of map ownership.
inline float lodPixels(unsigned level) { return 180.0f * std::pow(0.45f, float(level)); }
inline float smoothCoverage(float value) {
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t*t*(3-2*t);
}
inline float objectCoverage(float distance, float radius, float projection,
                            float nearPixels, float farPixels, float drawDistance) {
    const float pixels = 2 * std::max(radius, 0.001f) * projection / std::max(distance, 0.001f);
    const float nearFade = nearPixels > 0 ? 1 - smoothCoverage((pixels / nearPixels - 0.9f) / 0.2f) : 1;
    const float farFade = farPixels > 0 ? smoothCoverage((pixels / farPixels - 0.9f) / 0.2f) : 1;
    // Keep large buildings until their near surface reaches the draw limit.
    const float rangeFade = 1 - smoothCoverage((std::max(0.0f, distance-radius) / std::max(drawDistance, 1.0f) - 0.85f) / 0.15f);
    return std::max(0.0f, farFade - (1-nearFade)) * rangeFade;
}

// Use a larger upload slice while frames are responsive; a slow/invalid sample
// immediately returns to the conservative slice. Driver allocations can overrun
// a slice, so the caller checks elapsed time between allocations.
inline int detailUploadMilliseconds(float dt) {
    return std::isfinite(dt) && dt > 0 && dt <= 1.0f/45 ? 4 : 2;
}

// Sustained frame pressure lowers detail quickly; spare time restores it slowly.
// A loading stall or focus loss must not be mistaken for steady rendering cost.
struct DetailBudget {
    int maps = 6;
    float slowSeconds = 0, fastSeconds = 0;
    void observe(float dt, int ceiling, bool eligible) {
        ceiling = std::clamp(ceiling, 1, 32);
        maps = std::clamp(maps, 1, ceiling);
        if (!eligible || !std::isfinite(dt) || dt <= 0 || dt > 0.25f) { slowSeconds = fastSeconds = 0; return; }
        slowSeconds = dt > 1.0f / 45 ? slowSeconds + dt : 0;
        fastSeconds = dt < 1.0f / 55 ? fastSeconds + dt : 0;
        if (slowSeconds >= 2) { maps = std::max(1, maps - 1); slowSeconds = fastSeconds = 0; }
        if (fastSeconds >= 5) { maps = std::min(ceiling, maps + 1); slowSeconds = fastSeconds = 0; }
    }
};

} // namespace albion::worldview
