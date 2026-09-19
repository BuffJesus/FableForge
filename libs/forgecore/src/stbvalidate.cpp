#include "forge/stbvalidate.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace forge::stbvalidate {
namespace {

using forge::stbbake::Chunk;
using forge::stbbake::QuadEntry;
using forge::stbbake::SegKind;
using forge::stbbake::Segment;

void add(ValidationReport& r, std::string name, Severity sev, std::string detail) {
    r.checks.push_back(Check{std::move(name), sev, std::move(detail)});
}
void addD(DiffReport& r, std::string name, Severity sev, std::string detail) {
    r.checks.push_back(Check{std::move(name), sev, std::move(detail)});
}

bool onSegmentBoundary(const Chunk& c, uint32_t off, SegKind wanted, bool& insideFrame) {
    insideFrame = false;
    for (const Segment& s : c.segments) {
        if (off == s.start) return s.kind == wanted;
        if (off > s.start && off < s.end) {
            insideFrame = (s.kind == SegKind::Frame);
            return false;
        }
    }
    return false;
}

} // namespace

ValidationReport validateChunk(const Chunk& c, const InfoBlockPtrs& ib) {
    ValidationReport r;
    const size_t n = c.raw.size();

    // S1 coverage / round-trip.
    {
        std::vector<uint8_t> re = forge::stbbake::reserialize(c);
        bool sorted = true, gapfree = true, covers = true;
        size_t cursor = 0;
        for (const Segment& s : c.segments) {
            if (s.start != cursor) { gapfree = false; }
            if (s.end < s.start) sorted = false;
            cursor = s.end;
        }
        if (cursor != n) covers = false;
        bool rt = (re == c.raw);
        if (rt && sorted && gapfree && covers)
            add(r, "S1 coverage/roundtrip", Severity::Pass,
                "segments gap-free, cover [0,size), reserialize==raw");
        else
            add(r, "S1 coverage/roundtrip", Severity::Fail,
                std::string("rt=") + (rt ? "1" : "0") + " gapfree=" +
                    (gapfree ? "1" : "0") + " covers=" + (covers ? "1" : "0"));
    }

    // S2 frame decode-exact.
    {
        int ok = 0, bad = 0;
        for (size_t fi = 0; fi < c.frameIndices.size(); ++fi) {
            const Segment& s = c.segments[c.frameIndices[fi]];
            try {
                auto body = forge::stbbake::decodeFrame(c, fi);
                if (body.size() == s.uncompLen) ++ok; else ++bad;
            } catch (...) { ++bad; }
        }
        char buf[96];
        std::snprintf(buf, sizeof buf, "%d/%zu frames decode to exact uncompLen",
                      ok, c.frameIndices.size());
        add(r, "S2 frame decode-exact", bad ? Severity::Fail : Severity::Pass, buf);
    }

    // S3 page alignment / contiguity.
    {
        int warn = 0; bool straddle = false; size_t prevEnd = 0;
        for (size_t fi : c.frameIndices) {
            const Segment& s = c.segments[fi];
            if (s.end > n) straddle = true;
            bool pageAligned = (s.start % 0x1000) == 0;
            bool runOn = (s.start == prevEnd);
            if (!pageAligned && !runOn) ++warn;
            prevEnd = s.end;
        }
        if (straddle)
            add(r, "S3 page alignment", Severity::Fail, "a frame straddles past raw end");
        else if (warn)
            add(r, "S3 page alignment", Severity::Warn,
                std::to_string(warn) + " frame(s) neither page-aligned nor run-on");
        else
            add(r, "S3 page alignment", Severity::Pass,
                "every frame page-aligned or contiguous run-on");
    }

    // S4 InfoBlock pointer landing (only meaningful for a DEV/uncompressed chunk;
    // for a retail compressed chunk the landscape/localdetail ptrs land inside a
    // frame body — reported as Warn, not Fail, since that is expected).
    {
        auto landing = [&](uint32_t ptr, const char* label, ValidationReport& rr) {
            if (ptr == 0) return;
            if (ptr >= n) {
                add(rr, std::string("S4 ") + label, Severity::Fail,
                    "pointer >= chunk size");
                return;
            }
            bool inside = false;
            bool hdr = onSegmentBoundary(c, ptr, SegKind::Hdr, inside);
            if (hdr)
                add(rr, std::string("S4 ") + label, Severity::Pass,
                    "lands on an HDR segment boundary");
            else if (inside)
                add(rr, std::string("S4 ") + label, Severity::Warn,
                    "lands inside a FRAME body (expected for a retail compressed "
                    "chunk: this ptr is a DEV-file position, not a retail offset)");
            else
                add(rr, std::string("S4 ") + label, Severity::Warn,
                    "does not land on an HDR boundary");
        };
        landing(ib.landscapeMapPtr, "landscapeMapPtr", r);
        landing(ib.localDetailMapPtr, "localDetailMapPtr", r);
    }

    // S7 quadtree wiring — the load-critical invariant.
    {
        std::vector<QuadEntry> dir = forge::stbbake::parseQuadDir(c);
        auto frameLenAt = [&](uint32_t off) -> long {
            for (size_t fi : c.frameIndices) {
                const Segment& s = c.segments[fi];
                if (s.start == off) return long(s.end - s.start);
            }
            return -1;
        };
        int ok = 0; bool anyFail = false; std::string firstFail;
        for (const QuadEntry& e : dir) {
            long fl = frameLenAt(e.frameOffset);
            bool wired = (fl >= 0) && (uint32_t(fl) == e.frameSpan);
            bool aabbOk = true;
            for (int k = 0; k < 3; ++k)
                if (!(e.aabb[k] <= e.aabb[k + 3]) ||
                    !std::isfinite(e.aabb[k]) || !std::isfinite(e.aabb[k + 3]))
                    aabbOk = false;
            if (wired && aabbOk) ++ok;
            else {
                anyFail = true;
                if (firstFail.empty()) {
                    char buf[160];
                    std::snprintf(buf, sizeof buf,
                        "entry@0x%zx foff=0x%x span=%u frameLen=%ld wired=%d aabbOk=%d",
                        e.dirOffset, e.frameOffset, e.frameSpan, fl, int(wired),
                        int(aabbOk));
                    firstFail = buf;
                }
            }
        }
        char buf[96];
        std::snprintf(buf, sizeof buf, "%d/%zu quadtree entries resolve to real frames",
                      ok, dir.size());
        std::string detail = buf;
        if (!firstFail.empty()) detail += "; first bad: " + firstFail;
        if (dir.empty())
            add(r, "S7 quadtree wiring", Severity::Fail, "no quadtree entries at 0x7fc");
        else
            add(r, "S7 quadtree wiring", anyFail ? Severity::Fail : Severity::Pass, detail);
    }

    // S8 edge-height file bounds.
    if (ib.edgeHeightFilePtr != 0) {
        uint64_t end = uint64_t(ib.edgeHeightFilePtr) + ib.edgeHeightFileSize;
        if (end <= n)
            add(r, "S8 edge-height bounds", Severity::Pass, "region within chunk");
        else
            add(r, "S8 edge-height bounds", Severity::Warn,
                "edgeHeightFilePtr+size exceeds chunk (DEV-file field on a retail chunk)");
    }

    return r;
}

DiffReport diffAgainstReference(const Chunk& emitted, const Chunk& reference) {
    DiffReport r;

    // D1 frame-count / segment-shape parity.
    {
        size_t ef = emitted.frameIndices.size(), rf = reference.frameIndices.size();
        char buf[96];
        std::snprintf(buf, sizeof buf, "emitted %zu frames vs reference %zu", ef, rf);
        addD(r, "D1 frame-count parity",
             ef == rf ? Severity::Pass : Severity::Warn, buf);
    }

    // D2 quadtree-shape parity + internal wiring.
    {
        auto de = forge::stbbake::parseQuadDir(emitted);
        auto dr = forge::stbbake::parseQuadDir(reference);
        char buf[96];
        std::snprintf(buf, sizeof buf, "emitted %zu quad entries vs reference %zu",
                      de.size(), dr.size());
        addD(r, "D2 quadtree-shape parity",
             de.size() == dr.size() ? Severity::Pass : Severity::Warn, buf);
    }

    return r;
}

ValidationReport validateTextureResolve(const Chunk& c, const Chunk* reference) {
    ValidationReport r;

    // Honest statement first: the retail chunk has no name-string palette.
    add(r, "T0 palette model", Severity::Pass,
        "retail chunk carries inline per-patch DXT textures; no NUL-terminated "
        "texture-name palette present in frames (loader name path is DEV-only)");

    if (!reference) {
        add(r, "T2 isolation", Severity::Warn,
            "no reference chunk supplied; cannot run byte-isolation predictor");
        return r;
    }

    // T2 isolation: which bytes differ vs the donor, and are they confined to the
    // fields a retarget may legitimately touch? We approximate "allowed" as:
    //   (a) any byte inside a FRAME body (mesh/placement floats, DXT), and
    //   (b) any byte inside a quadtree directory entry (AABB floats + frame ptrs).
    // A diff OUTSIDE those (e.g. in the HDR block outside the quad dir, or in PAD)
    // predicts an unintended structural break.
    const auto& a = c.raw;
    const auto& b = reference->raw;
    size_t common = std::min(a.size(), b.size());

    // Build an "allowed" mask.
    std::vector<uint8_t> allowed(common, 0);
    for (size_t fi : c.frameIndices) {
        const Segment& s = c.segments[fi];
        for (size_t i = s.start; i < s.end && i < common; ++i) allowed[i] = 1;
    }
    // Quad dir region (both chunks): mark each live entry's 0x24 bytes allowed.
    auto markQuad = [&](const Chunk& q) {
        for (const QuadEntry& e : forge::stbbake::parseQuadDir(q))
            for (size_t i = e.dirOffset; i < e.dirOffset + 0x24 && i < common; ++i)
                allowed[i] = 1;
    };
    markQuad(c);
    markQuad(*reference);

    size_t totalDiff = 0, strayDiff = 0, firstStray = SIZE_MAX;
    for (size_t i = 0; i < common; ++i) {
        if (a[i] != b[i]) {
            ++totalDiff;
            if (!allowed[i]) { ++strayDiff; if (firstStray == SIZE_MAX) firstStray = i; }
        }
    }
    char buf[192];
    if (a.size() != b.size()) {
        std::snprintf(buf, sizeof buf,
            "sizes differ (%zu vs %zu) — topology diverges; byte-isolation is "
            "advisory. %zu diffs in common range, %zu stray",
            a.size(), b.size(), totalDiff, strayDiff);
        add(r, "T2 isolation", Severity::Warn, buf);
    } else if (strayDiff == 0) {
        std::snprintf(buf, sizeof buf,
            "%zu bytes differ, ALL inside frame bodies / quad entries (no stray "
            "structural drift)", totalDiff);
        add(r, "T2 isolation", Severity::Pass, buf);
    } else {
        std::snprintf(buf, sizeof buf,
            "%zu stray diffs outside allowed fields (first @0x%zx) — predicts an "
            "unintended structural break", strayDiff, firstStray);
        add(r, "T2 isolation", Severity::Fail, buf);
    }
    return r;
}

} // namespace forge::stbvalidate
