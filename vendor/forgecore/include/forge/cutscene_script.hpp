#pragma once
// Codec for the CCutsceneDef macro-command stream stored in script.bin payloads.
// Layout (evidence: FableTLC/docs/FINDINGS.md, validated on all 595 retail
// cutscenes): 9-byte header, u32 command count at +0x09, then that many
// null-terminated command strings from +0x0D. Any bytes past the last command
// are preserved verbatim on re-encode.

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

}  // namespace forge::cutscene
