#pragma once
#include <cstdint>
#include <vector>

namespace forge::stbstorage {
// Validate complete decoded terrain-frame storage domains, including nested
// range-coded geometry, water and edge arrays. Throws on unsupported/malformed
// layouts. Does not certify STB frame ownership, archive metadata, local detail,
// asset-ID destinations or geometry quality. Input bytes remain unchanged.
void validateForeground(const std::vector<uint8_t>& body);
// Supported background subset: one inline DXT1 texture, native detail-mode
// selection of shared/local indices, or water-only framing.
void validateBackground(const std::vector<uint8_t>& body);
}
