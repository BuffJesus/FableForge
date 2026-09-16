#include "forge/big.hpp"

#include <cstring>
#include <fstream>
#include <map>
#include <stdexcept>

namespace forge::big {
namespace {

struct Cursor {
    const std::vector<uint8_t>& d;
    size_t p = 0;

    void need(size_t n) const {
        if (p + n > d.size()) throw std::runtime_error("big: unexpected end of file");
    }
    uint32_t u32() {
        need(4);
        uint32_t v = uint32_t(d[p]) | uint32_t(d[p + 1]) << 8 |
                     uint32_t(d[p + 2]) << 16 | uint32_t(d[p + 3]) << 24;
        p += 4;
        return v;
    }
    std::string str(size_t n) {
        need(n);
        std::string s(reinterpret_cast<const char*>(&d[p]), n);
        p += n;
        return s;
    }
    // Bank names terminate on NUL or 0xFF (per the reference reader).
    std::string asciiz() {
        std::string s;
        while (p < d.size() && d[p] != 0 && d[p] != 0xFF) s.push_back((char)d[p++]);
        if (p < d.size()) ++p; // consume terminator
        return s;
    }
};

// The subHeader is an ASCIIZ def-type string for BIN banks, but raw binary for
// asset banks (textures/meshes). Only return it when it is clean printable text.
std::string asciizFrom(const std::vector<uint8_t>& b) {
    size_t n = 0;
    while (n < b.size() && b[n] != 0) {
        if (b[n] < 0x20 || b[n] > 0x7E) return {};  // binary subheader
        ++n;
    }
    return std::string(reinterpret_cast<const char*>(b.data()), n);
}

} // namespace

File File::open(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("big: cannot open " + path.string());
    File out;
    out.raw_.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    const auto& d = out.raw_;
    if (d.size() < 16) throw std::runtime_error("big: file too small");

    // magic: "BIGB" or "B\0\0\0"
    if (!(d[0] == 'B' && ((d[1] == 'I' && d[2] == 'G' && d[3] == 'B') ||
                          (d[1] == 0 && d[2] == 0 && d[3] == 0))))
        throw std::runtime_error("big: bad magic (expected BIGB or B\\0\\0\\0)");
    out.magic_.assign(reinterpret_cast<const char*>(d.data()), 4);

    Cursor c{d, 4};
    out.version_ = c.u32();
    uint32_t bankDirOffset = c.u32();
    out.contentType_ = c.u32();

    Cursor dir{d, bankDirOffset};
    uint32_t bankCount = dir.u32();
    for (uint32_t i = 0; i < bankCount; ++i) {
        Bank bank;
        bank.name = dir.asciiz();
        bank.id = dir.u32();
        uint32_t entryCount = dir.u32();
        uint32_t entryStart = dir.u32();
        dir.u32();                       // bank length (unused for reading)
        bank.blockSize = dir.u32();

        if (entryCount > 0) {
            Cursor e{d, entryStart};
            uint32_t typeCount = e.u32();
            e.p += (size_t)typeCount * 8; // skip the type-count table
            for (uint32_t k = 0; k < entryCount; ++k) {
                Entry en;
                en.magic = e.u32();
                en.id = e.u32();
                en.type = e.u32();
                en.length = e.u32();
                en.dataOffset = e.u32();
                en.devFileType = e.u32();
                uint32_t nameLen = e.u32();
                en.name = e.str(nameLen);
                en.devCrc = e.u32();
                uint32_t devSourceCount = e.u32();
                for (uint32_t s = 0; s < devSourceCount; ++s) {
                    uint32_t sl = e.u32();
                    en.devSources.push_back(e.str(sl));
                }
                uint32_t subLen = e.u32();
                en.subHeader.assign(d.begin() + e.p, d.begin() + e.p + subLen);
                e.p += subLen;
                en.definition = asciizFrom(en.subHeader);
                bank.entries.push_back(std::move(en));
            }
        }
        out.banks_.push_back(std::move(bank));
    }
    return out;
}

const Bank* File::findBank(const std::string& name) const {
    for (const auto& b : banks_)
        if (b.name == name) return &b;
    return nullptr;
}

Bank* File::findBank(const std::string& name) {
    for (auto& b : banks_)
        if (b.name == name) return &b;
    return nullptr;
}

std::vector<uint8_t> File::entryData(const Entry& e) const {
    if (!e.data.empty() || e.length == 0) return e.data;
    if ((size_t)e.dataOffset + e.length > raw_.size())
        throw std::runtime_error("big: entry data out of range");
    return std::vector<uint8_t>(raw_.begin() + e.dataOffset,
                                raw_.begin() + e.dataOffset + e.length);
}

Bank& File::addBank(const std::string& name, uint32_t id) {
    Bank b;
    b.name = name;
    b.id = id;
    b.blockSize = 1;
    banks_.push_back(std::move(b));
    return banks_.back();
}

namespace {
void put32(std::vector<uint8_t>& o, uint32_t v) {
    o.push_back(uint8_t(v));
    o.push_back(uint8_t(v >> 8));
    o.push_back(uint8_t(v >> 16));
    o.push_back(uint8_t(v >> 24));
}
void patch32(std::vector<uint8_t>& o, size_t at, uint32_t v) {
    o[at] = uint8_t(v); o[at + 1] = uint8_t(v >> 8);
    o[at + 2] = uint8_t(v >> 16); o[at + 3] = uint8_t(v >> 24);
}
} // namespace

std::vector<uint8_t> File::serialize() const {
    std::vector<uint8_t> o;
    // Preserve the source archive's header flavor. Retail text.big uses BIGB;
    // community FMP packages commonly use B\0\0\0. Silently converting the
    // former was tolerated by our reader but is not an EgoCore-compatible
    // write and needlessly risks the retail loader.
    const std::string headerMagic =
        magic_.size() == 4 ? magic_ : std::string("B\0\0\0", 4);
    o.insert(o.end(), headerMagic.begin(), headerMagic.end());
    put32(o, version_);
    const size_t bankDirOffPos = o.size();
    put32(o, 0);                 // bankDirOffset placeholder
    put32(o, contentType_);

    // Resolve each entry's payload (data field, else slice from backing file).
    auto payloadOf = [&](const Entry& e) -> std::vector<uint8_t> {
        if (!e.data.empty() || e.length == 0) return e.data;
        return entryData(e);
    };

    // Body: write all payloads, remembering each entry's data offset.
    std::vector<std::vector<uint32_t>> dataOffsets(banks_.size());
    std::vector<std::vector<uint32_t>> dataLengths(banks_.size());
    for (size_t bi = 0; bi < banks_.size(); ++bi) {
        for (const auto& e : banks_[bi].entries) {
            auto pl = payloadOf(e);
            dataOffsets[bi].push_back((uint32_t)o.size());
            dataLengths[bi].push_back((uint32_t)pl.size());
            o.insert(o.end(), pl.begin(), pl.end());
        }
    }

    // Per-bank entry tables (record their start offsets for the directory).
    std::vector<uint32_t> entryStart(banks_.size());
    std::vector<uint32_t> tableLen(banks_.size());
    for (size_t bi = 0; bi < banks_.size(); ++bi) {
        entryStart[bi] = (uint32_t)o.size();
        // EgoCore and retail emit a type histogram before the entry records.
        // The reader does not require it, but preserving it makes rebuilt
        // text.big archives structurally faithful instead of merely readable.
        std::map<uint32_t, uint32_t> typeCounts;
        for (const auto& e : banks_[bi].entries) ++typeCounts[e.type];
        put32(o, (uint32_t)typeCounts.size());
        for (const auto& [type, count] : typeCounts) {
            put32(o, type);
            put32(o, count);
        }
        for (size_t ei = 0; ei < banks_[bi].entries.size(); ++ei) {
            const auto& e = banks_[bi].entries[ei];
            put32(o, e.magic);
            put32(o, e.id);
            put32(o, e.type);
            put32(o, dataLengths[bi][ei]);
            put32(o, dataOffsets[bi][ei]);
            put32(o, e.devFileType);
            put32(o, (uint32_t)e.name.size());
            o.insert(o.end(), e.name.begin(), e.name.end());
            put32(o, e.devCrc);
            put32(o, (uint32_t)e.devSources.size());
            for (const auto& s : e.devSources) {
                put32(o, (uint32_t)s.size());
                o.insert(o.end(), s.begin(), s.end());
            }
            put32(o, (uint32_t)e.subHeader.size());
            o.insert(o.end(), e.subHeader.begin(), e.subHeader.end());
        }
        tableLen[bi] = (uint32_t)o.size() - entryStart[bi];
    }

    // Bank directory.
    const uint32_t bankDirOffset = (uint32_t)o.size();
    put32(o, (uint32_t)banks_.size());
    for (size_t bi = 0; bi < banks_.size(); ++bi) {
        const auto& b = banks_[bi];
        o.insert(o.end(), b.name.begin(), b.name.end());
        o.push_back(0);          // NUL terminator
        put32(o, b.id);
        put32(o, (uint32_t)b.entries.size());
        put32(o, entryStart[bi]);
        put32(o, tableLen[bi]);
        put32(o, b.blockSize ? b.blockSize : 1);
    }

    patch32(o, bankDirOffPos, bankDirOffset);
    return o;
}

} // namespace forge::big
