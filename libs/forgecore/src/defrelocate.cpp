#include "forge/defrelocate.hpp"
#include "forge/defdecode.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace forge::defrelocate {
namespace {
uint32_t word(const std::vector<uint8_t>& data, size_t at) {
    return uint32_t(data[at]) | uint32_t(data[at + 1]) << 8 |
           uint32_t(data[at + 2]) << 16 | uint32_t(data[at + 3]) << 24;
}
[[noreturn]] void fail(const bin::Entry& entry, const std::string& reason) {
    throw std::runtime_error("definition relocation: " + entry.definition + "/" + entry.name + ": " + reason);
}
}

Analysis analyze(const bin::Entry& entry, const defschema::Schema& schema, const Maps& maps) {
    std::set<uint32_t> changed;
    for (const auto* domain : {&maps.gameEntries, &maps.nameOffsets}) {
        for (const auto& [before, after] : *domain) {
            if (before == after) continue;
            if (before == UINT32_MAX || after == UINT32_MAX)
                fail(entry, "cannot relocate to or from the null identity");
            if (domain == &maps.nameOffsets && (!before || !after))
                fail(entry, "cannot relocate to or from a zero name offset");
            changed.insert(before);
        }
    }
    Analysis result;
    if (changed.empty()) return result;
    for (size_t at = 0; at + 4 <= entry.data.size(); ++at) {
        const auto value = word(entry.data, at);
        if (changed.contains(value)) result.unresolved.push_back({at, value});
    }
    // An opaque payload with no candidate needs no schema interpretation.
    if (result.unresolved.empty()) return result;
    const auto* type = defdecode::resolveType(schema, entry.definition, entry.data);
    if (!type) return result;
    const auto decoded = defdecode::decode(entry.data, *type);
    if (!decoded.clean() || defdecode::encode(decoded) != entry.data ||
        std::any_of(decoded.fields.begin(), decoded.fields.end(),
                    [](const auto& field) { return !field.tagOk; })) return result;

    // Covered bytes belong to verified structural/scalar/reference fields.
    // A four-byte reference cannot straddle those established boundaries.
    std::vector<bool> covered(entry.data.size(), false);
    auto cover = [&](size_t at, size_t size) {
        if (at > covered.size() || size > covered.size() - at)
            fail(entry, "reference layout exceeds payload");
        std::fill(covered.begin() + at, covered.begin() + at + size, true);
    };
    auto reference = [&](size_t at, Domain domain, std::string path) {
        cover(at, 4);
        const auto& remap = domain == Domain::GameEntry ? maps.gameEntries : maps.nameOffsets;
        const auto before = word(entry.data, at);
        const auto found = remap.find(before);
        if (found != remap.end() && found->second != before)
            result.rewrites.push_back({at, domain, before, found->second, std::move(path)});
    };

    // Existing fmplinks::locate's native-verified CDefs layout. Discovery is
    // restricted to the OBJECT class; a similarly sized unknown prefix is not
    // evidence that it contains a component listing.
    if (entry.definition == "OBJECT" || entry.definition == "CObjectDef") {
        if (decoded.prefixLen >= 5) {
            const size_t count = size_t(entry.data[3]) | size_t(entry.data[4]) << 8;
            if (decoded.prefixLen == 5 + count * 12) {
                cover(0, 5);
                for (size_t i = 0; i < count; ++i) {
                    const size_t at = 5 + i * 12;
                    cover(at, 4); // CDefName is a CRC, not a numeric entry ID.
                    const auto path = ".CDefs." + std::to_string(i);
                    reference(at + 4, Domain::GameEntry, path + ".CDefDataEntry");
                    reference(at + 8, Domain::GameEntry, path + ".InheritedEntryID");
                }
            }
        }
    }
    size_t at = decoded.prefixLen;
    for (const auto& field : decoded.fields) {
        cover(at, 4); // Field tag CRC.
        at += 4;
        const bool index = field.type == "CDefIndex";
        const bool name = field.type == "CDefString";
        const bool indices = field.type == "Vector_VCDefIndex__" || field.type == "vector<CDefIndex>";
        const bool names = field.type == "Vector_VCDefString__" || field.type == "vector<CDefString>";
        if (index || name) {
            if (field.value.size() != 4) fail(entry, "invalid reference width for " + field.name);
            reference(at, index ? Domain::GameEntry : Domain::NameOffset, field.name);
        } else if (indices || names) {
            if (field.value.size() < 4) fail(entry, "truncated reference vector " + field.name);
            const size_t count = word(field.value, 0);
            if (count > (field.value.size() - 4) / 4 || field.value.size() != 4 + count * 4)
                fail(entry, "invalid reference vector length for " + field.name);
            cover(at, 4); // Count, never an entry reference.
            for (size_t i = 0; i < count; ++i)
                reference(at + 4 + i * 4, indices ? Domain::GameEntry : Domain::NameOffset,
                          field.name + "[" + std::to_string(i) + "]");
        } else if ((field.type == "float" && field.value.size() == 4) ||
                   ((field.type == "bool" || field.type == "uint8") && field.value.size() == 1)) {
            cover(at, field.value.size());
        }
        // int32/uint32/Vector_J and unknown types need field-specific evidence;
        // their C++ storage type does not tell us their reference domain.
        at += field.value.size();
    }
    std::erase_if(result.unresolved, [&](const Candidate& candidate) {
        return std::any_of(covered.begin() + candidate.offset, covered.begin() + candidate.offset + 4,
                           [](bool byte) { return byte; });
    });
    return result;
}

std::vector<uint8_t> relocate(const bin::Entry& entry, const defschema::Schema& schema, const Maps& maps) {
    const auto analysis = analyze(entry, schema, maps);
    if (!analysis.unresolved.empty()) {
        const auto& first = analysis.unresolved.front();
        fail(entry, "unclassified possible reference " + std::to_string(first.value) +
                    " at payload offset " + std::to_string(first.offset));
    }
    auto result = entry.data;
    for (const auto& rewrite : analysis.rewrites)
        for (size_t i = 0; i < 4; ++i) result[rewrite.offset + i] = uint8_t(rewrite.after >> (8 * i));
    return result;
}
} // namespace forge::defrelocate
