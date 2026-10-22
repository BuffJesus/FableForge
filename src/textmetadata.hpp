#pragma once
#include <algorithm>
#include <optional>
#include <string>
#include <vector>
#include "forge/textbig.hpp"
namespace albion::textwrite {
struct MetadataChanges {
    std::optional<std::string> speaker,speechBank,identifier;
    std::optional<std::vector<forge::textbig::Tag>> tags;
};
inline bool editableMetadata(const std::string& value) {
    return value.size()<4096 && std::all_of(value.begin(),value.end(),[](unsigned char c){return c!=0 && c<128;});
}
} // namespace albion::textwrite
