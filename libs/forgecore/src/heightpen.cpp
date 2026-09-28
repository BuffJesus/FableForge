#include "forge/heightpen.hpp"

#include <algorithm>
#include <cmath>

namespace forge::heightpen {
namespace {

constexpr float kDnz = 1.0e-4f;   // DNZ_FOR_HEIGHTS

// GFFloatToLongNear: fistp in the default mode, round half to even
int nearest(float v) { return int(std::nearbyint(v)); }

float limit(float v) {   // GFLimit(v, 0, 2048 - DNZ): NaN passes through
    if (0.0f > v) return 0.0f;
    if (kMaxHeight < v) return kMaxHeight;
    return v;
}

bool inside(const terrain::Heightfield& f, int x, int y) { return x >= 0 && y >= 0 && x < f.cellsX() && y < f.cellsY(); }

} // namespace

float sizeToRadius(float slider) { return std::pow(2.0f, slider) / 1.0f; }

float speedToOpacity(float speed) {
    return 1.0f - float(std::cos(double(std::clamp(speed, 0.0f, 1.0f) * 0.25f) * 6.2831854820251465));
}

uint32_t lcgStep(uint32_t& seed) {
    seed = seed * 0x24A1u + 0x24DFu;
    seed = (seed >> 13) | (seed << 19);   // ror 13
    return seed;
}

uint32_t random(uint32_t max, uint32_t& seed) {
    lcgStep(seed);   // advances even when max is 0
    return max ? seed % max : 0u;
}

float floatRandom(float max, uint32_t& seed) {
    const double d = double(max) * 65536.0;
    lcgStep(seed);
    if (0.0001 < d) return float(std::fmod(double(seed), d) / 65536.0);
    return 0.0f;
}

size_t changeHeight(terrain::Heightfield& field, float x, float y, float radius, float delta, std::vector<uint8_t>& altered) {
    const size_t cells = size_t(field.cellsX()) * size_t(field.cellsY());
    if (altered.size() != cells) altered.assign(cells, 0);
    const int cx = nearest(x), cy = nearest(y);
    const int x0 = int(std::floor(float(cx) - radius)), x1 = int(std::ceil(float(cx) + radius));
    const int y0 = int(std::floor(float(cy) - radius)), y1 = int(std::ceil(float(cy) + radius));
    size_t changed = 0;
    for (int by = y0; by <= y1; ++by)
        for (int bx = x0; bx <= x1; ++bx) {
            if (!inside(field, bx, by)) continue;
            const double dx = double(bx - cx), dy = double(by - cy);
            const float d = float(std::sqrt(float(dx * dx + dy * dy)));
            if (!(radius > d - 0.5f)) continue;
            uint8_t& seen = altered[size_t(by) * field.cellsX() + bx];
            if (seen) continue;
            float& h = field.at(bx, by);
            const float next = limit(h + delta);
            seen = 1;
            if (next != h) { h = next; ++changed; }
        }
    return changed;
}

size_t paintHeight(terrain::Heightfield& field, float x, float y, float radius, float target, float opacity) {
    const int cx = nearest(x), cy = nearest(y);
    const int x0 = int(std::floor(float(cx) - radius)), x1 = int(std::ceil(float(cx) + radius));
    const int y0 = int(std::floor(float(cy) - radius)), y1 = int(std::ceil(float(cy) + radius));
    size_t changed = 0;
    for (int by = y0; by <= y1; ++by)
        for (int bx = x0; bx <= x1; ++bx) {
            const double dx = double(bx - cx), dy = double(by - cy);
            const float d = std::sqrt(float(dx * dx + dy * dy));
            if (!(radius > d - 0.5f)) continue;
            if (!inside(field, bx, by)) continue;
            float& h = field.at(bx, by);
            float n = (target - h) * opacity + h;
            n = target > h ? std::min(n, target) : std::max(n, target);   // never past the target
            n = limit(n);
            if (n != h) { h = n; ++changed; }
        }
    return changed;
}

size_t smear(terrain::Heightfield& field, float x, float y, float radius, float smoothness, float spikyness) {
    static const int around[8][2] = {{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}};   // MapAround8
    const int r = int(radius > 1.0f ? radius : 1.0f);
    const int cx = nearest(x), cy = nearest(y);
    const int side = 2 * r;
    const float t2 = spikyness * spikyness;
    auto filtered = [&](int bx, int by) {
        const float h0 = field.at(bx, by);
        float mn = 1e30f, mx = -1e30f, sum = 0.0f;
        int n = 0;
        for (const auto& o : around) {
            const int nx = bx + o[0], ny = by + o[1];
            if (!inside(field, nx, ny)) continue;
            const float h = field.at(nx, ny);
            ++n;
            if (h < mn) mn = h;
            if (h > mx) mx = h;
            sum += h;
        }
        const float avg = sum / float(n);
        const float ratio = std::fabs(h0 - avg) / (mx - mn);
        if (ratio > t2) {   // NaN (a flat neighbourhood equal to the block) stays
            const float t = avg * smoothness;
            return (1.0f - smoothness) * h0 + t;
        }
        return h0;
    };
    // pass 1 reads the unmodified heights (vanilla fills a buffer first), pass 2 writes
    std::vector<float> buf(size_t(side) * side, 0.0f);
    std::vector<uint8_t> in(size_t(side) * side, 0);
    for (int j = 0; j < side; ++j)
        for (int i = 0; i < side; ++i) {
            const int bx = cx - r + i, by = cy - r + j;
            if (!inside(field, bx, by)) continue;
            if (!(std::sqrt(float((r - i) * (r - i) + (r - j) * (r - j))) < float(r))) continue;
            buf[size_t(j) * side + i] = filtered(bx, by);
            in[size_t(j) * side + i] = 1;
        }
    size_t changed = 0;
    for (int j = 0; j < side; ++j)
        for (int i = 0; i < side; ++i) {
            if (!in[size_t(j) * side + i]) continue;
            const int bx = cx - r + i, by = cy - r + j;
            const float v = limit(buf[size_t(j) * side + i]);
            float& h = field.at(bx, by);
            if (std::fabs(v - h) > kDnz) { h = v; ++changed; }
        }
    return changed;
}

size_t noise(terrain::Heightfield& field, float x, float y, float radius, float magnifier, uint32_t& seed) {
    const int cx = nearest(x), cy = nearest(y);
    const float r2 = radius * radius;
    float sum = 0.0f;
    uint32_t count = 0;
    for (int bx = int(float(cx) - radius); float(bx) <= float(cx) + radius; ++bx)
        for (int by = int(float(cy) - radius); float(by) <= float(cy) + radius; ++by) {
            const float fx = float(bx - cx), fy = float(by - cy);
            if (fx * fx + fy * fy <= r2 && inside(field, bx, by)) { sum += field.at(bx, by); ++count; }
        }
    if (count == 0 || !(double(sum) / double(count) > 0.01)) return 0;   // flat, sea-level ground: nothing
    uint32_t n = random(count + 1, seed) + 1;
    if (n & 1u) ++n;
    float amp = floatRandom(float(double(magnifier) * 0.05), seed) + 0.01f;
    const float rr = radius;
    const uint32_t R = uint32_t(std::max(0.0f, std::ceil(rr)));   // GFFloatToLongCeil
    size_t changed = 0;
    for (uint32_t k = 0; k < n; ++k) {
        int dx = int(random(R, seed)), dy = int(random(R, seed));
        if (random(2, seed) == 0) dx = -dx;
        if (random(2, seed) == 0) dy = -dy;
        const int px = cx + dx, py = cy + dy;
        if (!inside(field, px, py)) continue;
        float& h = field.at(px, py);
        float nh = h + amp;
        if (nh < 0.0f) { amp = -nh; nh = 0.0f; }
        if (nh != h) ++changed;
        h = nh;
        amp = -amp;   // the sign flips only after a poke that landed
    }
    return changed;
}

} // namespace forge::heightpen
