#include "terrainexport.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <atomic>
#include <mutex>
#include <thread>
#include <set>
#include <stdexcept>

#include "forge/big.hpp"
#include "forge/bin.hpp"
#include "forge/defdecode.hpp"
#include "forge/defschema.hpp"
#include "forge/terraintex.hpp"
#include "forge/wld.hpp"
#include "miniz/miniz.h"
#include "nlohmann/json.hpp"
#include "glbwriter.hpp"
#include "stbterrain.hpp"
#include "terrainexport_internal.hpp"

#include "../vendor/embedded_schema.hpp"

namespace albion::terrainexport {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

void say(const Options& o, Scene& s, const std::string& msg, bool warning) {
    if (warning) s.warnings.push_back(msg);
    if (o.log) o.log((warning ? "warning: " : "") + msg);
}

// Fable (x, y, z) -> output space.
void toUp(UpAxis up, float x, float y, float z, float& ox, float& oy, float& oz) {
    if (up == UpAxis::Y) { ox = x; oy = z; oz = -y; }
    else                 { ox = x; oy = y; oz = z; }
}

float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

float smoothstep(float a, float b, float x) {
    if (b <= a) return x >= b ? 1.0f : 0.0f;
    const float t = clamp01((x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}

// Bilinear, wrapping sample of an RGBA8 image at (u, v) in texture repeats.
void sampleWrap(const Image& img, float u, float v, float out[4]) {
    const float fx = (u - std::floor(u)) * float(img.width);
    const float fy = (v - std::floor(v)) * float(img.height);
    const int x0 = int(fx) % int(img.width), y0 = int(fy) % int(img.height);
    const int x1 = (x0 + 1) % int(img.width), y1 = (y0 + 1) % int(img.height);
    const float tx = fx - std::floor(fx), ty = fy - std::floor(fy);
    auto px = [&](int x, int y) { return &img.rgba[(size_t(y) * img.width + size_t(x)) * 4]; };
    const uint8_t *a = px(x0, y0), *b = px(x1, y0), *c = px(x0, y1), *d = px(x1, y1);
    for (int i = 0; i < 4; ++i) {
        const float top = a[i] + (b[i] - a[i]) * tx;
        const float bot = c[i] + (d[i] - c[i]) * tx;
        out[i] = top + (bot - top) * ty;
    }
}

Image rgbaImage(uint32_t w, uint32_t h, std::vector<uint8_t> rgba, std::string name) {
    Image img;
    img.width = w; img.height = h; img.rgba = std::move(rgba); img.name = std::move(name);
    return img;
}

} // namespace

// ----------------------------------------------------------------- world

namespace {
std::string lowerStr(std::string s) { for (char& c : s) c = char(std::tolower((unsigned char)c)); return s; }
std::string stemOf(const std::string& levelName) {
    std::string s = levelName;
    const size_t slash = s.find_last_of("/\\");
    if (slash != std::string::npos) s = s.substr(slash + 1);
    const size_t dot = s.rfind('.');
    if (dot != std::string::npos) s = s.substr(0, dot);
    return s;
}
} // namespace

RegionIndex loadRegionIndex(const fs::path& gameRoot) {
    RegionIndex idx;
    const fs::path wldPath = gameRoot / "data" / "Levels" / "FinalAlbion.wld";
    if (!fs::exists(wldPath)) return idx;
    try {
        const auto wld = forge::wld::File::parse(wldPath);
        for (const auto& m : wld.maps()) idx.originOfMap[lowerStr(stemOf(m.levelName))] = {float(m.mapX), float(m.mapY)};
        for (const auto& r : wld.regions())
            for (const auto& lvl : r.containsMaps) {
                const std::string stem = stemOf(lvl);
                idx.regionOfMap[lowerStr(stem)] = r.regionName;
                idx.mapsOfRegion[r.regionName].push_back(stem);
            }
        idx.loaded = true;
    } catch (...) {}
    return idx;
}

bool worldOrigin(const fs::path& gameRoot, const std::string& mapName, float& x, float& y) {
    const auto idx = loadRegionIndex(gameRoot);
    auto it = idx.originOfMap.find(lowerStr(mapName));
    if (it == idx.originOfMap.end()) return false;
    x = it->second.first; y = it->second.second;
    return true;
}

// ----------------------------------------------------------------- geometry

Scene buildMesh(const forge::lev::File& level, const Options& options) {
    Scene scene;
    scene.sourceName = level.source();
    scene.mapWidth = level.width();
    scene.mapHeight = level.height();
    scene.uid = level.uid();
    scene.up = options.up;
    scene.walkableColor = options.walkableColor;
    scene.layers = options.layers;

    const int cx = level.cellsX(), cy = level.cellsY();
    scene.vertices.resize(size_t(cx) * size_t(cy));
    scene.minHeight = 1e30f; scene.maxHeight = -1e30f;

    auto h = [&](int x, int y) {
        x = std::clamp(x, 0, cx - 1); y = std::clamp(y, 0, cy - 1);
        return level.heightAt(x, y);
    };
    for (int y = 0; y < cy; ++y) {
        for (int x = 0; x < cx; ++x) {
            Vertex& v = scene.vertices[size_t(y) * cx + x];
            const float z = h(x, y);
            scene.minHeight = std::min(scene.minHeight, z);
            scene.maxHeight = std::max(scene.maxHeight, z);
            toUp(options.up, options.originX + float(x), options.originY + float(y), z,
                 v.px, v.py, v.pz);
            // Central differences on the 1-unit grid (one-sided at the border).
            const float dx0 = x > 0 ? 1.0f : 0.0f, dx1 = x < cx - 1 ? 1.0f : 0.0f;
            const float dy0 = y > 0 ? 1.0f : 0.0f, dy1 = y < cy - 1 ? 1.0f : 0.0f;
            const float dzdx = (h(x + 1, y) - h(x - 1, y)) / std::max(dx0 + dx1, 1.0f);
            const float dzdy = (h(x, y + 1) - h(x, y - 1)) / std::max(dy0 + dy1, 1.0f);
            float nx = -dzdx, ny = -dzdy, nz = 1.0f;
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            nx /= len; ny /= len; nz /= len;
            toUp(options.up, nx, ny, nz, v.nx, v.ny, v.nz);
            v.u = float(x) / float(std::max(scene.mapWidth, 1));
            v.v = float(y) / float(std::max(scene.mapHeight, 1));
            v.walkable = level.walkableAt(x, y);
            for (int i = 0; i < 3; ++i) {
                v.themeIndex[i] = level.themeIndexAt(x, y, i);
                v.themeWeight[i] = level.themeStrengthAt(x, y, i);
            }
        }
    }
    if (scene.vertices.empty()) { scene.minHeight = scene.maxHeight = 0; }

    scene.indices.reserve(size_t(scene.mapWidth) * scene.mapHeight * 6);
    const bool masked = options.cellMask && options.cellMask->size() == size_t(scene.mapWidth) * size_t(scene.mapHeight);
    for (int y = 0; y < scene.mapHeight; ++y) {
        for (int x = 0; x < scene.mapWidth; ++x) {
            if (masked && !(*options.cellMask)[size_t(y) * scene.mapWidth + x]) { ++scene.hiddenCells; continue; }
            const uint32_t a = uint32_t(y * cx + x), b = a + 1;
            const uint32_t c = a + uint32_t(cx), d = c + 1;
            // CCW seen from +Z in Fable space; the Y-up mapping is a proper
            // rotation so winding survives.
            scene.indices.insert(scene.indices.end(), {a, b, d, a, d, c});
        }
    }
    return scene;
}

// ----------------------------------------------------------------- textures

std::vector<uint8_t> bgra8ToRgba(const uint8_t* p, uint32_t w, uint32_t h) {
    std::vector<uint8_t> out(size_t(w) * h * 4);
    for (size_t i = 0; i < size_t(w) * h; ++i) {
        out[i * 4 + 0] = p[i * 4 + 2];
        out[i * 4 + 1] = p[i * 4 + 1];
        out[i * 4 + 2] = p[i * 4 + 0];
        out[i * 4 + 3] = p[i * 4 + 3];
    }
    return out;
}

namespace {

void rgb565(uint16_t c, uint8_t out[3]) {
    const uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    out[0] = uint8_t((r * 255 + 15) / 31);
    out[1] = uint8_t((g * 255 + 31) / 63);
    out[2] = uint8_t((b * 255 + 15) / 31);
}

// Decode one 8-byte BC1 colour block into 16 RGBA pixels. `opaque` forces
// alpha 255 (BC2/BC3 colour blocks are always 4-colour mode).
void bc1Block(const uint8_t* b, uint8_t px[16][4], bool opaque) {
    const uint16_t c0 = uint16_t(b[0] | (b[1] << 8)), c1 = uint16_t(b[2] | (b[3] << 8));
    uint8_t pal[4][4];
    rgb565(c0, pal[0]); rgb565(c1, pal[1]);
    pal[0][3] = pal[1][3] = 255;
    if (c0 > c1 || opaque) {
        for (int i = 0; i < 3; ++i) {
            pal[2][i] = uint8_t((2 * pal[0][i] + pal[1][i] + 1) / 3);
            pal[3][i] = uint8_t((pal[0][i] + 2 * pal[1][i] + 1) / 3);
        }
        pal[2][3] = pal[3][3] = 255;
    } else {
        for (int i = 0; i < 3; ++i) pal[2][i] = uint8_t((pal[0][i] + pal[1][i]) / 2);
        pal[2][3] = 255;
        pal[3][0] = pal[3][1] = pal[3][2] = pal[3][3] = 0;
    }
    const uint32_t bits = uint32_t(b[4]) | (uint32_t(b[5]) << 8) |
                          (uint32_t(b[6]) << 16) | (uint32_t(b[7]) << 24);
    for (int i = 0; i < 16; ++i) {
        const uint32_t idx = (bits >> (2 * i)) & 3;
        std::memcpy(px[i], pal[idx], 4);
    }
}

void putBlock(std::vector<uint8_t>& out, uint32_t w, uint32_t h, uint32_t bx, uint32_t by,
              const uint8_t px[16][4]) {
    for (uint32_t j = 0; j < 4; ++j) {
        const uint32_t y = by * 4 + j;
        if (y >= h) break;
        for (uint32_t i = 0; i < 4; ++i) {
            const uint32_t x = bx * 4 + i;
            if (x >= w) break;
            std::memcpy(&out[(size_t(y) * w + x) * 4], px[j * 4 + i], 4);
        }
    }
}

} // namespace

std::vector<uint8_t> decodeBc1ToRgba(const uint8_t* blocks, uint32_t w, uint32_t h) {
    std::vector<uint8_t> out(size_t(w) * h * 4, 0);
    const uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4;
    uint8_t px[16][4];
    for (uint32_t by = 0; by < bh; ++by)
        for (uint32_t bx = 0; bx < bw; ++bx) {
            bc1Block(blocks + (size_t(by) * bw + bx) * 8, px, false);
            putBlock(out, w, h, bx, by, px);
        }
    return out;
}

std::vector<uint8_t> decodeBc2ToRgba(const uint8_t* blocks, uint32_t w, uint32_t h) {
    std::vector<uint8_t> out(size_t(w) * h * 4, 0);
    const uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4;
    uint8_t px[16][4];
    for (uint32_t by = 0; by < bh; ++by)
        for (uint32_t bx = 0; bx < bw; ++bx) {
            const uint8_t* b = blocks + (size_t(by) * bw + bx) * 16;
            bc1Block(b + 8, px, true);
            for (int i = 0; i < 16; ++i) {
                const uint32_t nib = (b[i / 2] >> ((i & 1) * 4)) & 0xF;
                px[i][3] = uint8_t(nib * 17);
            }
            putBlock(out, w, h, bx, by, px);
        }
    return out;
}

namespace {
// Encoded PNGs are cached for the process by content hash: a batch export meets
// the same 512x512 wall texture on every map, and encoding is the single most
// expensive step of a textured export (~4 s of an 8 s map).
uint64_t imageHash(const Image& image) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ull; };
    mix(image.width); mix(image.height);
    const uint8_t* p = image.rgba.data();
    const size_t n = image.rgba.size();
    // every byte for small images, a strided sample plus the tail for large ones
    const size_t step = n > (1u << 20) ? 7 : 1;
    for (size_t i = 0; i < n; i += step) mix(p[i]);
    for (size_t i = n > 64 ? n - 64 : 0; i < n; ++i) mix(p[i]);
    return h;
}
std::mutex g_pngMutex;
std::map<uint64_t, std::vector<uint8_t>> g_pngCache;

// PNG encoder with zlib level 9 (miniz's convenience writer stops at 6) and an
// RGB path for opaque images. Row filters are implemented but off: see below.
std::vector<uint8_t> encodePngUncached(const Image& image) {
    // Opaque images go out as RGB: a quarter smaller and no alpha to sample.
    bool opaque = true;
    for (size_t i = 3; i < image.rgba.size(); i += 4) if (image.rgba[i] != 255) { opaque = false; break; }
    const int ch = opaque ? 3 : 4;
    const size_t W = image.width, H = image.height, stride = W * size_t(ch);
    std::vector<uint8_t> raw(H * (stride + 1));
    std::vector<uint8_t> row(stride), prev(stride, 0), cand(stride);
    for (size_t y = 0; y < H; ++y) {
        const uint8_t* src = image.rgba.data() + y * W * 4;
        if (ch == 4) std::memcpy(row.data(), src, stride);
        else for (size_t x = 0; x < W; ++x) { row[x * 3] = src[x * 4]; row[x * 3 + 1] = src[x * 4 + 1]; row[x * 3 + 2] = src[x * 4 + 2]; }
        int bestF = 0; uint64_t bestSum = ~0ull;
        std::vector<uint8_t> best(stride);
        // DXT-decoded textures are 4x4 blocks of few colours: plain LZ on the raw
        // rows beats every predictor on them (measured: filtered +20%), so only
        // filter type 0 is tried. Kept as a loop so other sources can opt in.
        for (int f = 0; f < 1; ++f) {
            uint64_t sum = 0;
            for (size_t i = 0; i < stride; ++i) {
                const int a = i >= size_t(ch) ? row[i - ch] : 0, b = prev[i], c = i >= size_t(ch) ? prev[i - ch] : 0;
                int pred = 0;
                switch (f) {
                    case 1: pred = a; break;
                    case 2: pred = b; break;
                    case 3: pred = (a + b) / 2; break;
                    case 4: { const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c); pred = (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c); break; }
                    default: break;
                }
                const uint8_t v = uint8_t(row[i] - pred);
                cand[i] = v;
                sum += v < 128 ? v : 256 - v;
                if (sum >= bestSum) break;
            }
            if (sum < bestSum) { bestSum = sum; bestF = f; best.swap(cand); cand.resize(stride); }
        }
        raw[y * (stride + 1)] = uint8_t(bestF);
        std::memcpy(raw.data() + y * (stride + 1) + 1, best.data(), stride);
        prev.swap(row); row.resize(stride);
    }
    mz_ulong zlen = mz_compressBound(mz_ulong(raw.size()));
    std::vector<uint8_t> z(zlen);
    if (mz_compress2(z.data(), &zlen, raw.data(), mz_ulong(raw.size()), 9) != MZ_OK) throw std::runtime_error("PNG deflate failed for " + image.name);
    z.resize(zlen);
    std::vector<uint8_t> out;
    out.reserve(zlen + 64);
    auto be32 = [&](uint32_t v) { out.push_back(uint8_t(v >> 24)); out.push_back(uint8_t(v >> 16)); out.push_back(uint8_t(v >> 8)); out.push_back(uint8_t(v)); };
    auto chunk = [&](const char* type, const uint8_t* data, size_t n) {
        be32(uint32_t(n));
        const size_t start = out.size();
        out.insert(out.end(), type, type + 4);
        out.insert(out.end(), data, data + n);
        be32(uint32_t(mz_crc32(MZ_CRC32_INIT, out.data() + start, n + 4)));
    };
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    out.insert(out.end(), sig, sig + 8);
    uint8_t ihdr[13];
    ihdr[0] = uint8_t(W >> 24); ihdr[1] = uint8_t(W >> 16); ihdr[2] = uint8_t(W >> 8); ihdr[3] = uint8_t(W);
    ihdr[4] = uint8_t(H >> 24); ihdr[5] = uint8_t(H >> 16); ihdr[6] = uint8_t(H >> 8); ihdr[7] = uint8_t(H);
    ihdr[8] = 8; ihdr[9] = ch == 3 ? 2 : 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    chunk("IHDR", ihdr, 13);
    chunk("IDAT", z.data(), z.size());
    chunk("IEND", nullptr, 0);
    return out;
}
} // namespace

std::vector<uint8_t> encodePng(const Image& image) {
    const uint64_t key = imageHash(image);
    {
        std::lock_guard<std::mutex> lock(g_pngMutex);
        auto hit = g_pngCache.find(key);
        if (hit != g_pngCache.end()) return hit->second;
    }
    auto out = encodePngUncached(image);
    std::lock_guard<std::mutex> lock(g_pngMutex);
    if (g_pngCache.size() > 4000) g_pngCache.clear();   // bounded; a full batch stays well under this
    g_pngCache[key] = out;
    return out;
}

void prewarmPng(const std::vector<const Image*>& images) {
    // Encode in parallel on the hardware threads; encodePng's cache makes the
    // later serial pass a lookup.
    std::vector<const Image*> todo;
    {
        std::lock_guard<std::mutex> lock(g_pngMutex);
        for (const Image* im : images) if (im && !g_pngCache.count(imageHash(*im))) todo.push_back(im);
    }
    if (todo.size() < 2) { for (const Image* im : todo) encodePng(*im); return; }
    const unsigned workers = std::max(2u, std::min(std::thread::hardware_concurrency(), 8u));
    std::atomic<size_t> next{0};
    std::vector<std::thread> pool;
    for (unsigned w = 0; w < workers; ++w)
        pool.emplace_back([&]() {
            for (size_t i = next++; i < todo.size(); i = next++) { try { encodePng(*todo[i]); } catch (...) {} }
        });
    for (auto& t : pool) t.join();
}

namespace {

struct TextureCache {
    const forge::big::File* big = nullptr;
    std::map<uint32_t, const forge::big::Entry*> byId;
    std::map<uint32_t, Image> decoded;
    std::set<uint32_t> failed;
    std::mutex mutex;

    const Image* get(uint32_t id, Scene& scene, const Options& o) {
        return get(id, [&](const std::string& m) { say(o, scene, m, true); });
    }

    const Image* get(uint32_t id, const std::function<void(const std::string&)>& warn) {
        if (id == 0) return nullptr;
        std::lock_guard<std::mutex> lock(mutex);
        auto hit = decoded.find(id);
        if (hit != decoded.end()) return &hit->second;
        if (failed.count(id)) return nullptr;
        auto e = byId.find(id);
        if (e == byId.end()) {
            failed.insert(id);
            warn("texture id " + std::to_string(id) + " is not in GBANK_MAIN_PC");
            return nullptr;
        }
        const auto mip = forge::terraintex::decodeMip0(e->second->subHeader, big->entryData(*e->second));
        if (!mip.ok) {
            failed.insert(id);
            warn("texture " + e->second->name + " (" + std::to_string(id) + "): " + mip.error);
            return nullptr;
        }
        std::vector<uint8_t> rgba;
        using F = forge::terraintex::PreviewFormat;
        if (mip.format == F::BC1) rgba = decodeBc1ToRgba(mip.bytes.data(), mip.width, mip.height);
        else if (mip.format == F::BC2) rgba = decodeBc2ToRgba(mip.bytes.data(), mip.width, mip.height);
        else rgba = bgra8ToRgba(mip.bytes.data(), mip.width, mip.height);
        auto& img = decoded[id] = rgbaImage(mip.width, mip.height, std::move(rgba), e->second->name);
        return &img;
    }
};

} // namespace

struct Context::Impl {
    bool ready = false;
    fs::path gameRoot;
    forge::terraintex::ThemeLibrary library;
    forge::big::File big;
    TextureCache cache;
    std::unique_ptr<forge::bin::File> defs;
    std::unique_ptr<forge::defschema::Schema> schema;
    std::mutex defMutex;
    std::map<std::string, std::pair<int, uint32_t>> modelCache;
};

int Context::graphicModelId(const std::string& name, uint32_t& modelId) const {
    modelId = 0;
    if (!ready() || !impl_->defs || !impl_->schema) return 0;
    std::lock_guard<std::mutex> lock(impl_->defMutex);
    auto hit = impl_->modelCache.find(name);
    if (hit != impl_->modelCache.end()) { modelId = hit->second.second; return hit->second.first; }
    int code = 0;
    if (const auto* entry = impl_->defs->find(name)) {
        code = -1;
        if (const auto* type = forge::defdecode::resolveType(*impl_->schema, entry->definition, entry->data)) {
            const auto decoded = forge::defdecode::decode(entry->data, *type);
            code = 1;
            for (const auto& f : decoded.fields)
                if (f.name == "Graphic" && f.value.size() >= 8) { std::memcpy(&modelId, f.value.data() + 4, 4); break; }
        }
    }
    impl_->modelCache[name] = {code, modelId};
    return code;
}

std::vector<std::pair<std::string, std::string>> Context::definitions(const std::vector<std::string>& types) const {
    std::vector<std::pair<std::string, std::string>> out;
    if (!ready() || !impl_->defs) return out;
    for (const auto& e : impl_->defs->entries()) {
        if (e.name.empty()) continue;
        for (const auto& t : types)
            if (e.definition == t) { out.emplace_back(e.name, e.definition); break; }
    }
    std::sort(out.begin(), out.end());
    return out;
}

const forge::terraintex::ThemeLibrary* Context::themeLibrary() const {
    return ready() ? &impl_->library : nullptr;
}

Context::Context() : impl_(std::make_shared<Impl>()) {}
bool Context::ready() const { return impl_ && impl_->ready; }
fs::path Context::gameRoot() const { return impl_ ? impl_->gameRoot : fs::path(); }

const Image* Context::texture(uint32_t id, std::string& warning) const {
    if (!ready()) return nullptr;
    return impl_->cache.get(id, [&](const std::string& m) { warning = m; });
}

bool Context::load(const fs::path& gameRoot, const fs::path& texturesBig, std::string& error) {
    auto impl = std::make_shared<Impl>();
    try {
        const fs::path defsDir = gameRoot / "data" / "CompiledDefs";
        impl->defs = std::make_unique<forge::bin::File>(forge::bin::File::open(defsDir / "names.bin", defsDir / "game.bin"));
        impl->schema = std::make_unique<forge::defschema::Schema>(forge::defschema::Schema::loadText(kEmbeddedDefSchema, "embedded"));
        impl->library = forge::terraintex::ThemeLibrary::load(*impl->defs, *impl->schema);
    } catch (const std::exception& e) {
        error = std::string("cannot load ENGINE_THEME defs: ") + e.what();
        return false;
    }
    try {
        impl->big = forge::big::File::open(texturesBig);
        const auto* bank = impl->big.findBank("GBANK_MAIN_PC");
        if (!bank) throw std::runtime_error("no GBANK_MAIN_PC bank in " + texturesBig.string());
        impl->cache.big = &impl->big;
        for (const auto& e : bank->entries) impl->cache.byId[e.id] = &e;
    } catch (const std::exception& e) {
        error = std::string("cannot open textures.big: ") + e.what();
        return false;
    }
    impl->gameRoot = gameRoot;
    impl->ready = true;
    impl_ = std::move(impl);
    return true;
}

// Water surface, the way the engine builds a water patch (debug build):
//   CEngineMap::PeekWaterDepth      0x02d5e000  depth = sum(slot blend/255 * theme.WaterHeight)
//   CEngineMap::PeekHasWaterFast    0x02d5d620  any slot's theme has WaterType != 0
//   CEngineMap::PeekInterpolatedWaterHeight 0x02d5db50  mean over the +-2 window of
//       (ground + depth) for cells with depth > 0.001; a dry vertex re-centres the
//       window on the first wet cell it finds within +-2 (the sheet reaches the bank)
//   CWaterPatchMesh::Build / BuildVertexBuffer 0x02e689b0 / 0x02e68320  vertex z =
//       level - 0.1; per-vertex fade = clamp(level + 0.1 - ground, 0, 2) / 2
// So WaterHeight is a DEPTH above the ground and the drawn sheet is the smoothed
// absolute surface. Where the smoothed sheet dips under the ground it is hidden;
// where it is less than 2 units deep the in-game shader fades it out, which is
// what makes shores read as shores. The fade is exported as COLOR_0 alpha.
void buildWater(const forge::lev::File& level, Scene& scene, const std::map<int, size_t>& slotToLayer, const Options& options) {
    const int cx = level.cellsX(), cy = level.cellsY();
    if (cx <= 1 || cy <= 1) return;
    std::vector<float> depth(size_t(cx) * cy, 0.0f);
    std::vector<uint8_t> ice(size_t(cx) * cy, 0);     // dominant water slot is EWaterType 8 (ice)
    int wet = 0;
    for (int y = 0; y < cy; ++y)
        for (int x = 0; x < cx; ++x) {
            const Vertex& v = scene.vertices[size_t(y) * cx + x];
            float d = 0.0f;
            bool hasWater = false;
            int bestW = 0, bestType = 0;
            for (int s = 0; s < 3; ++s) {
                auto it = slotToLayer.find(v.themeIndex[s]);
                if (it == slotToLayer.end()) continue;
                const ThemeLayer& L = scene.themes[it->second];
                if (L.waterType != 0) {
                    hasWater = true;
                    if (v.themeWeight[s] > bestW) { bestW = v.themeWeight[s]; bestType = L.waterType; }
                }
                d += float(v.themeWeight[s]) / 255.0f * L.waterHeight;
            }
            if (hasWater && d > 0.001f) { depth[size_t(y) * cx + x] = d; ice[size_t(y) * cx + x] = bestType == 8 ? 1 : 0; ++wet; }
        }
    if (wet == 0) return;
    auto ground = [&](int x, int y) { return level.heightAt(x, y); };
    // PeekInterpolatedWaterHeight(x, y, 2).
    std::vector<float> lvl(depth.size(), 0.0f);
    std::vector<uint8_t> cellIce(depth.size(), 0);
    for (int y = 0; y < cy; ++y)
        for (int x = 0; x < cx; ++x) {
            int cxm = x, cym = y;
            if (depth[size_t(y) * cx + x] <= 0.0f) {
                bool found = false;
                for (int j = std::max(0, y - 2); j <= std::min(cy - 1, y + 2) && !found; ++j)
                    for (int i = std::max(0, x - 2); i <= std::min(cx - 1, x + 2) && !found; ++i)
                        if (depth[size_t(j) * cx + i] > 0.0f) { cxm = i; cym = j; found = true; }
                if (!found) continue;
            }
            float sum = 0.0f; int n = 0, iceN = 0;
            for (int j = std::max(0, cym - 2); j <= std::min(cy - 1, cym + 2); ++j)
                for (int i = std::max(0, cxm - 2); i <= std::min(cx - 1, cxm + 2); ++i)
                    if (depth[size_t(j) * cx + i] > 0.001f) { sum += ground(i, j) + depth[size_t(j) * cx + i]; ++n; iceN += ice[size_t(j) * cx + i]; }
            if (n) { lvl[size_t(y) * cx + x] = sum / float(n); cellIce[size_t(y) * cx + x] = iceN * 2 > n; }
        }
    WaterMesh& w = scene.water;
    w.wetVertices = wet;
    std::vector<int> vidx(depth.size(), -1);
    auto emit = [&](int x, int y) {
        int& id = vidx[size_t(y) * cx + x];
        if (id >= 0) return uint32_t(id);
        const float L = lvl[size_t(y) * cx + x];
        float px, py, pz;
        toUp(options.up, options.originX + float(x), options.originY + float(y), L - 0.1f, px, py, pz);
        w.positions.insert(w.positions.end(), {px, py, pz});
        w.ice.push_back(cellIce[size_t(y) * cx + x]);
        w.fade.push_back(std::clamp((L + 0.1f - ground(x, y)) / 2.0f, 0.0f, 1.0f));
        id = int(w.positions.size() / 3 - 1);
        return uint32_t(id);
    };
    for (int y = 0; y + 1 < cy; ++y)
        for (int x = 0; x + 1 < cx; ++x) {
            const int c[4] = {y * cx + x, y * cx + x + 1, (y + 1) * cx + x, (y + 1) * cx + x + 1};
            const int xs[4] = {x, x + 1, x, x + 1}, ys[4] = {y, y, y + 1, y + 1};
            bool all = true, visible = false;
            for (int k = 0; k < 4; ++k) {
                const float L = lvl[size_t(c[k])];
                all = all && L > 0.0f;
                visible = visible || (L > 0.0f && L - 0.1f > ground(xs[k], ys[k]));   // the vertex itself pokes out of the ground
            }
            if (!all || !visible) continue;
            int iceN = 0;
            for (int k : c) iceN += cellIce[size_t(k)];
            const uint32_t a = emit(x, y), b = emit(x + 1, y), cc = emit(x, y + 1), d = emit(x + 1, y + 1);
            auto& tri = iceN >= 2 ? w.iceIndices : w.indices;
            tri.insert(tri.end(), {a, cc, b, b, cc, d});
        }
}

Scene buildScene(const forge::lev::File& level, const Options& options, const Context* context) {
    Scene scene = buildMesh(level, options);
    if (!options.textures) return scene;

    Context local;
    if (!context || !context->ready()) {
        std::string error;
        if (!local.load(options.gameRoot, options.texturesBig, error)) {
            say(options, scene, error + " -- exporting untextured", true);
            return scene;
        }
        context = &local;
    }
    Context::Impl& ctx = context->impl();
    TextureCache& cache = ctx.cache;
    const auto rows = forge::terraintex::resolvePalette(level, ctx.library);

    // 3. Per-slot layers.
    std::map<int, size_t> slotToLayer;
    for (const auto& r : rows) {
        ThemeLayer layer;
        layer.slot = r.slot;
        layer.name = r.paletteName.empty() ? r.defName : r.paletteName;
        layer.defIndex = r.defIndex;
        layer.resolved = r.resolved;
        layer.vertexReferences = r.cellCount;
        // The LEV stores a GLOBAL def index that goes stale when game.bin changes
        // (maps authored against an older bank); the palette also stores the name
        // and names are stable. Retail LEVs are stale: Bowerstone Bridge's slot
        // "WATER_BWLAKE_8" points at index 1934, which is WATER_BWLAKE_1 in the
        // shipped game.bin (and "WATER_BWLAKE_0" at 1929 is Hook Coast ICE). So
        // when the name at the stored index disagrees with the palette name and the
        // palette name exists, the NAME wins.
        const auto* byName = r.paletteName.empty() ? nullptr : ctx.library.byName(r.paletteName);
        if (byName && byName->decoded && (!r.resolved || !r.nameMatches)) {
            if (r.resolved) ++scene.nameResolvedThemes;
            layer.resolved = true;
            layer.defIndex = byName->defIndex;
            layer.baseTexture = byName->textures.base[0];
            layer.cliffTexture = byName->textures.cliff[0];
            layer.waterHeight = byName->waterHeight;
            layer.waterType = byName->waterType;
            if (!r.resolved) ++scene.nameResolvedThemes;
        } else if (r.resolved) {
            layer.baseTexture = r.textures.base[0];
            layer.cliffTexture = r.textures.cliff[0];
            if (const auto* t = ctx.library.byDefIndex(r.defIndex)) { layer.waterHeight = t->waterHeight; layer.waterType = t->waterType; }
        } else {
            ++scene.unresolvedThemes;
            say(options, scene, "palette slot " + std::to_string(r.slot) + " (" + layer.name +
                ") is not an ENGINE_THEME in this install (stale def index, no theme of that name)", true);
        }
        slotToLayer[r.slot] = scene.themes.size();
        scene.themes.push_back(layer);
    }
    if (options.log) options.log(std::to_string(scene.themes.size()) + " ground themes in use");
    if (options.water) buildWater(level, scene, slotToLayer, options);
    if (options.log && !scene.water.empty())
        options.log("water: " + std::to_string(scene.water.wetVertices) + " wet vertices, " +
                    std::to_string(scene.water.indices.size() / 3) + " triangles");

    // Decode every referenced texture once.
    std::map<uint32_t, const Image*> imgs;
    for (auto& layer : scene.themes) {
        if (!layer.resolved) continue;
        for (uint32_t id : {layer.baseTexture, layer.cliffTexture})
            if (id && !imgs.count(id)) imgs[id] = cache.get(id, scene, options);
    }
    if (options.layers) {
        for (auto& layer : scene.themes) {
            auto add = [&](uint32_t id, const char* kind) -> int {
                const Image* src = id ? imgs[id] : nullptr;
                if (!src) return -1;
                Image copy = *src;
                copy.name = std::to_string(layer.slot) + "_" + layer.name + "_" + kind;
                scene.layerImages.push_back(std::move(copy));
                return int(scene.layerImages.size() - 1);
            };
            layer.baseImage = add(layer.baseTexture, "base");
            layer.cliffImage = add(layer.cliffTexture, "cliff");
        }
    }

    // 4a. The engine's own bake: STB foreground passes. Each 16x16 patch lists its
    // texture passes; a pass has a mapping direction (0 flat: u = x/8, v = y/8;
    // 1..4: u = -x, +x, +y, -y over 8, v = -z/8), a texture id and the vertices it
    // covers with a per-vertex blend byte. The engine draws every pass over the
    // low-res background patch with alpha = blend * GetMappingDirectionBlend(dir, n)
    // (FableWin 0x02cae000: flatness t = clamp((asin(n.z)/(pi/2) - 0.5) / 0.25);
    // dir 0 -> t; dir d -> (1 - t) * clamp(1 - (acos(dot(n.xy, D_d)) / (pi/2) - 0.25) / 0.5)
    // with D_1..4 = (0,-1), (0,1), (-1,0), (1,0)). We composite the same passes as a
    // normalised weighted sum, per texel, so the cliff projections land exactly
    // where the engine puts them.
    if (options.engineLayers && !options.mapName.empty()) {
        const auto fl = stbterrain::loadLayers(options.gameRoot, options.mapName, scene.mapWidth, scene.mapHeight);
        if (fl.found && !fl.layers.empty()) {
            const int tpc = std::max(options.texelsPerCell, 1);
            const int cx = level.cellsX(), cy = level.cellsY();
            const uint32_t W = uint32_t(scene.mapWidth) * tpc, H = uint32_t(scene.mapHeight) * tpc;
            if (W == 0 || H == 0) return scene;
            struct Pass { uint32_t tex; uint8_t dir; float alpha; };
            std::vector<std::vector<Pass>> passes(size_t(cx) * cy);
            std::map<uint32_t, const Image*> layerImgs;
            int passCount = 0;
            for (const auto& L : fl.layers) {
                if (L.texture == 0) continue;
                if (!layerImgs.count(L.texture)) layerImgs[L.texture] = cache.get(L.texture, scene, options);
                ++passCount;
                for (const auto& v : L.vertices) {
                    if (v.x < 0 || v.y < 0 || v.x >= cx || v.y >= cy || v.blend == 0) continue;
                    // Fable-space normal of the grid vertex (buildMesh stored it in the
                    // requested up-axis space; undo that here).
                    const Vertex& gv = scene.vertices[size_t(v.y) * cx + v.x];
                    float nx = gv.nx, ny = gv.ny, nz = gv.nz;
                    if (options.up == UpAxis::Y) { const float fy_ = -gv.nz, fz_ = gv.ny; ny = fy_; nz = fz_; }
                    const float t = std::clamp((std::asin(std::clamp(nz, -1.0f, 1.0f)) / 1.5707963f - 0.5f) / 0.25f, 0.0f, 1.0f);
                    float w;
                    if (L.direction == 0) w = t;
                    else if (t >= 1.0f) w = 0.0f;
                    else {
                        static const float D[5][2] = {{0, 0}, {0, -1}, {0, 1}, {-1, 0}, {1, 0}};
                        const float len = std::sqrt(nx * nx + ny * ny);
                        float s = 1.0f;
                        if (len > 1e-6f) {
                            const float c = std::clamp((nx * D[L.direction][0] + ny * D[L.direction][1]) / len, -1.0f, 1.0f);
                            s = std::clamp(1.0f - (std::acos(c) / 1.5707963f - 0.25f) / 0.5f, 0.0f, 1.0f);
                        }
                        w = (1.0f - t) * s;
                    }
                    const float alpha = w * float(v.blend) / 255.0f;
                    if (alpha <= 0.0f) continue;
                    auto& list = passes[size_t(v.y) * cx + v.x];
                    bool merged = false;
                    for (auto& pss : list) if (pss.tex == L.texture && pss.dir == L.direction) { pss.alpha = std::max(pss.alpha, alpha); merged = true; break; }
                    if (!merged) list.push_back({L.texture, L.direction, alpha});
                }
            }
            if (options.log) options.log("baking " + std::to_string(W) + "x" + std::to_string(H) + " albedo from " + std::to_string(passCount) +
                                         " engine texture passes in " + std::to_string(fl.frames) + " patches");
            scene.albedo = rgbaImage(W, H, std::vector<uint8_t>(size_t(W) * H * 4, 255), "albedo");
            const float tile = options.tileSize > 0 ? options.tileSize : 8.0f;
            const float gain = options.gain > 0 ? options.gain : 1.0f;
            // Fallback colour where no pass covers a texel: the engine's background bake.
            const auto bg = stbterrain::backgroundAlbedo(options.gameRoot, options.mapName, scene.mapWidth, scene.mapHeight);
            int uncovered = 0;
            for (uint32_t py = 0; py < H; ++py) {
                const float wy = (float(py) + 0.5f) / float(tpc);
                const int iy = std::min(int(wy), scene.mapHeight - 1);
                const float fy = wy - float(iy);
                for (uint32_t px = 0; px < W; ++px) {
                    const float wx = (float(px) + 0.5f) / float(tpc);
                    const int ix = std::min(int(wx), scene.mapWidth - 1);
                    const float fx = wx - float(ix);
                    const int c[4] = {iy * cx + ix, iy * cx + ix + 1, (iy + 1) * cx + ix, (iy + 1) * cx + ix + 1};
                    const float bw[4] = {(1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy};
                    const float h00 = level.heightAt(ix, iy), h10 = level.heightAt(ix + 1, iy);
                    const float h01 = level.heightAt(ix, iy + 1), h11 = level.heightAt(ix + 1, iy + 1);
                    const float z = h00 + (h10 - h00) * fx + (h01 - h00) * fy + (h00 - h10 - h01 + h11) * fx * fy;
                    float rgb[3] = {0, 0, 0}, total = 0;
                    std::array<std::pair<uint64_t, float>, 24> acc{}; int n = 0;
                    for (int k = 0; k < 4; ++k)
                        for (const auto& pss : passes[size_t(c[k])]) {
                            const float wgt = bw[k] * pss.alpha;
                            if (wgt <= 0) continue;
                            const uint64_t key = (uint64_t(pss.tex) << 8) | pss.dir;
                            int j = 0;
                            for (; j < n; ++j) if (acc[size_t(j)].first == key) { acc[size_t(j)].second += wgt; break; }
                            if (j == n && n < int(acc.size())) acc[size_t(n++)] = {key, wgt};
                        }
                    for (int j = 0; j < n; ++j) {
                        const uint32_t tex = uint32_t(acc[size_t(j)].first >> 8);
                        const int dir = int(acc[size_t(j)].first & 0xff);
                        const Image* img = layerImgs[tex];
                        if (!img) continue;
                        float u, v;
                        switch (dir) {
                            case 1: u = -wx / tile; v = -z / tile; break;
                            case 2: u = wx / tile; v = -z / tile; break;
                            case 3: u = wy / tile; v = -z / tile; break;
                            case 4: u = -wy / tile; v = -z / tile; break;
                            default: u = wx / tile; v = wy / tile; break;
                        }
                        float sb[4]; sampleWrap(*img, u, v, sb);
                        for (int i = 0; i < 3; ++i) rgb[i] += sb[i] * acc[size_t(j)].second;
                        total += acc[size_t(j)].second;
                    }
                    uint8_t* out = &scene.albedo.rgba[(size_t(py) * W + px) * 4];
                    if (total > 0.02f) {
                        for (int i = 0; i < 3; ++i) out[i] = uint8_t(std::clamp(rgb[i] / total * gain + 0.5f, 0.0f, 255.0f));
                    } else if (bg.found && bg.image.width && bg.image.height) {
                        const uint32_t bx = std::min(uint32_t(wx * bg.texelsPerCell), bg.image.width - 1), by = std::min(uint32_t(wy * bg.texelsPerCell), bg.image.height - 1);
                        const uint8_t* src = &bg.image.rgba[(size_t(by) * bg.image.width + bx) * 4];
                        for (int i = 0; i < 3; ++i) out[i] = uint8_t(std::clamp(src[i] * gain + 0.5f, 0.0f, 255.0f));
                        ++uncovered;
                    } else { out[0] = out[1] = out[2] = 128; ++uncovered; }
                    out[3] = 255;
                }
            }
            if (options.log && uncovered) options.log("  " + std::to_string(uncovered) + " texels had no pass and took the background bake");
            scene.hasAlbedo = true;
            scene.engineBake = true;
            scene.enginePasses = passCount;
            return scene;
        }
        if (options.log) options.log("no STB foreground passes for this map (" + fl.note + "); baking from the LEV theme blend");
    }

    // 4b. Bake the albedo from the LEV theme blend (loose .lev / no STB).
    const int tpc = std::max(options.texelsPerCell, 1);
    const int cx = level.cellsX();
    const uint32_t W = uint32_t(scene.mapWidth) * tpc, H = uint32_t(scene.mapHeight) * tpc;
    if (W == 0 || H == 0) return scene;
    if (options.log) options.log("baking " + std::to_string(W) + "x" + std::to_string(H) + " albedo");
    scene.albedo = rgbaImage(W, H, std::vector<uint8_t>(size_t(W) * H * 4, 255), "albedo");
    const float tile = options.tileSize > 0 ? options.tileSize : 4.0f;
    const float gain = options.gain > 0 ? options.gain : 1.0f;

    struct Acc { int slot; float w; };
    for (uint32_t py = 0; py < H; ++py) {
        const float wy = (float(py) + 0.5f) / float(tpc);
        const int iy = std::min(int(wy), scene.mapHeight - 1);
        const float fy = wy - float(iy);
        for (uint32_t px = 0; px < W; ++px) {
            const float wx = (float(px) + 0.5f) / float(tpc);
            const int ix = std::min(int(wx), scene.mapWidth - 1);
            const float fx = wx - float(ix);

            const Vertex* c[4] = {&scene.vertices[size_t(iy) * cx + ix],
                                  &scene.vertices[size_t(iy) * cx + ix + 1],
                                  &scene.vertices[size_t(iy + 1) * cx + ix],
                                  &scene.vertices[size_t(iy + 1) * cx + ix + 1]};
            const float bw[4] = {(1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy};

            // Slope from the cell's corner heights (Fable-space heights).
            const float h00 = level.heightAt(ix, iy), h10 = level.heightAt(ix + 1, iy);
            const float h01 = level.heightAt(ix, iy + 1), h11 = level.heightAt(ix + 1, iy + 1);
            const float dzdx = ((h10 - h00) + (h11 - h01)) * 0.5f;
            const float dzdy = ((h01 - h00) + (h11 - h10)) * 0.5f;
            const float slope = std::sqrt(dzdx * dzdx + dzdy * dzdy);
            const float cliff = smoothstep(options.cliffStartSlope, options.cliffFullSlope, slope);
            const float z = h00 + (h10 - h00) * fx + (h01 - h00) * fy +
                            (h00 - h10 - h01 + h11) * fx * fy;

            std::array<Acc, 12> acc{};
            int n = 0;
            for (int k = 0; k < 4; ++k)
                for (int s = 0; s < 3; ++s) {
                    const float w = bw[k] * float(c[k]->themeWeight[s]) / 255.0f;
                    if (w <= 0) continue;
                    const int slot = c[k]->themeIndex[s];
                    int j = 0;
                    for (; j < n; ++j) if (acc[j].slot == slot) { acc[j].w += w; break; }
                    if (j == n) acc[n++] = {slot, w};
                }

            float rgb[3] = {0, 0, 0}, total = 0;
            for (int j = 0; j < n; ++j) {
                const Image* base = nullptr; const Image* cl = nullptr;
                auto it = slotToLayer.find(acc[j].slot);
                if (it != slotToLayer.end()) {
                    const ThemeLayer& L = scene.themes[it->second];
                    base = L.baseTexture ? imgs[L.baseTexture] : nullptr;
                    cl = L.cliffTexture ? imgs[L.cliffTexture] : nullptr;
                }
                if (!base && !cl) { // unresolved: neutral grey so the map still reads
                    rgb[0] += 128 * acc[j].w; rgb[1] += 128 * acc[j].w; rgb[2] += 128 * acc[j].w;
                    total += acc[j].w;
                    continue;
                }
                float sb[4] = {128, 128, 128, 255}, sc[4] = {128, 128, 128, 255};
                if (base) sampleWrap(*base, wx / tile, wy / tile, sb);
                if (cl) {
                    // Project the cliff texture along the horizontal axis that runs
                    // across the slope, with height as the second coordinate.
                    const float along = std::fabs(dzdx) >= std::fabs(dzdy) ? wy : wx;
                    sampleWrap(*cl, along / tile, z / tile, sc);
                } else std::memcpy(sc, sb, sizeof sc);
                if (!base) std::memcpy(sb, sc, sizeof sb);
                for (int i = 0; i < 3; ++i)
                    rgb[i] += (sb[i] * (1 - cliff) + sc[i] * cliff) * acc[j].w;
                total += acc[j].w;
            }
            uint8_t* out = &scene.albedo.rgba[(size_t(py) * W + px) * 4];
            if (total > 0)
                for (int i = 0; i < 3; ++i)
                    out[i] = uint8_t(std::clamp(rgb[i] / total * gain + 0.5f, 0.0f, 255.0f));
            else out[0] = out[1] = out[2] = 128;
            out[3] = 255;
        }
    }
    scene.hasAlbedo = true;
    return scene;
}

// ------------------------------------------------------------------- writers

namespace {

std::string themeSummaryJson(const Scene& scene, const std::vector<std::string>& pngs) {
    json themes = json::array();
    for (const auto& t : scene.themes) {
        json j = {{"slot", t.slot}, {"name", t.name}, {"def_index", t.defIndex},
                  {"resolved", t.resolved}, {"vertex_references", t.vertexReferences},
                  {"base_texture_id", t.baseTexture}, {"cliff_texture_id", t.cliffTexture}};
        if (t.baseImage >= 0 && size_t(t.baseImage) < pngs.size()) j["base_png"] = pngs[size_t(t.baseImage)];
        if (t.cliffImage >= 0 && size_t(t.cliffImage) < pngs.size()) j["cliff_png"] = pngs[size_t(t.cliffImage)];
        themes.push_back(j);
    }
    json doc = {
        {"source", scene.sourceName},
        {"map_width", scene.mapWidth}, {"map_height", scene.mapHeight},
        {"vertex_grid", {scene.mapWidth + 1, scene.mapHeight + 1}},
        {"up_axis", scene.up == UpAxis::Y ? "Y (Fable z->y, y->-z)" : "Z (Fable native)"},
        {"splat_attributes", {"_THEME_INDEX (VEC3 u8: LEV palette slot per layer)",
                              "_THEME_WEIGHT (VEC3 u8 normalized: layer strength, sums to 1)"}},
        {"themes", themes},
        {"note", "cell theme slots index this palette; base/cliff PNGs are the retail "
                 "textures.big GBANK_MAIN_PC entries the ENGINE_THEME def references"},
    };
    return doc.dump(2);
}

} // namespace

std::vector<fs::path> writeLayerSidecars(const Scene& scene, const fs::path& out) {
    std::vector<fs::path> written;
    if (!scene.layers) return written;
    const fs::path dir = out.parent_path() / (out.stem().string() + "_themes");
    fs::create_directories(dir);
    std::vector<std::string> pngNames;
    for (const auto& img : scene.layerImages) {
        std::string safe = img.name;
        for (char& ch : safe) if (!(std::isalnum((unsigned char)ch) || ch == '_' || ch == '-')) ch = '_';
        const fs::path p = dir / (safe + ".png");
        const auto png = encodePng(img);
        std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
        written.push_back(p);
        pngNames.push_back(fs::relative(p, out.parent_path()).generic_string());
    }
    const fs::path meta = out.parent_path() / (out.stem().string() + ".themes.json");
    std::ofstream(meta) << themeSummaryJson(scene, pngNames);
    written.push_back(meta);
    return written;
}


int appendTerrain(glb::Builder& b, const Scene& scene) {
    const size_t n = scene.vertices.size();
    std::vector<float> pos(n * 3), nrm(n * 3), uv(n * 2);
    for (size_t i = 0; i < n; ++i) {
        const Vertex& v = scene.vertices[i];
        pos[i * 3] = v.px; pos[i * 3 + 1] = v.py; pos[i * 3 + 2] = v.pz;
        nrm[i * 3] = v.nx; nrm[i * 3 + 1] = v.ny; nrm[i * 3 + 2] = v.nz;
        uv[i * 2] = v.u; uv[i * 2 + 1] = v.v;
    }
    json attributes = {{"POSITION", b.positions(pos)}, {"NORMAL", b.vec3(nrm)}, {"TEXCOORD_0", b.vec2(uv)}};
    const int aIdx = b.indices(scene.indices);
    if (scene.walkableColor) {
        std::vector<uint8_t> col(n * 4);
        for (size_t i = 0; i < n; ++i) {
            const bool w = scene.vertices[i].walkable;
            col[i * 4] = 255; col[i * 4 + 1] = w ? 255 : 70; col[i * 4 + 2] = w ? 255 : 70; col[i * 4 + 3] = 255;
        }
        attributes["COLOR_0"] = b.accessor(b.view(col.data(), col.size(), 34962, 4), 5121, "VEC4", n, {{"normalized", true}});
    }
    if (scene.layers) {
        std::vector<uint8_t> idx(n * 4, 0), wgt(n * 4, 0); // VEC3 u8 padded to a 4-byte stride
        for (size_t i = 0; i < n; ++i)
            for (int k = 0; k < 3; ++k) {
                idx[i * 4 + k] = scene.vertices[i].themeIndex[k];
                wgt[i * 4 + k] = scene.vertices[i].themeWeight[k];
            }
        attributes["_THEME_INDEX"] = b.accessor(b.view(idx.data(), idx.size(), 34962, 4), 5121, "VEC3", n);
        attributes["_THEME_WEIGHT"] = b.accessor(b.view(wgt.data(), wgt.size(), 34962, 4), 5121, "VEC3", n, {{"normalized", true}});
    }

    json material = {{"name", "terrain"},
                     {"pbrMetallicRoughness", {{"metallicFactor", 0.0}, {"roughnessFactor", 1.0}}},
                     {"doubleSided", false}};
    if (scene.hasAlbedo) material["pbrMetallicRoughness"]["baseColorTexture"] = {{"index", b.texture(scene.albedo, "albedo", false)}};
    else material["pbrMetallicRoughness"]["baseColorFactor"] = {0.55, 0.6, 0.5, 1.0};
    const int mat = b.material(material);

    json extras = {{"source", scene.sourceName}, {"map_width", scene.mapWidth},
                   {"map_height", scene.mapHeight}, {"uid", std::to_string(scene.uid)},
                   {"height_min", scene.minHeight}, {"height_max", scene.maxHeight},
                   {"fable_units", "1 vertex = 1 world unit; heights = lev raw * 2048"},
                   {"up_axis", scene.up == UpAxis::Y ? "Y" : "Z"},
                   {"generator", "Albion Atlas"}};
    if (!scene.themes.empty()) {
        json th = json::array();
        for (const auto& t : scene.themes)
            th.push_back({{"slot", t.slot}, {"name", t.name}, {"resolved", t.resolved},
                          {"base_texture_id", t.baseTexture}, {"cliff_texture_id", t.cliffTexture}});
        extras["themes"] = th;
    }
    const int mesh = b.mesh({{"name", "terrain"}, {"extras", extras},
                             {"primitives", {{{"attributes", attributes}, {"indices", aIdx}, {"material", mat}, {"mode", 4}}}}});
    json node = {{"name", fs::path(scene.sourceName).stem().string()}, {"mesh", mesh}};
    if (!scene.water.empty()) {
        const size_t wn = scene.water.positions.size() / 3;
        std::vector<float> wnrm(wn * 3, 0.0f);
        for (size_t i = 0; i < wn; ++i) { if (scene.up == UpAxis::Y) wnrm[i * 3 + 1] = 1.0f; else wnrm[i * 3 + 2] = 1.0f; }
        json wattr = {{"POSITION", b.positions(scene.water.positions)}, {"NORMAL", b.vec3(wnrm)}};
        {
            // COLOR_0 alpha = the engine's depth fade (0 at the shore, 1 at 2+ units deep).
            std::vector<uint8_t> col(wn * 4, 255);
            for (size_t i = 0; i < wn; ++i) col[i * 4 + 3] = uint8_t(std::lround(255.0f * scene.water.fade[i]));
            wattr["COLOR_0"] = b.accessor(b.view(col.data(), col.size(), 34962, 4), 5121, "VEC4", wn, {{"normalized", true}});
        }
        json prims = json::array();
        if (!scene.water.indices.empty()) {
            const int wmat = b.material({{"name", "water"},
                                         {"pbrMetallicRoughness", {{"baseColorFactor", {0.16, 0.36, 0.5, 0.62}}, {"metallicFactor", 0.0}, {"roughnessFactor", 0.15}}},
                                         {"alphaMode", "BLEND"}, {"doubleSided", true}});
            prims.push_back({{"attributes", wattr}, {"indices", b.indices(scene.water.indices)}, {"material", wmat}, {"mode", 4}});
        }
        if (!scene.water.iceIndices.empty()) {
            const int imat = b.material({{"name", "ice"},
                                         {"pbrMetallicRoughness", {{"baseColorFactor", {0.78, 0.86, 0.92, 0.9}}, {"metallicFactor", 0.0}, {"roughnessFactor", 0.3}}},
                                         {"alphaMode", "BLEND"}, {"doubleSided", true}});
            prims.push_back({{"attributes", wattr}, {"indices", b.indices(scene.water.iceIndices)}, {"material", imat}, {"mode", 4}});
        }
        const int wmesh = b.mesh({{"name", "water"},
                                  {"extras", {{"wet_vertices", scene.water.wetVertices},
                                              {"note", "ground + LEV theme blend * ENGINE_THEME WaterHeight, 5x5 mean, at level - 0.1 (engine water patch); COLOR_0 alpha = depth fade over 2 units; ice = EWaterType 8"}}},
                                  {"primitives", prims}});
        node["children"] = {b.node({{"name", "Water"}, {"mesh", wmesh}})};
    }
    return b.node(node);
}

std::vector<uint8_t> buildGlb(const Scene& scene) {
    glb::Builder b;
    const int root = appendTerrain(b, scene);
    return b.finish(scene.sourceName, {root}, "Albion Atlas");
}

std::vector<fs::path> writeGlb(const Scene& scene, const fs::path& out) {
    const auto glb = buildGlb(scene);
    if (!out.parent_path().empty()) fs::create_directories(out.parent_path());
    std::ofstream f(out, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write " + out.string());
    f.write(reinterpret_cast<const char*>(glb.data()), std::streamsize(glb.size()));
    std::vector<fs::path> written{out};
    for (auto& p : writeLayerSidecars(scene, out)) written.push_back(p);
    return written;
}

std::vector<fs::path> writeObj(const Scene& scene, const fs::path& out) {
    if (!out.parent_path().empty()) fs::create_directories(out.parent_path());
    const std::string stem = out.stem().string();
    const fs::path mtlPath = out.parent_path() / (stem + ".mtl");
    const fs::path pngPath = out.parent_path() / (stem + "_albedo.png");
    std::vector<fs::path> written;

    std::ofstream obj(out);
    if (!obj) throw std::runtime_error("cannot write " + out.string());
    obj << "# Albion Atlas export of " << scene.sourceName << "\n"
        << "# map " << scene.mapWidth << "x" << scene.mapHeight << " cells, 1 unit per cell, up="
        << (scene.up == UpAxis::Y ? "Y" : "Z") << "\n"
        << "mtllib " << mtlPath.filename().string() << "\n"
        << "o " << fs::path(scene.sourceName).stem().string() << "\n";
    char line[128];
    for (const auto& v : scene.vertices) {
        std::snprintf(line, sizeof line, "v %.4f %.4f %.4f\n", v.px, v.py, v.pz); obj << line;
    }
    for (const auto& v : scene.vertices) {
        // OBJ's vt origin is bottom-left; the scene's v is top-down (glTF).
        std::snprintf(line, sizeof line, "vt %.6f %.6f\n", v.u, 1.0f - v.v); obj << line;
    }
    for (const auto& v : scene.vertices) {
        std::snprintf(line, sizeof line, "vn %.4f %.4f %.4f\n", v.nx, v.ny, v.nz); obj << line;
    }
    obj << "usemtl terrain\n";
    for (size_t i = 0; i + 2 < scene.indices.size(); i += 3) {
        const uint32_t a = scene.indices[i] + 1, b = scene.indices[i + 1] + 1, c = scene.indices[i + 2] + 1;
        std::snprintf(line, sizeof line, "f %u/%u/%u %u/%u/%u %u/%u/%u\n", a, a, a, b, b, b, c, c, c);
        obj << line;
    }
    if (!scene.water.empty()) {
        const uint32_t base = uint32_t(scene.vertices.size());
        obj << "o Water\n";
        for (size_t i = 0; i + 2 < scene.water.positions.size(); i += 3) {
            std::snprintf(line, sizeof line, "v %.4f %.4f %.4f\n", scene.water.positions[i], scene.water.positions[i + 1], scene.water.positions[i + 2]); obj << line;
        }
        for (int pass = 0; pass < 2; ++pass) {
            const auto& tri = pass ? scene.water.iceIndices : scene.water.indices;
            if (tri.empty()) continue;
            obj << (pass ? "usemtl ice\n" : "usemtl water\n");
            for (size_t i = 0; i + 2 < tri.size(); i += 3) {
                std::snprintf(line, sizeof line, "f %u %u %u\n", base + tri[i] + 1, base + tri[i + 1] + 1, base + tri[i + 2] + 1);
                obj << line;
            }
        }
    }
    written.push_back(out);

    std::ofstream mtl(mtlPath);
    mtl << "newmtl terrain\nKa 1 1 1\nKd 1 1 1\nKs 0 0 0\nd 1\nillum 1\n";
    if (!scene.water.empty()) mtl << "newmtl water\nKa 0.16 0.36 0.5\nKd 0.16 0.36 0.5\nKs 0.3 0.3 0.3\nd 0.62\nillum 2\n"
                                   << "newmtl ice\nKa 0.78 0.86 0.92\nKd 0.78 0.86 0.92\nKs 0.2 0.2 0.2\nd 0.9\nillum 2\n";
    if (scene.hasAlbedo) {
        mtl << "map_Kd " << pngPath.filename().string() << "\n";
        const auto png = encodePng(scene.albedo);
        std::ofstream(pngPath, std::ios::binary).write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
        written.push_back(pngPath);
    }
    written.push_back(mtlPath);
    for (auto& p : writeLayerSidecars(scene, out)) written.push_back(p);
    return written;
}

} // namespace albion::terrainexport
