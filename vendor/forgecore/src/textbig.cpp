#include "forge/textbig.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace forge::textbig {
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
    std::string s(reinterpret_cast<const char*>(&b[p]), n);
    p += n;
    // The stored length may include a trailing NUL; strip it.
    if (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

// UTF-16LE (NUL-NUL terminated) -> UTF-8. Advances p past the terminator.
std::string rdUtf16z(const std::vector<uint8_t>& b, size_t& p) {
    std::string out;
    while (p + 1 < b.size()) {
        uint32_t c = uint32_t(b[p]) | uint32_t(b[p + 1]) << 8;
        p += 2;
        if (c == 0) break;
        uint32_t cp = c;
        if (c >= 0xD800 && c <= 0xDBFF) {
            if (p + 1 >= b.size())
                throw std::runtime_error("textbig: truncated UTF-16 surrogate");
            const uint32_t low = uint32_t(b[p]) | uint32_t(b[p + 1]) << 8;
            if (low < 0xDC00 || low > 0xDFFF)
                throw std::runtime_error("textbig: invalid UTF-16 surrogate");
            p += 2;
            cp = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
        } else if (c >= 0xDC00 && c <= 0xDFFF) {
            throw std::runtime_error("textbig: unexpected UTF-16 low surrogate");
        }
        if (cp < 0x80) {
            out.push_back(char(cp));
        } else if (cp < 0x800) {
            out.push_back(char(0xC0 | (cp >> 6)));
            out.push_back(char(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(char(0xE0 | (cp >> 12)));
            out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(char(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(char(0xF0 | (cp >> 18)));
            out.push_back(char(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(char(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

std::string rdAsciiz(const std::vector<uint8_t>& b, size_t& p) {
    std::string s;
    while (p < b.size() && b[p] != 0) s.push_back(char(b[p++]));
    if (p < b.size()) ++p;  // NUL
    return s;
}

void put32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(uint8_t(value));
    out.push_back(uint8_t(value >> 8));
    out.push_back(uint8_t(value >> 16));
    out.push_back(uint8_t(value >> 24));
}

void putLenStr(std::vector<uint8_t>& out, const std::string& value) {
    put32(out, (uint32_t)value.size());
    out.insert(out.end(), value.begin(), value.end());
}

void putUtf16z(std::vector<uint8_t>& out, const std::string& utf8) {
    size_t p = 0;
    while (p < utf8.size()) {
        const uint8_t lead = (uint8_t)utf8[p++];
        uint32_t cp = 0;
        unsigned continuation = 0;
        if (lead < 0x80) {
            cp = lead;
        } else if ((lead & 0xE0) == 0xC0) {
            cp = lead & 0x1F;
            continuation = 1;
        } else if ((lead & 0xF0) == 0xE0) {
            cp = lead & 0x0F;
            continuation = 2;
        } else if ((lead & 0xF8) == 0xF0) {
            cp = lead & 0x07;
            continuation = 3;
        } else {
            throw std::runtime_error("textbig: invalid UTF-8 lead byte");
        }
        for (unsigned i = 0; i < continuation; ++i) {
            if (p >= utf8.size() || ((uint8_t)utf8[p] & 0xC0) != 0x80)
                throw std::runtime_error("textbig: invalid UTF-8 continuation");
            cp = (cp << 6) | ((uint8_t)utf8[p++] & 0x3F);
        }
        if ((continuation == 1 && cp < 0x80) ||
            (continuation == 2 && cp < 0x800) ||
            (continuation == 3 && cp < 0x10000) ||
            cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            throw std::runtime_error("textbig: invalid UTF-8 code point");

        auto put16 = [&](uint16_t v) {
            out.push_back(uint8_t(v));
            out.push_back(uint8_t(v >> 8));
        };
        if (cp <= 0xFFFF) {
            put16((uint16_t)cp);
        } else {
            cp -= 0x10000;
            put16((uint16_t)(0xD800 + (cp >> 10)));
            put16((uint16_t)(0xDC00 + (cp & 0x3FF)));
        }
    }
    out.push_back(0);
    out.push_back(0);
}

} // namespace

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
        put32(out, (uint32_t)tag.position);
        out.insert(out.end(), tag.name.begin(), tag.name.end());
        out.push_back(0);
    }
    return out;
}

UpsertResult upsertString(big::File& file, const std::string& name, Entry value,
                          std::optional<uint32_t> requestedId,
                          const std::string& donorName) {
    if (name.empty()) throw std::runtime_error("textbig: entry name must not be empty");
    value.type = 0;
    if (value.identifier.empty()) value.identifier = name;
    const auto payload = encode(value);

    big::Bank* existingBank = nullptr;
    big::Entry* existing = nullptr;
    big::Bank* donorBank = nullptr;
    const big::Entry* donor = nullptr;
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

    if (donor == nullptr || donorBank == nullptr)
        throw std::runtime_error(
            donorName.empty() ? "textbig: archive contains no type-0 donor"
                              : "textbig: donor entry not found: " + donorName);
    if (donor->type != 0)
        throw std::runtime_error("textbig: donor entry is not a type-0 string");

    if (maxId == std::numeric_limits<uint32_t>::max())
        throw std::runtime_error("textbig: no free entry ID");
    const uint32_t id = requestedId.value_or(maxId + 1);
    if (id == 0 || idUsedByOther(id, nullptr))
        throw std::runtime_error("textbig: requested ID is zero or already in use");

    big::Entry added = *donor; // preserve retail magic/CRC/dependency/subheader metadata
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
