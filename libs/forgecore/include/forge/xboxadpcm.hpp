#pragma once
// Xbox IMA ADPCM (WAVE fmt 0x0069) decoder used by TLC dialogue .lut and
// sound .lug banks. One 36-byte block per channel yields 64 PCM frames.

#include <cstdint>
#include <span>
#include <vector>

namespace forge::xboxadpcm {

// Returns interleaved PCM16. Rejects partial blocks or unsupported layouts.
std::vector<int16_t> decode(std::span<const uint8_t> bytes,
                            uint16_t channels,uint16_t blockAlign);

} // namespace forge::xboxadpcm
