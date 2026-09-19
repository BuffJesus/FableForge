#pragma once
// Reader for FinalAlbion_RT.stb static-map banks. STB is a BBB-style container
// plus a special "__STATIC_MAP_COMMON_HEADER__" entry mapping level names to
// level chunks inside the archive. Layout grounded in decompiled FableMod.BBB /
// FableMod.STB, and verified against SilverChest.StbBridge output.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace forge::stb {

struct Entry {
    uint32_t id = 0;
    uint32_t type = 0;
    uint32_t size = 0;
    uint32_t offset = 0;
    uint32_t crc = 0;
    std::string name;
    std::string devName;
};

struct StaticMap {
    std::string levelName;
    uint32_t relativeOffset = 0;
    uint32_t absoluteOffset = 0;
};

class Archive {
public:
    static Archive open(const std::filesystem::path& path);

    const std::filesystem::path& path() const { return path_; }
    const std::vector<Entry>& entries() const { return entries_; }
    const std::vector<StaticMap>& staticMaps() const { return staticMaps_; }
    uint32_t alignment() const { return alignment_; }
    uint32_t tableOffset() const { return tableOffset_; }

    const Entry* findEntry(const std::string& name) const;
    std::vector<uint8_t> read(const Entry& entry) const;
    std::vector<uint8_t> readStaticMapRecord(const StaticMap& map) const;
    size_t extract(const std::filesystem::path& outDir,
                   const std::string& filter = {},
                   const std::function<void(const Entry&)>& onFile = {}) const;

private:
    std::filesystem::path path_;
    uint32_t alignment_ = 0;
    uint32_t tableOffset_ = 0;
    std::vector<Entry> entries_;
    std::vector<StaticMap> staticMaps_;
};

// Append one static-map chunk without relocating any existing payload. A new
// common-header blob and TOC are appended; the old table/common header become
// unreachable dead space, preserving every original byte and payload offset.
// `commonRecord` is the complete donor/authored common-header map record: the
// 0x5C CStaticMapInfoBlock plus its landscape/local-detail control subheaders,
// ending at HeaderEndPtr. Its five absolute logical offsets are rebased and its
// BankFileIndex is patched to the newly assigned entry id. `levelName` is the
// key (normally Data\\Levels\\FinalAlbion\\Name.lev); `entryName` is the BBB entry
// name used for the chunk.
void appendStaticMap(const std::filesystem::path& srcPath,
                     const std::filesystem::path& outPath,
                     const std::string& levelName,
                     const std::string& entryName,
                     const std::vector<uint8_t>& chunk,
                     const std::vector<uint8_t>& commonRecord);

struct StaticMapAppend {
    std::string levelName;
    std::string entryName;
    std::vector<uint8_t> chunk;
    std::vector<uint8_t> commonRecord;
};

// Atomic multi-map form of appendStaticMap. The source bank is parsed and
// copied once, one relocated common header and one final TOC are emitted, and
// every new record receives its final entry id/pointers in that pass.
void appendStaticMaps(const std::filesystem::path& srcPath,
                      const std::filesystem::path& outPath,
                      const std::vector<StaticMapAppend>& maps);

// Replace an existing entry payload without touching the common-header manifest.
// The source remains a byte-identical prefix; the new payload and a cloned TOC are
// appended, and only the target entry's size/offset plus the top-level table pointer
// change. This is the safest path for an already live-proven custom map record.
void replaceEntryPayload(const std::filesystem::path& srcPath,
                         const std::filesystem::path& outPath,
                         const std::string& entryName,
                         const std::vector<uint8_t>& payload);

// Replace a static-map chunk and its same-sized common-header control record.
// The record's logical pointers are rebased to the existing map slot and its
// live bank-file index is preserved.
void replaceStaticMap(const std::filesystem::path& srcPath,
                      const std::filesystem::path& outPath,
                      const std::string& levelName,
                      const std::string& entryName,
                      const std::vector<uint8_t>& payload,
                      const std::vector<uint8_t>& commonRecord);

// Atomic same-size replacement for several existing static maps. The bank is
// read and written once; payload offsets, entry ids, and TOC bytes stay fixed.
void replaceStaticMaps(const std::filesystem::path& srcPath,
                       const std::filesystem::path& outPath,
                       const std::vector<StaticMapAppend>& maps);

// Atomic variable-size replacement for several existing static maps. New
// payloads and one cloned TOC are appended; common records stay in their live
// slots and are rebased there. Unreplaced payloads and entry ids are preserved.
void replaceStaticMapsRelayout(const std::filesystem::path& srcPath,
                               const std::filesystem::path& outPath,
                               const std::vector<StaticMapAppend>& maps);

} // namespace forge::stb
