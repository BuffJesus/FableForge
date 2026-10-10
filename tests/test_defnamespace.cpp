#include "forge/defnamespace.hpp"
#include "forge/defdecode.hpp"
#include "forge/modorder.hpp"
#include <array>
#include <cstdio>
#include <stdexcept>
#include <iostream>
#include <nlohmann/json.hpp>

// Read-only original-bank audit: baseline root, schema, then source roots.
int audit(int argc, char** argv) {
    using namespace forge;
    auto open = [](const char* root, const char* bank) {
        const auto folder = std::filesystem::path(root) / "data" / "CompiledDefs";
        return bin::File::open(folder / "names.bin", folder / bank);
    };
    const auto baseline = open(argv[1], "game.bin");
    const auto schema = defschema::Schema::load(argv[2]);
    std::vector<bin::File> files;
    for (int i = 3; i < argc; ++i) files.push_back(open(argv[i], "game.bin"));
    std::vector<defnamespace::Input> inputs;
    for (const auto& file : files) inputs.push_back({file.entries(), file.originalNames()});
    const auto plan = defnamespace::allocate({baseline.entries(), baseline.originalNames()}, inputs);
    std::optional<defnamespace::Prepared> prepared;
    std::string preparationError;
    try { prepared=defnamespace::prepare({baseline.entries(),baseline.originalNames()},inputs,schema); }
    catch (const std::exception& e) { preparationError=e.what(); }
    nlohmann::json report{{"scope", "Read-only namespace and BIN reference audit; no external-reference or writer acceptance"},
        {"baseline", argv[1]}, {"entries", plan.entries.size()}, {"names", plan.names.size()},
        {"sources", nlohmann::json::array()}, {"preparation_error",preparationError}};
    const auto lookups=defnamespace::analyzeNameLookups(plan,inputs);
    report["core_name_lookup"]={{"ambiguous_groups",nlohmann::json::array()},
        {"sources",nlohmann::json::array()}};
    for(const auto crc:lookups.ambiguousNamedCrcs)
        report["core_name_lookup"]["ambiguous_groups"].push_back({{"crc",crc},{"slots",lookups.plannedCandidates.at(crc)}});
    for(const auto& source:lookups.sources) {
        nlohmann::json row={{"source_crc_groups",source.existingQueryGroups},{"added_crc_groups",source.addedQueryGroups},
            {"changed_existing_groups",nlohmann::json::array()}};
        for(const auto& change:source.changed)row["changed_existing_groups"].push_back({{"crc",change.crc},
            {"source_slots_in_plan",change.sourceSlotsInPlan},{"planned_slots",change.plannedSlots}});
        report["core_name_lookup"]["sources"].push_back(std::move(row));
    }
    for (size_t i = 0; i < files.size(); ++i) {
        const auto& maps = plan.sources[i];
        nlohmann::json source{{"root", argv[i + 3]}, {"game_map", maps.gameEntries},
            {"name_map", maps.nameOffsets}, {"rewrites", 0}, {"unresolved", 0},
            {"records", nlohmann::json::array()}};
        if (prepared) {
            source["prepared_game"]=nlohmann::json::array();
            for (const auto& contribution : prepared->sources[i]) {
                const auto& e=contribution.entry;
                source["prepared_game"].push_back({{"target",contribution.targetSlot},
                    {"definition",e.definition},{"name",e.name},{"ordinal",e.indexInDefinition},
                    {"payload_sha256",modorder::sha256OfBytes(std::string_view(
                        reinterpret_cast<const char*>(e.data.data()),e.data.size()))}});
            }
        }
        for (const auto* bank : {"game.bin", "script.bin", "frontend.bin"}) {
            const auto file = open(argv[i + 3], bank);
            for (size_t slot = 0; slot < file.entries().size(); ++slot) {
                const auto& entry = file.entries()[slot];
                const auto analysis = defrelocate::analyze(entry, schema, maps);
                if (analysis.rewrites.empty() && analysis.unresolved.empty()) continue;
                nlohmann::json row{{"bank", bank}, {"slot", slot}, {"name", entry.name},
                    {"rewrites", nlohmann::json::array()}, {"unresolved", nlohmann::json::array()}};
                for (const auto& r : analysis.rewrites)
                    row["rewrites"].push_back({{"offset",r.offset},{"before",r.before},{"after",r.after},{"path",r.path},
                        {"domain",r.domain == defrelocate::Domain::GameEntry ? "game" : "name"}});
                for (const auto& c : analysis.unresolved)
                    row["unresolved"].push_back({{"offset",c.offset},{"value",c.value}});
                if (analysis.unresolved.empty()) {
                    auto hex = [](const std::vector<uint8_t>& bytes) {
                        static constexpr char digits[] = "0123456789abcdef";
                        std::string result;
                        for (auto byte : bytes) { result += digits[byte >> 4]; result += digits[byte & 15]; }
                        return result;
                    };
                    row["after_hex"] = hex(defrelocate::relocate(entry, schema, maps));
                }
                source["rewrites"] = source["rewrites"].get<size_t>() + analysis.rewrites.size();
                source["unresolved"] = source["unresolved"].get<size_t>() + analysis.unresolved.size();
                source["records"].push_back(std::move(row));
            }
        }
        report["sources"].push_back(std::move(source));
    }
    std::cout << report.dump(2) << '\n';
    return 0; // inventory success does not assert relocation safety
}

int main(int argc, char** argv) {
    if (argc >= 4) {
        try { return audit(argc, argv); }
        catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
    }
    if (argc != 1) return 2;
    using namespace forge;
    using namespace defnamespace;
    int failures = 0;
    auto check = [&](bool value, const char* why) {
        if (!value) { ++failures; std::fprintf(stderr, "%s\n", why); }
    };
    auto rejects = [&](auto action, const char* why) {
        try { action(); check(false, why); } catch (const std::runtime_error&) {}
    };
    std::vector<bin::Entry> base{{"OBJECT", "BASE", 0, {1}}, {"CDef", "", 0, {2}}};
    std::vector<Name> baseNames{{1, "OBJECT"}, {2, "BASE"}, {3, "CDef"}};
    auto a = base, b = base;
    a.push_back({"OBJECT", "RATS", 1, {3}});
    a.push_back({"CDef", "", 1, {4}});
    b.push_back({"CDef", "SWORD", 1, {5}});
    b.push_back({"CDef", "", 2, {4}}); // identical bytes do not imply shared identity
    auto an = baseNames, bn = baseNames;
    an.emplace_back(4, "RATS");
    bn.emplace_back(5, "SWORD");
    const Input baseline{base, baseNames};
    std::array<Input, 2> inputs{{{a, an}, {b, bn}}};
    auto plan = allocate(baseline, inputs);
    check(plan.entries.size() == 6, "both distinct additions survive");
    check(plan.entries[0].data == base[0].data && plan.entries[1].data == base[1].data,
          "baseline payloads preserved");
    check(plan.sources[0].gameEntries.empty(), "first source retains numeric slots");
    check(plan.sources[1].gameEntries == std::map<uint32_t,uint32_t>{{2,4},{3,5}},
          "second source gets simultaneous mappings");
    check(plan.entries[4].indexInDefinition == 2 && plan.entries[5].indexInDefinition == 3,
          "output per-definition ordinals rebuilt");
    check(plan.names == std::vector<Name>{{1,"OBJECT"},{2,"BASE"},{3,"CDef"},{4,"RATS"},{5,"SWORD"}},
          "baseline names and both divergent names survive");
    check(plan.sources[1].nameOffsets == std::map<uint32_t,uint32_t>{{33,42}},
          "source name offsets map to appended names");
    std::swap(inputs[0], inputs[1]);
    auto reverse = allocate(baseline, inputs);
    check(reverse.entries.size() == 6 && reverse.entries[2].name == "SWORD" &&
          reverse.entries[4].name == "RATS", "both orders preserve all additions");
    check(reverse.sources[1].nameOffsets == std::map<uint32_t,uint32_t>{{33,43}},
          "reverse variable-length name allocation");
    auto same = a;
    same[2].data = {9};
    inputs = {{{a, an}, {same, an}}};
    auto shared = allocate(baseline, inputs);
    check(shared.entries.size() == 5 && !shared.sources[1].gameEntries.contains(2) &&
          shared.sources[1].gameEntries.at(3) == 4, "named contributors share; anonymous additions stay separate");
    check(shared.entries[2].data == a[2].data && same[2].data == std::vector<uint8_t>{9},
          "allocation does not select or mutate contributor payloads");
    auto shortBase = std::vector<bin::Entry>{base[0]};
    std::array<Input,1> single{{{shortBase, std::span(baseNames).first(1)}}};
    check(allocate(baseline, single).entries.size() == 2, "shorter source preserves baseline");
    auto bad = a;
    bad[0].name = "WRONG";
    single[0] = {bad, an};
    rejects([&] { allocate(baseline, single); }, "baseline identity change refused");
    bad = a; bad[2].indexInDefinition = 7;
    single[0] = {bad, an};
    rejects([&] { allocate(baseline, single); }, "noncanonical source refused");
    bad = a; bad.push_back({"OBJECT","RATS",2,{8}});
    single[0] = {bad, an};
    auto repeated = allocate(baseline, single);
    check(repeated.entries.size() == 5 && repeated.sources[0].gameEntries.empty(),
          "duplicate named additions retain distinct numeric identities");
    bad = a; bad[2].name = "BASE";
    single[0] = {bad, an};
    auto baselineAlias = allocate(baseline, single);
    check(baselineAlias.entries.size() == 4 && baselineAlias.sources[0].gameEntries.empty(),
          "addition sharing baseline name gets a separate slot");
    bad = a; bad[2].definition = "CDef"; bad[2].indexInDefinition = 1;
    bad[3].indexInDefinition = 2;
    inputs = {{{a,an},{bad,an}}};
    auto differentTypes = allocate(baseline, inputs);
    check(differentTypes.entries.size() == 6 && differentTypes.sources[1].gameEntries.at(2) == 4,
          "same name with distinct types cannot coalesce");
    bad = a; bad.push_back({"OBJECT","RATS",2,{8}});
    inputs = {{{a,an},{bad,an}}};
    auto ambiguous = allocate(baseline, inputs);
    check(ambiguous.entries.size() == 7 && ambiguous.sources[1].gameEntries.at(2) == 4 &&
          ambiguous.sources[1].gameEntries.at(4) == 6,
          "ambiguity in any source prevents cross-source coalescing");
    auto badNames = an; badNames[0].first = 99;
    single[0] = {a, badNames};
    rejects([&] { allocate(baseline, single); }, "baseline CRC change refused");
    badNames = an; badNames.emplace_back(99, "RATS");
    single[0] = {a, badNames};
    rejects([&] { allocate(baseline, single); }, "conflicting added CRC refused");
    badNames = an; badNames.emplace_back(9, std::string("bad\0name",8));
    single[0] = {a, badNames};
    rejects([&] { allocate(baseline, single); }, "embedded null refused");
    auto duplicateNames = baseNames; duplicateNames.push_back(baseNames[1]);
    single[0] = {base, duplicateNames};
    auto duplicates = allocate({base, duplicateNames}, single);
    check(duplicates.names == duplicateNames && duplicates.sources[0].nameOffsets.empty(),
          "individual duplicate baseline name offsets preserved");
    check(a[2].indexInDefinition == 1 && b[2].indexInDefinition == 1 &&
          bn.size() == 4 && base.size() == 2, "inputs unchanged after successes and failures");
    {
        auto namesFor=[](const std::vector<bin::Entry>& entries) {
            std::map<std::string,uint32_t> unique;
            for(const auto& e:entries)for(const auto& name:{e.definition,e.name})unique[name]=bin::nameCrc(name);
            std::vector<Name> names;
            for(const auto& [name,crc]:unique)names.emplace_back(crc,name);
            return names;
        };
        check(bin::nameCrc("CREATURE_TRADER_01")==0xAA22BB08 && bin::nameCrc("Graphic")==0x2E6B63C8,
              "native name CRC differs from original known values");
        check(bin::nameCrc("LOOKUP_ZERO_SY2CT3I")==0 && bin::nameCrc("")==0,
              "nonempty CRC-zero fixture changed");
        std::vector<bin::Entry> lb{{"CDef","",0,{}}};
        auto la=lb, lc=lb;
        la.push_back({"OBJECT","LOOKUP_ZERO_SY2CT3I",0,{}});
        lc.push_back({"CDef","",1,{}});
        auto lbn=namesFor(lb), lan=namesFor(la), lcn=namesFor(lc);
        std::array<Input,2> li{{{la,lan},{lc,lcn}}};
        const auto lp=allocate({lb,lbn},li);
        const auto review=analyzeNameLookups(lp,li);
        check(review.plannedCandidates.at(0)==std::vector<uint32_t>({0,1,2}) &&
              review.ambiguousNamedCrcs==std::vector<uint32_t>{0},"zero-CRC candidates lost anonymous rows");
        check(review.candidatesFor("").empty() && review.candidatesFor("LOOKUP_ZERO_SY2CT3I").size()==3 &&
              review.candidatesFor("missing").empty(),"empty query confused with nonempty zero-CRC query");
        check(review.sources[0].changed.size()==1 &&
              review.sources[0].changed[0].sourceSlotsInPlan==std::vector<uint32_t>({0,1}) &&
              review.sources[0].changed[0].plannedSlots==std::vector<uint32_t>({0,1,2}) &&
              review.sources[1].addedQueryGroups==1,"zero-CRC source candidate growth not reported");
        auto invalid=lp;
        invalid.sources.pop_back();
        rejects([&]{analyzeNameLookups(invalid,li);},"source/map count mismatch accepted");
        invalid=lp;invalid.sources[1].gameEntries[0]=2;
        rejects([&]{analyzeNameLookups(invalid,li);},"noninjective lookup mapping accepted");
        invalid=lp;invalid.sources[1].gameEntries[1]=1;
        rejects([&]{analyzeNameLookups(invalid,li);},"mapped lookup identity mismatch accepted");
        invalid=lp;invalid.sources[1].gameEntries[9]=2;
        rejects([&]{analyzeNameLookups(invalid,li);},"out-of-range map key accepted");
        invalid=lp;invalid.names[0].first^=1;
        rejects([&]{analyzeNameLookups(invalid,li);},"invalid planned name CRC accepted");
        lan[0].first^=1;
        rejects([&]{analyzeNameLookups(lp,li);},"invalid source name CRC accepted");
        lan[0].first^=1;
        la[1].name="LOOKUP_IOAQYHT9HSJ4";
        lc[1]={"OBJECT","LOOKUP_MC32E53Z7H9W",0,{}};
        lan=namesFor(la);lcn=namesFor(lc);li={{{la,lan},{lc,lcn}}};
        const auto collision=analyzeNameLookups(allocate({lb,lbn},li),li);
        check(collision.ambiguousNamedCrcs==std::vector<uint32_t>{0x5B4D7946} &&
              collision.plannedCandidates.at(0x5B4D7946)==std::vector<uint32_t>({1,2}) &&
              collision.sources[0].changed.size()==1 && collision.sources[1].changed.size()==1,
              "distinct names sharing a CRC were treated as independent lookups");
    }
    // Source-local references must be normalized before version comparison.
    const auto schema = defschema::Schema::loadText(R"({
        "SCALAR":{"prefix_len":3,"fields":[{"name":"Index","type":"CDefIndex"}]}
    })");
    auto payload = [](uint32_t reference) {
        std::vector<uint8_t> bytes{1,0,0};
        for (auto word : {defdecode::fieldTag("Index"), reference})
            for (unsigned i=0;i<4;++i) bytes.push_back(uint8_t(word>>(i*8)));
        return bytes;
    };
    std::vector<bin::Entry> prepBase{{"SCALAR","BASE",0,payload(UINT32_MAX)}};
    auto pa=prepBase, pb=prepBase;
    pa.push_back({"SCALAR","A",1,payload(2)});
    pa.push_back({"SCALAR","SHARED",2,payload(2)});
    pb.push_back({"SCALAR","SHARED",1,payload(1)});
    pb.push_back({"SCALAR","",2,payload(1)});
    std::vector<Name> pn{{1,"SCALAR"},{2,"BASE"}};
    std::array<Input,2> prepInputs{{{pa,pn},{pb,pn}}};
    const auto prepared=prepare({prepBase,pn},prepInputs,schema);
    check(prepared.sources[0][2].targetSlot==2 && prepared.sources[1][1].targetSlot==2,
          "shared contributors retain separate source ownership at one output slot");
    check(prepared.sources[0][2].entry.data==prepared.sources[1][1].entry.data &&
          prepared.sources[1][1].entry.data==payload(2),
          "equivalent source-local references agree after relocation");
    check(prepared.sources[1][1].entry.indexInDefinition==2 &&
          prepared.sources[1][2].targetSlot==3 &&
          prepared.sources[1][2].entry.indexInDefinition==3 &&
          prepared.sources[1][2].entry.data==payload(2),
          "anonymous contributor gets planned identity and its own relocated payload");
    pb[1].data=payload(UINT32_MAX);
    const auto conflicting=prepare({prepBase,pn},prepInputs,schema);
    check(conflicting.sources[0][2].entry.data!=conflicting.sources[1][1].entry.data,
          "preparation preserves real conflicts instead of choosing a winner");
    check(pb[2].data==payload(1) && pb[2].indexInDefinition==2,
          "preparation leaves source references and ordinals unchanged");
    std::array<Input,1> shorter{{{std::span(prepBase).first(0),pn}}};
    check(prepare({prepBase,pn},shorter,schema).sources[0].empty(),
          "short source does not contribute fabricated baseline rows");
    pb[2].data={1,0,0,0}; // moved ID in an unrecognized payload
    rejects([&] { prepare({prepBase,pn},prepInputs,schema); },
          "unknown candidate refuses preparation of the entire source set");
    check(pb[2].data==std::vector<uint8_t>({1,0,0,0}) && pa[2].data==payload(2),
          "failed preparation preserves all inputs");
    return failures ? 1 : 0;
}
