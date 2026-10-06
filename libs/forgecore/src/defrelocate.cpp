#include "forge/defrelocate.hpp"
#include "forge/defdecode.hpp"
#include "forge/fmplinks.hpp"
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
    // Native category alias verified by CControlsDef::Transfer (01939BE8) and
    // the literal Controls/toggle tags in CONTROL_SCHEME payloads. Keep this
    // recovery local; it does not change editor field-merge eligibility.
    if (!type && entry.definition == "CONTROL_SCHEME") type = schema.find("CControlsDef");
    if (!type) return result;
    const auto decoded = defdecode::decode(entry.data, *type);
    if (!decoded.clean() || defdecode::encode(decoded) != entry.data ||
        std::any_of(decoded.fields.begin(), decoded.fields.end(),
                    [](const auto& field) { return !field.tagOk; })) return result;

    // Covered bytes belong to verified structural/scalar/reference fields.
    // A four-byte reference cannot straddle those established boundaries.
    std::vector<bool> covered(entry.data.size(), false);
    std::vector<size_t> primitiveBoundaries;
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
    // restricted to verified owners; a similarly sized unknown prefix is not
    // evidence that it contains a component listing. Retail Building::Transfer
    // 00454B8E calls ThingBase::Transfer 00676BE0 (component set before fields).
    if (entry.definition == "OBJECT" || entry.definition == "CObjectDef" ||
        entry.definition == "BUILDING" || entry.definition == "CThingBuildingDef") {
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
    if (entry.definition == "CCreatureDef") {
        fmplinks::CreaturePrefixLayout layout;
        try { layout = fmplinks::creaturePrefixLayout(decoded.prefix); }
        catch (const std::runtime_error&) {
            // Unsupported prefix variants retain all candidate matches. No
            // partial reference list escapes the shared layout verifier.
        }
        for (const auto offset : layout.structuralWords) cover(offset, 4);
        for (const auto& location : layout.references) {
            if (location.destination == fmplinks::Destination::NameOffset)
                reference(location.offset, Domain::NameOffset,
                          "CreaturePrefix.Name@" + std::to_string(location.offset));
            else if (location.destination == fmplinks::Destination::MeshEntry ||
                     location.destination == fmplinks::Destination::TextureEntry)
                cover(location.offset, 4); // separate asset namespaces, retain bytes
            else fail(entry, "unexpected creature prefix reference domain");
        }
    }
    size_t at = decoded.prefixLen;
    for (const auto& field : decoded.fields) {
        cover(at, 4); // Field tag CRC.
        at += 4;
        // Donor CHeroAbilityDef::Transfer 019356A2 explicitly transfers
        // DummyObject as CDefIndex; Ego_r PDB +168 agrees. The retail alias
        // schema still spells this field '?'. Other unknown fields stay opaque.
        const bool heroDummy = (entry.definition == "HERO_ABILITY" || entry.definition == "CHeroAbilityDef") &&
            field.name == "DummyObject" && field.type == "?";
        const bool index = field.type == "CDefIndex" || heroDummy;
        const bool name = field.type == "CDefString";
        const bool indices = field.type == "Vector_VCDefIndex__" || field.type == "vector<CDefIndex>";
        const bool names = field.type == "Vector_VCDefString__" || field.type == "vector<CDefString>";
        const bool heroText = (entry.definition == "HERO_ABILITY" || entry.definition == "CHeroAbilityDef") &&
            field.name == "LevDescription" && field.type == "?";
        const bool brainBehaviours = (entry.definition == "BRAIN" || entry.definition == "CBrainDef") &&
            field.name == "Behaviours" && field.type == "Vector_VCBrainBehaviour__";
        const bool shopStock = entry.definition == "CShopDef" && field.type == "?" &&
            (field.name == "DefaultStock" || field.name == "StockCategoryDefaults");
        const bool controls = (entry.definition == "CONTROL_SCHEME" || entry.definition == "CControlsDef") &&
            field.name == "Controls" && field.type == "Vector_VCActionInputControl__";
        const bool primitiveVector = field.type == "Vector_J" || field.type == "Vector_K" ||
            field.type == "Vector_int32" || field.type == "Vector_uint32" || field.type == "Vector_float";
        const bool primitiveMap = field.type == "Map_JJ" || field.type == "Map_KK" ||
            field.type == "Map_JK" || field.type == "Map_KJ" ||
            ((entry.definition == "UI" || entry.definition == "CUIDef") && field.name == "Sprites" &&
             field.type == "Map_JW4ETableSprites_NUISystem__");
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
        } else if (primitiveVector || primitiveMap) {
            // Counted primitive vectors/maps serialize four-byte words (pairs
            // for maps). Integer semantics stay unknown, but a reference may
            // not straddle two distinct serialized primitive values.
            if (field.value.size() < 4) fail(entry, "truncated primitive container");
            const size_t count = word(field.value, 0), stride = primitiveMap ? 8 : 4;
            if (count > (field.value.size() - 4) / stride || field.value.size() != 4 + count * stride)
                fail(entry, "invalid primitive container length");
            cover(at, 4);
            if (field.type == "Vector_float") cover(at + 4, field.value.size() - 4);
            else for (size_t offset = 4; offset < field.value.size(); offset += 4)
                primitiveBoundaries.push_back(at + offset);
        } else if (controls) {
            // CActionInputControl is five device/action enum words followed by
            // a C2DVector (Ego_r PDB, defdecode's verified 28-byte renderer).
            // None belongs to the game-entry or names-offset namespaces.
            if (field.value.size() < 4) fail(entry, "truncated input control list");
            const size_t count = word(field.value, 0);
            if (count > (field.value.size() - 4) / 28 || field.value.size() != 4 + count * 28)
                fail(entry, "invalid input control list length");
            cover(at, field.value.size());
        } else if (shopStock) {
            if (field.value.size() < 4) fail(entry, "truncated shop stock list");
            const size_t count = word(field.value, 0);
            if (!count) {
                if (field.value.size() != 4) fail(entry, "invalid empty shop stock list");
            } else {
                // Reuse the FMP locator's complete count/stride/eight-tag
                // verification instead of maintaining a second tag table.
                // These are legacy FMP control IDs, not native field-tag CRCs.
                const auto path = field.name == "DefaultStock" ?
                    "3996108785.CShopDefArrayList1.0.ItemForSell" : "2023417370.unknown.0.Unknown";
                const auto first = fmplinks::locate(entry, schema, {path, {}});
                if (first.offset != at + 8 || first.destination != fmplinks::Destination::GameEntry)
                    fail(entry, "shop stock locator disagrees with decoded field");
                for (size_t i = 0; i < count; ++i) {
                    for (size_t member = 0; member < 8; ++member) cover(at + 4 + i * 64 + member * 8, 4);
                    reference(at + 8 + i * 64, Domain::GameEntry, field.name + "[" + std::to_string(i) + "].Item");
                }
            }
            cover(at, 4);
        } else if (brainBehaviours) {
            // The verified FMP CreatureActionList layout and Ego_r PDB agree:
            // 12-byte CBrainBehaviour = CDefString, Priority, ScriptAIPriority.
            // Establish reference boundaries; do not infer references from the
            // two remaining integer parameters, even when their values match.
            if (field.value.size() < 4) fail(entry, "truncated brain behaviour list");
            const size_t count = word(field.value, 0);
            if (count > 4096 || count > (field.value.size() - 4) / 12 ||
                field.value.size() != 4 + count * 12)
                fail(entry, "invalid brain behaviour list length");
            cover(at, 4);
            for (size_t i = 0; i < count; ++i)
                reference(at + 4 + i * 12, Domain::NameOffset, "Behaviours[" + std::to_string(i) + "].Name");
        } else if (heroText) {
            // Donor 019356A2/PDB: vector<long>; fmplinks::locate's verified
            // LevDescription path binds its elements to TextEntry, a separate
            // namespace. Equal game/name IDs must never change these words.
            if (field.value.size() < 4) fail(entry, "truncated hero text vector");
            const size_t count = word(field.value, 0);
            if (count > (field.value.size() - 4) / 4 || field.value.size() != 4 + count * 4)
                fail(entry, "invalid hero text vector length");
            cover(at, field.value.size());
        } else if ((field.type == "float" && field.value.size() == 4) ||
                   ((field.type == "bool" || field.type == "uint8") && field.value.size() == 1)) {
            cover(at, field.value.size());
        }
        // int32/uint32/Vector_J and unknown types need field-specific evidence;
        // their C++ storage type does not tell us their reference domain.
        at += field.value.size();
    }
    std::sort(primitiveBoundaries.begin(), primitiveBoundaries.end());
    std::erase_if(result.unresolved, [&](const Candidate& candidate) {
        const auto next = std::upper_bound(primitiveBoundaries.begin(), primitiveBoundaries.end(), candidate.offset);
        return (next != primitiveBoundaries.end() && *next < candidate.offset + 4) ||
            std::any_of(covered.begin() + candidate.offset, covered.begin() + candidate.offset + 4,
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
