#pragma once
// Ground-theme palette integrity: keep a LEV's theme slots pointing at the right
// game.bin defs for the install it will actually be loaded by.
//
// A LEV palette slot stores a GLOBAL def entry index (see forge/lev.hpp). That
// index is position-dependent, so a level authored against one game.bin and
// shipped against another silently addresses the wrong def. Our own authored
// terrain did exactly that -- ForgeTest64_final.lev and the AshfallHollow.lev
// cloned from it stored indices that landed on CREATURE defs, which read in
// bounds and produce plausible garbage rather than a crash.
//
// The fix is mechanical: resolve each slot's NAME against the target install and
// write that index back. Names are stable; indices are not. Rebasing therefore
// belongs on the save/bake path, and the audit belongs in validation.

#include <filesystem>
#include <string>
#include <vector>

#include "forge/bin.hpp"
#include "forge/lev.hpp"

namespace forge::themepalette {

inline constexpr char kThemeDefType[] = "ENGINE_THEME";

// Adapt a loaded def bank into the resolver forge::lev asks for. The returned
// table borrows `defs`; it must not outlive it.
lev::DefIndexTable makeDefIndexTable(const bin::File& defs);

// Open <gameRoot>/data/CompiledDefs/{names.bin,<binName>}.
bin::File openDefBank(const std::filesystem::path& gameRoot,
                      const std::string& binName = "game.bin");

struct LevelReport {
    std::filesystem::path path;
    int namedSlots = 0;
    std::vector<lev::ThemePaletteIssue> issues; // wrong or unresolvable slots
    int wrong = 0;        // resolvable, but pointing at the wrong index
    int unresolvable = 0; // the target install has no theme with that name
    bool clean() const { return wrong == 0 && unresolvable == 0; }
};

// Audit one level. Never writes.
LevelReport auditLevel(const std::filesystem::path& levelPath,
                       const lev::DefIndexTable& defs);

// Audit, then rewrite every resolvable wrong slot and save in place. Returns the
// report as it was BEFORE the rewrite, so callers can report what they fixed.
// Unresolvable slots are left untouched and still reported -- inventing an index
// would turn a loud validation failure into a quiet wrong one.
LevelReport rebaseLevel(const std::filesystem::path& levelPath,
                        const lev::DefIndexTable& defs);

// One line per issue, e.g.
//   slot   2  GROUND_GRASS_NO_LOCAL_DETAIL  stored 1689 -> 1917  (was CREATURE)
std::string formatIssue(const lev::ThemePaletteIssue& issue);

} // namespace forge::themepalette
