#include "stbterrain.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

#include "forge/lzo.hpp"
#include "forge/rangecodec.hpp"
#include "forge/stbbake.hpp"
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
    // A chunk's LZO frames are packed back-to-back at byte granularity: retail
    // OakValeWest_v2 holds 895, and 530 of them start off the 4-byte lattice the
    // old probe stepped on (the town-square oak and three quarters of the grass
    // lived in those). Walk them with the writer's proven byte-granular walker.
    for (auto& b : forge::stbbake::walkFramedBlocks(d)) onFrame(b.data);
}

// Shared: locate the map's chunk + world origin.
bool findChunk(const fs::path& gameRoot, const std::string& mapName, std::vector<uint8_t>& chunk,
               int& worldX, int& worldY, std::string& note) {
    const fs::path stbPath = gameRoot / "data" / "Levels" / "FinalAlbion_RT.stb";
    if (!fs::exists(stbPath)) { note = "no STB"; return false; }
    try {
        const auto archive = forge::stb::Archive::open(stbPath);
        const std::string stem = lower(mapName);
        const forge::stb::StaticMap* map = nullptr;
        for (const auto& c : archive.staticMaps())
            if (lower(fs::path(c.levelName).stem().string()) == stem) { map = &c; break; }
        if (!map) { note = "no STB entry"; return false; }
        const auto record = archive.readStaticMapRecord(*map);
        if (record.size() < forge::stbinfo::kInfoBlockSize) { note = "short record"; return false; }
        const auto info = forge::stbinfo::readInfoBlock(record.data());
        worldX = info.worldX; worldY = info.worldY;
        for (const auto& e : archive.entries())
            if (int32_t(e.id) == info.bankFileIndex) { chunk = archive.read(e); break; }
        if (chunk.empty()) { note = "no chunk"; return false; }
        return true;
    } catch (const std::exception& e) {
        note = e.what();
        return false;
    }
}

} // namespace

BackgroundAlbedo backgroundAlbedo(const fs::path& gameRoot, const std::string& mapName, int mapWidth, int mapHeight) {
    BackgroundAlbedo out;
    std::vector<uint8_t> chunk;
    int worldX = 0, worldY = 0;
    if (!findChunk(gameRoot, mapName, chunk, worldX, worldY, out.note) || mapWidth <= 0 || mapHeight <= 0) return out;
    const int tpc = out.texelsPerCell;
    out.image.width = uint32_t(mapWidth * tpc);
    out.image.height = uint32_t(mapHeight * tpc);
    out.image.rgba.assign(size_t(out.image.width) * out.image.height * 4, 0);
    out.image.name = "background";
    std::vector<uint8_t> covered(size_t(mapWidth) * mapHeight, 0);
    forEachFrame(chunk, [&](const std::vector<uint8_t>& b) {
        // CLandscapeBackgroundPatch: 17-byte header, 19-byte inline texture header, DXT1 mip 0.
        if (b.size() < 17 + 19) return;
        auto r16 = [&](size_t o) { return uint16_t(b[o] | (b[o + 1] << 8)); };
        auto r32 = [&](size_t o) { uint32_t v; std::memcpy(&v, b.data() + o, 4); return v; };
        const uint16_t pw = r16(0), ph = r16(2), c0 = r16(4), c1 = r16(6);
        const bool waterOnly = b[8] != 0;
        const uint16_t vertexCount = r16(12);
        const bool isDXT = b[16] != 0;
        if (waterOnly || pw == 0 || ph == 0 || pw > 64 || ph > 64) return;
        if (uint32_t(pw + 1) * uint32_t(ph + 1) != vertexCount) return;
        const uint16_t tw = r16(17), th = r16(19);
        const uint8_t levels = b[21];
        const uint32_t fmt0 = r32(22);
        if (!isDXT || levels != 1 || tw == 0 || th == 0 || tw > 512 || th > 512) return;
        if ((fmt0 & 0xffu) != 3u && fmt0 != 3u) return;   // DXT1 only
        const size_t mip0 = size_t(tw) * th / 2;
        if (17 + 19 + mip0 > b.size()) return;
        // Patch origin: world grid coords (subtract the map origin), else map-local.
        int px = int(c0) - worldX, py = int(c1) - worldY;
        if (px < 0 || py < 0 || px + pw > mapWidth || py + ph > mapHeight) { px = c0; py = c1; }
        if (px < 0 || py < 0 || px + pw > mapWidth || py + ph > mapHeight) return;
        const auto rgba = terrainexport::decodeBc1ToRgba(b.data() + 17 + 19, tw, th);
        // Resample the patch texture onto the tpc grid of its pw x ph cells.
        for (int y = 0; y < ph * tpc; ++y)
            for (int x = 0; x < pw * tpc; ++x) {
                const int sx = std::min(int(tw) - 1, x * int(tw) / (pw * tpc));
                const int sy = std::min(int(th) - 1, y * int(th) / (ph * tpc));
                const uint8_t* s = &rgba[(size_t(sy) * tw + sx) * 4];
                uint8_t* d = &out.image.rgba[((size_t(py) * tpc + y) * out.image.width + size_t(px) * tpc + x) * 4];
                d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255;
            }
        for (int y = 0; y < ph; ++y) for (int x = 0; x < pw; ++x) covered[size_t(py + y) * mapWidth + px + x] = 1;
        ++out.patches;
    });
    const size_t cov = size_t(std::count(covered.begin(), covered.end(), uint8_t(1)));
    out.found = out.patches > 0;
    out.note = std::to_string(out.patches) + " background patches cover " + std::to_string(cov) + " of " + std::to_string(size_t(mapWidth) * mapHeight) + " cells";
    return out;
}

ForegroundLayers loadLayers(const fs::path& gameRoot, const std::string& mapName, int mapWidth, int mapHeight) {
    ForegroundLayers out;
    std::vector<uint8_t> chunk;
    int worldX = 0, worldY = 0;
    if (!findChunk(gameRoot, mapName, chunk, worldX, worldY, out.note)) return out;
    int frameIndex = 0;
    forEachFrame(chunk, [&](const std::vector<uint8_t>& b) {
        ++frameIndex;
        // Same grammar as parseForeground, keeping everything.
        size_t p = 0;
        auto need = [&](size_t n) { return p + n <= b.size(); };
        auto r8 = [&]() { return b[p++]; };
        auto r16 = [&]() { const uint16_t v = uint16_t(b[p] | (b[p + 1] << 8)); p += 2; return v; };
        auto r32 = [&]() { uint32_t v; std::memcpy(&v, b.data() + p, 4); p += 4; return v; };
        auto rf = [&]() { float v; std::memcpy(&v, b.data() + p, 4); p += 4; return v; };
        if (!need(2)) return;
        const uint16_t layers = r16();
        if (layers == 0 || layers > 64) return;
        std::vector<ForegroundLayer> parsed;
        for (uint16_t li = 0; li < layers; ++li) {
            if (!need(2 + 2 + 1 + 12 + 1 + 4 + 4 + 4)) return;
            ForegroundLayer L;
            L.patchIndex = frameIndex - 1; L.layerIndex = li;
            const uint16_t vc = r16();
            const uint16_t pc = r16();
            L.direction = r8();
            if (vc == 0 || vc > 4096 || L.direction > 4) return;
            L.texture = r32(); L.backgroundTexture = r32(); L.bumpTexture = r32();
            if (L.texture > 100000 || L.backgroundTexture > 100000 || L.bumpTexture > 100000) return;
            L.sharedIndexBuffer = r8() != 0;
            r32(); r32();
            L.selfIllumination = rf();
            if (!need(size_t(vc) * 15)) return;
            L.vertices.reserve(vc);
            for (uint16_t vi = 0; vi < vc; ++vi) {
                LayerVertex v;
                v.x = int(r16()) - worldX; v.y = int(r16()) - worldY;
                v.height = rf(); r32();
                v.blend = r8(); v.b1 = r8(); v.b2 = r8();
                if (v.x < 0 || v.y < 0 || v.x > mapWidth || v.y > mapHeight) return;
                L.vertices.push_back(v);
            }
            if (!L.sharedIndexBuffer) {
                if (pc > 20000 || !need((size_t(pc) + 2) * 2)) return;
                L.strip.reserve(size_t(pc) + 2);
                for (size_t i = 0; i < size_t(pc) + 2; ++i) L.strip.push_back(r16());
                for (uint16_t idx : L.strip) if (idx >= vc) return;
            }
            parsed.push_back(std::move(L));
        }
        if (!need(1)) return;
        ++out.frames;
        for (auto& L : parsed) out.layers.push_back(std::move(L));
    });
    out.found = out.frames > 0;
    if (!out.found) out.note = "no foreground frames matched";
    return out;
}

CellMask load(const fs::path& gameRoot, const std::string& mapName, int mapWidth, int mapHeight) {
    CellMask m;
    m.width = mapWidth; m.height = mapHeight;
    m.present.assign(size_t(std::max(mapWidth, 0)) * size_t(std::max(mapHeight, 0)), 1);
    m.presentCells = int(m.present.size());
    if (mapWidth <= 0 || mapHeight <= 0) { m.note = "empty map"; return m; }
    std::vector<uint8_t> chunk;
    int worldX = 0, worldY = 0;
    if (!findChunk(gameRoot, mapName, chunk, worldX, worldY, m.note)) return m;

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

// ---- baked water patches (CWaterPatchMesh::Save) ----

void packWaterRecord(const WaterPatchRecord& r, uint8_t* out) {
    auto put16 = [&](size_t at, uint16_t v) { out[at] = uint8_t(v); out[at + 1] = uint8_t(v >> 8); };
    auto putf = [&](size_t at, float v) { std::memcpy(out + at, &v, 4); };
    put16(0, r.x); put16(2, r.y);
    putf(4, r.z);
    put16(8, uint16_t(r.waveS)); put16(10, uint16_t(r.waveC)); put16(12, uint16_t(r.depth));
    putf(14, r.distToShore);
    for (int i = 0; i < 12; ++i) putf(18 + size_t(i) * 4, r.shore[i]);
}

WaterPatchRecord unpackWaterRecord(const uint8_t* in) {
    WaterPatchRecord r;
    auto get16 = [&](size_t at) { return uint16_t(in[at] | (in[at + 1] << 8)); };
    auto getf = [&](size_t at) { float v; std::memcpy(&v, in + at, 4); return v; };
    r.x = get16(0); r.y = get16(2);
    r.z = getf(4);
    r.waveS = int16_t(get16(8)); r.waveC = int16_t(get16(10)); r.depth = int16_t(get16(12));
    r.distToShore = getf(14);
    for (int i = 0; i < 12; ++i) r.shore[i] = getf(18 + size_t(i) * 4);
    return r;
}

WaterPatches loadWaterPatches(const fs::path& gameRoot, const std::string& mapName) {
    WaterPatches out;
    std::vector<uint8_t> chunk;
    if (!findChunk(gameRoot, mapName, chunk, out.worldX, out.worldY, out.note)) return out;
    int frameIndex = -1;
    forEachFrame(chunk, [&](const std::vector<uint8_t>& b) {
        forge::stbbake::ForegroundFrame fg;
        try { fg = forge::stbbake::parseForegroundFrame(b); } catch (const std::exception&) { return; }
        if (fg.layers.empty()) return;
        ++frameIndex;
        ++out.frames;
        if (!fg.hasWater) return;
        const auto& p = fg.waterPayload;
        if (p.size() < 20) { out.note = "water payload shorter than its header"; return; }
        WaterPatch w;
        w.frameIndex = frameIndex;
        auto i32 = [&](size_t at) { int32_t v; std::memcpy(&v, p.data() + at, 4); return v; };
        auto f32 = [&](size_t at) { float v; std::memcpy(&v, p.data() + at, 4); return v; };
        w.offsetX = i32(0); w.offsetY = i32(4); w.span = f32(8); w.waterType = i32(12);
        const int32_t len = i32(16);
        if (len < 0 || 20 + size_t(len) > p.size()) { out.note = "water block length out of range"; return; }
        w.block.assign(p.begin() + 20, p.begin() + 20 + len);
        try {
            const auto raw = forge::rangecodec::decode(w.block.data(), w.block.size(), kWaterRecordCount, kWaterRecordSize);
            w.records.reserve(kWaterRecordCount);
            for (size_t i = 0; i < kWaterRecordCount; ++i) w.records.push_back(unpackWaterRecord(raw.data() + i * kWaterRecordSize));
        } catch (const std::exception& e) {
            out.note = std::string("water block: ") + e.what();
            return;
        }
        out.patches.push_back(std::move(w));
    });
    out.found = out.frames > 0;
    if (!out.found && out.note.empty()) out.note = "no foreground frames matched";
    return out;
}

// ---- background water sub-patches ----

size_t trailerWaterFlagOffset(const std::vector<uint8_t>& trailer) {
    const size_t p = forge::stbbake::trailerWaterFlagOffset(trailer);
    return p == SIZE_MAX ? std::string::npos : p;
}

BackgroundWater loadBackgroundWater(const fs::path& gameRoot, const std::string& mapName) {
    BackgroundWater out;
    std::vector<uint8_t> chunk;
    if (!findChunk(gameRoot, mapName, chunk, out.worldX, out.worldY, out.note)) return out;
    int frameIndex = -1;
    forEachFrame(chunk, [&](const std::vector<uint8_t>& b) {
        ++frameIndex;
        forge::stbbake::PatchHeader h;
        try { h = forge::stbbake::parsePatchHeader(b); } catch (...) { return; }
        if (!h.valid || h.isWaterOnly) return;
        forge::stbbake::PatchBody pb;
        try { pb = forge::stbbake::parsePatchBody(b); } catch (...) { return; }
        if (!pb.valid || pb.waterOnly) return;
        ++out.patches;
        const size_t flag = trailerWaterFlagOffset(pb.trailer);
        if (flag == std::string::npos || pb.trailer[flag] == 0) return;
        BackgroundWaterPatch w;
        w.frameIndex = frameIndex; w.coordX = h.coord0; w.coordY = h.coord1; w.pw = h.pw; w.ph = h.ph;
        w.patchVertices = h.vertexCount; w.trailerWaterOffset = flag;
        w.header = h;
        try { w.meshVertices = forge::stbbake::decodePatchVertices(pb); w.meshTriangles = forge::stbbake::patchTriangles(pb, w.meshVertices); } catch (...) {}
        size_t p = flag + 1;
        auto need = [&](size_t n) { return p + n <= pb.trailer.size(); };
        auto u16 = [&]() { const uint16_t v = uint16_t(pb.trailer[p] | (pb.trailer[p + 1] << 8)); p += 2; return v; };
        auto i32 = [&]() { int32_t v; std::memcpy(&v, pb.trailer.data() + p, 4); p += 4; return v; };
        if (!need(16)) { out.note = "background water header truncated"; return; }
        const uint16_t vc = u16(), tc = u16();
        w.waterType = i32(); w.stride = i32();
        if (w.stride != 0x38 && w.stride != 0x0c) { out.note = "unexpected background water stride"; return; }
        const int32_t vlen = i32();
        if (vlen < 0 || !need(size_t(vlen))) { out.note = "background water VB truncated"; return; }
        try {
            const auto raw = forge::rangecodec::decode(pb.trailer.data() + p, size_t(vlen), vc, size_t(w.stride));
            p += size_t(vlen);
            for (size_t i = 0; i < vc; ++i) {
                const uint8_t* r = raw.data() + i * size_t(w.stride);
                BackgroundWaterVertex v;
                if (w.stride == 0x0c) { std::memcpy(&v.x, r, 4); std::memcpy(&v.y, r + 4, 4); std::memcpy(&v.z, r + 8, 4); }
                else {
                    v.x = float(uint16_t(r[0] | (r[1] << 8))); v.y = float(uint16_t(r[2] | (r[3] << 8)));
                    std::memcpy(&v.z, r + 4, 4);
                    for (int k = 0; k < 12; ++k) std::memcpy(&v.shore[k], r + 8 + k * 4, 4);
                }
                w.vertices.push_back(v);
            }
            if (tc) {
                if (!need(4)) { out.note = "background water IB truncated"; return; }
                const int32_t ilen = i32();
                if (ilen < 0 || !need(size_t(ilen))) { out.note = "background water IB truncated"; return; }
                const auto ib = forge::rangecodec::decode(pb.trailer.data() + p, size_t(ilen), size_t(tc) * 3, 2);
                p += size_t(ilen);
                for (size_t i = 0; i < size_t(tc) * 3; ++i) w.indices.push_back(uint16_t(ib[i * 2] | (ib[i * 2 + 1] << 8)));
            }
        } catch (const std::exception& e) { out.note = std::string("background water: ") + e.what(); return; }
        out.water.push_back(std::move(w));
    });
    out.found = out.patches > 0;
    return out;
}

} // namespace albion::stbterrain
