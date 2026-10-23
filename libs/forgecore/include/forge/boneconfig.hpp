#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace forge::boneconfig {
struct Bone {
    size_t offset = 0;
    std::string name;
    uint32_t crc = 0;
    std::array<float,3> scale{1,1,1};
};
struct Config {
    size_t bodyBegin = 0, bodyEnd = 0;
    std::vector<Bone> rows; // source order, including identity rows
};
// Read the verified CRLF .bncfg subset: one named XYZ scale per line between
// exact bone-data markers. Unknown header/group text and input remain untouched.
// Bounded to 1 MiB, 4096 rows, 255-byte printable ASCII names, finite decimals.
// This is not the general native CStringParser grammar or skeleton deformation.
Config read(std::string_view text);
// Native target preparation: remove exact identity scales, sort by unsigned CRC.
// Duplicate CRCs (including collisions) refuse; native tie behavior is unverified.
std::vector<Bone> prepare(const Config& config);
} // namespace forge::boneconfig
