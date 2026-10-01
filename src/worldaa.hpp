#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace albion::worldview {
inline uint64_t targetBytes(uint32_t width, uint32_t height, unsigned samples) {
    // RGBA8 + D24S8 samples, plus a single-sample RGBA8 resolve for MSAA.
    return uint64_t(width)*height*(samples > 1 ? 8*samples+4 : 8);
}

inline unsigned aaSamples(unsigned requested, unsigned support, bool valid,
                          uint64_t budget, uint64_t usage, uint64_t sampledTargetBytes,
                          uint32_t width, uint32_t height, unsigned current) {
    if (!valid || !budget) return 1;
    constexpr uint64_t MiB=1024u*1024u;
    const uint64_t other=usage-std::min(usage,sampledTargetBytes);
    const uint64_t room=budget-std::min(budget,other);
    const uint64_t reserve=std::max(256*MiB,budget/5);
    for(unsigned samples : {4u,2u}) {
        if(samples>requested || !(support&samples)) continue;
        const uint64_t bytes=targetBytes(width,height,samples);
        // Upgrade hysteresis plus room for the old target during transactional
        // creation. Unknown/exhausted telemetry always keeps the 1x fallback.
        const uint64_t margin=samples>current ? std::max(64*MiB,sampledTargetBytes) : 0;
        if(bytes<=budget/16 && room>=reserve+margin && bytes<=room-reserve-margin) return samples;
    }
    return 1;
}

struct AaBudget {
    unsigned samples=4;
    float slow=0, fast=0;
    void observe(float dt, bool eligible) {
        if(!eligible || !std::isfinite(dt) || dt<=0 || dt>.25f) { slow=fast=0; return; }
        slow=dt>1.0f/45 ? slow+dt : 0;
        // Normal presentation is VSync-limited. Leave tolerance around 60 Hz
        // instead of requiring ten uninterrupted seconds faster than VSync.
        fast=dt<1.0f/55 ? fast+dt : 0;
        if(slow>=1.5f) { samples=std::max(1u,samples/2); slow=fast=0; }
        if(fast>=10) { samples=std::min(4u,samples*2); slow=fast=0; }
    }
};
} // namespace albion::worldview
