#pragma once
// Reader for Fable: The Lost Chapters runtime save games ("FableSave!").
// Format reverse-engineered by the FableTLC decomp agent (docs/SAVEGAME_FORMAT.md):
//   [10] "FableSave!"  [2] pad  [u32 @0x0C signature]  [u32 @0x10 chunk0 ulen]
//   [u32 @0x14 chunk1 ulen]  [u32 @0x18 chunk0 clen]  [chunk0 zlib @0x1C]
//   [u32 chunk1 clen]  [chunk1 zlib]  [16-byte trailer]  [zero pad to 300 KB]
// Inflated chunks are a CPersistContext binary stream of named sections
//   [name\0][u32 sectionLen][field]*, each field [u32 seed-0 CRC(name)][value]
// with the same value encodings as game.bin (int/float=4B, bool=1B,
// CCharString=NUL-terminated, wide=UTF-16 NUL-terminated).
//
// SCOPE: reads the validated 23-field HEADER section (world/region/save meta +
// gameplay flags) AND the hero live-stats (gold/morality/age/renown). The hero
// stats live one zlib layer deeper: chunk1's inflated SAVED_ENTITIES holds N
// independently-zlib-compressed cells; the cell containing `CTCHeroStats\0` has
// the tagged stat fields ([seed-0 CRC tag][value]) in a bounded window after it
// (docs/SAVE_HERO_STATS.md). READ-ONLY.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace forge::save {

struct Field {
    std::string name;
    std::string type;   // "string" | "wstring" | "int32" | "float" | "bool" | "range3"
    std::string value;  // rendered
    bool tagOk = false; // tag == seed-0 CRC(name)
};

struct File {
    uint32_t signature = 0;
    size_t fileSize = 0;
    size_t chunk0Ulen = 0;
    size_t chunk1Ulen = 0;
    std::string sectionName;      // "HEADER"
    std::vector<Field> header;    // decoded HEADER fields
    std::vector<Field> heroStats; // CTCHeroStats: Money/Morality/Age/Fatness/Renown
    bool allTagsOk = false;

    static File read(const std::filesystem::path& path);
};

} // namespace forge::save
