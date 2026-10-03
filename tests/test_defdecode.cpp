#include "forge/defdecode.hpp"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <exception>

int main() {
    int failures = 0;
    auto check = [&](bool okay, const char* label) {
        if (!okay) { ++failures; std::fprintf(stderr, "def value: %s\n", label); }
    };
    for (const auto* type : {"CCharString", "CDefString"}) {
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
    return failures ? 1 : 0;
}
