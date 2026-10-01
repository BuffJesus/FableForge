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
#include <functional>
#include <span>
#include <set>
#include <string>
#include <vector>

#include "forge/lipsync.hpp"

namespace albion::modpack {

inline constexpr const char* kFileName = "forge_pack.json";

struct ModelRecipe {
    std::string name, model, texture, donor = "OBJECT_BARREL_UNBREAKABLE";   // meshimport's default donor
    bool collision = true;
};
struct GroundThemeRecipe {
    std::string name, png, cliffPng, donor = "GROUND_GRASS";
};
struct LipSyncRecipe {
    std::string language,bank;
    uint32_t soundId=0;
    forge::lipsync::Entry value;
};
struct Pack {
    int version = 1;
    std::string name;
    std::vector<ModelRecipe> models;
    std::vector<GroundThemeRecipe> groundThemes;
    std::vector<LipSyncRecipe> lipSync;
    // masters: the names (load-order names or pack names) of the mods this one builds on --
    // a level pack painting another pack's ground theme, a quest pack using a pack's level.
    // They must be in the order, enabled, and load before it.
    std::vector<std::string> masters;
};

// The problems with `pack`'s masters in an order given as (name, pack name, enabled) in
// load order; `self` is the pack's own position. Empty = fine.
struct OrderEntry { std::string name, packName; bool enabled = true; };
std::vector<std::string> masterProblems(const Pack& pack, size_t self, const std::vector<OrderEntry>& order);

bool isPack(const std::filesystem::path& folder);
// Throws std::runtime_error on a malformed file.
Pack load(const std::filesystem::path& folder);
// Staged manifest replacement; throws on write/commit failure, preserving the
// previous manifest on a reported failure (recovery path reported if rollback fails).
void save(const std::filesystem::path& folder, const Pack& pack);

// A new pack folder (forge_pack.json + assets/); false when it exists already.
bool create(const std::filesystem::path& folder, const std::string& name, std::string& error);
// Stage source files under assets/<kind>/<recipe>/<role>/ and append the recipe.
// A recipe name the pack already has is replaced; other recipes keep their files.
// Source paths are filesystem paths; saved paths are relative to the pack.
// Reported failures roll back prior asset/manifest replacements. The caller adds
// the folder to the load order. Legacy flat asset paths remain readable.
bool addModel(const std::filesystem::path& folder, ModelRecipe recipe, std::string& error);
bool addGroundTheme(const std::filesystem::path& folder, GroundThemeRecipe recipe, std::string& error);
// Stage one language's edited records as manifest recipes. Later packs overlay
// only their named bank/ID records during composition, so unrelated lines mix.
bool addLipSync(const std::filesystem::path& folder,const std::string& language,
                std::span<const forge::lipsync::ArchiveEdit> edits,std::string& error);

// The composer's recipe step: every recipe of the pack into outRoot (reading outRoot's
// copy of a bank / game.bin when an earlier layer wrote one, else baseRoot's). A failing
// recipe is reported in `errors` and skipped; the others still apply.
struct ApplyReport {
    std::vector<std::string> added;    // "OBJECT_MYCUBE (mesh 12345)", "GROUND_MY_MOSS (def 4321)"
    std::vector<std::string> notes;
    std::vector<std::string> errors;
};
ApplyReport apply(const std::filesystem::path& folder, const std::filesystem::path& baseRoot,
                  const std::filesystem::path& outRoot,
                  const std::set<std::string>& skipLipLanguages = {});

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

// A shadow install's differences from the base, written into a pack as the layers the
// composer applies: FinalAlbion.wld / .bwd (merged per record), every level file the
// shadow's WAD or loose folder has new or changed (data/Levels/FinalAlbion/), and every
// static map whose chunk is new or changed (stb/<map>.chunk + .record). The editor runs a
// world edit (new level, map move, region edit) against a shadow copy of the containers
// and captures it this way; `forge-tools mods capture` is the same step.
struct CaptureReport { std::vector<std::string> files, maps, errors; };
// The shadow a world edit into `pack` runs against: gameRoot's world containers (and
// defs) copied to `shadow`, with the pack's current world files, level files and static
// maps laid over them -- so edits into one pack accumulate. `viewOnly` copies just the
// FinalAlbion.bwd / .wld (all the World view reads; kilobytes, not the 800 MB of
// containers).
bool prepareShadow(const std::filesystem::path& gameRoot, const std::filesystem::path& pack,
                   const std::filesystem::path& shadow, bool viewOnly, std::string& error);
CaptureReport capture(const std::filesystem::path& shadowRoot, const std::filesystem::path& baseRoot,
                      const std::filesystem::path& pack);

// A world edit into a pack: prepareShadow (full) in an operation-owned temp folder, `op` against it,
// capture the result into the pack, remove the shadow. `op` returns false with its own
// error to abort (nothing is captured then).
bool intoPack(const std::filesystem::path& gameRoot, const std::filesystem::path& pack,
              const std::function<bool(const std::filesystem::path& shadowRoot, std::string& error)>& op,
              std::vector<std::string>& notes, std::string& error);
// The process-owned temp folder the World view of a pack reads (prepareShadow viewOnly).
std::filesystem::path viewShadowRoot();

} // namespace albion::modpack
