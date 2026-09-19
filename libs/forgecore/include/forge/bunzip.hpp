#pragma once
// Minimal decompress-only bzip2 (BZh) reader. bsdiff `.patch` files (used by e.g.
// the Unofficial Fable Patch) store three bzip2 streams; FableForge vendors miniz
// (zlib) but not bzip2, so this provides just the inflate side.
//
// Supports the standard bzip2 stream: "BZh" + level, one or more compressed
// blocks (0x314159265359), MTF/RLE2 + multi-table Huffman + inverse BWT + RLE1,
// end-of-stream (0x177245385090). Randomized blocks (a deprecated bzip2 feature
// never emitted by real compressors) are not supported.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace forge::bunzip {

// Decompress a full bzip2 stream. Throws std::runtime_error on malformed input.
std::vector<uint8_t> decompress(const uint8_t* data, size_t len);

} // namespace forge::bunzip
