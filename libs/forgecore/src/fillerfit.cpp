#include "forge/fillerfit.hpp"

#include <algorithm>
#include <cmath>

namespace forge::fillerfit {

uint32_t gfRandom(uint32_t range, uint32_t& seed) {
    seed = seed * 0x24a1u;
    seed = seed + 0x24dfu;
    seed = (seed >> 13) | (seed << 19);   // GFROR13
    return range == 0 ? 0 : seed % range;
}

namespace {

// GFGetHermiteSplinePosition: basis weights in float32, sum ((h00 p0 + h01 p1) + h10 m0) + h11 m1
float hermite(float p0, float m0, float p1, float m1, float s) {
    const float s2 = s * s;
    const float s3 = s2 * s;
    const float h00 = float(double(s3) * 2.0 - double(s2) * 3.0 + 1.0);
    const float h01 = float(double(s3) * -2.0 + double(s2) * 3.0);
    const float h10 = float((double(s3) - double(s2) * 2.0) + double(s));
    const float h11 = s3 - s2;
    const float a = p0 * h00, b = p1 * h01, c = m0 * h10, d = m1 * h11;
    return ((a + b) + c) + d;
}

// WrapDifference: (a - b) mod n for a, b in [0, n)
float wrapDifference(int a, int b, int n) { return float(a < b ? a - b + n : a - b); }

} // namespace

float cardinalSpline(const std::vector<float>& p, float tension, float s) {
    const int n = int(p.size());
    if (n == 0) return 0.0f;
    if (n == 1) return p[0];
    if (s <= 0.0f) return p[0];
    if (s >= 1.0f) return p[size_t(n - 1)];
    const int segs = n - 1;
    const float fsegs = float(segs);
    const int from = int(std::floor(fsegs * s)) % n;
    const int to = (from + 1) % n;
    const float prev = from == 0 ? p[size_t(from)] : p[size_t(from - 1)];
    const float next = to < segs ? p[size_t(to + 1)] : p[size_t(to)];
    const float t0 = float(from) / fsegs, t1 = float(to) / fsegs;
    const float u = (s - t0) / (t1 - t0);
    const float m0 = (p[size_t(to)] - prev) * tension;
    const float m1 = (next - p[size_t(from)]) * tension;
    return hermite(p[size_t(from)], m0, p[size_t(to)], m1, u);
}

std::vector<float> edgeRing(int x0, int y0, int W, int H, const std::vector<Neighbour>& neighbours, Report* report) {
    std::vector<float> ring(size_t(2 * W + 2 * H), kNoNeighbour);
    const int x1 = x0 + W - 1, y1 = y0 + H - 1;
    // IntersectSegment over closed ranges; the vertex grid copies the shared vertices inclusively
    auto overlap = [](int a0, int a1, int b0, int b1, int& o0, int& o1) {
        if (a0 > b1 || b0 > a1) return false;
        o0 = std::max(a0, b0); o1 = std::min(a1, b1);
        return true;
    };
    for (const auto& nb : neighbours) {
        if (nb.cellsX < 2 || nb.cellsY < 2 || nb.heights.size() < size_t(nb.cellsX) * nb.cellsY) continue;
        const int nx1 = nb.x0 + nb.cellsX - 1, ny1 = nb.y0 + nb.cellsY - 1;
        auto at = [&](int x, int y) { return nb.heights[size_t(y) * nb.cellsX + x]; };
        int o0 = 0, o1 = 0;
        const bool xs = overlap(x0, x1, nb.x0, nx1, o0, o1);
        if (xs && nb.y0 == y1) {          // below: its top row is our bottom row
            for (int x = o0; x <= o1; ++x) ring[size_t(2 * W + H - 1 - (x - x0))] = at(x - nb.x0, 0);
            if (report) report->south.push_back(nb.name);
        }
        if (xs && ny1 == y0) {            // above: its bottom row is our top row
            for (int x = o0; x <= o1; ++x) ring[size_t(x - x0)] = at(x - nb.x0, nb.cellsY - 1);
            if (report) report->north.push_back(nb.name);
        }
        const bool ys = overlap(y0, y1, nb.y0, ny1, o0, o1);
        if (ys && nx1 == x0) {            // left: its right column is our left column
            for (int y = o0; y <= o1; ++y) ring[size_t(2 * W + 2 * H - 1 - (y - y0))] = at(nb.cellsX - 1, y - nb.y0);
            if (report) report->west.push_back(nb.name);
        }
        if (ys && nb.x0 == x1) {          // right: its left column is our right column
            for (int y = o0; y <= o1; ++y) ring[size_t(W + (y - y0))] = at(0, y - nb.y0);
            if (report) report->east.push_back(nb.name);
        }
    }
    if (report) report->sidesFound = int(std::count_if(ring.begin(), ring.end(), [](float v) { return v != kNoNeighbour; }));
    return ring;
}

bool fit(std::vector<float>& h, int x0, int y0, int W, int H, const std::vector<Neighbour>& neighbours, const Params& prm, Report* report) {
    if (W < 3 || H < 3 || h.size() < size_t(W) * H) return false;
    std::vector<float> ring = edgeRing(x0, y0, W, H, neighbours, report);
    const int n = int(ring.size());
    if (std::all_of(ring.begin(), ring.end(), [](float v) { return v == kNoNeighbour; })) return false;
    auto cell = [&](int x, int y) -> float& { return h[size_t(y) * W + x]; };

    // 2. the sentinel runs, each a cardinal spline over {before, mean, after}
    int filled = 0;
    {
        bool started = false, inGap = false;
        int gapStart = 0, first = 0, i = 0;
        do {
            if (ring[size_t(i)] == kNoNeighbour || started) {
                if (started) {
                    if (ring[size_t(i)] == kNoNeighbour && !inGap) { gapStart = (i == 0 ? n : i) - 1; inGap = true; }
                    if (ring[size_t(i)] != kNoNeighbour && inGap) {
                        const int gapEnd = i;
                        inGap = false;
                        int f = gapStart == n - 1 ? 0 : gapStart + 1;
                        const float a = ring[size_t(gapStart)], b = ring[size_t(gapEnd)];
                        const std::vector<float> pts = {a, float((double(a) + double(b)) / 2.0), b};
                        do {
                            const float t = float(double(wrapDifference(f, gapStart, n)) / double(wrapDifference(gapEnd, gapStart, n)));
                            ring[size_t(f)] = cardinalSpline(pts, prm.tension, t);
                            ++filled;
                            if (++f == n) f = 0;
                        } while (f != gapEnd);
                    }
                    if (i == first) break;
                }
            } else {
                started = true;
                first = i;
            }
            if (++i == n) i = 0;
        } while (started || i != 0);
    }
    if (report) report->sidesFilled = filled;

    // 3. the ring onto the border
    for (int i = 0; i < W; ++i) {
        cell(i, 0) = ring[size_t(i)];
        cell(i, H - 1) = ring[size_t(H - 1 + W * 2 - i)];
    }
    for (int j = 0; j < H; ++j) {
        cell(W - 1, j) = ring[size_t(W + j)];
        cell(0, j) = ring[size_t(H * 2 - 1 + W * 2 - j)];
    }

    uint32_t seed = 0x346780u;
    const uint32_t lo = uint32_t(std::max(0.0f, std::trunc(prm.lowNoise)));
    const uint32_t hi = uint32_t(std::max(0.0f, std::trunc(prm.highNoise)));
    auto ridge = [&](float a, float b) {
        std::vector<float> pts(5);
        pts[0] = a;
        const float s1 = prm.peakHeight * prm.step + a;
        pts[1] = float(gfRandom(lo, seed)) + s1;
        const float top = a <= b ? b : a;
        pts[2] = float(gfRandom(lo, seed)) + top + prm.peakHeight;
        const float s3 = prm.peakHeight * prm.step + b;
        pts[3] = float(gfRandom(lo, seed)) + s3;
        pts[4] = b;
        return pts;
    };
    // 4. columns
    for (int x = 1; x != W - 1; ++x) {
        const auto pts = ridge(cell(x, 0), cell(x, H - 1));
        for (int y = 1; y != H - 1; ++y) cell(x, y) = cardinalSpline(pts, prm.tension, float(y) / float(H));
    }
    // 5. rows, blended with the columns by the distance to the nearest edge
    for (int y = 1; y != H - 1; ++y) {
        const auto pts = ridge(cell(0, y), cell(W - 1, y));
        for (int x = 1; x != W - 1; ++x) {
            const float here = cell(x, y);
            const float s = cardinalSpline(pts, prm.tension, float(x) / float(W));
            const int dy = y < H - y ? y : H - y;
            const int dx = x < W - x ? x : W - x;
            const float w = float(dx) / (float(dx) + float(dy));
            const float wr = 1.0f - w;
            const float colPart = here * w;
            const float rowPart = s * wr;
            cell(x, y) = float(gfRandom(hi, seed)) + rowPart + colPart;
        }
    }
    // 6. the in-place 3x3 mean
    static const int off[9][2] = {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {0, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};
    for (int x = 1; x != W - 1; ++x)
        for (int y = 1; y != H - 1; ++y) {
            float sum = 0.0f;
            for (const auto& o : off) sum = cell(x + o[0], y + o[1]) + sum;
            cell(x, y) = float(double(sum) / 9.0);
        }
    return true;
}

} // namespace forge::fillerfit
