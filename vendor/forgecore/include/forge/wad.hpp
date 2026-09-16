#pragma once
// Reader for Fable TLC level archives (FinalAlbion.wad).
// Byte layout cross-checked against EgoCore (MIT, AeoN/AlbionSecrets) WADBackend.h
// and the fabletlcmod.com community documentation:
//   header: u32 entryCount at offset 20, u32 footerOffset at offset 28
//   footer: u32 statsCount, statsCount*8 bytes of stats, then per entry:
//     u32 magic (must be 42), u32 id, u32 type, u32 size, u32 offset, u32 crc,
//     u32 nameLen + name bytes, u32 timestamp, u32 depCount + (u32 len + bytes)*,
//     u32 infoSize + info bytes

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace forge::wad {

struct Entry {
    uint32_t id = 0;
    uint32_t type = 0;
    uint32_t size = 0;
    uint32_t offset = 0;
    uint32_t crc = 0;
    uint32_t timestamp = 0;
    std::string name;                       // archive path, e.g. Data\Levels\FinalAlbion\x.lev
    std::vector<std::string> dependencies;
    std::vector<uint8_t> info;               // opaque per-entry footer info bytes
};

class Archive {
public:
    // Opens and parses the table of contents. Throws std::runtime_error on
    // malformed input (bad entry magic, truncated footer, unreadable file).
    static Archive open(const std::filesystem::path& path);

    const std::vector<Entry>& entries() const { return entries_; }
    const std::filesystem::path& path() const { return path_; }

    // Reads one entry's payload from the archive.
    std::vector<uint8_t> read(const Entry& entry) const;

    // Compares an entry payload with a file without assigning precedence.
    // Used by package validation to detect byte-different loose shadows.
    bool payloadEqualsFile(const Entry& entry,
                           const std::filesystem::path& file) const;

    // Extracts entries under outDir, preserving the archive-relative path.
    // Returns the number of files written. onFile (optional) is called with
    // each archive name before extraction. filter (optional, case-insensitive
    // substring) limits which entries are written.
    size_t extract(const std::filesystem::path& outDir,
                   const std::string& filter = {},
                   const std::function<void(const Entry&)>& onFile = {}) const;

private:
    std::filesystem::path path_;
    std::vector<Entry> entries_;
};

// Rewrite the archive at srcPath to outPath, replacing the payloads of entries
// whose archive name matches a key of `replacements` (case-insensitive, '\'
// separators). Every byte the reader does not interpret (header fields, stats
// block, per-entry info blocks) is preserved verbatim. A same-size payload is
// patched in place; a resized one is appended to the end of the data region
// (the old bytes become dead space) and the entry's footer offset/size plus the
// header's footer offset are patched. With an empty map the output is
// byte-identical to the input. Throws if a replacement name matches no entry.
// Returns the number of entries replaced.
size_t repack(const std::filesystem::path& srcPath,
              const std::map<std::string, std::vector<uint8_t>>& replacements,
              const std::filesystem::path& outPath);

struct CloneEntry {
    std::string donorName;
    std::string newName;
};

struct NativeEntry {
    std::string name;
    std::vector<uint8_t> payload;
};

// Append byte-identical clones of existing entries under new names. Existing
// payload offsets and metadata remain unchanged; cloned dependencies have a
// donor filename stem replaced with the new stem. IDs are assigned max+1.
// The source is never modified.
size_t appendClonedEntries(const std::filesystem::path& srcPath,
                           const std::vector<CloneEntry>& entries,
                           const std::filesystem::path& outPath);

// Append newly authored payloads without borrowing a donor entry. TLC's WAD
// footer stores an 88-byte CFileInformation value: three default-constructible
// 28-byte CDateAndTime objects followed by the read-only byte and padding.
// Native entries use the engine's zero/default value, type/timestamp/CRC zero,
// and the retail single empty dependency.
size_t appendNativeEntries(const std::filesystem::path& srcPath,
                           const std::vector<NativeEntry>& entries,
                           const std::filesystem::path& outPath);

} // namespace forge::wad
