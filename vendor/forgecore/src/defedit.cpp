#include "forge/defedit.hpp"

#include <cctype>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "forge/defdecode.hpp"

namespace forge::defedit {
namespace {

// Split "a,b,c" into pieces (no trimming beyond surrounding spaces per piece).
std::vector<std::string> splitCommas(std::string_view s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        size_t comma = s.find(',', start);
        std::string_view piece =
            s.substr(start, comma == std::string_view::npos ? std::string_view::npos
                                                            : comma - start);
        // trim surrounding whitespace
        size_t a = 0, b = piece.size();
        while (a < b && std::isspace((unsigned char)piece[a])) ++a;
        while (b > a && std::isspace((unsigned char)piece[b - 1])) --b;
        out.emplace_back(piece.substr(a, b - a));
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    return out;
}

void putU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(uint8_t(v));
    b.push_back(uint8_t(v >> 8));
    b.push_back(uint8_t(v >> 16));
    b.push_back(uint8_t(v >> 24));
}

void putFloat(std::vector<uint8_t>& b, float v) {
    uint32_t u;
    std::memcpy(&u, &v, 4);
    putU32(b, u);
}

int32_t parseI32(const std::string& s, const std::string& what) {
    try {
        size_t pos = 0;
        long v = std::stol(s, &pos, 0);
        if (pos != s.size()) throw std::invalid_argument("trailing");
        return int32_t(v);
    } catch (const std::exception&) {
        throw std::runtime_error("defedit: cannot parse '" + s +
                                 "' as an integer for " + what);
    }
}

uint32_t parseU32(const std::string& s, const std::string& what) {
    try {
        size_t pos = 0;
        unsigned long v = std::stoul(s, &pos, 0);
        if (pos != s.size()) throw std::invalid_argument("trailing");
        return uint32_t(v);
    } catch (const std::exception&) {
        throw std::runtime_error("defedit: cannot parse '" + s +
                                 "' as an unsigned integer for " + what);
    }
}

float parseFloat(const std::string& s, const std::string& what) {
    try {
        size_t pos = 0;
        float v = std::stof(s, &pos);
        if (pos != s.size()) throw std::invalid_argument("trailing");
        return v;
    } catch (const std::exception&) {
        throw std::runtime_error("defedit: cannot parse '" + s +
                                 "' as a float for " + what);
    }
}

uint8_t parseColorByte(const std::string& s, const std::string& what) {
    long v = parseI32(s, what);
    if (v < 0 || v > 255)
        throw std::runtime_error("defedit: colour component '" + s +
                                 "' out of range 0-255 for " + what);
    return uint8_t(v);
}

bool parseBool(const std::string& s) {
    if (s == "1" || s == "true" || s == "TRUE" || s == "True") return true;
    if (s == "0" || s == "false" || s == "FALSE" || s == "False") return false;
    throw std::runtime_error("defedit: cannot parse '" + s +
                             "' as a bool (use 1/0/true/false)");
}

// Encode `valueString` into the on-disk byte form for the declared `type`.
// `oldValue` is the field's current bytes — used only to preserve exact
// layout for CWideString (whose count-prefix width we confirm from the donor).
std::vector<uint8_t> encodeValue(const std::string& type,
                                 std::string_view valueString,
                                 const std::vector<uint8_t>& oldValue,
                                 const std::string& field) {
    const std::string vs(valueString);
    std::vector<uint8_t> out;

    if (type == "int32") {
        putU32(out, uint32_t(parseI32(vs, field)));
        return out;
    }
    if (type == "uint32") {
        putU32(out, parseU32(vs, field));
        return out;
    }
    if (type == "CDefString") {
        // A def-string is a 4-byte reference index, not inline text.
        putU32(out, parseU32(vs, field));
        return out;
    }
    if (type == "float") {
        putFloat(out, parseFloat(vs, field));
        return out;
    }
    if (type == "bool") {
        out.push_back(parseBool(vs) ? 1 : 0);
        return out;
    }
    if (type == "C2DVector") {
        auto parts = splitCommas(vs);
        if (parts.size() != 2)
            throw std::runtime_error("defedit: C2DVector " + field +
                                     " expects \"x,y\", got '" + vs + "'");
        putFloat(out, parseFloat(parts[0], field + ".x"));
        putFloat(out, parseFloat(parts[1], field + ".y"));
        return out;
    }
    if (type == "C3DVector") {
        auto parts = splitCommas(vs);
        if (parts.size() != 3)
            throw std::runtime_error("defedit: C3DVector " + field +
                                     " expects \"x,y,z\", got '" + vs + "'");
        putFloat(out, parseFloat(parts[0], field + ".x"));
        putFloat(out, parseFloat(parts[1], field + ".y"));
        putFloat(out, parseFloat(parts[2], field + ".z"));
        return out;
    }
    if (type == "CRGBColour" || type == "ColourRGBA") {
        auto parts = splitCommas(vs);
        if (parts.size() != 3 && parts.size() != 4)
            throw std::runtime_error("defedit: " + type + " " + field +
                                     " expects \"r,g,b\" or \"r,g,b,a\" (0-255), "
                                     "got '" + vs + "'");
        // On-disk order matches the donor: [b0 b1 b2 a] where the decoded value
        // for opaque colours reads 000000ff, i.e. r,g,b then alpha as the 4th
        // byte. We write the four components in r,g,b,a source order.
        out.push_back(parseColorByte(parts[0], field + ".r"));
        out.push_back(parseColorByte(parts[1], field + ".g"));
        out.push_back(parseColorByte(parts[2], field + ".b"));
        out.push_back(parts.size() == 4 ? parseColorByte(parts[3], field + ".a")
                                        : uint8_t(255));
        return out;
    }
    if (type == "CCharString") {
        // Null-terminated ASCII.
        for (char c : vs) out.push_back(uint8_t(c));
        out.push_back(0);
        return out;
    }
    if (type == "CWideString") {
        // u16 count prefix + count UTF-16LE code units. Confirm the donor uses a
        // 2-byte prefix (all observed CWideStrings do) before writing.
        if (oldValue.size() < 2)
            throw std::runtime_error(
                "defedit: CWideString " + field +
                " has an unexpected on-disk layout (value < 2 bytes); refusing "
                "to guess its encoding");
        out.push_back(uint8_t(vs.size()));
        out.push_back(uint8_t(vs.size() >> 8));
        for (char c : vs) {
            out.push_back(uint8_t(c));  // ASCII low byte
            out.push_back(0);           // UTF-16LE high byte
        }
        return out;
    }

    throw std::runtime_error("defedit: field " + field + " has unsupported type '" +
                             type + "' (supported: int32, uint32, float, bool, "
                             "C2DVector, C3DVector, CRGBColour, CWideString, "
                             "CDefString, CCharString)");
}

SetFieldResult setFieldImpl(bin::File& file, const defschema::Schema& schema,
                            size_t index, std::string_view fieldName,
                            std::string_view valueString) {
    if (index >= file.entries().size())
        throw std::runtime_error("defedit: entry index " + std::to_string(index) +
                                 " out of range");
    const bin::Entry& entry = file.entries()[index];

    const auto* def =
        defdecode::resolveType(schema, entry.definition, entry.data);
    if (def == nullptr)
        throw std::runtime_error(
            "defedit: no schema decodes def type '" + entry.definition +
            "' for entry " + std::to_string(index) +
            " — cannot locate fields by tag");

    auto decoded = defdecode::decode(entry.data, *def);
    if (!decoded.clean())
        throw std::runtime_error(
            "defedit: entry " + std::to_string(index) + " (" + entry.definition +
            (entry.name.empty() ? "" : " / " + entry.name) +
            ") did not decode cleanly — refusing to edit an unrecognized payload");

    defdecode::DecodedField* target = nullptr;
    for (auto& f : decoded.fields) {
        if (f.name == fieldName) { target = &f; break; }
    }
    if (target == nullptr)
        throw std::runtime_error(
            "defedit: field '" + std::string(fieldName) + "' not found in " +
            entry.definition + " (entry " + std::to_string(index) + ")");

    SetFieldResult r;
    r.entryIndex = index;
    r.definition = entry.definition;
    r.entryName = entry.name;
    r.field = std::string(fieldName);
    r.type = target->type;
    r.oldValue = defdecode::formatValue(*target);
    r.oldPayloadSize = entry.data.size();

    target->value =
        encodeValue(target->type, valueString, target->value, r.field);
    r.newValue = defdecode::formatValue(*target);

    std::vector<uint8_t> rebuilt = defdecode::encode(decoded);
    r.newPayloadSize = rebuilt.size();
    file.setEntryData(index, std::move(rebuilt));
    return r;
}

// Decode the named entry and hand back the decoded field (throws like setFieldImpl).
defdecode::DecodedField* locateField(const bin::File& file, const defschema::Schema& schema,
                                     std::string_view entryName, std::string_view fieldName,
                                     defdecode::Decoded& decoded, size_t& index) {
    if (entryName.empty())
        throw std::runtime_error("defedit: empty entry name never matches");
    const bin::Entry* entry = file.find(entryName);
    if (entry == nullptr)
        throw std::runtime_error("defedit: no entry named '" + std::string(entryName) + "'");
    index = size_t(entry - file.entries().data());
    const auto* def = defdecode::resolveType(schema, entry->definition, entry->data);
    if (def == nullptr)
        throw std::runtime_error("defedit: no schema decodes def type '" + entry->definition + "'");
    decoded = defdecode::decode(entry->data, *def);
    if (!decoded.clean())
        throw std::runtime_error("defedit: entry " + std::string(entryName) + " did not decode cleanly");
    for (auto& f : decoded.fields)
        if (f.name == fieldName) return &f;
    throw std::runtime_error("defedit: field '" + std::string(fieldName) + "' not found in " + entry->definition);
}

} // namespace

std::vector<uint8_t> getFieldBytes(const bin::File& file, const defschema::Schema& schema,
                                   std::string_view entryName, std::string_view fieldName) {
    defdecode::Decoded decoded;
    size_t index = 0;
    return locateField(file, schema, entryName, fieldName, decoded, index)->value;
}

SetFieldResult setFieldBytes(bin::File& file, const defschema::Schema& schema,
                             std::string_view entryName, std::string_view fieldName,
                             std::vector<uint8_t> valueBytes) {
    defdecode::Decoded decoded;
    size_t index = 0;
    defdecode::DecodedField* target = locateField(file, schema, entryName, fieldName, decoded, index);
    const bin::Entry& entry = file.entries()[index];
    SetFieldResult r;
    r.entryIndex = index;
    r.definition = entry.definition;
    r.entryName = entry.name;
    r.field = std::string(fieldName);
    r.type = target->type;
    r.oldValue = defdecode::formatValue(*target);
    r.oldPayloadSize = entry.data.size();
    target->value = std::move(valueBytes);
    r.newValue = defdecode::formatValue(*target);
    std::vector<uint8_t> rebuilt = defdecode::encode(decoded);
    r.newPayloadSize = rebuilt.size();
    file.setEntryData(index, std::move(rebuilt));
    return r;
}

SetFieldResult setField(bin::File& file, const defschema::Schema& schema,
                        std::string_view entryName, std::string_view fieldName,
                        std::string_view valueString) {
    if (entryName.empty())
        throw std::runtime_error(
            "defedit: empty entry name never matches; use the index overload for "
            "unnamed sub-defs");
    const bin::Entry* entry = file.find(entryName);
    if (entry == nullptr)
        throw std::runtime_error("defedit: no entry named '" +
                                 std::string(entryName) + "'");
    const size_t index = size_t(entry - file.entries().data());
    return setFieldImpl(file, schema, index, fieldName, valueString);
}

SetFieldResult setField(bin::File& file, const defschema::Schema& schema,
                        size_t entryIndex, std::string_view fieldName,
                        std::string_view valueString) {
    return setFieldImpl(file, schema, entryIndex, fieldName, valueString);
}

} // namespace forge::defedit
