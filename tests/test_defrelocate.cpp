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
