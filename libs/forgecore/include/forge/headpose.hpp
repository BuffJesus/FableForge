#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "forge/animation.hpp"
#include "forge/lipsync.hpp"
#include "forge/meshpreview.hpp"

namespace forge::headpose {

// Row-major matrix for column-vector points. Geometry::Bone::inverseBind uses
// this convention; the returned skin matrix is posedGlobal * inverseBind.
using Matrix=std::array<float,16>;
using AnimationMap=std::unordered_map<std::string,const animation::Animation*>;

struct Pose {
    std::vector<Matrix> skin;
    size_t posedBones=0;
};

// Blend the supplied phoneme poses and MM rest weight over the mesh bind pose.
// A missing animation or bone track falls back to bind pose. Type-9 phoneme
// animations are sampled at their first frame; dialogue timing comes from the
// already-interpolated lipsync::Pose.
Pose evaluate(const meshpreview::Geometry& mesh,const lipsync::Pose& mouth,
              const AnimationMap& animations);

// Copy geometry with linear-blend skinned positions and normals. Unskinned
// vertices and the source geometry stay unchanged.
meshpreview::Geometry skin(const meshpreview::Geometry& mesh,const Pose& pose);

// Add the separate retail eye mesh at the two EYE_SET bind transforms.
// The added vertices follow those bones when a head pose moves them.
void attachEyes(meshpreview::Geometry& head,const meshpreview::Geometry& eye,
                uint8_t sides=3,float renderSize=1);

std::array<float,3> transformPoint(const Matrix& matrix,const std::array<float,3>& point);

} // namespace forge::headpose
