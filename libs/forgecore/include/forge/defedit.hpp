#pragma once
// Ergonomic single-field editor for compiled defs (game.bin / frontend.bin /
// script.bin) — the key write primitive for UI authoring. Where questcard clones
// a whole donor and patches a fixed set of scalar fields, defedit::setField edits
// ONE named field of an EXISTING entry, in place, by its CRC tag.
//
// HOW IT LOCATES THE FIELD (same approach as defdecode / questcard — it does NOT
// trust def_schema donor_off): the entry payload is decoded against its schema
// DefType, which anchors every named field by its reflected-CRC-32 tag and gives
// the exact tag-to-next-tag byte range of each value. setField finds the target
// field in that decode, replaces only its value bytes with the typed encoding of
// `valueString`, re-encodes the payload (prefix + [tag][value] per field, which
// round-trips every untouched field byte-identically), and writes it back via
// bin::File::setEntryData. The File then re-serializes on save() exactly like the
// quest-card path — every OTHER entry stays byte-identical.
//
// SUPPORTED TYPES (the UI-relevant set) and their valueString grammar:
//   int32          "391" / "-2"            (decimal)
//   uint32         "4313"                  (decimal, also 0x-hex)
//   float          "1.5" / "-0.25"         (finite C float)
//   bool           "1"/"0"/"true"/"false"
//   C2DVector      "x,y"                    (two floats)
//   C3DVector      "x,y,z"                  (three floats)
//   CRGBColour     "r,g,b" or "r,g,b,a"    (each 0-255; a defaults to 255)
//   CWideString    "UTF-8 text"            (NUL-terminated UTF-16LE units)
//   CDefString     "1234" / "-1"            (signed names-table offset; raw u32/hex accepted)
//   CCharString    "ENG_ARIAL_16"          (null-terminated ASCII)
// Any other declared type is rejected with a clear error (caller must not guess).

#include <cstddef>
#include <string>
#include <string_view>

#include "forge/bin.hpp"
#include "forge/big.hpp"
#include "forge/defschema.hpp"

namespace forge::defedit {

struct SetFieldResult {
    size_t entryIndex = 0;      // global entry index that was edited
    std::string definition;     // entry.definition
    std::string entryName;      // entry.name ("" for unnamed sub-defs)
    std::string field;          // field name edited
    std::string type;           // field's declared schema type
    std::string oldValue;       // human-readable old value (formatValue)
    std::string newValue;       // human-readable new value (formatValue)
    size_t oldPayloadSize = 0;  // payload byte length before the edit
    size_t newPayloadSize = 0;  // payload byte length after the edit
};

struct CreatureBodyModelResult {
    size_t entryIndex, bodyPart, alternative, offset;
    uint32_t oldModelId, newModelId;
};
// Change exactly one RandomAppearanceMorph body-model word in memory. Requires
// a clean exact CCreatureDef decode, complete supported prefix and unique type-5
// current/replacement IDs in one MBANK_ALLMESHES bank. No mesh payload, texture,
// names or other definition bytes change. Invalid requests refuse before mutation.
// Identity/type checks do NOT establish skeleton or runtime compatibility.
// Callers must publish through the guarded existing-entry transaction.
CreatureBodyModelResult setCreatureBodyModel(bin::File& file,
    const defschema::Schema& schema,size_t entryIndex,size_t bodyPart,size_t alternative,
    uint32_t modelId,const big::File& graphics);

// Edit one field of the entry named `entryName` (empty name never matches;
// use the index overload for unnamed sub-defs). Locates the field by CRC tag,
// parses `valueString` per the field's declared type, rewrites just that value,
// and re-serializes the entry into `file`. `schema` must contain the entry's
// def type. Throws std::runtime_error if: the entry is not found, its def type
// has no schema / does not decode cleanly, the field is not present, the type is
// unsupported, or `valueString` does not parse for that type.
SetFieldResult setField(bin::File& file, const defschema::Schema& schema,
                        std::string_view entryName, std::string_view fieldName,
                        std::string_view valueString);

// Index overload — required for unnamed sub-defs (name == "").
SetFieldResult setField(bin::File& file, const defschema::Schema& schema,
                        size_t entryIndex, std::string_view fieldName,
                        std::string_view valueString);

// Clone an exact donor payload into a new final record slot. The name overload
// requires one exact, nonempty donor name; the index overload supports unnamed
// donors. Existing identities and loaded name-pool records remain unchanged.
// Publishing requires a paired bank/name-pool transaction, unlike setField.
size_t cloneEntry(bin::File& file, size_t donorIndex, std::string newName);
size_t cloneEntry(bin::File& file, std::string_view donorName, std::string newName);

// Distinct Creature clone: relocate only verified self references to the final
// append index, preserving shared components, all prior records and name offsets.
// Requires a clean CREATURE CDefs layout; unresolved matching words refuse before
// mutation. No private appearance/body cloning or publication is performed here.
size_t cloneCreature(bin::File& file, const defschema::Schema& schema,
                     size_t donorIndex, std::string newName);

struct CreatureVisualCloneResult {
    size_t rootIndex, bodyIndex, appearanceIndex;
};
struct CreatureVisualGraph {
    size_t rootIndex, bodyIndex, appearanceIndex;
    size_t componentCount, bodyRow, appearanceRow;
};
// Resolve the two visual CDefs rows without mutation. Requires a clean CREATURE
// root, exactly one correctly typed row per visual component, and an original
// parent whose corresponding row points to the same component. This validates
// graph ownership, not component payloads, runtime profile or asset availability.
CreatureVisualGraph resolveCreatureVisuals(const bin::File& file,
                     const defschema::Schema& schema, size_t rootIndex);
// Append a private root/CCreatureDef/CAppearanceDef graph in memory. Explicit
// names must be unique printable ASCII (127-byte maximum) and introduce no
// compiled-name CRC ambiguity. Only the two selected visual CDefs rows receive
// new component indices/new-root original parents; every other CDefs row stays
// byte-identical, retaining shared gameplay ownership. Verified references in
// all three payloads relocate; unresolved candidates refuse the whole plan.
// All prior slots and loaded name records stay exact. A disposable File holds
// every append until validation succeeds, so refusal never mutates the caller.
// Models, textures and animations remain shared. No files/publication here.
CreatureVisualCloneResult cloneCreatureVisuals(bin::File& file,
                     const defschema::Schema& schema, size_t donorIndex,
                     std::string rootName, std::string bodyName, std::string appearanceName);

// The raw value bytes of one field (everything between its tag and the next
// tag), for the types encodeValue does not parse -- maps, vectors, nested
// structs. Same entry/field location rules as setField.
std::vector<uint8_t> getFieldBytes(const bin::File& file, const defschema::Schema& schema,
                                   std::string_view entryName, std::string_view fieldName);

// Replace one field's raw value bytes (any type; the caller owns the encoding,
// e.g. a Map_JVCCharString__ is [u32 count] then count x [presized string][u32]).
// Every other field of the entry round-trips byte-identically.
SetFieldResult setFieldBytes(bin::File& file, const defschema::Schema& schema,
                             std::string_view entryName, std::string_view fieldName,
                             std::vector<uint8_t> valueBytes);

} // namespace forge::defedit
