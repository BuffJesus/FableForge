#include "forge/fmplinks.hpp"
#include "forge/defdecode.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace {
using Bytes = std::vector<uint8_t>;
void word(Bytes& out, uint32_t value) { for (int i = 0; i < 4; ++i) out.push_back(uint8_t(value >> (i * 8))); }
void text(Bytes& out, const std::string& value) {
    size_t n = value.size();
    do { out.push_back(uint8_t(n & 127) | (n >= 128 ? 128 : 0)); n >>= 7; } while (n);
    out.insert(out.end(), value.begin(), value.end());
}
void ref(Bytes& out, uint32_t id) { out.push_back(9); word(out, id); }
void string(Bytes& out, uint32_t id, const std::string& value) { out.push_back(6); word(out, id); text(out, value); }
Bytes metadata() {
    Bytes b{0}; word(b, 1); word(b, UINT32_MAX); word(b, 1); word(b, 0);
    b.push_back(4); word(b, 1); text(b, "System.Collections.Specialized.NameValueCollection");
    word(b, 3); text(b, "Count"); text(b, "Keys"); text(b, "Values");
    b.insert(b.end(), {0, 6, 5, 8}); word(b, 1); ref(b, 2); ref(b, 3);
    b.push_back(17); word(b, 2); word(b, 1); string(b, 4, "123._");
    b.push_back(16); word(b, 3); word(b, 1); ref(b, 5);
    b.push_back(4); word(b, 5); text(b, "System.Collections.ArrayList"); word(b, 3);
    text(b, "_items"); text(b, "_size"); text(b, "_version"); b.insert(b.end(), {5, 0, 0, 8, 8});
    ref(b, 6); word(b, 1); word(b, 0);
    b.push_back(16); word(b, 6); word(b, 4); string(b, 7, "TARGET"); b.insert(b.end(), {13, 3, 11});
    return b;
}
Bytes metadataTwo(const std::string& path = "123._") {
    Bytes b{0}; word(b, 1); word(b, UINT32_MAX); word(b, 1); word(b, 0);
    b.push_back(4); word(b, 1); text(b, "System.Collections.Specialized.NameValueCollection");
    word(b, 3); text(b, "Count"); text(b, "Keys"); text(b, "Values");
    b.insert(b.end(), {0, 6, 5, 8}); word(b, 1); ref(b, 2); ref(b, 3);
    b.push_back(17); word(b, 2); word(b, 1); string(b, 4, path);
    b.push_back(16); word(b, 3); word(b, 1); ref(b, 5);
    b.push_back(4); word(b, 5); text(b, "System.Collections.ArrayList"); word(b, 3);
    text(b, "_items"); text(b, "_size"); text(b, "_version"); b.insert(b.end(), {5, 0, 0, 8, 8});
    ref(b, 6); word(b, 2); word(b, 0);
    b.push_back(16); word(b, 6); word(b, 2); string(b, 7, "FIRST"); string(b, 8, "SECOND"); b.push_back(11);
    return b;
}
}

int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) { if (!ok) { ++failures; std::fprintf(stderr, "FMP links: %s\n", what); } };
    auto rejects = [&](auto action, const char* what) {
        try { action(); check(false, what); } catch (const std::runtime_error&) {}
    };
    const auto valid = metadata();
    const auto links = forge::fmplinks::readMetadata(valid);
    check(links.size() == 1 && links[0].path == "123._" && links[0].target == "TARGET", "forward refs, arrays and null padding");
    const auto groups = forge::fmplinks::readMetadataGroups(metadataTwo());
    check(groups.size() == 1 && groups[0].path == "123._" &&
          groups[0].targets == std::vector<std::string>({"FIRST", "SECOND"}),
          "two-target metadata is available for inspection");
    rejects([&] { forge::fmplinks::readMetadata(metadataTwo()); },
            "writer keeps unsupported two-target references guarded");
    const auto villageLinks = forge::fmplinks::readMetadata(metadataTwo("4272390883.VillagePeopleID"));
    check(villageLinks.size() == 2 &&
          villageLinks[0].path == "4272390883.VillagePeopleID.0" && villageLinks[0].target == "FIRST" &&
          villageLinks[1].path == "4272390883.VillagePeopleID.1" && villageLinks[1].target == "SECOND",
          "repeated village controls preserve both ordered targets");
    for (size_t size = 0; size < valid.size(); ++size)
        rejects([&] { forge::fmplinks::readMetadata(std::span(valid).first(size)); }, "every truncated prefix refuses");
    auto bad = valid; bad.push_back(0);
    rejects([&] { forge::fmplinks::readMetadata(bad); }, "trailing bytes refuse");
    bad = valid; bad[9] = 2;
    rejects([&] { forge::fmplinks::readMetadata(bad); }, "unknown stream version refuses");
    bad = valid; bad[bad.size() - 2] = 5;
    rejects([&] { forge::fmplinks::readMetadata(bad); }, "excessive null run refuses");
    bad = valid; bad[17] = 21;
    rejects([&] { forge::fmplinks::readMetadata(bad); }, "method-call records refuse");
    bad = valid; bad[1] = 99;
    rejects([&] { forge::fmplinks::readMetadata(bad); }, "dangling root reference refuses");
    bad = valid;
    const Bytes arrayHeader{17, 2, 0, 0, 0, 1, 0, 0, 0};
    const auto arrayAt = std::search(bad.begin(), bad.end(), arrayHeader.begin(), arrayHeader.end());
    check(arrayAt != bad.end(), "synthetic array header exists");
    if (arrayAt != bad.end()) {
        const auto offset = size_t(arrayAt - bad.begin());
        bad[offset + 5] = 0xff; bad[offset + 6] = bad[offset + 7] = bad[offset + 8] = 0x7f;
        rejects([&] { forge::fmplinks::readMetadata(bad); }, "excessive array allocation refuses");
    }

    const auto schema = forge::defschema::Schema::loadText(R"({"TEST":{"prefix_len":5,"fields":[{"name":"Alpha","type":"int32"},{"name":"Beta","type":"Vector_J"}]},"PREFIX":{"prefix_len":17,"fields":[]}})");
    forge::defdecode::Decoded decoded;
    decoded.prefix = {1, 0, 1, 0, 0}; decoded.prefixLen = 5; decoded.allTagsOk = true;
    forge::defdecode::DecodedField alpha, beta;
    alpha.name = "Alpha"; alpha.type = "int32"; alpha.tag = forge::defdecode::fieldTag(alpha.name); alpha.tagOk = true; word(alpha.value, 123);
    beta.name = "Beta"; beta.type = "Vector_J"; beta.tag = forge::defdecode::fieldTag(beta.name); beta.tagOk = true;
    for (uint32_t value : {3, 77, 78, 79}) word(beta.value, value);
    decoded.fields = {alpha, beta};
    forge::bin::Entry owner{"TEST", "OWNER", 0, forge::defdecode::encode(decoded)};
    const auto original = owner.data;
    forge::bin::File targets;
    targets.addEntry("TARGET", "A", {0}); targets.addEntry("TARGET", "B", {0});
    // Legacy big-endian control IDs for Alpha/Beta, independently computed.
    const std::string alphaId = "1944126423", betaId = "1098180366";
    const std::string scalar = alphaId + "._";
    const std::string vector = betaId + ".Array.Add(ENTRY).1._";
    std::vector<forge::fmplinks::Link> references{{scalar, "A"}, {vector, "B"}};
    const auto patched = forge::fmplinks::relink(owner, targets, schema, references);
    auto expected = original; std::fill(expected.begin() + 9, expected.begin() + 13, 0);
    expected[25] = 1; expected[26] = expected[27] = expected[28] = 0;
    check(patched == expected && owner.data == original, "only intended scalar/vector words change; input stays const");
    references = {{scalar, "A"}, {betaId + ".unknown.1.KnockDown Effects", "B"}};
    check(forge::fmplinks::relink(owner, targets, schema, references) == expected,
          "flat reference vector item may have a descriptive legacy leaf name");
    references = {{scalar, "MISSING"}};
    rejects([&] { forge::fmplinks::relink(owner, targets, schema, references); }, "missing named target refuses");
    check(forge::fmplinks::relink(owner, targets, schema, references, true) == original, "unused missing target may be deferred by composer");
    references = {{betaId + ".Array.3._", "A"}};
    rejects([&] { forge::fmplinks::relink(owner, targets, schema, references); }, "out-of-range vector link refuses");
    references = {{betaId + ".Array.-1._", "A"}};
    rejects([&] { forge::fmplinks::relink(owner, targets, schema, references); }, "negative index refuses");
    references = {{scalar, "A"}, {alphaId + ".Other", "B"}};
    rejects([&] { forge::fmplinks::relink(owner, targets, schema, references); }, "overlapping links refuse");
    references = {{"1._", "A"}};
    rejects([&] { forge::fmplinks::relink(owner, targets, schema, references); }, "unknown control refuses");
    auto broken = owner; broken.data[13] ^= 0x80;
    references = {{scalar, "A"}};
    rejects([&] { forge::fmplinks::relink(broken, targets, schema, references); }, "unclean decode refuses");
    broken = owner; broken.data[17] = 100;
    references = {{vector, "B"}};
    rejects([&] { forge::fmplinks::relink(broken, targets, schema, references); }, "reference vector count must fit field bytes");

    const auto objectSchema = forge::defschema::Schema::loadText(
        R"({"OBJECT":{"prefix_len":3,"fields":[{"name":"Graphic","type":"CEngineGraphic"}]}})");
    forge::defdecode::Decoded objectDecoded;
    objectDecoded.prefix = {1, 0, 0}; objectDecoded.prefixLen = 3; objectDecoded.allTagsOk = true;
    forge::defdecode::DecodedField graphic;
    graphic.name = "Graphic"; graphic.type = "CEngineGraphic";
    graphic.tag = forge::defdecode::fieldTag(graphic.name); graphic.tagOk = true;
    word(graphic.value, 5); word(graphic.value, 77);
    graphic.value.insert(graphic.value.end(), {0, 0, 0, 0, 0, 0, 128, 63, 0});
    objectDecoded.fields = {graphic};
    forge::bin::Entry object{"OBJECT", "OBJECT_PROBE", 0, forge::defdecode::encode(objectDecoded)};
    const auto originalObject = object.data;
    const std::map<std::string, uint32_t> meshIds{{"MESH_PROBE", 8133}};
    references = {{"3361958702.Model ID", "MESH_PROBE"}};
    const auto linkedObject = forge::fmplinks::relink(object, targets, objectSchema,
                                                     references, false, &meshIds);
    auto expectedObject = originalObject;
    expectedObject[11] = 0xc5; expectedObject[12] = 0x1f;
    expectedObject[13] = expectedObject[14] = 0;
    check(linkedObject == expectedObject && object.data == originalObject,
          "CEngineGraphic Model ID changes only its four-byte mesh reference");
    rejects([&] { forge::fmplinks::relink(object, targets, objectSchema, references); },
            "mesh link without an identity map refuses");

    const auto villageSchema = forge::defschema::Schema::loadText(
        R"({"CVillagePeopleDef":{"prefix_len":3,"fields":[{"name":"MaleTeacher","type":"int32"},{"name":"MaleTeacher","type":"int32"}]}})");
    forge::defdecode::Decoded villageDecoded;
    villageDecoded.prefix = {1, 0, 0}; villageDecoded.prefixLen = 3; villageDecoded.allTagsOk = true;
    forge::defdecode::DecodedField teacher;
    teacher.name = "MaleTeacher"; teacher.type = "int32";
    teacher.tag = forge::defdecode::fieldTag(teacher.name); teacher.tagOk = true;
    word(teacher.value, 77);
    villageDecoded.fields = {teacher, teacher};
    forge::bin::Entry village{"CVillagePeopleDef", "VILLAGE_PROBE", 0,
                              forge::defdecode::encode(villageDecoded)};
    const auto originalVillage = village.data;
    references = {{"4272390883.VillagePeopleID.0", "B"},
                  {"4272390883.VillagePeopleID.1", "A"}};
    auto linkedVillage = forge::fmplinks::relink(village, targets, villageSchema, references);
    auto expectedVillage = originalVillage;
    expectedVillage[7] = 1; expectedVillage[8] = expectedVillage[9] = expectedVillage[10] = 0;
    expectedVillage[15] = expectedVillage[16] = expectedVillage[17] = expectedVillage[18] = 0;
    check(linkedVillage == expectedVillage && village.data == originalVillage,
          "both repeated MaleTeacher references change only their own words");
    references = {{"4272390883.VillagePeopleID.2", "A"}};
    rejects([&] { forge::fmplinks::relink(village, targets, villageSchema, references); },
            "out-of-range repeated village reference refuses");

    const auto creatureSchema = forge::defschema::Schema::loadText(
        R"({"CREATURE":{"prefix_len":3,"fields":[{"name":"InitialWeaponDef","type":"?"},{"name":"RangedWeaponDef","type":"?"}]}})");
    forge::defdecode::Decoded creatureDecoded;
    creatureDecoded.prefix = {1, 0, 0}; creatureDecoded.prefixLen = 3; creatureDecoded.allTagsOk = true;
    forge::defdecode::DecodedField weapons, ranged;
    weapons.name = "InitialWeaponDef"; weapons.type = "?";
    weapons.tag = forge::defdecode::fieldTag(weapons.name); weapons.tagOk = true;
    word(weapons.value, UINT32_MAX);
    word(weapons.value, forge::defdecode::fieldTag("PrimaryMeleeWeaponDef")); word(weapons.value, 77);
    word(weapons.value, forge::defdecode::fieldTag("SecondaryMeleeWeaponDef")); word(weapons.value, 78);
    ranged.name = "RangedWeaponDef"; ranged.type = "?";
    ranged.tag = forge::defdecode::fieldTag(ranged.name); ranged.tagOk = true;
    word(ranged.value, UINT32_MAX);
    word(ranged.value, forge::defdecode::fieldTag("InitialSheathedWeapon")); word(ranged.value, 0);
    word(ranged.value, forge::defdecode::fieldTag("PBrain")); word(ranged.value, 79);
    word(ranged.value, forge::defdecode::fieldTag("CreatureInteractionType")); word(ranged.value, 0);
    creatureDecoded.fields = {weapons, ranged};
    forge::bin::Entry creature{"CREATURE", "CREATURE_PROBE", 0,
                               forge::defdecode::encode(creatureDecoded)};
    const auto originalCreature = creature.data;
    references = {{"3898150842.Weapon1ID", "B"}, {"2486342274.Weapon2ID", "A"},
                  {"1867951032.BrainID", "B"}};
    const auto linkedCreature = forge::fmplinks::relink(creature, targets, creatureSchema, references);
    auto expectedCreature = originalCreature;
    expectedCreature[15] = 1; expectedCreature[16] = expectedCreature[17] = expectedCreature[18] = 0;
    expectedCreature[23] = expectedCreature[24] = expectedCreature[25] = expectedCreature[26] = 0;
    expectedCreature[47] = 1; expectedCreature[48] = expectedCreature[49] = expectedCreature[50] = 0;
    check(linkedCreature == expectedCreature && creature.data == originalCreature,
          "nested creature weapon and brain links change only their validated words");
    auto malformedCreature = creature;
    malformedCreature.data[11] ^= 1;
    rejects([&] { forge::fmplinks::relink(malformedCreature, targets, creatureSchema, references); },
            "unexpected nested creature tag refuses");

    const auto assetCreatureSchema = forge::defschema::Schema::loadText(
        R"({"CREATURE":{"prefix_len":3,"fields":[{"name":"Damage","type":"?"},{"name":"Graphic","type":"?"},{"name":"ResetCombatTypeOnUnsheatheDefaultWeapons","type":"?"}]}})");
    forge::defdecode::Decoded assetDecoded;
    assetDecoded.prefix = {1, 0, 0}; assetDecoded.prefixLen = 3; assetDecoded.allTagsOk = true;
    forge::defdecode::DecodedField damage, creatureGraphic, combat;
    damage.name = "Damage"; damage.type = "?";
    damage.tag = forge::defdecode::fieldTag(damage.name); damage.tagOk = true;
    word(damage.value, 0); word(damage.value, forge::defdecode::fieldTag("DamageEffectDef"));
    word(damage.value, 0); word(damage.value, forge::defdecode::fieldTag("StunnedParticle"));
    word(damage.value, 0);
    creatureGraphic.name = "Graphic"; creatureGraphic.type = "?";
    creatureGraphic.tag = forge::defdecode::fieldTag(creatureGraphic.name); creatureGraphic.tagOk = true;
    word(creatureGraphic.value, 4); word(creatureGraphic.value, 0);
    creatureGraphic.value.insert(creatureGraphic.value.end(), 9, 0);
    word(creatureGraphic.value, forge::defdecode::fieldTag("EyeGraphic"));
    word(creatureGraphic.value, 5); word(creatureGraphic.value, 0);
    word(creatureGraphic.value, 0); word(creatureGraphic.value, 0);
    creatureGraphic.value.push_back(0);
    word(creatureGraphic.value, forge::defdecode::fieldTag("DefaultOwner")); word(creatureGraphic.value, 0);
    word(creatureGraphic.value, forge::defdecode::fieldTag("ControlledMovementType")); word(creatureGraphic.value, 0);
    word(creatureGraphic.value, forge::defdecode::fieldTag("CombatTypeDef")); word(creatureGraphic.value, 0);
    combat.name = "ResetCombatTypeOnUnsheatheDefaultWeapons"; combat.type = "?";
    combat.tag = forge::defdecode::fieldTag(combat.name); combat.tagOk = true;
    combat.value.push_back(0);
    word(combat.value, forge::defdecode::fieldTag("HeroCombatDef")); word(combat.value, 0);
    word(combat.value, 0x1acec323u); word(combat.value, 0);
    word(combat.value, forge::defdecode::fieldTag("CombatDiameter")); word(combat.value, 0);
    assetDecoded.fields = {damage, creatureGraphic, combat};
    forge::bin::Entry assetCreature{"CREATURE", "CREATURE_ASSET_PROBE", 0,
                                    forge::defdecode::encode(assetDecoded)};
    const auto originalAsset = assetCreature.data;
    const std::map<std::string, uint32_t> creatureMeshes{{"MESH_BODY", 8136}, {"MESH_EYE", 8137}};
    const std::map<std::string, uint32_t> creatureTextures{{"TEXTURE_TORSO", 200}};
    references = {{"3896642804.TorsoMainTextureID", "TEXTURE_TORSO"},
                  {"3361958702.Model ID", "MESH_BODY"},
                  {"2104816530.Model Eye ID", "MESH_EYE"},
                  {"587433138.AttackStyleID", "B"},
                  {"368349979.CombatType1ID", "B"},
                  {"600034842.CombatType2ID", "A"}};
    const auto linkedAsset = forge::fmplinks::relink(assetCreature, targets, assetCreatureSchema,
                                                    references, false, &creatureMeshes, &creatureTextures);
    auto expectedAsset = originalAsset;
    auto put = [&](size_t at, uint32_t value) {
        for (int byte = 0; byte < 4; ++byte) expectedAsset[at + byte] = uint8_t(value >> (byte * 8));
    };
    put(23, 200); put(35, 8136); put(56, 8137);
    put(89, 1); put(102, 1); put(110, 0);
    check(linkedAsset == expectedAsset && assetCreature.data == originalAsset,
          "nested creature texture, body/eye mesh and combat words relink without other edits");
    rejects([&] { forge::fmplinks::relink(assetCreature, targets, assetCreatureSchema,
                                         references, false, &creatureMeshes); },
            "missing named creature texture refuses");

    const auto effectsSchema = forge::defschema::Schema::loadText(
        R"({"CSpecialEffectsDef":{"prefix_len":3,"fields":[{"name":"SpecialEffects","type":"CSpecialEffectsStringMap"}]}})");
    forge::defdecode::Decoded effectsDecoded;
    effectsDecoded.prefix = {1, 0, 0}; effectsDecoded.prefixLen = 3; effectsDecoded.allTagsOk = true;
    forge::defdecode::DecodedField effectsField;
    effectsField.name = "SpecialEffects"; effectsField.type = "CSpecialEffectsStringMap";
    effectsField.tag = forge::defdecode::fieldTag(effectsField.name); effectsField.tagOk = true;
    word(effectsField.value, 2);
    word(effectsField.value, 0x11111111u); word(effectsField.value, 7);
    word(effectsField.value, 0x22222222u); word(effectsField.value, 8);
    effectsDecoded.fields = {effectsField};
    forge::bin::Entry specialEffects{"CSpecialEffectsDef", "SPECIAL_EFFECTS_PROBE", 0,
                                     forge::defdecode::encode(effectsDecoded)};
    const auto originalEffects = specialEffects.data;
    const std::map<std::string, uint32_t> effectIds{{"EFFECT_B", 909}};
    references = {{"44838243.CSpecialEffectsDefList.1.CSpecialEffectsDefLink", "EFFECT_B"}};
    auto linkedEffects = forge::fmplinks::relink(specialEffects, targets, effectsSchema,
                                                 references, false, nullptr, nullptr, &effectIds);
    auto expectedEffects = originalEffects;
    expectedEffects[23] = 0x8d; expectedEffects[24] = 0x03;
    expectedEffects[25] = expectedEffects[26] = 0;
    check(linkedEffects == expectedEffects && specialEffects.data == originalEffects,
          "special effects link rewrites only the second effect ID");
    references = {{"44838243.CSpecialEffectsDefList.2.CSpecialEffectsDefLink", "EFFECT_B"}};
    rejects([&] { forge::fmplinks::relink(specialEffects, targets, effectsSchema,
                                         references, false, nullptr, nullptr, &effectIds); },
            "special effects index outside the array refuses");
    auto malformedEffects = specialEffects;
    malformedEffects.data.pop_back();
    references = {{"44838243.CSpecialEffectsDefList.1.CSpecialEffectsDefLink", "EFFECT_B"}};
    rejects([&] { forge::fmplinks::relink(malformedEffects, targets, effectsSchema,
                                         references, false, nullptr, nullptr, &effectIds); },
            "truncated special effects record refuses");

    const auto soundSchema = forge::defschema::Schema::loadText(
        R"({"CEntitySoundDef":{"prefix_len":3,"fields":[{"name":"SoundMap","type":"CSoundMap"}]}})");
    forge::defdecode::Decoded soundDecoded;
    soundDecoded.prefix = {1, 0, 0}; soundDecoded.prefixLen = 3; soundDecoded.allTagsOk = true;
    forge::defdecode::DecodedField soundField;
    soundField.name = "SoundMap"; soundField.type = "CSoundMap";
    soundField.tag = forge::defdecode::fieldTag(soundField.name); soundField.tagOk = true;
    word(soundField.value, 0); word(soundField.value, 1);
    word(soundField.value, 0x96cee596u); word(soundField.value, 301315);
    soundDecoded.fields = {soundField};
    forge::bin::Entry soundMap{"CEntitySoundDef", "SOUND_MAP_PROBE", 0,
                               forge::defdecode::encode(soundDecoded)};
    const forge::fmplinks::Link fireLink{
        "1791516475.unknown.0.Default Fire Effect", "ON_FIRE"};
    const auto location = forge::fmplinks::locate(soundMap, soundSchema, fireLink);
    check(location.offset == 19 && location.destination == forge::fmplinks::Destination::NameOffset,
          "sound-map criteria link addresses its name-offset word");
    auto malformedSound = soundMap;
    malformedSound.data.pop_back();
    rejects([&] { forge::fmplinks::locate(malformedSound, soundSchema, fireLink); },
            "truncated sound-map criteria refuses");
    auto unsupportedSound = soundMap;
    unsupportedSound.data[7] = 1;
    rejects([&] { forge::fmplinks::locate(unsupportedSound, soundSchema, fireLink); },
            "unverified sound-map singles layout refuses");

    forge::bin::Entry prefix{"PREFIX", "COMPONENTS", 0, {1, 0, 1, 1, 0}};
    word(prefix.data, 42); word(prefix.data, UINT32_MAX); word(prefix.data, 77);
    const auto oldPrefix = prefix.data;
    references = {{".CDefs.0.InheritedEntryID", "B"}};
    const auto component = forge::fmplinks::relink(prefix, targets, schema, references);
    auto expectedPrefix = oldPrefix; expectedPrefix[13] = 1; expectedPrefix[14] = expectedPrefix[15] = expectedPrefix[16] = 0;
    check(component == expectedPrefix && prefix.data == oldPrefix, "CDef inheritance word changes; class/data/header untouched");
    references = {{".CDefs.1.InheritedEntryID", "A"}};
    rejects([&] { forge::fmplinks::relink(prefix, targets, schema, references); }, "component index refuses beyond listing");
    std::printf("FMP metadata and bounded symbolic reference tests: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
