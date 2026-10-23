#include "forge/defdecode.hpp"
#include "forge/modorder.hpp"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <charconv>
#include <cstring>
#include <exception>
#include <stdexcept>

int main() {
    int failures = 0;
    auto check = [&](bool okay, const char* label) {
        if (!okay) { ++failures; std::fprintf(stderr, "def value: %s\n", label); }
    };
    check(forge::modorder::sha256OfBytes("") ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "empty layout bytes use standard SHA256");
    check(forge::modorder::sha256OfBytes("abc") ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "layout identity SHA256 matches known vector");
    for (const auto* type : {"CCharString"}) {
        forge::defdecode::DecodedField field;
        field.type = type;
        field.value = {'A', '"', '\\', '\n', '\r', '\t', 1, 0x7f, 0xff, 0, 'Z'};
        check(forge::defdecode::formatValue(field) ==
              "\"A\\\"\\\\\\n\\r\\t\\x01\\x7f\\xff\"", "byte escapes and NUL termination");
        field.value = {};
        check(forge::defdecode::formatValue(field) == "\"\"", "empty string");
        field.value = {'p', 'l', 'a', 'i', 'n', 0};
        check(forge::defdecode::formatValue(field) == "\"plain\"", "printable text unchanged");
        field.value.clear();
        for (unsigned byte = 1; byte <= 255; ++byte) field.value.push_back(uint8_t(byte));
        const auto formatted = forge::defdecode::formatValue(field);
        bool ascii = true;
        for (unsigned char byte : formatted) ascii = ascii && byte >= 0x20 && byte < 0x7f;
        check(ascii, "every non-NUL input byte has printable ASCII presentation");
        try {
            const auto json = nlohmann::json{{"value", formatted}}.dump();
            check(nlohmann::json::parse(json).at("value").get<std::string>() == formatted,
                  "strict JSON preserves formatted byte representation");
        } catch (const std::exception& error) {
            ++failures; std::fprintf(stderr, "def value: JSON failed: %s\n", error.what());
        }
    }
    const std::vector<std::pair<std::vector<uint8_t>,std::string>> wideCases={
        {{0,0},""},{{65,0,0,0},"A"},{{0xe9,0,0,0},"\xc3\xa9"},
        {{0x22,0x6f,0,0},"\xe6\xbc\xa2"},{{0x3d,0xd8,0x42,0xde,0,0},"\xf0\x9f\x99\x82"},
        {{'A',0,'"',0,'\\',0,'\n',0,'\t',0,1,0,0x7f,0,0,0},std::string("A\"\\\n\t\x01\x7f")}
    };
    for(const auto& [bytes,text]:wideCases) {
        forge::defdecode::DecodedField wide;wide.type="CWideString";wide.value=bytes;
        const auto formatted=forge::defdecode::formatValue(wide);
        try {check(nlohmann::json::parse(formatted).get<std::string>()==text,"wide text is a valid exact quoted UTF-8 value");}
        catch(const std::exception&) {check(false,"wide formatter produced invalid JSON string syntax");}
        check(wide.value==bytes,"wide display modified source bytes");
    }
    const std::vector<std::pair<std::vector<uint8_t>,std::string>> malformedWide={
        {{},"<empty>"},{{65},"41"},{{65,0},"4100"},{{65,0,0,0,66,0,0,0},"4100000042000000"},
        {{0,0xd8,0,0},"00d80000"},{{0,0xdc,0,0},"00dc0000"},{{0,0xd8,65,0,0,0},"00d841000000"}
    };
    for(const auto& [bytes,expected]:malformedWide) {
        forge::defdecode::DecodedField wide;wide.type="CWideString";wide.value=bytes;
        check(forge::defdecode::formatValue(wide)==expected,"unsupported wide payload retains complete raw display");
    }
    forge::defdecode::DecodedField number;
    number.type = "int32";
    number.value = {0xff, 0xff, 0xff, 0xff};
    check(forge::defdecode::formatValue(number) == "-1", "typed scalar unchanged");
    number.type = "CDefString";
    check(forge::defdecode::formatValue(number) == "-1", "absent name-table reference");
    number.value = {0x00, 0x01, 0x00, 0x00};
    check(forge::defdecode::formatValue(number) == "256", "reference with embedded zero bytes");
    number.value = {0xff, 0xff, 0xff, 0x7f};
    check(forge::defdecode::formatValue(number) == "2147483647", "largest signed reference");
    number.value = {0x41, 0x42, 0x43};
    check(forge::defdecode::formatValue(number) == "414243", "malformed reference retains raw bytes");
    std::string previousFloat;
    for (uint32_t bits : {0x41a00000u, 0x41a00001u, 0x3eaaaaabu, 0xbf800000u,
                          0x80000000u, 0x7f7fffffu, 0x00800000u, 0x00000001u}) {
        forge::defdecode::DecodedField floating;
        floating.type = "float"; floating.value.resize(4);
        std::memcpy(floating.value.data(), &bits, 4);
        const auto original = floating.value;
        const auto formatted = forge::defdecode::formatValue(floating);
        float parsed = 0;
        const auto result = std::from_chars(formatted.data(), formatted.data() + formatted.size(), parsed);
        uint32_t recovered = 0; std::memcpy(&recovered, &parsed, 4);
        check(result.ec == std::errc{} && result.ptr == formatted.data() + formatted.size() && recovered == bits,
              "finite float display round-trips exact bits, including signed zero and subnormal");
        check(floating.value == original, "float presentation preserves source bytes");
        if (bits == 0x41a00001u) check(formatted != previousFloat, "adjacent floats remain distinguishable");
        previousFloat = formatted;
    }
    forge::defdecode::Decoded base;
    base.allTagsOk = true;
    base.prefix = {1};
    for (uint32_t tag : {1u, 2u}) {
        forge::defdecode::DecodedField field;
        field.type = "bool"; field.tag = tag; field.tagOk = true; field.value = {1};
        base.fields.push_back(field);
    }
    auto first = base, last = base;
    first.prefix = {2}; last.prefix = {3};
    first.fields[0].value = {2}; last.fields[0].value = {3};
    first.fields[1].value = {4}; last.fields[1].value = {5};
    const auto merged = forge::defdecode::mergeFields(base, {{"first",first},{"last",last}});
    check(merged.ok && merged.conflicts.size() == 3, "prefix and duplicate unnamed conflicts retained");
    if (merged.conflicts.size() == 3) {
        check(merged.conflicts[0].partIndex == 0 && merged.conflicts[1].partIndex == 1 &&
              merged.conflicts[2].partIndex == 2, "conflict identity does not depend on display name");
    }
    check(merged.payload == std::vector<uint8_t>({3,1,0,0,0,3,2,0,0,0,5}),
          "conflict identity preserves merged payload");
    check(merged.sourceVersions == std::vector<std::optional<size_t>>({1,1,1}),
          "last changing source owns each selected conflicting part");
    const auto vanilla = forge::defdecode::mergeFields(base, {{"first",first},{"last",last}}, "vanilla");
    check(vanilla.sourceVersions == std::vector<std::optional<size_t>>(3),
          "vanilla conflicting parts carry no mod symbols");
    auto independent = last; independent.fields[0] = base.fields[0];
    const auto chosen = forge::defdecode::mergeFields(base, {{"first",first},{"last",independent}}, "last");
    check(chosen.sourceVersions == std::vector<std::optional<size_t>>({1,0,1}),
          "choice owns conflicts; independent edit retains its actual source");
    const auto agreement = forge::defdecode::mergeFields(base, {{"first",first},{"last",first}});
    check(agreement.sourceVersions == std::vector<std::optional<size_t>>({0,0,0}),
          "agreeing changed parts retain their first equivalent source");
    const auto mixed = forge::defdecode::mergeFields(base, {{"first",first},{"last",last}},
                                                    "last", {{0,"first"},{1,"vanilla"},{2,"first"}});
    check(mixed.ok && mixed.payload == std::vector<uint8_t>({2,1,0,0,0,1,2,0,0,0,4}),
          "part choices independently override record choice, including prefix and unnamed parts");
    check(mixed.sourceVersions == std::vector<std::optional<size_t>>({0,std::nullopt,0}),
          "part-selected symbols follow chosen bytes, baseline retains no mod source");
    const auto absent = forge::defdecode::mergeFields(base, {{"first",first},{"last",last}},
                                                     "first", {{1,"missing"}});
    check(absent.payload == forge::defdecode::mergeFields(base, {{"first",first},{"last",last}}, "first").payload,
          "unavailable part source falls back to record choice");
    const auto unchanged = forge::defdecode::mergeFields(base,
        {{"first",first},{"last",last},{"unchanged",base}}, "first", {{1,"unchanged"}});
    check(unchanged.payload == absent.payload && unchanged.sourceVersions[1] == 0,
          "selected mod without an edit of a contested part falls back to record choice");
    const auto cleared = forge::defdecode::mergeFields(base, {{"first",first},{"last",last}},
                                                      "last", {{0,"first"},{1,"vanilla"}});
    check(cleared.payload == std::vector<uint8_t>({2,1,0,0,0,1,2,0,0,0,5}) &&
          cleared.sourceVersions == std::vector<std::optional<size_t>>({0,std::nullopt,1}),
          "clearing one part choice returns only that part to the record default");
    auto mismatched = last; mismatched.fields[0].tag = 99;
    check(!forge::defdecode::mergeFields(base, {{"first",first},{"last",mismatched}},
                                       "first", {{1,"last"}}).ok,
          "part choices cannot bypass the existing incompatible-layout refusal");
    const auto unique = forge::defdecode::mergeFields(base, {{"first",first},{"last",independent}},
                                                     "last", {{1,"vanilla"}});
    check(!unique.sourceVersions[1] && unique.payload[5] == 1,
          "saved baseline part choice survives collapse to one changing source");
    const auto same = forge::defdecode::mergeFields(base, {{"first",first},{"last",first}},
                                                   "last", {{1,"vanilla"}});
    check(same.payload[5] == 1 && !same.sourceVersions[1],
          "saved baseline part choice survives agreement between sources");
    const auto single = forge::defdecode::mergeFields(base, {{"first",first}}, {}, {{1,"missing"}});
    check(single.payload == forge::defdecode::encode(first) && single.conflicts.size() == 1 &&
          single.conflicts[0].mods == std::vector<std::string>{"first"},
          "missing saved source remains reviewable with one surviving writer");
    const auto samePick = forge::defdecode::mergeFields(base, {{"first",first},{"last",first}}, {}, {{1,"last"}});
    check(samePick.payload == agreement.payload && samePick.sourceVersions[1] == 1,
          "agreeing sources retain explicitly selected metadata ownership");
    const auto noChange = forge::defdecode::mergeFields(base, {{"first",base}}, {}, {{1,"missing"}});
    check(noChange.payload == forge::defdecode::encode(base) && noChange.conflicts.size() == 1 &&
          noChange.conflicts[0].mods.empty() && !noChange.sourceVersions[1],
          "saved source with no remaining field edits remains reviewable");
    bool refused = false;
    try { (void)forge::defdecode::mergeFields(base, {{"first",first}}, {}, {{3,"first"}}); }
    catch (const std::invalid_argument&) { refused = true; }
    check(refused, "invalid part index refuses rather than selecting another field");
    {
        forge::defdecode::DecodedField field;
        field.name="Components";field.tag=forge::defdecode::fieldTag(field.name);field.tagOk=true;
        auto word=[](std::vector<uint8_t>& bytes,uint32_t value) {
            for(unsigned i=0;i<4;++i) bytes.push_back(uint8_t(value>>(8*i)));
        };
        word(field.value,2);word(field.value,17);word(field.value,99);field.value.push_back(0);
        word(field.value,42);word(field.value,0);field.value.push_back(1);word(field.value,0x01ab6cce);
        check(forge::defdecode::runtimeComponentNameOffsets(field)==std::vector<uint32_t>({17,42}),
              "runtime component names use the recovered nine-byte stride");
        for(int kind=0;kind<5;++kind) {
            auto bad=field;
            if(kind==0) bad.value[0]=3;
            if(kind==1) bad.value.pop_back();
            if(kind==2) bad.value.back()=0;
            if(kind==3) bad.tagOk=false;
            if(kind==4) for(int i=0;i<4;++i) bad.value[i]=0xff;
            bool rejected=false;
            try{(void)forge::defdecode::runtimeComponentNameOffsets(bad);}catch(const std::invalid_argument&){rejected=true;}
            check(rejected,"malformed runtime component list refuses before use");
        }
        field.value.clear();word(field.value,0);word(field.value,0x01ab6cce);
        check(forge::defdecode::runtimeComponentNameOffsets(field).empty(),"valid empty runtime component list");
    }
    return failures ? 1 : 0;
}
