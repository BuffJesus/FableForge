#include "forge/fractal.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace forge::fractal {

namespace {

uint32_t bitsOf(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
float floatOf(uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }

// GFFastOneOverSqrt: table guess + one Newton step
float fastRsqrt(float x) {
    const auto& tab = Generator::rsqrtTable();
    const uint32_t b = bitsOf(x);
    const float y = floatOf((((0x17Cu - ((b >> 23) & 0xFFu)) >> 1) << 23) | (uint32_t(tab[(b >> 17) & 0x7F]) << 15));
    return float((3.0 - double(y) * double(y) * double(x)) * double(y) * 0.5);
}

struct Rng {
    uint32_t seed = 0;
    uint32_t step() {
        const uint32_t v = seed * 0x24A1u + 0x24DFu;
        seed = (v >> 13) | (v << 19);
        return seed;
    }
    double floatRandom(float m) {   // GFFloatRandom
        step();
        const double mm = double(m) * 65536.0;
        return mm <= 0.0001 ? 0.0 : double(float(std::fmod(double(seed), mm) / 65536.0));
    }
    uint32_t random(uint32_t m) { step(); return m == 0 ? 0 : seed % m; }   // GFRandom
};

// float operands round every operation to float, as the engine's x87 code does
// under the 24-bit precision control D3D sets
float fade(float t) { return (3.0f - t * 2.0f) * t * t; }
float lerp(float t, float a, float b) { return (b - a) * t + a; }
float dot(const std::array<float, 2>& g, float a, float b) { return b * g[1] + a * g[0]; }

} // namespace

const std::array<uint8_t, 128>& Generator::rsqrtTable() {
    static const std::array<uint8_t, 128> table = [] {
        std::array<uint8_t, 128> t{};
        for (uint32_t i = 0; i < 128; ++i) {
            const float x = floatOf((i << 17) | 0x3F000000u);
            const float s = float(std::sqrt(double(x)));
            const float y = float(1.0 / double(s));
            t[i] = uint8_t(((bitsOf(y) + 0x2000u) >> 15) & 0xFFu);
        }
        return t;
    }();
    return table;
}

Generator::Generator(const Params& params) : params_(params) {
    Rng rng;
    for (int i = 0; i < 256; ++i) {
        p_[size_t(i)] = i;
        const float gx = float(rng.floatRandom(2.0f) - 1.0);
        const float gy = float(rng.floatRandom(2.0f) - 1.0);
        const double sum = double(float(double(gx) * gx)) + double(float(double(gy) * gy));
        const float r = fastRsqrt(float(sum));
        g_[size_t(i)] = {gx * r, gy * r};
    }
    for (int i = 255; i >= 1; --i) std::swap(p_[size_t(i)], p_[rng.random(256)]);   // % 256, as the engine does
    for (int i = 0; i < 256; ++i) { p_[size_t(256 + i)] = p_[size_t(i)]; g_[size_t(256 + i)] = g_[size_t(i)]; }
    for (int i = 0; i < 128; ++i) exp_[size_t(i)] = std::pow(params_.lacunarity, -double(i) * params_.dimension);
}

float Generator::perlin(float x, float y) const {
    const float tx = float(double(x) + 4096.0), ty = float(double(y) + 4096.0);
    const int ix = int(tx), iy = int(ty);
    const int bx0 = ix & 0xFF, bx1 = (bx0 + 1) & 0xFF, by0 = iy & 0xFF, by1 = (by0 + 1) & 0xFF;
    const float rx0 = tx - float(ix), rx1 = rx0 - 1.0f;
    const float ry0 = ty - float(iy), ry1 = ry0 - 1.0f;
    const int i = p_[size_t(bx0)], j = p_[size_t(bx1)];
    const int b00 = p_[size_t(i + by0)], b10 = p_[size_t(j + by0)], b01 = p_[size_t(i + by1)], b11 = p_[size_t(j + by1)];
    const float sx = fade(rx0), sy = fade(ry0);
    const float a = lerp(sx, dot(g_[size_t(b00)], rx0, ry0), dot(g_[size_t(b10)], rx1, ry0));
    const float b = lerp(sx, dot(g_[size_t(b01)], rx0, ry1), dot(g_[size_t(b11)], rx1, ry1));
    return lerp(sy, a, b);
}

double Generator::hybrid(float x, float y) const {
    const double off = 0.699999988079071;   // 0.7f
    const float lac = float(params_.lacunarity);
    double r = (double(perlin(x, y)) + 1.0) / 2.0;
    r = (2.0 * (1.0 - std::cos(double(float(r * 0.25)) * double(6.2831855f))) - 1.0) + off;
    double w = r;
    x *= lac; y *= lac;
    int i = 1;
    while (w > 0.001 && double(i) < params_.octaves && i < 127) {
        w = std::min(w, 1.0);
        const double sig = (double(perlin(x, y)) + off) * exp_[size_t(i)];
        r += w * sig;
        w = sig * w;
        x *= lac; y *= lac;
        ++i;
    }
    const double rem = params_.octaves - std::trunc(params_.octaves);
    if (rem != 0.0 && i < 128) r += double(perlin(x, y)) * rem * exp_[size_t(i)];
    r = (r + 0.30000001192092896) / 3.200000047683716;
    return std::clamp(r, 0.0, 1.0);
}

float Generator::heightAt(double worldX, double worldY) const {
    const float vx = float(worldX + params_.mapX), vy = float(worldY + params_.mapY);
    const float d = float(4096.0 * double(float(params_.worldScaler)));
    double h = hybrid(vx / d, vy / d);
    if (params_.useFalloff) {
        const double dist = std::hypot(worldX - 2048.0, worldY - 2048.0);
        const double st = params_.startFalloff, en = params_.endFalloff;
        if (dist >= en) h = 0.0;
        else if (dist > st && en > st) h *= std::cos(double(float((dist - st) / (en - st) * 0.25)) * 6.2831855);
    }
    return float(h);
}

} // namespace forge::fractal
