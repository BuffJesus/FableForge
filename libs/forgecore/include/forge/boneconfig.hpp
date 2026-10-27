#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
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
struct WeightedTarget { std::span<const Bone> rows; float amount = 1; };
// Combine prepared targets in supplied parameter order. Supports the nonnegative
// random-appearance range [0,1]; negative-target selection remains separate.
// Each arithmetic step rounds to float32, matching the native vector methods.
// Output is CRC-sorted; names/offsets come from the first contributing target.
// Cross-target CRC/name collisions, malformed targets and nonfinite results refuse.
std::vector<Bone> combine(std::span<const WeightedTarget> targets);
// Resolve combined scales into mesh-local bone order by name CRC. Missing keys
// produce identity, including when the combined target is empty. No skinning.
std::vector<std::array<float,3>> mapScales(std::span<const Bone> combined,
                                       std::span<const uint32_t> meshBoneCrcs);
} // namespace forge::boneconfig
