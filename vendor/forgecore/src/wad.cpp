#include "forge/wad.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>

namespace fs = std::filesystem;

namespace forge::wad {

// Retail WAD entry payloads are all aligned to this boundary.
constexpr size_t kEntryAlignment = 2048;

namespace {

uint32_t readU32(std::istream& in, const char* what) {
    uint32_t value = 0;
    in.read(reinterpret_cast<char*>(&value), sizeof(value));
    if (!in) {
        throw std::runtime_error(std::string("wad: truncated while reading ") + what);
    }
    return value;
}

std::string readString(std::istream& in, uint32_t length, const char* what) {
    std::string value(length, '\0');
    if (length > 0) {
        in.read(value.data(), length);
        if (!in) {
            throw std::runtime_error(std::string("wad: truncated while reading ") + what);
        }
        value.erase(std::find(value.begin(), value.end(), '\0'), value.end());
    }
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

} // namespace

Archive Archive::open(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("wad: cannot open " + path.string());
    }

    file.seekg(20, std::ios::beg);
    const uint32_t entryCount = readU32(file, "entry count");

    file.seekg(28, std::ios::beg);
    const uint32_t footerOffset = readU32(file, "footer offset");

    file.seekg(footerOffset, std::ios::beg);
    if (!file) {
        throw std::runtime_error("wad: footer offset out of range");
    }

    const uint32_t statsCount = readU32(file, "stats count");
    file.seekg(static_cast<std::streamoff>(statsCount) * 8, std::ios::cur);

    Archive archive;
    archive.path_ = path;
    archive.entries_.reserve(entryCount);

    for (uint32_t i = 0; i < entryCount; ++i) {
        const uint32_t magic = readU32(file, "entry magic");
        if (magic != 42) {
            throw std::runtime_error("wad: entry " + std::to_string(i) +
                                     " has bad magic " + std::to_string(magic));
        }

        Entry entry;
        entry.id = readU32(file, "entry id");
        entry.type = readU32(file, "entry type");
        entry.size = readU32(file, "entry size");
        entry.offset = readU32(file, "entry offset");
        entry.crc = readU32(file, "entry crc");

        const uint32_t nameLen = readU32(file, "entry name length");
        entry.name = readString(file, nameLen, "entry name");

        entry.timestamp = readU32(file, "entry timestamp");

        const uint32_t depCount = readU32(file, "dependency count");
        entry.dependencies.reserve(depCount);
        for (uint32_t d = 0; d < depCount; ++d) {
            const uint32_t depLen = readU32(file, "dependency length");
            entry.dependencies.push_back(readString(file, depLen, "dependency name"));
        }

        const uint32_t infoSize = readU32(file, "entry info size");
        entry.info.resize(infoSize);
        if (infoSize)
            file.read(reinterpret_cast<char*>(entry.info.data()), infoSize);
        if (!file) {
            throw std::runtime_error("wad: truncated entry info block");
        }

        archive.entries_.push_back(std::move(entry));
    }

    return archive;
}

std::vector<uint8_t> Archive::read(const Entry& entry) const {
    std::ifstream file(path_, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("wad: cannot open " + path_.string());
    }

    std::vector<uint8_t> buffer(entry.size);
    file.seekg(entry.offset, std::ios::beg);
    file.read(reinterpret_cast<char*>(buffer.data()), entry.size);
    if (!file) {
        throw std::runtime_error("wad: truncated payload for " + entry.name);
    }
    return buffer;
}

bool Archive::payloadEqualsFile(const Entry& entry, const fs::path& path) const {
    std::error_code error;
    const auto size = fs::file_size(path, error);
    if (error) {
        throw std::runtime_error("wad: cannot stat comparison file " +
                                 path.string());
    }
    if (size != entry.size) return false;

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("wad: cannot open comparison file " +
                                 path.string());
    }
    const auto payload = read(entry);
    std::vector<uint8_t> candidate(entry.size);
    file.read(reinterpret_cast<char*>(candidate.data()), entry.size);
    if (!file) {
        throw std::runtime_error("wad: truncated comparison file " +
                                 path.string());
    }
    return payload == candidate;
}

size_t Archive::extract(const fs::path& outDir, const std::string& filter,
                        const std::function<void(const Entry&)>& onFile) const {
    size_t written = 0;

    for (const Entry& entry : entries_) {
        if (entry.size == 0 || entry.name.empty()) continue;
        if (!containsIgnoreCase(entry.name, filter)) continue;

        if (onFile) onFile(entry);

        std::string relative = entry.name;
        std::replace(relative.begin(), relative.end(), '/', '\\');

        fs::path outPath = outDir;
        for (size_t start = 0; start < relative.size();) {
            size_t end = relative.find('\\', start);
            if (end == std::string::npos) end = relative.size();
            std::string part = relative.substr(start, end - start);
            if (!part.empty() && part != "." && part != "..") {
                outPath /= part;
            }
            start = end + 1;
        }

        fs::create_directories(outPath.parent_path());

        const std::vector<uint8_t> payload = read(entry);
        std::ofstream out(outPath, std::ios::binary);
        if (!out.is_open()) {
            throw std::runtime_error("wad: cannot write " + outPath.string());
        }
        out.write(reinterpret_cast<const char*>(payload.data()),
                  static_cast<std::streamsize>(payload.size()));
        ++written;
    }

    return written;
}

size_t repack(const fs::path& srcPath,
              const std::map<std::string, std::vector<uint8_t>>& replacements,
              const fs::path& outPath) {
    std::ifstream in(srcPath, std::ios::binary);
    if (!in.is_open()) {
        throw std::runtime_error("wad: cannot open " + srcPath.string());
    }
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
    in.close();
    if (bytes.size() < 32) {
        throw std::runtime_error("wad: file too small: " + srcPath.string());
    }

    auto u32At = [&](size_t pos) {
        uint32_t value;
        std::memcpy(&value, bytes.data() + pos, 4);
        return value;
    };

    const uint32_t entryCount = u32At(20);
    const uint32_t footerOffset = u32At(28);
    if (footerOffset > bytes.size()) {
        throw std::runtime_error("wad: footer offset out of range");
    }

    std::vector<char> data(bytes.begin(),
                           bytes.begin() + static_cast<ptrdiff_t>(footerOffset));
    std::vector<char> footer(bytes.begin() + static_cast<ptrdiff_t>(footerOffset),
                             bytes.end());

    // Lowercase-keyed view of the replacements, tracking which get used.
    std::map<std::string, const std::vector<uint8_t>*> pending;
    for (const auto& [name, payload] : replacements) {
        std::string key = name;
        std::replace(key.begin(), key.end(), '/', '\\');
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        pending[key] = &payload;
    }

    // Walk the footer, mirroring open(), tracking each entry's field positions
    // relative to the footer start.
    size_t pos = 4 + static_cast<size_t>(u32At(footerOffset)) * 8; // skip stats
    size_t replaced = 0;
    bool payloadRelocated = false;
    auto footerU32 = [&](size_t at) {
        if (at + 4 > footer.size()) {
            throw std::runtime_error("wad: truncated footer");
        }
        uint32_t value;
        std::memcpy(&value, footer.data() + at, 4);
        return value;
    };

    for (uint32_t i = 0; i < entryCount; ++i) {
        if (footerU32(pos) != 42) {
            throw std::runtime_error("wad: entry " + std::to_string(i) +
                                     " has bad magic");
        }
        const size_t sizePos = pos + 12;
        const size_t offsetPos = pos + 16;
        const uint32_t size = footerU32(sizePos);
        const uint32_t offset = footerU32(offsetPos);

        const uint32_t nameLen = footerU32(pos + 24);
        std::string name(footer.data() + pos + 28, nameLen);
        name.erase(std::find(name.begin(), name.end(), '\0'), name.end());
        pos += 28 + nameLen + 4; // past name + timestamp

        const uint32_t depCount = footerU32(pos);
        pos += 4;
        for (uint32_t d = 0; d < depCount; ++d) {
            pos += 4 + footerU32(pos);
        }
        pos += 4 + footerU32(pos); // info block

        std::string key = name;
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        const auto it = pending.find(key);
        if (it == pending.end()) continue;
        const std::vector<uint8_t>& payload = *it->second;

        if (payload.size() == size) {
            std::memcpy(data.data() + offset, payload.data(), payload.size());
        } else {
            payloadRelocated = true;
            data.resize((data.size() + kEntryAlignment - 1) /
                            kEntryAlignment * kEntryAlignment,
                        '\0');
            const uint32_t newOffset = static_cast<uint32_t>(data.size());
            const uint32_t newSize = static_cast<uint32_t>(payload.size());
            data.insert(data.end(), payload.begin(), payload.end());
            std::memcpy(footer.data() + sizePos, &newSize, 4);
            std::memcpy(footer.data() + offsetPos, &newOffset, 4);
        }
        ++replaced;
        pending.erase(it);
    }

    if (!pending.empty()) {
        throw std::runtime_error("wad: replacement matches no entry: " +
                                 pending.begin()->first);
    }

    if (payloadRelocated) {
        data.resize((data.size() + kEntryAlignment - 1) /
                        kEntryAlignment * kEntryAlignment,
                    '\0');
    }
    const uint32_t newFooterOffset = static_cast<uint32_t>(data.size());
    std::memcpy(data.data() + 28, &newFooterOffset, 4);

    std::ofstream out(outPath, std::ios::binary);
    if (!out.is_open()) {
        throw std::runtime_error("wad: cannot write " + outPath.string());
    }
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    out.write(footer.data(), static_cast<std::streamsize>(footer.size()));
    return replaced;
}

namespace {
struct AppendSpec {
    std::string donorName;
    std::string newName;
    std::vector<uint8_t> payload;
    bool native = false;
};

size_t appendEntriesImpl(const fs::path& srcPath,
                         const std::vector<AppendSpec>& specs,
                         const fs::path& outPath) {
    if (specs.empty()) {
        return repack(srcPath, {}, outPath);
    }
    std::ifstream in(srcPath, std::ios::binary);
    if (!in) throw std::runtime_error("wad: cannot open " + srcPath.string());
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
    if (bytes.size() < 32) throw std::runtime_error("wad: source is too small");
    auto u32 = [&](const std::vector<char>& data, size_t at, const char* what) {
        if (at + 4 > data.size())
            throw std::runtime_error(std::string("wad: truncated ") + what);
        uint32_t value = 0;
        std::memcpy(&value, data.data() + at, 4);
        return value;
    };
    const uint32_t entryCount = u32(bytes, 20, "entry count");
    const uint32_t footerOffset = u32(bytes, 28, "footer offset");
    if (footerOffset > bytes.size())
        throw std::runtime_error("wad: footer offset out of range");
    std::vector<char> data(bytes.begin(),
                           bytes.begin() + static_cast<ptrdiff_t>(footerOffset));
    std::vector<char> footer(bytes.begin() + static_cast<ptrdiff_t>(footerOffset),
                             bytes.end());

    struct Metadata {
        uint32_t id = 0, type = 0, size = 0, offset = 0, crc = 0, timestamp = 0;
        std::string name;
        bool nameTerminated = false;
        std::vector<std::pair<std::string, bool>> dependencies;
        std::vector<char> info;
    };
    std::vector<Metadata> metadata;
    metadata.reserve(entryCount);
    size_t pos = 4 + size_t(u32(footer, 0, "stats count")) * 8;
    uint32_t maxId = 0;
    for (uint32_t i = 0; i < entryCount; ++i) {
        if (u32(footer, pos, "entry magic") != 42)
            throw std::runtime_error("wad: bad entry magic");
        Metadata item;
        item.id = u32(footer, pos + 4, "entry id");
        item.type = u32(footer, pos + 8, "entry type");
        item.size = u32(footer, pos + 12, "entry size");
        item.offset = u32(footer, pos + 16, "entry offset");
        item.crc = u32(footer, pos + 20, "entry crc");
        const uint32_t nameLength = u32(footer, pos + 24, "entry name length");
        pos += 28;
        if (pos + nameLength > footer.size())
            throw std::runtime_error("wad: truncated entry name");
        item.name.assign(footer.data() + pos, nameLength);
        item.nameTerminated = !item.name.empty() && item.name.back() == '\0';
        if (item.nameTerminated) item.name.pop_back();
        pos += nameLength;
        item.timestamp = u32(footer, pos, "timestamp");
        pos += 4;
        const uint32_t dependencyCount = u32(footer, pos, "dependency count");
        pos += 4;
        for (uint32_t dependency = 0; dependency < dependencyCount; ++dependency) {
            const uint32_t length = u32(footer, pos, "dependency length");
            pos += 4;
            if (pos + length > footer.size())
                throw std::runtime_error("wad: truncated dependency");
            std::string value(footer.data() + pos, length);
            const bool terminated = !value.empty() && value.back() == '\0';
            if (terminated) value.pop_back();
            item.dependencies.emplace_back(std::move(value), terminated);
            pos += length;
        }
        const uint32_t infoSize = u32(footer, pos, "info size");
        pos += 4;
        if (pos + infoSize > footer.size())
            throw std::runtime_error("wad: truncated info block");
        item.info.assign(footer.begin() + static_cast<ptrdiff_t>(pos),
                         footer.begin() + static_cast<ptrdiff_t>(pos + infoSize));
        pos += infoSize;
        if (uint64_t(item.offset) + item.size > footerOffset)
            throw std::runtime_error("wad: entry payload outside data region");
        maxId = std::max(maxId, item.id);
        metadata.push_back(std::move(item));
    }
    const size_t entriesEnd = pos;

    std::map<std::string, const Metadata*> byName;
    for (const auto& item : metadata) {
        std::string key = item.name;
        std::replace(key.begin(), key.end(), '/', '\\');
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        byName.emplace(std::move(key), &item);
    }
    auto keyFor = [](std::string value) {
        std::replace(value.begin(), value.end(), '/', '\\');
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    };
    std::set<std::string> requestedNames;
    for (const auto& spec : specs) {
        if (spec.newName.empty() || (!spec.native && spec.donorName.empty()))
            throw std::runtime_error("wad: append names cannot be empty");
        const std::string newKey = keyFor(spec.newName);
        if (byName.contains(newKey) || !requestedNames.insert(newKey).second)
            throw std::runtime_error("wad: appended entry already exists: " + spec.newName);
        if (!spec.native && !byName.contains(keyFor(spec.donorName)))
            throw std::runtime_error("wad: donor entry not found: " + spec.donorName);
    }

    auto appendU32 = [](std::vector<char>& target, uint32_t value) {
        const size_t at = target.size();
        target.resize(at + 4);
        std::memcpy(target.data() + at, &value, 4);
    };
    auto stem = [](std::string path) {
        std::replace(path.begin(), path.end(), '\\', '/');
        const size_t slash = path.find_last_of('/');
        std::string leaf = slash == std::string::npos ? path : path.substr(slash + 1);
        const size_t dot = leaf.find_last_of('.');
        return dot == std::string::npos ? leaf : leaf.substr(0, dot);
    };
    auto replaceStem = [](std::string value, const std::string& oldStem,
                          const std::string& newStem) {
        std::string loweredValue = value;
        std::string loweredStem = oldStem;
        std::transform(loweredValue.begin(), loweredValue.end(), loweredValue.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::transform(loweredStem.begin(), loweredStem.end(), loweredStem.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const size_t at = loweredValue.find(loweredStem);
        if (at != std::string::npos) value.replace(at, oldStem.size(), newStem);
        return value;
    };

    std::vector<char> appendedMetadata;
    std::map<uint32_t, uint32_t> addedPerType;
    for (const auto& spec : specs) {
        const Metadata* donor = spec.native ? nullptr : byName.at(keyFor(spec.donorName));
        // Retail aligns every entry payload to a 2048-byte boundary (796/796 in
        // FinalAlbion.wad, zero exceptions). Pad before placing the payload.
        if (data.size() % kEntryAlignment != 0) {
            data.resize((data.size() / kEntryAlignment + 1) * kEntryAlignment, '\0');
        }
        const uint32_t payloadOffset = static_cast<uint32_t>(data.size());
        if (spec.native) {
            data.insert(data.end(), spec.payload.begin(), spec.payload.end());
        } else {
            data.insert(data.end(), bytes.begin() + donor->offset,
                        bytes.begin() + donor->offset + donor->size);
        }
        const uint32_t id = ++maxId;
        const uint32_t type = spec.native ? 0 : donor->type;
        const uint32_t payloadSize = spec.native
            ? static_cast<uint32_t>(spec.payload.size()) : donor->size;
        appendU32(appendedMetadata, 42);
        appendU32(appendedMetadata, id);
        appendU32(appendedMetadata, type);
        appendU32(appendedMetadata, payloadSize);
        appendU32(appendedMetadata, payloadOffset);
        appendU32(appendedMetadata, spec.native ? 0 : donor->crc);
        const bool nameTerminated = spec.native || donor->nameTerminated;
        appendU32(appendedMetadata,
                  static_cast<uint32_t>(spec.newName.size() +
                                        (nameTerminated ? 1 : 0)));
        appendedMetadata.insert(appendedMetadata.end(),
                                spec.newName.begin(), spec.newName.end());
        if (nameTerminated) appendedMetadata.push_back('\0');
        appendU32(appendedMetadata, spec.native ? 0 : donor->timestamp);
        if (spec.native) {
            appendU32(appendedMetadata, 1);
            appendU32(appendedMetadata, 1);
            appendedMetadata.push_back('\0');
            appendU32(appendedMetadata, 88);
            appendedMetadata.insert(appendedMetadata.end(), 88, '\0');
            ++addedPerType[type];
            continue;
        }
        appendU32(appendedMetadata,
                  static_cast<uint32_t>(donor->dependencies.size()));
        const std::string oldStem = stem(spec.donorName);
        const std::string newStem = stem(spec.newName);
        for (const auto& [dependency, terminated] : donor->dependencies) {
            const std::string rewritten =
                replaceStem(dependency, oldStem, newStem);
            appendU32(appendedMetadata,
                      static_cast<uint32_t>(rewritten.size() + (terminated ? 1 : 0)));
            appendedMetadata.insert(appendedMetadata.end(),
                                    rewritten.begin(), rewritten.end());
            if (terminated) appendedMetadata.push_back('\0');
        }
        appendU32(appendedMetadata, static_cast<uint32_t>(donor->info.size()));
        appendedMetadata.insert(appendedMetadata.end(),
                                donor->info.begin(), donor->info.end());
        ++addedPerType[type];
    }
    footer.insert(footer.begin() + static_cast<ptrdiff_t>(entriesEnd),
                  appendedMetadata.begin(), appendedMetadata.end());
    // The per-type stats block at the head of the footer must agree with the
    // new entry count; retail keeps stats[type].count == entryCount. Leaving it
    // stale makes appended entries invisible to the engine's name lookup, which
    // then yields entry index 0 and a crash in CPackedUIntArray::operator[].
    {
        const uint32_t statsCount = u32(footer, 0, "stats count");
        const uint32_t total = entryCount + static_cast<uint32_t>(specs.size());
        if (statsCount == 1) {
            // Retail's single stats pair tracks the total entry count exactly
            // (FinalAlbion.wad ships (0, 796) alongside entryCount 796).
            std::memcpy(footer.data() + 8, &total, 4);
        } else {
            for (uint32_t i = 0; i < statsCount; ++i) {
                const size_t at = 4 + size_t(i) * 8;
                const uint32_t type = u32(footer, at, "stats type");
                const auto added = addedPerType.find(type);
                if (added == addedPerType.end()) continue;
                const uint32_t updated =
                    u32(footer, at + 4, "stats count") + added->second;
                std::memcpy(footer.data() + at + 4, &updated, 4);
            }
        }
    }

    // Keep the footer 2048-aligned like retail.
    if (data.size() % kEntryAlignment != 0) {
        data.resize((data.size() / kEntryAlignment + 1) * kEntryAlignment, '\0');
    }

    const uint32_t newCount =
        entryCount + static_cast<uint32_t>(specs.size());
    const uint32_t newFooterOffset = static_cast<uint32_t>(data.size());
    // Offsets 20 and 24 are two counts that retail always keeps equal.
    std::memcpy(data.data() + 20, &newCount, 4);
    std::memcpy(data.data() + 24, &newCount, 4);
    std::memcpy(data.data() + 28, &newFooterOffset, 4);
    if (!outPath.parent_path().empty()) fs::create_directories(outPath.parent_path());
    std::ofstream out(outPath, std::ios::binary);
    if (!out) throw std::runtime_error("wad: cannot write " + outPath.string());
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    out.write(footer.data(), static_cast<std::streamsize>(footer.size()));
    if (!out) throw std::runtime_error("wad: failed writing " + outPath.string());
    return specs.size();
}
} // namespace

size_t appendClonedEntries(const fs::path& srcPath,
                           const std::vector<CloneEntry>& clones,
                           const fs::path& outPath) {
    std::vector<AppendSpec> specs;
    specs.reserve(clones.size());
    for (const auto& clone : clones)
        specs.push_back({clone.donorName, clone.newName, {}, false});
    return appendEntriesImpl(srcPath, specs, outPath);
}

size_t appendNativeEntries(const fs::path& srcPath,
                           const std::vector<NativeEntry>& entries,
                           const fs::path& outPath) {
    std::vector<AppendSpec> specs;
    specs.reserve(entries.size());
    for (const auto& entry : entries)
        specs.push_back({{}, entry.name, entry.payload, true});
    return appendEntriesImpl(srcPath, specs, outPath);
}

} // namespace forge::wad
