#include "forge/defrelocate.hpp"
#include "forge/defdecode.hpp"
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace {
using Bytes = std::vector<uint8_t>;
void word(Bytes& out, uint32_t value) {
    for (int i = 0; i < 4; ++i) out.push_back(uint8_t(value >> (8 * i)));
}
void field(Bytes& out, const char* name, std::initializer_list<uint32_t> values) {
    word(out, forge::defdecode::fieldTag(name));
    for (auto value : values) word(out, value);
}
Bytes fixture(bool moved) {
    Bytes out{1, 0, 1, 1, 0};
    word(out, 77); // CDefName CRC deliberately equals a moved ID.
    word(out, moved ? 88 : 77);
    word(out, moved ? 99 : 88);
    field(out, "Index", {moved ? 88u : 77u});
    field(out, "Name", {moved ? 900u : 77u});
    field(out, "Indices", {3, moved ? 88u : 77u, moved ? 99u : 88u, UINT32_MAX});
    field(out, "Names", {2, moved ? 900u : 77u, UINT32_MAX});
    field(out, "Float", {77}); // A float's raw bits must stay unchanged.
    field(out, "Other", {12345});
    return out;
}
std::string hex(const Bytes& data) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    for (auto byte : data) { out += digits[byte >> 4]; out += digits[byte & 15]; }
    return out;
}

// Optional read-only corpus audit, kept separate from the synthetic CTest run.
// Arguments: root schema gameFrom gameTo nameFrom nameTo.
int audit(char** argv) {
    using namespace forge;
    const std::filesystem::path folder = std::filesystem::path(argv[1]) / "data" / "CompiledDefs";
    const auto schema = defschema::Schema::load(argv[2]);
    auto number = [](const char* text) {
        size_t end = 0;
        const auto value = std::stoull(text, &end);
        if (text[end] || value > UINT32_MAX) throw std::runtime_error("invalid audit ID");
        return uint32_t(value);
    };
    const defrelocate::Maps maps{{{number(argv[3]), number(argv[4])}}, {{number(argv[5]), number(argv[6])}}};
    nlohmann::json report{{"root", argv[1]}, {"records", nlohmann::json::array()},
                          {"entries", 0}, {"rewrites", 0}, {"unresolved", 0}};
    for (const auto* bank : {"game.bin", "script.bin", "frontend.bin"}) {
        const auto source = bin::File::open(folder / "names.bin", folder / bank);
        report["entries"] = report["entries"].get<size_t>() + source.entries().size();
        for (size_t i = 0; i < source.entries().size(); ++i) {
            const auto& entry = source.entries()[i];
            const auto analysis = defrelocate::analyze(entry, schema, maps);
            if (analysis.rewrites.empty() && analysis.unresolved.empty()) continue;
            nlohmann::json row{{"bank", bank}, {"index", i}, {"name", entry.name},
                {"rewrites", nlohmann::json::array()}, {"unresolved", nlohmann::json::array()}};
            for (const auto& rewrite : analysis.rewrites)
                row["rewrites"].push_back({{"offset", rewrite.offset}, {"before", rewrite.before},
                    {"after", rewrite.after}, {"path", rewrite.path},
                    {"domain", rewrite.domain == defrelocate::Domain::GameEntry ? "game" : "name"}});
            for (const auto& candidate : analysis.unresolved)
                row["unresolved"].push_back({{"offset", candidate.offset}, {"value", candidate.value}});
            if (analysis.unresolved.empty()) {
                row["before_hex"] = hex(entry.data);
                row["after_hex"] = hex(defrelocate::relocate(entry, schema, maps));
            }
            report["rewrites"] = report["rewrites"].get<size_t>() + analysis.rewrites.size();
            report["unresolved"] = report["unresolved"].get<size_t>() + analysis.unresolved.size();
            report["records"].push_back(std::move(row));
        }
    }
    std::cout << report.dump(2) << '\n';
    return report["unresolved"].get<size_t>() ? 1 : 0;
}
}

int main(int argc, char** argv) {
    if (argc == 7) {
        try { return audit(argv); }
        catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
    }
    if (argc != 1) { std::fprintf(stderr, "Expected root schema gameFrom gameTo nameFrom nameTo\n"); return 2; }
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) { ++failures; std::fprintf(stderr, "definition relocation: %s\n", what); }
    };
    auto rejects = [&](auto action, const char* what) {
        try { action(); check(false, what); } catch (const std::runtime_error&) {}
    };
    using namespace forge;
    const auto schema = defschema::Schema::loadText(R"({
        "OBJECT":{"fields":[
            {"name":"Index","type":"CDefIndex"},
            {"name":"Name","type":"CDefString"},
            {"name":"Indices","type":"Vector_VCDefIndex__"},
            {"name":"Names","type":"vector<CDefString>"},
            {"name":"Float","type":"float"},
            {"name":"Other","type":"int32"}]},
        "OPAQUE":{"prefix_len":17,"fields":[]},
        "VECTOR":{"prefix_len":3,"fields":[{"name":"Array","type":"Vector_VCDefIndex__"}]},
        "SCALAR":{"prefix_len":3,"fields":[{"name":"Index","type":"CDefIndex"}]}
    })");
    const defrelocate::Maps maps{{{77, 88}, {88, 99}}, {{77, 900}}};
    const bin::Entry entry{"OBJECT", "TEST", 0, fixture(false)};
    const auto analysis = defrelocate::analyze(entry, schema, maps);
    check(analysis.unresolved.empty() && analysis.rewrites.size() == 7,
          "component and typed scalar/vector references discovered in their domains");
    check(defrelocate::relocate(entry, schema, maps) == fixture(true),
          "simultaneous relocation preserves CRCs, floats, tags, nulls and non-reference words");
    check(entry.data == fixture(false), "source payload remains unchanged");
    const auto ownerSchema = defschema::Schema::loadText(R"({
        "BUILDING":{"fields":[{"name":"Value","type":"int32"}]},
        "CThingBuildingDef":{"fields":[{"name":"Value","type":"int32"}]},
        "HERO_ABILITY":{"fields":[{"name":"DummyObject","type":"?"},{"name":"Name","type":"?"}]},
        "CHeroAbilityDef":{"fields":[{"name":"DummyObject","type":"?"},{"name":"Name","type":"?"}]},
        "UNVERIFIED":{"fields":[{"name":"DummyObject","type":"?"},{"name":"Name","type":"?"}]}
    })");
    for (const auto* owner : {"BUILDING", "CThingBuildingDef"}) {
        auto bytes = fixture(false); bytes.resize(17); field(bytes, "Value", {12345});
        auto expected = fixture(true); expected.resize(17); field(expected, "Value", {12345});
        bin::Entry building{owner, "BUILDING_PROBE", 0, bytes};
        check(defrelocate::relocate(building, ownerSchema, maps) == expected,
              "building component data/inheritance move while CRC/header/trailing scalar stay exact");
        building.data[3] = 2;
        rejects([&] { defrelocate::relocate(building, ownerSchema, maps); },
                "building listing with wrong count refuses");
    }
    for (const auto* owner : {"HERO_ABILITY", "CHeroAbilityDef"}) {
        bin::Entry hero{owner, "ABILITY_PROBE", 0, {1,0,1,0,0}};
        field(hero.data, "DummyObject", {77}); field(hero.data, "Name", {12345});
        auto expected = hero.data; expected[9] = 88;
        check(defrelocate::relocate(hero, ownerSchema, maps) == expected,
              "verified hero DummyObject moves as game index despite unknown schema storage type");
        const defrelocate::Maps namesOnly{{},{{77,900}}};
        check(defrelocate::relocate(hero, ownerSchema, namesOnly) == hero.data,
              "hero DummyObject never treated as a name offset");
        hero.data.resize(13); field(hero.data, "Name", {77});
        rejects([&] { defrelocate::relocate(hero, ownerSchema, maps); },
                "hero unrelated unknown scalar remains unresolved");
        hero.data = {1,0,1,0,0}; field(hero.data, "DummyObject", {77,88});
        field(hero.data, "Name", {12345});
        rejects([&] { defrelocate::relocate(hero, ownerSchema, maps); },
                "hero DummyObject incorrect width refuses");
        hero.definition = "UNVERIFIED";
        hero.data = {1,0,1,0,0}; field(hero.data, "DummyObject", {77});
        field(hero.data, "Name", {12345});
        rejects([&] { defrelocate::relocate(hero, ownerSchema, maps); },
                "DummyObject field name alone does not establish reference semantics");
    }
    const auto heroTextSchema = defschema::Schema::loadText(R"({
        "HERO_ABILITY":{"fields":[{"name":"LevDescription","type":"?"}]}
    })");
    bin::Entry descriptions{"HERO_ABILITY", "TEXT_IDS", 0, {1,0,1,0,0}};
    field(descriptions.data, "LevDescription", {3,77,88,UINT32_MAX});
    check(defrelocate::relocate(descriptions, heroTextSchema, maps) == descriptions.data,
          "hero text IDs that collide with moved game/name IDs stay unchanged");
    descriptions.data = {1,0,1,0,0}; field(descriptions.data, "LevDescription", {3,77});
    rejects([&] { defrelocate::relocate(descriptions, heroTextSchema, maps); },
            "truncated text vector cannot certify reference domain");
    const auto brainSchema = defschema::Schema::loadText(R"({
        "CBrainDef":{"fields":[{"name":"Behaviours","type":"Vector_VCBrainBehaviour__"}]}
    })");
    bin::Entry brain{"BRAIN", "BRAIN_PROBE", 0, {1,0,1,0,0}};
    field(brain.data, "Behaviours", {2,77,60,1,88,48,1});
    auto brainExpected = brain.data;
    brainExpected[13] = uint8_t(900); brainExpected[14] = uint8_t(900 >> 8);
    check(defrelocate::relocate(brain, brainSchema, maps) == brainExpected,
          "nested brain names relocate independently of numeric game IDs and priority fields");
    const defrelocate::Maps crossBoundary{{{15360,20000}}, {}};
    check(defrelocate::relocate(brain, brainSchema, crossBoundary) == brain.data,
          "unaligned match crossing known name-reference boundary is not rewritten");
    brain.data = {1,0,1,0,0}; field(brain.data, "Behaviours", {1,77,88,1});
    rejects([&] { defrelocate::relocate(brain, brainSchema, maps); },
            "aligned brain parameter candidate is not inferred to be a reference");
    brain.data = {1,0,1,0,0}; field(brain.data, "Behaviours", {2,77,60,1});
    rejects([&] { defrelocate::relocate(brain, brainSchema, maps); },
            "truncated brain row array refuses");
    for (const auto* fieldName : {"DefaultStock", "StockCategoryDefaults"}) {
        const auto shopSchema = defschema::Schema::loadText(std::string(R"({
            "CShopDef":{"fields":[{"name":")") + fieldName + R"(","type":"?"}]}
        })");
        bin::Entry shop{"CShopDef", "SHOP_PROBE", 0, {1,0,1,0,0}};
        word(shop.data, defdecode::fieldTag(fieldName)); word(shop.data, 2);
        constexpr uint32_t tags[] = {0x0da8270b,0x03a7ab09,0x7afc0513,0x34fd91b8,
                                     0x669cc35b,0xdc6d0e85,0x9e63ec0d,0x44f5c7a0};
        for (size_t row = 0; row < 2; ++row)
            for (size_t member = 0; member < 8; ++member) {
                word(shop.data, tags[member]); word(shop.data, member ? 1000 + member : (row ? 88 : 77));
            }
        auto expected = shop.data; expected[17] = 88; expected[81] = 99;
        check(defrelocate::relocate(shop, shopSchema, maps) == expected,
              "both tagged shop stock layouts relocate objects with all remaining bytes exact");
        const defrelocate::Maps namesOnly{{},{{77,900}}};
        check(defrelocate::relocate(shop, shopSchema, namesOnly) == shop.data,
              "shop object identities are not names.bin offsets");
        auto malformedShop = shop; malformedShop.data[21] ^= 1;
        rejects([&] { defrelocate::relocate(malformedShop, shopSchema, maps); },
                "wrong shop row tag refuses before relocation");
        malformedShop = shop; malformedShop.data.pop_back();
        rejects([&] { defrelocate::relocate(malformedShop, shopSchema, maps); },
                "truncated shop row refuses before relocation");
        malformedShop = shop;
        for (size_t i = 0; i < 4; ++i) malformedShop.data[25+i] = uint8_t(77u >> (8*i));
        rejects([&] { defrelocate::relocate(malformedShop, shopSchema, maps); },
                "unclassified shop parameter is not guessed to be an object reference");
    }
    const auto controlSchema = defschema::Schema::loadText(R"({
        "CControlsDef":{"fields":[{"name":"Controls","type":"Vector_VCActionInputControl__"},
                                    {"name":"Index","type":"CDefIndex"}]}
    })");
    bin::Entry controls{"CONTROL_SCHEME","CONTROLS_PROBE",0,{1,1,1,0,0}};
    field(controls.data,"Controls",{1,77,88,77,88,77,88,77});
    field(controls.data,"Index",{77});
    auto controlExpected = controls.data;
    controlExpected[controlExpected.size()-4] = 88;
    check(defrelocate::relocate(controls,controlSchema,maps) == controlExpected,
          "input enum/direction words preserve colliding IDs while a real reference still relocates");
    auto badControls = controls; badControls.data[9] = 2;
    rejects([&] { defrelocate::relocate(badControls,controlSchema,maps); },
            "input control count must match the full 28-byte row array");
    badControls = controls; badControls.data[5] ^= 1;
    rejects([&] { defrelocate::relocate(badControls,controlSchema,maps); },
            "control category alias cannot bypass native tag verification");
    for (const auto* container : {"Vector_J","Vector_K","Vector_int32","Vector_uint32",
                                  "Vector_float","Map_JJ","Map_KK","Map_JK","Map_KJ"}) {
        const auto containerSchema = defschema::Schema::loadText(std::string(R"({
            "PROBE":{"fields":[{"name":"Array","type":")") + container + R"("},
                {"name":"Index","type":"CDefIndex"}]}})");
        const bool isMap = std::string_view(container).starts_with("Map_");
        bin::Entry probe{"PROBE","BOUNDARIES",0,{1,0,1}};
        word(probe.data,defdecode::fieldTag("Array")); word(probe.data,isMap ? 1 : 2);
        word(probe.data,1); word(probe.data,60); field(probe.data,"Index",{77});
        const defrelocate::Maps boundaryMaps{{{15360,20000},{77,88}}, {}};
        auto expected = probe.data; expected[expected.size()-4] = 88;
        check(defrelocate::relocate(probe,containerSchema,boundaryMaps) == expected,
              "primitive container boundaries reject crossed-byte matches while typed references relocate");
        auto aligned = probe;
        for (size_t i = 0; i < 4; ++i) aligned.data[11+i] = uint8_t(15360u >> (8*i));
        if (std::string_view(container) == "Vector_float") {
            auto floatExpected = aligned.data; floatExpected[floatExpected.size()-4] = 88;
            check(defrelocate::relocate(aligned,containerSchema,boundaryMaps) == floatExpected,
                  "float vector bits are preserved even when equal to moved IDs");
        } else rejects([&] { defrelocate::relocate(aligned,containerSchema,boundaryMaps); },
                       "aligned integer container matches still require reference evidence");
        auto truncated = probe; truncated.data[7] = 3;
        rejects([&] { defrelocate::relocate(truncated,containerSchema,boundaryMaps); },
                "invalid primitive container count refuses before boundary classification");
    }
    const defrelocate::Maps onlyNames{{}, {{77, 900}}};
    check(defrelocate::analyze(entry, schema, onlyNames).rewrites.size() == 2,
          "equal game IDs do not become names.bin offsets");
    const defrelocate::Maps onlyGame{{{77, 88}, {88, 99}}, {}};
    check(defrelocate::analyze(entry, schema, onlyGame).rewrites.size() == 5,
          "equal names.bin offsets do not become game IDs");
    const defrelocate::Maps counts{{{3, 500}}, {}};
    check(defrelocate::relocate(entry, schema, counts) == entry.data,
          "vector counts are structural values even when equal to moved IDs");

    auto unknown = entry;
    unknown.data.resize(unknown.data.size() - 4); word(unknown.data, 77);
    const auto blocked = defrelocate::analyze(unknown, schema, maps);
    check(blocked.unresolved.size() == 1 && blocked.unresolved[0].offset == unknown.data.size() - 4,
          "plain int32 is an unresolved candidate rather than an inferred reference");
    const auto before = unknown.data;
    rejects([&] { defrelocate::relocate(unknown, schema, maps); }, "unresolved candidate prevents partial output");
    check(unknown.data == before, "refusal preserves source bytes");

    auto opaque = entry; opaque.definition = "OPAQUE"; opaque.data.resize(17);
    check(defrelocate::analyze(opaque, schema, maps).rewrites.empty() &&
          !defrelocate::analyze(opaque, schema, maps).unresolved.empty(),
          "unknown class with CDefs-shaped prefix is not interpreted as OBJECT");
    opaque.definition = "NOT_IN_SCHEMA";
    check(!defrelocate::analyze(opaque, schema, maps).unresolved.empty(), "missing schema retains candidates");
    opaque.data = {1, 0, 1, 44, 45};
    check(defrelocate::relocate(opaque, schema, maps) == opaque.data, "untouched opaque payload is preserved");
    opaque.data = {9, 77, 0, 0, 0, 9};
    check(defrelocate::analyze(opaque, schema, maps).unresolved[0].offset == 1,
          "unaligned references are included in conservative candidate scan");

    bin::Entry vector{"VECTOR", "BAD_COUNT", 0, {1, 0, 1}};
    field(vector.data, "Array", {2, 77});
    rejects([&] { defrelocate::relocate(vector, schema, maps); }, "truncated typed vector refuses");
    vector.data = {1, 0, 1}; field(vector.data, "Array", {UINT32_MAX, 77});
    rejects([&] { defrelocate::relocate(vector, schema, maps); }, "overflowing typed vector count refuses");
    bin::Entry scalar{"SCALAR", "BAD_WIDTH", 0, {1, 0, 1}};
    field(scalar.data, "Index", {77, 88});
    rejects([&] { defrelocate::relocate(scalar, schema, maps); }, "oversized scalar reference refuses");
    auto malformed = entry; malformed.data[17] ^= 1;
    rejects([&] { defrelocate::relocate(malformed, schema, maps); }, "missing field tag retains unresolved references");
    malformed = entry; malformed.data[3] = 2;
    rejects([&] { defrelocate::relocate(malformed, schema, maps); }, "inconsistent CDefs count cannot certify prefix references");
    const auto unnamedSchema = defschema::Schema::loadText(R"({
        "SCALAR":{"prefix_len":3,"fields":[{"name":"","type":"CDefIndex"}]}
    })");
    scalar.data = {1, 0, 1}; word(scalar.data, 123); word(scalar.data, 77);
    rejects([&] { defrelocate::relocate(scalar, unnamedSchema, maps); },
            "unverified unnamed field tag cannot certify a typed reference");

    const defrelocate::Maps tags{{{defdecode::fieldTag("Index"), 123}}, {}};
    check(defrelocate::relocate(entry, schema, tags) == entry.data, "field tag collision is never rewritten");
    const defrelocate::Maps noop{{{77, 77}, {UINT32_MAX, UINT32_MAX}}, {}};
    check(defrelocate::relocate(opaque, schema, noop) == opaque.data, "identity maps require no schema inference");
    for (const auto invalid : {defrelocate::Maps{{{UINT32_MAX, 1}}, {}},
                               defrelocate::Maps{{{1, UINT32_MAX}}, {}},
                               defrelocate::Maps{{}, {{0, 4}}}})
        rejects([&] { defrelocate::relocate(entry, schema, invalid); }, "reserved identities cannot move");
    std::printf("Definition relocation tests: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
