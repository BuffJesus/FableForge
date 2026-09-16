#pragma once
// Parser for Fable TLC .qst quest-registry files — plain-ASCII scripts of
//   AddQuest("<Name>", TRUE|FALSE);
//   AddTestQuest("<Name>", "<StartHSP>", <int>, "<Display>", "<Ini>", "<End>", "<Card>");
// statements that the engine text-scans at world load ("Load Quests").
// Grammar per docs/QST_FORMAT.md: free-form whitespace inside calls, stray
// junk between statements tolerated (retail FinalAlbion.qst ships with one),
// and // + /* */ comments skipped. The parser is loss-less: every input byte
// lands either in a decoded statement's raw text or in an inter-statement
// gap, so an unmodified file serializes back byte-identically.

#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace forge::qst {

struct Statement {
    std::string keyword;           // "AddQuest" | "AddTestQuest"
    std::vector<std::string> args; // decoded argument strings, quotes stripped
    std::string raw;               // exact source text of the call incl. ';'
    size_t line = 0;               // 1-based line number in the source

    const std::string& name() const { return args.front(); } // arg 1 = quest name
    bool isQuest() const { return keyword == "AddQuest"; }
    // AddQuest arg 2: TRUE = active from game start, FALSE = dormant.
    bool active() const;
};

class File {
public:
    static File parse(const std::filesystem::path& path);
    static File parseText(std::string text, std::string sourceName = {});

    const std::vector<Statement>& statements() const { return statements_; }
    const std::string& source() const { return source_; }
    size_t questCount() const;     // AddQuest statements
    size_t testQuestCount() const; // AddTestQuest statements
    // First AddQuest with this name, or nullptr.
    const Statement* findQuest(std::string_view name) const;

    // Byte-identical to the parsed input while unmodified.
    std::string serialize() const;

    // --- Mutations (each touches only the statement it edits; every other
    // byte round-trips unchanged).

    // Flip the TRUE/FALSE flag of AddQuest <name> in place — only the flag
    // token changes, the statement's whitespace/tab alignment is preserved
    // (ArenaRevisited-style one-token edit). Returns false if absent.
    bool setQuestActive(std::string_view name, bool active);
    // Append `AddQuest("<name>", TRUE|FALSE);` on its own line at EOF (the
    // proven custom-quest registration mechanism). Uses the file's dominant
    // line terminator.
    void addQuest(std::string_view name, bool active);
    // Append an arbitrary statement's source text (e.g. a mod's AddTestQuest,
    // formatting preserved) on its own line at EOF.
    void appendStatement(std::string rawStatement);

private:
    // Alternating document pieces: stmt >= 0 indexes statements_, else `gap`
    // holds verbatim inter-statement bytes (whitespace/comments/junk).
    struct Element {
        ptrdiff_t stmt = -1;
        std::string gap;
    };

    void appendRaw(std::string rawStatement, std::string keyword,
                   std::vector<std::string> args);

    std::string source_;
    std::string lineTerminator_ = "\r\n"; // dominant terminator, from parse
    std::vector<Element> elements_;
    std::vector<Statement> statements_; // in file order
};

// --- Statement-level merge: base + N mods (load order) -> merged file. ------
// Union keyed by quest name: flag flips on existing quests edit the flag token
// in place (retail ordering/formatting preserved), new quests append at end in
// their mod's original formatting; new AddTestQuest statements append too.
// Removals are NOT applied (conservative). Same-name flag conflicts resolve by
// load order (later mod wins) unless overridden per quest via `picks`
// (quest name -> winning mod label, or "vanilla" to keep base).

struct MergeInput {
    std::string label; // mod name, matched against picks
    const File* file = nullptr;
};

struct MergeConflict {
    std::string name;                                  // quest name
    std::vector<std::pair<std::string, bool>> wanted;  // (mod, flag) in load order
    std::string winner;                                // mod label or "vanilla"
    bool overridden = false;                           // decided by a pick
};

struct MergeResult {
    File merged;
    size_t applied = 0;      // flag flips + additions taken
    size_t addedQuests = 0;  // new AddQuest statements appended
    size_t addedTests = 0;   // new AddTestQuest statements appended
    std::vector<MergeConflict> conflicts;
};

MergeResult merge(const File& base, const std::vector<MergeInput>& mods,
                  const std::map<std::string, std::string>& picks = {});

} // namespace forge::qst
