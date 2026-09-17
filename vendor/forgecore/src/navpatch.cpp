#include "forge/navpatch.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace forge::navmesh {
namespace {

constexpr uint32_t kEndSymbol = 0xACBDEF12u;
constexpr float kRootSize = 32.0f;

struct Cursor {
    const std::vector<uint8_t>& b;
    size_t p;
    void need(size_t n, const char* what) const {
        if (p + n > b.size()) throw std::runtime_error(std::string("nav: truncated ") + what);
    }
    uint8_t u8(const char* what) { need(1, what); return b[p++]; }
    uint32_t u32(const char* what) {
        need(4, what);
        uint32_t v; std::memcpy(&v, b.data() + p, 4); p += 4; return v;
    }
    int32_t i32(const char* what) { return static_cast<int32_t>(u32(what)); }
    float f32(const char* what) { uint32_t v = u32(what); float f; std::memcpy(&f, &v, 4); return f; }
    uint64_t u64(const char* what) { const uint64_t lo = u32(what); const uint64_t hi = u32(what); return lo | (hi << 32); }
};

struct Writer {
    std::vector<uint8_t> b;
    void u8(uint8_t v) { b.push_back(v); }
    void u32(uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i))); }
    void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
    void f32(float f) { uint32_t v; std::memcpy(&v, &f, 4); u32(v); }
    void u64(uint64_t v) { u32(uint32_t(v)); u32(uint32_t(v >> 32)); }
    void bytes(const std::vector<uint8_t>& v) { b.insert(b.end(), v.begin(), v.end()); }
    void text(const std::string& s) { b.insert(b.end(), s.begin(), s.end()); }
};

float nodeSize(int level) { return kRootSize / float(1 << level); }

// Root slot (layer-major, gy-major) from a root's centre.
int rootSlotOf(const RetailNode& n, int rootsX) {
    const int gx = int(std::lround((n.cx - kRootSize * 0.5f) / kRootSize));
    const int gy = int(std::lround((n.cy - kRootSize * 0.5f) / kRootSize));
    return gy * rootsX + gx;
}

struct SectionIndex {
    std::unordered_map<int32_t, size_t> at;   // node index -> position in nodes
    // per layer: slot -> nodes position of the root (marker or real)
    std::vector<std::vector<size_t>> roots;
};

// The loader appends root records to the current layer's root array in file
// order, so a root's slot is its rank among the layer's roots; markers carry
// no coordinates. Real roots are cross-checked against their centre.
std::vector<std::vector<size_t>> rootSlots(const RetailSection& sec, int rootsX, int rootsY,
                                           const std::vector<char>* dead = nullptr) {
    std::vector<std::vector<size_t>> roots(sec.layerCount, std::vector<size_t>(size_t(rootsX) * rootsY, SIZE_MAX));
    std::vector<size_t> counter(sec.layerCount, 0);
    for (size_t i = 0; i < sec.nodes.size(); ++i) {
        const RetailNode& n = sec.nodes[i];
        if (dead && (*dead)[i]) continue;
        if (!(n.root || n.marker)) continue;
        if (n.layer >= sec.layerCount) throw std::runtime_error("nav: root layer out of range");
        const size_t slot = counter[n.layer]++;
        if (slot >= roots[n.layer].size()) throw std::runtime_error("nav: more roots than slots");
        if (!n.marker && size_t(rootSlotOf(n, rootsX)) != slot)
            throw std::runtime_error("nav: root record out of slot order");
        roots[n.layer][slot] = i;
    }
    return roots;
}

SectionIndex indexSection(const RetailSection& sec, int rootsX, int rootsY) {
    SectionIndex ix;
    for (size_t i = 0; i < sec.nodes.size(); ++i) {
        const RetailNode& n = sec.nodes[i];
        if (n.marker) continue;
        if (!ix.at.emplace(n.index, i).second)
            throw std::runtime_error("nav: duplicate node index " + std::to_string(n.index));
    }
    ix.roots = rootSlots(sec, rootsX, rootsY);
    return ix;
}

void emitSubtree(Writer& w, const RetailSection& sec, const SectionIndex& ix, size_t pos) {
    const RetailNode& n = sec.nodes[pos];
    w.u8(n.switchable ? 1 : 0); w.u8(n.root ? 1 : 0); w.u8(n.blocked ? 1 : 0);
    if (n.marker) return;
    w.u8(n.leaf ? 1 : 0); w.u8(n.level); w.u8(n.layer);
    w.f32(n.cx); w.f32(n.cy); w.i32(n.index);
    if (!n.leaf) {
        for (int32_t c : n.children) w.i32(c);
        for (int32_t c : n.children) {
            if (c == 0) continue;
            const auto it = ix.at.find(c);
            if (it == ix.at.end()) throw std::runtime_error("nav: child index " + std::to_string(c) + " has no node");
            emitSubtree(w, sec, ix, it->second);
        }
        return;
    }
    w.i32(n.region); w.u8(n.preference);
    w.i32(int32_t(n.neighbours.size()));
    for (int32_t nb : n.neighbours) w.i32(nb);
    if (n.switchable) {
        w.u32(uint32_t(n.uids.size()));
        for (uint64_t u : n.uids) w.u64(u);
    }
}

size_t countSubtree(const RetailSection& sec, const SectionIndex& ix, size_t pos) {
    const RetailNode& n = sec.nodes[pos];
    if (n.marker || n.leaf) return 1;
    size_t total = 1;
    for (int32_t c : n.children) if (c != 0) total += countSubtree(sec, ix, ix.at.at(c));
    return total;
}

std::vector<uint8_t> emitSection(const RetailSection& sec, int rootsX, int rootsY, uint32_t blockEnd) {
    const SectionIndex ix = indexSection(sec, rootsX, rootsY);
    Writer w;
    w.u32(blockEnd); w.u32(sec.version); w.f32(sec.width); w.f32(sec.height);
    w.u32(sec.regionCount);
    w.u32(uint32_t(sec.positions.size() / 12));
    w.bytes(sec.positions);
    w.u32(sec.layerCount);
    size_t total = 0;
    for (const auto& layer : ix.roots)
        for (size_t pos : layer) {
            if (pos == SIZE_MAX) throw std::runtime_error("nav: a root slot has no node");
            total += countSubtree(sec, ix, pos);
        }
    w.i32(int32_t(total));
    for (const auto& layer : ix.roots)
        for (size_t pos : layer) emitSubtree(w, sec, ix, pos);
    w.u32(kEndSymbol);
    return w.b;
}

} // namespace

RetailNav parseNavigation(const lev::File& file) {
    const auto& bytes = file.originalBytes();
    const int w = file.width(), h = file.height();
    if (w <= 0 || h <= 0 || w % 32 != 0 || h % 32 != 0)
        throw std::runtime_error("nav: map dimensions must be positive multiples of 32");
    const size_t rootsPerLayer = size_t(w / 32) * size_t(h / 32);

    Cursor dir{bytes, file.navigationOffset()};
    dir.u32("directory end");
    const uint32_t count = dir.u32("section count");
    if (count > 256) throw std::runtime_error("nav: implausible section count");
    struct Toc { std::string name; uint32_t offset; };
    std::vector<Toc> toc;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t len = dir.u32("section name length");
        if (len > 4096) throw std::runtime_error("nav: invalid section name");
        dir.need(len, "section name");
        std::string name(reinterpret_cast<const char*>(bytes.data() + dir.p), len);
        dir.p += len;
        toc.push_back({std::move(name), dir.u32("section offset")});
    }

    RetailNav nav;
    for (const auto& entry : toc) {
        Cursor c{bytes, entry.offset};
        RetailSection sec;
        sec.name = entry.name;
        c.u32("block end");
        sec.version = c.u32("block version");
        if (sec.version != 8) throw std::runtime_error("nav: unsupported block version " + std::to_string(sec.version));
        sec.width = c.f32("width"); sec.height = c.f32("height");
        if (std::lround(sec.width) != w || std::lround(sec.height) != h)
            throw std::runtime_error("nav: block dimensions differ from the LEV");
        sec.regionCount = c.u32("region count");
        const uint32_t positions = c.u32("position count");
        if (positions > 100000) throw std::runtime_error("nav: implausible position count");
        c.need(size_t(positions) * 12, "positions");
        sec.positions.assign(bytes.begin() + c.p, bytes.begin() + c.p + size_t(positions) * 12);
        c.p += size_t(positions) * 12;
        sec.layerCount = c.u32("layer count");
        if (sec.layerCount > 16) throw std::runtime_error("nav: implausible layer count");
        const int32_t total = c.i32("node total");
        if (total < 0 || size_t(total) > bytes.size()) throw std::runtime_error("nav: implausible node total");
        sec.nodes.reserve(size_t(total));
        size_t rootsSeen = 0; uint8_t layer = 0;
        for (int32_t i = 0; i < total; ++i) {
            RetailNode n;
            n.switchable = c.u8("switchable") != 0;
            n.root = c.u8("root") != 0;
            n.blocked = c.u8("blocked") != 0;
            if (!n.switchable && n.blocked) {
                if (!n.root) throw std::runtime_error("nav: blocked marker below the root level");
                n.marker = true; n.layer = layer;
            } else {
                n.leaf = c.u8("leaf") != 0;
                n.level = c.u8("level");
                n.layer = c.u8("layer");
                n.cx = c.f32("cx"); n.cy = c.f32("cy");
                n.index = c.i32("index");
                if (n.level > 6) throw std::runtime_error("nav: node level out of range");
                if (n.root && n.layer != layer) throw std::runtime_error("nav: root layer does not follow the file order");
                if (!n.leaf) {
                    for (int32_t& ch : n.children) ch = c.i32("child");
                } else {
                    n.region = c.i32("region");
                    n.preference = c.u8("preference");
                    const int32_t nn = c.i32("neighbour count");
                    if (nn < 0 || nn > 4096) throw std::runtime_error("nav: implausible neighbour count");
                    n.neighbours.resize(size_t(nn));
                    for (int32_t& nb : n.neighbours) nb = c.i32("neighbour");
                    if (n.switchable) {
                        const uint32_t m = c.u32("uid count");
                        if (m > 4096) throw std::runtime_error("nav: implausible uid count");
                        n.uids.resize(m);
                        for (uint64_t& u : n.uids) u = c.u64("uid");
                    }
                }
            }
            if (n.root || n.marker) {
                if (++rootsSeen == rootsPerLayer) { rootsSeen = 0; ++layer; }
            }
            sec.nodes.push_back(std::move(n));
        }
        if (c.u32("end symbol") != kEndSymbol) throw std::runtime_error("nav: missing end symbol in section " + sec.name);
        nav.sections.push_back(std::move(sec));
    }
    return nav;
}

std::vector<uint8_t> emitNavigation(const lev::File& file, const RetailNav& nav) {
    const int rootsX = file.width() / 32, rootsY = file.height() / 32;
    size_t directorySize = 8;
    for (const auto& sec : nav.sections) directorySize += 8 + sec.name.size();
    uint32_t offset = file.navigationOffset() + uint32_t(directorySize);
    std::vector<std::vector<uint8_t>> blocks;
    std::vector<uint32_t> offsets;
    for (const auto& sec : nav.sections) {
        offsets.push_back(offset);
        auto sizing = emitSection(sec, rootsX, rootsY, 0);
        offset += uint32_t(sizing.size());
        blocks.push_back(emitSection(sec, rootsX, rootsY, offset));
    }
    Writer dir;
    dir.u32(offsets.empty() ? file.navigationOffset() + uint32_t(directorySize) : offsets.front());
    dir.u32(uint32_t(nav.sections.size()));
    for (size_t i = 0; i < nav.sections.size(); ++i) {
        dir.u32(uint32_t(nav.sections[i].name.size()));
        dir.text(nav.sections[i].name);
        dir.u32(offsets[i]);
    }
    std::vector<uint8_t> out(file.originalBytes().begin(), file.originalBytes().begin() + file.navigationOffset());
    out.insert(out.end(), dir.b.begin(), dir.b.end());
    for (const auto& b : blocks) out.insert(out.end(), b.begin(), b.end());
    if (out.size() != offset) throw std::runtime_error("nav: emitted size does not match the offset chain");
    return out;
}

namespace {

// Working view of one section for the walkability patch.
struct Work {
    RetailSection& sec;
    int w, h, rootsX, rootsY;
    std::unordered_map<int32_t, size_t> at;      // index -> position (push_back only; erased at the end)
    std::unordered_map<int32_t, int32_t> parent; // index -> parent index (roots absent)
    std::vector<char> dead;
    std::vector<int32_t> cover;                  // layer-0 half-cell grid: covering leaf index or 0
    std::unordered_set<int32_t> dirty;           // leaves whose neighbours must be recomputed
    bool changed = false;
    int32_t nextIndex = 1;
    WalkabilityPatchResult stats;

    Work(RetailSection& s, int width, int height)
        : sec(s), w(width), h(height), rootsX(width / 32), rootsY(height / 32) {
        dead.assign(sec.nodes.size(), 0);
        cover.assign(size_t(2 * w) * size_t(2 * h), 0);
        for (size_t i = 0; i < sec.nodes.size(); ++i) {
            const RetailNode& n = sec.nodes[i];
            if (n.marker) continue;
            at[n.index] = i;
            nextIndex = std::max(nextIndex, n.index + 1);
            if (!n.leaf) for (int32_t c : n.children) if (c != 0) parent[c] = n.index;
        }
        for (size_t i = 0; i < sec.nodes.size(); ++i) {
            const RetailNode& n = sec.nodes[i];
            if (!n.marker && n.leaf && n.layer == 0) coverLeaf(n, n.index);
        }
    }

    RetailNode& node(int32_t index) { return sec.nodes[at.at(index)]; }
    bool alive(int32_t index) const { auto it = at.find(index); return it != at.end() && !dead[it->second]; }

    void halfRect(const RetailNode& n, int& x0, int& y0, int& x1, int& y1) const {
        const float half = nodeSize(n.level) * 0.5f;
        x0 = std::max(0, int(std::lround((n.cx - half) * 2)));
        y0 = std::max(0, int(std::lround((n.cy - half) * 2)));
        x1 = std::min(2 * w, int(std::lround((n.cx + half) * 2)));
        y1 = std::min(2 * h, int(std::lround((n.cy + half) * 2)));
    }
    void coverLeaf(const RetailNode& n, int32_t value) {
        int x0, y0, x1, y1; halfRect(n, x0, y0, x1, y1);
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) cover[size_t(y) * size_t(2 * w) + size_t(x)] = value;
    }
    int32_t coverAt(int hx, int hy) const {
        if (hx < 0 || hy < 0 || hx >= 2 * w || hy >= 2 * h) return 0;
        return cover[size_t(hy) * size_t(2 * w) + size_t(hx)];
    }

    size_t add(RetailNode n) {
        n.index = nextIndex++;
        at[n.index] = sec.nodes.size();
        sec.nodes.push_back(std::move(n));
        dead.push_back(0);
        return sec.nodes.size() - 1;
    }

    void unlinkNeighbours(int32_t index) {
        RetailNode& n = node(index);
        for (int32_t nb : n.neighbours) {
            if (!alive(nb)) continue;
            auto& list = node(nb).neighbours;
            list.erase(std::remove(list.begin(), list.end(), index), list.end());
            dirty.insert(nb);
        }
        n.neighbours.clear();
        dirty.erase(index);
    }

    void prune(int32_t index) {
        RetailNode& n = node(index);
        for (int32_t c : n.children) if (c != 0) return;
        if (n.root) {
            // an empty root is written as the 3-byte blocked marker
            RetailNode m; m.marker = true; m.root = true; m.layer = n.layer;
            m.cx = n.cx; m.cy = n.cy;   // kept so the slot can still be found
            at.erase(index);
            n = m;
            return;
        }
        const int32_t p = parent.at(index);
        RetailNode& pn = node(p);
        for (int32_t& c : pn.children) if (c == index) c = 0;
        dead[at.at(index)] = 1;
        at.erase(index);
        parent.erase(index);
        prune(p);
    }

    void removeLeaf(int32_t index) {
        RetailNode& n = node(index);
        coverLeaf(n, 0);
        unlinkNeighbours(index);
        ++stats.leavesRemoved;
        auto pit = parent.find(index);
        if (pit == parent.end()) {
            // a root that is itself a leaf (a fully walkable 32x32 block)
            RetailNode m; m.marker = true; m.root = true; m.layer = n.layer; m.cx = n.cx; m.cy = n.cy;
            at.erase(index);
            n = m;
            return;
        }
        const int32_t p = pit->second;
        RetailNode& pn = node(p);
        for (int32_t& c : pn.children) if (c == index) c = 0;
        dead[at.at(index)] = 1;
        at.erase(index);
        parent.erase(index);
        prune(p);
    }

    // Turn a leaf into an internal node with four leaf children that keep its attributes.
    void splitLeaf(int32_t index) {
        ++stats.nodesSplit;
        coverLeaf(node(index), 0);
        unlinkNeighbours(index);
        RetailNode proto = node(index);
        {
            RetailNode& n = node(index);
            n.leaf = false; n.region = 0; n.preference = 0x80; n.neighbours.clear();
            n.switchable = false; n.uids.clear(); n.blocked = false;
            for (int32_t& c : n.children) c = 0;
        }
        const float q = nodeSize(proto.level) * 0.25f;
        constexpr int sx[4] = {-1, 1, -1, 1};
        constexpr int sy[4] = {-1, -1, 1, 1};
        for (int i = 0; i < 4; ++i) {
            RetailNode c = proto;
            c.root = false; c.marker = false;
            c.level = uint8_t(proto.level + 1);
            c.cx = proto.cx + sx[i] * q; c.cy = proto.cy + sy[i] * q;
            c.neighbours.clear();
            for (int32_t& ch : c.children) ch = 0;
            const size_t pos = add(std::move(c));
            const int32_t ci = sec.nodes[pos].index;
            node(index).children[i] = ci;
            parent[ci] = index;
            if (sec.nodes[pos].layer == 0) coverLeaf(sec.nodes[pos], ci);
            dirty.insert(ci);
        }
    }

    void blockCell(int x, int y) {
        bool any = false;
        for (;;) {
            int32_t found = 0;
            for (int j = 0; j < 2 && !found; ++j)
                for (int i = 0; i < 2 && !found; ++i) found = coverAt(2 * x + i, 2 * y + j);
            if (!found) break;
            any = true; changed = true;
            if (node(found).level < 5) splitLeaf(found);
            else removeLeaf(found);
        }
        if (any) ++stats.cellsBlocked;
    }

    void openCell(int x, int y, bool preferred) {
        for (int j = 0; j < 2; ++j)
            for (int i = 0; i < 2; ++i)
                if (coverAt(2 * x + i, 2 * y + j)) { ++stats.cellsSkipped; return; }
        const int gx = x / 32, gy = y / 32;
        const size_t slot = size_t(gy) * size_t(rootsX) + size_t(gx);
        const size_t rootPos = rootSlots(sec, rootsX, rootsY, &dead)[0][slot];
        if (rootPos == SIZE_MAX) throw std::runtime_error("nav: root slot missing while opening a cell");
        if (sec.nodes[rootPos].marker) {
            RetailNode& r = sec.nodes[rootPos];
            r.marker = false; r.root = true; r.leaf = false; r.level = 0; r.layer = 0;
            r.cx = kRootSize * (gx + 0.5f); r.cy = kRootSize * (gy + 0.5f);
            r.index = nextIndex++;
            at[r.index] = rootPos;
        }
        int32_t cur = sec.nodes[rootPos].index;
        const float px = x + 0.5f, py = y + 0.5f;
        for (;;) {
            RetailNode& n = node(cur);
            if (n.leaf) { ++stats.cellsSkipped; return; }   // cannot happen with no coverage, but stay safe
            const int qx = px > n.cx ? 1 : 0, qy = py > n.cy ? 1 : 0;
            const int q = qy * 2 + qx;
            if (n.children[q] == 0) {
                RetailNode c;
                c.level = uint8_t(n.level + 1); c.layer = 0;
                const float off = nodeSize(n.level) * 0.25f;
                c.cx = n.cx + (qx ? off : -off); c.cy = n.cy + (qy ? off : -off);
                if (c.level == 5) { c.leaf = true; c.preference = preferred ? 0x00 : 0x80; c.region = 0; }
                const size_t pos = add(std::move(c));
                const int32_t ci = sec.nodes[pos].index;
                node(cur).children[q] = ci;
                parent[ci] = cur;
                if (sec.nodes[pos].leaf) {
                    coverLeaf(sec.nodes[pos], ci);
                    dirty.insert(ci);
                    ++stats.leavesAdded; ++stats.cellsOpened; changed = true;
                    return;
                }
                cur = ci;
            } else {
                cur = n.children[q];
            }
        }
    }

    void recomputeNeighbours() {
        for (int32_t index : std::vector<int32_t>(dirty.begin(), dirty.end())) {
            if (!alive(index)) continue;
            RetailNode& n = node(index);
            if (!n.leaf || n.layer != 0 || n.blocked) continue;
            int x0, y0, x1, y1; halfRect(n, x0, y0, x1, y1);
            std::vector<int32_t> geo;
            auto consider = [&](int hx, int hy) {
                const int32_t c = coverAt(hx, hy);
                if (c == 0 || c == index || !alive(c) || node(c).blocked) return;
                if (std::find(geo.begin(), geo.end(), c) == geo.end()) geo.push_back(c);
            };
            for (int y = y0; y < y1; ++y) { consider(x0 - 1, y); consider(x1, y); }
            for (int x = x0; x < x1; ++x) { consider(x, y0 - 1); consider(x, y1); }
            std::sort(geo.begin(), geo.end());
            std::vector<int32_t> list;
            for (int32_t nb : n.neighbours) {
                if (!alive(nb)) continue;
                const bool crossLayer = node(nb).layer != 0;
                if (crossLayer || std::binary_search(geo.begin(), geo.end(), nb)) list.push_back(nb);
            }
            for (int32_t g : geo)
                if (std::find(list.begin(), list.end(), g) == list.end()) list.push_back(g);
            n.neighbours = std::move(list);
            for (int32_t g : geo) {
                auto& other = node(g).neighbours;
                if (std::find(other.begin(), other.end(), index) == other.end()) other.push_back(index);
            }
        }
    }

    void relabelRegions() {
        // components over the non-switchable alive leaves
        std::unordered_map<int32_t, int> comp;
        std::vector<std::vector<int32_t>> members;
        for (size_t i = 0; i < sec.nodes.size(); ++i) {
            const RetailNode& s = sec.nodes[i];
            if (dead[i] || s.marker || !s.leaf || s.switchable || comp.count(s.index)) continue;
            const int id = int(members.size());
            members.emplace_back();
            std::vector<int32_t> stack{s.index};
            comp[s.index] = id;
            while (!stack.empty()) {
                const int32_t cur = stack.back(); stack.pop_back();
                members.back().push_back(cur);
                for (int32_t nb : node(cur).neighbours) {
                    if (!alive(nb) || comp.count(nb)) continue;
                    const RetailNode& t = node(nb);
                    if (!t.leaf || t.switchable) continue;
                    comp[nb] = id; stack.push_back(nb);
                }
            }
        }
        std::vector<size_t> order(members.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return members[a].size() > members[b].size(); });
        int32_t maxRegion = 0;
        for (size_t i = 0; i < sec.nodes.size(); ++i)
            if (!dead[i] && !sec.nodes[i].marker && sec.nodes[i].leaf) maxRegion = std::max(maxRegion, sec.nodes[i].region);
        std::unordered_set<int32_t> taken;
        for (size_t c : order) {
            std::map<int32_t, size_t> votes;
            for (int32_t m : members[c]) { const int32_t r = node(m).region; if (r != 0) ++votes[r]; }
            int32_t best = 0; size_t bestVotes = 0;
            for (const auto& [r, v] : votes)
                if (v > bestVotes && !taken.count(r)) { best = r; bestVotes = v; }
            if (votes.size() > 1) stats.regionsMerged += votes.size() - 1;
            if (best == 0) { best = ++maxRegion; ++stats.regionsAdded; }
            taken.insert(best);
            for (int32_t m : members[c]) node(m).region = best;
        }
        int32_t top = 0;
        for (size_t i = 0; i < sec.nodes.size(); ++i)
            if (!dead[i] && !sec.nodes[i].marker && sec.nodes[i].leaf) top = std::max(top, sec.nodes[i].region);
        sec.regionCount = std::max(sec.regionCount, uint32_t(top) + 1);
    }

    void compact() {
        std::vector<RetailNode> kept;
        kept.reserve(sec.nodes.size());
        for (size_t i = 0; i < sec.nodes.size(); ++i) if (!dead[i]) kept.push_back(std::move(sec.nodes[i]));
        sec.nodes = std::move(kept);
    }
};

} // namespace

WalkabilityPatchResult patchWalkability(RetailNav& nav, const lev::File& file,
                                        const std::vector<std::pair<int, int>>& cells) {
    WalkabilityPatchResult total;
    const int w = file.width(), h = file.height();
    bool first = true;
    for (auto& sec : nav.sections) {
        if (sec.layerCount == 0) {
            // an empty tree: give it one layer of blocked roots so cells can be opened
            sec.layerCount = 1;
            for (int i = 0; i < (w / 32) * (h / 32); ++i) { RetailNode m; m.marker = true; m.root = true; sec.nodes.push_back(m); }
        }
        Work work(sec, w, h);
        for (const auto& [x, y] : cells) {
            if (x < 0 || y < 0 || x >= w || y >= h) continue;   // the nav covers w x h cells
            if (file.walkableAt(x, y)) work.openCell(x, y, file.preferredPathAt(x, y));
            else work.blockCell(x, y);
        }
        if (work.changed) {
            work.recomputeNeighbours();
            work.relabelRegions();
            work.compact();
        }
        // every section sees the same cells: cell counts come from the first,
        // node/region counts are summed over the sections
        if (first) { total.cellsBlocked = work.stats.cellsBlocked; total.cellsOpened = work.stats.cellsOpened; total.cellsSkipped = work.stats.cellsSkipped; first = false; }
        total.leavesRemoved += work.stats.leavesRemoved; total.leavesAdded += work.stats.leavesAdded;
        total.nodesSplit += work.stats.nodesSplit;
        total.regionsAdded += work.stats.regionsAdded; total.regionsMerged += work.stats.regionsMerged;
    }
    return total;
}

} // namespace forge::navmesh
