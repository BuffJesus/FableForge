#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace albion::terrainlod {
struct Level { uint32_t first = 0, count = 0; float error = 0; };
struct Patch {
    float lo[3] = {}, hi[3] = {};
    std::vector<Level> levels;
};
struct Result {
    std::vector<uint32_t> indices;
    std::vector<Patch> patches;
};

// Preview-only index LODs for complete, unit-spaced Y-up terrain grids. Vertices
// stay untouched. Every patch edge retains all its original vertices at every
// LOD, so neighbours can choose independently without adding T-junctions.
// Vertex requires px,py,pz and walk (the overview-morph height).
template<class Vertex>
Result build(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& source,
             int patchCells = 32) {
    Result result;
    if (vertices.empty() || source.empty() || patchCells < 4 || patchCells > 128) return result;
    float minX = vertices[0].px, maxX = minX, minZ = vertices[0].pz, maxZ = minZ;
    for (const auto& v : vertices) {
        if (!std::isfinite(v.px) || !std::isfinite(v.py) || !std::isfinite(v.pz) || !std::isfinite(v.walk)) return {};
        minX = std::min(minX, v.px); maxX = std::max(maxX, v.px);
        minZ = std::min(minZ, v.pz); maxZ = std::max(maxZ, v.pz);
    }
    if (maxX-minX > 16384 || maxZ-minZ > 16384) return {};
    const int width = int(std::lround(maxX-minX))+1, height = int(std::lround(maxZ-minZ))+1;
    if (width < 2 || height < 2 || size_t(width)*height != vertices.size() ||
        size_t(width-1)*(height-1)*6 != source.size()) return {};
    const uint32_t missing = std::numeric_limits<uint32_t>::max();
    std::vector<uint32_t> grid(vertices.size(), missing);
    for (size_t i = 0; i < vertices.size(); ++i) {
        const auto& v = vertices[i];
        const int x = int(std::lround(v.px-minX)), y = int(std::lround(maxZ-v.pz));
        if (x < 0 || y < 0 || x >= width || y >= height ||
            std::abs(v.px-minX-x) > 0.001f || std::abs(maxZ-v.pz-y) > 0.001f) return {};
        auto& slot = grid[size_t(y)*width+x];
        if (slot != missing) return {};
        slot = uint32_t(i);
    }
    auto index = [&](int x, int y) { return grid[size_t(y)*width+x]; };
    // Reject masked/non-grid geometry instead of silently filling its holes.
    std::vector<unsigned char> cells(size_t(width-1)*(height-1));
    for (size_t i = 0; i < source.size(); i += 3) {
        int x[3], y[3];
        for (int j = 0; j < 3; ++j) {
            if (source[i+j] >= vertices.size()) return {};
            const auto& v = vertices[source[i+j]];
            x[j] = int(std::lround(v.px-minX)); y[j] = int(std::lround(maxZ-v.pz));
        }
        const int lx = std::min({x[0],x[1],x[2]}), ly = std::min({y[0],y[1],y[2]});
        if (std::max({x[0],x[1],x[2]})-lx != 1 || std::max({y[0],y[1],y[2]})-ly != 1) return {};
        const int area = (x[1]-x[0])*(y[2]-y[0])-(y[1]-y[0])*(x[2]-x[0]);
        if (area != 1 || lx >= width-1 || ly >= height-1) return {};
        // Original terrain's diagonal is top-left to bottom-right.
        unsigned bit = 0;
        for (int j = 0; j < 3; ++j) bit |= 1u << ((y[j]-ly)*2+x[j]-lx);
        const unsigned char flag = bit == 11 ? 1 : bit == 13 ? 2 : 0;
        auto& seen = cells[size_t(ly)*(width-1)+lx];
        if (!flag || (seen & flag)) return {};
        seen |= flag;
    }
    for (auto c : cells) if (c != 3) return {};
    for (int sy = 0; sy < height-1; sy += patchCells) for (int sx = 0; sx < width-1; sx += patchCells) {
        const int ex = std::min(sx+patchCells,width-1), ey = std::min(sy+patchCells,height-1);
        Patch patch;
        patch.lo[0] = minX+sx; patch.hi[0] = minX+ex;
        patch.lo[2] = maxZ-ey; patch.hi[2] = maxZ-sy;
        patch.lo[1] = std::numeric_limits<float>::max(); patch.hi[1] = -patch.lo[1];
        for (int y = sy; y <= ey; ++y) for (int x = sx; x <= ex; ++x) {
            const auto& v = vertices[index(x,y)];
            patch.lo[1] = std::min({patch.lo[1],v.py,v.walk});
            patch.hi[1] = std::max({patch.hi[1],v.py,v.walk});
        }
        for (int step : {1,2,4,8}) {
            Level level; level.first = uint32_t(result.indices.size());
            auto triangle = [&](int ax,int ay,int bx,int by,int cx,int cy) {
                result.indices.insert(result.indices.end(), {index(ax,ay),index(bx,by),index(cx,cy)});
                if (step == 1) return;
                const float area = float((bx-ax)*(cy-ay)-(by-ay)*(cx-ax));
                const auto& a = vertices[index(ax,ay)]; const auto& b = vertices[index(bx,by)]; const auto& c = vertices[index(cx,cy)];
                for (int y = std::min({ay,by,cy}); y <= std::max({ay,by,cy}); ++y)
                    for (int x = std::min({ax,bx,cx}); x <= std::max({ax,bx,cx}); ++x) {
                        const float wb = float((x-ax)*(cy-ay)-(y-ay)*(cx-ax))/area;
                        const float wc = float((bx-ax)*(y-ay)-(by-ay)*(x-ax))/area;
                        if (wb < -1e-6f || wc < -1e-6f || wb+wc > 1.000001f) continue;
                        const auto& v = vertices[index(x,y)];
                        level.error = std::max({level.error, std::abs(v.py-(a.py+wb*(b.py-a.py)+wc*(c.py-a.py))),
                            std::abs(v.walk-(a.walk+wb*(b.walk-a.walk)+wc*(c.walk-a.walk)))});
                    }
                // The two triangulations can cross between source vertices.
                // Their height difference is affine on each overlay triangle:
                // extrema are source vertices or crossings of triangle edges.
                // Sample crossings with all unit grid and source diagonal lines.
                auto crossing = [&](float x,float y,float coarseHeight,float coarseWalk) {
                    const int gx=std::clamp(int(std::floor(x)),0,width-2);
                    const int gy=std::clamp(int(std::floor(y)),0,height-2);
                    const float fx=x-gx, fy=y-gy;
                    const auto& aa=vertices[index(gx,gy)]; const auto& dd=vertices[index(gx+1,gy+1)];
                    const auto& side=vertices[index(gx+(fx>=fy),gy+(fx<fy))];
                    const float diagonal=std::min(fx,fy), other=std::abs(fx-fy);
                    level.error=std::max({level.error,
                        std::abs(coarseHeight-(aa.py+diagonal*(dd.py-aa.py)+other*(side.py-aa.py))),
                        std::abs(coarseWalk-(aa.walk+diagonal*(dd.walk-aa.walk)+other*(side.walk-aa.walk)))});
                };
                const int tx[3]={ax,bx,cx}, ty[3]={ay,by,cy};
                for(int e=0;e<3;++e) {
                    const int n=(e+1)%3;
                    const auto& va=vertices[index(tx[e],ty[e])]; const auto& vb=vertices[index(tx[n],ty[n])];
                    for(int axis=0;axis<3;++axis) {
                        const int start=axis==0?tx[e]:axis==1?ty[e]:ty[e]-tx[e];
                        const int end=axis==0?tx[n]:axis==1?ty[n]:ty[n]-tx[n];
                        if(start==end) continue;
                        for(int line=std::min(start,end);line<=std::max(start,end);++line) {
                            const float t=float(line-start)/float(end-start);
                            crossing(tx[e]+t*(tx[n]-tx[e]),ty[e]+t*(ty[n]-ty[e]),
                                va.py+t*(vb.py-va.py),va.walk+t*(vb.walk-va.walk));
                        }
                    }
                }
            };
            for (int y = sy; y < ey; y += step) for (int x = sx; x < ex; x += step) {
                const int xx = std::min(x+step,ex), yy = std::min(y+step,ey);
                if (step == 1 || (x > sx && y > sy && xx < ex-1 && yy < ey-1)) {
                    triangle(x,y,xx,y,xx,yy); triangle(x,y,xx,yy,x,yy);
                } else if (xx-x < 2 || yy-y < 2) {
                    for (int fy = y; fy < yy; ++fy) for (int fx = x; fx < xx; ++fx) {
                        triangle(fx,fy,fx+1,fy,fx+1,fy+1); triangle(fx,fy,fx+1,fy+1,fx,fy+1);
                    }
                } else {
                    std::vector<std::array<int,2>> ring;
                    ring.push_back({x,y});
                    if (y == sy) for (int p=x+1;p<xx;++p) ring.push_back({p,y});
                    ring.push_back({xx,y});
                    if (xx == ex || ex-xx == 1) for (int p=y+1;p<yy;++p) ring.push_back({xx,p});
                    ring.push_back({xx,yy});
                    if (yy == ey || ey-yy == 1) for (int p=xx-1;p>x;--p) ring.push_back({p,yy});
                    ring.push_back({x,yy});
                    if (x == sx) for (int p=yy-1;p>y;--p) ring.push_back({x,p});
                    for (size_t p=0;p<ring.size();++p) {
                        const auto& a=ring[p]; const auto& b=ring[(p+1)%ring.size()];
                        triangle((x+xx)/2,(y+yy)/2,a[0],a[1],b[0],b[1]);
                    }
                }
            }
            level.count = uint32_t(result.indices.size())-level.first;
            if (!patch.levels.empty() && level.count >= patch.levels.back().count) {
                result.indices.resize(level.first); continue;
            }
            // Maximum vertical error over the overlay of both triangulations,
            // checked at both morph endpoints (up to floating-point rounding).
            patch.levels.push_back(level);
        }
        result.patches.push_back(std::move(patch));
    }
    return result;
}
}
