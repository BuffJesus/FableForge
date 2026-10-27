#include "forge/characterfit.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <unordered_map>

namespace forge::characterfit {
namespace {
[[noreturn]] void fail(const char* reason) { throw std::runtime_error(std::string("character fit: ") + reason); }

struct Point { float x, y, z; uint32_t part, vertex; };

// Uniform hash grid over the donor vertices with ring-expanding k-nearest search.
class Grid {
public:
    Grid(std::vector<Point> points, float cell) : points_(std::move(points)), cell_(cell) {
        for (uint32_t i = 0; i < points_.size(); ++i) {
            cells_[key(cellOf(points_[i].x), cellOf(points_[i].y), cellOf(points_[i].z))].push_back(i);
            if (points_[i].part >= partSizes_.size()) partSizes_.resize(points_[i].part + 1);
            ++partSizes_[points_[i].part];
        }
    }
    // Up to k nearest points, optionally restricted to one part; (distance, index) ascending.
    std::vector<std::pair<float, uint32_t>> nearest(float x, float y, float z, size_t k, int part = -1) const {
        std::vector<std::pair<float, uint32_t>> best;
        k = std::min(k, part < 0 ? points_.size() : (size_t(part) < partSizes_.size() ? partSizes_[size_t(part)] : 0));
        if (!k) return best;
        const long cx = cellOf(x), cy = cellOf(y), cz = cellOf(z);
        // The grid spans 48 cells across the donor; 256 rings reach well past it.
        for (long ring = 0; ring <= 256; ++ring) {
            for (long dx = -ring; dx <= ring; ++dx) for (long dy = -ring; dy <= ring; ++dy) for (long dz = -ring; dz <= ring; ++dz) {
                if (std::max({std::labs(dx), std::labs(dy), std::labs(dz)}) != ring) continue;
                const auto found = cells_.find(key(cx + dx, cy + dy, cz + dz));
                if (found == cells_.end()) continue;
                for (const uint32_t i : found->second) {
                    const auto& p = points_[i];
                    if (part >= 0 && p.part != uint32_t(part)) continue;
                    const float d = std::sqrt((p.x - x) * (p.x - x) + (p.y - y) * (p.y - y) + (p.z - z) * (p.z - z));
                    best.push_back({d, i});
                }
            }
            std::sort(best.begin(), best.end());
            if (best.size() > k) best.resize(k);
            // Points outside the cells searched so far are at least ring * cell away.
            if (best.size() == k && best.back().first <= float(ring) * cell_) break;
        }
        return best;
    }
    const Point& at(uint32_t i) const { return points_[i]; }
private:
    long cellOf(float v) const { return long(std::floor(v / cell_)); }
    static uint64_t key(long x, long y, long z) {
        return (uint64_t(uint32_t(x) & 0x1fffff) << 42) | (uint64_t(uint32_t(y) & 0x1fffff) << 21) | uint64_t(uint32_t(z) & 0x1fffff);
    }
    std::vector<Point> points_;
    std::vector<size_t> partSizes_;
    float cell_;
    std::unordered_map<uint64_t, std::vector<uint32_t>> cells_;
};

bool finite3(float x, float y, float z) { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }
} // namespace

Fit fit(std::span<const meshcompose::Primitive> input, std::span<const meshpreview::Geometry> donor, const Options& options) {
    if (donor.empty()) fail("no donor parts");
    if (options.neighbours == 0 || options.neighbours > 64) fail("neighbours must be 1..64");
    std::vector<Point> points;
    std::array<float, 3> dmin{1e30f, 1e30f, 1e30f}, dmax{-1e30f, -1e30f, -1e30f};
    for (uint32_t p = 0; p < donor.size(); ++p) {
        if (donor[p].bones.empty()) fail("donor part has no skeleton");
        for (uint32_t v = 0; v < donor[p].vertices.size(); ++v) {
            const auto& d = donor[p].vertices[v];
            if (!d.skinned) fail("donor vertex is not skinned");
            if (!finite3(d.x, d.y, d.z)) fail("non-finite donor vertex");
            points.push_back({d.x, d.y, d.z, p, v});
            dmin = {std::min(dmin[0], d.x), std::min(dmin[1], d.y), std::min(dmin[2], d.z)};
            dmax = {std::max(dmax[0], d.x), std::max(dmax[1], d.y), std::max(dmax[2], d.z)};
        }
    }
    if (points.empty()) fail("donor has no vertices");
    std::array<float, 3> imin{1e30f, 1e30f, 1e30f}, imax{-1e30f, -1e30f, -1e30f};
    size_t triangles = 0;
    for (const auto& prim : input) {
        triangles += prim.faces.size();
        for (const auto& v : prim.verts) {
            if (!finite3(v.x, v.y, v.z)) fail("non-finite input vertex");
            imin = {std::min(imin[0], v.x), std::min(imin[1], v.y), std::min(imin[2], v.z)};
            imax = {std::max(imax[0], v.x), std::max(imax[1], v.y), std::max(imax[2], v.z)};
        }
    }
    if (!triangles) fail("input has no triangles");

    Fit result;
    if (options.align) {
        const float inputHeight = imax[2] - imin[2], donorHeight = dmax[2] - dmin[2];
        if (!(inputHeight > 0) || !(donorHeight > 0)) fail("input or donor has no height");
        result.scale = donorHeight / inputHeight;
        // Feet on the donor's feet, centres matched in X and Y.
        result.offset = {(dmin[0] + dmax[0]) / 2 - result.scale * (imin[0] + imax[0]) / 2,
                         (dmin[1] + dmax[1]) / 2 - result.scale * (imin[1] + imax[1]) / 2,
                         dmin[2] - result.scale * imin[2]};
    }
    const float extent = std::max({dmax[0] - dmin[0], dmax[1] - dmin[1], dmax[2] - dmin[2], 1e-3f});
    const Grid grid(std::move(points), extent / 48);
    const auto place = [&](const meshcompose::Vec3& v) {
        return std::array<float, 3>{v.x * result.scale + result.offset[0], v.y * result.scale + result.offset[1],
                                    v.z * result.scale + result.offset[2]};
    };

    result.parts.resize(donor.size());
    for (size_t p = 0; p < donor.size(); ++p) {
        auto& g = result.parts[p].geometry;
        g.bones = donor[p].bones;
        g.boneCount = uint32_t(g.bones.size());
        g.skeletonBlocks = donor[p].skeletonBlocks;
        g.materials.resize(input.size());
        for (size_t m = 0; m < input.size(); ++m) g.materials[m].id = int32_t(m);
    }
    // Per (part, input primitive, input vertex) -> output vertex; seams duplicate vertices.
    std::vector<std::map<std::pair<size_t, uint32_t>, uint32_t>> remap(donor.size());
    for (size_t pi = 0; pi < input.size(); ++pi) {
        const auto& prim = input[pi];
        for (const auto& face : prim.faces) {
            for (const auto corner : face) if (corner >= prim.verts.size()) fail("input face references a missing vertex");
            std::array<float, 3> c{};
            for (const auto corner : face) {
                const auto at = place(prim.verts[corner]);
                for (size_t k = 0; k < 3; ++k) c[k] += at[k] / 3;
            }
            // Owner = the part whose surface lies closest to the whole triangle: summed
            // nearest-vertex distance of its corners and centroid. Layers overlap (a
            // shirt over the waist of the trousers), so the centroid alone misassigns.
            uint32_t part = 0;
            float bestCost = std::numeric_limits<float>::infinity();
            for (uint32_t candidate = 0; candidate < donor.size(); ++candidate) {
                float cost = 0;
                const auto measure = [&](const std::array<float, 3>& at) {
                    const auto near = grid.nearest(at[0], at[1], at[2], 1, int(candidate));
                    cost += near.empty() ? std::numeric_limits<float>::infinity() : near.front().first;
                };
                measure(c);
                for (const auto corner : face) measure(place(prim.verts[corner]));
                if (cost < bestCost) { bestCost = cost; part = candidate; }
            }
            if (!std::isfinite(bestCost)) fail("no donor vertex near a triangle");
            auto& out = result.parts[part];
            std::array<uint32_t, 3> local{};
            for (size_t k = 0; k < 3; ++k) {
                const uint32_t corner = face[k];
                auto [it, inserted] = remap[part].try_emplace({pi, corner}, uint32_t(out.geometry.vertices.size()));
                if (inserted) {
                    const auto at = place(prim.verts[corner]);
                    meshpreview::Vertex v;
                    v.x = at[0]; v.y = at[1]; v.z = at[2];
                    if (corner < prim.normals.size()) { v.nx = prim.normals[corner].x; v.ny = prim.normals[corner].y; v.nz = prim.normals[corner].z; }
                    if (corner < prim.uvs.size()) { v.u = prim.uvs[corner].u; v.v = prim.uvs[corner].v; }
                    // Inverse-distance blend of the part's nearest donor influences;
                    // an exact hit copies that vertex's influences unchanged.
                    const auto near = grid.nearest(at[0], at[1], at[2], options.neighbours, int(part));
                    if (near.empty()) fail("donor part has no vertices");
                    std::map<uint16_t, double> influence;
                    for (const auto& [distance, index] : near) {
                        const double w = distance <= 1e-6f ? 1e12 : 1.0 / distance;
                        const auto& d = donor[part].vertices[grid.at(index).vertex];
                        for (size_t j = 0; j < 4; ++j) if (d.weights[j] > 0) influence[d.joints[j]] += w * d.weights[j];
                        if (distance <= 1e-6f) break;
                    }
                    std::vector<std::pair<double, uint16_t>> ranked;
                    for (const auto& [joint, weight] : influence) ranked.push_back({weight, joint});
                    std::sort(ranked.rbegin(), ranked.rend());
                    if (ranked.size() > 4) ranked.resize(4);
                    double total = 0;
                    for (const auto& r : ranked) total += r.first;
                    for (size_t j = 0; j < ranked.size(); ++j) { v.joints[j] = ranked[j].second; v.weights[j] = float(ranked[j].first / total); }
                    v.skinned = true;
                    out.geometry.vertices.push_back(v);
                }
                local[k] = it->second;
            }
            out.geometry.triangles.push_back({local[0], local[1], local[2], int32_t(pi)});
            ++out.sourceTriangles;
        }
    }
    return result;
}

} // namespace forge::characterfit
