#pragma once
// Record-level merge of the world containers across mods: several mods that each
// ship a FinalAlbion.bwd (and .wld) -- one adds a level, one moves a map, one
// takes over a filler region -- all land, instead of the last whole file winning.
//
// The BWD is what the engine loads (UseCompiledWorldFiles, the retail default;
// see forge/bwd.hpp), so the merge works on its records:
//   * a base map / region is keyed by its SLOT -- mods append new records and
//     re-purpose regions in place (worldinstall's takeover keeps the slot), so a
//     base slot means the same record in every mod;
//   * a map a mod appends is keyed by its level name, a region by its name, and
//     gets the next free slot in load order;
//   * every field a mod changes against the base is applied (a later mod
//     changing the same field differently wins, and is reported);
//   * a region's contains / sees lists are diffed as sets of LEVEL NAMES (the
//     slots differ between mods) and the adds / removes applied, then resolved to
//     the merged slots.
// The WLD text is then brought in step with the merged BWD through forge::wld's
// line-precise edits (the tools that read the text keep working).

#include <string>
#include <vector>

#include "forge/bwd.hpp"
#include "forge/wld.hpp"

namespace forge::worldmerge {

struct Layer {
    std::string label;   // the mod's name in every report row
    bwd::File bwd;
};

struct Report {
    std::vector<std::string> added;      // "map Foo (slot 400) from PackA"
    std::vector<std::string> changed;    // "map Bar: box from PackB"
    std::vector<std::string> conflicts;  // "map Bar box: PackA -> PackB (the later wins)"
    std::vector<std::string> notes;      // not mirrored into the WLD, dropped references, ...
    bool empty() const { return added.empty() && changed.empty(); }
};

// base + every layer's changes against base, in order.
bwd::File merge(const bwd::File& base, const std::vector<Layer>& layers, Report& report);

// The base WLD with the merged BWD's differences from the base BWD applied
// (moves, new maps, new regions, region texts, contains / sees).
wld::File mirror(const wld::File& baseWld, const bwd::File& baseBwd, const bwd::File& merged, Report& report);

// "Data\\Levels\\FinalAlbion\\X.lev" -> "FinalAlbion\\X.lev" (the WLD's LevelName form)
std::string wldLevelName(const std::string& bwdLevelName);

} // namespace forge::worldmerge
