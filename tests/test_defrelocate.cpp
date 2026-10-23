#include "forge/defrelocate.hpp"
#include "forge/defdecode.hpp"
#include "forge/fmplinks.hpp"
#include "forge/creatureappearance.hpp"
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
Bytes animationFixture(bool moved) {
    Bytes out{1,0,1};
    field(out,"Animation",{1,77,77,moved ? 900u : 77u,moved ? 900u : 77u,13});
    for (uint32_t kind=0;kind<=12;++kind) {
        word(out,kind);
        if (kind==3) { word(out,77);word(out,77); }
        else if (kind==4) { word(out,1);word(out,60); }
        else if (kind==5) word(out,12345);
        else if (kind==6) { out.push_back(1);out.push_back(0); }
        else if (kind==7) { out.push_back(0);word(out,77);word(out,77); }
        else if (kind==8 || kind==11) word(out,moved ? 900 : 77);
        else if (kind==12) { word(out,5);word(out,77);out.push_back('X'); }
        else word(out,77);
    }
    word(out,77);word(out,77);word(out,77);word(out,moved ? 900 : 77);
    field(out,"Tail",{});out.push_back(0);
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
    if(argc==4 && std::string_view(argv[1])=="--creature-appearance-audit") {
        try {
            using namespace forge;
            using Json=nlohmann::json;
            const auto folder=std::filesystem::path(argv[2])/"data"/"CompiledDefs";
            const auto schema=defschema::Schema::load(argv[3]);
            const auto source=bin::File::open(folder/"names.bin",folder/"game.bin");
            std::map<uint32_t,std::string> names;
            uint64_t offset=4;
            for(const auto& [crc,name]:source.originalNames()) {
                if(offset>UINT32_MAX)throw std::runtime_error("names offset overflow");
                names.emplace(uint32_t(offset),name);offset+=name.size()+5;
            }
            const auto mapJson=[](const creatureappearance::TextureMap& map) {
                auto rows=Json::array();
                for(const auto& row:map.rows)rows.push_back({{"offset",row.offset},{"original",row.original},
                    {"replacement",row.replacement},{"group",row.group}});
                return Json{{"rows",rows},{"dirty",map.dirty}};
            };
            auto rows=Json::array();
            for(size_t i=0;i<source.entries().size();++i) {
                const auto& entry=source.entries()[i];if(entry.definition!="CCreatureDef")continue;
                Json row{{"index",i},{"name",entry.name}};
                try {
                    const auto decoded=defdecode::decode(entry,schema);
                    if(!decoded.clean() || defdecode::encode(decoded)!=entry.data)
                        throw std::runtime_error("unclean component decode");
                    const auto layout=creatureappearance::read(decoded.prefix);
                    row["bodies"]=Json::array();row["body_texture_maps"]=Json::array();
                    row["slots"]=Json::array();row["skeletal_morphs"]=Json::array();
                    for(size_t body=0;body<layout.bodies.size();++body) {
                        auto alternatives=Json::array();
                        for(size_t alternative=0;alternative<layout.bodies[body].size();++alternative) {
                            const auto& model=layout.bodies[body][alternative];
                            alternatives.push_back({{"mesh",model.mesh},{"priority",model.priority},
                                {"textures",mapJson(model.textures)}});
                            row["slots"].push_back({{"body_part",body},{"alternative",alternative},
                                {"offset",model.offset},{"id",model.mesh}});
                        }
                        row["bodies"].push_back(alternatives);
                        row["body_texture_maps"].push_back(mapJson(layout.bodyTextures[body]));
                    }
                    for(const auto& morph:layout.skeletal) {
                        const auto found=names.find(morph.nameOffset);
                        if(found==names.end())throw std::runtime_error("skeletal name is not a pool boundary");
                        row["skeletal_morphs"].push_back({{"key",morph.key},{"group",morph.group},
                            {"name_offset",morph.nameOffset},{"name",found->second},{"allow_variation",morph.allowVariation}});
                    }
                    row["global_texture_map"]=mapJson(layout.globalTextures);
                    row["texture_group_count"]=layout.textureGroupCount;
                    row["stats_value_offset"]=layout.statsValueOffset;
                }catch(const std::exception& e){row["refused"]=e.what();}
                rows.push_back(std::move(row));
            }
            std::cout<<Json{{"records",rows}}.dump()<<'\n';return 0;
        }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
    }
    if(argc==4 && std::string_view(argv[1])=="--creature-body-audit") {
        try {
            const auto folder=std::filesystem::path(argv[2])/"data"/"CompiledDefs";
            const auto schema=forge::defschema::Schema::load(argv[3]);
            const auto source=forge::bin::File::open(folder/"names.bin",folder/"game.bin");
            auto rows=nlohmann::json::array();
            for(size_t i=0;i<source.entries().size();++i) {
                const auto& entry=source.entries()[i];if(entry.definition!="CCreatureDef") continue;
                nlohmann::json row{{"index",i},{"models",nlohmann::json::array()}};
                try {
                    const auto decoded=forge::defdecode::decode(entry,schema);
                    if(!decoded.clean() || forge::defdecode::encode(decoded)!=entry.data)
                        throw std::runtime_error("unclean component decode");
                    const auto layout=forge::fmplinks::creaturePrefixLayout(decoded.prefix);
                    row["prefix_length"]=decoded.prefixLen;
                    for(const auto& slot:layout.bodyModels) {
                        uint32_t value=0;
                        for(size_t n=0;n<4;++n)value|=uint32_t(entry.data.at(slot.offset+n))<<(8*n);
                        row["models"].push_back({{"body_part",slot.bodyPart},{"alternative",slot.alternative},
                                                {"offset",slot.offset},{"id",value}});
                    }
                } catch(const std::exception& e) {row["refused"]=e.what();}
                rows.push_back(std::move(row));
            }
            std::cout<<nlohmann::json{{"records",rows}}.dump()<<'\n';return 0;
        }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
    }
    if (argc == 4 && std::string_view(argv[1]) == "--creature-animation-name-audit") {
        try {
            using namespace forge;
            const auto folder=std::filesystem::path(argv[2])/"data"/"CompiledDefs";
            const auto schema=defschema::Schema::load(argv[3]);
            const auto source=bin::File::open(folder/"names.bin",folder/"game.bin");
            const auto isolatedSchema=defschema::Schema::loadText(R"({
                "CAppearanceDef":{"prefix_len":3,"fields":[{"name":"Animation","type":"CAnimationSet"}]}
            })");
            // In-memory synthetic destinations only. The independent auditor
            // verifies that every non-null native Animation name is in this
            // map; low integers stay out so unproven combo/recoil domains are
            // not implicitly claimed. Small-name collisions have CTest cases.
            defrelocate::Maps maps;
            uint64_t offset=4;
            for(const auto& name:source.originalNames()) {
                if(offset>=65536) {
                    if(offset+0x10000000>=UINT32_MAX) throw std::runtime_error("animation audit name overflow");
                    maps.nameOffsets.emplace(uint32_t(offset),uint32_t(offset+0x10000000));
                }
                offset+=name.second.size()+5;
            }
            nlohmann::json rows=nlohmann::json::array();
            for(size_t i=0;i<source.entries().size();++i) {
                const auto& entry=source.entries()[i];if(entry.definition!="CAppearanceDef") continue;
                const auto decoded=defdecode::decode(entry,schema);
                if(!decoded.clean() || defdecode::encode(decoded)!=entry.data || decoded.prefixLen!=3)
                    throw std::runtime_error("animation audit appearance decode mismatch");
                std::vector<const defdecode::DecodedField*> matches;
                for(const auto& field:decoded.fields) if(field.name=="Animation" && field.type=="CAnimationSet" && field.tagOk)
                    matches.push_back(&field);
                if(matches.size()!=1) throw std::runtime_error("animation audit requires one verified field");
                bin::Entry isolated{"CAppearanceDef",entry.name,entry.indexInDefinition,decoded.prefix};
                word(isolated.data,defdecode::fieldTag("Animation"));
                isolated.data.insert(isolated.data.end(),matches.front()->value.begin(),matches.front()->value.end());
                const auto analysis=defrelocate::analyze(isolated,isolatedSchema,maps);
                nlohmann::json row{{"index",i},{"before_hex",hex(isolated.data)},
                    {"rewrites",nlohmann::json::array()},{"unresolved",nlohmann::json::array()}};
                for(const auto& rewrite:analysis.rewrites)
                    row["rewrites"].push_back({{"offset",rewrite.offset},{"before",rewrite.before},
                        {"after",rewrite.after},{"path",rewrite.path},
                        {"domain",rewrite.domain==defrelocate::Domain::GameEntry?"game":"name"}});
                for(const auto& candidate:analysis.unresolved)
                    row["unresolved"].push_back({{"offset",candidate.offset},{"value",candidate.value}});
                if(analysis.unresolved.empty()) row["after_hex"]=hex(defrelocate::relocate(isolated,isolatedSchema,maps));
                rows.push_back(std::move(row));
            }
            std::cout<<nlohmann::json{{"records",rows},{"name_map",maps.nameOffsets},
                {"clone_eligibility_verified",false}}.dump()<<'\n';return 0;
        } catch(const std::exception& e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
    }
    if (argc == 4 && std::string_view(argv[1]) == "--creature-visual-audit") {
        try {
            using namespace forge;
            const auto folder=std::filesystem::path(argv[2])/"data"/"CompiledDefs";
            const auto schema=defschema::Schema::load(argv[3]);
            const auto source=bin::File::open(folder/"names.bin",folder/"game.bin");
            const auto& entries=source.entries();
            if(entries.size()>UINT32_MAX-3) throw std::runtime_error("visual audit index overflow");
            auto read=[](const auto& bytes,size_t at) {
                if(at>bytes.size() || bytes.size()-at<4) throw std::runtime_error("truncated visual audit word");
                return uint32_t(bytes[at]) | uint32_t(bytes[at+1])<<8 | uint32_t(bytes[at+2])<<16 | uint32_t(bytes[at+3])<<24;
            };
            nlohmann::json rows=nlohmann::json::array();
            for(size_t i=0;i<entries.size();++i) {
                const auto& root=entries[i];if(root.definition!="CREATURE") continue;
                const auto decoded=defdecode::decode(root,schema);
                if(!decoded.clean() || defdecode::encode(decoded)!=root.data || root.data.size()<5)
                    throw std::runtime_error("visual audit root decode mismatch");
                const size_t count=size_t(root.data[3]) | size_t(root.data[4])<<8;
                if(decoded.prefixLen!=5+12*count) throw std::runtime_error("visual audit prefix mismatch");
                std::vector<size_t> nodes{i};
                for(const auto* kind:{"CCreatureDef","CAppearanceDef"}) {
                    std::vector<size_t> matches;
                    for(size_t row=0;row<count;++row) if(read(root.data,5+12*row)==defdecode::fieldTag(kind))
                        matches.push_back(read(root.data,9+12*row));
                    if(matches.size()!=1 || matches.front()>=entries.size() || entries[matches.front()].definition!=kind) break;
                    nodes.push_back(matches.front());
                }
                if(nodes.size()!=3) continue; // Missing shapes are reported independently, not accepted.
                defrelocate::Maps maps;
                for(size_t n=0;n<nodes.size();++n) maps.gameEntries.emplace(uint32_t(nodes[n]),uint32_t(entries.size()+n));
                nlohmann::json payloads=nlohmann::json::array();
                for(const auto node:nodes) {
                    const auto& entry=entries[node];const auto view=defdecode::decode(entry,schema);
                    const auto analysis=defrelocate::analyze(entry,schema,maps);
                    nlohmann::json payload{{"index",node},{"type",entry.definition},{"prefix",view.prefixLen},
                        {"clean_roundtrip",view.clean() && defdecode::encode(view)==entry.data},
                        {"rewrites",nlohmann::json::array()},{"unresolved",nlohmann::json::array()}};
                    for(const auto& rewrite:analysis.rewrites)
                        payload["rewrites"].push_back({{"offset",rewrite.offset},{"before",rewrite.before},
                            {"after",rewrite.after},{"path",rewrite.path},
                            {"domain",rewrite.domain==defrelocate::Domain::GameEntry?"game":"name"}});
                    for(const auto& candidate:analysis.unresolved)
                        payload["unresolved"].push_back({{"offset",candidate.offset},{"value",candidate.value}});
                    if(analysis.unresolved.empty()) payload["after_hex"]=hex(defrelocate::relocate(entry,schema,maps));
                    payloads.push_back(std::move(payload));
                }
                rows.push_back({{"root",i},{"payloads",std::move(payloads)}});
            }
            std::cout<<nlohmann::json{{"entries",entries.size()},{"records",rows},
                {"clone_eligibility_verified",false}}.dump()<<'\n';
            return 0;
        } catch(const std::exception& e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
    }
    if (argc == 4 && std::string_view(argv[1]) == "--creature-self-audit") {
        try {
            using namespace forge;
            const auto folder = std::filesystem::path(argv[2]) / "data" / "CompiledDefs";
            const auto schema = defschema::Schema::load(argv[3]);
            const auto source = bin::File::open(folder / "names.bin", folder / "game.bin");
            nlohmann::json rows = nlohmann::json::array();
            for (size_t i = 0; i < source.entries().size(); ++i) {
                const auto& entry = source.entries()[i];
                if (entry.definition != "CREATURE") continue;
                const defrelocate::Maps maps{{{uint32_t(i),uint32_t(source.entries().size())}}, {}};
                const auto decoded = defdecode::decode(entry,schema);
                const auto analysis = defrelocate::analyze(entry,schema,maps);
                nlohmann::json row{{"index",i},{"name",entry.name},{"prefix",decoded.prefixLen},
                    {"rewrites",nlohmann::json::array()},{"unresolved",nlohmann::json::array()}};
                for (const auto& rewrite : analysis.rewrites)
                    row["rewrites"].push_back({{"offset",rewrite.offset},{"before",rewrite.before},
                        {"after",rewrite.after},{"path",rewrite.path}});
                for (const auto& candidate : analysis.unresolved)
                    row["unresolved"].push_back({{"offset",candidate.offset},{"value",candidate.value}});
                if (analysis.unresolved.empty()) row["after_hex"] = hex(defrelocate::relocate(entry,schema,maps));
                rows.push_back(std::move(row));
            }
            std::cout << nlohmann::json{{"entries",source.entries().size()},{"records",rows}}.dump() << '\n';
            return 0; // Audit reports unresolved donors, never silently accepts them.
        } catch (const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
    }
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
        "CREATURE":{"fields":[{"name":"Value","type":"int32"}]},
        "CThingCreatureDef":{"fields":[{"name":"Value","type":"int32"}]},
        "HERO_ABILITY":{"fields":[{"name":"DummyObject","type":"?"},{"name":"Name","type":"?"}]},
        "CHeroAbilityDef":{"fields":[{"name":"DummyObject","type":"?"},{"name":"Name","type":"?"}]},
        "UNVERIFIED":{"fields":[{"name":"DummyObject","type":"?"},{"name":"Name","type":"?"}]}
    })");
    for (const auto* owner : {"BUILDING", "CThingBuildingDef", "CREATURE", "CThingCreatureDef"}) {
        auto bytes = fixture(false); bytes.resize(17); field(bytes, "Value", {12345});
        auto expected = fixture(true); expected.resize(17); field(expected, "Value", {12345});
        bin::Entry building{owner, "BUILDING_PROBE", 0, bytes};
        check(defrelocate::relocate(building, ownerSchema, maps) == expected,
              "confirmed owner component data/inheritance move while CRC/header/trailing scalar stay exact");
        building.data[3] = 2;
        rejects([&] { defrelocate::relocate(building, ownerSchema, maps); },
                "confirmed owner listing with wrong count refuses");
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
                                  "Vector_float","Map_JJ","Map_KK","Map_JK","Map_KJ",
                                  "Map_JW4EIdleStateGroup__"}) {
        const bool idle = std::string_view(container) == "Map_JW4EIdleStateGroup__";
        const std::string owner = idle ? "CIdleSchedulerDef" : "PROBE";
        const std::string member = idle ? "Period" : "Array";
        const auto containerSchema = defschema::Schema::loadText(std::string("{\"") + owner +
            "\":{\"fields\":[{\"name\":\"" + member + "\",\"type\":\"" + container + R"("},
                {"name":"Index","type":"CDefIndex"}]}})");
        const bool isMap = std::string_view(container).starts_with("Map_");
        bin::Entry probe{owner,"BOUNDARIES",0,{1,0,1}};
        word(probe.data,defdecode::fieldTag(member)); word(probe.data,isMap ? 1 : 2);
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
    for (const auto* owner : {"OPINION_REACTION_MASK", "OPINION_DEED_MASK"}) {
        const bool reaction = std::string_view(owner) == "OPINION_REACTION_MASK";
        const std::string member = reaction ? "ReactionEnabled" : "DeedEnabled";
        const auto maskSchema = defschema::Schema::loadText(std::string("{\"") + owner +
            "\":{\"fields\":[{\"name\":\"" + member + "\",\"type\":\"" +
            (reaction ? "?" : "Map__NW4EOpinionDeedType__") + "\"}]}}");
        bin::Entry mask{owner,"MASK",0,{1,0,1}};
        word(mask.data,defdecode::fieldTag(member)); word(mask.data,2);
        word(mask.data,77); mask.data.push_back(1); word(mask.data,88); mask.data.push_back(0);
        check(defrelocate::relocate(mask,maskSchema,maps) == mask.data,
              "opinion enum and boolean values retain colliding namespace IDs");
        auto badMask = mask; badMask.data.back() = 2;
        rejects([&] { defrelocate::relocate(badMask,maskSchema,maps); }, "noncanonical opinion boolean refuses");
        badMask = mask; badMask.data[7] = 3;
        rejects([&] { defrelocate::relocate(badMask,maskSchema,maps); }, "truncated opinion mask refuses");
    }
    for (const auto* owner : {"CInventoryItemDef", "REGION", "HERO_ABILITY"}) {
        const std::string member = std::string_view(owner) == "CInventoryItemDef" ? "ItemDetails" :
            std::string_view(owner) == "REGION" ? "RegionDescription" : "MaxedOutDescription";
        const auto textSchema = defschema::Schema::loadText(std::string("{\"") + owner +
            "\":{\"fields\":[{\"name\":\"" + member + "\",\"type\":\"" +
            (std::string_view(owner) == "HERO_ABILITY" ? "?" : "uint32") +
            "\"},{\"name\":\"Index\",\"type\":\"CDefIndex\"}]}}");
        bin::Entry text{owner,"TEXT_DOMAIN",0,{1,0,1}};
        word(text.data,defdecode::fieldTag(member)); word(text.data,77); field(text.data,"Index",{77});
        auto expected = text.data; expected[expected.size()-4] = 88;
        check(defrelocate::relocate(text,textSchema,maps) == expected,
              "verified FMP text domain is preserved while a game reference relocates");
        auto badText = text; badText.data.insert(badText.data.begin()+11,0);
        rejects([&] { defrelocate::relocate(badText,textSchema,maps); }, "oversized text field cannot certify its domain");
    }
    const auto statSchema = defschema::Schema::loadText(R"({
        "HERO_STATS":{"fields":[{"name":"StatLevels","type":"vector<CAbilityTrackDef>"}]},
        "CTavernGameDef":{"fields":[{"name":"BestScore","type":"?"}]}})");
    bin::Entry stat{"HERO_STATS","NESTED_TEXT",0,{1,0,1}};
    field(stat.data,"StatLevels",{1}); field(stat.data,"ExperienceStat",{77});
    field(stat.data,"Abilities",{1}); field(stat.data,"ExperienceCost",{1});
    field(stat.data,"AbilityUnlocked",{77}); field(stat.data,"Description",{77});
    field(stat.data,"MaxedOutDescription",{77});
    check(defrelocate::relocate(stat,statSchema,maps)==stat.data,
          "nested stat text and enum fields retain values equal to changed IDs");
    auto badStat=stat; badStat.data[31]=77;
    rejects([&] { defrelocate::relocate(badStat,statSchema,maps); }, "nested stat integer cost remains opaque");
    badStat=stat; badStat.data[27]^=1;
    rejects([&] { defrelocate::relocate(badStat,statSchema,maps); }, "every nested stat tag must match");
    badStat=stat; badStat.data[23]=2;
    rejects([&] { defrelocate::relocate(badStat,statSchema,maps); }, "nested stat count must fit all members");
    badStat=stat; badStat.data.push_back(0);
    rejects([&] { defrelocate::relocate(badStat,statSchema,maps); }, "nested stat trailing data refuses");
    bin::Entry tavern{"CTavernGameDef","SCORE_LABEL",0,{1,0,1}};
    field(tavern.data,"BestScore",{77});
    check(defrelocate::relocate(tavern,statSchema,maps)==tavern.data,
          "tavern score label belongs to the text bank");
    word(tavern.data,0);
    rejects([&] { defrelocate::relocate(tavern,statSchema,maps); }, "tavern text field must be four bytes");
    const auto matchSchema = defschema::Schema::loadText(R"({
        "OPINION_REACTION_MANAGER":{"fields":[{"name":"Matches",
          "type":"V__list_PAVCReactionMatch__V__allocator_PAVCReactionMatch___std___std__"}]}})");
    for (uint32_t kind=0;kind<=4;++kind) {
        bin::Entry match{"OPINION_REACTION_MANAGER","MATCHES",0,{1,0,1}};
        field(match.data,"Matches",{1,kind,1});
        if (kind<4) { word(match.data,60); if(kind>=2) word(match.data,3); }
        else word(match.data,2); // two opinion floats, followed by radius
        const size_t floats=kind==4 ? 3 : kind==3 ? 9 : 6;
        for(size_t i=0;i<floats;++i) word(match.data,77);
        const defrelocate::Maps matchMaps{{{77,88},{15360,20000}}, {}};
        check(defrelocate::relocate(match,matchSchema,matchMaps)==match.data,
              "reaction variant floats and integer boundaries retain colliding bytes");
        auto badMatch=match; badMatch.data[15]=77;
        rejects([&] { defrelocate::relocate(badMatch,matchSchema,matchMaps); }, "reaction integer parameters stay opaque");
        badMatch=match; badMatch.data[11]=5;
        rejects([&] { defrelocate::relocate(badMatch,matchSchema,matchMaps); }, "unknown reaction match type refuses");
        badMatch=match; badMatch.data.pop_back();
        rejects([&] { defrelocate::relocate(badMatch,matchSchema,matchMaps); }, "incomplete reaction match body refuses");
    }
    const auto frequencySchema = defschema::Schema::loadText(R"({
        "OPINION_REACTION_MANAGER":{"fields":[{"name":"Frequency","type":"CReactionFrequencyTraitsArray"}]}})");
    bin::Entry frequency{"OPINION_REACTION_MANAGER","FREQUENCIES",0,{1,0,1}};
    word(frequency.data,defdecode::fieldTag("Frequency"));
    word(frequency.data,1); word(frequency.data,77); word(frequency.data,88);
    word(frequency.data,2); for (size_t i=0;i<5;++i) word(frequency.data,77);
    word(frequency.data,3); frequency.data.push_back(1);
    for (size_t i=0;i<5;++i) word(frequency.data,77);
    for (size_t i=3;i<158;++i) word(frequency.data,0);
    check(defrelocate::relocate(frequency,frequencySchema,maps) == frequency.data,
          "all frequency variants preserve float and frame-count values across colliding IDs");
    auto badFrequency = frequency; badFrequency.data[7] = 4;
    rejects([&] { defrelocate::relocate(badFrequency,frequencySchema,maps); }, "unknown frequency discriminant refuses");
    badFrequency = frequency; badFrequency.data.pop_back();
    rejects([&] { defrelocate::relocate(badFrequency,frequencySchema,maps); }, "frequency table requires both 79-element groups");
    badFrequency = frequency; badFrequency.data[47] = 2;
    rejects([&] { defrelocate::relocate(badFrequency,frequencySchema,maps); }, "frequency boolean must be canonical");
    badFrequency = frequency; badFrequency.data.push_back(0);
    rejects([&] { defrelocate::relocate(badFrequency,frequencySchema,maps); }, "frequency table cannot have opaque trailing bytes");
    const auto soundSchema = defschema::Schema::loadText(R"({
        "SOUND_SETUP":{"fields":[{"name":"MusicEntries","type":"Vector_VCMusicEntry__"},
            {"name":"MusicSetEntries","type":"Vector_VCMusicSetEntry__"},
            {"name":"ReverbEnvironmentEntries","type":"Vector_VCReverbEnvironmentEntry__"}]}})");
    bin::Entry sound{"SOUND_SETUP","SOUND_DOMAINS",0,{1,0,1}};
    field(sound.data,"MusicEntries",{1,5,77,77}); sound.data.push_back(1);
    field(sound.data,"MusicSetEntries",{1,1,60,3});
    field(sound.data,"ReverbEnvironmentEntries",{1});
    for (size_t i=0; i<21; ++i) word(sound.data,77);
    const defrelocate::Maps soundMaps{{{77,88},{15360,20000}}, {{77,900}}};
    auto soundExpected = sound.data; soundExpected[15] = 132; soundExpected[16] = 3;
    check(defrelocate::relocate(sound,soundSchema,soundMaps) == soundExpected,
          "music names relocate independently while gain/reverb bits and integer boundaries preserve data");
    auto badSound = sound; badSound.data[23] = 2;
    rejects([&] { defrelocate::relocate(badSound,soundSchema,soundMaps); }, "invalid music boolean refuses");
    badSound = sound; badSound.data[11] = 77;
    rejects([&] { defrelocate::relocate(badSound,soundSchema,soundMaps); }, "opaque music ID is not treated as a game reference");
    badSound = sound; badSound.data[7] = 2;
    rejects([&] { defrelocate::relocate(badSound,soundSchema,soundMaps); }, "music row count must match full value");
    badSound = sound; badSound.data.pop_back();
    rejects([&] { defrelocate::relocate(badSound,soundSchema,soundMaps); }, "reverb row must contain all 21 floats");
    const auto animationSchema = defschema::Schema::loadText(R"({
        "CAppearanceDef":{"fields":[{"name":"Animation","type":"CAnimationSet"},
            {"name":"Tail","type":"bool"}]},
        "UNVERIFIED_ANIMATION":{"fields":[{"name":"Animation","type":"CAnimationSet"},
            {"name":"Tail","type":"bool"}]}
    })");
    bin::Entry animation{"CAppearanceDef","ANIMATION_DOMAINS",0,animationFixture(false)};
    const defrelocate::Maps animationMaps{{{77,88},{15360,20000}},{{77,900},{15360,21000}}};
    const auto animationAnalysis = defrelocate::analyze(animation,animationSchema,animationMaps);
    check(animationAnalysis.unresolved.empty() && animationAnalysis.rewrites.size()==5,
          "all thirteen animation variants identify only five native names-offset references");
    check(defrelocate::relocate(animation,animationSchema,animationMaps)==animationFixture(true),
          "animation CRCs, asset IDs, scalar bits, inline strings and cross-primitive matches stay exact");
    check(defrelocate::relocate(animation,animationSchema,defrelocate::Maps{{{77,88},{15360,20000}}, {}})==animation.data,
          "animation names and asset IDs never become game-entry references");
    check(animation.data==animationFixture(false),"animation relocation leaves input unchanged");
    auto badAnimation=animation;
    badAnimation.data[7]=2;
    rejects([&] {defrelocate::relocate(badAnimation,animationSchema,animationMaps);},"animation entry count requires complete rows");
    badAnimation=animation;for(size_t i=7;i<11;++i) badAnimation.data[i]=255;
    rejects([&] {defrelocate::relocate(badAnimation,animationSchema,animationMaps);},"overflowing animation entry count refuses");
    badAnimation=animation;badAnimation.data[27]=14;
    rejects([&] {defrelocate::relocate(badAnimation,animationSchema,animationMaps);},"animation component count is bounded");
    badAnimation=animation;badAnimation.data[31]=13;
    rejects([&] {defrelocate::relocate(badAnimation,animationSchema,animationMaps);},"unknown animation component refuses");
    badAnimation=animation;badAnimation.data[39]=0;
    rejects([&] {defrelocate::relocate(badAnimation,animationSchema,animationMaps);},"duplicate animation component refuses");
    for(const auto offset:{91u,97u}) {
        badAnimation=animation;badAnimation.data[offset]=2;
        rejects([&] {defrelocate::relocate(badAnimation,animationSchema,animationMaps);},"animation bool must be canonical");
    }
    badAnimation=animation;for(size_t i=142;i<146;++i) badAnimation.data[i]=255;
    rejects([&] {defrelocate::relocate(badAnimation,animationSchema,animationMaps);},"animation inline string length must fit");
    badAnimation=animation;badAnimation.data.insert(badAnimation.data.end()-5,0);
    rejects([&] {defrelocate::relocate(badAnimation,animationSchema,animationMaps);},"animation trailing data refuses");
    badAnimation=animation;badAnimation.data.erase(badAnimation.data.end()-6);
    rejects([&] {defrelocate::relocate(badAnimation,animationSchema,animationMaps);},"truncated animation defaults refuse");
    for(const auto offset:{71u,75u,83u}) {
        badAnimation=animation;
        for(size_t i=0;i<4;++i) badAnimation.data[offset+i]=uint8_t(77u >> (8*i));
        rejects([&] {defrelocate::relocate(badAnimation,animationSchema,animationMaps);},
                "unproven whole combo/recoil integers remain refusal candidates");
    }
    auto truncationMaps=animationMaps;
    // The structural tag is always present even before a truncated value's
    // first numeric candidate; force analysis rather than its no-candidate no-op.
    truncationMaps.gameEntries.emplace(defdecode::fieldTag("Animation"),1234567);
    for(size_t end=7;end<animation.data.size()-5;++end) {
        badAnimation=animation;badAnimation.data.resize(end);
        field(badAnimation.data,"Tail",{});badAnimation.data.push_back(0);
        rejects([&] {defrelocate::relocate(badAnimation,animationSchema,truncationMaps);},
                "every truncated animation value refuses before reference output");
    }
    badAnimation=animation;badAnimation.definition="UNVERIFIED_ANIMATION";
    rejects([&] {defrelocate::relocate(badAnimation,animationSchema,animationMaps);},"animation layout is limited to its verified owner/field");
    bin::Entry emptyAnimation{"CAppearanceDef","EMPTY_ANIMATION",0,{1,0,1}};
    field(emptyAnimation.data,"Animation",{0,77,77,77,77});field(emptyAnimation.data,"Tail",{});emptyAnimation.data.push_back(0);
    auto emptyExpected=emptyAnimation.data;emptyExpected[23]=132;emptyExpected[24]=3;
    check(defrelocate::relocate(emptyAnimation,animationSchema,animationMaps)==emptyExpected,
          "empty animation set still relocates only DefaultGroup");
    badAnimation=emptyAnimation;badAnimation.data.insert(badAnimation.data.end()-5,0);
    check(defrelocate::relocate(badAnimation,animationSchema,{})==badAnimation.data,
          "default no-change relocation does not interpret an untouched payload");
    rejects([&] {defrelocate::relocate(badAnimation,animationSchema,{},true);},
            "explicit known-layout validation checks Animation even with no changed IDs");
    const auto expressionSchema = defschema::Schema::loadText(R"({
        "CExpressionSubDef":{"fields":[{"name":"ExpressionDef","type":"int32"}]}})");
    bin::Entry expression{"CExpressionSubDef","EXPRESSION_OWNER",0,{0,0,0}};
    field(expression.data,"ExpressionDef",{77});
    auto expressionExpected = expression.data; expressionExpected[7] = 88;
    check(defrelocate::relocate(expression,expressionSchema,maps) == expressionExpected,
          "native expression field follows global definition IDs, not colliding name offsets");
    for (const auto sentinel : {0u,0xfffffffeu}) {
        auto nullExpression = expression;
        for (size_t i=0; i<4; ++i) nullExpression.data[7+i] = uint8_t(sentinel >> (i*8));
        const defrelocate::Maps nullMaps{{{sentinel,99}}, {}};
        check(defrelocate::relocate(nullExpression,expressionSchema,nullMaps) == nullExpression.data,
              "nonpositive expression sentinels retain native null behavior");
    }
    const defrelocate::Maps invalidExpression{{{77,0}}, {}};
    rejects([&] { defrelocate::relocate(expression,expressionSchema,invalidExpression); },
            "real expression reference cannot become a null expression");
    auto oversizedExpression = expression; word(oversizedExpression.data,88);
    rejects([&] { defrelocate::relocate(oversizedExpression,expressionSchema,maps); },
            "expression reference width must match the native scalar");
    const auto stringMapSchema = defschema::Schema::loadText(R"({
        "UI":{"fields":[{"name":"ActionMap","type":"Map_VCCharString__K"}]}})");
    bin::Entry stringMap{"UI","STRING_BOUNDARY",0,{1,0,1}};
    field(stringMap.data,"ActionMap",{2,1}); stringMap.data.push_back('A'); stringMap.data.push_back(0);
    word(stringMap.data,60); stringMap.data.push_back('M'); stringMap.data.push_back(0);
    const defrelocate::Maps stringMaps{{{15360,20000}}, {}};
    check(defrelocate::relocate(stringMap,stringMapSchema,stringMaps) == stringMap.data,
          "inline string terminators establish boundaries before unknown integer keys");
    auto badStringMap = stringMap; badStringMap.data.pop_back();
    rejects([&] { defrelocate::relocate(badStringMap,stringMapSchema,stringMaps); }, "unterminated string map refuses");
    badStringMap = stringMap; badStringMap.data.push_back(1);
    rejects([&] { defrelocate::relocate(badStringMap,stringMapSchema,stringMaps); }, "string map trailing bytes refuse");
    badStringMap = stringMap; badStringMap.data[17] = 0; badStringMap.data[18] = 60;
    rejects([&] { defrelocate::relocate(badStringMap,stringMapSchema,stringMaps); }, "aligned string map integer key stays unknown");
    const auto costSchema = defschema::Schema::loadText(R"({
        "HERO_ABILITY":{"fields":[{"name":"StaminaCost","type":"?"}]}})");
    bin::Entry cost{"HERO_ABILITY","COST_BOUNDARY",0,{1,0,1}};
    field(cost.data,"StaminaCost",{2,1,60});
    const defrelocate::Maps costMaps{{{15360,20000}}, {}};
    check(defrelocate::relocate(cost,costSchema,costMaps) == cost.data,
          "native hero cost vector excludes cross-element byte matches");
    cost.data[7] = 3;
    rejects([&] { defrelocate::relocate(cost,costSchema,costMaps); }, "hero cost vector count must match");
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
