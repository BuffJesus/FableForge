#include "forge/defdecode.hpp"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace forge::defdecode {
namespace {

std::array<uint32_t, 256> makeTable() {
    std::array<uint32_t, 256> t{};
    for (uint32_t n = 0; n < 256; ++n) {
        uint32_t c = n;
        for (int k = 0; k < 8; ++k)
            c = (c & 1) ? (c >> 1) ^ 0xEDB88320u : c >> 1;
        t[n] = c;
    }
    return t;
}
const std::array<uint32_t, 256>& table() {
    static const std::array<uint32_t, 256> t = makeTable();
    return t;
}

// Little-endian u32 read (bounds-checked by caller).
uint32_t rd32(const std::vector<uint8_t>& b, size_t p) {
    return uint32_t(b[p]) | uint32_t(b[p + 1]) << 8 | uint32_t(b[p + 2]) << 16 |
           uint32_t(b[p + 3]) << 24;
}

// Find the little-endian tag `t` in `b` at or after `from`; npos if absent.
size_t findTag(const std::vector<uint8_t>& b, uint32_t t, size_t from) {
    if (b.size() < 4) return std::string::npos;
    uint8_t want[4] = {uint8_t(t), uint8_t(t >> 8), uint8_t(t >> 16),
                       uint8_t(t >> 24)};
    for (size_t i = from; i + 4 <= b.size(); ++i) {
        if (std::memcmp(&b[i], want, 4) == 0) return i;
    }
    return std::string::npos;
}

bool isEnum(const std::string& t) {
    if (t.rfind("enum", 0) == 0) return true;
    if (t.rfind("W4", 0) == 0) return true;
    return t.size() >= 2 && t[0] == 'E' && t[1] >= 'A' && t[1] <= 'Z';
}

} // namespace

uint32_t fieldTag(std::string_view name) {
    const auto& t = table();
    uint32_t c = 0; // seed 0, no final inversion
    for (unsigned char b : name) c = (c >> 8) ^ t[(c ^ b) & 0xFF];
    return c;
}

Decoded decode(const std::vector<uint8_t>& payload,
               const defschema::DefType& def) {
    Decoded out;

    // Prefix ends at the first named field's tag; when the schema declares an
    // explicit prefix_len (types whose Transfer writes zero fields), use that;
    // otherwise default to 3 if no tag anchors it.
    size_t pos = def.prefixLen >= 0 ? size_t(def.prefixLen) : 3;
    for (const auto& f : def.fields) {
        if (!f.name.empty()) {
            size_t at = findTag(payload, fieldTag(f.name), 0);
            if (at != std::string::npos) pos = at;
            break;
        }
    }
    out.prefixLen = pos;
    out.prefix.assign(payload.begin(),
                      payload.begin() + std::min(pos, payload.size()));
    out.allTagsOk = payload.size() >= pos; // truncated prefix is not clean

    for (size_t fi = 0; fi < def.fields.size(); ++fi) {
        const auto& f = def.fields[fi];
        DecodedField df;
        df.name = f.name;
        df.type = f.type;

        // Resync: for named fields, jump to the field's tag if it is not at pos.
        if (!f.name.empty()) {
            uint32_t want = fieldTag(f.name);
            if (pos + 4 > payload.size() || rd32(payload, pos) != want) {
                size_t at = findTag(payload, want, pos);
                if (at != std::string::npos) pos = at;
            }
        }
        if (pos + 4 > payload.size()) {
            out.allTagsOk = false;
            break; // truncated
        }
        df.tag = rd32(payload, pos);
        df.tagOk = !f.name.empty() && df.tag == fieldTag(f.name);
        if (!df.tagOk && !f.name.empty()) out.allTagsOk = false;
        pos += 4;

        // Value = bytes up to the next named field's tag (or end of buffer).
        size_t valueEnd = payload.size();
        for (size_t nj = fi + 1; nj < def.fields.size(); ++nj) {
            if (def.fields[nj].name.empty()) continue;
            size_t at = findTag(payload, fieldTag(def.fields[nj].name), pos);
            if (at != std::string::npos) valueEnd = at;
            break;
        }
        df.value.assign(payload.begin() + pos, payload.begin() + valueEnd);
        out.fields.push_back(std::move(df));
        pos = valueEnd;
    }

    out.leftover = payload.size() > pos ? payload.size() - pos : 0;
    return out;
}

std::vector<uint8_t> encode(const Decoded& d) {
    std::vector<uint8_t> out = d.prefix;
    for (const auto& f : d.fields) {
        out.push_back(uint8_t(f.tag));
        out.push_back(uint8_t(f.tag >> 8));
        out.push_back(uint8_t(f.tag >> 16));
        out.push_back(uint8_t(f.tag >> 24));
        out.insert(out.end(), f.value.begin(), f.value.end());
    }
    return out;
}

const defschema::DefType* resolveType(const defschema::Schema& schema,
                                      const std::string& binDefinition,
                                      const std::vector<uint8_t>& samplePayload) {
    // Candidate 1: the literal definition string (class-named types).
    // Candidate 2: "C" + CamelCase(CATEGORY_NAME) + "Def" (category-named types).
    std::string camel = "C";
    bool up = true;
    for (char ch : binDefinition) {
        if (ch == '_') { up = true; continue; }
        camel += up ? char(std::toupper((unsigned char)ch))
                    : char(std::tolower((unsigned char)ch));
        up = false;
    }
    camel += "Def";

    for (const std::string& cand : {binDefinition, camel}) {
        const auto* def = schema.find(cand);
        if (def == nullptr) continue;
        // Confirm: the sample must decode cleanly under this schema.
        if (decode(samplePayload, *def).clean()) return def;
    }
    return nullptr;
}

Decoded decode(const bin::Entry& entry, const defschema::Schema& schema) {
    const auto* def = resolveType(schema, entry.definition, entry.data);
    if (def == nullptr)
        throw std::runtime_error("no schema for def type: " + entry.definition);
    return decode(entry.data, *def);
}

FieldMerge mergeFields(
    const Decoded& base,
    const std::vector<std::pair<std::string, Decoded>>& versions,
    const std::string& pick) {
    FieldMerge r;
    if (!base.clean()) return r;
    for (const auto& [name, d] : versions) {
        if (!d.clean() || d.fields.size() != base.fields.size()) return r;
        for (size_t i = 0; i < d.fields.size(); ++i) {
            if (d.fields[i].tag != base.fields[i].tag) return r; // layout mismatch
        }
    }

    const bool keepBase = pick == "vanilla" || pick == "base" ||
                          pick == "none" || pick == "-";

    Decoded merged = base;  // tags + shape from base; values overwritten below

    // Merge one part given its base value and each version's value for it.
    // Applies the result into `dst`; records auto-merge / conflict in `r`.
    auto mergePart = [&](const std::string& partName,
                         const std::vector<uint8_t>& baseVal,
                         const std::vector<std::vector<uint8_t>>& modVals,
                         std::vector<uint8_t>& dst) {
        std::vector<std::string> changers;   // mods that differ from base
        std::vector<const std::vector<uint8_t>*> changerVals;
        for (size_t m = 0; m < versions.size(); ++m) {
            if (modVals[m] != baseVal) {
                changers.push_back(versions[m].first);
                changerVals.push_back(&modVals[m]);
            }
        }
        if (changers.empty()) { dst = baseVal; return; }

        bool allSame = true;
        for (size_t k = 1; k < changerVals.size(); ++k) {
            if (*changerVals[k] != *changerVals[0]) { allSame = false; break; }
        }
        if (changers.size() == 1 || allSame) {
            dst = *changerVals[0];
            r.autoMerged.push_back(partName);
            return;
        }

        // True conflict: pick override, else load order (last changer wins).
        FieldMerge::Conflict c;
        c.part = partName;
        c.mods = changers;
        if (!pick.empty()) {
            if (keepBase) { dst = baseVal; c.winner = "vanilla"; }
            else {
                dst = *changerVals.back();
                c.winner = changers.back();
                for (size_t m = 0; m < versions.size(); ++m) {
                    if (versions[m].first == pick && modVals[m] != baseVal) {
                        dst = modVals[m]; c.winner = pick; break;
                    }
                }
            }
        } else {
            dst = *changerVals.back();
            c.winner = changers.back();
        }
        r.conflicts.push_back(std::move(c));
    };

    // Prefix (base-class bytes) is merged as a part named "<prefix>".
    {
        std::vector<std::vector<uint8_t>> modVals;
        for (const auto& [n, d] : versions) modVals.push_back(d.prefix);
        mergePart("<prefix>", base.prefix, modVals, merged.prefix);
    }
    for (size_t i = 0; i < base.fields.size(); ++i) {
        std::vector<std::vector<uint8_t>> modVals;
        for (const auto& [n, d] : versions) modVals.push_back(d.fields[i].value);
        mergePart(base.fields[i].name.empty() ? "<unnamed>"
                                              : base.fields[i].name,
                  base.fields[i].value, modVals, merged.fields[i].value);
    }

    r.payload = encode(merged);
    r.ok = true;
    return r;
}

std::string formatValue(const DecodedField& f) {
    const auto& v = f.value;
    char buf[64];

    auto hex = [&]() {
        std::string s;
        for (uint8_t b : v) {
            std::snprintf(buf, sizeof(buf), "%02x", b);
            s += buf;
        }
        return s.empty() ? std::string("<empty>") : s;
    };

    if (f.type == "bool") {
        if (v.size() == 1) return v[0] ? "true" : "false";
        return hex();
    }
    if (f.type == "int32" && v.size() == 4) {
        int32_t x;
        std::memcpy(&x, v.data(), 4);
        std::snprintf(buf, sizeof(buf), "%d", x);
        return buf;
    }
    if ((f.type == "uint32" || f.type == "CDefIndex" || f.type == "J" ||
         f.type == "CGameFlag" || f.type == "CBookIndex" || isEnum(f.type)) &&
        v.size() == 4) {
        uint32_t x;
        std::memcpy(&x, v.data(), 4);
        std::snprintf(buf, sizeof(buf), "%u", x);
        return buf;
    }
    if (f.type == "float" && v.size() == 4) {
        float x;
        std::memcpy(&x, v.data(), 4);
        std::snprintf(buf, sizeof(buf), "%g", x);
        return buf;
    }
    if (f.type == "CCharString" || f.type == "CDefString") {
        std::string s = "\"";
        for (uint8_t b : v) {
            if (b == 0) break;
            s += char(b);
        }
        s += "\"";
        return s;
    }
    if (f.type.rfind("Vector_", 0) == 0 && v.size() >= 4) {
        uint32_t count;
        std::memcpy(&count, v.data(), 4);
        std::string s = "count=" + std::to_string(count);

        // CActionInputControl (CControlsDef.Controls) = fixed 28-byte records:
        //   [i32 GameAction][i32 ControllerType: 1=pad,2=key,3=mouse]
        //   [i32 keyVal][i32 xboxVal][i32 mouseVal][f32 dirX][f32 dirY]
        // The device slot matching ControllerType holds the binding; the others
        // are 0. On-disk encoding confirmed against retail game.bin (per-def zlib
        // level-1 stream) -- see FINDINGS.md "ON-DISK ENCODING -- empirically
        // resolved". Render action->button so remaps are legible.
        if (f.type == "Vector_VCActionInputControl__" && count > 0 &&
            v.size() - 4 == static_cast<size_t>(count) * 28) {
            s += " [";
            for (uint32_t i = 0; i < count && i < 16; ++i) {
                const uint8_t* p = v.data() + 4 + static_cast<size_t>(i) * 28;
                int32_t act, typ, key, xb, ms;
                std::memcpy(&act, p, 4);
                std::memcpy(&typ, p + 4, 4);
                std::memcpy(&key, p + 8, 4);
                std::memcpy(&xb, p + 12, 4);
                std::memcpy(&ms, p + 16, 4);
                const char* dev =
                    typ == 1 ? "pad" : typ == 2 ? "key" : typ == 3 ? "mouse" : "?";
                int32_t dv = typ == 1 ? xb : typ == 2 ? key : typ == 3 ? ms : 0;
                if (i) s += ",";
                std::snprintf(buf, sizeof(buf), "a%d:%s=%d", act, dev, dv);
                s += buf;
            }
            if (count > 16) s += ",...";
            s += "]";
            return s;
        }

        // show scalar elements when the element width divides evenly
        if (count > 0 && (v.size() - 4) % count == 0) {
            size_t w = (v.size() - 4) / count;
            if (w == 4) {
                s += " [";
                for (uint32_t i = 0; i < count && i < 16; ++i) {
                    uint32_t x;
                    std::memcpy(&x, v.data() + 4 + i * 4, 4);
                    if (i) s += ",";
                    s += std::to_string(x);
                }
                if (count > 16) s += ",...";
                s += "]";
            }
        }
        return s;
    }
    return hex();
}

} // namespace forge::defdecode
