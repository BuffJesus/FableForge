#include "forge/fmplinks.hpp"
#include "forge/defdecode.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <tuple>

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
    for (const auto& path : {"2996266366.Flourish Effect", "2996266366._", "473863218.Flourish Sprite", "473863218._",
                              "1732080037.unknown.0.CWeapon Sprite_Weapon_Trail1",
                              "1732080037.WeaponTrails[AUGMENTATION] = CWeaponTrailGraphicSet(link,link).0._"}) {
        const auto pair = forge::fmplinks::readMetadata(metadataTwo(path));
        check(pair.size() == 2 && pair[0].path == std::string(path) + ".0" &&
              pair[0].target == "FIRST" && pair[1].path == std::string(path) + ".1" &&
              pair[1].target == "SECOND", "weapon multi-target group preserves slot order");
    }
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
    check(!targets.originalNameOffset("B") && targets.savedNameOffset("A") == 15 &&
          targets.savedNameOffset("B") == 21,
          "new entry names receive the same offsets as the save writer");
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

    const auto legacySchema = forge::defschema::Schema::loadText(R"({
        "CShopDef":{"prefix_len":3,"fields":[{"name":"InitialHawkingPhrase","type":"?"}]},
        "CExperienceDef":{"prefix_len":3,"fields":[{"name":"PickupParticleEffectsPerExperiencePoints","type":"Map_JJ"}]},
        "CGlobalDef":{"prefix_len":3,"fields":[{"name":"SwitchGraphic","type":"CEngineGraphic"}]}
    })");
    auto legacyEntry = [](const std::string& definition, const std::string& fieldName,
                          const std::string& fieldType, const Bytes& value) {
        forge::defdecode::Decoded record;
        record.prefix = {1, 0, 0}; record.prefixLen = 3; record.allTagsOk = true;
        forge::defdecode::DecodedField field;
        field.name = fieldName; field.type = fieldType;
        field.tag = forge::defdecode::fieldTag(fieldName); field.tagOk = true;
        field.value = value; record.fields = {field};
        return forge::bin::Entry{definition, "PROBE", 0, forge::defdecode::encode(record)};
    };
    Bytes shopValue; word(shopValue, 77);
    {
        const auto replacementSchema = forge::defschema::Schema::loadText(
            R"({"CTextureReplacementDef":{"prefix_len":3,"fields":[{"name":"Entries","type":"Vector_VCEntry_CTextureReplacementDef__"}]}})");
        Bytes pairs; word(pairs, 2); word(pairs, 77); word(pairs, 78); word(pairs, 79); word(pairs, 80);
        const auto replacement = legacyEntry("CTextureReplacementDef", "Entries",
            "Vector_VCEntry_CTextureReplacementDef__", pairs);
        const std::vector<forge::fmplinks::Link> replacementLinks{
            {"659892607.CTextureReplacementDef_List.0.Texture To Replace", "OLD"},
            {"659892607.CTextureReplacementDef_List.1.Replacement Texture", "NEW"}};
        const std::map<std::string, uint32_t> textureMap{{"OLD", 1001}, {"NEW", 1002}};
        auto expected = replacement.data;
        auto putWord = [&](size_t at, uint32_t value) {
            for (size_t n = 0; n < 4; ++n) expected[at+n] = uint8_t(value >> (8*n));
        };
        putWord(11, 1001); putWord(23, 1002);
        check(forge::fmplinks::relink(replacement, targets, replacementSchema, replacementLinks,
                  false, nullptr, &textureMap) == expected,
              "texture replacement pairs change only selected old/new texture words");
        check(forge::fmplinks::locate(replacement, replacementSchema, replacementLinks[0]).destination ==
                  forge::fmplinks::Destination::TextureEntry, "replacement source uses texture identity");
        for (const auto& path : {"659892607.CTextureReplacementDef_List.2.Texture To Replace",
                                "659892607.CTextureReplacementDef_List.0.unknown"})
            rejects([&] { forge::fmplinks::locate(replacement, replacementSchema, {path, "OLD"}); },
                    "out-of-range and unverified texture replacement paths refuse");
        for (int variant = 0; variant < 3; ++variant) {
            auto malformed = replacement;
            if (variant == 0) malformed.data[7] = 3;
            if (variant == 1) malformed.data.push_back(0);
            if (variant == 2) malformed.data[9] = 1;
            rejects([&] { forge::fmplinks::locate(malformed, replacementSchema, replacementLinks[0]); },
                    "texture replacement counts, trailing bytes and bounds refuse");
        }
        rejects([&] { forge::fmplinks::relink(replacement, targets, replacementSchema,
                          replacementLinks, false, nullptr, nullptr); },
                "texture replacement missing texture map refuses");
    }
    {
        const auto labelSchema = forge::defschema::Schema::loadText(
            R"({"INVENTORY_TYPE":{"prefix_len":3,"fields":[{"name":"InventoryName","type":"uint32"}]}})");
        const auto labelRecord = legacyEntry("INVENTORY_TYPE", "InventoryName", "uint32", shopValue);
        const std::vector<forge::fmplinks::Link> labelLinks{{"3517586038.unknown", "LABEL"}};
        const std::map<std::string, uint32_t> labelText{{"LABEL", 14444}};
        const auto labelLocation = forge::fmplinks::locate(labelRecord, labelSchema, labelLinks[0]);
        check(labelLocation.offset == 7 && labelLocation.destination == forge::fmplinks::Destination::TextEntry,
              "inventory name uses text identity");
        auto expectedLabel = labelRecord.data;
        const uint32_t labelId = 14444;
        std::memcpy(expectedLabel.data() + 7, &labelId, 4);
        check(forge::fmplinks::relink(labelRecord, targets, labelSchema, labelLinks,
              false, nullptr, nullptr, nullptr, &labelText) == expectedLabel,
              "inventory label changes only its text word");
        rejects([&] { forge::fmplinks::relink(labelRecord, targets, labelSchema, labelLinks); },
                "inventory label refuses missing text map");
    }
    for (const auto& [fieldName, fieldType, path, destination] :
         std::vector<std::tuple<std::string, std::string, std::string, forge::fmplinks::Destination>>{
             {"RegionDescription", "uint32", "801675342.Region Description", forge::fmplinks::Destination::TextEntry},
             {"HeroBreathParticle", "int32", "435772141.Area Effects", forge::fmplinks::Destination::EffectEntry}}) {
        const auto regionSchema = forge::defschema::Schema::loadText(
            "{\"REGION\":{\"prefix_len\":3,\"fields\":[{\"name\":\"" +
            fieldName + "\",\"type\":\"" + fieldType + "\"}]}}");
        const auto region = legacyEntry("REGION", fieldName, fieldType, shopValue);
        const auto location = forge::fmplinks::locate(region, regionSchema, {path, "ASSET_PROBE"});
        check(location.offset == 7 && location.destination == destination,
              "region reference uses its explicit asset identity domain");
        const std::map<std::string, uint32_t> assets{{"ASSET_PROBE", 14444}};
        const std::vector<forge::fmplinks::Link> links{{path, "ASSET_PROBE"}};
        const auto linked = forge::fmplinks::relink(region, targets, regionSchema, links, false,
            nullptr, nullptr, destination == forge::fmplinks::Destination::EffectEntry ? &assets : nullptr,
            destination == forge::fmplinks::Destination::TextEntry ? &assets : nullptr);
        auto expected = region.data;
        const uint32_t value = 14444;
        std::memcpy(expected.data() + 7, &value, 4);
        check(linked == expected, "region relinking changes only the asset word");
        rejects([&] { forge::fmplinks::relink(region, targets, regionSchema, links); },
                "region asset references refuse a missing asset map");
    }
    const auto expressionSchema = forge::defschema::Schema::loadText(
        R"({"EXPRESSION":{"prefix_len":3,"fields":[{"name":"SpriteGraphic","type":"int32"}]}})");
    const auto expression = legacyEntry("EXPRESSION", "SpriteGraphic", "int32", shopValue);
    const std::map<std::string, uint32_t> expressionTextures{{"HUD_EXPR_PROBE", 4321}};
    const std::vector<forge::fmplinks::Link> expressionLinks{
        {"222402816.ExpressionHudIconID", "HUD_EXPR_PROBE"}};
    const auto linkedExpression = forge::fmplinks::relink(expression, targets,
        expressionSchema, expressionLinks, false, nullptr, &expressionTextures);
    auto expectedExpression = expression.data;
    const uint32_t expressionIcon = 4321;
    std::memcpy(expectedExpression.data() + 7, &expressionIcon, 4);
    check(linkedExpression == expectedExpression,
          "expression HUD icon changes only its texture ID word");
    rejects([&] { forge::fmplinks::relink(expression, targets, expressionSchema,
        expressionLinks); }, "expression HUD icon requires a texture identity map");
    const auto expressionText = legacyEntry("EXPRESSION", "TextTag", "uint32", shopValue);
    const auto expressionTextSchema = forge::defschema::Schema::loadText(
        R"({"EXPRESSION":{"prefix_len":3,"fields":[{"name":"TextTag","type":"uint32"}]}})");
    const std::map<std::string, uint32_t> expressionTexts{{"TEXT_EXPR_PROBE", 12345}};
    const std::vector<forge::fmplinks::Link> expressionTextLinks{
        {"4192721254.TEXT_EXPRESSION_ID", "TEXT_EXPR_PROBE"}};
    const auto linkedExpressionText = forge::fmplinks::relink(expressionText, targets,
        expressionTextSchema, expressionTextLinks, false, nullptr, nullptr, nullptr, &expressionTexts);
    auto expectedExpressionText = expressionText.data;
    const uint32_t expressionTextId = 12345;
    std::memcpy(expectedExpressionText.data() + 7, &expressionTextId, 4);
    check(linkedExpressionText == expectedExpressionText,
          "expression text tag changes only its text ID word");
    rejects([&] { forge::fmplinks::relink(expressionText, targets, expressionTextSchema,
        expressionTextLinks); }, "expression text tag requires a text identity map");
    const auto shop = legacyEntry("CShopDef", "InitialHawkingPhrase", "?", shopValue);
    const auto hawking = forge::fmplinks::locate(shop, legacySchema,
                                               {"3449087046.HAWKING", "TEXT_PROBE"});
    check(hawking.offset == 7 && hawking.destination == forge::fmplinks::Destination::TextEntry,
          "shop hawking control addresses its four-byte text id");
    for (const auto& [fieldName, path] : std::vector<std::pair<std::string, std::string>>{
            {"ReplyToRequestPhrase", "2474119287.REPLYTOREQUEST"},
            {"CustomerFollowPhrase", "2490160490.FOLLOWME"},
            {"ThankingPhrase", "4019884189.THANKING"}}) {
        const auto phraseSchema = forge::defschema::Schema::loadText(
            "{\"CShopDef\":{\"prefix_len\":3,\"fields\":[{\"name\":\"" +
            fieldName + "\",\"type\":\"?\"}]}}");
        const auto phrase = legacyEntry("CShopDef", fieldName, "?", shopValue);
        const std::map<std::string, uint32_t> phraseTexts{{"TEXT_PHRASE", 15001}};
        const std::vector<forge::fmplinks::Link> phraseLinks{{path, "TEXT_PHRASE"}};
        const auto linked = forge::fmplinks::relink(phrase, targets, phraseSchema,
            phraseLinks, false, nullptr, nullptr, nullptr, &phraseTexts);
        auto expected = phrase.data;
        const uint32_t value = 15001;
        std::memcpy(expected.data() + 7, &value, 4);
        check(linked == expected, "shop phrase changes only the text identity word");
        rejects([&] { forge::fmplinks::relink(phrase, targets, phraseSchema, phraseLinks); },
                "shop phrase requires a text identity map");
    }
    Bytes effectMap; word(effectMap, 2); word(effectMap, 10); word(effectMap, 77);
    const auto suitSchema = forge::defschema::Schema::loadText(
        R"({"CHeroSuitDef":{"prefix_len":3,"fields":[{"name":"SuitParts","type":"Map_JW4EClothingSuitPart__"}]}})");
    Bytes suitParts; word(suitParts, 2);
    word(suitParts, 1); word(suitParts, 77); word(suitParts, 5); word(suitParts, 78);
    const auto suit = legacyEntry("CHeroSuitDef", "SuitParts", "Map_JW4EClothingSuitPart__", suitParts);
    const std::vector<forge::fmplinks::Link> suitLinks{
        {"1104892239.unknown.0.Suit ID", "A"}, {"1104892239.unknown.1.Suit ID", "B"}};
    const auto linkedSuit = forge::fmplinks::relink(suit, targets, suitSchema, suitLinks);
    auto expectedSuit = suit.data;
    const uint32_t firstSuitObject = 0, secondSuitObject = 1;
    std::memcpy(expectedSuit.data() + 15, &firstSuitObject, 4);
    std::memcpy(expectedSuit.data() + 23, &secondSuitObject, 4);
    check(linkedSuit == expectedSuit && suit.data == legacyEntry("CHeroSuitDef", "SuitParts",
          "Map_JW4EClothingSuitPart__", suitParts).data,
          "suit object links preserve clothing-part enum keys and every other byte");
    rejects([&] { forge::fmplinks::locate(suit, suitSchema,
        {"1104892239.unknown.2.Suit ID", "A"}); }, "suit part map refuses an out-of-range index");
    auto badSuitParts = suitParts; badSuitParts[0] = 3;
    const auto badSuit = legacyEntry("CHeroSuitDef", "SuitParts", "Map_JW4EClothingSuitPart__", badSuitParts);
    rejects([&] { forge::fmplinks::locate(badSuit, suitSchema, suitLinks[0]); },
            "suit part map refuses a count extending beyond the payload");
    const auto stockSchema = forge::defschema::Schema::loadText(
        R"({"CShopDef":{"prefix_len":3,"fields":[{"name":"DefaultStock","type":"?"}]}})");
    const auto keeperSchema = forge::defschema::Schema::loadText(
        R"({"CShopDef":{"prefix_len":3,"fields":[{"name":"PreferredShopkeeper","type":"?"}]}})");
    const auto keeper = legacyEntry("CShopDef", "PreferredShopkeeper", "?", shopValue);
    const auto keeperLocation = forge::fmplinks::locate(keeper, keeperSchema,
        {"425186066.ShopKeeperCreatureID", "CREATURE_PROBE"});
    check(keeperLocation.offset == 7 &&
          keeperLocation.destination == forge::fmplinks::Destination::GameEntry,
          "preferred shopkeeper addresses its game identity word");
    Bytes stock; word(stock, 2);
    for (uint32_t row = 0; row < 2; ++row)
        for (uint32_t tag : {0x0da8270bu, 0x03a7ab09u, 0x7afc0513u, 0x34fd91b8u,
                             0x669cc35bu, 0xdc6d0e85u, 0x9e63ec0du, 0x44f5c7a0u}) {
            word(stock, tag); word(stock, 77 + row);
        }
    const auto stockEntry = legacyEntry("CShopDef", "DefaultStock", "?", stock);
    const auto categorySchema = forge::defschema::Schema::loadText(
        R"({"CShopDef":{"prefix_len":3,"fields":[{"name":"StockCategoryDefaults","type":"?"}]}})");
    const auto categoryEntry = legacyEntry("CShopDef", "StockCategoryDefaults", "?", stock);
    const auto categoryLocation = forge::fmplinks::locate(categoryEntry, categorySchema,
        {"2023417370.unknown.1.Unknown", "INVENTORY_CATEGORY_PROBE"});
    check(categoryLocation.offset == 79 &&
          categoryLocation.destination == forge::fmplinks::Destination::GameEntry,
          "stock category default addresses its category identity word");
    const auto stockLink = forge::fmplinks::locate(stockEntry, stockSchema,
        {"3996108785.CShopDefArrayList1.1.ItemForSell", "OBJECT_PROBE"});
    check(stockLink.offset == 79 && stockLink.destination == forge::fmplinks::Destination::GameEntry,
          "shop stock addresses only the object value in the selected tagged row");
    rejects([&] { forge::fmplinks::locate(stockEntry, stockSchema,
        {"3996108785.CShopDefArrayList1.2.ItemForSell", "OBJECT_PROBE"}); },
        "shop stock refuses an out-of-range row");
    auto badStock = stock; badStock[12] ^= 1;
    const auto badStockEntry = legacyEntry("CShopDef", "DefaultStock", "?", badStock);
    rejects([&] { forge::fmplinks::locate(badStockEntry, stockSchema,
        {"3996108785.CShopDefArrayList1.0.ItemForSell", "OBJECT_PROBE"}); },
        "shop stock refuses an unexpected member tag");
    auto oversizedStock = stock; oversizedStock[0] = 3;
    const auto oversizedStockEntry = legacyEntry("CShopDef", "DefaultStock", "?", oversizedStock);
    rejects([&] { forge::fmplinks::locate(oversizedStockEntry, stockSchema,
        {"3996108785.CShopDefArrayList1.0.ItemForSell", "OBJECT_PROBE"}); },
        "shop stock refuses a count extending beyond its payload");
    word(effectMap, 20); word(effectMap, 78);
    const auto experience = legacyEntry("CExperienceDef",
        "PickupParticleEffectsPerExperiencePoints", "Map_JJ", effectMap);
    const auto effect = forge::fmplinks::locate(experience, legacySchema,
                                               {"495036219.unknown.1.unknown", "EFFECT_PROBE"});
    check(effect.offset == 23 && effect.destination == forge::fmplinks::Destination::EffectEntry,
          "experience effect map addresses the value after its threshold");
    rejects([&] { forge::fmplinks::locate(experience, legacySchema,
                  {"495036219.unknown.2.unknown", "EFFECT_PROBE"}); },
            "experience effect map refuses an out-of-range index");
    Bytes switchValue; word(switchValue, 5); word(switchValue, 77);
    switchValue.insert(switchValue.end(), 9, 0);
    const auto global = legacyEntry("GLOBAL", "SwitchGraphic", "CEngineGraphic", switchValue);
    const auto switchMesh = forge::fmplinks::locate(global, legacySchema,
                                                  {"1020994168.unknown", "MESH_PROBE"});
    check(switchMesh.offset == 11 && switchMesh.destination == forge::fmplinks::Destination::MeshEntry,
          "global switch graphic addresses only its model id");

    const auto weaponSchema = forge::defschema::Schema::loadText(R"({"CWeaponDef":{
        "prefix_len":3,"fields":[
            {"name":"FlourishParticles","type":"?"},
            {"name":"FlourishSpecialWeaponTrail","type":"?"},
            {"name":"WeaponTrails","type":"?"},
            {"name":"Explosion","type":"?"}]}})");
    forge::defdecode::Decoded weaponDecoded;
    weaponDecoded.prefix = {1, 0, 0}; weaponDecoded.prefixLen = 3;
    weaponDecoded.allTagsOk = true;
    forge::defdecode::DecodedField flourish, sprite, trails, explosion;
    auto setField = [](forge::defdecode::DecodedField& field,
                       const std::string& name) {
        field.name = name; field.type = "?";
        field.tag = forge::defdecode::fieldTag(name); field.tagOk = true;
    };
    setField(flourish, "FlourishParticles");
    for (uint32_t value : {0, 77, 78, 0, 0, 1, 31, 1, 32}) word(flourish.value, value);
    setField(sprite, "FlourishSpecialWeaponTrail");
    word(sprite.value, 11); word(sprite.value, 12);
    setField(trails, "WeaponTrails");
    for (uint32_t value : {2, 0, 10, 12, 1, 13, 14}) word(trails.value, value);
    setField(explosion, "Explosion"); word(explosion.value, 79);
    weaponDecoded.fields = {flourish, sprite, trails, explosion};
    forge::bin::Entry weapon{"CWeaponDef", "WEAPON_PROBE", 0,
                             forge::defdecode::encode(weaponDecoded)};
    auto locateWeapon = [&](const std::string& path) {
        return forge::fmplinks::locate(weapon, weaponSchema, {path, "TARGET"});
    };
    const auto flourish0 = locateWeapon("2996266366.Flourish Effect.0");
    const auto flourish1 = locateWeapon("2996266366.Flourish Effect.1");
    check(locateWeapon("2996266366._.0").offset == flourish0.offset &&
          locateWeapon("2996266366._.1").offset == flourish1.offset &&
          locateWeapon("2996266366._.0").destination == forge::fmplinks::Destination::EffectEntry,
          "legacy underscore flourish name preserves exact effect slots");
    check(flourish0.offset + 4 == flourish1.offset &&
          flourish0.destination == forge::fmplinks::Destination::EffectEntry &&
          flourish1.destination == forge::fmplinks::Destination::EffectEntry,
          "flourish effects address consecutive effect words");
    const auto firstSlot = locateWeapon(
        "2996266366.Weapon Augmentation Slots List 1.0.Weapon Augmentation Slot Name 1");
    const auto secondSlot = locateWeapon(
        "2996266366.Weapon Augmentation Slots List 2.0.Weapon Augmentation Slot Name 2");
    check(firstSlot.offset == 31 && secondSlot.offset == 39 &&
          firstSlot.destination == forge::fmplinks::Destination::NameOffset &&
          secondSlot.destination == forge::fmplinks::Destination::NameOffset,
          "both flourish name lists address their own saved name-offset words");
    const auto sprite0 = locateWeapon("473863218.Flourish Sprite.0");
    const auto sprite1 = locateWeapon("473863218.Flourish Sprite.1");
    check(locateWeapon("473863218._.0").offset == sprite0.offset &&
          locateWeapon("473863218._.1").offset == sprite1.offset &&
          locateWeapon("473863218._.0").destination == forge::fmplinks::Destination::TextureEntry,
          "legacy underscore sprite name preserves both texture slots");
    check(sprite0.offset + 4 == sprite1.offset &&
          sprite0.destination == forge::fmplinks::Destination::TextureEntry &&
          sprite1.destination == forge::fmplinks::Destination::TextureEntry,
          "flourish sprites address consecutive texture words");
    {
        const std::vector<forge::fmplinks::Link> oldNames{
            {"2996266366._.0", "FX_A"}, {"2996266366._.1", "FX_B"},
            {"473863218._.0", "TEX_A"}, {"473863218._.1", "TEX_B"}};
        const std::map<std::string, uint32_t> effectMap{{"FX_A", 30001}, {"FX_B", 30002}};
        const std::map<std::string, uint32_t> textureMap{{"TEX_A", 40001}, {"TEX_B", 40002}};
        auto expected = weapon.data;
        for (const auto& [at, value] : std::vector<std::pair<size_t, uint32_t>>{
                 {flourish0.offset, 30001}, {flourish1.offset, 30002},
                 {sprite0.offset, 40001}, {sprite1.offset, 40002}})
            std::memcpy(expected.data() + at, &value, 4);
        check(forge::fmplinks::relink(weapon, targets, weaponSchema, oldNames,
                  false, nullptr, &textureMap, &effectMap) == expected,
              "underscore aliases change only four identified asset words");
        rejects([&] { locateWeapon("2996266366._.2"); }, "underscore effect alias rejects third slot");
        rejects([&] { locateWeapon("473863218._.2"); }, "underscore sprite alias rejects third slot");
    }
    const auto trail0 = locateWeapon("1732080037.unknown.1.CWeapon Sprite_Weapon_Trail1.0");
    const auto trail1 = locateWeapon("1732080037.unknown.1.CWeapon Sprite_Weapon_Trail1.1");
    {
        const std::string alias = "1732080037.WeaponTrails[AUGMENTATION] = CWeaponTrailGraphicSet(link,link).1._";
        const std::vector<forge::fmplinks::Link> aliasLinks{{alias + ".0", "A"}, {alias + ".1", "B"}};
        const std::map<std::string, uint32_t> textures{{"A", 50001}, {"B", 50002}};
        auto expected = weapon.data;
        const uint32_t a = 50001, b = 50002;
        std::memcpy(expected.data() + trail0.offset, &a, 4);
        std::memcpy(expected.data() + trail1.offset, &b, 4);
        check(forge::fmplinks::relink(weapon, targets, weaponSchema, aliasLinks,
                  false, nullptr, &textures) == expected,
              "legacy trail names preserve keys and all other bytes");
        rejects([&] { locateWeapon(alias + ".2"); }, "legacy trail rejects third texture slot");
        rejects([&] { locateWeapon("1732080037.WeaponTrails[AUGMENTATION] = CWeaponTrailGraphicSet(link,link).2._.0"); },
                "legacy trail rejects out-of-range row");
    }
    check(trail0.offset + 4 == trail1.offset &&
          trail0.destination == forge::fmplinks::Destination::TextureEntry &&
          trail1.destination == forge::fmplinks::Destination::TextureEntry,
          "indexed weapon trail addresses both texture words in the selected row");
    const auto explosionLink = locateWeapon("301428501.unknown");
    check(explosionLink.destination == forge::fmplinks::Destination::GameEntry,
          "weapon explosion names a game definition");
    rejects([&] { locateWeapon("2996266366.Flourish Effect.2"); },
            "third flourish effect slot refuses");
    rejects([&] { locateWeapon(
                  "2996266366.Weapon Augmentation Slots List 1.1.Weapon Augmentation Slot Name 1"); },
            "flourish name-list index outside count refuses");
    rejects([&] { locateWeapon("1732080037.unknown.2.CWeapon Sprite_Weapon_Trail1.0"); },
            "weapon trail index outside count refuses");
    auto malformedWeapon = weapon;
    const auto trailTag = forge::defdecode::fieldTag("WeaponTrails");
    for (size_t at = 0; at + 4 <= malformedWeapon.data.size(); ++at) {
        uint32_t found = 0;
        std::memcpy(&found, malformedWeapon.data.data() + at, 4);
        if (found == trailTag) {
            malformedWeapon.data[at + 4] = 3;
            break;
        }
    }
    rejects([&] { forge::fmplinks::locate(malformedWeapon, weaponSchema,
                  {"1732080037.unknown.0.CWeapon Sprite_Weapon_Trail1.0", "TARGET"}); },
            "weapon trail count inconsistent with payload refuses");

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

    const auto heroSchema = forge::defschema::Schema::loadText(
        R"({"HERO_ABILITY":{"prefix_len":3,"fields":[{"name":"Name","type":"?"},{"name":"LevDescription","type":"?"}]}})");
    forge::defdecode::Decoded heroDecoded;
    heroDecoded.prefix = {1, 0, 0}; heroDecoded.prefixLen = 3; heroDecoded.allTagsOk = true;
    forge::defdecode::DecodedField heroName;
    heroName.name = "Name"; heroName.type = "?";
    heroName.tag = forge::defdecode::fieldTag(heroName.name); heroName.tagOk = true;
    word(heroName.value, 1);
    forge::defdecode::DecodedField heroLevels;
    heroLevels.name = "LevDescription"; heroLevels.type = "?";
    heroLevels.tag = forge::defdecode::fieldTag(heroLevels.name); heroLevels.tagOk = true;
    word(heroLevels.value, 2); word(heroLevels.value, 3); word(heroLevels.value, 4);
    heroDecoded.fields = {heroName, heroLevels};
    forge::bin::Entry heroAbility{"HERO_ABILITY", "ABILITY_PROBE", 0,
                                  forge::defdecode::encode(heroDecoded)};
    const auto originalHero = heroAbility.data;
    const std::map<std::string, uint32_t> textIds{{"TEXT_NAME", 10677}, {"TEXT_LEVEL", 15106}};
    references = {{"604919263.GUIDescription1ID", "TEXT_NAME"},
                  {"2555713587.HAtextArray.1.Hero Ability Text Description", "TEXT_LEVEL"}};
    const auto linkedHero = forge::fmplinks::relink(heroAbility, targets, heroSchema, references,
                                                    false, nullptr, nullptr, nullptr, &textIds);
    auto expectedHero = originalHero;
    const auto nameSlot = forge::fmplinks::locate(heroAbility, heroSchema, references[0]);
    const auto levelSlot = forge::fmplinks::locate(heroAbility, heroSchema, references[1]);
    const uint32_t nameId = 10677, levelId = 15106;
    std::memcpy(expectedHero.data() + nameSlot.offset, &nameId, 4);
    std::memcpy(expectedHero.data() + levelSlot.offset, &levelId, 4);
    check(linkedHero == expectedHero && heroAbility.data == originalHero,
          "hero ability text links rewrite exactly the two addressed IDs");
    auto malformedHero = heroAbility;
    malformedHero.data[levelSlot.offset - 8] = 3;
    rejects([&] { forge::fmplinks::relink(malformedHero, targets, heroSchema, references,
                                         false, nullptr, nullptr, nullptr, &textIds); },
            "hero ability text count must match the bounded field");
    references = {{"2555713587.HAtextArray.2.Hero Ability Text Description", "TEXT_LEVEL"}};
    rejects([&] { forge::fmplinks::relink(heroAbility, targets, heroSchema, references,
                                         false, nullptr, nullptr, nullptr, &textIds); },
            "hero ability text index outside the array refuses");
    references = {{"604919263.GUIDescription1ID", "MISSING_TEXT"}};
    rejects([&] { forge::fmplinks::relink(heroAbility, targets, heroSchema, references,
                                         false, nullptr, nullptr, nullptr, &textIds); },
            "missing text target refuses");

    const auto inventorySchema = forge::defschema::Schema::loadText(
        R"({"CInventoryItemDef":{"prefix_len":3,"fields":[{"name":"Graphic","type":"CEngineGraphic"},{"name":"ItemDescription","type":"uint32"},{"name":"ItemDetails","type":"uint32"}]}})");
    forge::defdecode::Decoded inventoryDecoded;
    inventoryDecoded.prefix = {1, 0, 0}; inventoryDecoded.prefixLen = 3;
    inventoryDecoded.allTagsOk = true;
    auto inventoryField = [](const char* name, const char* type, std::vector<uint8_t> value) {
        forge::defdecode::DecodedField field;
        field.name = name; field.type = type; field.value = std::move(value);
        field.tag = forge::defdecode::fieldTag(name); field.tagOk = true;
        return field;
    };
    std::vector<uint8_t> inventoryGraphic;
    word(inventoryGraphic, 1); word(inventoryGraphic, 7);
    word(inventoryGraphic, 0x3f800000); word(inventoryGraphic, 0x3f000000);
    inventoryGraphic.push_back(0x7f);
    std::vector<uint8_t> inventoryTitle, inventoryDetails;
    word(inventoryTitle, 8); word(inventoryDetails, 9);
    inventoryDecoded.fields = {inventoryField("Graphic", "CEngineGraphic", inventoryGraphic),
                               inventoryField("ItemDescription", "uint32", inventoryTitle),
                               inventoryField("ItemDetails", "uint32", inventoryDetails)};
    forge::bin::Entry inventory{"CInventoryItemDef", "INVENTORY_PROBE", 0,
                                forge::defdecode::encode(inventoryDecoded)};
    const auto originalInventory = inventory.data;
    const std::map<std::string, uint32_t> inventoryTextures{{"INVENTORY_ICON", 5665}};
    const std::map<std::string, uint32_t> inventoryTexts{{"ITEM_TITLE", 15100}, {"ITEM_DETAILS", 15101}};
    references = {{"3361958702.Inventory HUD Icon", "INVENTORY_ICON"},
                  {"4197094506.TEXT_TITLE", "ITEM_TITLE"},
                  {"1637763631.TEXT_DESCRIPTION", "ITEM_DETAILS"}};
    const auto linkedInventory = forge::fmplinks::relink(inventory, targets, inventorySchema,
        references, false, nullptr, &inventoryTextures, nullptr, &inventoryTexts);
    auto expectedInventory = originalInventory;
    for (const auto& [at, value] : std::vector<std::pair<size_t, uint32_t>>{{11, 5665}, {28, 15100}, {36, 15101}})
        std::memcpy(expectedInventory.data() + at, &value, 4);
    check(linkedInventory == expectedInventory && inventory.data == originalInventory,
          "inventory icon and title/details use asset IDs without changing graphic kind/tail");
    rejects([&] { forge::fmplinks::relink(inventory, targets, inventorySchema,
        references, false, nullptr, nullptr, nullptr, &inventoryTexts); },
        "inventory texture cannot fall back to a game record");
    rejects([&] { forge::fmplinks::relink(inventory, targets, inventorySchema,
        references, false, nullptr, &inventoryTextures); },
        "inventory title/details require matching text identities");

    const auto augmentSchema = forge::defschema::Schema::loadText(
        R"({"CAugmentationDef":{"prefix_len":3,"fields":[{"name":"Name","type":"?"},{"name":"Type","type":"?"},{"name":"InventoryGraphic","type":"?"}]}})");
    forge::defdecode::Decoded augmentDecoded;
    augmentDecoded.prefix = {1, 0, 1}; augmentDecoded.prefixLen = 3;
    augmentDecoded.allTagsOk = true;
    std::vector<uint8_t> augmentName, augmentType, augmentGraphic;
    word(augmentName, 77); word(augmentType, 0x200); word(augmentGraphic, 88);
    augmentDecoded.fields = {inventoryField("Name", "?", augmentName),
                            inventoryField("Type", "?", augmentType),
                            inventoryField("InventoryGraphic", "?", augmentGraphic)};
    forge::bin::Entry augment{"CAugmentationDef", "AUGMENT_PROBE", 0,
                             forge::defdecode::encode(augmentDecoded)};
    references = {{"604919263.GUIDescription1ID", "ITEM_TITLE"},
                  {"3069668529.HUD Icon Texture", "INVENTORY_ICON"}};
    const auto linkedAugment = forge::fmplinks::relink(augment, targets, augmentSchema,
        references, false, nullptr, &inventoryTextures, nullptr, &inventoryTexts);
    auto expectedAugment = augment.data;
    const uint32_t augmentText = 15100, augmentTexture = 5665;
    std::memcpy(expectedAugment.data() + 7, &augmentText, 4);
    std::memcpy(expectedAugment.data() + 23, &augmentTexture, 4);
    check(linkedAugment == expectedAugment, "augmentation text/icon rewrite exact scalars and preserve type");
    rejects([&] { forge::fmplinks::relink(augment, targets, augmentSchema,
        references, false, nullptr, nullptr, nullptr, &inventoryTexts); },
        "augmentation icon requires texture identities");
    rejects([&] { forge::fmplinks::relink(augment, targets, augmentSchema,
        references, false, nullptr, &inventoryTextures); },
        "augmentation name requires text identities");
    rejects([&] { forge::fmplinks::locate(augment, augmentSchema,
        {"604919263.Unverified Name", "ITEM_TITLE"}); },
        "augmentation control requires exact legacy path");

    const auto reactionSchema = forge::defschema::Schema::loadText(
        R"({"OPINION_DEED_EFFECTS":{"prefix_len":3,"fields":[{"name":"Reaction","type":"vector<COpinionDeedReactionDef>"}]}})");
    std::vector<uint8_t> reactionValue;
    word(reactionValue, 2);
    for (const uint32_t attitudes : {2u, 0u}) {
        word(reactionValue, forge::defdecode::fieldTag("Attitudes"));
        word(reactionValue, attitudes);
        for (uint32_t n = 0; n < attitudes; ++n) word(reactionValue, n + 3);
        word(reactionValue, forge::defdecode::fieldTag("Animation")); word(reactionValue, 77);
        word(reactionValue, forge::defdecode::fieldTag("DialogueTag")); word(reactionValue, 88);
    }
    forge::defdecode::Decoded reactionDecoded;
    reactionDecoded.prefix = {1, 0, 0}; reactionDecoded.prefixLen = 3;
    reactionDecoded.allTagsOk = true;
    reactionDecoded.fields = {inventoryField("Reaction", "vector<COpinionDeedReactionDef>", reactionValue)};
    forge::bin::Entry reaction{"OPINION_DEED_EFFECTS", "REACTION_PROBE", 0,
                              forge::defdecode::encode(reactionDecoded)};
    references = {{"3367666488.unknown.0.Animation", "B"},
                  {"3367666488.unknown.1.Text", "ITEM_TITLE"}};
    const auto reactionAnimation = forge::fmplinks::locate(reaction, reactionSchema, references[0]);
    const auto reactionText = forge::fmplinks::locate(reaction, reactionSchema, references[1]);
    check(reactionAnimation.offset == 31 && reactionText.offset == 63 &&
          reactionAnimation.destination == forge::fmplinks::Destination::NameOffset &&
          reactionText.destination == forge::fmplinks::Destination::TextEntry,
          "opinion reaction references retain domains across variable attitude counts");
    auto expectedReaction = reaction.data;
    const uint32_t reactionName = *targets.savedNameOffset("B");
    std::memcpy(expectedReaction.data() + 31, &reactionName, 4);
    std::memcpy(expectedReaction.data() + 63, &augmentText, 4);
    check(forge::fmplinks::relink(reaction, targets, reactionSchema, references,
          false, nullptr, nullptr, nullptr, &inventoryTexts) == expectedReaction,
          "opinion reaction relinks only selected animation/text words");
    auto badReaction = reaction;
    badReaction.data[15] = 0xff;
    rejects([&] { forge::fmplinks::locate(badReaction, reactionSchema, references[0]); },
        "oversized nested reaction attitude count refuses");
    badReaction = reaction; badReaction.data[59] ^= 1;
    rejects([&] { forge::fmplinks::locate(badReaction, reactionSchema, references[0]); },
        "malformed later reaction tag refuses even for an earlier selection");
    rejects([&] { forge::fmplinks::locate(reaction, reactionSchema,
        {"3367666488.unknown.2.Text", "ITEM_TITLE"}); },
        "opinion reaction index outside the vector refuses");
    rejects([&] { forge::fmplinks::relink(reaction, targets, reactionSchema, references); },
        "opinion reaction dialogue requires text identities");

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
