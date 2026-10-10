#include "forge/textbig.hpp"
#include "forge/unicode.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace forge::textbig {
bool compatibleSpeakerHeader(const big::Bank& baseline, const big::Bank& source,
                             const big::Entry& record, const Entry& value) {
    if (record.type != 0 || record.subHeader.size() != 4 || value.type != 0) return false;
    const auto narrator = [](const big::Bank& bank) -> const big::Entry* {
        const big::Entry* result = nullptr;
        for (const auto& entry : bank.entries) if (entry.type == 2) {
            if (result || entry.name != "[NarratorList]" || entry.length != 0 || !entry.data.empty()) return nullptr;
            result = &entry;
        }
        return result;
    };
    const auto* a = narrator(baseline);
    const auto* b = narrator(source);
    if (!a || !b || a->subHeader != b->subHeader || b->subHeader.size() < 4) return false;
    const auto u32 = [](const std::vector<uint8_t>& bytes, size_t offset) {
        return uint32_t(bytes[offset]) | uint32_t(bytes[offset+1]) << 8 |
               uint32_t(bytes[offset+2]) << 16 | uint32_t(bytes[offset+3]) << 24;
    };
    const auto count = u32(b->subHeader, 0), selected = u32(record.subHeader, 0);
    if (selected >= count || count > b->subHeader.size() - 4) return false;
    size_t cursor = 4;
    for (uint32_t i = 0; i < count; ++i) {
        while (cursor < b->subHeader.size() && b->subHeader[cursor] != 0) ++cursor;
        if (cursor == b->subHeader.size()) return false;
        ++cursor;
    }
    return cursor == b->subHeader.size();
}
namespace {

uint32_t rd32(const std::vector<uint8_t>& b, size_t p) {
    if (p + 4 > b.size()) throw std::runtime_error("textbig: truncated u32");
    return uint32_t(b[p]) | uint32_t(b[p + 1]) << 8 | uint32_t(b[p + 2]) << 16 |
           uint32_t(b[p + 3]) << 24;
}

// Length-prefixed 8-bit string: u32 len, then len bytes (Latin-1/ASCII).
std::string rdLenStr(const std::vector<uint8_t>& b, size_t& p) {
    uint32_t n = rd32(b, p);
    p += 4;
    if (p + n > b.size()) throw std::runtime_error("textbig: truncated string");
    std::string s(reinterpret_cast<const char*>(b.data()+p), n);
    p += n;
    // The stored length may include a trailing NUL; strip it.
    if (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

// UTF-16LE (NUL-NUL terminated) -> UTF-8. Advances p past the terminator.
std::string rdUtf16z(const std::vector<uint8_t>& b, size_t& p) {
    return unicode::readUtf16z(b,p);
}

std::string rdAsciiz(const std::vector<uint8_t>& b, size_t& p) {
    std::string s;
    while (p < b.size() && b[p] != 0) s.push_back(char(b[p++]));
    if (p == b.size()) throw std::runtime_error("textbig: unterminated tag name");
    ++p;  // NUL
    return s;
}

void put32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(uint8_t(value));
    out.push_back(uint8_t(value >> 8));
    out.push_back(uint8_t(value >> 16));
    out.push_back(uint8_t(value >> 24));
}

void putLenStr(std::vector<uint8_t>& out, const std::string& value) {
    if (value.find('\0') != std::string::npos)
        throw std::runtime_error("textbig: embedded NUL in metadata string");
    put32(out, (uint32_t)value.size());
    out.insert(out.end(), value.begin(), value.end());
}

void putUtf16z(std::vector<uint8_t>& out, const std::string& utf8) {
    unicode::appendUtf16z(out,utf8);
}

} // namespace

std::vector<std::string> decodeNarrators(const std::vector<uint8_t>& subHeader) {
    const auto count=rd32(subHeader,0);
    if(count>subHeader.size()-4) throw std::runtime_error("textbig: truncated narrator list");
    size_t cursor=4;
    std::vector<std::string> names;
    for(uint32_t i=0;i<count;++i) {
        std::string name;
        while(cursor<subHeader.size() && subHeader[cursor]!=0) {
            if(subHeader[cursor]>=128) throw std::runtime_error("textbig: narrator name is not ASCII");
            name.push_back(char(subHeader[cursor++]));
        }
        if(cursor==subHeader.size()) throw std::runtime_error("textbig: unterminated narrator name");
        ++cursor;names.push_back(std::move(name));
    }
    if(cursor!=subHeader.size()) throw std::runtime_error("textbig: trailing narrator bytes");
    return names;
}

Entry decode(const std::vector<uint8_t>& payload, int type) {
    Entry e;
    e.type = type;
    size_t p = 0;

    if (type == 1) {  // group
        uint32_t count = rd32(payload, p);
        p += 4;
        for (uint32_t i = 0; i < count; ++i) {
            e.groupMembers.push_back(rd32(payload, p));
            p += 4;
        }
        return e;
    }
    if (type != 0) return e;  // narrator (type 2) handled elsewhere

    e.content = rdUtf16z(payload, p);
    e.speechBank = rdLenStr(payload, p);
    e.speaker = rdLenStr(payload, p);
    e.identifier = rdLenStr(payload, p);
    uint32_t tagCount = rd32(payload, p);
    p += 4;
    for (uint32_t i = 0; i < tagCount; ++i) {
        Tag t;
        t.position = (int32_t)rd32(payload, p);
        p += 4;
        t.name = rdAsciiz(payload, p);
        e.tags.push_back(std::move(t));
    }
    return e;
}

std::vector<uint8_t> encode(const Entry& entry) {
    std::vector<uint8_t> out;
    if (entry.type == 1) {
        put32(out, (uint32_t)entry.groupMembers.size());
        for (uint32_t id : entry.groupMembers) put32(out, id);
        return out;
    }
    if (entry.type != 0)
        throw std::runtime_error("textbig: only type 0 strings and type 1 groups can be encoded");

    putUtf16z(out, entry.content);
    putLenStr(out, entry.speechBank);
    putLenStr(out, entry.speaker);
    putLenStr(out, entry.identifier);
    put32(out, (uint32_t)entry.tags.size());
    for (const auto& tag : entry.tags) {
        if (tag.name.find('\0') != std::string::npos)
            throw std::runtime_error("textbig: embedded NUL in tag name");
        put32(out, (uint32_t)tag.position);
        out.insert(out.end(), tag.name.begin(), tag.name.end());
        out.push_back(0);
    }
    return out;
}

UpsertResult upsertString(big::File& file, const std::string& name, Entry value,
                          std::optional<uint32_t> requestedId,
                          const std::string& donorName) {
    if (name.empty() || name.find('\0') != std::string::npos)
        throw std::runtime_error("textbig: entry name must be nonempty and NUL-free");
    value.type = 0;
    if (value.identifier.empty()) value.identifier = name;
    const auto payload = encode(value);

    big::Bank* existingBank = nullptr;
    big::Entry* existing = nullptr;
    big::Bank* donorBank = nullptr;
    const big::Entry* donor = nullptr;
    size_t donorMatches = 0;
    uint32_t maxId = 0;

    for (auto& bank : file.banks()) {
        for (auto& candidate : bank.entries) {
            maxId = std::max(maxId, candidate.id);
            if (candidate.name == name) {
                if (existing != nullptr)
                    throw std::runtime_error("textbig: duplicate existing entry name " + name);
                existingBank = &bank;
                existing = &candidate;
            }
            if (!donorName.empty() && candidate.name == donorName) {
                ++donorMatches;
                donorBank = &bank;
                donor = &candidate;
            } else if (donorName.empty() && donor == nullptr && candidate.type == 0) {
                donorBank = &bank;
                donor = &candidate;
            }
        }
    }

    auto idUsedByOther = [&](uint32_t id, const big::Entry* self) {
        for (const auto& bank : file.banks())
            for (const auto& candidate : bank.entries)
                if (&candidate != self && candidate.id == id) return true;
        return false;
    };

    if (existing != nullptr) {
        if (existing->type != 0)
            throw std::runtime_error("textbig: existing entry is not a type-0 string");
        const uint32_t id = requestedId.value_or(existing->id);
        if (id == 0 || idUsedByOther(id, existing))
            throw std::runtime_error("textbig: requested ID is zero or already in use");
        existing->id = id;
        existing->data = payload;
        existing->length = (uint32_t)payload.size();
        existing->dataOffset = 0;
        return {id, name, existingBank->name, false};
    }

    if (donorMatches > 1)
        throw std::runtime_error("textbig: ambiguous donor entry name " + donorName);
    if (donor == nullptr || donorBank == nullptr)
        throw std::runtime_error(
            donorName.empty() ? "textbig: archive contains no type-0 donor"
                              : "textbig: donor entry not found: " + donorName);
    if (donor->type != 0)
        throw std::runtime_error("textbig: donor entry is not a type-0 string");

    if (!requestedId && maxId == std::numeric_limits<uint32_t>::max())
        throw std::runtime_error("textbig: no free entry ID");
    const uint32_t id = requestedId ? *requestedId : maxId + 1;
    if (id == 0 || idUsedByOther(id, nullptr))
        throw std::runtime_error("textbig: requested ID is zero or already in use");

    big::Entry added = *donor; // preserve CRC/dependency/subheader metadata
    if (file.magic() == "BIGB") added.magic = 42;
    added.name = name;
    added.id = id;
    added.type = 0;
    added.dataOffset = 0;
    added.length = (uint32_t)payload.size();
    added.data = payload;
    donorBank->entries.push_back(std::move(added));
    return {id, name, donorBank->name, true};
}

} // namespace forge::textbig
