#pragma once
// forge::egocore -- an EgoCore mod folder (Mods/<Name>/<Name>.dll [+ Data/Defs/*.def text
// overrides, <Name>_Info.txt]) as one more pack type of the composer (0.20 step 8).
//
// The DLL part is a file copy plus one `[Mods]` line in the game root's Mods.ini, exactly the
// way EgoCore's own ModManager writes it (`Name\Name.dll=1`), so FSE_Launcher loads it and
// EgoCore's UI still lists it. The def part is text: each `#definition ... #end_definition`
// block in the mod's .def replaces the same-typed, same-named block of the retail text tree
// (EgoCore's MergeDefFile), the tree is compiled twice with `defc` (jamen/fable-defs, the
// byte-deterministic compiler EgoCore itself embeds) -- once plain, once with the overlay --
// and the records that differ become a field-level layer applied onto the RETAIL records
// (the compiled bins are not retail's: the text tree predates TLC and defc's record header
// byte differs, so only the changed fields travel).
#include <filesystem>
#include <string>
#include <vector>

namespace forge::egocore {

struct Paths {
    std::filesystem::path defc;       // the compiler; empty = FORGE_DEFC, then "defc.exe" beside the running exe, then PATH
    std::filesystem::path defsText;   // the retail text Data/Defs tree; empty = FORGE_DEFS_TEXT, then <gameRoot>/Data/Defs
    std::filesystem::path schema;     // def_schema.json for the field-level diff
};

struct Report {
    std::string modName;
    bool hasDll = false;
    size_t defFiles = 0, blocksReplaced = 0, blocksAdded = 0;
    size_t recordsChanged = 0, fieldsApplied = 0, recordsNew = 0, recordsSkipped = 0;
    size_t resourceReplaced = 0, resourceAdded = 0;   // bank entries from .resource overrides
    std::vector<std::string> resourceBanks;           // the banks written (data/... relative)
    std::vector<std::string> notes;
};

// The mod's def overrides as a game-root layer: <outRoot>/data/CompiledDefs/{game.bin,names.bin}
// = the retail bins with the changed fields applied. Returns false (with notes) when the mod has
// no Data/Defs, or the compiler / text tree cannot be found.
bool normaliseDefs(const std::filesystem::path& modFolder, const std::filesystem::path& gameRoot,
                   const std::filesystem::path& outRoot, const Paths& paths, Report& report);

// Copy Mods/<Name>/ into <outRoot>/Mods/<Name>/ (everything but Data/, which the layer above
// consumed) and register the DLL in <outRoot>/Mods.ini, merging with <gameRoot>/Mods.ini's
// existing lines (the FSE core line is kept first).
void installDll(const std::filesystem::path& modFolder, const std::filesystem::path& gameRoot,
                const std::filesystem::path& outRoot, Report& report);

// Merge the mod's block(s) into a copy of `target` text (EgoCore's MergeDefFile): same type+name
// replaces, else appends. Returns the merged text; counts in the report.
std::string mergeDefText(const std::string& target, const std::string& mod, Report& report);
std::string mergeDefText(const std::string& target, const std::string& mod, Report& report, std::vector<std::string>* addedBlocks);

std::filesystem::path findDefc(const Paths& paths);

// The mod's asset overrides as bank layers (EgoCore's ModBankPatcher convention): every
// `Data/<path>/<bank>.big/[<SubBank>/]<Entry>.resource` [+ `.header` = the entry's info block]
// replaces that entry's payload (and info) in a copy of the bank, or appends a new entry with the
// next id when the bank has no such name. The bank is read from <outRoot> when an earlier layer of
// the same build already wrote it, else from <gameRoot>, and written to <outRoot>/data/<path>.
// Returns the number of overrides applied (0 = the mod ships none).
size_t applyResourceOverrides(const std::filesystem::path& modFolder, const std::filesystem::path& gameRoot,
                              const std::filesystem::path& outRoot, Report& report);

} // namespace forge::egocore
