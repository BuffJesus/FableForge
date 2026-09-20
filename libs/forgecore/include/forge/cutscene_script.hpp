#pragma once
// Codec for the CCutsceneDef macro-command stream stored in script.bin payloads.
// Layout (evidence: FableTLC/docs/FINDINGS.md, validated on all 595 retail
// cutscenes): 9-byte header, u32 command count at +0x09, then that many
// null-terminated command strings from +0x0D. Any bytes past the last command
// are preserved verbatim on re-encode.

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace forge::cutscene {

// Decode the command strings from a CCutsceneDef payload. Returns empty if the
// payload is too short to hold the 13-byte header.
inline std::vector<std::string> decodeCommands(const std::vector<uint8_t>& data) {
    std::vector<std::string> commands;
    if (data.size() < 13) return commands;
    uint32_t declared = 0;
    for (int i = 0; i < 4; ++i) {
        declared |= static_cast<uint32_t>(data[9 + i]) << (8 * i);
    }
    size_t pos = 13;
    for (uint32_t i = 0; i < declared && pos < data.size(); ++i) {
        const size_t start = pos;
        while (pos < data.size() && data[pos] != 0) ++pos;
        commands.emplace_back(data.begin() + static_cast<ptrdiff_t>(start),
                              data.begin() + static_cast<ptrdiff_t>(pos));
        if (pos < data.size()) ++pos;
    }
    return commands;
}

// Byte offset just past the last decoded command, so trailing bytes survive.
inline size_t commandsEnd(const std::vector<uint8_t>& data) {
    if (data.size() < 13) return data.size();
    uint32_t declared = 0;
    for (int i = 0; i < 4; ++i) {
        declared |= static_cast<uint32_t>(data[9 + i]) << (8 * i);
    }
    size_t pos = 13;
    for (uint32_t i = 0; i < declared && pos < data.size(); ++i) {
        while (pos < data.size() && data[pos] != 0) ++pos;
        if (pos < data.size()) ++pos;
    }
    return pos;
}

// Re-encode a payload with an edited command list, preserving the 13-byte header
// (9-byte header + u32 count) and any trailing bytes. The command count field is
// left unchanged; callers that only substitute verbs keep the same count.
inline std::vector<uint8_t> encodeCommands(const std::vector<uint8_t>& original,
                                           const std::vector<std::string>& commands) {
    std::vector<uint8_t> out(original.begin(), original.begin() + 13);
    for (const auto& command : commands) {
        out.insert(out.end(), command.begin(), command.end());
        out.push_back(0);
    }
    const size_t tail = commandsEnd(original);
    out.insert(out.end(), original.begin() + static_cast<ptrdiff_t>(tail),
               original.end());
    return out;
}

// ---------------------------------------------------------------------------
// Whole-def codec. The "9-byte header" above is really the 5-byte untagged
// base-class prefix (01 00 01 00 00 on every retail entry) followed by the
// 4-byte CRC tag of the first field. A CCutsceneDef payload is that prefix and
// then eight tagged Vector<CCharString> fields in Transfer() order
// (def_schema.json CCutsceneDef; tags = crc0 of the field name):
//   Macro      the command stream RunCutsceneMacro_Func executes
//   SkipCond   commands run instead when the player skips the scene (cleanup)
//   SetupCond  setup-condition strings (retail: empty, or one "" entry)
//   Lights, LightScene, Sound, Answer0, Answer1
// Each vector = u32 count, then count NUL-terminated strings. A new cutscene
// must carry all eight fields, so authoring goes through Def, not a bare
// command list.

inline constexpr size_t kFieldCount = 8;
inline constexpr std::array<const char*, kFieldCount> kFieldNames = {
    "Macro", "SkipCond", "SetupCond", "Lights",
    "LightScene", "Sound", "Answer0", "Answer1"};
// crc0(name): reflected CRC-32, poly 0xEDB88320, seed 0, no final xor.
inline constexpr std::array<uint32_t, kFieldCount> kFieldTags = {
    0xa96c1e5au, 0xffbacac1u, 0x1245c34fu, 0x8ed2167du,
    0x581803e5u, 0xff6d1b9du, 0xedd430ceu, 0x9ad30058u};
inline constexpr std::array<uint8_t, 5> kRetailPrefix = {0x01, 0x00, 0x01, 0x00, 0x00};

struct Def {
    std::vector<uint8_t> prefix{kRetailPrefix.begin(), kRetailPrefix.end()};
    std::array<std::vector<std::string>, kFieldCount> fields;
    std::vector<uint8_t> leftover;  // bytes after the last field, kept verbatim

    std::vector<std::string>& macro() { return fields[0]; }
    const std::vector<std::string>& macro() const { return fields[0]; }
    std::vector<std::string>& skipCond() { return fields[1]; }
};

inline uint32_t readU32(const std::vector<uint8_t>& d, size_t pos) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(d[pos + i]) << (8 * i);
    return v;
}

// Decode a full payload. Returns false when the layout is not the expected
// prefix + eight tagged fields; such a Def must not be re-encoded (the parsed
// part and `leftover` are kept only for diagnostics). Retail: 595/595 clean.
inline bool decodeDef(const std::vector<uint8_t>& data, Def& def) {
    def = Def{};
    // The prefix ends where the Macro tag begins (retail: always offset 5).
    size_t pos = 0;
    bool found = false;
    for (size_t p = 0; p + 4 <= data.size() && p <= 16; ++p) {
        if (readU32(data, p) == kFieldTags[0]) { pos = p; found = true; break; }
    }
    if (!found) { def.leftover = data; return false; }
    def.prefix.assign(data.begin(), data.begin() + static_cast<ptrdiff_t>(pos));
    bool clean = true;
    for (size_t f = 0; f < kFieldCount; ++f) {
        if (pos + 8 > data.size() || readU32(data, pos) != kFieldTags[f]) {
            clean = false;
            break;
        }
        const uint32_t count = readU32(data, pos + 4);
        pos += 8;
        for (uint32_t i = 0; i < count; ++i) {
            const size_t start = pos;
            while (pos < data.size() && data[pos] != 0) ++pos;
            def.fields[f].emplace_back(data.begin() + static_cast<ptrdiff_t>(start),
                                       data.begin() + static_cast<ptrdiff_t>(pos));
            if (pos < data.size()) ++pos;
        }
    }
    def.leftover.assign(data.begin() + static_cast<ptrdiff_t>(pos), data.end());
    return clean;
}

inline std::vector<uint8_t> encodeDef(const Def& def) {
    std::vector<uint8_t> out(def.prefix.begin(), def.prefix.end());
    auto putU32 = [&out](uint32_t v) {
        for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
    };
    for (size_t f = 0; f < kFieldCount; ++f) {
        putU32(kFieldTags[f]);
        putU32(static_cast<uint32_t>(def.fields[f].size()));
        for (const auto& s : def.fields[f]) {
            out.insert(out.end(), s.begin(), s.end());
            out.push_back(0);
        }
    }
    out.insert(out.end(), def.leftover.begin(), def.leftover.end());
    return out;
}

}  // namespace forge::cutscene
