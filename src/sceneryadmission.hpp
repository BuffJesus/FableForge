#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace albion::worldview {
struct SceneryAdmission {
    float priority = 0;
    uint64_t required = 0;
    // Renderer supplies the exact bytes releasable by the entire eligible set,
    // accounting for textures shared with retained maps/other render layers.
    uint64_t reclaimable = std::numeric_limits<uint64_t>::max();
};
struct SceneryOccupant {
    float priority = 0;
    uint64_t bytes = 0;
    bool protectedHandoff = false;
};
struct SceneryReplacement { int admission = -1, victim = -1; };
inline constexpr float kSceneryReplacementMargin = 32;

// Demands arrive in priority order. A further margin on top of the residency
// bonus prevents two almost-equivalent maps from repeatedly replacing each
// other at the memory boundary. Only one resident fades at a time; accounting
// is sampled again after its actual GPU resources are released. Admission must
// fit after the whole eligible set is retired before any resident starts fading.
inline SceneryReplacement sceneryReplacement(const std::vector<SceneryAdmission>& demands,
                                              const std::vector<SceneryOccupant>& residents,
                                              uint64_t available, uint64_t limit,
                                              int fadingVictim = -1) {
    if (!limit) return {};
    for (size_t i=0;i<demands.size();++i) {
        const auto& demand=demands[i];
        // Do not start reclaiming for a lower request while a higher one can
        // already enter as soon as worker/payload retirement permits it.
        if (!demand.required || demand.required<=available) return {};
        if (demand.required>limit) continue;
        auto eligible=[&](int j) {
            const auto& resident=residents[size_t(j)];
            return resident.bytes && !resident.protectedHandoff &&
                   resident.priority>demand.priority+kSceneryReplacementMargin;
        };
        uint64_t reclaimable=0;
        for (size_t j=0;j<residents.size();++j) if (eligible(int(j)))
            reclaimable+=std::min(residents[j].bytes,limit-std::min(limit,reclaimable));
        reclaimable=std::min(reclaimable,demand.reclaimable);
        if (demand.required-available>reclaimable) continue;
        if (fadingVictim>=0 && size_t(fadingVictim)<residents.size() && eligible(fadingVictim))
            return {int(i),fadingVictim};
        int worst=-1;
        for (size_t j=0;j<residents.size();++j)
            if (eligible(int(j)) && (worst<0 || residents[j].priority>residents[size_t(worst)].priority)) worst=int(j);
        if (worst>=0) return {int(i),worst};
    }
    return {};
}
}
