#include "navlines.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <stdexcept>

#include "forge/lzo.hpp"

namespace albion::navlines {
namespace {

uint32_t rdU32(const std::vector<uint8_t>& d, size_t at) {
    if (at + 4 > d.size()) throw std::runtime_error("hull: read past end");
    uint32_t v; std::memcpy(&v, d.data() + at, 4); return v;
}
float rdF32(const std::vector<uint8_t>& d, size_t at) {
    if (at + 4 > d.size()) throw std::runtime_error("hull: read past end");
    float v; std::memcpy(&v, d.data() + at, 4); return v;
}

// GFGetSign
int sign(float v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }

} // namespace

HullMesh decodeHull(const std::vector<uint8_t>& d, const HullOptions& options) {
    if (d.size() < 13 || std::memcmp(d.data(), ">>>>3DMF", 8) != 0)
        throw std::runtime_error("hull: invalid 3DMF header");
    HullMesh out;
    size_t start = 12;
    while (start < d.size() && d[start]) ++start;
    if (start == d.size()) throw std::runtime_error("hull: unterminated name");
    ++start;
    while (start % 4) ++start;
    struct Sub { std::array<float, 12> trfm{}; bool hasTrfm = false; };
    std::function<void(size_t, size_t, Sub*)> walk = [&](size_t at, size_t end, Sub* sub) {
        while (at + 8 <= end) {
            const std::string tag(reinterpret_cast<const char*>(d.data() + at), 4);
            const uint32_t n = rdU32(d, at + 4);
            const size_t body = at + 8;
            if (!std::all_of(tag.begin(), tag.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)); }) || n > end - body)
                throw std::runtime_error("hull: invalid chunk");
            if (tag == "3DRT" || tag == "MTLS" || tag == "HLPR") {
                walk(body, body + n, sub);
            } else if (tag == "SUBM") {
                size_t q = body;
                while (q < body + n && d[q]) ++q;
                if (body + n - q < 17) throw std::runtime_error("hull: truncated SUBM header");
                q += 1 + 16;   // name, sub-mesh index, parent, first child, next sibling
                Sub s;
                walk(q, body + n, &s);
            } else if (tag == "TRFM" && sub) {
                if (n < 48) throw std::runtime_error("hull: truncated TRFM");
                for (int i = 0; i < 12; ++i) sub->trfm[size_t(i)] = rdF32(d, body + size_t(i) * 4);
                sub->hasTrfm = true;
            } else if (tag == "PRIM") {
                if (n < 4) throw std::runtime_error("hull: truncated PRIM");
                // TRIS indices address this PRIM's own VERT list
                const size_t base = out.verts.size();
                std::vector<std::array<uint32_t, 3>> tris;
                size_t at2 = body + 4, end2 = body + n;
                while (at2 + 8 <= end2) {
                    const std::string t2(reinterpret_cast<const char*>(d.data() + at2), 4);
                    const uint32_t n2 = rdU32(d, at2 + 4);
                    const size_t b2 = at2 + 8;
                    if (n2 > end2 - b2) throw std::runtime_error("hull: truncated primitive chunk");
                    if (t2 == "TRIS") {
                        if (n2 < 4) throw std::runtime_error("hull: truncated TRIS");
                        const uint32_t c = rdU32(d, b2);
                        if (c > (n2 - 4) / 6) throw std::runtime_error("hull: triangle count exceeds TRIS");
                        for (uint32_t i = 0; i < c; ++i) {
                            uint16_t ix[3]; std::memcpy(ix, d.data() + b2 + 4 + size_t(i) * 6, 6);
                            tris.push_back({uint32_t(ix[0]), uint32_t(ix[1]), uint32_t(ix[2])});
                        }
                    } else if (t2 == "VERT") {
                        if (n2 < 4) throw std::runtime_error("hull: truncated VERT");
                        const uint32_t c = rdU32(d, b2);
                        if (c > (n2 - 4) / 32) throw std::runtime_error("hull: vertex count exceeds VERT");
                        for (uint32_t i = 0; i < c; ++i) {
                            const size_t o = b2 + 4 + size_t(i) * 32;
                            std::array<float, 3> v{rdF32(d, o), rdF32(d, o + 4), rdF32(d, o + 8)};
                            if (options.applySubMeshTransform && sub && sub->hasTrfm) {
                                const auto& m = sub->trfm;
                                v = {v[0] * m[0] + v[1] * m[3] + v[2] * m[6] + m[9],
                                     v[0] * m[1] + v[1] * m[4] + v[2] * m[7] + m[10],
                                     v[0] * m[2] + v[1] * m[5] + v[2] * m[8] + m[11]};
                            }
                            out.verts.push_back(v);
                        }
                    }
                    at2 = b2 + n2;
                }
                for (auto t : tris) {
                    for (auto& i : t) i += uint32_t(base);
                    if (t[0] < out.verts.size() && t[1] < out.verts.size() && t[2] < out.verts.size()) out.tris.push_back(t);
                }
            } else if (tag == "HPNT") {
                if (n < 21) throw std::runtime_error("hull: truncated HPNT");
                // f32 x, y, z; i32 two indices (-1, -1 seen); ASCIIZ name
                std::array<float, 3> p{rdF32(d, body), rdF32(d, body + 4), rdF32(d, body + 8)};
                size_t q = body + 20;
                std::string name;
                while (q < body + n && d[q]) name.push_back(char(d[q++]));
                if (q == body + n) throw std::runtime_error("hull: unterminated helper name");
                out.helpers.push_back({name, p});
            }
            at = body + n;
        }
    };
    walk(start, d.size(), nullptr);
    return out;
}

std::vector<Line> lineListFromBaseline(const HullMesh& mesh, float z) {
    std::vector<Line> lines;
    // the baseline: the triangle centroid height closest to z
    float bestDiff = 1e10f, best = 0.0f;
    for (const auto& t : mesh.tris) {
        const float c = (mesh.verts[t[0]][2] + mesh.verts[t[1]][2] + mesh.verts[t[2]][2]) * 0.3333333f;
        const float diff = std::fabs(c - z);
        if (diff < bestDiff) { best = c; bestDiff = diff; }
    }
    if (bestDiff == 1e10f) return lines;
    for (const auto& t : mesh.tris) {
        const auto& a = mesh.verts[t[0]];
        const auto& b = mesh.verts[t[1]];
        const auto& c = mesh.verts[t[2]];
        const int sa = sign(a[2] - best), sb = sign(b[2] - best), sc = sign(c[2] - best);
        // the engine's six mixed-sign cases: no vertex on the plane, not all on one side
        if (sa == 0 || sb == 0 || sc == 0 || (sa == sb && sb == sc)) continue;
        std::array<float, 2> pts[2];
        int k = 0;
        auto edge = [&](const std::array<float, 3>& p, const std::array<float, 3>& q, int sp, int sq) {
            if (sp == sq || k >= 2) return;
            const float f = (best - p[2]) / (q[2] - p[2]);
            pts[k++] = {p[0] + (q[0] - p[0]) * f, p[1] + (q[1] - p[1]) * f};
        };
        edge(a, b, sa, sb); edge(b, c, sb, sc); edge(c, a, sc, sa);
        if (k == 2) lines.push_back({pts[0][0], pts[0][1], pts[1][0], pts[1][1]});
    }
    return lines;
}

std::vector<std::vector<Line>> navigationLines(const HullMesh& mesh) {
    // CalculateNumberOfNavigationLineLevels: the highest i in 0..8 with a NAV_LAYER_0<i+1> helper
    auto find = [&](int i) -> const std::array<float, 3>* {
        const std::string want = "NAV_LAYER_0" + std::to_string(i + 1);
        for (const auto& h : mesh.helpers) if (h.first == want) return &h.second;
        return nullptr;
    };
    int count = 0;
    for (int i = 0; i < 9; ++i) if (find(i)) count = i + 1;
    std::vector<std::vector<Line>> levels(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
        if (const auto* p = find(i)) levels[size_t(i)] = lineListFromBaseline(mesh, (*p)[2]);
    return levels;
}

Hull buildHull(const HullMesh& mesh) {
    Hull hull;
    hull.levels = navigationLines(mesh);
    if (mesh.verts.empty()) return hull;
    std::array<float, 3> lo = mesh.verts[0], hi = mesh.verts[0];
    for (const auto& v : mesh.verts) for (size_t k = 0; k < 3; ++k) {
        lo[k] = std::min(lo[k], v[k]);
        hi[k] = std::max(hi[k], v[k]);
    }
    for (size_t k = 0; k < 3; ++k) hull.centre[k] = 0.5f * (lo[k] + hi[k]);
    const float dx = 0.5f * (hi[0] - lo[0]), dy = 0.5f * (hi[1] - lo[1]), dz = 0.5f * (hi[2] - lo[2]);
    hull.radius = std::sqrt(dx * dx + dy * dy + dz * dz);
    return hull;
}

bool requestsHigherDetail(const Box& node, const std::vector<Box>& detailedAreas) {
    for (const auto& b : detailedAreas) {
        // C2DBoxF::ContainsPoint: lower bounds inclusive, upper bounds exclusive.
        const bool x = (node.x0 >= b.x0 && node.x0 < b.x1) || (node.x1 >= b.x0 && node.x1 < b.x1);
        const bool y = (node.y0 >= b.y0 && node.y0 < b.y1) || (node.y1 >= b.y0 && node.y1 < b.y1);
        if (x && y) return true;
    }
    return false;
}

HullCache::HullCache(const forge::big::File& graphics, HullOptions options) : big_(graphics), options_(options) {
    if (const auto* bank = big_.findBank("MBANK_ALLMESHES"))
        for (const auto& e : bank->entries) byId_[e.id] = &e;
}

const Hull* HullCache::forRenderMesh(uint32_t meshId) {
    const auto r = byId_.find(meshId);
    if (r == byId_.end() || r->second->subHeader.size() < 4) return nullptr;
    uint32_t physics = 0;
    std::memcpy(&physics, r->second->subHeader.data(), 4);
    return physics ? forPhysicsMesh(physics) : nullptr;
}

const Hull* HullCache::forPhysicsMesh(uint32_t physicsId) {
    const auto it = cache_.find(physicsId);
    if (it != cache_.end()) return it->second.get();
    std::unique_ptr<Hull> hull;
    {
        const auto p = byId_.find(physicsId);
        if (p != byId_.end() && p->second->type == 3) {
            const auto raw = big_.entryData(*p->second);
            if (raw.size() >= 4) {
                uint32_t usize = 0; std::memcpy(&usize, raw.data(), 4);
                std::vector<uint8_t> plain;
                try { plain = forge::lzo::decompress(raw.data() + 4, raw.size() - 4, usize); }
                catch (const std::exception&) { plain.assign(raw.begin() + 4, raw.end()); }
                try {
                    const auto mesh = decodeHull(plain, options_);
                    hull = std::make_unique<Hull>(buildHull(mesh));
                } catch (const std::exception&) { hull.reset(); }
            }
        }
    }
    const Hull* out = hull.get();
    cache_[physicsId] = std::move(hull);
    return out;
}

Line toWorld(const Line& l, const foliageexport::Instance& in) {
    // world = pos + lx*m[0..2] + ly*m[3..5] + lz*m[6..8] (thingsexport::thingBasis; m carries 0.01 * scale)
    auto tx = [&](float x, float y) { return std::array<float, 2>{in.x + x * in.m[0] + y * in.m[3], in.y + x * in.m[1] + y * in.m[4]}; };
    const auto a = tx(l.x0, l.y0), b = tx(l.x1, l.y1);
    return {a[0], a[1], b[0], b[1]};
}

Box detailBox(const Hull& hull, const foliageexport::Instance& in, int w, int h) {
    const float cx = in.x + hull.centre[0] * in.m[0] + hull.centre[1] * in.m[3] + hull.centre[2] * in.m[6];
    const float cy = in.y + hull.centre[0] * in.m[1] + hull.centre[1] * in.m[4] + hull.centre[2] * in.m[7];
    const float scale = std::sqrt(in.m[0] * in.m[0] + in.m[1] * in.m[1] + in.m[2] * in.m[2]);
    const float r = hull.radius * scale;
    return {std::max(0.0f, cx - r), std::max(0.0f, cy - r), std::min(float(w), cx + r), std::min(float(h), cy + r)};
}

std::vector<uint8_t> rasterise(const std::vector<Line>& lines, int width, int height) {
    std::vector<uint8_t> out(size_t(width) * size_t(height), 0);
    // Liang-Barsky clip of each segment against each candidate cell's box (touching counts)
    auto hits = [](const Line& l, float bx0, float by0, float bx1, float by1) {
        float t0 = 0, t1 = 1;
        const float dx = l.x1 - l.x0, dy = l.y1 - l.y0;
        const float p[4] = {-dx, dx, -dy, dy};
        const float q[4] = {l.x0 - bx0, bx1 - l.x0, l.y0 - by0, by1 - l.y0};
        for (int i = 0; i < 4; ++i) {
            if (p[i] == 0) { if (q[i] < 0) return false; continue; }
            const float r = q[i] / p[i];
            if (p[i] < 0) t0 = std::max(t0, r); else t1 = std::min(t1, r);
            if (t0 > t1) return false;
        }
        return true;
    };
    for (const auto& l : lines) {
        const int x0 = std::max(0, int(std::floor(std::min(l.x0, l.x1)))), x1 = std::min(width - 1, int(std::floor(std::max(l.x0, l.x1))));
        const int y0 = std::max(0, int(std::floor(std::min(l.y0, l.y1)))), y1 = std::min(height - 1, int(std::floor(std::max(l.y0, l.y1))));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                if (hits(l, float(x), float(y), float(x + 1), float(y + 1))) out[size_t(y) * size_t(width) + size_t(x)] = 1;
    }
    return out;
}

} // namespace albion::navlines
