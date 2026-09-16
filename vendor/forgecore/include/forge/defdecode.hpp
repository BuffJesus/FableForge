#pragma once
// Decoder for compiled-def payloads (game.bin / script.bin / frontend.bin) into
// named, typed field values.
//
// THE game.bin FIELD-TAG HASH (reverse-engineered + corpus-validated 2026-07-19,
// FableTLC docs/FINDINGS.md "game.bin FIELD ENCODING FULLY CRACKED"):
//   tag(fieldName) = reflected CRC-32 (poly 0xEDB88320, the standard zlib table)
//                    with seed = 0 and NO final inversion, over the ASCII field
//                    name as-is (no null terminator), stored little-endian.
//   This is NOT CCharString::GetCRC() (which seeds 0xFFFFFFFF) -- the seed
//   difference is why crc32/fnv/djb2/GetCRC never matched.
//
// PAYLOAD LAYOUT per entry: [variable untagged prefix][field]*
//   prefix = base-class data with no CRC tags (3 bytes for most types, 5 for
//            some, larger for a few); its end = the first named field's tag.
//   field  = [4-byte CRC(name) little-endian tag][value]
//   value sizes: int32/uint32/float/enum/CDefIndex = 4B, bool = 1B,
//                CCharString = null-terminated, Vector_<T> = [u32 count][T x n].
// Serialization order == Transfer() call order == defschema field order.
//
// This decoder is type-tolerant: each field's value is the raw bytes between its
// tag and the next field's tag (located by CRC search), so every named field is
// recovered even across value types this build does not size explicitly. That is
// exactly the per-field byte range field-level merge needs. On top of that, typed
// interpretation is provided for the known scalar/string types.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "forge/bin.hpp"
#include "forge/defschema.hpp"

namespace forge::defdecode {

// game.bin field-name tag: reflected CRC-32, seed 0, no final xor.
uint32_t fieldTag(std::string_view name);

struct DecodedField {
    std::string name;                 // "" for unnamed schema fields
    std::string type;                 // defschema type string
    uint32_t tag = 0;                 // 4-byte tag read from the payload
    bool tagOk = false;               // tag == fieldTag(name) (false if unnamed)
    std::vector<uint8_t> value;       // raw value bytes (tag-to-next-tag range)
};

struct Decoded {
    size_t prefixLen = 0;             // untagged base-class prefix length
    std::vector<uint8_t> prefix;      // the prefix bytes (base-class data)
    std::vector<DecodedField> fields; // in serialization order
    size_t leftover = 0;              // trailing bytes after the last field value
    bool allTagsOk = false;           // every named field's tag matched

    // A clean decode (allTagsOk && leftover==0) round-trips exactly through
    // encode(); only such payloads are safe to field-merge or re-encode.
    bool clean() const { return allTagsOk && leftover == 0; }
};

// Decode one entry against its def-type schema. `def` must match entry.definition.
Decoded decode(const std::vector<uint8_t>& payload, const defschema::DefType& def);

// Rebuild a payload from a decode: prefix bytes, then [4-byte tag][value] per
// field. encode(decode(p)) == p for any clean() decode.
std::vector<uint8_t> encode(const Decoded& d);

// Resolve a bin Entry.definition string to a schema DefType. Most def types are
// stored under their C++ class name (CChestDef) and match directly; others use a
// game-facing category (CREATURE -> CCreatureDef). Tries the literal name, then
// the "C"+CamelCase(category)+"Def" transform, and CONFIRMS the candidate by
// decoding `samplePayload` cleanly (all field tags present). Returns nullptr if
// no schema type decodes the sample -- the caller must not field-merge it.
const defschema::DefType* resolveType(const defschema::Schema& schema,
                                      const std::string& binDefinition,
                                      const std::vector<uint8_t>& samplePayload);

// Convenience: look the def type up in the schema, then decode.
// Throws std::runtime_error if the entry's definition type is not in the schema.
Decoded decode(const bin::Entry& entry, const defschema::Schema& schema);

// --- Field-level merge -----------------------------------------------------
// Three-way compose of one record: base plus every mod version that CHANGED it.
struct FieldMerge {
    bool ok = false;                     // false -> caller falls back to
                                         //          whole-record merge
    std::vector<uint8_t> payload;        // merged payload (valid iff ok)
    std::vector<std::string> autoMerged; // parts exactly one mod changed (or
                                         // several changed identically)
    struct Conflict {
        std::string part;                // field name, or "<prefix>"
        std::vector<std::string> mods;   // mods that set differing values
        std::string winner;              // mod whose value was applied,
                                         // or "vanilla"
    };
    std::vector<Conflict> conflicts;     // parts >1 mod changed to differing
                                         // values (load-order/pick decided)
};

// `versions` = (modName, decoded) for each mod that changed this record, in load
// order. Per part (prefix + each field): 0 changers -> base; 1 changer (or all
// identical) -> that value (auto-merged); >1 differing -> conflict, winner by
// load order unless `pick` names a mod ("vanilla"/"base" keeps the base value).
// Returns ok=false if base or any version is not clean() or the field layouts
// do not align (different def types) -- the caller must then merge whole-record.
FieldMerge mergeFields(const Decoded& base,
                       const std::vector<std::pair<std::string, Decoded>>& versions,
                       const std::string& pick = {});

// Human-readable rendering of a field value using its type
// (e.g. "391" for int32, "\"GATEWAY_IDLE_01\"" for CCharString,
// "[2820]" for Vector_VCDefIndex__). Falls back to hex for unknown types.
std::string formatValue(const DecodedField& field);

} // namespace forge::defdecode
