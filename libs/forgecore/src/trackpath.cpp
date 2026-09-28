#include "forge/trackpath.hpp"

#include <cmath>

namespace forge::trackpath {
namespace {
float segment(const Point& a, const Point& b) {
    const float dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}
} // namespace

float length(const std::vector<Point>& nodes) {
    float total = 0.0f;
    for (size_t i = 1; i < nodes.size(); ++i) total += segment(nodes[i - 1], nodes[i]);
    return total;
}

Point pointAtDistance(const std::vector<Point>& nodes, float d) {
    if (nodes.empty()) return {0, 0, 0};
    float acc = 0.0f;
    for (size_t i = 1; i < nodes.size(); ++i) {
        const float seg = segment(nodes[i - 1], nodes[i]);
        if (d < acc + seg) {
            const float f = (d - acc) / seg;
            const Point& a = nodes[i - 1];
            const Point& b = nodes[i];
            return {a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f};
        }
        acc += seg;
    }
    return nodes.back();
}

} // namespace forge::trackpath
