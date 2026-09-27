#include "forge/navmesh.hpp"
#include "navgeometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>

namespace forge::navmesh {
namespace {

// FableWin CNavQuadTreeNode::Initialise (0x032851b0), IsAreaClear
// (0x0328c7e0), GoToHigherDetail (0x03286830). Ground only for now.
struct Node {
    RetailNode value;
    std::array<std::unique_ptr<Node>, 4> children;
    std::vector<Node*> neighbours;
    bool reachable = false;
};

bool finer(const DetailArea& n, const std::vector<DetailArea>& areas) {
    for (const auto& b : areas) {
        const bool x = (n.x0 >= b.x0 && n.x0 < b.x1) || (n.x1 >= b.x0 && n.x1 < b.x1);
        const bool y = (n.y0 >= b.y0 && n.y0 < b.y1) || (n.y1 >= b.y0 && n.y1 < b.y1);
        if (x && y) return true;
    }
    return false;
}


std::unique_ptr<Node> build(const lev::File& file, const GroundGeometry& geometry,
                            int level, float cx, float cy) {
    const float half = 16.0f / float(1 << level);
    const DetailArea box{cx - half, cy - half, cx + half, cy + half};
    bool blocked = false, allBlocked = true, same = true;
    const bool first = file.preferredPathAt(int(box.x0), int(box.y0));
    for (int y = int(box.y0); float(y) < box.y1; ++y)
        for (int x = int(box.x0); float(x) < box.x1; ++x) {
            const bool walk = file.walkableAt(x, y);
            blocked |= !walk;
            allBlocked &= !walk;
            same &= file.preferredPathAt(x, y) == first;
        }
    if (allBlocked) return nullptr;
    if (!blocked) blocked = std::any_of(geometry.blockingLines.begin(), geometry.blockingLines.end(),
                                      [&](const Line& l) { return detail::lineBlocksArea(l, box); });
    std::vector<uint64_t> uids;
    if (!blocked) for (const auto& door : geometry.switchableLines)
        if (std::any_of(door.lines.begin(), door.lines.end(), [&](const Line& l) { return detail::lineBlocksArea(l, box); }))
            uids.push_back(door.uid);
    const bool maySplit = level < 5 || (level == 5 && finer(box, geometry.detailedAreas));
    auto node = std::make_unique<Node>();
    auto& v = node->value;
    v.level = uint8_t(level); v.cx = cx; v.cy = cy; v.root = level == 0;
    v.preference = first ? 0 : 0x80;
    if (!blocked && uids.empty() && (!maySplit || same)) { v.leaf = true; return node; }
    if (!maySplit) {
        if (blocked) return nullptr;
        v.leaf = true; v.switchable = true; v.uids = std::move(uids);
        return node;
    }
    bool any = false;
    for (int i = 0; i < 4; ++i) {
        node->children[i] = build(file, geometry, level + 1,
                                  cx + (i & 1 ? half * 0.5f : -half * 0.5f),
                                  cy + (i & 2 ? half * 0.5f : -half * 0.5f));
        any |= node->children[i] != nullptr;
    }
    return any ? std::move(node) : nullptr;
}

void validate(const lev::File& file, const RetailSection& source, const GroundGeometry& geometry) {
    if (source.version != 8 || source.layerCount != 1)
        throw std::invalid_argument("ground nav: requires version 8 with exactly one layer");
    const int w = file.width(), h = file.height();
    if (w <= 0 || h <= 0 || w % 32 || h % 32 || int64_t(w) * h > 16777216)
        throw std::invalid_argument("ground nav: invalid or excessive map dimensions");
    if (source.width != float(w) || source.height != float(h) || source.positions.size() % 12)
        throw std::invalid_argument("ground nav: section dimensions/positions do not match LEV");
    for (size_t at = 8; at < source.positions.size(); at += 12) {
        int32_t layer; std::memcpy(&layer, source.positions.data() + at, 4);
        if (layer != 0) throw std::invalid_argument("ground nav: position refers to another layer");
    }
    auto validLine = [](const Line& l) {
        return std::isfinite(l.x0) && std::isfinite(l.y0) && std::isfinite(l.x1) && std::isfinite(l.y1);
    };
    for (const auto& l : geometry.blockingLines) if (!validLine(l))
        throw std::invalid_argument("ground nav: non-finite blocking line");
    for (const auto& a : geometry.detailedAreas)
        if (!validLine({a.x0, a.y0, a.x1, a.y1}) || a.x0 >= a.x1 || a.y0 >= a.y1)
            throw std::invalid_argument("ground nav: invalid detailed area");
    std::set<uint64_t> seen;
    for (const auto& d : geometry.switchableLines) {
        if (!d.uid || d.uid > 0xffffffffffULL || !seen.insert(d.uid).second)
            throw std::invalid_argument("ground nav: switchable UID must be a unique nonzero map-thing UID");
        for (const auto& l : d.lines) if (!validLine(l))
            throw std::invalid_argument("ground nav: non-finite switchable line");
    }
}

} // namespace

GroundResult generateGround(const lev::File& file, const RetailSection& source,
                            const GroundGeometry& geometry) {
    validate(file, source, geometry);
    GroundResult result;
    result.section = source;
    result.section.nodes.clear();
    const int w = file.width(), h = file.height(), rw = w * 2, rh = h * 2;
    std::vector<std::unique_ptr<Node>> roots;
    for (int y = 0; y < h; y += 32) for (int x = 0; x < w; x += 32)
        roots.push_back(build(file, geometry, 0, float(x + 16), float(y + 16)));
    std::vector<Node*> raster(size_t(rw) * rh, nullptr), leaves;
    auto visit = [&](auto&& self, Node* n) -> void {
        if (!n) return;
        if (!n->value.leaf) { for (auto& c : n->children) self(self, c.get()); return; }
        leaves.push_back(n);
        const float half = 16.0f / float(1 << n->value.level);
        for (int y = int(std::lround(2 * (n->value.cy - half))); y < int(std::lround(2 * (n->value.cy + half))); ++y)
            for (int x = int(std::lround(2 * (n->value.cx - half))); x < int(std::lround(2 * (n->value.cx + half))); ++x)
                raster[size_t(y) * rw + x] = n;
    };
    for (auto& r : roots) visit(visit, r.get());
    auto link = [](Node* a, Node* b) {
        if (!a || !b || a == b) return;
        if (std::find(a->neighbours.begin(), a->neighbours.end(), b) == a->neighbours.end()) {
            a->neighbours.push_back(b); b->neighbours.push_back(a);
        }
    };
    for (int y = 0; y < rh; ++y) for (int x = 0; x < rw; ++x) {
        Node* a = raster[size_t(y) * rw + x];
        if (x + 1 < rw) link(a, raster[size_t(y) * rw + x + 1]);
        if (y + 1 < rh) link(a, raster[size_t(y + 1) * rw + x]);
    }
    // Native SetUpRegions (0x0328f270): seed in input order. Normal leaves
    // propagate to every neighbour; switchable leaves propagate only to other
    // switches. Then seed separate regions on the far side of assigned doors.
    uint32_t region = 0;
    std::vector<Node*> pending;
    auto flood = [&](Node* start) {
        if (start->value.region) return;
        const int32_t id = int32_t(++region);
        start->value.region = id; start->reachable = true; pending.push_back(start);
        while (!pending.empty()) {
            Node* n = pending.back(); pending.pop_back();
            for (Node* b : n->neighbours) {
                if (b->value.region || (n->value.switchable && !b->value.switchable)) continue;
                b->value.region = id; b->reachable = true; pending.push_back(b);
            }
        }
    };
    for (size_t p = 0; p < source.positions.size(); p += 12) {
        float x, y;
        std::memcpy(&x, source.positions.data() + p, 4);
        std::memcpy(&y, source.positions.data() + p + 4, 4);
        if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 || x >= w || y >= h) continue;
        Node* n = raster[size_t(int(y * 2)) * rw + int(x * 2)];
        if (!n) continue;
        ++result.anchorsUsed;
        flood(n);
    }
    auto rootSlot = [w](const Node* n) {
        return int(n->value.cy / 32) * (w / 32) + int(n->value.cx / 32);
    };
    for (size_t begin = 0; begin < leaves.size();) {
        size_t end = begin + 1;
        while (end < leaves.size() && rootSlot(leaves[end]) == rootSlot(leaves[begin])) ++end;
        bool added;
        do {
            added = false;
            for (size_t i = begin; i < end; ++i) {
                Node* n = leaves[i];
                if (n->value.switchable || n->value.region) continue;
                if (std::any_of(n->neighbours.begin(), n->neighbours.end(), [](Node* b) {
                    return b->value.switchable && b->value.region;
                })) { flood(n); added = true; break; }
            }
        } while (added);
        begin = end;
    }
    for (Node* n : leaves) if (!n->reachable) {
        // RemoveRegionZero for switchable leaves (0x03282290) always returns
        // false: unvisited doors survive closed with region zero. Their stored
        // links to other surviving doors remain available for later unlocking.
        if (n->value.switchable) {
            n->reachable = true; n->value.blocked = true;
        } else ++result.leavesRemoved;
    }
    // Remove graph edges before destroying unreachable nodes.
    for (Node* n : leaves) if (n->reachable)
        std::erase_if(n->neighbours, [](Node* b) { return !b->reachable; });
    auto prune = [&](auto&& self, std::unique_ptr<Node>& n) -> void {
        if (!n) return;
        if (n->value.leaf) { if (!n->reachable) n.reset(); return; }
        bool any = false;
        for (auto& c : n->children) { self(self, c); any |= c != nullptr; }
        if (!any) n.reset();
    };
    for (auto& r : roots) prune(prune, r);
    int32_t index = 1;
    auto number = [&](auto&& self, Node* n) -> void {
        for (auto& c : n->children) if (c) self(self, c.get());
        n->value.index = index++;
    };
    for (auto& r : roots) { if (r) number(number, r.get()); else ++index; }
    auto emit = [&](auto&& self, Node* n) -> void {
        for (size_t i = 0; i < 4; ++i) n->value.children[i] = n->children[i] ? n->children[i]->value.index : 0;
        for (Node* b : n->neighbours) n->value.neighbours.push_back(b->value.index);
        std::sort(n->value.neighbours.begin(), n->value.neighbours.end());
        result.section.nodes.push_back(n->value);
        for (auto& c : n->children) if (c) self(self, c.get());
    };
    for (auto& r : roots) {
        if (r) emit(emit, r.get());
        else { RetailNode n; n.marker = n.root = n.blocked = true; result.section.nodes.push_back(n); }
    }
    result.section.regionCount = region + 1;
    return result;
}

} // namespace forge::navmesh
