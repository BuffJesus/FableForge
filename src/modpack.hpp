#pragma once
// A FableForge mod pack: a folder with forge_pack.json. Besides any game-root tree it
// carries (data/Levels/... -- merged by the composer like every tree), it holds
// RECIPES: imports the composer re-runs at build time against the tree being built,
// so a new mesh / texture / def gets its ids from whatever the other mods produced
// (appended ids float with the load order; nothing has to be re-pointed by hand).
//
//   {
//     "version": 1, "name": "My pack",
//     "models":       [{"name": "MYCUBE", "model": "assets/cube.glb", "texture": "assets/wood.png",
//                       "donor": "OBJECT_BARREL_UNBREAKABLE", "collision": true}],
//     "groundThemes": [{"name": "GROUND_MY_MOSS", "png": "assets/moss.png", "cliffPng": "",
//                       "donor": "GROUND_GRASS"}]
//   }
// Paths are relative to the pack folder; the source files live under assets/.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace albion::modpack {

inline constexpr const char* kFileName = "forge_pack.json";

struct ModelRecipe {
    std::string name, model, texture, donor = "OBJECT_BARREL_UNBREAKABLE";   // meshimport's default donor
    bool collision = true;
};
struct GroundThemeRecipe {
    std::string name, png, cliffPng, donor = "GROUND_GRASS";
};
struct Pack {
    int version = 1;
    std::string name;
    std::vector<ModelRecipe> models;
    std::vector<GroundThemeRecipe> groundThemes;
};

bool isPack(const std::filesystem::path& folder);
// Throws std::runtime_error on a malformed file.
Pack load(const std::filesystem::path& folder);
void save(const std::filesystem::path& folder, const Pack& pack);

// A new pack folder (forge_pack.json + assets/); false when it exists already.
bool create(const std::filesystem::path& folder, const std::string& name, std::string& error);
// Copy the source files into assets/ and append the recipe. A name the pack already
// has is replaced. The caller adds the folder to the load order.
bool addModel(const std::filesystem::path& folder, ModelRecipe recipe, std::string& error);
bool addGroundTheme(const std::filesystem::path& folder, GroundThemeRecipe recipe, std::string& error);

// The composer's recipe step: every recipe of the pack into outRoot (reading outRoot's
// copy of a bank / game.bin when an earlier layer wrote one, else baseRoot's). A failing
// recipe is reported in `errors` and skipped; the others still apply.
struct ApplyReport {
    std::vector<std::string> added;    // "OBJECT_MYCUBE (mesh 12345)", "GROUND_MY_MOSS (def 4321)"
    std::vector<std::string> notes;
    std::vector<std::string> errors;
};
ApplyReport apply(const std::filesystem::path& folder, const std::filesystem::path& baseRoot, const std::filesystem::path& outRoot);

// The static-map layer: every <pack>/stb/<map>.chunk (+ .record) the editor baked replaces
// that map's chunk + record in outRoot's FinalAlbion_RT.stb (copied from baseRoot first
// when no earlier layer wrote one) -- in place when the size is unchanged, else re-laid.
// Two packs baking different maps both land; the same map: the later one wins (the
// caller reports `maps` per pack to find those).
struct StbReport { std::vector<std::string> maps, errors; };
StbReport applyStaticMaps(const std::filesystem::path& folder, const std::filesystem::path& baseRoot, const std::filesystem::path& outRoot);
// One map's chunk + record into an STB file (the shared writer).
bool writeStaticMapChunk(const std::filesystem::path& stb, const std::string& mapName,
                         const std::vector<uint8_t>& chunk, const std::vector<uint8_t>& record, std::string& error);

} // namespace albion::modpack
