#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace forge::defexternal {

struct Review {
    bool definitionIndependent = false;
    std::string reason;
    // One-based source line for text refusals; zero when not applicable.
    // EOF structural failures use the line following the final raw line.
    size_t line = 0;
    std::string directive;
};

// Review complete known BIG entry layouts for independence from game.bin IDs
// and names.bin offsets. No raw-value absence or file hash implies acceptance.
// Unknown banks/types, opaque layouts and malformed inputs return false.
// Scope: supported PC textures, English text, compiled meshes, 3DAF animation
// and 3DMF physics.
// Does not certify archive metadata, other entries, asset-ID links, or a source
// installation. A composer must account for every external file separately.
Review reviewBigEntry(std::string_view bank, uint32_t type,
                      const std::vector<uint8_t>& subHeader,
                      const std::vector<uint8_t>& payload);

// Observed loose lighting-table TGA subset: uncompressed 24-bit BGR, no ID or
// colour map, zero origin/descriptor, and a standard footer with no extension
// or developer data. Validates the entire file. Pixels are colour channels;
// environment filename/row/column bindings and source admission remain separate.
Review reviewLightingTga(const std::vector<uint8_t>& payload);

// Complete compiled-world storage has strings, coordinates, UIDs and local
// map slots, not game.bin indices or names.bin offsets. This check validates
// its known framing and local slot bounds; symbolic region/asset names still
// require resolution by the caller. It does not admit an installation.
Review reviewBwd(const std::vector<uint8_t>& payload);

// Strict line-oriented WLD subset: every directive, value and enclosing block
// must have a known domain. Unknown syntax refuses even when the permissive
// editor parser can round-trip it. Symbolic names and paths still need caller
// resolution; this does not certify world topology or runtime behavior.
Review reviewWld(std::string_view text);

// Strict TNG subset: Thing/Object/Marker/Building/Village/Holy Site/TrackNode/
// Switch/AICreature types with
// appropriate base/physical fields, null ScriptData and explicitly reviewed
// component layouts. Unknown thing types, components, fields and syntax refuse.
// Definition names remain symbolic
// and require resolution; this is not complete coverage of shipped TNG/GTG.
Review reviewTng(std::string_view text);

struct TngDefinitionField {
    std::string thingType;
    std::string component; // Empty for top-level thing fields.
    std::string field; // Preserves indexed field spelling.
    std::string name;
    size_t line = 0; // One-based source line.
    int32_t worldMapSlot = 0; // Positive for a GTG body; zero for standalone TNG.
    bool conditional = false; // Potential query; runtime branch execution is not proven.
};
struct TngDefinitionInventory {
    Review storage;
    std::vector<TngDefinitionField> fields;
};
inline constexpr std::string_view tngDefinitionInventoryScope =
    "Fully storage-reviewed TNG/GTG only: DefinitionType, conditional Switch.EnvironmentDef, CTCAtmosPlayer.AtmosName, "
    "conditional AICreature.OverridingBrainName, "
    "CTCCreatureGenerator.CreatureFamilies, CTCHero.hero_title_object_def_name, "
    "and ContainerContents in CTCContainerRewardHero/CTCChest/CTCSearchableContainer. "
    "Other thing/component references are not enumerated; GTG map slots require world resolution; no source admission.";
// Refused text produces no fields. This intentionally partial name inventory
// does not resolve names, certify runtime classes or select conditional lookups.
TngDefinitionInventory inventoryTngDefinitionFields(std::string_view text);

// GTG's NEWMAP/ENDMAP wrapper contains a local world-map slot and a complete
// TNG body. Each body must pass reviewTng; positive unique slot values must
// still be resolved against the caller's world. No whole-source admission.
Review reviewGtg(std::string_view text);
// Same partial field inventory as TNG, with enclosing map slots and GTG file
// line numbers. Any refused body suppresses the entire file's field inventory.
TngDefinitionInventory inventoryGtgDefinitionFields(std::string_view text);

} // namespace forge::defexternal
