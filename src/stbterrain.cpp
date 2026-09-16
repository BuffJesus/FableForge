#include "stbterrain.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

#include "forge/lzo.hpp"
#include "forge/stb.hpp"
#include "forge/stbinfo.hpp"

namespace albion::stbterrain {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}

struct Layer {
    std::vector<std::pair<uint16_t, uint16_t>> xy;
    std::vector<uint16_t> strip;
    bool shared = false;
};

// CLandscapeForegroundPatch frame body:
//   u16 layerCount; per layer: u16 vertexCount, u16 polygonCount, u8 mappingDirection,
//   3*u32 textures, u8 sharedIndexBuffer, u32 textureMaxSize, u32 bumpMaxSize,
//   f32 selfIllumination, vertexCount*(u16 x, u16 y, f32 height, u32 normal, u8 blend,
//   u8 cliffU, u8 cliffV), [polygonCount+2 u16 strip unless shared]; u8 hasWater; ...
bool parseForeground(const std::vector<uint8_t>& b, std::vector<Layer>& out) {
    size_t p = 0;
    auto need = [&](size_t n) { return p + n <= b.size(); };
    auto r8 = [&]() { return b[p++]; };
    auto r16 = [&]() { const uint16_t v = uint16_t(b[p] | (b[p + 1] << 8)); p += 2; return v; };
    auto r32 = [&]() { uint32_t v; std::memcpy(&v, b.data() + p, 4); p += 4; return v; };
    if (!need(2)) return false;
    const uint16_t layers = r16();
    if (layers == 0 || layers > 64) return false;
    for (uint16_t li = 0; li < layers; ++li) {
        if (!need(2 + 2 + 1 + 12 + 1 + 4 + 4 + 4)) return false;
        Layer L;
        const uint16_t vc = r16();
        const uint16_t pc = r16();
        const uint8_t dir = r8();
        if (vc == 0 || vc > 4096 || dir > 4) return false;
        for (int i = 0; i < 3; ++i) { const uint32_t tex = r32(); if (tex > 100000) return false; }
        L.shared = r8() != 0;
        const uint32_t tms = r32(), bms = r32(); (void)tms; (void)bms;
        r32();  // self illumination
        if (!need(size_t(vc) * 15)) return false;
        L.xy.reserve(vc);
        for (uint16_t vi = 0; vi < vc; ++vi) {
            const uint16_t x = r16(), y = r16();
            p += 4 + 4 + 3;
            L.xy.emplace_back(x, y);
        }
        if (!L.shared) {
            if (pc > 20000 || !need((size_t(pc) + 2) * 2)) return false;
            L.strip.reserve(size_t(pc) + 2);
            for (size_t i = 0; i < size_t(pc) + 2; ++i) L.strip.push_back(r16());
            for (uint16_t idx : L.strip) if (idx >= vc) return false;
        }
        out.push_back(std::move(L));
    }
    return need(1);   // hasWater byte at least
}

template <typename Fn>
void forEachFrame(const std::vector<uint8_t>& d, Fn&& onFrame) {
    auto u32 = [&](size_t o) { uint32_t v; std::memcpy(&v, d.data() + o, 4); return v; };
    const size_t n = d.size();
    for (size_t off = 0; off + 8 < n; off += 4) {
        const uint32_t a = u32(off), b = u32(off + 4);
        const uint32_t pairs[2][2] = {{a, b}, {b, a}};
        for (int k = 0; k < 2; ++k) {
            const uint32_t unc = pairs[k][0], comp = pairs[k][1];
            if (comp < 32 || comp > 400000 || unc < 64 || unc > 4000000 || comp > unc) continue;
            if (off + 8 + size_t(comp) > n) continue;
            std::vector<uint8_t> out;
            try { out = forge::lzo::decompress(d.data() + off + 8, comp, unc); }
            catch (const std::exception&) { continue; }
            onFrame(out);
            off = ((off + 8 + comp + 3) & ~size_t(3)) - 4;
            break;
        }
    }
}

} // namespace

CellMask load(const fs::path& gameRoot, const std::string& mapName, int mapWidth, int mapHeight) {
    CellMask m;
    m.width = mapWidth; m.height = mapHeight;
    m.present.assign(size_t(std::max(mapWidth, 0)) * size_t(std::max(mapHeight, 0)), 1);
    m.presentCells = int(m.present.size());
    const fs::path stbPath = gameRoot / "data" / "Levels" / "FinalAlbion_RT.stb";
    if (!fs::exists(stbPath) || mapWidth <= 0 || mapHeight <= 0) { m.note = "no STB"; return m; }
    std::vector<uint8_t> chunk;
    int worldX = 0, worldY = 0;
    try {
        const auto archive = forge::stb::Archive::open(stbPath);
        const std::string stem = lower(mapName);
        const forge::stb::StaticMap* map = nullptr;
        for (const auto& c : archive.staticMaps())
            if (lower(fs::path(c.levelName).stem().string()) == stem) { map = &c; break; }
        if (!map) { m.note = "no STB entry"; return m; }
        const auto record = archive.readStaticMapRecord(*map);
        if (record.size() < forge::stbinfo::kInfoBlockSize) { m.note = "short record"; return m; }
        const auto info = forge::stbinfo::readInfoBlock(record.data());
        worldX = info.worldX; worldY = info.worldY;
        for (const auto& e : archive.entries())
            if (int32_t(e.id) == info.bankFileIndex) { chunk = archive.read(e); break; }
        if (chunk.empty()) { m.note = "no chunk"; return m; }
    } catch (const std::exception& e) {
        m.note = e.what();
        return m;
    }

    std::vector<uint8_t> touched(m.present.size(), 0);
    int frames = 0;
    const bool debug = std::getenv("ALBION_DEBUG") != nullptr;
    int total = 0, fgParsed = 0, rejected = 0;
    forEachFrame(chunk, [&](const std::vector<uint8_t>& body) {
        ++total;
        std::vector<Layer> layers;
        if (!parseForeground(body, layers)) return;
        ++fgParsed;
        if (debug) {
            int minx = 1 << 30, maxx = -(1 << 30), miny = 1 << 30, maxy = -(1 << 30); size_t verts = 0, strips = 0;
            for (const auto& L : layers) { for (auto [x, y] : L.xy) { minx = std::min<int>(minx, x); maxx = std::max<int>(maxx, x); miny = std::min<int>(miny, y); maxy = std::max<int>(maxy, y); ++verts; } strips += L.strip.size(); }
            std::fprintf(stderr, "fg frame %zu bytes: %zu layers, %zu verts, %zu strip idx, x %d..%d y %d..%d (local %d..%d, %d..%d)\n",
                         body.size(), layers.size(), verts, strips, minx, maxx, miny, maxy, minx - worldX, maxx - worldX, miny - worldY, maxy - worldY);
        }
        // Vertices are stored in WORLD grid units; sanity-check against the map box.
        bool any = false;
        for (const auto& L : layers) {
            if (L.shared) {
                // sharedIndexBuffer = the engine's canonical full-grid index buffer for
                // this vertex grid (a 17x17 layer is a complete 16x16 patch): every
                // cell inside the layer's vertex box is drawn.
                int minx = 1 << 30, maxx = -(1 << 30), miny = 1 << 30, maxy = -(1 << 30);
                for (auto [vx, vy] : L.xy) { minx = std::min<int>(minx, int(vx) - worldX); maxx = std::max<int>(maxx, int(vx) - worldX); miny = std::min<int>(miny, int(vy) - worldY); maxy = std::max<int>(maxy, int(vy) - worldY); }
                if (minx < 0 || miny < 0 || maxx > mapWidth || maxy > mapHeight || maxx - minx > 64 || maxy - miny > 64) { ++rejected; continue; }
                for (int y = miny; y < maxy && y < mapHeight; ++y)
                    for (int x = minx; x < maxx && x < mapWidth; ++x) { touched[size_t(y) * mapWidth + x] = 1; any = true; }
                continue;
            }
            const std::vector<uint16_t>* strip = &L.strip;
            if (strip->size() < 3) continue;
            for (size_t i = 2; i < strip->size(); ++i) {
                const uint16_t a = (*strip)[i - 2], b = (*strip)[i - 1], c = (*strip)[i];
                if (a == b || b == c || a == c) continue;   // degenerate bridge
                int xs[3] = {int(L.xy[a].first) - worldX, int(L.xy[b].first) - worldX, int(L.xy[c].first) - worldX};
                int ys[3] = {int(L.xy[a].second) - worldY, int(L.xy[b].second) - worldY, int(L.xy[c].second) - worldY};
                const int x0 = std::min({xs[0], xs[1], xs[2]}), x1 = std::max({xs[0], xs[1], xs[2]});
                const int y0 = std::min({ys[0], ys[1], ys[2]}), y1 = std::max({ys[0], ys[1], ys[2]});
                if (x0 < 0 || y0 < 0 || x1 > mapWidth || y1 > mapHeight || x1 - x0 > 32 || y1 - y0 > 32) { ++rejected; if (debug) std::fprintf(stderr, "  rejected tri local (%d..%d, %d..%d)\n", x0, x1, y0, y1); continue; }  // out of this map box: skip the triangle
                // Decimated patches span several cells per triangle: mark the box.
                for (int y = y0; y < std::max(y1, y0 + 1) && y < mapHeight; ++y)
                    for (int x = x0; x < std::max(x1, x0 + 1) && x < mapWidth; ++x) { touched[size_t(y) * mapWidth + x] = 1; any = true; }
            }
        }
        if (any) ++frames;
    });
    m.frames = frames;
    if (debug) std::fprintf(stderr, "frames total %d, foreground-parsed %d, used %d, rejected %d\n", total, fgParsed, frames, rejected);
    if (frames == 0) { m.note = "no foreground frames matched"; return m; }
    m.found = true;
    m.present = std::move(touched);
    m.presentCells = int(std::count(m.present.begin(), m.present.end(), uint8_t(1)));
    return m;
}

} // namespace albion::stbterrain
