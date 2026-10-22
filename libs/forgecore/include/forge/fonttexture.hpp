#pragma once
#include "forge/texturewrite.hpp"
#include <cstdint>
#include <vector>

namespace forge::fonttexture {
// Static FONT_* payloads only. Retains logical metrics and native sampler UVs;
// supports uncompressed true-colour TGA atlases and proportional resizing.
// Retail vertex evidence: tools/static_font_atlas_probe.py, 0xAB7E0B..0xAB824A.
std::vector<uint8_t> replaceStaticAtlas(const std::vector<uint8_t>& original,
                                      const texturewrite::Image& image);
}
