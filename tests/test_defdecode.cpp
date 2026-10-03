#include "forge/defdecode.hpp"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <exception>

int main() {
    int failures = 0;
    auto check = [&](bool okay, const char* label) {
        if (!okay) { ++failures; std::fprintf(stderr, "def value: %s\n", label); }
    };
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
    return failures ? 1 : 0;
}
