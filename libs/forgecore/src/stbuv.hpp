#pragma once
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace forge::stbbake::detail {
// PackUVForVertexBuffer (02CDE610) stores UV*255 to float, then uses
// GFFloatToLongNear (018C4020). Model its explicit nearest/even FPU mode;
// the live editor's rounding mode is a separate runtime-state question.
inline uint16_t packBackgroundUV(float coordinate) {
    if(!std::isfinite(coordinate) || coordinate<0 || coordinate>1)
        throw std::invalid_argument("background UV must be finite and within 0..1");
    const float scaled=coordinate*255.f;
    const int low=int(scaled);
    const float remainder=scaled-float(low);
    return uint16_t(low+(remainder>.5f || (remainder==.5f && (low&1))));
}

inline float unpackBackgroundUV(uint16_t coordinate) {
    if(coordinate>255)throw std::invalid_argument("packed background UV exceeds 255");
    return float(coordinate)/255.f;
}
} // namespace forge::stbbake::detail
