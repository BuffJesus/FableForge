#pragma once
// forge::rangecodec — CRangeCompressor, the inner per-element codec the STB
// landscape patch bodies wrap the vertex/index buffers in (distinct from the
// outer LZO1X frame). Ported from CRangeCompressor::Decompress @0x00f39ed0 and
// its RAW/stored path.
//
// On-disk block (what the patch writer wraps as [s32 compLen][block]):
//   byte[0] = flags.
//     flags == 0x00  -> RAW/stored: byte[1..] = count*stride raw element bytes.
//     flags != 0     -> per-column range/bit-packed stream (decode below).
//   A compressed stream is a sequence of column descriptors terminated when a
//   descriptor byte has its high bit set (< 0 as signed).
//
// For AUTHORING, forge emits RAW blocks (encodeRaw): the engine's decoder
// reproduces them byte-exact and no arithmetic-coder parity is needed. decode()
// is the full port so forge can READ donor blocks (to preserve UV/grid fields
// when rewriting only heights).

#include <cstdint>
#include <string>
#include <vector>

namespace forge::rangecodec {

// Decode a CRangeCompressor block into `count` elements of `stride` bytes each
// (stride ∈ {1,2,4}). `block` points at the flags byte; `blockLen` is its length.
// Returns count*stride decoded bytes. Throws std::runtime_error on malformed input.
std::vector<uint8_t> decode(const uint8_t* block, size_t blockLen, size_t count, size_t stride);

// Emit a RAW/stored block for `count*stride` element bytes: [0x00][raw bytes].
// This is the byte-exact form the engine's decoder accepts on the flags==0 path.
std::vector<uint8_t> encodeRaw(const uint8_t* elems, size_t count, size_t stride);
inline std::vector<uint8_t> encodeRaw(const std::vector<uint8_t>& v) {
    return encodeRaw(v.data(), v.size(), 1);
}

// Emit a valid compressed block using one independently bit-packed byte column
// per record byte.  This is intentionally simpler than the editor's optimizer,
// but uses the same decoder grammar and is compact enough for fixed-span donor
// patch edits.  The returned block includes the leading non-zero codec byte and
// the terminating 0x80 descriptor.
std::vector<uint8_t> encodeByteColumns(const uint8_t* elems, size_t count, size_t stride);

// Encode a record as typed 1/2/4-byte columns.  Constant bits are carried by
// the decoder's OR field and only varying bits are packed, which closely matches
// the compactness needed by retail landscape VBs without cloning the editor's
// full search heuristic.  fieldWidths must sum to stride.
std::vector<uint8_t> encodeFieldColumns(const uint8_t* elems, size_t count, size_t stride,
                                        const std::vector<size_t>& fieldWidths);

// Replace an output byte-column range in `authored` with the exact descriptor
// chunks from `donor`.  Both streams must decode the same count/stride and no
// descriptor may cross the replacement boundary.  Used to preserve the retail
// landscape normal column while authoring position/height columns.
std::vector<uint8_t> spliceColumnRange(const std::vector<uint8_t>& authored,
                                       const uint8_t* donor, size_t donorLen,
                                       size_t count, size_t stride,
                                       size_t replaceOffset, size_t replaceLength);

// Add a constant to one exact 1/2/4-byte descriptor column by adjusting the
// retail stream's encoded bias in place. The block length and every packed
// residual bit remain unchanged. Throws unless one descriptor exactly covers
// the requested column and uses one unambiguous pre- or post-add bias. The
// result is decoded and verified before it is returned.
std::vector<uint8_t> addColumnConstant(const uint8_t* block, size_t blockLen,
                                       size_t count, size_t stride,
                                       size_t columnOffset, size_t columnWidth,
                                       int32_t delta);

// The editor's own compressor, ported from FableWin CRangeCompressor::Compress
// (column split search, five per-column transforms, dword bit-packing, RAW
// fallback). Reproduces retail landscape VB/IB blocks byte-for-byte from their
// decoded elements, so an edited block packs exactly like the donor did.
std::vector<uint8_t> encodeNative(const uint8_t* elems, size_t count, size_t stride);

// Diagnostic: the five candidate costs (bytes: packed values + descriptor) the native
// encoder weighs for one block of `blockSize` bytes at `offset`, and the split total.
std::string debugBlockCosts(const uint8_t* elems, size_t count, size_t stride, size_t offset, size_t blockSize);

} // namespace forge::rangecodec
