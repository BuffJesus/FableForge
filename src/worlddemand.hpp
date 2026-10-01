#pragma once
#include <algorithm>
#include <cmath>

namespace albion::worldview {

// Requested visible range is independent of the near/full-detail map budget.
inline constexpr float kDefaultDrawDistance = 250.0f;
inline constexpr float kSceneryBoundsPadding = 32.0f;
inline constexpr float kSceneryPreloadFactor = 1.15f;
inline constexpr float kSceneryResidentMargin = 32.0f;
inline constexpr float kResidentPriorityMaximum = 45.0f;

inline float requestedDrawDistance(float base) {
    return std::isnan(base) ? kDefaultDrawDistance : std::clamp(base, 100.0f, 1000.0f);
}

inline float sceneryLoadRadius(float drawDistance) {
    return requestedDrawDistance(drawDistance) * kSceneryPreloadFactor + kSceneryBoundsPadding;
}

// Minimum 3D distance to the entire box, not its centre or ground projection.
// Missing/invalid bounds conservatively return zero so callers do not reject
// unknown geometry. Expand map terrain bounds before calling for overhangs.
inline float boxDistance(const float eye[3], const float lo[3], const float hi[3]) {
    double squared = 0;
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(eye[axis]) || !std::isfinite(lo[axis]) ||
            !std::isfinite(hi[axis]) || lo[axis] > hi[axis]) return 0;
        const double d = std::max({double(lo[axis]) - eye[axis], 0.0, double(eye[axis]) - hi[axis]});
        squared += d * d;
    }
    return float(std::sqrt(squared));
}

inline bool sceneryInRange(float distance, float drawDistance, bool resident = false) {
    return distance <= sceneryLoadRadius(drawDistance) + (resident ? kSceneryResidentMargin : 0.0f);
}

// Soft scheduling preference, never a visibility rejection. A sphere around the
// map includes its corners; callers expand terrain bounds for overhanging meshes.
// The diagonal view cone is deliberately wider than the rectangular frustum.
inline float demandViewWeight(const float eye[3], const float direction[3],
                              float fovY, float aspect, const float lo[3], const float hi[3]) {
    float delta[3], distanceSquared = 0, radiusSquared = 0, directionSquared = 0, dot = 0;
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(eye[i]) || !std::isfinite(direction[i]) ||
            !std::isfinite(lo[i]) || !std::isfinite(hi[i]) || lo[i] > hi[i]) return 1;
        delta[i] = (lo[i] + hi[i]) * 0.5f - eye[i];
        const float extent = (hi[i] - lo[i]) * 0.5f;
        distanceSquared += delta[i] * delta[i]; radiusSquared += extent * extent;
        directionSquared += direction[i] * direction[i]; dot += delta[i] * direction[i];
    }
    if (distanceSquared <= radiusSquared || directionSquared <= 0 ||
        !std::isfinite(fovY) || !std::isfinite(aspect) || fovY <= 0 || aspect <= 0) return 1;
    const float angle = std::acos(std::clamp(dot / std::sqrt(distanceSquared * directionSquared), -1.0f, 1.0f));
    const float sphereAngle = std::asin(std::clamp(std::sqrt(radiusSquared / distanceSquared), 0.0f, 1.0f));
    const float cone = std::atan(std::tan(std::min(fovY, 3.0f) * 0.5f) * std::sqrt(1 + aspect * aspect));
    const float t = std::clamp((angle - sphereAngle - cone - 0.12f) / 0.2f, 0.0f, 1.0f);
    return 1 - t * t * (3 - 2 * t);
}

inline float demandPriority(float distance, float radius, bool resident, bool underfoot, float viewWeight) {
    if (underfoot) return -4 * radius;
    // Keep the immediate surroundings equally useful when turning in place.
    if (distance <= radius * 0.15f) viewWeight = 1;
    // Increasing draw distance must not multiply the advantage of maps that
    // happened to load first, starving newly eligible scenery.
    const float residentBonus = resident ? std::min(radius * 0.18f, kResidentPriorityMaximum) : 0.0f;
    return distance - residentBonus - radius * 0.35f * std::clamp(viewWeight, 0.0f, 1.0f);
}

} // namespace albion::worldview
