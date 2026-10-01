#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "forge/big.hpp"

namespace forge::lipsync {

struct PresetTrack {
    std::string symbol;
    std::string animation;
};

struct HeadPreset {
    std::string name;
    std::string mesh;
    std::vector<PresetTrack> tracks;
    std::string eyeMesh; // Empty for heads without EYE_SET attachment bones.
    uint8_t eyeSides=3; // bit 0: EYE_SET_L, bit 1: EYE_SET_R
    float eyeRenderSize=1; // EyeGraphic.RenderSizeX in the retail creature def.
};

// Original five presets from EgoCore's lip sync preview, extended with retail
// villager variants and the Oracle's own pose family. Demon Door uses AI for AH;
// Demon Door and Oracle use their ST animation for SZ.
const std::vector<HeadPreset>& headPresets();

struct PresetAssets {
    uint32_t meshId = 0;
    uint32_t eyeMeshId = 0;
    std::vector<uint32_t> animationIds; // same order as HeadPreset::tracks
    std::vector<std::string> missing;
    bool complete() const {return meshId && missing.empty();}
};

// Resolve exact names in graphics.big/MBANK_ALLMESHES. A missing or duplicate
// asset stays explicit; the timeline can still run without the head.
PresetAssets inspectHeadPreset(const big::File& graphics,const HeadPreset& preset);

} // namespace forge::lipsync
