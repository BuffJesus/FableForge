#pragma once
// forge::lzo — thin wrapper over vendored minilzo 2.10 (third_party/minilzo).
//
// The STB baked-landscape codec (CLandscapeBackgroundPatch::SaveCompressed
// @0x02ce30c0) uses stock LZO1X: the engine writes lzo1x_999 and decodes with
// lzo1x_decompress @0x00c06b90. This was proven byte-exact against real retail
// FinalAlbion_RT.stb chunks (work/terrain_path/codec_foundation). We emit lzo1x_1
// (worse ratio, same standard decoder) — no encoder parity is needed to render.
//
// On-disk block frame (empirically pinned; matches SaveCompressed's two
// WriteSLONG calls): [uncomp_len : u32 LE][comp_len : u32 LE][ lzo1x body ].

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace forge::lzo {

// Raw LZO1X-1 compress. Returns the compressed body (no frame header).
std::vector<uint8_t> compress(const uint8_t* data, size_t len);
inline std::vector<uint8_t> compress(const std::vector<uint8_t>& v) {
    return compress(v.data(), v.size());
}

// Raw LZO1X-999 compression through a system liblzo2 implementation.  This is
// the encoder used by the retail terrain writer and is required when an edited
// frame must remain within the donor's original page allocation.
std::vector<uint8_t> compress999(const uint8_t* data, size_t len);

// Raw LZO1X decompress into a caller-known uncompressed length.
std::vector<uint8_t> decompress(const uint8_t* data, size_t len, size_t uncompLen);

// Non-throwing probe: true only when `data` decodes to EXACTLY `out.size()`
// bytes with no error (the frame-scanner gate in stbbake).
bool tryDecompress(const uint8_t* data, size_t len, std::vector<uint8_t>& out);
// Same probe into a caller-owned buffer of exactly `outLen` bytes (no fill).
bool tryDecompress(const uint8_t* data, size_t len, uint8_t* out, size_t outLen);

// Raw LZO1X decompress where only an UPPER BOUND on the output is known (the
// Fable chunked-texture stream stores no per-chunk uncompressed length; the
// bound is the bytes still owed by the mip surface). Returns exactly what the
// stream produced.
std::vector<uint8_t> decompressBounded(const uint8_t* data, size_t len,
                                       size_t maxOut);

// Emit one framed block: [uncompLen u32LE][compLen u32LE][lzo1x body].
// This is exactly the frame the retail loader walks.
std::vector<uint8_t> compressFramed(const uint8_t* data, size_t len);
inline std::vector<uint8_t> compressFramed(const std::vector<uint8_t>& v) {
    return compressFramed(v.data(), v.size());
}
std::vector<uint8_t> compressFramed999(const uint8_t* data, size_t len);
inline std::vector<uint8_t> compressFramed999(const std::vector<uint8_t>& v) {
    return compressFramed999(v.data(), v.size());
}

// Decode one framed block starting at frame[0]; advances `pos` past it.
// Throws on a malformed frame or decode failure.
std::vector<uint8_t> decompressFramed(const std::vector<uint8_t>& frame, size_t& pos);

} // namespace forge::lzo
