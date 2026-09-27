#pragma once

#include "forge/navmesh.hpp"
#include <algorithm>
#include <cmath>

// Internal geometry kernel, also exercised against the debug executable by
// tools/verify_debug_nav.py. No engine binary or native code is distributed.
namespace forge::navmesh::detail {

// C2DLineF::IntersectsWith (0x0324c6d0). Keep the native float
// roundings: almost-parallel lines can differ from an exact determinant test.
struct Vec { float x, y; };
inline float dot(Vec a, Vec b) { return float(double(a.x) * b.x + double(a.y) * b.y); }
inline Vec direction(const Line& l) {
    const Vec d{l.x1 - l.x0, l.y1 - l.y0};
    const float xx = float(double(d.x) * d.x), yy = float(double(d.y) * d.y);
    const float length = float(std::sqrt(double(float(xx + yy))));
    const float inverse = 1.0f / length;
    return {d.x * inverse, d.y * inverse};
}
inline bool intersects(const Line& a, const Line& b) {
    constexpr float epsilon = 0.0001f;
    if (double(std::min(a.x0, a.x1)) - std::max(b.x0, b.x1) > epsilon ||
        double(std::min(b.x0, b.x1)) - std::max(a.x0, a.x1) > epsilon ||
        double(std::min(a.y0, a.y1)) - std::max(b.y0, b.y1) > epsilon ||
        double(std::min(b.y0, b.y1)) - std::max(a.y0, a.y1) > epsilon) return false;
    if ((std::abs(a.x1 - a.x0) < epsilon && std::abs(a.y1 - a.y0) < epsilon) ||
        (std::abs(b.x1 - b.x0) < epsilon && std::abs(b.y1 - b.y0) < epsilon)) return false;
    const Vec u = direction(a), v = direction(b), w{a.x0 - b.x0, a.y0 - b.y0};
    const float c = dot(u, v), denominator = float(double(c) * c - 1.0);
    if (denominator == 0) return false;
    const float t = float((double(dot(w, u)) - double(dot(w, v)) * c) / denominator);
    const Vec delta{u.x * t, u.y * t}, p{a.x0 + delta.x, a.y0 + delta.y};
    auto within = [&](const Line& l) {
        return double(p.x) - std::min(l.x0, l.x1) > -epsilon &&
               double(std::max(l.x0, l.x1)) - p.x > -epsilon &&
               double(p.y) - std::min(l.y0, l.y1) > -epsilon &&
               double(std::max(l.y0, l.y1)) - p.y > -epsilon;
    };
    return within(a) && within(b);
}

inline bool lineBlocksArea(const Line& l, const DetailArea& b) {
    // IsAreaBlockedByLines (0x0328e000): broad phase, half-open endpoint
    // containment, then four non-parallel edge tests (box wrapper 0x017fe9a5).
    constexpr float epsilon = 0.0001f;
    if (double(std::max(l.x0, l.x1)) - b.x0 <= -epsilon ||
        double(std::min(l.x0, l.x1)) - b.x1 > epsilon ||
        double(std::max(l.y0, l.y1)) - b.y0 <= -epsilon ||
        double(std::min(l.y0, l.y1)) - b.y1 > epsilon) return false;
    auto inside = [&](float x, float y) { return x >= b.x0 && x < b.x1 && y >= b.y0 && y < b.y1; };
    if (inside(l.x0, l.y0) || inside(l.x1, l.y1)) return true;
    return intersects(l, {b.x0, b.y0, b.x1, b.y0}) ||
           intersects(l, {b.x1, b.y0, b.x1, b.y1}) ||
           intersects(l, {b.x0, b.y1, b.x1, b.y1}) ||
           intersects(l, {b.x0, b.y0, b.x0, b.y1});
}

} // namespace forge::navmesh::detail
