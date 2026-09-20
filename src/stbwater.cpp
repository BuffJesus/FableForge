#include "stbwater.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "forge/rangecodec.hpp"
#include "stbterrain.hpp"

namespace albion::stbwater {

namespace {
constexpr int kGrid = 17;
constexpr size_t kRecords = size_t(kGrid) * kGrid;
constexpr size_t kRecordSize = 0x42;
}

Record makeRecord(int worldX, int worldY, float h, float ground, bool ice) {
    Record r;
    r.x = uint16_t(worldX);
    r.y = uint16_t(worldY);
    // CWaterPatchMesh::BuildVertexBuffer: z sits 0.1 under the level (0.001 for ice), on a 1/256 grid, floored
    const float zRaw = std::max(h - (ice ? 0.001f : 0.1f), 0.0f);
    r.z = std::floor(zRaw * 256.0f) / 256.0f;
    // world units -> turns -> radians with the float constants, products kept in double
    const double kInv2Pi = double(float(1.0 / 6.283185307179586)), k2Pi = double(float(6.283185307179586));
    const double ax = double(worldX) * kInv2Pi * k2Pi, ay = double(worldY) * kInv2Pi * k2Pi;
    r.waveS = int16_t(std::lround((std::sin(ax) + std::sin(ay)) * 32767.0 / 2.0));
    r.waveC = int16_t(std::lround((std::cos(ax) + std::cos(ay)) * 32767.0 / 2.0));
    // the fade: how far under the (quantised) surface the ground is, 0..2 units
    const float depth = std::clamp((r.z + 0.1f) - ground, 0.0f, 2.0f) * 32767.0f / 2.0f;
    r.depth = int16_t(std::lround(depth));
    return r;
}

float bodySpan(const terrainexport::WaterLevels& levels) {
    int minX = 1 << 30, minY = 1 << 30, maxX = -1, maxY = -1;
    for (int y = 0; y < levels.height; ++y)
        for (int x = 0; x < levels.width; ++x)
            if (levels.level[size_t(y) * levels.width + x] > 0.001f) {
                minX = std::min(minX, x); maxX = std::max(maxX, x);
                minY = std::min(minY, y); maxY = std::max(maxY, y);
            }
    if (maxX < 0) return 0.0f;
    const float dx = float(maxX - minX), dy = float(maxY - minY);
    return std::sqrt(dx * dx + dy * dy);
}

std::vector<uint8_t> buildPatchPayload(const MapInput& in, int patchX, int patchY) {
    std::vector<uint8_t> out;
    if (!in.levels || !in.ground || in.levels->empty()) return out;
    const terrainexport::WaterLevels& wl = *in.levels;
    if (in.ground->size() != wl.level.size()) return out;
    auto levelAt = [&](int x, int y) { return wl.at(x, y); };
    auto groundAt = [&](int x, int y) {
        const int cx = std::clamp(x, 0, wl.width - 1), cy = std::clamp(y, 0, wl.height - 1);
        return (*in.ground)[size_t(cy) * wl.width + cx];
    };
    // BuildLayersFromThemes: a descriptor only when any vertex height > 0.001
    bool any = false;
    int types[16] = {};
    for (int j = 0; j <= 16 && !any; ++j)
        for (int i = 0; i <= 16; ++i)
            if (levelAt(patchX + i, patchY + j) > 0.001f) { any = true; break; }
    if (!any) return out;
    // the patch's dominant WaterType over its wet vertices (0 when the halo alone reaches in)
    for (int j = 0; j <= 16; ++j)
        for (int i = 0; i <= 16; ++i) {
            const int x = patchX + i, y = patchY + j;
            if (x < 0 || y < 0 || x >= wl.width || y >= wl.height) continue;
            const int t = wl.type[size_t(y) * wl.width + x];
            if (t > 0 && t < 16) ++types[t];
        }
    int waterType = 0, best = 0;
    for (int t = 1; t < 16; ++t) if (types[t] > best) { best = types[t]; waterType = t; }
    const bool ice = waterType == 8;
    // FindCorrectWaterLevel: the mean of the non-zero heights within +-2 cells of the patch's own array
    auto vertexLevel = [&](int i, int j) {
        const float h = levelAt(patchX + i, patchY + j);
        if (h > 0.001f) return h;
        float sum = 0; int n = 0;
        for (int jj = std::max(0, j - 2); jj <= std::min(16, j + 2); ++jj)
            for (int ii = std::max(0, i - 2); ii <= std::min(16, i + 2); ++ii) {
                const float v = levelAt(patchX + ii, patchY + jj);
                if (v > 0.001f) { sum += v; ++n; }
            }
        return n ? sum / float(n) : groundAt(patchX + i, patchY + j) - 1.0f;
    };
    std::vector<uint8_t> raw(kRecords * kRecordSize, 0);
    size_t k = 0;
    for (int i = 0; i <= 16; ++i)          // retail order: x outer, y inner (audited: x constant across 17 consecutive records)
        for (int j = 0; j <= 16; ++j, ++k) {
            const int x = patchX + i, y = patchY + j;
            const Record r = makeRecord(in.worldX + x, in.worldY + y, vertexLevel(i, j), groundAt(x, y), ice);
            stbterrain::WaterPatchRecord rec;
            rec.x = r.x; rec.y = r.y; rec.z = r.z; rec.waveS = r.waveS; rec.waveC = r.waveC; rec.depth = r.depth;
            rec.distToShore = 0.0f;
            stbterrain::packWaterRecord(rec, raw.data() + k * kRecordSize);
        }
    std::vector<uint8_t> block;
    try {
        block = forge::rangecodec::encodeNative(raw.data(), kRecords, kRecordSize);
        if (forge::rangecodec::decode(block.data(), block.size(), kRecords, kRecordSize) != raw) block.clear();
    } catch (...) { block.clear(); }
    if (block.empty()) block = forge::rangecodec::encodeRaw(raw.data(), kRecords, kRecordSize);
    // CWaterPatchMesh::Save: i32 Offset.x, i32 Offset.y, f32 WaterBodySpan, i32 WaterType, i32 len, block
    auto put32 = [&](int32_t v) { for (int b = 0; b < 4; ++b) out.push_back(uint8_t(uint32_t(v) >> (8 * b))); };
    auto putf = [&](float v) { uint32_t u; std::memcpy(&u, &v, 4); put32(int32_t(u)); };
    put32(patchX); put32(patchY); putf(in.bodySpan); put32(waterType);
    put32(int32_t(block.size()));
    out.insert(out.end(), block.begin(), block.end());
    return out;
}

} // namespace albion::stbwater
