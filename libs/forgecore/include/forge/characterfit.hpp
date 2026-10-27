#pragma once
// Donor-guided character conversion: fit an arbitrary humanoid mesh onto a retail
// Creature's body parts. Native draws each selected part as its own skinned mesh in
// one shared model space (OnAppearanceDraw 0252e720), so the donor parts' bind-pose
// vertices partition the body. Each input triangle goes to the part owning the
// nearest donor vertex; each output vertex takes that part's skin influences by
// inverse-distance blending of its nearest donor vertices. Every part keeps the
// donor's own skeleton (bones, inverse binds, global IDs) untouched.

#include <array>
#include <cstddef>
#include <span>
#include <vector>

#include "forge/meshcompose.hpp"
#include "forge/meshpreview.hpp"

namespace forge::characterfit {

struct Options {
    bool align = true;        // scale to the donor's height, centre XY, stand on its feet
    size_t neighbours = 4;    // donor vertices blended per output vertex (1 = nearest only)
};

struct Part {
    meshpreview::Geometry geometry; // skinned vertices, donor bones, input UVs/normals
    size_t sourceTriangles = 0;
};

struct Fit {
    std::vector<Part> parts;        // parallel to the donor parts; may be empty
    float scale = 1;                // applied to the input before the offset
    std::array<float, 3> offset{};
};

// `donor` are the selected part meshes in bind pose (meshpreview::readLod0 of each
// part's mesh ID); every donor vertex must be skinned. Input primitives are in
// Fable space (meshimport::loadModel). Triangle material = input primitive index.
// Throws on empty input, an unskinned donor or non-finite geometry.
Fit fit(std::span<const meshcompose::Primitive> input,
        std::span<const meshpreview::Geometry> donor, const Options& options = {});

} // namespace forge::characterfit
