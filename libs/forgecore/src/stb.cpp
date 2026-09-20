#include "forge/stb.hpp"
#include "forge/stbinfo.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>

namespace fs = std::filesystem;

namespace forge::stb {
namespace {

uint32_t readU32(std::istream& in, const char* what) {
    uint32_t value = 0;
    in.read(reinterpret_cast<char*>(&value), sizeof(value));
    if (!in) {
        throw std::runtime_error(std::string("stb: truncated while reading ") + what);
    }
    return value;
}

std::string readString(std::istream& in, uint32_t length, const char* what) {
    std::string value(length, '\0');
    if (length > 0) {
        in.read(value.data(), length);
        if (!in) {
            throw std::runtime_error(std::string("stb: truncated while reading ") + what);
        }
        value.erase(std::find(value.begin(), value.end(), '\0'), value.end());
    }
    return value;
}

std::string lowered(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool containsIgnoreCase(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                          [](char a, char b) {
                              return std::tolower(static_cast<unsigned char>(a)) ==
                                     std::tolower(static_cast<unsigned char>(b));
                          });
    return it != haystack.end();
}

void appendSafePath(fs::path& outPath, std::string relative) {
    std::replace(relative.begin(), relative.end(), '/', '\\');
    for (size_t start = 0; start < relative.size();) {
        size_t end = relative.find('\\', start);
        if (end == std::string::npos) end = relative.size();
        std::string part = relative.substr(start, end - start);
        if (!part.empty() && part != "." && part != "..") {
            outPath /= part;
        }
        start = end + 1;
    }
}

uint32_t u32At(const std::vector<uint8_t>& b, size_t o, const char* what) {
    if (o + 4 > b.size()) throw std::runtime_error(std::string("stb: truncated ") + what);
    uint32_t v = 0; std::memcpy(&v, b.data() + o, 4); return v;
}

void putU32(std::vector<uint8_t>& b, uint32_t v) {
    const size_t o = b.size(); b.resize(o + 4); std::memcpy(b.data() + o, &v, 4);
}

void patchU32(std::vector<uint8_t>& b, size_t o, uint32_t v) {
    if (o + 4 > b.size()) throw std::runtime_error("stb: patch outside buffer");
    std::memcpy(b.data() + o, &v, 4);
}

size_t alignSize(size_t n, size_t alignment) {
    if (!alignment) return n;
    return ((n + alignment - 1) / alignment) * alignment;
}

} // namespace

Archive Archive::open(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("stb: cannot open " + path.string());
    }

    const uint32_t magic = readU32(file, "magic");
    if (magic != 0x42424242u) {
        throw std::runtime_error("stb: bad magic");
    }
    file.seekg(16, std::ios::beg);
    const uint32_t alignment = readU32(file, "alignment");
    (void)readU32(file, "entry count plus header");
    const uint32_t entryCount = readU32(file, "entry count");
    const uint32_t tableOffset = readU32(file, "table offset");

    file.seekg(tableOffset, std::ios::beg);
    if (!file) {
        throw std::runtime_error("stb: table offset out of range");
    }

    file.seekg(12, std::ios::cur); // BBBDevHeader
    if (!file) {
        throw std::runtime_error("stb: truncated dev header");
    }

    Archive archive;
    archive.path_ = path;
    archive.alignment_ = alignment;
    archive.tableOffset_ = tableOffset;
    archive.entries_.reserve(entryCount);

    for (uint32_t i = 0; i < entryCount; ++i) {
        const uint32_t entryMagic = readU32(file, "entry magic");
        if (entryMagic != 42) {
            throw std::runtime_error("stb: entry " + std::to_string(i) +
                                     " has bad magic " + std::to_string(entryMagic));
        }

        Entry entry;
        entry.id = readU32(file, "entry id");
        entry.type = readU32(file, "entry type");
        entry.size = readU32(file, "entry size");
        entry.offset = readU32(file, "entry offset");
        entry.crc = readU32(file, "entry crc");

        const uint32_t nameLen = readU32(file, "entry name length");
        entry.name = readString(file, nameLen, "entry name");

        (void)readU32(file, "entry dev id");
        (void)readU32(file, "entry dev type");
        const uint32_t devNameLen = readU32(file, "entry dev name length");
        entry.devName = readString(file, devNameLen, "entry dev name");

        const uint32_t extraSize = readU32(file, "entry extra size");
        file.seekg(extraSize, std::ios::cur);
        if (!file) {
            throw std::runtime_error("stb: truncated entry extra block");
        }

        archive.entries_.push_back(std::move(entry));
    }

    if (const Entry* common = archive.findEntry("__STATIC_MAP_COMMON_HEADER__")) {
        const std::vector<uint8_t> data = archive.read(*common);
        if (data.size() >= 4) {
            const uint8_t* cur = data.data();
            const uint8_t* end = data.data() + data.size();
            uint32_t count = 0;
            std::memcpy(&count, cur, 4);
            cur += 4;
            archive.staticMaps_.reserve(count);
            for (uint32_t i = 0; i < count && cur < end; ++i) {
                const uint8_t* nameStart = cur;
                while (cur < end && *cur != 0) ++cur;
                if (cur >= end) break;
                std::string name(reinterpret_cast<const char*>(nameStart),
                                 reinterpret_cast<const char*>(cur));
                ++cur;
                if (end - cur < 4) break;
                uint32_t relative = 0;
                std::memcpy(&relative, cur, 4);
                cur += 4;
                archive.staticMaps_.push_back({name, relative, common->offset + relative});
            }
        }
    }

    return archive;
}

const Entry* Archive::findEntry(const std::string& name) const {
    const std::string wanted = lowered(name);
    for (const Entry& entry : entries_) {
        if (lowered(entry.name) == wanted) {
            return &entry;
        }
    }
    return nullptr;
}

std::vector<uint8_t> Archive::read(const Entry& entry) const {
    std::ifstream file(path_, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("stb: cannot open " + path_.string());
    }
    std::vector<uint8_t> buffer(entry.size);
    file.seekg(entry.offset, std::ios::beg);
    file.read(reinterpret_cast<char*>(buffer.data()), entry.size);
    if (!file) {
        throw std::runtime_error("stb: truncated payload for " + entry.name);
    }
    return buffer;
}

std::vector<uint8_t> Archive::readStaticMapRecord(const StaticMap& map) const {
    const Entry* common = findEntry("__STATIC_MAP_COMMON_HEADER__");
    if (!common) throw std::runtime_error("stb: common-header entry not found");
    const std::vector<uint8_t> data = read(*common);
    const size_t rel = map.relativeOffset;
    if (rel + 0x5C > data.size()) throw std::runtime_error("stb: map record truncated");
    const uint32_t end = u32At(data, rel + 0x58, "map HeaderEndPtr");
    if (end <= rel || end > data.size()) throw std::runtime_error("stb: bad map HeaderEndPtr");
    return std::vector<uint8_t>(data.begin() + rel, data.begin() + end);
}

size_t Archive::extract(const fs::path& outDir, const std::string& filter,
                        const std::function<void(const Entry&)>& onFile) const {
    size_t written = 0;
    for (const Entry& entry : entries_) {
        if (entry.size == 0 || entry.name.empty()) continue;
        if (!containsIgnoreCase(entry.name, filter)) continue;
        if (onFile) onFile(entry);

        fs::path outPath = outDir;
        appendSafePath(outPath, entry.name);
        fs::create_directories(outPath.parent_path());

        const std::vector<uint8_t> payload = read(entry);
        std::ofstream out(outPath, std::ios::binary);
        if (!out.is_open()) {
            throw std::runtime_error("stb: cannot write " + outPath.string());
        }
        out.write(reinterpret_cast<const char*>(payload.data()),
                  static_cast<std::streamsize>(payload.size()));
        ++written;
    }
    return written;
}

void appendStaticMaps(const fs::path& srcPath, const fs::path& outPath,
                      const std::vector<StaticMapAppend>& maps) {
    if (maps.empty())
        throw std::runtime_error("stb: appendStaticMaps requires at least one map");
    std::set<std::string> requestedLevels, requestedEntries;
    for (const auto& map : maps) {
        if (map.levelName.empty() || map.entryName.empty() || map.chunk.empty())
            throw std::runtime_error(
                "stb: appendStaticMaps requires non-empty names/chunks");
        if (map.commonRecord.size() < 0x7D)
            throw std::runtime_error("stb: common map record is too short");
        if (!requestedLevels.insert(lowered(map.levelName)).second)
            throw std::runtime_error("stb: duplicate requested static-map name");
        if (!requestedEntries.insert(lowered(map.entryName)).second)
            throw std::runtime_error("stb: duplicate requested entry name");
    }

    std::ifstream in(srcPath, std::ios::binary);
    if (!in) throw std::runtime_error("stb: cannot open " + srcPath.string());
    std::vector<uint8_t> src((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    if (src.size() < 32 || u32At(src, 0, "magic") != 0x42424242u)
        throw std::runtime_error("stb: bad source header");
    const uint32_t alignment = u32At(src, 16, "alignment");
    const uint32_t entryCount = u32At(src, 24, "entry count");
    const uint32_t tableOffset = u32At(src, 28, "table offset");
    if (tableOffset + 12 > src.size()) throw std::runtime_error("stb: bad table offset");

    struct RawEntry { size_t begin, end, sizePos, offsetPos; uint32_t id; std::string name; };
    std::vector<RawEntry> rawEntries;
    rawEntries.reserve(entryCount);
    size_t pos = size_t(tableOffset) + 12;
    const RawEntry* commonMeta = nullptr;
    uint32_t maxId = 0;
    for (uint32_t i = 0; i < entryCount; ++i) {
        const size_t begin = pos;
        if (u32At(src, pos, "entry magic") != 42) throw std::runtime_error("stb: bad entry magic");
        const uint32_t id = u32At(src, pos + 4, "entry id");
        const uint32_t nameLen = u32At(src, pos + 24, "name length");
        if (pos + 28 + nameLen + 12 > src.size()) throw std::runtime_error("stb: bad entry name");
        std::string name(reinterpret_cast<const char*>(src.data() + pos + 28), nameLen);
        name.erase(std::find(name.begin(), name.end(), '\0'), name.end());
        pos += 28 + nameLen;
        pos += 8; // dev id/type
        const uint32_t devNameLen = u32At(src, pos, "dev name length");
        pos += 4 + devNameLen;
        const uint32_t extraSize = u32At(src, pos, "extra size");
        pos += 4 + extraSize;
        if (pos > src.size()) throw std::runtime_error("stb: truncated entry metadata");
        rawEntries.push_back({begin, pos, begin + 12, begin + 16, id, name});
        if (name == "__STATIC_MAP_COMMON_HEADER__") commonMeta = &rawEntries.back();
        maxId = std::max(maxId, id);
    }
    if (!commonMeta) throw std::runtime_error("stb: common-header entry not found");
    for (const auto& e : rawEntries)
        if (requestedEntries.count(lowered(e.name)))
            throw std::runtime_error("stb: appended entry already exists: " + e.name);

    const uint32_t commonSize = u32At(src, commonMeta->sizePos, "common size");
    const uint32_t commonOffset = u32At(src, commonMeta->offsetPos, "common offset");
    if (uint64_t(commonOffset) + commonSize > src.size())
        throw std::runtime_error("stb: common header outside file");
    std::vector<uint8_t> oldCommon(src.begin() + commonOffset,
                                   src.begin() + commonOffset + commonSize);
    const uint32_t mapCount = u32At(oldCommon, 0, "map count");
    struct MapRow { std::string name; uint32_t rel; };
    std::vector<MapRow> rows; rows.reserve(mapCount);
    size_t cp = 4;
    for (uint32_t i = 0; i < mapCount; ++i) {
        const size_t ns = cp;
        while (cp < oldCommon.size() && oldCommon[cp]) ++cp;
        if (cp >= oldCommon.size()) throw std::runtime_error("stb: malformed common index");
        std::string name(reinterpret_cast<const char*>(oldCommon.data() + ns), cp - ns);
        ++cp;
        const uint32_t rel = u32At(oldCommon, cp, "map relative offset"); cp += 4;
        if (requestedLevels.count(lowered(name)))
            throw std::runtime_error("stb: static-map name already exists: " + name);
        rows.push_back({std::move(name), rel});
    }
    const size_t oldIndexEnd = cp;
    uint64_t indexDelta64 = 0;
    uint64_t appendedRecords64 = 0;
    for (const auto& map : maps) {
        indexDelta64 += uint64_t(map.levelName.size()) + 1 + 4;
        appendedRecords64 += map.commonRecord.size();
    }
    if (indexDelta64 > std::numeric_limits<uint32_t>::max() ||
        uint64_t(oldCommon.size()) + indexDelta64 + appendedRecords64 >
            std::numeric_limits<uint32_t>::max())
        throw std::runtime_error("stb: expanded common header exceeds 32-bit offsets");
    const uint32_t indexDelta = uint32_t(indexDelta64);

    struct NewMapRow {
        const StaticMapAppend* map;
        uint32_t rel;
        uint32_t id;
    };
    std::vector<NewMapRow> newRows;
    newRows.reserve(maps.size());
    uint64_t nextRel = uint64_t(oldCommon.size()) + indexDelta;
    for (size_t i = 0; i < maps.size(); ++i) {
        if (uint64_t(maxId) + 1 + i > std::numeric_limits<uint32_t>::max())
            throw std::runtime_error("stb: entry id overflow");
        newRows.push_back({&maps[i], uint32_t(nextRel),
                           uint32_t(uint64_t(maxId) + 1 + i)});
        nextRel += maps[i].commonRecord.size();
    }

    struct OutputMapRow { std::string name; uint32_t rel; };
    std::vector<OutputMapRow> outputRows;
    outputRows.reserve(rows.size() + newRows.size());
    for (const auto& row : rows)
        outputRows.push_back({row.name, row.rel + indexDelta});
    for (const auto& row : newRows)
        outputRows.push_back({row.map->levelName, row.rel});
    std::sort(outputRows.begin(), outputRows.end(),
              [](const OutputMapRow& a, const OutputMapRow& b) {
                  return a.name < b.name;
              });

    std::vector<uint8_t> newCommon;
    newCommon.reserve(size_t(nextRel));
    putU32(newCommon, mapCount + uint32_t(maps.size()));
    for (const auto& row : outputRows) {
        newCommon.insert(newCommon.end(), row.name.begin(), row.name.end());
        newCommon.push_back(0);
        putU32(newCommon, row.rel);
    }
    newCommon.insert(newCommon.end(), oldCommon.begin() + oldIndexEnd, oldCommon.end());

    // Growing the name index moves every existing map record by indexDelta.
    // Their five internal pointers are absolute offsets within the common
    // header, so the records must be rebased as well as their index entries.
    // Without this, the engine reaches the first static map with stale
    // Landscape/LocalDetail/HeaderEnd pointers and crashes during map-open.
    for (const auto& row : rows) {
        const uint32_t oldRel = row.rel;
        const uint32_t oldEnd = u32At(oldCommon, oldRel + 0x58, "existing map HeaderEndPtr");
        if (oldEnd <= oldRel || oldEnd > oldCommon.size())
            throw std::runtime_error("stb: existing map HeaderEndPtr outside common header");
        const uint32_t newRel = oldRel + indexDelta;
        if (newRel + (oldEnd - oldRel) > newCommon.size())
            throw std::runtime_error("stb: rebased existing map outside common header");
        for (size_t at : {size_t(0x20), size_t(0x24), size_t(0x58),
                          size_t(0x5C), size_t(0x71)}) {
            const uint32_t value = u32At(newCommon, newRel + at,
                                         "existing map internal pointer");
            patchU32(newCommon, newRel + at, value + indexDelta);
        }
    }

    for (const auto& row : newRows) {
        std::vector<uint8_t> record = row.map->commonRecord;
        const uint32_t landscape = u32At(record, 0x20, "record landscape ptr");
        if (landscape < 0x5C)
            throw std::runtime_error("stb: record landscape pointer precedes record");
        const uint32_t oldBase = landscape - 0x5C;
        const int64_t delta = int64_t(row.rel) - int64_t(oldBase);
        for (size_t at : {size_t(0x20), size_t(0x24), size_t(0x58),
                          size_t(0x5C), size_t(0x71)}) {
            const int64_t value = int64_t(u32At(record, at, "record pointer")) + delta;
            if (value < 0 || value > 0xFFFFFFFFll)
                throw std::runtime_error("stb: record rebase overflow");
            patchU32(record, at, uint32_t(value));
        }
        patchU32(record, 4, row.id);
        if (uint64_t(u32At(record, 0x58, "record end")) !=
            uint64_t(row.rel) + record.size())
            throw std::runtime_error("stb: common record HeaderEndPtr/size mismatch");
        newCommon.insert(newCommon.end(), record.begin(), record.end());
    }

    // Preserve the complete source as a prefix. Append payloads in TOC order:
    // the relocated common header remains the penultimate table entry and the
    // new map chunk is last. Retail STBs have monotonic payload offsets in table
    // order with no exceptions.
    std::vector<uint8_t> out = src;
    out.resize(alignSize(out.size(), alignment), 0);
    const uint32_t newCommonOffset = uint32_t(out.size());
    out.insert(out.end(), newCommon.begin(), newCommon.end());
    out.resize(alignSize(out.size(), alignment), 0);
    std::vector<uint32_t> chunkOffsets;
    chunkOffsets.reserve(maps.size());
    for (const auto& map : maps) {
        if (out.size() > std::numeric_limits<uint32_t>::max())
            throw std::runtime_error("stb: chunk offset exceeds 32-bit range");
        chunkOffsets.push_back(uint32_t(out.size()));
        out.insert(out.end(), map.chunk.begin(), map.chunk.end());
        out.resize(alignSize(out.size(), alignment), 0);
    }
    if (out.size() > std::numeric_limits<uint32_t>::max())
        throw std::runtime_error("stb: table offset exceeds 32-bit range");
    const uint32_t newTableOffset = uint32_t(out.size());
    out.insert(out.end(), src.begin() + tableOffset, src.begin() + tableOffset + 12);
    patchU32(out, newTableOffset + 8, entryCount + uint32_t(maps.size()));
    for (const auto& e : rawEntries) {
        const size_t dst = out.size();
        out.insert(out.end(), src.begin() + e.begin, src.begin() + e.end);
        if (e.name == "__STATIC_MAP_COMMON_HEADER__") {
            patchU32(out, dst + (e.sizePos - e.begin), uint32_t(newCommon.size()));
            patchU32(out, dst + (e.offsetPos - e.begin), newCommonOffset);
        }
    }
    auto appendEntry = [&](const std::string& name, uint32_t id,
                           uint32_t offset, uint32_t size) {
        putU32(out, 42); putU32(out, id); putU32(out, 0); putU32(out, size);
        putU32(out, offset); putU32(out, 0); // STB retail CRC is zero for all entries
        putU32(out, uint32_t(name.size()));
        out.insert(out.end(), name.begin(), name.end());
        putU32(out, 0); putU32(out, 1);
        putU32(out, uint32_t(name.size()));
        out.insert(out.end(), name.begin(), name.end());
        putU32(out, 0);
    };
    for (size_t i = 0; i < newRows.size(); ++i)
        appendEntry(newRows[i].map->entryName, newRows[i].id, chunkOffsets[i],
                    uint32_t(newRows[i].map->chunk.size()));
    patchU32(out, 20, entryCount + uint32_t(maps.size()) + 1);
    patchU32(out, 24, entryCount + uint32_t(maps.size()));
    patchU32(out, 28, newTableOffset);

    std::ofstream output(outPath, std::ios::binary);
    if (!output) throw std::runtime_error("stb: cannot write " + outPath.string());
    output.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
    if (!output) throw std::runtime_error("stb: failed writing " + outPath.string());
}

void appendStaticMap(const fs::path& srcPath, const fs::path& outPath,
                     const std::string& levelName, const std::string& entryName,
                     const std::vector<uint8_t>& chunk,
                     const std::vector<uint8_t>& commonRecord) {
    appendStaticMaps(srcPath, outPath,
                     {{levelName, entryName, chunk, commonRecord}});
}

void replaceEntryPayload(const fs::path& srcPath, const fs::path& outPath,
                         const std::string& entryName,
                         const std::vector<uint8_t>& payload) {
    if (entryName.empty() || payload.empty())
        throw std::runtime_error("stb: replace requires non-empty name/payload");
    std::ifstream in(srcPath, std::ios::binary);
    if (!in) throw std::runtime_error("stb: cannot open " + srcPath.string());
    std::vector<uint8_t> src((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    if (src.size() < 32 || u32At(src, 0, "magic") != 0x42424242u)
        throw std::runtime_error("stb: bad source header");
    const uint32_t alignment = u32At(src, 16, "alignment");
    const uint32_t entryCount = u32At(src, 24, "entry count");
    const uint32_t tableOffset = u32At(src, 28, "table offset");
    if (tableOffset + 12 > src.size()) throw std::runtime_error("stb: bad table offset");

    struct RawEntry {
        size_t begin, end, sizePos, offsetPos;
        uint32_t payloadSize, payloadOffset;
        std::string name;
    };
    std::vector<RawEntry> entries;
    size_t pos = size_t(tableOffset) + 12;
    long target = -1;
    for (uint32_t i = 0; i < entryCount; ++i) {
        const size_t begin = pos;
        if (u32At(src, pos, "entry magic") != 42)
            throw std::runtime_error("stb: bad entry magic");
        const uint32_t nameLen = u32At(src, pos + 24, "name length");
        if (pos + 28 + nameLen + 12 > src.size())
            throw std::runtime_error("stb: bad entry name");
        std::string name(reinterpret_cast<const char*>(src.data() + pos + 28), nameLen);
        name.erase(std::find(name.begin(), name.end(), '\0'), name.end());
        pos += 28 + nameLen + 8;
        const uint32_t devNameLen = u32At(src, pos, "dev name length");
        pos += 4 + devNameLen;
        const uint32_t extraSize = u32At(src, pos, "extra size");
        pos += 4 + extraSize;
        if (pos > src.size()) throw std::runtime_error("stb: truncated entry metadata");
        entries.push_back({begin, pos, begin + 12, begin + 16,
                           u32At(src, begin + 12, "payload size"),
                           u32At(src, begin + 16, "payload offset"), name});
        if (lowered(name) == lowered(entryName)) target = long(entries.size() - 1);
    }
    if (target < 0) throw std::runtime_error("stb: replacement entry not found: " + entryName);

    // The terrain authoring path deliberately preserves the donor chunk span.
    // Keep the entire archive layout and common-header manifest byte-identical
    // when the replacement has the same size; only the target payload changes.
    // This avoids appending a duplicate payload/table and, more importantly,
    // preserves every absolute file-block address already baked into the chunk.
    const RawEntry& targetEntry = entries[size_t(target)];
    if (payload.size() == targetEntry.payloadSize) {
        const size_t payloadEnd = size_t(targetEntry.payloadOffset) + payload.size();
        if (payloadEnd > src.size())
            throw std::runtime_error("stb: replacement target payload is out of bounds");
        std::copy(payload.begin(), payload.end(), src.begin() + targetEntry.payloadOffset);
        std::ofstream output(outPath, std::ios::binary);
        if (!output) throw std::runtime_error("stb: cannot write " + outPath.string());
        output.write(reinterpret_cast<const char*>(src.data()), std::streamsize(src.size()));
        if (!output) throw std::runtime_error("stb: failed writing " + outPath.string());
        return;
    }

    std::vector<uint8_t> out = src;
    out.resize(alignSize(out.size(), alignment), 0);
    const uint32_t payloadOffset = uint32_t(out.size());
    out.insert(out.end(), payload.begin(), payload.end());
    out.resize(alignSize(out.size(), alignment), 0);
    const uint32_t newTableOffset = uint32_t(out.size());
    out.insert(out.end(), src.begin() + tableOffset, src.begin() + tableOffset + 12);
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        const size_t dst = out.size();
        out.insert(out.end(), src.begin() + e.begin, src.begin() + e.end);
        if (long(i) == target) {
            patchU32(out, dst + (e.sizePos - e.begin), uint32_t(payload.size()));
            patchU32(out, dst + (e.offsetPos - e.begin), payloadOffset);
        }
    }
    patchU32(out, 28, newTableOffset);

    std::ofstream output(outPath, std::ios::binary);
    if (!output) throw std::runtime_error("stb: cannot write " + outPath.string());
    output.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
    if (!output) throw std::runtime_error("stb: failed writing " + outPath.string());
}

void replaceStaticMap(const fs::path& srcPath, const fs::path& outPath,
                      const std::string& levelName, const std::string& entryName,
                      const std::vector<uint8_t>& payload,
                      const std::vector<uint8_t>& commonRecord) {
    const Archive archive = Archive::open(srcPath);
    const StaticMap* map = nullptr;
    for (const auto& candidate : archive.staticMaps())
        if (lowered(candidate.levelName) == lowered(levelName)) { map = &candidate; break; }
    if (!map) throw std::runtime_error("stb: static map not found: " + levelName);
    const auto oldRecord = archive.readStaticMapRecord(*map);
    if (commonRecord.size() != oldRecord.size())
        throw std::runtime_error("stb: replacement common record size changed");
    if (commonRecord.size() < 0x75)
        throw std::runtime_error("stb: replacement common record is too short");

    replaceEntryPayload(srcPath, outPath, entryName, payload);
    std::fstream io(outPath, std::ios::binary | std::ios::in | std::ios::out);
    if (!io) throw std::runtime_error("stb: cannot patch common record");
    std::vector<uint8_t> record = commonRecord;
    const uint32_t oldBase = u32At(record, 0x20, "record landscape ptr") - 0x5C;
    const int64_t delta = int64_t(map->relativeOffset) - int64_t(oldBase);
    for (size_t at : {size_t(0x20), size_t(0x24), size_t(0x58),
                      size_t(0x5C), size_t(0x71)}) {
        const int64_t value = int64_t(u32At(record, at, "record pointer")) + delta;
        if (value < 0 || value > 0xFFFFFFFFll)
            throw std::runtime_error("stb: replacement common record rebase overflow");
        patchU32(record, at, uint32_t(value));
    }
    patchU32(record, 4, u32At(oldRecord, 4, "existing bank index"));
    io.seekp(std::streamoff(map->absoluteOffset));
    io.write(reinterpret_cast<const char*>(record.data()), std::streamsize(record.size()));
    if (!io) throw std::runtime_error("stb: failed patching common record");
}

void replaceStaticMaps(const fs::path& srcPath, const fs::path& outPath,
                       const std::vector<StaticMapAppend>& replacements) {
    if (replacements.empty())
        throw std::invalid_argument("stb: replacement batch is empty");
    const Archive archive = Archive::open(srcPath);
    std::ifstream input(srcPath, std::ios::binary);
    std::vector<uint8_t> out((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
    if (input.bad()) throw std::runtime_error("stb: failed reading source bank");
    std::set<std::string> seenLevels, seenEntries;
    for (const auto& replacement : replacements) {
        if (!seenLevels.insert(lowered(replacement.levelName)).second ||
            !seenEntries.insert(lowered(replacement.entryName)).second)
            throw std::invalid_argument("stb: duplicate batch replacement target");
        const StaticMap* map = nullptr;
        for (const auto& candidate : archive.staticMaps())
            if (lowered(candidate.levelName) == lowered(replacement.levelName)) {
                map = &candidate; break;
            }
        const Entry* entry = archive.findEntry(replacement.entryName);
        if (!map || !entry)
            throw std::runtime_error("stb: replacement target not found: " +
                                     replacement.levelName);
        if (replacement.chunk.size() != entry->size)
            throw std::runtime_error("stb: batch replacement payload size changed");
        const auto oldRecord = archive.readStaticMapRecord(*map);
        if (replacement.commonRecord.size() != oldRecord.size() ||
            replacement.commonRecord.size() < 0x75)
            throw std::runtime_error("stb: batch replacement common record size changed");
        if (size_t(entry->offset) + replacement.chunk.size() > out.size() ||
            size_t(map->absoluteOffset) + replacement.commonRecord.size() > out.size())
            throw std::runtime_error("stb: batch replacement target out of bounds");
        std::copy(replacement.chunk.begin(), replacement.chunk.end(),
                  out.begin() + entry->offset);
        auto record = replacement.commonRecord;
        const uint32_t oldBase = u32At(record, 0x20, "record landscape ptr") - 0x5C;
        const int64_t delta = int64_t(map->relativeOffset) - int64_t(oldBase);
        for (size_t at : {size_t(0x20), size_t(0x24), size_t(0x58),
                          size_t(0x5C), size_t(0x71)}) {
            const int64_t value = int64_t(u32At(record, at, "record pointer")) + delta;
            if (value < 0 || value > 0xFFFFFFFFll)
                throw std::runtime_error("stb: batch common record rebase overflow");
            patchU32(record, at, uint32_t(value));
        }
        patchU32(record, 4, u32At(oldRecord, 4, "existing bank index"));
        std::copy(record.begin(), record.end(), out.begin() + map->absoluteOffset);
    }
    std::ofstream output(outPath, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("stb: cannot write " + outPath.string());
    output.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
    if (!output) throw std::runtime_error("stb: failed writing " + outPath.string());
}

void replaceStaticMapsRelayout(const fs::path& srcPath, const fs::path& outPath,
                               const std::vector<StaticMapAppend>& replacements) {
    if (replacements.empty())
        throw std::invalid_argument("stb: relayout replacement batch is empty");
    const Archive archive = Archive::open(srcPath);
    std::ifstream input(srcPath, std::ios::binary);
    if (!input) throw std::runtime_error("stb: cannot open " + srcPath.string());
    std::vector<uint8_t> src((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
    if (src.size() < 32 || u32At(src, 0, "magic") != 0x42424242u)
        throw std::runtime_error("stb: bad source header");
    const uint32_t alignment = u32At(src, 16, "alignment");
    const uint32_t entryCount = u32At(src, 24, "entry count");
    const uint32_t tableOffset = u32At(src, 28, "table offset");
    if (size_t(tableOffset) + 12 > src.size())
        throw std::runtime_error("stb: bad table offset");

    struct RawEntry { size_t begin, end, sizePos, offsetPos; std::string name; };
    std::vector<RawEntry> entries;
    size_t pos = size_t(tableOffset) + 12;
    for (uint32_t i = 0; i < entryCount; ++i) {
        const size_t begin = pos;
        if (u32At(src, pos, "entry magic") != 42)
            throw std::runtime_error("stb: bad entry magic");
        const uint32_t nameLen = u32At(src, pos + 24, "name length");
        if (pos + 28 + nameLen + 12 > src.size())
            throw std::runtime_error("stb: bad entry name");
        std::string name(reinterpret_cast<const char*>(src.data() + pos + 28), nameLen);
        name.erase(std::find(name.begin(), name.end(), '\0'), name.end());
        pos += 28 + nameLen + 8;
        const uint32_t devNameLen = u32At(src, pos, "dev name length");
        pos += 4 + devNameLen;
        const uint32_t extraSize = u32At(src, pos, "extra size");
        pos += 4 + extraSize;
        if (pos > src.size()) throw std::runtime_error("stb: truncated entry metadata");
        entries.push_back({begin, pos, begin + 12, begin + 16, name});
    }

    struct Target { const StaticMapAppend* replacement; const StaticMap* map; size_t entry; };
    std::vector<Target> targets;
    std::set<std::string> seenLevels, seenEntries;
    for (const auto& replacement : replacements) {
        const std::string levelKey = lowered(replacement.levelName);
        const std::string entryKey = lowered(replacement.entryName);
        if (!seenLevels.insert(levelKey).second || !seenEntries.insert(entryKey).second)
            throw std::invalid_argument("stb: duplicate relayout replacement target");
        const StaticMap* map = nullptr;
        for (const auto& candidate : archive.staticMaps())
            if (lowered(candidate.levelName) == levelKey) { map = &candidate; break; }
        size_t entryIndex = entries.size();
        for (size_t i = 0; i < entries.size(); ++i)
            if (lowered(entries[i].name) == entryKey) { entryIndex = i; break; }
        if (!map || entryIndex == entries.size())
            throw std::runtime_error("stb: relayout replacement target not found: " +
                                     replacement.levelName);
        const auto oldRecord = archive.readStaticMapRecord(*map);
        if (replacement.chunk.empty() || replacement.commonRecord.size() != oldRecord.size() ||
            replacement.commonRecord.size() < 0x75)
            throw std::runtime_error("stb: invalid relayout replacement data: " +
                                     replacement.levelName);
        targets.push_back({&replacement, map, entryIndex});
    }

    std::vector<uint8_t> out = src;
    std::vector<uint32_t> newOffsets(targets.size());
    for (size_t i = 0; i < targets.size(); ++i) {
        out.resize(alignSize(out.size(), alignment), 0);
        if (out.size() > std::numeric_limits<uint32_t>::max())
            throw std::runtime_error("stb: relayout payload offset overflow");
        newOffsets[i] = uint32_t(out.size());
        const auto& chunk = targets[i].replacement->chunk;
        out.insert(out.end(), chunk.begin(), chunk.end());
    }
    out.resize(alignSize(out.size(), alignment), 0);
    if (out.size() > std::numeric_limits<uint32_t>::max())
        throw std::runtime_error("stb: relayout table offset overflow");
    const uint32_t newTableOffset = uint32_t(out.size());
    out.insert(out.end(), src.begin() + tableOffset, src.begin() + tableOffset + 12);
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        const size_t dst = out.size();
        out.insert(out.end(), src.begin() + entry.begin, src.begin() + entry.end);
        for (size_t t = 0; t < targets.size(); ++t) if (targets[t].entry == i) {
            const auto& chunk = targets[t].replacement->chunk;
            if (chunk.size() > std::numeric_limits<uint32_t>::max())
                throw std::runtime_error("stb: relayout payload size overflow");
            patchU32(out, dst + entry.sizePos - entry.begin, uint32_t(chunk.size()));
            patchU32(out, dst + entry.offsetPos - entry.begin, newOffsets[t]);
        }
    }
    patchU32(out, 28, newTableOffset);

    for (const auto& target : targets) {
        const auto oldRecord = archive.readStaticMapRecord(*target.map);
        auto record = target.replacement->commonRecord;
        const uint32_t oldBase = u32At(record, 0x20, "record landscape ptr") - 0x5C;
        const int64_t delta = int64_t(target.map->relativeOffset) - int64_t(oldBase);
        for (size_t at : {size_t(0x20), size_t(0x24), size_t(0x58),
                          size_t(0x5C), size_t(0x71)}) {
            const int64_t value = int64_t(u32At(record, at, "record pointer")) + delta;
            if (value < 0 || value > 0xFFFFFFFFll)
                throw std::runtime_error("stb: relayout common record rebase overflow");
            patchU32(record, at, uint32_t(value));
        }
        patchU32(record, 4, u32At(oldRecord, 4, "existing bank index"));
        if (size_t(target.map->absoluteOffset) + record.size() > src.size())
            throw std::runtime_error("stb: relayout common record is out of bounds");
        std::copy(record.begin(), record.end(), out.begin() + target.map->absoluteOffset);
    }

    std::ofstream output(outPath, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("stb: cannot write " + outPath.string());
    output.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
    if (!output) throw std::runtime_error("stb: failed writing " + outPath.string());
}

namespace {
struct CompactPlan {
    std::vector<uint8_t> src;
    uint32_t alignment = 0, entryCount = 0, tableOffset = 0;
    struct Row { size_t begin, end, sizePos, offsetPos; uint32_t size, offset; };
    std::vector<Row> rows;
    uint64_t compactSize = 0;
};

CompactPlan planCompaction(const fs::path& srcPath) {
    CompactPlan plan;
    std::ifstream input(srcPath, std::ios::binary);
    if (!input) throw std::runtime_error("stb: cannot open " + srcPath.string());
    plan.src.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    const auto& src = plan.src;
    if (src.size() < 32 || u32At(src, 0, "magic") != 0x42424242u) throw std::runtime_error("stb: bad magic");
    plan.alignment = u32At(src, 16, "alignment");
    plan.entryCount = u32At(src, 24, "entry count");
    plan.tableOffset = u32At(src, 28, "table offset");
    if (plan.alignment == 0 || size_t(plan.tableOffset) + 12 > src.size()) throw std::runtime_error("stb: bad header");
    size_t pos = size_t(plan.tableOffset) + 12;
    for (uint32_t i = 0; i < plan.entryCount; ++i) {
        const size_t begin = pos;
        if (u32At(src, pos, "entry magic") != 42) throw std::runtime_error("stb: bad entry magic");
        const uint32_t nameLen = u32At(src, pos + 24, "name length");
        pos += 28 + nameLen + 8;
        const uint32_t devNameLen = u32At(src, pos, "dev name length");
        pos += 4 + devNameLen;
        const uint32_t extraSize = u32At(src, pos, "extra size");
        pos += 4 + extraSize;
        if (pos > src.size()) throw std::runtime_error("stb: truncated entry metadata");
        CompactPlan::Row r{begin, pos, begin + 12, begin + 16,
                           u32At(src, begin + 12, "payload size"), u32At(src, begin + 16, "payload offset")};
        if (uint64_t(r.offset) + r.size > src.size()) throw std::runtime_error("stb: payload out of range");
        plan.rows.push_back(r);
    }
    if (pos != src.size()) throw std::runtime_error("stb: bytes after the table");
    uint64_t at = 32;
    for (const auto& r : plan.rows) at = alignSize(size_t(at), plan.alignment) + r.size;
    at = alignSize(size_t(at), plan.alignment);            // the table sits on an alignment boundary
    plan.compactSize = at + (src.size() - plan.tableOffset);
    return plan;
}
} // namespace

CompactReport compactMeasure(const fs::path& srcPath) {
    // header + table only (the GUI polls this; the bank is ~570 MB)
    std::ifstream f(srcPath, std::ios::binary);
    if (!f) throw std::runtime_error("stb: cannot open " + srcPath.string());
    f.seekg(0, std::ios::end);
    const uint64_t fileSize = uint64_t(f.tellg());
    f.seekg(0);
    std::vector<uint8_t> hdr(32);
    f.read(reinterpret_cast<char*>(hdr.data()), 32);
    if (!f || u32At(hdr, 0, "magic") != 0x42424242u) throw std::runtime_error("stb: bad magic");
    const uint32_t alignment = u32At(hdr, 16, "alignment"), entryCount = u32At(hdr, 24, "entry count"), tableOffset = u32At(hdr, 28, "table offset");
    if (alignment == 0 || uint64_t(tableOffset) + 12 > fileSize) throw std::runtime_error("stb: bad header");
    std::vector<uint8_t> table(size_t(fileSize - tableOffset));
    f.seekg(tableOffset);
    f.read(reinterpret_cast<char*>(table.data()), std::streamsize(table.size()));
    if (!f) throw std::runtime_error("stb: truncated table");
    size_t pos = 12;
    uint64_t at = 32;
    for (uint32_t i = 0; i < entryCount; ++i) {
        if (u32At(table, pos, "entry magic") != 42) throw std::runtime_error("stb: bad entry magic");
        const uint32_t size = u32At(table, pos + 12, "payload size");
        const uint32_t nameLen = u32At(table, pos + 24, "name length");
        pos += 28 + nameLen + 8;
        const uint32_t devNameLen = u32At(table, pos, "dev name length");
        pos += 4 + devNameLen;
        const uint32_t extraSize = u32At(table, pos, "extra size");
        pos += 4 + extraSize;
        if (pos > table.size()) throw std::runtime_error("stb: truncated entry metadata");
        at = alignSize(size_t(at), alignment) + size;
    }
    at = alignSize(size_t(at), alignment);
    CompactReport rep;
    rep.bytesBefore = fileSize;
    rep.bytesAfter = at + table.size();
    rep.entries = entryCount;
    return rep;
}

CompactReport compactBank(const fs::path& srcPath, const fs::path& outPath) {
    const CompactPlan plan = planCompaction(srcPath);
    const auto& src = plan.src;
    std::vector<uint8_t> out;
    out.reserve(size_t(plan.compactSize));
    out.insert(out.end(), src.begin(), src.begin() + 32);
    std::vector<uint32_t> offsets;
    offsets.reserve(plan.rows.size());
    for (const auto& r : plan.rows) {
        out.resize(alignSize(out.size(), plan.alignment), 0);
        offsets.push_back(uint32_t(out.size()));
        out.insert(out.end(), src.begin() + r.offset, src.begin() + r.offset + r.size);
    }
    out.resize(alignSize(out.size(), plan.alignment), 0);
    if (out.size() > std::numeric_limits<uint32_t>::max()) throw std::runtime_error("stb: table offset exceeds 32-bit range");
    const uint32_t newTableOffset = uint32_t(out.size());
    out.insert(out.end(), src.begin() + plan.tableOffset, src.begin() + plan.tableOffset + 12);
    for (size_t i = 0; i < plan.rows.size(); ++i) {
        const auto& r = plan.rows[i];
        const size_t dst = out.size();
        out.insert(out.end(), src.begin() + r.begin, src.begin() + r.end);
        patchU32(out, dst + (r.offsetPos - r.begin), offsets[i]);
    }
    patchU32(out, 28, newTableOffset);
    if (out.size() != plan.compactSize) throw std::runtime_error("stb: compaction size mismatch");

    std::ofstream output(outPath, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("stb: cannot write " + outPath.string());
    output.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
    if (!output) throw std::runtime_error("stb: failed writing " + outPath.string());
    CompactReport rep;
    rep.bytesBefore = src.size();
    rep.bytesAfter = out.size();
    rep.entries = plan.entryCount;
    return rep;
}

} // namespace forge::stb
