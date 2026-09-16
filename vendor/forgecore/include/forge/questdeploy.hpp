#pragma once
// One-command quest deployment for FSE / ForgeFSE game roots.
//
// An FSE quest needs THREE things to actually run (proven by the ForgeFSE
// live test, docs/HANDOFF.md checkpoint):
//   1. the compiled Lua script at   <root>/FSE/<Quest>/<Quest>.lua
//   2. an entry in the Quests table of   <root>/FSE/quests.lua
//   3. an   AddQuest("<Quest>", TRUE|FALSE);   line in
//      <root>/data/Levels/FinalAlbion.qst — the engine's activation list.
//      Missing this = the quest registers but never runs, and a Steam
//      integrity verify silently wipes custom lines from the retail .qst.
//
// This module plans/applies all three edits atomically-ish (backups first),
// and `doctor` audits the three-part invariant for every registered quest.
//
// quests.lua is edited TEXTUALLY: the Quests table is parsed just enough to
// locate each `Key = { ... }` entry; append/replace splices only that entry's
// bytes, so the rest of the file round-trips byte-for-byte (same philosophy
// as forge::qst).

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "forge/fse.hpp"
#include "forge/questnodes.hpp"

namespace forge::questdeploy {

// --- quests.lua (FSE quest registry) ----------------------------------------

struct EntityScriptRef {
    std::string name;
    std::string file;  // FSE-relative, no .lua extension
    long long id = 0;
};

struct QuestRef {
    std::string key;   // Lua table key of the entry
    std::string name;  // name = "..." field (registration name)
    std::string file;  // file = "..." field, FSE-relative, no .lua extension
    long long id = 0;
    bool hasId = false;
    bool master = false;
    std::vector<EntityScriptRef> entityScripts;
    // Byte range [begin, end) of `Key = { ... }` in the source text.
    size_t begin = 0, end = 0;
    // Raw byte range of the `entity_scripts = { ... }` field (0,0 if absent);
    // preserved verbatim when the entry is replaced.
    size_t esBegin = 0, esEnd = 0;
};

class QuestsLua {
public:
    static QuestsLua parse(const std::filesystem::path& path);
    // Throws std::runtime_error if no top-level `Quests = { ... }` table is
    // found or an entry is not a table constructor.
    static QuestsLua parseText(std::string text, std::string sourceName = {});

    const std::string& source() const { return source_; }
    const std::vector<QuestRef>& quests() const { return quests_; }
    const QuestRef* find(std::string_view name) const;  // by name field / key

    // Smallest id >= min not taken by any quest or entity script.
    long long nextFreeId(long long min = 50000) const;

    // Full file text with quest <name> appended (before the Quests table's
    // closing brace, install-style 4/8-space indent) or, when an entry with
    // that name/key exists, its bytes replaced in place (an existing
    // entity_scripts field is kept verbatim). Every other byte is preserved.
    std::string withQuest(const std::string& name, const std::string& file,
                          long long id) const;

private:
    std::string source_;
    std::string eol_ = "\n";  // dominant line terminator
    size_t tableOpen_ = 0, tableClose_ = 0;  // braces of `Quests = { ... }`
    std::vector<QuestRef> quests_;
};

// --- deploy ------------------------------------------------------------------

struct FileChange {
    std::filesystem::path path;
    bool existed = false;
    std::string before;  // empty when !existed
    std::string after;
    bool changed() const { return !existed || before != after; }
};

struct DeployOptions {
    long long id = -1;    // < 0 = keep the existing entry's id, else auto-pick
                          //       the next free id >= 50000
    int activeFlag = -1;  // -1 = keep existing .qst flag (new quests: FALSE),
                          //  0 = force FALSE (dormant), 1 = force TRUE (active)
    // When set: manifest-overlay registry + validation warnings, like
    // `forge quest compile --manifest`.
    const fse::Manifest* manifest = nullptr;
};

struct DeployPlan {
    std::string questName;
    long long id = 0;
    bool idWasAuto = false;
    bool active = false;                 // resulting .qst flag
    std::vector<std::string> warnings;   // compile/manifest warnings
    FileChange script;    // <root>/FSE/<Quest>/<Quest>.lua
    FileChange registry;  // <root>/FSE/quests.lua
    FileChange qst;       // <root>/data/Levels/FinalAlbion.qst
};

// Compile the graph and compute all three file edits WITHOUT writing anything.
// Throws std::runtime_error on compile errors, a missing quests.lua / .qst
// (not an FSE game root), a non-identifier quest name, or an --id collision.
DeployPlan planDeploy(const questnodes::Graph& graph,
                      const std::filesystem::path& gameRoot,
                      const DeployOptions& options = {});

// Write every changed file from the plan; files that already existed are
// first copied to "<file>.bak". Creates the quest's script directory.
// Returns the paths written (skips unchanged files).
std::vector<std::filesystem::path> applyDeploy(const DeployPlan& plan);

// --- doctor ------------------------------------------------------------------

struct DoctorIssue {
    std::string quest;    // quest name ("" for root-level problems)
    std::string problem;  // what is broken
    std::string fix;      // how to fix it
};

struct DoctorReport {
    std::filesystem::path questsLuaPath;
    std::filesystem::path qstPath;
    size_t questsChecked = 0;
    std::vector<DoctorIssue> issues;
    bool healthy() const { return issues.empty(); }
};

// Audit the three-part invariant for every quest in <root>/FSE/quests.lua:
// Lua file exists (quest + entity scripts), AddQuest line present in
// FinalAlbion.qst, ids unique. The "Steam verify broke my quests" diagnosis.
DoctorReport doctor(const std::filesystem::path& gameRoot);

} // namespace forge::questdeploy
