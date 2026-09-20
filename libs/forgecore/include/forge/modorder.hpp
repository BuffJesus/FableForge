#pragma once
// forge::modorder -- the persisted load order of a Fable install's mods (0.20 Mod packs).
//
// `<gameRoot>/forge_mods.json` lists the packs in load order (first = lowest priority, the
// last word wins), each with a name, its source (an .fmp, a bsdiff .patch, a game-root tree,
// an EgoCore Mods/<Name>/ folder), the sha256 of that source (identity: a re-downloaded or
// edited pack is a different pack) and an enabled flag. `forge-tools mods add/remove/order/
// list` edit it; `mods build` composes the enabled sources in order onto the retail baseline
// with modsMerge. Nothing here touches the game files themselves.
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace forge::modorder {

enum class Kind { Fmp, Patch, Tree, EgoCore, Qst, Unknown };
const char* kindName(Kind k);
// By the source's shape: *.fmp, *.patch, *.qst, a folder with Data/ (tree) or <Name>.dll (EgoCore).
Kind classify(const std::filesystem::path& source);

struct Entry {
    std::string name;
    std::string source;      // path as given (absolute, or relative to the game root)
    Kind kind = Kind::Unknown;
    std::string sha256;      // of the source file, or of the folder's file list + contents
    bool enabled = true;
    std::string note;        // free text (version, origin URL ...)
};

struct Order {
    int version = 1;
    std::vector<Entry> mods;
};

std::filesystem::path orderPath(const std::filesystem::path& gameRoot);
Order load(const std::filesystem::path& gameRoot);                 // empty order when absent
void save(const std::filesystem::path& gameRoot, const Order& order);

// Hex sha256 of a file, or of a folder (every regular file's relative path + bytes, sorted).
std::string sha256Of(const std::filesystem::path& p);

// Edits. `add` appends (or inserts at `at` when >= 0); a source already listed (same sha256)
// is refused. `remove` and `move` address an entry by name or 0-based index; both throw
// std::runtime_error with a reason when the entry is not there.
Entry& add(Order& order, const std::filesystem::path& gameRoot, const std::filesystem::path& source, const std::string& name = "", int at = -1);
void remove(Order& order, const std::string& nameOrIndex);
void move(Order& order, const std::string& nameOrIndex, int to);
void setEnabled(Order& order, const std::string& nameOrIndex, bool enabled);
int indexOf(const Order& order, const std::string& nameOrIndex);   // -1 when absent

// The sources modsMerge takes, enabled entries in order, resolved against the game root.
std::vector<std::string> buildSources(const Order& order, const std::filesystem::path& gameRoot);
// the names of the enabled mods, parallel to buildSources (the labels every merge report row carries)
std::vector<std::string> buildLabels(const Order& order);

} // namespace forge::modorder
