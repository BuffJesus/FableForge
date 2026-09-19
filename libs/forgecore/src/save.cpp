#include "forge/save.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>

#include "forge/defdecode.hpp"  // fieldTag (seed-0 reflected CRC-32)
#include "miniz/miniz.h"

namespace forge::save {
namespace {

// The validated HEADER schema (docs/SAVEGAME_FORMAT.md / decode_header.py), in
// Transfer() call order. Types: S=asciiz, W=wide(UTF-16z), L=int32, F=float,
// B=bool, R=CFloatRange(3 floats).
struct SchemaField { const char* name; char kind; };
const SchemaField kHeader[] = {
    {"WorldName", 'S'}, {"WorldFrame", 'L'}, {"TeleportingEnabled", 'B'},
    {"SavingEnabled", 'B'}, {"ExperienceSpendingEnabled", 'B'},
    {"CreatureGenerationEnabled", 'B'}, {"CreatureGenerationDisabledGroups", 'L'},
    {"HeroSleepingEnabled", 'B'}, {"MapTableShowQuestCardsOnUsed", 'B'},
    {"MiniMapEnabled", 'B'}, {"MiniMapActiveBeforeDisabled", 'B'},
    {"GuildMasterMessagesEnabled", 'B'}, {"SummonerDeathExplosionAffectsHero", 'B'},
    {"MostRecentSaveType", 'L'}, {"MostRecentSaveTypeBeforeManualSave", 'L'},
    {"MostRecentManualSaveName", 'W'}, {"SaveGameMarkerPos", 'R'},
    {"SaveGameMarkerAngleXY", 'F'}, {"GuildSealRecallPos", 'R'},
    {"GuildSealRecallAngleXY", 'F'}, {"CurrentRegionName", 'S'},
    {"CurrentRegionMinimapGraphicName", 'S'}, {"TotalTimePlayed", 'F'},
};

uint32_t rd32(const std::vector<uint8_t>& b, size_t p) {
    return uint32_t(b[p]) | uint32_t(b[p + 1]) << 8 | uint32_t(b[p + 2]) << 16 |
           uint32_t(b[p + 3]) << 24;
}

std::vector<uint8_t> inflate(const uint8_t* src, size_t clen, size_t ulen) {
    std::vector<uint8_t> out(ulen);
    mz_ulong dstLen = (mz_ulong)ulen;
    if (mz_uncompress(out.data(), &dstLen, src, (mz_ulong)clen) != MZ_OK)
        throw std::runtime_error("save: zlib inflate failed");
    out.resize(dstLen);
    return out;
}

// Streaming inflate of a zlib stream of unknown output size (nested cells).
// Returns {} on failure; sets `consumed` to the input bytes used on success.
std::vector<uint8_t> inflateStream(const uint8_t* src, size_t srcLen,
                                   size_t& consumed) {
    mz_stream s{};
    if (mz_inflateInit(&s) != MZ_OK) return {};
    s.next_in = src;
    s.avail_in = (unsigned)std::min<size_t>(srcLen, 0xFFFFFFFF);
    std::vector<uint8_t> out;
    std::vector<uint8_t> buf(1 << 15);
    int rc;
    do {
        s.next_out = buf.data();
        s.avail_out = (unsigned)buf.size();
        rc = mz_inflate(&s, MZ_NO_FLUSH);
        out.insert(out.end(), buf.data(), buf.data() + (buf.size() - s.avail_out));
    } while (rc == MZ_OK);
    consumed = (size_t)s.total_in;
    mz_inflateEnd(&s);
    if (rc != MZ_STREAM_END) return {};
    return out;
}

// Find `needle` in `hay`; return index or npos.
size_t find(const std::vector<uint8_t>& hay, const char* needle, size_t nlen,
            size_t from = 0) {
    if (nlen == 0 || hay.size() < nlen) return SIZE_MAX;
    for (size_t i = from; i + nlen <= hay.size(); ++i)
        if (std::memcmp(&hay[i], needle, nlen) == 0) return i;
    return SIZE_MAX;
}

// Decode the CTCHeroStats fields from the chunk1 entity graph (one zlib layer
// deeper: SAVED_ENTITIES -> nested cells -> the cell with CTCHeroStats).
std::vector<Field> decodeHeroStats(const std::vector<uint8_t>& chunk1) {
    struct Stat { const char* name; char kind; };  // i=int32, f=float
    static const Stat kStats[] = {
        {"Money", 'i'}, {"Morality", 'i'}, {"Age", 'f'}, {"Fatness", 'f'},
        {"RenownLevel", 'i'},
    };
    std::vector<Field> out;
    size_t se = find(chunk1, "SAVED_ENTITIES", 14);
    if (se == SIZE_MAX) return out;

    // Walk nested zlib cells from SAVED_ENTITIES; find the one with CTCHeroStats.
    size_t i = se;
    while (i + 2 < chunk1.size()) {
        if (chunk1[i] == 0x78 &&
            (chunk1[i + 1] == 0x9c || chunk1[i + 1] == 0xda || chunk1[i + 1] == 0x01)) {
            size_t consumed = 0;
            auto cell = inflateStream(&chunk1[i], chunk1.size() - i, consumed);
            if (cell.size() > 32 && consumed > 0) {
                size_t h = find(cell, "CTCHeroStats", 13);  // includes NUL
                if (h != SIZE_MAX) {
                    const size_t winEnd = std::min(cell.size(), h + 0x140);
                    for (const auto& st : kStats) {
                        uint32_t tag = forge::defdecode::fieldTag(st.name);
                        uint8_t want[4] = {uint8_t(tag), uint8_t(tag >> 8),
                                           uint8_t(tag >> 16), uint8_t(tag >> 24)};
                        for (size_t p = h; p + 8 <= winEnd; ++p) {
                            if (std::memcmp(&cell[p], want, 4) != 0) continue;
                            Field f;
                            f.name = st.name;
                            char buf[32];
                            if (st.kind == 'i') {
                                int32_t v;
                                std::memcpy(&v, &cell[p + 4], 4);
                                std::snprintf(buf, sizeof(buf), "%d", v);
                                f.type = "int32";
                            } else {
                                float v;
                                std::memcpy(&v, &cell[p + 4], 4);
                                std::snprintf(buf, sizeof(buf), "%g", v);
                                f.type = "float";
                            }
                            f.value = buf;
                            f.tagOk = true;
                            out.push_back(std::move(f));
                            break;
                        }
                    }
                    return out;
                }
                i += consumed;
                continue;
            }
        }
        ++i;
    }
    return out;
}

const char* typeName(char k) {
    switch (k) {
        case 'S': return "string";
        case 'W': return "wstring";
        case 'L': return "int32";
        case 'F': return "float";
        case 'B': return "bool";
        case 'R': return "range3";
    }
    return "?";
}

} // namespace

File File::read(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("save: cannot open " + path.string());
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
    if (d.size() < 0x20 || std::memcmp(d.data(), "FableSave!", 10) != 0)
        throw std::runtime_error("save: not a FableSave! file");

    File out;
    out.fileSize = d.size();
    out.signature = rd32(d, 0x0C);
    out.chunk0Ulen = rd32(d, 0x10);
    out.chunk1Ulen = rd32(d, 0x14);
    const uint32_t c0Clen = rd32(d, 0x18);
    if (0x1C + (size_t)c0Clen > d.size())
        throw std::runtime_error("save: chunk0 out of range");

    const auto hdr = inflate(&d[0x1C], c0Clen, out.chunk0Ulen);

    // Section: [name\0][u32 sectionLen][fields...]
    size_t p = 0;
    while (p < hdr.size() && hdr[p] != 0) ++p;
    out.sectionName.assign(hdr.begin(), hdr.begin() + p);
    ++p;                 // NUL
    p += 4;              // u32 section length

    out.allTagsOk = true;
    for (const auto& sf : kHeader) {
        if (p + 4 > hdr.size()) { out.allTagsOk = false; break; }
        Field fld;
        fld.name = sf.name;
        fld.type = typeName(sf.kind);
        const uint32_t tag = rd32(hdr, p);
        fld.tagOk = (tag == forge::defdecode::fieldTag(sf.name));
        if (!fld.tagOk) out.allTagsOk = false;
        p += 4;

        char buf[64];
        switch (sf.kind) {
            case 'S': {
                size_t e = p;
                while (e < hdr.size() && hdr[e] != 0) ++e;
                fld.value.assign(hdr.begin() + p, hdr.begin() + e);
                p = e + 1;
                break;
            }
            case 'W': {
                std::string s;
                while (p + 1 < hdr.size()) {
                    uint16_t w = hdr[p] | (hdr[p + 1] << 8);
                    p += 2;
                    if (w == 0) break;
                    s.push_back(w >= 32 && w < 127 ? char(w) : '?');
                }
                fld.value = s;
                break;
            }
            case 'L': {
                std::snprintf(buf, sizeof(buf), "%u", rd32(hdr, p));
                fld.value = buf; p += 4; break;
            }
            case 'F': {
                float v; std::memcpy(&v, &hdr[p], 4);
                std::snprintf(buf, sizeof(buf), "%g", v);
                fld.value = buf; p += 4; break;
            }
            case 'B': {
                fld.value = hdr[p] ? "true" : "false"; p += 1; break;
            }
            case 'R': {
                float x[3]; std::memcpy(x, &hdr[p], 12);
                std::snprintf(buf, sizeof(buf), "(%g, %g, %g)", x[0], x[1], x[2]);
                fld.value = buf; p += 12; break;
            }
        }
        out.header.push_back(std::move(fld));
    }

    // Hero stats live in chunk1 (the ENTITIES payload): inflate it, then walk the
    // nested SAVED_ENTITIES cells to CTCHeroStats.
    const uint32_t c1Clen = rd32(d, 0x1C + c0Clen);
    const size_t c1Off = 0x1C + (size_t)c0Clen + 4;
    if (out.chunk1Ulen > 0 && c1Off + c1Clen <= d.size()) {
        try {
            const auto chunk1 = inflate(&d[c1Off], c1Clen, out.chunk1Ulen);
            out.heroStats = decodeHeroStats(chunk1);
        } catch (const std::exception&) {
            // chunk1 decode is best-effort; HEADER is the guaranteed part.
        }
    }
    return out;
}

} // namespace forge::save
