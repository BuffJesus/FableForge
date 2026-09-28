#include "worldtiles.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

#include "forge/lev.hpp"

namespace albion::worldtiles {

namespace {
constexpr uint32_t kMagic = 0x31545746;   // "FWT1"

// a height tint when no textures are at hand: low teal -> moss -> sand -> snow
terrainexport::Image tintImage(const Tile& t) {
    terrainexport::Image img;
    img.width = uint32_t(t.gw); img.height = uint32_t(t.gh);
    img.rgba.resize(size_t(t.gw) * t.gh * 4);
    const float span = std::max(t.maxH - t.minH, 1e-3f);
    static const float stops[4][3] = {{38, 64, 78}, {84, 122, 82}, {186, 170, 128}, {238, 236, 228}};
    for (size_t i = 0; i < t.heights.size(); ++i) {
        const float f = std::clamp((t.heights[i] - t.minH) / span, 0.0f, 1.0f) * 3.0f;
        const int k = std::min(2, int(f));
        const float u = f - float(k);
        for (int c = 0; c < 3; ++c) img.rgba[i * 4 + c] = uint8_t(stops[k][c] + (stops[k + 1][c] - stops[k][c]) * u);
        img.rgba[i * 4 + 3] = 255;
    }
    return img;
}

// box-filter an image down so its longer side is at most `maxSide`
terrainexport::Image shrink(const terrainexport::Image& src, int maxSide) {
    const int w = int(src.width), h = int(src.height);
    const int f = std::max(1, (std::max(w, h) + maxSide - 1) / maxSide);
    if (f == 1) return src;
    terrainexport::Image out;
    out.width = uint32_t(std::max(1, w / f)); out.height = uint32_t(std::max(1, h / f));
    out.rgba.assign(size_t(out.width) * out.height * 4, 0);
    for (uint32_t y = 0; y < out.height; ++y)
        for (uint32_t x = 0; x < out.width; ++x) {
            uint32_t acc[4] = {0, 0, 0, 0};
            for (int j = 0; j < f; ++j)
                for (int i = 0; i < f; ++i) {
                    const size_t s = (size_t(y * f + j) * w + (x * f + i)) * 4;
                    for (int c = 0; c < 4; ++c) acc[c] += src.rgba[s + c];
                }
            for (int c = 0; c < 4; ++c) out.rgba[(size_t(y) * out.width + x) * 4 + c] = uint8_t(acc[c] / uint32_t(f * f));
        }
    return out;
}
} // namespace

int strideFor(int cellsX, int cellsY, int target) {
    const int longSide = std::max(cellsX, cellsY) - 1;
    return std::max(1, int(std::lround(float(longSide) / float(std::max(target, 1)))));
}

int sampleIndex(int i, int count, int cells, int stride) {
    return i == count - 1 ? cells - 1 : std::min(i * stride, cells - 1);
}

Tile buildTile(const forge::lev::File& level, const std::string& name, const terrainexport::Context* context,
               const std::filesystem::path& gameRoot, float gain, int maxTexels) {
    Tile t;
    t.name = name;
    t.cellsX = level.cellsX(); t.cellsY = level.cellsY();
    t.stride = strideFor(t.cellsX, t.cellsY);
    t.gw = (t.cellsX - 1 + t.stride - 1) / t.stride + 1;
    t.gh = (t.cellsY - 1 + t.stride - 1) / t.stride + 1;
    t.heights.resize(size_t(t.gw) * t.gh);
    t.minH = 1e30f; t.maxH = -1e30f;
    for (int j = 0; j < t.gh; ++j)
        for (int i = 0; i < t.gw; ++i) {
            const float h = level.heightAt(sampleIndex(i, t.gw, t.cellsX, t.stride), sampleIndex(j, t.gh, t.cellsY, t.stride));
            t.heights[size_t(j) * t.gw + i] = h;
            t.minH = std::min(t.minH, h); t.maxH = std::max(t.maxH, h);
        }
    if (t.heights.empty()) t.minH = t.maxH = 0;
    if (context && context->ready()) {
        terrainexport::Options o;
        o.textures = true; o.texelsPerCell = 1; o.gain = gain; o.up = terrainexport::UpAxis::Z;
        o.water = false; o.gameRoot = gameRoot; o.mapName = name;
        try {
            const auto sc = terrainexport::buildScene(level, o, context);
            if (sc.hasAlbedo && sc.albedo.width && sc.albedo.height) t.ground = shrink(sc.albedo, maxTexels);
        } catch (...) {}
    }
    if (t.ground.rgba.empty()) t.ground = tintImage(t);
    return t;
}

float heightAt(const Tile& t, float x, float y) {
    if (t.heights.empty()) return 0.0f;
    // grid index space: sample i sits at map x = sampleIndex(i); the last column may be closer
    auto toGrid = [&](float v, int count, int cells) {
        v = std::clamp(v, 0.0f, float(cells - 1));
        const float g = v / float(t.stride);
        const int i = std::min(int(g), count - 2 < 0 ? 0 : count - 2);
        const float x0 = float(sampleIndex(i, count, cells, t.stride)), x1 = float(sampleIndex(std::min(i + 1, count - 1), count, cells, t.stride));
        const float f = x1 > x0 ? std::clamp((v - x0) / (x1 - x0), 0.0f, 1.0f) : 0.0f;
        return std::pair<int, float>{i, f};
    };
    const auto [i, fx] = toGrid(x, t.gw, t.cellsX);
    const auto [j, fy] = toGrid(y, t.gh, t.cellsY);
    const int i1 = std::min(i + 1, t.gw - 1), j1 = std::min(j + 1, t.gh - 1);
    auto H = [&](int a, int b) { return t.heights[size_t(b) * t.gw + a]; };
    const float top = H(i, j) + (H(i1, j) - H(i, j)) * fx;
    const float bot = H(i, j1) + (H(i1, j1) - H(i, j1)) * fx;
    return top + (bot - top) * fy;
}

void appendMesh(const Tile& t, foliageexport::Scene& scene, float worldX, float worldY) {
    if (t.gw < 2 || t.gh < 2) return;
    foliageexport::Mesh m;
    m.name = t.name;
    const float w = float(t.cellsX - 1), h = float(t.cellsY - 1);
    m.geometry.vertices.reserve(size_t(t.gw) * t.gh);
    for (int j = 0; j < t.gh; ++j)
        for (int i = 0; i < t.gw; ++i) {
            const float x = float(sampleIndex(i, t.gw, t.cellsX, t.stride)), y = float(sampleIndex(j, t.gh, t.cellsY, t.stride));
            // normal from the neighbouring samples
            const int il = std::max(i - 1, 0), ir = std::min(i + 1, t.gw - 1), jd = std::max(j - 1, 0), ju = std::min(j + 1, t.gh - 1);
            const float xl = float(sampleIndex(il, t.gw, t.cellsX, t.stride)), xr = float(sampleIndex(ir, t.gw, t.cellsX, t.stride));
            const float yd = float(sampleIndex(jd, t.gh, t.cellsY, t.stride)), yu = float(sampleIndex(ju, t.gh, t.cellsY, t.stride));
            const float dzdx = xr > xl ? (t.heights[size_t(j) * t.gw + ir] - t.heights[size_t(j) * t.gw + il]) / (xr - xl) : 0.0f;
            const float dzdy = yu > yd ? (t.heights[size_t(ju) * t.gw + i] - t.heights[size_t(jd) * t.gw + i]) / (yu - yd) : 0.0f;
            float nx = -dzdx, ny = -dzdy, nz = 1.0f;
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            m.geometry.vertices.push_back({x, y, t.heights[size_t(j) * t.gw + i], nx / len, ny / len, nz / len,
                                           w > 0 ? x / w : 0.0f, h > 0 ? y / h : 0.0f});
        }
    foliageexport::SubMesh part;
    for (int j = 0; j + 1 < t.gh; ++j)
        for (int i = 0; i + 1 < t.gw; ++i) {
            const uint32_t a = uint32_t(j * t.gw + i), b = a + 1, c = a + uint32_t(t.gw), d = c + 1;
            part.indices.insert(part.indices.end(), {a, c, b, b, c, d});
        }
    part.image = int(scene.images.size());
    scene.images.push_back(t.ground);
    m.parts.push_back(std::move(part));
    foliageexport::Instance inst;
    inst.mesh = int(scene.meshes.size());
    inst.x = worldX; inst.y = worldY; inst.z = 0; inst.scale = 1;
    scene.meshes.push_back(std::move(m));
    scene.instances.push_back(inst);
}

bool saveTile(const std::filesystem::path& file, const std::string& key, const Tile& t) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary);
    if (!out) return false;
    auto u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto f32 = [&](float v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    u32(kMagic); u32(uint32_t(key.size())); out.write(key.data(), std::streamsize(key.size()));
    u32(uint32_t(t.name.size())); out.write(t.name.data(), std::streamsize(t.name.size()));
    u32(uint32_t(t.cellsX)); u32(uint32_t(t.cellsY)); u32(uint32_t(t.stride)); u32(uint32_t(t.gw)); u32(uint32_t(t.gh));
    f32(t.minH); f32(t.maxH);
    out.write(reinterpret_cast<const char*>(t.heights.data()), std::streamsize(t.heights.size() * 4));
    u32(t.ground.width); u32(t.ground.height);
    out.write(reinterpret_cast<const char*>(t.ground.rgba.data()), std::streamsize(t.ground.rgba.size()));
    return bool(out);
}

bool loadTile(const std::filesystem::path& file, const std::string& key, Tile& t) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    auto u32 = [&]() { uint32_t v = 0; in.read(reinterpret_cast<char*>(&v), 4); return v; };
    auto f32 = [&]() { float v = 0; in.read(reinterpret_cast<char*>(&v), 4); return v; };
    if (u32() != kMagic) return false;
    const uint32_t kl = u32();
    if (kl > 4096) return false;
    std::string k(kl, '\0'); in.read(k.data(), kl);
    if (k != key) return false;
    const uint32_t nl = u32();
    if (nl > 1024) return false;
    t.name.assign(nl, '\0'); in.read(t.name.data(), nl);
    t.cellsX = int(u32()); t.cellsY = int(u32()); t.stride = int(u32()); t.gw = int(u32()); t.gh = int(u32());
    t.minH = f32(); t.maxH = f32();
    if (t.gw <= 0 || t.gh <= 0 || t.gw > 4096 || t.gh > 4096) return false;
    t.heights.resize(size_t(t.gw) * t.gh);
    in.read(reinterpret_cast<char*>(t.heights.data()), std::streamsize(t.heights.size() * 4));
    t.ground.width = u32(); t.ground.height = u32();
    if (t.ground.width > 4096 || t.ground.height > 4096) return false;
    t.ground.rgba.resize(size_t(t.ground.width) * t.ground.height * 4);
    in.read(reinterpret_cast<char*>(t.ground.rgba.data()), std::streamsize(t.ground.rgba.size()));
    return bool(in);
}

} // namespace albion::worldtiles
