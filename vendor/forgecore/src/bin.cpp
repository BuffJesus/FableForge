#include "forge/bin.hpp"

#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>

#define MINIZ_HEADER_FILE_ONLY
#include "miniz/miniz.h"

namespace fs = std::filesystem;

namespace forge::bin {
namespace {

std::vector<uint8_t> readFile(const fs::path& path, const char* what) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        throw std::runtime_error(std::string(what) + ": cannot open " +
                                 path.string());
    }
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
}

uint32_t u32At(const std::vector<uint8_t>& bytes, size_t pos, const char* what) {
    if (pos + 4 > bytes.size()) {
        throw std::runtime_error(std::string("bin: truncated reading ") + what);
    }
    uint32_t value;
    std::memcpy(&value, bytes.data() + pos, 4);
    return value;
}

struct NamesTable {
    std::vector<uint8_t> header;                          // 20 bytes
    std::vector<std::pair<uint32_t, std::string>> ordered; // (crc, name)
    std::map<uint32_t, std::string> byOffset; // name-text offset -> name
};

// names.bin: 20-byte header, then {u32 crc, ASCIIZ name}. A reference is the
// offset of the name text relative to the end of the header.
NamesTable loadNames(const fs::path& path) {
    const auto bytes = readFile(path, "names.bin");
    if (bytes.size() < 20) {
        throw std::runtime_error("names.bin: too small");
    }
    const uint32_t count = u32At(bytes, 8, "names count");

    NamesTable table;
    table.header.assign(bytes.begin(), bytes.begin() + 20);
    size_t pos = 20;
    for (uint32_t i = 0; i < count; ++i) {
        if (pos + 4 >= bytes.size()) {
            throw std::runtime_error("names.bin: truncated entry");
        }
        const uint32_t nameOffset = static_cast<uint32_t>(pos - 20 + 4);
        const uint32_t crc = u32At(bytes, pos, "name crc");
        pos += 4;
        const size_t start = pos;
        while (pos < bytes.size() && bytes[pos] != 0) ++pos;
        std::string name(bytes.begin() + static_cast<ptrdiff_t>(start),
                         bytes.begin() + static_cast<ptrdiff_t>(pos));
        table.byOffset.emplace(nameOffset, name);
        table.ordered.emplace_back(crc, std::move(name));
        ++pos; // null terminator
    }
    return table;
}

// Fable's name CRC = the engine's crc0 (CCRC::Calc(0,...) / CCharString::ComputeCRC32
// @0x00404310): reflected CRC-32, poly 0xEDB88320, seed 0, NO final inversion.
// Verified against retail names.bin: 13593/13593 stored CRCs equal crc0(name);
// crc0("CREATURE_TRADER_01")=0xAA22BB08, crc0("Graphic")=0x2E6B63C8 (field tag).
// The old SilverChest formula (0xFFFFFFFF - mz_crc32) matched 0/13593 real names, so
// every NEWLY appended name got an un-resolvable CRC and the engine could not find
// the def by name (docs/DEF_LOAD_CONTRACT.md, def-load-contract RE 2026-07-24).
uint32_t nameCrc(const std::string& name) {
    if (name.empty()) return 0;
    uint32_t crc = 0; // seed 0 (NOT 0xFFFFFFFF)
    for (const unsigned char c : name) {
        crc ^= c;
        for (int i = 0; i < 8; ++i)
            crc = (crc >> 1) ^ (0xEDB88320u & static_cast<uint32_t>(-static_cast<int32_t>(crc & 1)));
    }
    return crc; // NO ^0xFFFFFFFF at the end
}

void putU32(std::vector<uint8_t>& out, uint32_t value) {
    const size_t at = out.size();
    out.resize(at + 4);
    std::memcpy(out.data() + at, &value, 4);
}

std::string nameFor(const std::map<uint32_t, std::string>& names, int32_t ref) {
    if (ref < 0) return {};
    const auto it = names.find(static_cast<uint32_t>(ref));
    return it != names.end() ? it->second : std::string();
}

} // namespace

File File::open(const fs::path& namesPath, const fs::path& binPath) {
    auto namesTable = loadNames(namesPath);
    const auto& names = namesTable.byOffset;
    const auto bytes = readFile(binPath, "bin");
    if (bytes.size() < 13) {
        throw std::runtime_error("bin: too small: " + binPath.string());
    }
    if (bytes[1] == 0xAA) {
        throw std::runtime_error("bin: Xbox-format bin not supported: " +
                                 binPath.string());
    }

    const uint32_t entryCount = u32At(bytes, 9, "entry count");
    const size_t mainTable = 13;
    const size_t chunkCountPos = mainTable + static_cast<size_t>(entryCount) * 12;
    const uint32_t chunkCount = u32At(bytes, chunkCountPos, "chunk count");
    const size_t chunkTable = chunkCountPos + 4;
    const size_t dataOffset = chunkTable + static_cast<size_t>(chunkCount) * 8 + 4;

    File file;
    file.binHeader_.assign(bytes.begin(), bytes.begin() + 9);
    file.namesHeader_ = std::move(namesTable.header);
    file.names_ = std::move(namesTable.ordered);
    file.entries_.reserve(entryCount);

    for (uint32_t chunk = 1; chunk < chunkCount; ++chunk) {
        const auto pairAt = [&](uint32_t index, size_t field) {
            return static_cast<int32_t>(
                u32At(bytes, chunkTable + static_cast<size_t>(index) * 8 + field,
                      "chunk table"));
        };
        const int32_t firstEntry = pairAt(chunk - 1, 0);
        const int32_t compressedStart = pairAt(chunk - 1, 4);
        const int32_t entriesEnd = pairAt(chunk, 0);
        const int32_t compressedEnd = pairAt(chunk, 4);

        const int32_t compressedLength = compressedEnd - compressedStart;
        const int32_t entriesInChunk = entriesEnd - firstEntry;
        if (compressedLength <= 0 || entriesInChunk <= 0) continue;

        const size_t src = dataOffset + static_cast<size_t>(compressedStart);
        if (src + static_cast<size_t>(compressedLength) > bytes.size()) {
            throw std::runtime_error("bin: chunk data out of range");
        }

        std::vector<uint8_t> inflated(65536);
        mz_ulong inflatedLen = static_cast<mz_ulong>(inflated.size());
        const int rc = mz_uncompress(inflated.data(), &inflatedLen,
                                     bytes.data() + src,
                                     static_cast<mz_ulong>(compressedLength));
        if (rc != MZ_OK) {
            throw std::runtime_error("bin: zlib inflate failed on chunk " +
                                     std::to_string(chunk) + " (rc " +
                                     std::to_string(rc) + ")");
        }
        inflated.resize(inflatedLen);

        for (int32_t local = 0; local < entriesInChunk; ++local) {
            const uint32_t tableIndex =
                static_cast<uint32_t>(firstEntry) + static_cast<uint32_t>(local);
            if (tableIndex >= entryCount) break;
            const size_t row = mainTable + static_cast<size_t>(tableIndex) * 12;

            Entry entry;
            entry.definition =
                nameFor(names, static_cast<int32_t>(u32At(bytes, row, "def ref")));
            entry.name = nameFor(
                names, static_cast<int32_t>(u32At(bytes, row + 4, "name ref")));
            entry.indexInDefinition =
                static_cast<int32_t>(u32At(bytes, row + 8, "def index"));

            if (static_cast<size_t>(local) * 2 + 2 > inflated.size()) {
                throw std::runtime_error("bin: chunk offset table truncated");
            }
            uint16_t start;
            std::memcpy(&start, inflated.data() + static_cast<size_t>(local) * 2, 2);
            size_t end = inflated.size();
            if (local + 1 < entriesInChunk) {
                uint16_t next;
                std::memcpy(&next,
                            inflated.data() + (static_cast<size_t>(local) + 1) * 2,
                            2);
                end = next;
            }
            if (start <= end && end <= inflated.size()) {
                entry.data.assign(inflated.begin() + start,
                                  inflated.begin() + static_cast<ptrdiff_t>(end));
            }
            file.entries_.push_back(std::move(entry));
        }
    }

    return file;
}

const Entry* File::find(std::string_view name) const {
    for (const Entry& entry : entries_) {
        if (entry.name == name) return &entry;
    }
    return nullptr;
}

void File::setEntryData(size_t index, std::vector<uint8_t> data) {
    if (index >= entries_.size()) {
        throw std::out_of_range("bin: entry index out of range");
    }
    entries_[index].data = std::move(data);
}

size_t File::addEntry(std::string definition, std::string name,
                      std::vector<uint8_t> data) {
    Entry entry;
    entry.definition = std::move(definition);
    entry.name = std::move(name);
    entry.data = std::move(data);
    entries_.push_back(std::move(entry));
    return entries_.size() - 1;
}

void File::save(const fs::path& namesPath, const fs::path& binPath) const {
    // Name table: keep the loaded order (offsets recompute deterministically,
    // so an unchanged name set writes byte-identical entries), append any new
    // names referenced by entries.
    std::vector<std::pair<uint32_t, std::string>> names = names_;
    std::map<std::string, uint32_t> offsetByName;
    uint32_t nextOffset = 4;
    for (const auto& [crc, name] : names) {
        offsetByName.emplace(name, nextOffset);
        nextOffset += static_cast<uint32_t>(name.size()) + 5;
    }
    auto ensureName = [&](const std::string& name) {
        const auto it = offsetByName.find(name);
        if (it != offsetByName.end()) return it->second;
        const uint32_t offset = nextOffset;
        names.emplace_back(nameCrc(name), name);
        offsetByName.emplace(name, offset);
        nextOffset += static_cast<uint32_t>(name.size()) + 5;
        return offset;
    };

    // Main table rows; indexInDefinition is the running occurrence count per
    // definition, recomputed exactly as the reference writer does.
    std::vector<uint8_t> mainTable;
    std::map<std::string, int32_t> perDefinition;
    for (const Entry& entry : entries_) {
        const uint32_t defOffset = ensureName(entry.definition);
        const uint32_t nameValue =
            entry.name.empty() ? 0xFFFFFFFFu : ensureName(entry.name);
        const int32_t defIndex = perDefinition[entry.definition]++;
        putU32(mainTable, defOffset);
        putU32(mainTable, nameValue);
        putU32(mainTable, static_cast<uint32_t>(defIndex));
    }

    // Chunk the payloads (<= 32 KiB target per inflated chunk).
    constexpr size_t kMaxChunk = 32768;
    std::vector<std::vector<uint8_t>> compressedChunks;
    std::vector<uint32_t> chunkStartIndices, chunkOffsets;
    uint32_t totalCompressed = 0;

    std::vector<uint16_t> offsets;
    std::vector<uint8_t> payloadBuf;
    size_t entriesInChunk = 0;

    const auto flush = [&](size_t endIndex) {
        if (entriesInChunk == 0) return;
        const size_t offsetsSize = entriesInChunk * 2;
        std::vector<uint8_t> chunk(offsetsSize + payloadBuf.size());
        for (size_t i = 0; i < offsets.size(); ++i) {
            const uint16_t value = static_cast<uint16_t>(offsetsSize + offsets[i]);
            std::memcpy(chunk.data() + i * 2, &value, 2);
        }
        std::memcpy(chunk.data() + offsetsSize, payloadBuf.data(),
                    payloadBuf.size());

        mz_ulong cap = mz_compressBound(static_cast<mz_ulong>(chunk.size()));
        std::vector<uint8_t> compressed(cap);
        if (mz_compress(compressed.data(), &cap, chunk.data(),
                        static_cast<mz_ulong>(chunk.size())) != MZ_OK) {
            throw std::runtime_error("bin: zlib compress failed");
        }
        compressed.resize(cap);

        chunkStartIndices.push_back(
            static_cast<uint32_t>(endIndex - entriesInChunk));
        chunkOffsets.push_back(totalCompressed);
        totalCompressed += static_cast<uint32_t>(compressed.size());
        compressedChunks.push_back(std::move(compressed));

        offsets.clear();
        payloadBuf.clear();
        entriesInChunk = 0;
    };

    for (size_t i = 0; i < entries_.size(); ++i) {
        const Entry& entry = entries_[i];
        const size_t projected =
            (entriesInChunk + 1) * 2 + payloadBuf.size() + entry.data.size();
        if (projected > kMaxChunk) flush(i);
        offsets.push_back(static_cast<uint16_t>(payloadBuf.size()));
        ++entriesInChunk;
        payloadBuf.insert(payloadBuf.end(), entry.data.begin(), entry.data.end());
    }
    flush(entries_.size());

    // game.bin: header, entry count, main table, chunk table (with the
    // (entryCount, totalCompressed) end sentinel plus one trailing u32),
    // then the compressed chunks.
    std::vector<uint8_t> out(binHeader_.begin(), binHeader_.end());
    putU32(out, static_cast<uint32_t>(entries_.size()));
    out.insert(out.end(), mainTable.begin(), mainTable.end());
    putU32(out, static_cast<uint32_t>(compressedChunks.size() + 1));
    for (size_t i = 0; i < compressedChunks.size(); ++i) {
        putU32(out, chunkStartIndices[i]);
        putU32(out, chunkOffsets[i]);
    }
    putU32(out, static_cast<uint32_t>(entries_.size()));
    putU32(out, totalCompressed);
    putU32(out, totalCompressed);
    for (const auto& chunk : compressedChunks) {
        out.insert(out.end(), chunk.begin(), chunk.end());
    }

    // names.bin: preserved header with entry count and table size patched.
    std::vector<uint8_t> namesOut(namesHeader_.begin(), namesHeader_.end());
    const uint32_t nameCount = static_cast<uint32_t>(names.size());
    std::memcpy(namesOut.data() + 8, &nameCount, 4);
    const uint32_t tableSize = nextOffset - 4;
    std::memcpy(namesOut.data() + 12, &tableSize, 4);
    for (const auto& [crc, name] : names) {
        putU32(namesOut, crc);
        namesOut.insert(namesOut.end(), name.begin(), name.end());
        namesOut.push_back(0);
    }

    const auto write = [](const fs::path& path, const std::vector<uint8_t>& data,
                          const char* what) {
        std::ofstream stream(path, std::ios::binary);
        if (!stream.is_open()) {
            throw std::runtime_error(std::string(what) + ": cannot write " +
                                     path.string());
        }
        stream.write(reinterpret_cast<const char*>(data.data()),
                     static_cast<std::streamsize>(data.size()));
    };
    write(namesPath, namesOut, "names.bin");
    write(binPath, out, "bin");
}

std::vector<ObjectFamily> decodeObjectFamilies(const File& file) {
    std::vector<ObjectFamily> families;
    const auto& entries = file.entries();
    for (uint32_t i = 0; i < entries.size(); ++i) {
        const Entry& entry = entries[i];
        if (entry.definition != "OBJECT_FAMILY" || entry.data.size() < 13) continue;

        ObjectFamily family;
        family.entryIndex = i;
        family.name = entry.name;
        uint32_t count;
        std::memcpy(&count, entry.data.data() + 9, 4);
        for (uint32_t m = 0; m < count; ++m) {
            const size_t off = 13 + static_cast<size_t>(m) * 8;
            if (off + 8 > entry.data.size()) break;
            FamilyMember member;
            std::memcpy(&member.objectIndex, entry.data.data() + off, 4);
            std::memcpy(&member.weight, entry.data.data() + off + 4, 4);
            family.members.push_back(member);
        }
        families.push_back(std::move(family));
    }
    return families;
}

std::vector<RewardDef> decodeContainerRewards(const File& file) {
    std::vector<RewardDef> rewards;
    const auto& entries = file.entries();
    for (uint32_t i = 0; i < entries.size(); ++i) {
        const Entry& entry = entries[i];
        if (entry.definition != "CContainerRewardHeroDef" ||
            entry.data.size() < 11) {
            continue;
        }

        RewardDef reward;
        reward.entryIndex = i;
        uint32_t count;
        std::memcpy(&count, entry.data.data() + 7, 4);
        for (uint32_t m = 0; m < count; ++m) {
            const size_t off = 11 + static_cast<size_t>(m) * 4;
            if (off + 4 > entry.data.size()) break;
            uint32_t familyIndex;
            std::memcpy(&familyIndex, entry.data.data() + off, 4);
            reward.familyIndices.push_back(familyIndex);
        }
        rewards.push_back(std::move(reward));
    }
    return rewards;
}

} // namespace forge::bin
