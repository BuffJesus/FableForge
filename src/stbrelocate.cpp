#include "stbrelocate.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <algorithm>
#include <climits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>

#include "forge/lzo.hpp"
#include "forge/rangecodec.hpp"
#include "forge/stbbake.hpp"
#include "forge/stbinfo.hpp"

namespace albion::editor {
namespace {

struct Walk {
    bool audit = false;
    int dx = 0, dy = 0;
    double xlo = 0, xhi = 0, ylo = 0, yhi = 0;   // audit box (with slack)
    RelocateReport* rep = nullptr;
    std::string where;

    double slackScale = 1.0;   // local-detail bounds reach past the map edge (tree radii): widened there
    // FNV-1a over every decoded record / body visited, in walk order: two
    // chunks with equal digests hold the same terrain data whatever their
    // range-block encodings or file-block layout (the round-trip oracle)
    uint64_t digest = 1469598103934665603ull;
    void mix(const uint8_t* p, size_t n) { for (size_t i = 0; i < n; ++i) { digest ^= p[i]; digest *= 1099511628211ull; } }
    void mix(const std::vector<uint8_t>& v) { mix(v.data(), v.size()); }
    void issue(const std::string& what, double v) {
        if (rep->issues.size() < 200) rep->issues.push_back(where + ": " + what + " = " + std::to_string(v));
    }
    std::vector<float>* sites = nullptr;   // audit: every coordinate visited, in walk order
    std::vector<uint8_t>* siteIsX = nullptr;
    void u16(uint8_t* p, bool isX, const char* what) {
        uint16_t v; std::memcpy(&v, p, 2);
        if (sites) { sites->push_back(float(v)); siteIsX->push_back(isX); }
        if (audit) {
            const double lo = isX ? xlo : ylo, hi = isX ? xhi : yhi;
            if (v < lo || v > hi) issue(what, v);
            return;
        }
        v = uint16_t(int(v) + (isX ? dx : dy));
        std::memcpy(p, &v, 2);
    }
    void f32(uint8_t* p, bool isX, const char* what) {
        float v; std::memcpy(&v, p, 4);
        if (sites) { sites->push_back(v); siteIsX->push_back(isX); }
        if (audit) {
            const double pad = 48.0 * (slackScale - 1.0);
            const double lo = (isX ? xlo : ylo) - pad, hi = (isX ? xhi : yhi) + pad;
            if (!std::isfinite(v) || v < lo || v > hi) issue(what, v);
            return;
        }
        v += float(isX ? dx : dy);
        std::memcpy(p, &v, 4);
    }
    // an (x, y) float pair
    void f32xy(uint8_t* p, const char* what) { f32(p, true, what); f32(p + 4, false, what); }
    void u16xy(uint8_t* p, const char* what) { u16(p, true, what); u16(p + 2, false, what); }
};

struct Cursor {
    std::vector<uint8_t>& d;
    size_t p = 0;
    explicit Cursor(std::vector<uint8_t>& v) : d(v) {}
    void need(size_t n, const char* what) const {
        if (p + n > d.size()) throw std::runtime_error(std::string("truncated ") + what);
    }
    uint32_t u32(const char* what) { need(4, what); uint32_t v; std::memcpy(&v, d.data() + p, 4); p += 4; return v; }
    int32_t i32(const char* what) { return int32_t(u32(what)); }
    uint16_t u16(const char* what) { need(2, what); uint16_t v; std::memcpy(&v, d.data() + p, 2); p += 2; return v; }
    uint8_t u8(const char* what) { need(1, what); return d[p++]; }
    float f32(const char* what) { need(4, what); float v; std::memcpy(&v, d.data() + p, 4); p += 4; return v; }
    uint8_t* at() { return d.data() + p; }
};

uint32_t rd32(const std::vector<uint8_t>& d, size_t at) {
    if (at + 4 > d.size()) throw std::runtime_error("read past end");
    uint32_t v; std::memcpy(&v, d.data() + at, 4); return v;
}

// A framed range block [i32 len][block] at cur.p holding `count` records of
// `stride` bytes: decode, let `edit` touch each record, re-encode with the
// editor's compressor. In audit mode the bytes are untouched. The rebuilt
// block is appended to `out`; cur advances past the original.
void rangeBlock(Walk& w, Cursor& cur, std::vector<uint8_t>& out, size_t count, size_t stride,
                const std::function<void(uint8_t*)>& edit, const char* what) {
    const int32_t len = cur.i32(what);
    if (len < 0) throw std::runtime_error(std::string("negative range block length in ") + what);
    cur.need(size_t(len), what);
    const uint8_t* block = cur.at();
    ++w.rep->rangeBlocks;
    std::vector<uint8_t> rec = forge::rangecodec::decode(block, size_t(len), count, stride);
    for (size_t i = 0; i < count; ++i) edit(rec.data() + i * stride);
    w.mix(rec);
    if (w.audit) {
        out.insert(out.end(), cur.at() - 4, cur.at() + size_t(len));
    } else {
        std::vector<uint8_t> enc = forge::rangecodec::encodeNative(rec.data(), count, stride);
        if (enc.size() != size_t(len)) ++w.rep->rangeBlocksResized;
        if (forge::rangecodec::decode(enc.data(), enc.size(), count, stride) != rec)
            throw std::runtime_error(std::string("re-encoded range block does not decode back (") + what + ")");
        const int32_t newLen = int32_t(enc.size());
        const uint8_t* lp = reinterpret_cast<const uint8_t*>(&newLen);
        out.insert(out.end(), lp, lp + 4);
        out.insert(out.end(), enc.begin(), enc.end());
    }
    cur.p += size_t(len);
}

void copyBytes(Cursor& cur, std::vector<uint8_t>& out, size_t n, const char* what) {
    cur.need(n, what);
    out.insert(out.end(), cur.at(), cur.at() + n);
    cur.p += n;
}

// ---- background patch trailer: 4 edge strips + optional water sub-patch ----
// CPatchTesselationEdgeStrip::Save: u16 start coordinate along the strip axis,
// u16 length, EBOOL, then four vertex arrays (i32 count, [i32 len][block]).
// Strips 0/1 run along Y (start = world Y), strips 2/3 along X (start = world X).
// CVertex (20 B): f32 height, u32 normal, u16 x, u16 y, ...; FanBaseVertex 16 B;
// WaterVertex 60 B and WaterFanBaseVertex 56 B carry no grid position (their
// place comes from the strip start + index; audited below).
void patchTrailer(Walk& w, std::vector<uint8_t>& trailer, std::vector<uint8_t>& out) {
    Cursor cur(trailer);
    static const size_t strides[4] = {0x14, 0x10, 0x3c, 0x38};
    for (int s = 0; s < 4; ++s) {
        cur.need(5, "edge strip header");
        w.u16(cur.at(), s >= 2, "edge strip start");
        copyBytes(cur, out, 5, "edge strip header");
        for (int arr = 0; arr < 4; ++arr) {
            const int32_t n = cur.i32("edge strip count");
            const uint8_t* np = reinterpret_cast<const uint8_t*>(&n);
            out.insert(out.end(), np, np + 4);
            if (n <= 0) continue;
            bool first = true;
            auto sample = [&](const char* kind, uint8_t* r) {
                if (!w.audit || !first) return;
                first = false;
                char hex[3 * 64 + 1] = {}; size_t k = 0;
                for (size_t b = 0; b < strides[arr] && k + 3 < sizeof hex; ++b) k += size_t(std::snprintf(hex + k, sizeof hex - k, "%02x%s", r[b], b % 4 == 3 ? " " : ""));
                if (w.rep->notes.size() < 40) w.rep->notes.push_back(w.where + ": " + kind + " sample " + hex);
            };
            if (arr == 0) {
                rangeBlock(w, cur, out, size_t(n), strides[arr], [&](uint8_t* r) { w.u16xy(r + 8, "edge vertex"); }, "edge CVertex block");
            } else if (arr == 1) {
                rangeBlock(w, cur, out, size_t(n), strides[arr], [&](uint8_t* r) { sample("fan base", r); w.u16xy(r + 8, "fan base vertex"); }, "edge fan-base block");
            } else {
                rangeBlock(w, cur, out, size_t(n), strides[arr], [&](uint8_t* r) { sample(arr == 2 ? "water vertex" : "water fan base", r); }, "edge water block");
            }
        }
    }
    const uint8_t water = cur.u8("water flag");
    out.push_back(water);
    if (water) {
        cur.need(16, "water sub-patch header");
        const uint16_t vc = cur.u16("water vertex count"), tc = cur.u16("water triangle count");
        const int32_t type = cur.i32("water type"), stride = cur.i32("water stride");
        if (stride != 0x38 && stride != 0xc) throw std::runtime_error("unexpected water sub-patch stride " + std::to_string(stride));
        out.insert(out.end(), cur.at() - 12, cur.at());
        const std::string what = "water sub-patch vertex (type " + std::to_string(type) + ", stride " + std::to_string(stride) + ")";
        // types 1/2/6/8 (56 B): u16 x, u16 y, then the wave/colour floats;
        // types 3/4/5 (12 B, sea): f32 x, f32 y, f32 z
        rangeBlock(w, cur, out, vc, size_t(stride), [&](uint8_t* r) {
            if (stride == 0xc) w.f32xy(r, what.c_str()); else w.u16xy(r, what.c_str());
        }, "water sub-patch VB");
        if (tc) rangeBlock(w, cur, out, size_t(tc) * 3, 2, [](uint8_t*) {}, "water sub-patch IB");
    }
    if (cur.p != trailer.size()) throw std::runtime_error("patch trailer has " + std::to_string(trailer.size() - cur.p) + " unparsed bytes");
}

// One background patch body (CLandscapeBackgroundPatch::Save). Returns the new body.
std::vector<uint8_t> patchBody(Walk& w, const std::vector<uint8_t>& body) {
    auto pb = forge::stbbake::parsePatchBody(body);
    if (!pb.valid && !pb.waterOnly) throw std::runtime_error("patch body did not parse");
    if (!pb.waterOnly) {
        // VB: [i32 len][block vertexCount x 16]: u16 gridX, u16 gridY, f32 h, u32 normal, u16 uv, u16 uv
        std::vector<uint8_t> vb;
        Cursor c(pb.vbBlock);
        rangeBlock(w, c, vb, pb.header.vertexCount, 16, [&](uint8_t* r) { w.u16xy(r, "patch vertex"); }, "patch VB");
        if (c.p != pb.vbBlock.size()) throw std::runtime_error("patch VB block has trailing bytes");
        pb.vbBlock = std::move(vb);
    }
    std::vector<uint8_t> trailer;
    patchTrailer(w, pb.trailer, trailer);
    pb.trailer = std::move(trailer);
    const auto hdr = forge::stbbake::serializePatchHeader(pb.header);
    w.mix(hdr); w.mix(pb.texture); w.mix(pb.ibBlock);
    return forge::stbbake::assemblePatchBody(pb);
}

// One foreground frame (CEngineLandscapePatch: layer meshes + CWaterPatchMesh).
std::vector<uint8_t> foregroundBody(Walk& w, const std::vector<uint8_t>& body) {
    auto fg = forge::stbbake::parseForegroundFrame(body);
    if (w.audit && forge::stbbake::serializeForegroundFrame(fg) != body)
        throw std::runtime_error("foreground frame does not round-trip");
    for (auto& layer : fg.layers)
        for (auto& v : layer.vertices) {
            uint8_t xy[4];
            std::memcpy(xy, &v.x, 2); std::memcpy(xy + 2, &v.y, 2);
            w.u16xy(xy, "layer vertex");
            std::memcpy(&v.x, xy, 2); std::memcpy(&v.y, xy + 2, 2);
        }
    if (fg.hasWater) {
        // CWaterPatchMesh::Save: i32, i32, f32, i32, then [i32 len][block 289 x 0x42]
        // whose records start u16 x, u16 y (the 17x17 water grid in world units)
        std::vector<uint8_t> payload;
        Cursor c(fg.waterPayload);
        copyBytes(c, payload, 16, "water mesh header");
        rangeBlock(w, c, payload, 0x121, 0x42, [&](uint8_t* r) { w.u16xy(r, "water mesh vertex"); }, "water mesh VB");
        if (c.p != fg.waterPayload.size()) throw std::runtime_error("water mesh payload has " + std::to_string(fg.waterPayload.size() - c.p) + " unparsed bytes");
        fg.waterPayload = std::move(payload);
    }
    const auto out = forge::stbbake::serializeForegroundFrame(fg);
    w.mix(out.data(), out.size() - fg.waterPayload.size());   // layers (the water block was mixed as records)
    return out;
}

// One local-detail cache-group frame (CObjectCacheGroupCollection::SaveContents).
// Edited in place: every coordinate is a plain float at a fixed position.
void groupBody(Walk& w, std::vector<uint8_t>& body) {
    Cursor c(body);
    const uint32_t collections = c.u32("collection count");
    if (collections > 256) throw std::runtime_error("implausible collection count");
    for (uint32_t ci = 0; ci < collections; ++ci) {
        c.u32("collection flag"); c.u32("collection type");
        const uint32_t prims = c.u32("primitive count");
        if (prims > 65536) throw std::runtime_error("implausible primitive count");
        for (uint32_t pi = 0; pi < prims; ++pi) {
            const uint32_t primType = c.u32("primitive type");
            c.need(40, "primitive bounds");
            w.f32xy(c.at(), "primitive box min"); w.f32xy(c.at() + 12, "primitive box max");
            w.f32xy(c.at() + 24, "primitive sphere");
            c.p += 40;
            if (primType == 0) {   // CLocalDetailPrimitiveMesh: 3x4 matrix (row 3 = position), f32
                c.need(52, "mesh primitive");
                w.f32xy(c.at() + 36, "mesh position");
                c.p += 52;
            } else if (primType == 1) {   // CLocalDetailPrimitiveRepeatedMesh
                const uint32_t n = c.u32("instance count");
                c.f32("max scale");
                if (n > 4096) throw std::runtime_error("implausible instance count");
                c.need(size_t(n) * 32, "instance arrays");
                c.p += size_t(n) * 16;   // A = (rotX, rotY, 0, 0)
                for (uint32_t i = 0; i < n; ++i) { w.f32xy(c.at(), "instance position"); c.p += 16; }
                if (c.u8("normals flag")) { const size_t P = (size_t(n) + 3) & ~size_t(3); c.need(P * 12, "normals"); c.p += P * 12; }
                if (c.u8("wind flag")) { c.need(n, "wind"); c.p += n; }
                if (c.u8("subsection flag")) {
                    const uint32_t cnt = c.u32("subsection count");
                    if (cnt > 4096) throw std::runtime_error("implausible subsection count");
                    for (uint32_t e = 0; e < cnt; ++e) {
                        c.need(0x50, "subsection element");
                        // SoA over four quadrants; slots with count 0 hold uninitialised residue
                        for (int k = 0; k < 4; ++k) {
                            if (c.at()[0x40 + k] == 0) continue;
                            w.f32(c.at() + k * 4, true, "subsection centre x"); w.f32(c.at() + 0x10 + k * 4, false, "subsection centre y");
                        }
                        c.p += 0x50;
                    }
                }
            } else if (primType == 2) {   // CLocalDetailPrimitiveMeshZSpriteBatch
                const uint32_t n = c.u32("z-sprite count");
                if (n > 65536) throw std::runtime_error("implausible z-sprite count");
                c.need(size_t(n) * 0x44 + size_t(n) * 16, "z-sprite arrays");
                for (uint32_t i = 0; i < n; ++i) { w.f32xy(c.at() + 36, "z-sprite position"); w.f32xy(c.at() + 52, "z-sprite sphere"); c.p += 0x44; }
                for (uint32_t i = 0; i < n; ++i) { w.f32xy(c.at(), "z-sprite object"); c.p += 16; }
            } else throw std::runtime_error("unknown primitive type " + std::to_string(primType));
        }
    }
    if (c.p != body.size()) throw std::runtime_error("cache-group frame has " + std::to_string(body.size() - c.p) + " unparsed bytes");
    w.mix(body);
}

struct FrameSlot { size_t segIndex = 0; size_t start = 0, end = 0, slotEnd = 0; };

struct Ctx {
    Walk w;
    std::vector<uint8_t>* chunk = nullptr;
    forge::stbbake::Chunk parsed;
    std::map<size_t, FrameSlot> frames;      // by absolute start
    std::set<size_t> done;                   // frames already handled
};

void rewriteFrame(Ctx& ctx, const FrameSlot& fs, const std::vector<uint8_t>& body, const char* what) {
    if (ctx.w.audit) return;
    const auto framed = forge::lzo::compressFramed999(body);
    if (framed.size() > fs.slotEnd - fs.start)
        throw std::runtime_error(std::string(what) + " frame at " + std::to_string(fs.start) + " grew past its slot (" + std::to_string(framed.size()) + " > " + std::to_string(fs.slotEnd - fs.start) + ")");
    std::copy(framed.begin(), framed.end(), ctx.chunk->begin() + std::ptrdiff_t(fs.start));
    std::fill(ctx.chunk->begin() + std::ptrdiff_t(fs.start + framed.size()), ctx.chunk->begin() + std::ptrdiff_t(fs.slotEnd), uint8_t(0));
}

const FrameSlot& frameAt(Ctx& ctx, size_t offset, const char* what) {
    auto it = ctx.frames.find(offset);
    if (it == ctx.frames.end()) throw std::runtime_error(std::string(what) + ": no LZO frame starts at " + std::to_string(offset));
    return it->second;
}

size_t frameIndexOf(const Ctx& ctx, const FrameSlot& fs) {
    for (size_t i = 0; i < ctx.parsed.frameIndices.size(); ++i)
        if (ctx.parsed.frameIndices[i] == fs.segIndex) return i;
    throw std::runtime_error("frame index lookup failed");
}

// ---- local-detail (foliage) quadtree -------------------------------------
// Node header (0x2C, in the parent's directory or the common record) -> the
// node's directory at fbPos+offIn: [u32 groupCount][groupCount x 0x28 group
// headers][4 x (u32 present [+0x2C child header])]. A group header's
// fbPos+offIn is its LZO cache frame. File blocks are the loader's async read
// units: an "own block" node owns [fbPos, fbPos+fbSize) holding its inline
// groups' frames, its inline children's content and, last, its directory; an
// own-block group's block is just its frame. Translated frames re-compress to
// slightly different sizes, so the whole section is re-laid in the engine's
// own order (CQuadTreeElement::SaveFileBlock / SaveSubFileBlocks) with every
// (fbPos, fbSize, offIn) rewritten; own blocks stay page aligned and the chunk
// grows when the section was the last thing in it.
struct LdGroup {
    std::vector<uint8_t> header;     // 0x28, sphere translated; triple patched at layout
    std::vector<uint8_t> body;       // decoded + translated contents
    std::vector<uint8_t> framed;     // re-compressed frame
    int32_t fbPos = 0, fbSize = 0, offIn = 0;
    bool own = false;
};
struct LdNode {
    std::vector<uint8_t> header;     // 0x2C (root: the record's copy)
    int32_t fbPos = 0, fbSize = 0, offIn = 0;
    bool own = false;
    std::vector<LdGroup> groups;
    std::vector<std::unique_ptr<LdNode>> children;   // 4 slots, null = absent
};

void ldParse(Ctx& ctx, LdNode& node, const LdNode* parent, int depth) {
    if (depth > 12) throw std::runtime_error("local-detail tree too deep");
    auto& chunk = *ctx.chunk;
    node.fbPos = int32_t(rd32(node.header, 24)); node.fbSize = int32_t(rd32(node.header, 28)); node.offIn = int32_t(rd32(node.header, 32));
    node.own = !parent || node.fbPos != parent->fbPos;
    if (node.fbPos <= 0 || node.offIn < 0 || size_t(node.fbPos) + size_t(node.offIn) + 4 > chunk.size()) throw std::runtime_error("local-detail node file block out of range");
    size_t p = size_t(node.fbPos) + size_t(node.offIn);
    const uint32_t groups = rd32(chunk, p); p += 4;
    if (groups > 4096) throw std::runtime_error("implausible local-detail group count");
    ++ctx.w.rep->detailNodes;
    for (uint32_t g = 0; g < groups; ++g) {
        if (p + 0x28 > chunk.size()) throw std::runtime_error("truncated local-detail group header");
        LdGroup grp;
        grp.header.assign(chunk.begin() + std::ptrdiff_t(p), chunk.begin() + std::ptrdiff_t(p + 0x28));
        grp.fbPos = int32_t(rd32(grp.header, 0)); grp.fbSize = int32_t(rd32(grp.header, 4)); grp.offIn = int32_t(rd32(grp.header, 8));
        grp.own = grp.fbPos != node.fbPos;
        ctx.w.where = "detail group @" + std::to_string(p);
        ctx.w.f32xy(grp.header.data() + 12, "group sphere");
        ctx.w.mix(grp.header.data() + 12, 0x28 - 12);
        ++ctx.w.rep->detailGroups;
        const size_t at = size_t(grp.fbPos) + size_t(grp.offIn);
        const FrameSlot& fs = frameAt(ctx, at, "local-detail group");
        if (ctx.done.count(fs.start)) throw std::runtime_error("local-detail group frame referenced twice");
        ctx.done.insert(fs.start);
        grp.body = forge::stbbake::decodeFrame(ctx.parsed, frameIndexOf(ctx, fs));
        ctx.w.where = "cache group frame @" + std::to_string(fs.start);
        groupBody(ctx.w, grp.body);
        ++ctx.w.rep->groupFrames;
        node.groups.push_back(std::move(grp));
        p += 0x28;
    }
    for (int q = 0; q < 4; ++q) {
        const uint32_t present = rd32(chunk, p); p += 4;
        if (!present) { node.children.emplace_back(nullptr); continue; }
        if (present != 1) throw std::runtime_error("local-detail child flag is not 0/1");
        if (p + 0x2c > chunk.size()) throw std::runtime_error("truncated local-detail child header");
        auto child = std::make_unique<LdNode>();
        child->header.assign(chunk.begin() + std::ptrdiff_t(p), chunk.begin() + std::ptrdiff_t(p + 0x2c));
        ctx.w.where = "detail node @" + std::to_string(p);
        ctx.w.f32xy(child->header.data(), "node sphere");
        ctx.w.mix(child->header.data(), 24); ctx.w.mix(child->header.data() + 36, 8);
        p += 0x2c;
        ldParse(ctx, *child, &node, depth + 1);
        node.children.push_back(std::move(child));
    }
}

void put32(std::vector<uint8_t>& d, size_t at, uint32_t v) { std::memcpy(d.data() + at, &v, 4); }

// The section writer: a byte vector positioned at absolute chunk offsets.
struct LdOut {
    std::vector<uint8_t> bytes;
    size_t base = 0;
    size_t cursor() const { return base + bytes.size(); }
    void align(size_t a) { while (cursor() % a) bytes.push_back(0); }
    void put(const std::vector<uint8_t>& v) { bytes.insert(bytes.end(), v.begin(), v.end()); }
    void put32(uint32_t v) { const uint8_t* p = reinterpret_cast<const uint8_t*>(&v); bytes.insert(bytes.end(), p, p + 4); }
    void patch(size_t absolute, const std::vector<uint8_t>& v) { std::copy(v.begin(), v.end(), bytes.begin() + std::ptrdiff_t(absolute - base)); }
};

// CQuadTreeElement::SaveFileBlock: inline group frames, inline children, then
// this node's directory. blockPos/blockSize describe the enclosing own block
// (blockSize is only known once the block is closed: directories are patched
// afterwards by ldPatch).
struct LdDirSite { LdNode* node; size_t at; };
void ldFileBlock(LdNode& node, LdOut& out, size_t blockPos, std::vector<LdDirSite>& dirs) {
    for (auto& g : node.groups) {
        if (g.own) continue;
        g.fbPos = int32_t(blockPos); g.offIn = int32_t(out.cursor() - blockPos);
        out.put(g.framed);
    }
    for (auto& c : node.children)
        if (c && !c->own) ldFileBlock(*c, out, blockPos, dirs);
    node.fbPos = int32_t(blockPos); node.offIn = int32_t(out.cursor() - blockPos);
    dirs.push_back({&node, out.cursor()});
    out.put32(uint32_t(node.groups.size()));
    for (auto& g : node.groups) out.put(g.header);
    for (auto& c : node.children) {
        out.put32(c ? 1u : 0u);
        if (c) out.put(c->header);
    }
}

// CQuadTreeElement::SaveSubFileBlocks: own-block groups (aligned), then the
// children: own-block ones as whole trees, inline ones recursively.
void ldTree(LdNode& node, LdOut& out, std::vector<LdDirSite>& dirs);
void ldSubBlocks(LdNode& node, LdOut& out, std::vector<LdDirSite>& dirs) {
    for (auto& g : node.groups) {
        if (!g.own) continue;
        out.align(2048);
        g.fbPos = int32_t(out.cursor()); g.offIn = 0; g.fbSize = int32_t(g.framed.size());
        out.put(g.framed);
    }
    for (auto& c : node.children) {
        if (!c) continue;
        if (c->own) ldTree(*c, out, dirs); else ldSubBlocks(*c, out, dirs);
    }
}
void ldTree(LdNode& node, LdOut& out, std::vector<LdDirSite>& dirs) {
    out.align(2048);
    const size_t start = out.cursor();
    ldFileBlock(node, out, start, dirs);
    const int32_t size = int32_t(out.cursor() - start);
    // every inline member of this block shares its (pos, size)
    std::function<void(LdNode&)> close = [&](LdNode& n) {
        n.fbSize = size;
        for (auto& g : n.groups) if (!g.own) g.fbSize = size;
        for (auto& c : n.children) if (c && !c->own) close(*c);
    };
    close(node);
    ldSubBlocks(node, out, dirs);
}

// Rewrite every directory with the final triples.
void ldPatch(LdOut& out, const std::vector<LdDirSite>& dirs) {
    for (const auto& d : dirs) {
        size_t p = d.at + 4;
        for (auto& g : d.node->groups) {
            put32(g.header, 0, uint32_t(g.fbPos)); put32(g.header, 4, uint32_t(g.fbSize)); put32(g.header, 8, uint32_t(g.offIn));
            out.patch(p, g.header); p += 0x28;
        }
        for (auto& c : d.node->children) {
            p += 4;
            if (!c) continue;
            put32(c->header, 24, uint32_t(c->fbPos)); put32(c->header, 28, uint32_t(c->fbSize)); put32(c->header, 32, uint32_t(c->offIn));
            out.patch(p, c->header); p += 0x2c;
        }
    }
}

size_t ldSectionStart(const LdNode& node) {
    size_t s = node.own ? size_t(node.fbPos) : SIZE_MAX;
    for (const auto& g : node.groups) if (g.own) s = std::min(s, size_t(g.fbPos));
    for (const auto& c : node.children) if (c) s = std::min(s, ldSectionStart(*c));
    return s;
}

void ldLayout(Ctx& ctx, LdNode& root) {
    auto& chunk = *ctx.chunk;
    std::function<void(LdNode&)> compressAll = [&](LdNode& n) {
        for (auto& g : n.groups) g.framed = forge::lzo::compressFramed999(g.body);
        for (auto& c : n.children) if (c) compressAll(*c);
    };
    compressAll(root);
    const size_t sectionStart = ldSectionStart(root);
    if (sectionStart % 2048) throw std::runtime_error("local-detail section does not start on a page boundary");
    for (const auto& [start, fs] : ctx.frames)
        if (start >= sectionStart && !ctx.done.count(start)) throw std::runtime_error("a non-foliage frame lies inside the local-detail section");
    LdOut out; out.base = sectionStart;
    std::vector<LdDirSite> dirs;
    root.own = true;
    ldTree(root, out, dirs);
    ldPatch(out, dirs);
    const size_t oldEnd = chunk.size(), newEnd = sectionStart + out.bytes.size();
    if (newEnd > oldEnd) {
        ctx.w.rep->notes.push_back("local-detail section grew: chunk " + std::to_string(oldEnd) + " -> " + std::to_string(newEnd) + " bytes");
        chunk.resize(newEnd);
    }
    std::copy(out.bytes.begin(), out.bytes.end(), chunk.begin() + std::ptrdiff_t(sectionStart));
    std::fill(chunk.begin() + std::ptrdiff_t(newEnd), chunk.end(), uint8_t(0));   // the section is the last thing in a retail chunk
}

// Background-LOD tree: node AABBs are world space (mapX/mapY are map-local);
// each LOD record references its patch frame as (fileBlockPos, fileBlockSize,
// offsetIntoFileBlock) -> frame at pos+offset. A file block holds the frames of
// several nodes, page aligned in retail.
struct LodRef { size_t recordOffset = 0; int32_t pos = 0, size = 0, off = 0; };
void treeNodes(Ctx& ctx, const forge::stbbake::BackgroundTreeNode& node, std::vector<LodRef>& refs) {
    auto& chunk = *ctx.chunk;
    const size_t h = node.headerOffset;
    if (h + 47 > chunk.size()) throw std::runtime_error("tree node header out of range");
    ctx.w.where = "background tree node @" + std::to_string(h);
    ctx.w.f32xy(chunk.data() + h + 23, "node aabb min");
    ctx.w.f32xy(chunk.data() + h + 35, "node aabb max");
    ctx.w.mix(chunk.data() + h, 11); ctx.w.mix(chunk.data() + h + 23, 24);
    ++ctx.w.rep->treeNodes;
    for (size_t i = 0; i < node.header.lod.size(); ++i) {
        const auto& l = node.header.lod[i];
        if (l.fileBlockPos == 0 && l.fileBlockSize == 0) continue;
        refs.push_back({h + 47 + i * 13, l.fileBlockPos, l.fileBlockSize, l.offsetIntoFileBlock});
    }
    for (const auto& c : node.children) treeNodes(ctx, c, refs);
}

// The end of the slot a run of frames may occupy in place: the start of the
// first non-pad segment after the last frame of the run (or the chunk end).
size_t slotEndAfter(const Ctx& ctx, size_t lastFrameStart) {
    const FrameSlot& fs = ctx.frames.at(lastFrameStart);
    return fs.slotEnd;
}

// Append bytes at the next page boundary of the chunk; returns their offset.
size_t appendAligned(std::vector<uint8_t>& chunk, const std::vector<uint8_t>& bytes) {
    while (chunk.size() % 2048) chunk.push_back(0);
    const size_t at = chunk.size();
    chunk.insert(chunk.end(), bytes.begin(), bytes.end());
    return at;
}

bool run(std::vector<uint8_t>& chunk, std::vector<uint8_t>& record, Walk w, RelocateReport& report, std::string& error) {
    try {
        Ctx ctx;
        ctx.w = w;
        ctx.w.rep = &report;
        ctx.w.sites = &report.sites; ctx.w.siteIsX = &report.siteIsX;
        report.sites.clear(); report.siteIsX.clear();
        ctx.chunk = &chunk;
        ctx.parsed = forge::stbbake::parseChunk(chunk);
        // frame slots: a frame owns its bytes plus the zero PAD up to the next non-pad segment
        for (size_t i = 0; i < ctx.parsed.frameIndices.size(); ++i) {
            const size_t si = ctx.parsed.frameIndices[i];
            const auto& s = ctx.parsed.segments[si];
            FrameSlot fs; fs.segIndex = si; fs.start = s.start; fs.end = s.end; fs.slotEnd = s.end;
            for (size_t k = si + 1; k < ctx.parsed.segments.size(); ++k) {
                if (ctx.parsed.segments[k].kind != forge::stbbake::SegKind::Pad) break;
                fs.slotEnd = ctx.parsed.segments[k].end;
            }
            ctx.frames[fs.start] = fs;
        }
        if (record.size() < 0x7d + 0x2c) throw std::runtime_error("common record too short for the local-detail root");
        const bool write = !ctx.w.audit;

        // 1. foreground directory AABBs + the foreground frames it points at.
        // One 0x24 record per 16x16 cell, row-major: {u32 frameOffset, u32 span,
        // f32 aabb[6], u32 flags}; a cell without a foreground mesh has a zero
        // frame pointer but is NOT a terminator (forgecore's parseQuadDir stops
        // there, which is why it under-counts foreground frames on the fillers).
        struct FgItem { size_t dirOffset = 0, oldStart = 0; std::vector<uint8_t> framed; };
        std::vector<FgItem> fg;
        {
            const uint32_t fgPos = rd32(record, 0x64);
            const auto info = forge::stbinfo::readInfoBlock(record.data());
            const size_t cells = size_t(info.mapWidth / 16) * size_t(info.mapHeight / 16);
            std::map<size_t, size_t> seen;   // frame start -> item index (a frame may be shared by cells)
            for (size_t i = 0; i < cells; ++i) {
                const size_t o = size_t(fgPos ? fgPos : 0x800) + i * 0x24;
                if (o + 0x24 > chunk.size()) throw std::runtime_error("foreground directory runs past the chunk");
                const uint32_t frameOffset = rd32(chunk, o);
                if (frameOffset == 0) {   // an all-zero record = a map without any foreground mesh
                    bool zero = true;
                    for (size_t k = 0; k < 0x24; ++k) zero = zero && chunk[o + k] == 0;
                    if (zero) continue;
                }
                ctx.w.where = "foreground directory @" + std::to_string(o);
                ctx.w.f32xy(chunk.data() + o + 8, "foreground aabb min");
                ctx.w.f32xy(chunk.data() + o + 20, "foreground aabb max");
                ctx.w.mix(chunk.data() + o + 8, 28);
                if (frameOffset == 0) continue;
                const FrameSlot& fs = frameAt(ctx, frameOffset, "foreground directory");
                if (seen.count(fs.start)) { fg.push_back({o, fs.start, fg[seen[fs.start]].framed}); continue; }
                ctx.done.insert(fs.start);
                const auto body = forge::stbbake::decodeFrame(ctx.parsed, frameIndexOf(ctx, fs));
                ctx.w.where = "foreground frame @" + std::to_string(fs.start);
                const auto out = foregroundBody(ctx.w, body);
                ++report.foregroundFrames;
                seen[fs.start] = fg.size();
                fg.push_back({o, fs.start, write ? forge::lzo::compressFramed999(out) : std::vector<uint8_t>{}});
            }
        }
        // 2. background-LOD tree node AABBs, collecting the patch references
        std::vector<LodRef> refs;
        {
            const uint32_t rootPos = rd32(record, 0x68);
            if (rootPos == 0 || rootPos >= chunk.size()) throw std::runtime_error("background tree root position out of range");
            const auto root = forge::stbbake::parseBackgroundTree(chunk, rootPos);
            treeNodes(ctx, root, refs);
        }

        // 3. local-detail tree: root header in the record, directories in the chunk
        if (record[0x79]) {
            ctx.w.slackScale = 2.0;
            ctx.w.where = "local-detail root (record)";
            float radius; std::memcpy(&radius, record.data() + 0x7d + 12, 4);
            if (radius > 0) ctx.w.f32xy(record.data() + 0x7d, "root sphere");   // a map without foliage keeps a zero sphere
            LdNode root;
            root.header.assign(record.begin() + 0x7d, record.begin() + 0x7d + 0x2c);
            ldParse(ctx, root, nullptr, 0);
            ctx.w.slackScale = 1.0;
            if (write) {
                ldLayout(ctx, root);
                put32(root.header, 24, uint32_t(root.fbPos)); put32(root.header, 28, uint32_t(root.fbSize)); put32(root.header, 32, uint32_t(root.offIn));
                std::copy(root.header.begin(), root.header.end(), record.begin() + 0x7d);
            }
        }

        if (write && !fg.empty()) {
            // retail keeps every foreground frame on a 2048-byte boundary; re-lay
            // the run in place when it fits, else append it to the chunk
            std::vector<size_t> order;
            for (size_t i = 0; i < fg.size(); ++i) if (fg[i].dirOffset && (i == 0 || fg[i].oldStart != fg[i - 1].oldStart)) order.push_back(i);
            std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return fg[a].oldStart < fg[b].oldStart; });
            std::vector<size_t> uniq;
            for (size_t i : order) if (uniq.empty() || fg[uniq.back()].oldStart != fg[i].oldStart) uniq.push_back(i);
            size_t regionStart = fg[uniq.front()].oldStart, need = 0;
            for (size_t i : uniq) { need = (need + 2047) & ~size_t(2047); need += fg[i].framed.size(); }
            const size_t regionEnd = slotEndAfter(ctx, fg[uniq.back()].oldStart);
            std::map<size_t, size_t> newStart;   // old start -> new start
            if (regionStart + need <= regionEnd) {
                std::fill(chunk.begin() + std::ptrdiff_t(regionStart), chunk.begin() + std::ptrdiff_t(regionEnd), uint8_t(0));
                size_t cursor = regionStart;
                for (size_t i : uniq) {
                    cursor = (cursor + 2047) & ~size_t(2047);
                    std::copy(fg[i].framed.begin(), fg[i].framed.end(), chunk.begin() + std::ptrdiff_t(cursor));
                    newStart[fg[i].oldStart] = cursor;
                    cursor += fg[i].framed.size();
                }
            } else {
                for (size_t i : uniq) {
                    std::fill(chunk.begin() + std::ptrdiff_t(fg[i].oldStart), chunk.begin() + std::ptrdiff_t(ctx.frames.at(fg[i].oldStart).slotEnd), uint8_t(0));
                    newStart[fg[i].oldStart] = appendAligned(chunk, fg[i].framed);
                }
                report.notes.push_back("foreground frames appended to the chunk (" + std::to_string(need) + " > " + std::to_string(regionEnd - regionStart) + " bytes in place)");
            }
            for (const auto& item : fg) {
                put32(chunk, item.dirOffset, uint32_t(newStart.at(item.oldStart)));
                put32(chunk, item.dirOffset + 4, uint32_t(item.framed.size()));
            }
        }

        // 4. background patches, per LOD file block: every frame of a block is
        // translated and re-compressed, the block re-laid contiguously in place
        // when it fits its slot, else appended to the chunk; every LOD record
        // that references the block is rewritten
        {
            std::map<int32_t, std::map<size_t, std::vector<uint8_t>>> blocks;   // block pos -> (frame start -> new framed bytes)
            for (const auto& r : refs) {
                const size_t start = size_t(r.pos) + size_t(r.off);
                auto& frames = blocks[r.pos];
                if (frames.count(start)) continue;
                const FrameSlot& fs = frameAt(ctx, start, "background LOD record");
                if (ctx.done.count(start)) throw std::runtime_error("LOD record references a foreground/foliage frame");
                ctx.done.insert(start);
                const auto body = forge::stbbake::decodeFrame(ctx.parsed, frameIndexOf(ctx, fs));
                ctx.w.where = "patch frame @" + std::to_string(start);
                const auto out = patchBody(ctx.w, body);
                if (ctx.w.audit && out != body)
                    report.issues.push_back(ctx.w.where + ": patch body does not round-trip (" + std::to_string(out.size()) + " vs " + std::to_string(body.size()) + ")");
                ++report.patchFrames;
                frames[start] = write ? forge::lzo::compressFramed999(out) : std::vector<uint8_t>{};
            }
            if (write) {
                std::map<int32_t, std::pair<int32_t, int32_t>> blockMoved;     // old pos -> (new pos, new size)
                std::map<size_t, int32_t> frameOff;                              // old frame start -> new offset in its block
                int appended = 0;
                for (auto& [pos, frames] : blocks) {
                    std::vector<uint8_t> bytes;
                    for (auto& [start, framed] : frames) { frameOff[start] = int32_t(bytes.size()); bytes.insert(bytes.end(), framed.begin(), framed.end()); }
                    const size_t slotEnd = slotEndAfter(ctx, frames.rbegin()->first);
                    if (size_t(pos) + bytes.size() <= slotEnd) {
                        std::fill(chunk.begin() + std::ptrdiff_t(pos), chunk.begin() + std::ptrdiff_t(slotEnd), uint8_t(0));
                        std::copy(bytes.begin(), bytes.end(), chunk.begin() + std::ptrdiff_t(pos));
                        blockMoved[pos] = {pos, int32_t(bytes.size())};
                    } else {
                        for (auto& [start, framed] : frames)
                            std::fill(chunk.begin() + std::ptrdiff_t(start), chunk.begin() + std::ptrdiff_t(ctx.frames.at(start).slotEnd), uint8_t(0));
                        const size_t at = appendAligned(chunk, bytes);
                        blockMoved[pos] = {int32_t(at), int32_t(bytes.size())};
                        ++appended;
                    }
                }
                for (const auto& r : refs) {
                    const auto& mv = blockMoved.at(r.pos);
                    put32(chunk, r.recordOffset + 1, uint32_t(mv.first));
                    put32(chunk, r.recordOffset + 5, uint32_t(mv.second));
                    put32(chunk, r.recordOffset + 9, uint32_t(frameOff.at(size_t(r.pos) + size_t(r.off))));
                }
                if (appended) report.notes.push_back(std::to_string(appended) + " of " + std::to_string(blocks.size()) + " patch file block(s) appended to the chunk");
            }
        }
        // 5. anything left must be a patch nobody references (retail carries a few); keep it in its slot
        for (auto& [start, fs] : ctx.frames) {
            if (ctx.done.count(start)) continue;
            const auto body = forge::stbbake::decodeFrame(ctx.parsed, frameIndexOf(ctx, fs));
            ctx.w.where = "unreferenced frame @" + std::to_string(start);
            std::vector<uint8_t> out;
            try { out = patchBody(ctx.w, body); }
            catch (const std::exception& e) {
                ++report.unclassifiedFrames;
                report.issues.push_back(ctx.w.where + ": not a patch (" + e.what() + "), " + std::to_string(body.size()) + " bytes");
                continue;
            }
            ++report.patchFrames;
            ctx.done.insert(start);
            if (write) {
                const auto framed = forge::lzo::compressFramed999(out);
                if (framed.size() <= fs.slotEnd - fs.start) {
                    std::copy(framed.begin(), framed.end(), chunk.begin() + std::ptrdiff_t(fs.start));
                    std::fill(chunk.begin() + std::ptrdiff_t(fs.start + framed.size()), chunk.begin() + std::ptrdiff_t(fs.slotEnd), uint8_t(0));
                } else report.notes.push_back(ctx.w.where + ": unreferenced patch left untranslated (no slot)");
            }
        }
        report.digest = ctx.w.digest;
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

} // namespace

bool relocateChunk(std::vector<uint8_t>& chunk, std::vector<uint8_t>& record, int dx, int dy,
                   RelocateReport& report, std::string& error) {
    Walk w; w.audit = false; w.dx = dx; w.dy = dy;
    return run(chunk, record, w, report, error);
}

bool auditChunk(const std::vector<uint8_t>& chunk, const std::vector<uint8_t>& record,
                int x0, int y0, int w0, int h0, RelocateReport& report, std::string& error) {
    Walk w; w.audit = true;
    const double slack = 2.0;
    w.xlo = x0 - slack; w.xhi = x0 + w0 + slack; w.ylo = y0 - slack; w.yhi = y0 + h0 + slack;
    std::vector<uint8_t> c = chunk, r = record;   // read-only walk, but the helpers take mutable buffers
    return run(c, r, w, report, error);
}

} // namespace albion::editor
