#pragma once
// Reader for Fable TLC compiled definitions (data\CompiledDefs\*.bin plus the
// shared names.bin string table). Layout cross-checked against
// SilverChest.Formats.Bin (local reference source) and validated against the
// retail install:
//   names.bin: 20-byte header (u32 magic @4, PC = 0xA8E36C34; s32 count @8),
//     then count x { u32 nameCrc, ASCIIZ name }. References into names.bin are
//     offsets of the name string relative to the end of the header.
//   game.bin:  9-byte header (byte 1 == 0xAA means Xbox), u32 entryCount @9,
//     main table @13 = entryCount x { s32 defNameOffset, s32 nameOffset,
//     s32 indexInDefinition }, u32 chunkCount, chunkCount x { s32 firstEntry,
//     s32 compressedOffset } (last pair is an end sentinel), u32, then the
//     zlib chunk data. Each inflated chunk (<= 64 KiB) starts with u16
//     payload offsets for its entries.
// Entry order IS the runtime global definition index (evidence in
// docs/FINDINGS.md 2026-07-18: reward payload indices resolve to entry rows).

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace forge::bin {

struct Entry {
    std::string definition;      // definition type, e.g. OBJECT, CChestDef
    std::string name;            // e.g. OBJECT_CHEST_OPENABLE; "" for sub-defs
    int32_t indexInDefinition = 0;
    std::vector<uint8_t> data;   // raw payload
};

class File {
public:
    static File open(const std::filesystem::path& namesPath,
                     const std::filesystem::path& binPath);

    const std::vector<Entry>& entries() const { return entries_; }

    // First entry with this exact name, or nullptr.
    const Entry* find(std::string_view name) const;

    // --- Write path. Mutate payloads (or add entries), then save both files.
    // Saving re-chunks and re-compresses, so output is not byte-identical to
    // the input; the guarantee is a semantic round-trip (same definitions,
    // names, indexInDefinition, and payload bytes after reload). names.bin is
    // rewritten with recomputed offsets; unchanged name sets produce
    // byte-identical names.bin output.
    void setEntryData(size_t index, std::vector<uint8_t> data);
    size_t addEntry(std::string definition, std::string name,
                    std::vector<uint8_t> data);
    void save(const std::filesystem::path& namesPath,
              const std::filesystem::path& binPath) const;

private:
    std::vector<Entry> entries_;
    std::vector<uint8_t> binHeader_;                // 9 bytes, preserved
    std::vector<uint8_t> namesHeader_;              // 20 bytes, preserved
    std::vector<std::pair<uint32_t, std::string>> names_; // (crc, name) in order
};

// Decoded OBJECT_FAMILY payload: 9-byte header, u32 count,
// count x (u32 objectDefIndex, f32 weight). Weights are per-mille;
// objectDefIndex 0 means "no drop".
struct FamilyMember {
    uint32_t objectIndex = 0;
    float weight = 0.0f;
};
struct ObjectFamily {
    uint32_t entryIndex = 0;
    std::string name;
    std::vector<FamilyMember> members;
};
std::vector<ObjectFamily> decodeObjectFamilies(const File& file);

// Decoded CContainerRewardHeroDef payload: 7-byte header, u32 count,
// count x u32 objectFamilyEntryIndex.
struct RewardDef {
    uint32_t entryIndex = 0;
    std::vector<uint32_t> familyIndices;
};
std::vector<RewardDef> decodeContainerRewards(const File& file);

} // namespace forge::bin
