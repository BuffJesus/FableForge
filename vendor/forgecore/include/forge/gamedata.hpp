#pragma once
// forge::gamedata -- the live data spine. Surfaces the COMPLETE, mod-aware lists
// of creatures / objects / regions straight from the game's own def tables, so
// authoring pickers (CLI and GUI) never depend on a hand-curated, stale list.
//
// This replaces FQT's hardcoded GameData.cs (~27 creatures, ~43 regions): reading
// a real retail game.bin yields ~508 creatures and ~2781 objects, and it reflects
// whatever mods have been applied to the install because it reads the live bin.
//
// Sources:
//   - creatures/objects: game.bin entries grouped by their `definition` category
//     ("CREATURE" / "OBJECT"), NULLDEF_* placeholders skipped.
//   - regions: a .wld's region names (the short "Oakvale" form quests bind via
//     AddQuestRegion, not the REGION_* def name).

#include <filesystem>
#include <string>
#include <vector>

#include "forge/bin.hpp"
#include "forge/wld.hpp"

namespace forge::gamedata {

struct Catalog {
    std::vector<std::string> creatures;  // definition == "CREATURE"
    std::vector<std::string> objects;    // definition == "OBJECT"
    std::vector<std::string> inventoryItems; // OBJECTs with CInventoryItemDef component
    std::vector<std::string> regions;    // wld regionName values
};

// Group def entries by category into sorted, de-duplicated name lists (NULLDEF_*
// and empty names skipped). Pure -- no file I/O, so unit-testable with synthetic
// entries.
Catalog fromEntries(const std::vector<bin::Entry>& entries);

// Fill `out.regions` from a wld region set (sorted, de-duplicated).
void addRegions(Catalog& out, const std::vector<wld::Region>& regions);

// Live read: creatures/objects from <gameRoot>/data/CompiledDefs/{game,names}.bin,
// and (if `wldPath` is given) regions from that .wld. Throws on missing bins.
Catalog readGameRoot(const std::filesystem::path& gameRoot,
                     const std::filesystem::path& wldPath = {});

}  // namespace forge::gamedata
