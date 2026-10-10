#pragma once
// Decoder for text.big string payloads (localization strings). text.big is a BIG
// archive (forge::big) whose entries carry a Type (0=string, 1=group, 2=narrator);
// this decodes the per-entry payload blob. Format from the decomp agent's
// docs/TEXTBIG_FORMAT.md §4 (verified against all 28,913 retail entries).
//
//   Type 0 (string): UTF-16LE \0\0-terminated Content, then u32len+bytes for
//     SpeechBank / Speaker / Identifier, then u32 TagCount, then per tag
//     { i32 Position, ASCIIZ Name }.
//   Type 1 (group):  u32 Count, Count x u32 member entry IDs.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "forge/big.hpp"

namespace forge::textbig {

struct Tag {
    int32_t position = 0;   // char index into Content where the tag fires
    std::string name;       // e.g. "ANIM:SCRIPT_CHEER_1", "CAM:(...)", mood
};

struct Entry {
    int type = 0;
    std::string content;    // decoded UTF-8 (from UTF-16LE) for Type 0
    std::string speechBank; // e.g. ScriptDialogue.lug
    std::string speaker;    // e.g. FARMER / NONE
    std::string identifier; // usually == entry name
    std::vector<Tag> tags;
    std::vector<uint32_t> groupMembers; // Type 1
};

// Decode a payload given the entry Type. Throws std::runtime_error on truncation.
Entry decode(const std::vector<uint8_t>& payload, int type);

// Type-2 names live in the TOC subheader, not the payload. Strictly consume
// its u32 count and NUL-terminated ASCII names; throws on malformed input.
std::vector<std::string> decodeNarrators(const std::vector<uint8_t>& subHeader);

// Encode a type-0 string or type-1 group payload in the retail/EgoCore format.
// Type 2 narrator lists are intentionally not synthesized by this API.
std::vector<uint8_t> encode(const Entry& entry);

// Type-0 Info is a four-byte index into the bank's type-2 NarratorList.
// A header can be copied unchanged when both lists agree and its index exists.
// Preserve authored differences from the payload's separate speaker string.
// Malformed/ambiguous lists return false.
bool compatibleSpeakerHeader(const big::Bank& baseline, const big::Bank& source,
                             const big::Entry& record, const Entry& value);

struct UpsertResult {
    uint32_t id = 0;
    std::string name;
    std::string bank;
    bool added = false;
};

// Add or replace one type-0 string in an opened text.big. New records clone a
// retail type-0 donor's non-payload metadata (the same strategy EgoCore uses),
// receive max(existing ID)+1 unless requestedId is provided, and keep
// Entry::identifier synchronized with `name` when it is empty.
//
// Existing entries are replaced by name and retain their ID unless requestedId
// explicitly changes it. Duplicate names/IDs and invalid donor choices throw.
// An explicit donor name must match exactly one record when adding a string.
UpsertResult upsertString(
    big::File& file, const std::string& name, Entry value,
    std::optional<uint32_t> requestedId = std::nullopt,
    const std::string& donorName = {});

} // namespace forge::textbig
