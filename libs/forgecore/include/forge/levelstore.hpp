#pragma once
// Where an install keeps its levels. Retail ships them packed in
// Data\Levels\FinalAlbion.wad. The common modding setup (Project Seasons,
// AlbionSecrets, most modders' dev installs) extracts them to loose files under
// Data\Levels\FinalAlbion\ and renames the WAD (usually to _FinalAlbion.wad) so
// the engine reads the loose copies. Both are real installs; this is the one
// place that tells them apart, so readers and writers stop assuming the WAD.
//
// Read precedence is FableForge's long-standing one: a loose file first, then
// the WAD entry. A renamed WAD is never read or written: the game does not load
// it, so showing its contents would misrepresent what is in the game.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace forge::levelstore {

enum class Kind {
    None,    // no FinalAlbion.wad and no loose .lev files
    Wad,     // FinalAlbion.wad present (loose files may override entries)
    Loose,   // no FinalAlbion.wad; the levels are the loose FinalAlbion\*.lev files
};

struct Layout {
    Kind kind = Kind::None;
    std::filesystem::path levelsDir;   // <root>/data/Levels
    std::filesystem::path looseDir;    // <root>/data/Levels/FinalAlbion
    std::filesystem::path wad;         // <root>/data/Levels/FinalAlbion.wad (empty unless Kind::Wad)
    std::filesystem::path renamedWad;  // a renamed WAD next to it (e.g. _FinalAlbion.wad), informational only
    size_t looseLevels = 0;            // loose .lev files found

    bool valid() const { return kind != Kind::None; }
    bool hasWad() const { return kind == Kind::Wad; }
    bool looseOnly() const { return kind == Kind::Loose; }
    // "FinalAlbion.wad" or "loose FinalAlbion\ files", for messages and labels
    std::string describe() const;
};

Layout detect(const std::filesystem::path& gameRoot);

struct Level {
    std::string stem;   // map name without extension, archive spelling
    uint64_t size = 0;  // .lev bytes
};

// Every level the game can load: the WAD's .lev entries (Kind::Wad) or the loose
// .lev files (Kind::Loose). Sorted case-insensitively by stem.
std::vector<Level> listLevels(const Layout& layout);

// The bytes of FinalAlbion\<fileName> (e.g. "Foo.tng") as FableForge reads them:
// the loose file when it exists, else the WAD entry. nullopt when neither has it.
std::optional<std::vector<uint8_t>> readFile(const Layout& layout, const std::string& fileName);

// readFile or throw "<fileName> is neither loose nor in <describe()>".
std::vector<uint8_t> requireFile(const Layout& layout, const std::string& fileName);

// The loose path FinalAlbion\<fileName> (whether or not it exists).
std::filesystem::path loosePath(const Layout& layout, const std::string& fileName);

} // namespace forge::levelstore
