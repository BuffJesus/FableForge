// FinalAlbion.gtg: the "global things" file, one TNG-shaped section per WLD map slot
// (region entrance points, holy-site player starts, global markers / cameras). The map
// screen and quest teleports drop the hero on a slot's REGION_ENTRANCE_POINT, and a
// HOLY_SITE_PLAYER_START named "<Level>HSP" is the retail convention for its start.
//
// Format (CRLF on retail, slots ascending, not every slot present; shown with \n):
//   NEWMAP <slot>\nVersion 2;\n\nXXXSectionStart NULL;\n\n<things>XXXSectionEnd;\n\n\nENDMAP\n
// and an empty slot is just  NEWMAP <slot>\nVersion 2;\n\nENDMAP\n
// Parsing keeps every byte of the sections it does not touch (round-trips retail).
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace albion::editor {

struct GtgSection {
    int slot = 0;
    std::string body;      // everything between "NEWMAP n\n" and "ENDMAP\n", verbatim
};

struct GtgFile {
    std::vector<GtgSection> sections;   // ascending slot order as on disk
    std::string tail;                   // bytes after the last ENDMAP (normally empty)
    std::string eol = "\r\n";           // the file's line ending (retail is CRLF); new blocks use it
    static GtgFile parse(const std::string& text);
    std::string serialize() const;
    const GtgSection* find(int slot) const;
    GtgSection& sectionFor(int slot);   // existing, or inserted in slot order (empty body)
    uint64_t maxUid() const;
};

struct RegionEntrance {
    float pos[3] = {0, 0, 0};          // map-local
    float forward[2] = {0, 1};
    std::string startScript;           // the HOLY_SITE_PLAYER_START's ScriptName, if any
};

// The slot's first REGION_ENTRANCE_POINT (and its HSP), if the slot has one.
std::optional<RegionEntrance> entranceOf(const std::filesystem::path& gameRoot, int slot, std::string& error);

// Write (or move) the slot's region entrance: one REGION_ENTRANCE_POINT and one
// HOLY_SITE_PLAYER_START "<levelName>HSP" at `pos` facing `forward` (map-local). An
// existing entrance authored by Atlas (same HSP name) is updated in place; retail ones
// are left alone and a second entrance is appended. One-time .atlas-orig backup.
bool setRegionEntrance(const std::filesystem::path& gameRoot, int slot, const std::string& levelName,
                       const float pos[3], const float forward[2], std::vector<std::string>& notes, std::string& error);

}  // namespace albion::editor
