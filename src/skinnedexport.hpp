#pragma once
// Rigged glTF export of one skinned retail mesh, the inverse of meshimport's
// skinned loader: glTF Y-up metres, (x, y, z)_fable = 100 * (x, -z, y)_gltf.
// Bones become nodes at their bind pose; inverseBindMatrices are the mesh's own
// converted with C^-1 * M * C so inverseBindInFable() returns them; JOINTS_0 are
// local bone slots and WEIGHTS_0 normalized bytes summing to exactly 255.

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "foliageexport.hpp"
#include "forge/meshcompose.hpp"

namespace albion::skinnedexport {

// Empty when `geometry` can be exported rigged, else the reason it cannot
// (no bones, an unskinned vertex, repeated bone names such as the retail
// IGNORE/EMITTER_ dummies on balverines and trolls). Callers fall back to static.
std::string unsupportedReason(const forge::meshpreview::Geometry& geometry);
// `mesh` comes from foliageexport::makeMesh (material parts and images). Throws
// for meshes without bones, unskinned vertices, duplicate bone names or
// non-invertible binds: no partial rig is written.
std::vector<uint8_t> build(const foliageexport::Mesh& mesh, const std::vector<terrainexport::Image>& images);
std::vector<std::filesystem::path> write(const foliageexport::Mesh& mesh,
                                         const std::vector<terrainexport::Image>& images,
                                         const std::filesystem::path& out);

// For fitted geometry: `slotOfMaterial[m]` is the input material slot behind mesh
// material m (characterfit materials = input primitives). Give each mesh part that
// slot's decoded base-colour image, appending decoded
// images to `images` once per material slot (`decoded` caches slot -> image index).
// Undecodable images are skipped with a warning; parts stay untextured.
void attachInputTextures(foliageexport::Mesh& mesh, std::vector<terrainexport::Image>& images,
                         const std::vector<int>& slotOfMaterial,
                         const std::vector<std::vector<uint8_t>>& materialImages,
                         std::map<int, int>& decoded, std::vector<std::string>& warnings);

} // namespace albion::skinnedexport
