#include "terrainexport.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
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

std::vector<uint8_t> encodePng(const Image& image) {
    size_t len = 0;
    void* png = tdefl_write_image_to_png_file_in_memory_ex(
        image.rgba.data(), int(image.width), int(image.height), 4, int(image.width) * 4, &len, 6, MZ_FALSE, nullptr, 0, nullptr, 0);
    if (!png) throw std::runtime_error("PNG encode failed for " + image.name);
    std::vector<uint8_t> out(static_cast<uint8_t*>(png), static_cast<uint8_t*>(png) + len);
    mz_free(png);
    return out;
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
        if (r.resolved) {
            layer.baseTexture = r.textures.base[0];
            layer.cliffTexture = r.textures.cliff[0];
        } else if (const auto* byName = r.paletteName.empty() ? nullptr : ctx.library.byName(r.paletteName);
                   byName && byName->decoded) {
            // The LEV stores a GLOBAL def index that goes stale when game.bin changes
            // (maps authored against an older bank). The palette also stores the
            // name, and names are stable, so fall back to it.
            layer.resolved = true;
            layer.defIndex = byName->defIndex;
            layer.baseTexture = byName->textures.base[0];
            layer.cliffTexture = byName->textures.cliff[0];
            ++scene.nameResolvedThemes;
        } else {
            ++scene.unresolvedThemes;
            say(options, scene, "palette slot " + std::to_string(r.slot) + " (" + layer.name +
                ") is not an ENGINE_THEME in this install (stale def index, no theme of that name)", true);
        }
        slotToLayer[r.slot] = scene.themes.size();
        scene.themes.push_back(layer);
    }
    if (options.log) options.log(std::to_string(scene.themes.size()) + " ground themes in use");

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

    // 4. Bake the albedo.
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
    return b.node({{"name", fs::path(scene.sourceName).stem().string()}, {"mesh", mesh}});
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
    written.push_back(out);

    std::ofstream mtl(mtlPath);
    mtl << "newmtl terrain\nKa 1 1 1\nKd 1 1 1\nKs 0 0 0\nd 1\nillum 1\n";
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
