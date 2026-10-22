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
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace forge::bwd { class File; }

namespace albion::editor {

struct GtgSection {
    int slot = 0;
    std::string body;      // everything between "NEWMAP n\n" and "ENDMAP\n", verbatim
    std::string headerEol, footerEol; // parsed envelope endings; empty uses the file default
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

// Rebase numeric map slots by case-insensitive level path. Section bodies and
// tail stay verbatim; ambiguous, missing or malformed identities refuse.
std::string remapGtgMapSlots(const std::string& text, const forge::bwd::File& sourceWorld,
                             const forge::bwd::File& composedWorld);
std::string remapGtgMapSlots(const std::string& text,const std::map<int,std::string>& sourceLevels,
                             const forge::bwd::File& composedWorld);

struct GtgMapLayer {
    std::string label, text; // game text; slots already mapped to composedWorld
    bool replace=false, hasHeader=false;
    std::vector<std::string> deleteUids;
};
GtgMapLayer prepareGtgMod(const std::string& label,const std::string& text,
                         const forge::bwd::File& sourceWorld,const forge::bwd::File& composedWorld);
GtgMapLayer prepareGtgMod(const std::string& label,const std::string& text,
                         const std::map<int,std::string>& sourceLevels,const forge::bwd::File& composedWorld);
// Clean a selected complete-file layer, applying only that source's directives.
// A deletion-only layer uses baseline text. No settings prefix reaches the game.
std::string prepareWholeGtgMod(const std::string& text,const std::string& baseline);
struct GtgMapConflict {
    std::string key, map, winner;
    int slot = 0;
    std::vector<std::string> mods;
    std::vector<std::string> removalMods;
    bool overridden = false, choiceUnavailable = false;
};
struct GtgMapMerge {
    std::string text;
    size_t added = 0, changed = 0, deleted = 0;
    std::string replacementMod;
    std::vector<GtgMapConflict> conflicts;
};
// Compose independent map sections against the baseline. Omitted sections are
// partial input, not deletions. Same-map objects compose by UID and retain their
// quest sections. Object choices use <level-path>|uid:<value>; an explicit map
// choice selects the complete section. @tail addresses nonempty trailing
// metadata. Selected blocks and untouched source bytes survive verbatim.
GtgMapMerge mergeGtgMaps(const std::string& baseline, const std::vector<GtgMapLayer>& layers,
                         const forge::bwd::File& composedWorld,
                         const std::map<std::string, std::string>& picks = {},
                         bool reviewChoices = false);

struct RegionEntrance {
    float pos[3] = {0, 0, 0};          // map-local
    float forward[2] = {0, 1};
    std::string startScript;           // the HOLY_SITE_PLAYER_START's ScriptName, if any
};

// The slot's first REGION_ENTRANCE_POINT (and its HSP), if the slot has one.
std::optional<RegionEntrance> entranceOf(const std::filesystem::path& gameRoot, int slot, std::string& error);

// Write (or move) the slot's region entrance: one REGION_ENTRANCE_POINT and one
// HOLY_SITE_PLAYER_START "<levelName>HSP" at `pos` facing `forward` (map-local). An
// existing entrance authored by FableForge (same HSP name) is updated in place; retail ones
// are left alone and a second entrance is appended. One-time .forge-orig backup.
bool setRegionEntrance(const std::filesystem::path& gameRoot, int slot, const std::string& levelName,
                       const float pos[3], const float forward[2], std::vector<std::string>& notes, std::string& error);

}  // namespace albion::editor
