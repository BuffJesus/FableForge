#pragma once
// Rigged glTF export of one skinned retail mesh, the inverse of meshimport's
// skinned loader: glTF Y-up metres, (x, y, z)_fable = 100 * (x, -z, y)_gltf.
// Bones become nodes at their bind pose; inverseBindMatrices are the mesh's own
// converted with C^-1 * M * C so inverseBindInFable() returns them; JOINTS_0 are
// local bone slots and WEIGHTS_0 normalized bytes summing to exactly 255.

#include <cstdint>
#include <filesystem>
#include <vector>

#include "foliageexport.hpp"

namespace albion::skinnedexport {

// `mesh` comes from foliageexport::makeMesh (material parts and images). Throws
// for meshes without bones, unskinned vertices, duplicate bone names or
// non-invertible binds: no partial rig is written.
std::vector<uint8_t> build(const foliageexport::Mesh& mesh, const std::vector<terrainexport::Image>& images);
std::vector<std::filesystem::path> write(const foliageexport::Mesh& mesh,
                                         const std::vector<terrainexport::Image>& images,
                                         const std::filesystem::path& out);

} // namespace albion::skinnedexport
