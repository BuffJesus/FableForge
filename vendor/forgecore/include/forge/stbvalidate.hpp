#pragma once
// forge::stbvalidate — offline pass/fail gate for STB baked-landscape chunks.
//
// Wraps forge::stbbake's segment model + quadtree parser into named invariant
// checks so an emitted / retargeted chunk can be accepted or rejected WITHOUT the
// game. Three layers (per the oracle spec):
//   L1 structural acceptance  — round-trip, frame decode-exact, page alignment,
//                               quadtree wiring (the load-critical S7 invariant).
//   L2 oracle diff            — structural parity against a reference (donor) chunk.
//   L3 texture-resolve/isolation — a static predictor of the white-out: which bytes
//                               differ from the donor and whether the differences
//                               are confined to the fields a retarget is allowed to
//                               touch (placement floats, quad AABBs, pointers).
//
// NOTE ON THE WHITE-OUT (ground truth, this run): the retail baked chunk carries
// NO NUL-terminated texture-name palette in its frames — every foreground patch
// embeds its own DXT texture inline (CPixelFormatInit 0x…0463/04e3 blobs). The
// CEngineTexturePalette name-string path (Load @egor 0x00a43e30) is the loader
// grammar, but in these filler maps the palette is empty/disabled, so the
// texture-resolve check here is an ISOLATION predictor, not a name-residency one.

#include <cstdint>
#include <string>
#include <vector>

#include "forge/stbbake.hpp"

namespace forge::stbvalidate {

enum class Severity { Pass, Warn, Fail };

struct Check {
    std::string name;
    Severity sev = Severity::Pass;
    std::string detail;
};

struct ValidationReport {
    std::vector<Check> checks;
    bool ok() const {
        for (const auto& c : checks)
            if (c.sev == Severity::Fail) return false;
        return true;
    }
};

// The six InfoBlock pointer/size fields the chunk is cross-checked against.
// (Absolute byte offsets in the DEV static-map file; for a retail compressed
// chunk the landscape/localdetail pointers do NOT land on plaintext — see the
// header note. Pass 0 for a field to skip its landing check.)
struct InfoBlockPtrs {
    uint32_t landscapeMapPtr = 0;
    uint32_t localDetailMapPtr = 0;
    uint32_t edgeHeightFilePtr = 0;
    uint32_t edgeHeightFileSize = 0;
    uint32_t checksumBlockFilePtr = 0;
    uint32_t checksumBlockFileSize = 0;
};

// LAYER 1 — structural acceptance over a parsed chunk.
ValidationReport validateChunk(const forge::stbbake::Chunk& c, const InfoBlockPtrs& ib);

// LAYER 2 — structural parity against a reference (donor) chunk.
struct DiffReport {
    std::vector<Check> checks;
    bool ok() const {
        for (const auto& c : checks)
            if (c.sev == Severity::Fail) return false;
        return true;
    }
};
DiffReport diffAgainstReference(const forge::stbbake::Chunk& emitted,
                                const forge::stbbake::Chunk& reference);

// LAYER 3 — texture-resolve / retarget-isolation predictor. `reference` is the
// pristine donor; the check flags any byte that differs OUTSIDE the fields a
// legitimate retarget touches (frame bodies, quad AABB floats, quad frame
// pointers). Stray structural drift => Fail (predicts an unintended break).
ValidationReport validateTextureResolve(const forge::stbbake::Chunk& c,
                                        const forge::stbbake::Chunk* reference);

} // namespace forge::stbvalidate
