#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>
#include "forge/boneconfig.hpp"
#include "forge/creatureappearance.hpp"
#include "forge/meshpreview.hpp"

namespace forge::creatureassembly {
// Caller-owned resolution; every callback throws when its input is unavailable.
struct Sources {
    std::function<std::string(uint32_t nameOffset)> skeletalName; // names-pool boundary -> "x.bncfg"
    std::function<std::string(const std::string& name)> boneConfig; // data/Bones/<name> text
    std::function<meshpreview::Geometry(uint32_t mesh)> mesh;      // decoded LOD0
};
struct Part {
    size_t body = 0, alternative = 0;
    uint32_t mesh = 0;
    std::vector<creatureappearance::TextureReplacement> textures; // part entries, then globals
    std::vector<std::array<float,3>> boneScales;                  // mesh-local slot order
    size_t scaledBones = 0;                                       // slots with a non-identity scale
    meshpreview::Geometry posed;                                  // bind pose, morph-scaled, skinned
};
struct Assembly {
    creatureappearance::Selection selection;
    std::vector<std::string> skeletalTargets; // selection order
    std::vector<boneconfig::Bone> combined;
    std::vector<Part> parts;
};
// Bind-pose preview of one random-appearance seed. Native OnAppearanceDraw
// (0252e720) creates one morphed animating mesh per selected part, all sharing
// the thing's animation/position/orientation interpolators, so parts are posed
// in a shared model space with no part-local transform. Each part maps the
// combined skeletal morph through its own bone name CRCs (GetBoneScalings is per
// mesh ID). An appearance without substituted parts, a missing mesh/target or a
// mesh whose stored bone CRC disagrees with its name refuses: no partial NPC.
// Not animation: bind keyframes only; normals use the skin matrix, not its
// inverse transpose, so nonuniformly scaled normals are approximate.
Assembly assemble(std::span<const uint8_t> creaturePrefix,uint32_t seed,bool decapitated,
                  const Sources& sources);
// One preview geometry from all posed parts (shared model space). Each part's
// composed replacements retarget its materials' diffuse and bump IDs: native
// GetDiffuseTexture/GetBumpTexture both consult the composed source map (one
// lookup, no chasing). Other texture channels are left as authored. Skeletons
// are dropped; vertices are already skinned.
meshpreview::Geometry merge(const Assembly& assembly);
} // namespace forge::creatureassembly
