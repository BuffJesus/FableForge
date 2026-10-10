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
        const bool expression = entry.definition == "CExpressionSubDef" &&
            field.name == "ExpressionDef" && field.type == "int32";
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
        // Existing FMP bindings certify these scalar text domains. Ask the
        // locator to validate the actual owner/tag/width before covering bytes.
        std::string textPath;
        if (entry.definition == "CInventoryItemDef" && field.type == "uint32") {
            if (field.name == "ItemDescription") textPath = "4197094506.TEXT_TITLE";
            if (field.name == "ItemDetails") textPath = "1637763631.TEXT_DESCRIPTION";
        }
        if (entry.definition == "REGION" && field.type == "uint32" && field.name == "RegionDescription")
            textPath = "801675342.Region Description";
        if (entry.definition == "HERO_ABILITY" && field.type == "?" && field.name == "MaxedOutDescription") {
            const auto tag = defdecode::fieldTag(field.name);
            const auto legacy = (tag >> 24) | ((tag >> 8) & 0xff00u) |
                ((tag << 8) & 0xff0000u) | (tag << 24);
            textPath = std::to_string(legacy) + ".HeroAbilityMasteredTextGUI_ID";
        }
        const bool heroCosts = (entry.definition == "HERO_ABILITY" || entry.definition == "CHeroAbilityDef") &&
            field.type == "?" && (field.name == "StaminaCost" || field.name == "ExperienceCostsToUpgrade");
        const bool opinionMask =
            (entry.definition == "OPINION_REACTION_MASK" && field.name == "ReactionEnabled" && field.type == "?") ||
            (entry.definition == "OPINION_DEED_MASK" && field.name == "DeedEnabled" &&
             field.type == "Map__NW4EOpinionDeedType__");
        const bool stringMap = field.type == "Map_VCCharString__K" || field.type == "Map_VCCharString__J";
        const bool sound = entry.definition == "SOUND_SETUP" || entry.definition == "CSoundSetupDef";
        const bool music = sound && field.name == "MusicEntries" && field.type == "Vector_VCMusicEntry__";
        const bool musicSet = sound && field.name == "MusicSetEntries" && field.type == "Vector_VCMusicSetEntry__";
        const bool reverb = sound && field.name == "ReverbEnvironmentEntries" && field.type == "Vector_VCReverbEnvironmentEntry__";
        const bool reactionFrequency = (entry.definition == "OPINION_REACTION_MANAGER" ||
            entry.definition == "COpinionReactionManagerDef") && field.name == "Frequency" &&
            field.type == "CReactionFrequencyTraitsArray";
        const bool reactionMatches = (entry.definition == "OPINION_REACTION_MANAGER" ||
            entry.definition == "COpinionReactionManagerDef") && field.name == "Matches" &&
            field.type == "V__list_PAVCReactionMatch__V__allocator_PAVCReactionMatch___std___std__";
        const bool statLevels = (entry.definition == "HERO_STATS" || entry.definition == "CHeroStatsDef") &&
            field.name == "StatLevels" && field.type == "vector<CAbilityTrackDef>";
        const bool tavernScoreText = entry.definition == "CTavernGameDef" &&
            field.name == "BestScore" && field.type == "?";
        const bool primitiveVector = field.type == "Vector_J" || field.type == "Vector_K" ||
            field.type == "Vector_int32" || field.type == "Vector_uint32" || field.type == "Vector_float" || heroCosts;
        const bool primitiveMap = field.type == "Map_JJ" || field.type == "Map_KK" ||
            field.type == "Map_JK" || field.type == "Map_KJ" ||
            // CIdleSchedulerDef::Transfer 01BBBBA3: map<EIdleStateGroup,long>.
            (entry.definition == "CIdleSchedulerDef" && field.name == "Period" &&
             field.type == "Map_JW4EIdleStateGroup__") ||
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
        } else if (expression) {
            // CTCExpression::OnCreate 0269BF40 reads +28; GetDef<CExpressionDef>
            // 01DFC5A0 uses GetPDefFromGlobalIndex only for signed values > 0.
            if (field.value.size() != 4) fail(entry, "invalid expression index width");
            const auto value = word(field.value, 0);
            if (value && value <= INT32_MAX) {
                const auto found = maps.gameEntries.find(value);
                if (found != maps.gameEntries.end() && (!found->second || found->second > INT32_MAX))
                    fail(entry, "expression index cannot relocate to a null value");
                reference(at, Domain::GameEntry, field.name);
            } else cover(at, 4);
        } else if (!textPath.empty()) {
            const auto location = fmplinks::locate(entry, schema, {textPath, {}});
            if (location.offset != at || location.destination != fmplinks::Destination::TextEntry)
                fail(entry, "text locator disagrees with decoded field");
            cover(at, 4);
        } else if (tavernScoreText) {
            // GetBestScore 0273CB00 passes CTavernGameDef +B4 to GetText
            // 022D72F0, which resolves it through the game text bank.
            if (field.value.size() != 4) fail(entry, "invalid tavern score text width");
            cover(at, 4);
        } else if (statLevels) {
            // Native transfers 01A0E34C/01A0F440. UI 023B058A passes both
            // nested Description and MaxedOutDescription to GetInventoryText.
            size_t cursor = 0;
            auto tagged = [&](std::string_view member, bool scalar, bool separateDomain) {
                const size_t width = scalar ? 8 : 4;
                if (cursor > field.value.size() || width > field.value.size() - cursor ||
                    word(field.value, cursor) != defdecode::fieldTag(member))
                    fail(entry, "invalid stat track member " + std::string(member));
                cover(at + cursor, scalar && separateDomain ? 8 : 4);
                cursor += width;
            };
            auto counted = [&](size_t minimum) {
                if (cursor > field.value.size() || field.value.size() - cursor < 4)
                    fail(entry, "truncated stat track count");
                const size_t count = word(field.value, cursor);
                cover(at + cursor, 4); cursor += 4;
                if (count > (field.value.size() - cursor) / minimum) fail(entry, "invalid stat track count");
                return count;
            };
            const auto tracks = counted(24);
            for (size_t i = 0; i < tracks; ++i) {
                tagged("ExperienceStat", true, true);
                tagged("Abilities", false, false);
                const auto levels = counted(24);
                for (size_t j = 0; j < levels; ++j) {
                    tagged("ExperienceCost", true, false); // retain opaque integer semantics
                    tagged("AbilityUnlocked", true, true); // EHeroAbility, not a definition ID
                    tagged("Description", true, true); // text bank
                }
                tagged("MaxedOutDescription", true, true); // text bank
            }
            if (cursor != field.value.size()) fail(entry, "trailing stat track bytes");
        } else if (reactionMatches) {
            // 01FF2940 writes count, type and virtual body. Types 0/1 write
            // two integers/six floats, 2 writes three/six, 3 three/nine.
            // Type 4 writes one integer, counted float opinions, radius.
            if (field.value.size() < 4) fail(entry, "truncated reaction matches");
            const size_t count = word(field.value, 0);
            if (count > (field.value.size() - 4) / 16) fail(entry, "invalid reaction match count");
            cover(at, 4);
            size_t cursor = 4;
            for (size_t i = 0; i < count; ++i) {
                if (cursor > field.value.size() || field.value.size() - cursor < 4)
                    fail(entry, "truncated reaction match type");
                const auto kind = word(field.value, cursor);
                if (kind > 4) fail(entry, "unknown reaction match type");
                cover(at + cursor, 4); cursor += 4;
                const size_t integers = kind < 2 ? 2 : kind < 4 ? 3 : 1;
                if (integers * 4 > field.value.size() - cursor) fail(entry, "truncated reaction match parameters");
                for (size_t j = 1; j < integers; ++j) primitiveBoundaries.push_back(at + cursor + j * 4);
                cursor += integers * 4;
                size_t floats = kind == 3 ? 9 : 6;
                if (kind == 4) {
                    if (field.value.size() - cursor < 4) fail(entry, "truncated reaction opinion count");
                    floats = word(field.value, cursor);
                    cover(at + cursor, 4); cursor += 4;
                    if (floats >= (field.value.size() - cursor) / 4)
                        fail(entry, "truncated reaction opinions or radius");
                    ++floats; // trailing sphere radius
                }
                if (floats > (field.value.size() - cursor) / 4) fail(entry, "truncated reaction match floats");
                cover(at + cursor, floats * 4); cursor += floats * 4;
            }
            if (cursor != field.value.size()) fail(entry, "trailing reaction match bytes");
        } else if (reactionFrequency) {
            // Native 01FEF150/01FEEEF0: two groups of 79 discriminated
            // frequency traits. Writers 01F1A280/01F1A970/01F1AF40 serialize
            // only floats, frame counts and a bool; never object pointers.
            size_t cursor = 0;
            for (size_t i = 0; i < 158; ++i) {
                if (cursor > field.value.size() || field.value.size() - cursor < 4)
                    fail(entry, "truncated reaction frequency traits");
                const auto kind = word(field.value, cursor); cursor += 4;
                if (kind > 3) fail(entry, "unknown reaction frequency trait kind");
                const size_t length = kind == 0 ? 0 : kind == 1 ? 8 : kind == 2 ? 20 : 21;
                if (length > field.value.size() - cursor) fail(entry, "truncated reaction frequency body");
                if (kind == 3 && field.value[cursor] > 1) fail(entry, "invalid reaction frequency boolean");
                cursor += length;
            }
            if (cursor != field.value.size()) fail(entry, "trailing reaction frequency bytes");
            cover(at, field.value.size());
        } else if (music || musicSet || reverb) {
            // Native binary writers 019D7BA4/019D7E11/019D8052, corroborated
            // by PDB members: music is u32/name/f32/bool; set is three i32;
            // reverb is 21 floats. Numeric music IDs remain opaque here.
            if (field.value.size() < 4) fail(entry, "truncated sound entries");
            const size_t count = word(field.value, 0), stride = music ? 13 : musicSet ? 12 : 84;
            if (count > (field.value.size() - 4) / stride || field.value.size() != 4 + count * stride)
                fail(entry, "invalid sound entry count");
            cover(at, 4);
            if (reverb) cover(at + 4, field.value.size() - 4);
            else if (musicSet) {
                for (size_t offset = 4; offset < field.value.size(); offset += 4)
                    primitiveBoundaries.push_back(at + offset);
            } else for (size_t i = 0; i < count; ++i) {
                const size_t row = 4 + i * stride;
                if (field.value[row + 12] > 1) fail(entry, "invalid music cache boolean");
                reference(at + row + 4, Domain::NameOffset, "MusicEntries[" + std::to_string(i) + "].Filename");
                cover(at + row + 8, 5);
            }
        } else if (stringMap) {
            // Native map<32-bit key,CCharString>: count, then key/ASCIIZ pairs.
            // Keep aligned key values opaque; inline strings are not offsets.
            if (field.value.size() < 4) fail(entry, "truncated string map");
            const size_t count = word(field.value, 0);
            if (count > (field.value.size() - 4) / 5) fail(entry, "invalid string map count");
            size_t cursor = 4;
            cover(at, 4);
            for (size_t i = 0; i < count; ++i) {
                if (cursor > field.value.size() || field.value.size() - cursor < 5)
                    fail(entry, "truncated string map row");
                const size_t start = cursor + 4;
                const auto end = std::find(field.value.begin() + start, field.value.end(), uint8_t(0));
                if (end == field.value.end()) fail(entry, "unterminated string map value");
                cursor = size_t(end - field.value.begin()) + 1;
                cover(at + start, cursor - start);
            }
            if (cursor != field.value.size()) fail(entry, "trailing string map bytes");
        } else if (opinionMask) {
            // Donor transfers 0193B908/0193BE86: enum key + one-byte bool.
            if (field.value.size() < 4) fail(entry, "truncated opinion mask");
            const size_t count = word(field.value, 0);
            if (count > (field.value.size() - 4) / 5 || field.value.size() != 4 + count * 5)
                fail(entry, "invalid opinion mask length");
            for (size_t i = 0; i < count; ++i)
                if (field.value[4 + i * 5 + 4] > 1) fail(entry, "invalid opinion mask boolean");
            cover(at, field.value.size());
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
