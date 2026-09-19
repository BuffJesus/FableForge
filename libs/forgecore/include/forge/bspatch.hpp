#pragma once
// Apply a bsdiff `BSDIFF40` binary patch (as used by the Unofficial Fable Patch:
// `game.bin.patch`, `FinalAlbion.wad.patch`) to a base file, producing the
// modified file. Patch = 32-byte header + three bzip2 streams (control, diff,
// extra); see forge::bunzip for the decompressor.
//
// Consuming these lets FableForge ingest bsdiff-distributed mods: apply the patch
// to the vanilla container, then `forge defs diff` the result to recover a
// record-level change set for the merge engine.

#include <cstdint>
#include <string>
#include <vector>

namespace forge::bspatch {

struct Header {
    uint64_t ctrlLen = 0;   // compressed control-block length
    uint64_t diffLen = 0;   // compressed diff-block length
    uint64_t newSize = 0;   // size of the produced (new) file
};

// Parse and validate a BSDIFF40 header. Throws on bad magic / short buffer.
Header readHeader(const std::vector<uint8_t>& patch);

// Apply `patch` to `oldFile`, returning the new file (length == header.newSize).
// Throws std::runtime_error on malformed input.
std::vector<uint8_t> apply(const std::vector<uint8_t>& oldFile,
                           const std::vector<uint8_t>& patch);

} // namespace forge::bspatch
