#include "forge/minimapframe.hpp"

#include <cmath>

namespace forge::minimapframe {

namespace {
void aspect(float w, float h, float& ax, float& ay) {
    ax = 1.0f; ay = 1.0f;
    if (h < w) ay = h / w;
    else if (w < h) ax = w / h;
}
} // namespace

void toPixel(const Framing& f, float width, float height, float size, float x, float y, float& px, float& py) {
    float ax, ay; aspect(width, height, ax, ay);
    const float u = x / width * ax, v = y / height * ay;
    px = f.scale * size * u + f.scale * f.offsetX;
    py = f.scale * size * (1.0f - v) + f.scale * f.offsetY;
}

bool fromPixel(const Framing& f, float width, float height, float size, float px, float py, float& x, float& y) {
    float ax, ay; aspect(width, height, ax, ay);
    if (f.scale <= 0.0f || size <= 0.0f) return false;
    const float u = (px - f.scale * f.offsetX) / (f.scale * size);
    const float v = 1.0f - (py - f.scale * f.offsetY) / (f.scale * size);
    x = u / ax * width;
    y = v / ay * height;
    return x >= 0.0f && x <= width && y >= 0.0f && y <= height;
}

Framing centred(float width, float height, float size) {
    float ax, ay; aspect(width, height, ax, ay);
    Framing f;
    // map spans x in [offX, offX + size*ax], y in [size*(1-ay) + offY, size + offY]: centre both
    f.offsetX = std::round(size * (1.0f - ax) * 0.5f);
    f.offsetY = std::round(-size * (1.0f - ay) * 0.5f);
    return f;
}

} // namespace forge::minimapframe
