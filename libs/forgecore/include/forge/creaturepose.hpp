#pragma once
#include <array>
#include <span>
#include <vector>
#include "forge/meshpreview.hpp"

namespace forge::creaturepose {
using Matrix=std::array<float,16>; // row-major storage, column-vector points
struct Frame {
    std::array<float,4> rotation{0,0,0,1}; // raw native quaternion convention
    std::array<float,3> position{};
};
struct Pose {
    std::vector<Matrix> global; // hierarchy followed by independent bone scaling
    std::vector<Matrix> skin;   // global * inverseBind
};
// Morph displacement, rigid hierarchy, independent scale, then inverse bind.
// Parents must precede children; negative parent slots are roots. Empty scales
// means identity. Does not sample animation or modify input frames/geometry.
Pose evaluate(std::span<const Frame> frames,std::span<const int32_t> parents,
              std::span<const std::array<float,3>> scales,std::span<const Matrix> inverseBinds);
// Read raw native rotation/position from the mesh's preserved 48-byte bind rows.
// Stored keyframe scaling is ignored, matching the native 4x4 pose stages.
Pose bindPose(const meshpreview::Geometry& mesh,std::span<const std::array<float,3>> scales={});
} // namespace forge::creaturepose
