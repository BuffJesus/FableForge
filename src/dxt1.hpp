#pragma once
// Minimal DXT1 (BC1) codec for the distant-LOD inline textures: an opaque
// range-fit encoder (the two RGB565 endpoints span the block's colour range
// along its dominant axis, four-colour palette, no alpha) and the decoder.
// Quality is what a horizon patch needs; nothing here is retail-exact.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace albion::dxt1 {

inline uint16_t pack565(int r, int g, int b) {
    return uint16_t(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
inline void unpack565(uint16_t c, int& r, int& g, int& b) {
    r = ((c >> 11) & 31) * 255 / 31; g = ((c >> 5) & 63) * 255 / 63; b = (c & 31) * 255 / 31;
}

// rgba: width*height*4, width/height multiples of 4. Returns width*height/2 bytes.
inline std::vector<uint8_t> encode(const uint8_t* rgba, int width, int height) {
    std::vector<uint8_t> out;
    out.reserve(size_t(width) * size_t(height) / 2);
    for (int by = 0; by < height; by += 4)
        for (int bx = 0; bx < width; bx += 4) {
            int px[16][3];
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x) {
                    const uint8_t* p = rgba + ((size_t(by + y) * size_t(width) + size_t(bx + x)) * 4);
                    px[y * 4 + x][0] = p[0]; px[y * 4 + x][1] = p[1]; px[y * 4 + x][2] = p[2];
                }
            // principal axis by the covariance's dominant direction (a few power iterations)
            float mean[3] = {0, 0, 0};
            for (auto& p : px) for (int k = 0; k < 3; ++k) mean[k] += float(p[k]);
            for (float& m : mean) m /= 16.0f;
            float cov[3][3] = {};
            for (auto& p : px)
                for (int i = 0; i < 3; ++i)
                    for (int j = 0; j < 3; ++j) cov[i][j] += (float(p[i]) - mean[i]) * (float(p[j]) - mean[j]);
            float axis[3] = {1, 1, 1};
            for (int it = 0; it < 8; ++it) {
                float n[3] = {0, 0, 0};
                for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) n[i] += cov[i][j] * axis[j];
                const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                if (len < 1e-6f) break;
                for (int i = 0; i < 3; ++i) axis[i] = n[i] / len;
            }
            float lo = 1e9f, hi = -1e9f;
            int loP = 0, hiP = 0;
            for (int i = 0; i < 16; ++i) {
                const float d = (float(px[i][0]) - mean[0]) * axis[0] + (float(px[i][1]) - mean[1]) * axis[1] + (float(px[i][2]) - mean[2]) * axis[2];
                if (d < lo) { lo = d; loP = i; }
                if (d > hi) { hi = d; hiP = i; }
            }
            uint16_t c0 = pack565(px[hiP][0], px[hiP][1], px[hiP][2]);
            uint16_t c1 = pack565(px[loP][0], px[loP][1], px[loP][2]);
            if (c0 < c1) std::swap(c0, c1);          // c0 > c1 selects the four-colour mode
            int pal[4][3];
            unpack565(c0, pal[0][0], pal[0][1], pal[0][2]);
            unpack565(c1, pal[1][0], pal[1][1], pal[1][2]);
            for (int k = 0; k < 3; ++k) {
                pal[2][k] = (2 * pal[0][k] + pal[1][k]) / 3;
                pal[3][k] = (pal[0][k] + 2 * pal[1][k]) / 3;
            }
            uint32_t indices = 0;
            for (int i = 0; i < 16; ++i) {
                int best = 0; long bestD = 1L << 40;
                for (int c = 0; c < (c0 == c1 ? 1 : 4); ++c) {
                    const long dr = px[i][0] - pal[c][0], dg = px[i][1] - pal[c][1], db = px[i][2] - pal[c][2];
                    const long d = dr * dr + dg * dg + db * db;
                    if (d < bestD) { bestD = d; best = c; }
                }
                indices |= uint32_t(best) << (2 * i);
            }
            out.push_back(uint8_t(c0)); out.push_back(uint8_t(c0 >> 8));
            out.push_back(uint8_t(c1)); out.push_back(uint8_t(c1 >> 8));
            for (int k = 0; k < 4; ++k) out.push_back(uint8_t(indices >> (8 * k)));
        }
    return out;
}

// Decode width*height/2 DXT1 bytes to RGBA.
inline std::vector<uint8_t> decode(const uint8_t* data, int width, int height) {
    std::vector<uint8_t> rgba(size_t(width) * size_t(height) * 4, 255);
    size_t p = 0;
    for (int by = 0; by < height; by += 4)
        for (int bx = 0; bx < width; bx += 4) {
            const uint16_t c0 = uint16_t(data[p] | (data[p + 1] << 8)), c1 = uint16_t(data[p + 2] | (data[p + 3] << 8));
            uint32_t idx = 0; std::memcpy(&idx, data + p + 4, 4);
            p += 8;
            int pal[4][3];
            unpack565(c0, pal[0][0], pal[0][1], pal[0][2]);
            unpack565(c1, pal[1][0], pal[1][1], pal[1][2]);
            for (int k = 0; k < 3; ++k) {
                if (c0 > c1) { pal[2][k] = (2 * pal[0][k] + pal[1][k]) / 3; pal[3][k] = (pal[0][k] + 2 * pal[1][k]) / 3; }
                else { pal[2][k] = (pal[0][k] + pal[1][k]) / 2; pal[3][k] = 0; }
            }
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x) {
                    const int c = int((idx >> (2 * (y * 4 + x))) & 3);
                    uint8_t* q = &rgba[((size_t(by + y) * size_t(width)) + size_t(bx + x)) * 4];
                    q[0] = uint8_t(pal[c][0]); q[1] = uint8_t(pal[c][1]); q[2] = uint8_t(pal[c][2]); q[3] = 255;
                }
        }
    return rgba;
}

} // namespace albion::dxt1
