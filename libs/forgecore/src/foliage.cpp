#include "forge/foliage.hpp"

#include "forge/big.hpp"

#include "forge/lzo.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>

namespace forge::foliage {

const char* primTypeName(PrimType t) {
    switch (t) {
        case PrimType::NearMesh:     return "near-mesh";
        case PrimType::RepeatedMesh: return "repeated (grass carpet)";
        case PrimType::ZSpriteBatch: return "z-sprite (distant tree)";
    }
    return "?";
}

const char* categoryName(Category c) {
    switch (c) {
        case Category::Grass:  return "grass";
        case Category::Flower: return "flower";
        case Category::Shrub:  return "shrub";
        case Category::Fern:   return "fern";
        case Category::Tree:   return "tree";
        case Category::Prop:   return "prop";
        case Category::Unknown: break;
    }
    return "unknown";
}

// --- Embedded scenery-mesh table -------------------------------------------
// The distinct MBANK_ALLMESHES meshes referenced by the recovered StartOakValeWest
// local-detail palette, plus the shared dandelion-leaf impostor. Byte-recovered +
// name-bound; see FableTLC work/local_detail_re/FOLIAGE_NAMES.md.
namespace {
const KnownMesh kMeshes[] = {
    {  155, "MESH_GRASSBLADES_02",               "GRASSBLADES_02_32",       "Grass blades (var 02)",        Category::Grass  },
    {  156, "MESH_GRASSBLADES_03",               "GRASSBLADES_03_32",       "Grass blades (var 03)",        Category::Grass  },
    {  157, "MESH_GRASSBLADES_04",               "GRASSBLADES_04_32",       "Grass blades (var 04)",        Category::Grass  },
    {  158, "MESH_BRAMBLE_01",                   "BRAMBLE_32",              "Bramble",                      Category::Shrub  },
    {  159, "MESH_FEATURE_HOLLOWSTUMP_01",       "",                        "Hollow stump feature",         Category::Prop   },
    { 7725, "MESH_SILVER_BIRCH_02",              "SILVERBIRCHTRUNKBUMP_24", "Silver birch (var 02)",        Category::Tree   },
    { 7728, "MESH_SILVER_BIRCH_MUTLISAPLING_01", "SILVERBIRCHTRUNKBUMP_24", "Silver birch multisapling 01", Category::Tree   },
    { 7729, "MESH_SILVER_BIRCH_MUTLISAPLING_02", "SILVERBIRCHTRUNKBUMP_24", "Silver birch multisapling 02", Category::Tree   },
    { 7730, "MESH_OAK_AUTUMN_01",                "LARGEOAKTRUNKBUMP",       "Oak (autumn, var 01)",         Category::Tree   },
    { 7734, "MESH_OAK_STUMP",                    "LARGEOAKTRUNKBUMP",       "Oak stump",                    Category::Tree   },
    { 7738, "MESH_OAK_GREEN_02",                 "LARGEOAKTRUNKBUMP",       "Oak (green, var 02)",          Category::Tree   },
    { 7739, "MESH_OAK_GREEN_03",                 "LARGEOAKTRUNKBUMP",       "Oak (green, var 03)",          Category::Tree   },
    { 7740, "MESH_OAK_SHADOW",                   "SHADOWOAK",               "Oak shadow-canopy",            Category::Tree   },
    { 7741, "MESH_DANDELIONFLOWERS_01",          "DANDELIONLEAF_32",        "Dandelion (impostor leaf)",    Category::Flower },
    { 7742, "MESH_POPPY_01",                     "POPPYFLOWER_32",          "Poppy",                        Category::Flower },
    { 7743, "MESH_GRASS_DRY_01",                 "DRYGRASS",                "Dry grass (var 01)",           Category::Grass  },
    { 7744, "MESH_GRASS_DRY_02",                 "DRYGRASS2",               "Dry grass (var 02)",           Category::Grass  },
    { 7745, "MESH_GRASS_DRY_03",                 "DRYGRASS3",               "Dry grass (var 03)",           Category::Grass  },
    { 7746, "MESH_GRASS_SNEAKING",               "BRIGHTWOOD_LONGGRASS",    "Long/sneaking grass",          Category::Grass  },
    { 7751, "MESH_BRACKEN_FLATBUSH_GREEN_01",    "BRACKENMASS2_GREEN",      "Bracken flatbush (green)",     Category::Fern   },
    { 7752, "MESH_BRACKEN_LOWPOLY_GREEN",        "BRACKENMASS_GREEN",       "Bracken lowpoly (green)",      Category::Fern   },
    { 7754, "MESH_BRACKEN_FLATBUSH_PALE",        "BRACKENMASS_PALE",        "Bracken flatbush (pale)",      Category::Fern   },
};
}  // namespace

const KnownMesh* lookupMesh(uint32_t meshIdx) {
    for (const auto& m : kMeshes)
        if (m.meshIdx == meshIdx) return &m;
    return nullptr;
}

// --- Built-in named brush palette ------------------------------------------
// The 25-entry StartOakValeWest local-detail palette, physically read + verified.
// primType and instanceCount are the proven per-type values (RECOVERED-HIGH);
// tree types carry no palette-histogram count (placement pass), marked -1.
namespace {
struct Seed {
    int      palIdx;
    uint32_t meshIdx;
    uint32_t zsprite;
    uint32_t shadow;
    PrimType prim;
    float    fadeEnd;
    float    fadeStart;
    int      count;
    const char* labelOverride;  // "" -> use the mesh's own label
};
const Seed kSeeds[] = {
    {  0, 156,    0,    0, PrimType::RepeatedMesh,  20.f, 22.f, 1937, "" },
    {  1, 157,    0,    0, PrimType::RepeatedMesh,  20.f, 22.f, 1252, "" },
    {  2, 155,    0,    0, PrimType::RepeatedMesh,  20.f, 22.f, 2100, "" },
    {  3, 158,    0,    0, PrimType::RepeatedMesh,  20.f, 22.f, 1335, "" },
    {  4, 7746,   0,    0, PrimType::RepeatedMesh,  20.f, 22.f, 2193, "" },
    {  5, 7745,   0,    0, PrimType::RepeatedMesh,  20.f, 22.f, 1997, "" },
    {  6, 7744,   0,    0, PrimType::RepeatedMesh,  20.f, 22.f, 1994, "" },
    {  7, 7742,   0,    0, PrimType::RepeatedMesh,  20.f, 22.f,   46, "" },
    {  8, 7743,   0,    0, PrimType::RepeatedMesh,  32.f, 34.f,   43, "" },
    {  9, 7751,   0,    0, PrimType::RepeatedMesh,  36.f, 40.f,   69, "" },
    { 10, 159,    0,    0, PrimType::RepeatedMesh,  36.f, 40.f,   56, "" },
    { 11, 7744,   0,    0, PrimType::RepeatedMesh,  20.f, 22.f,    9, "Dry grass (var 02, layer 2)" },
    { 12, 7745,   0,    0, PrimType::RepeatedMesh,  20.f, 22.f,    8, "Dry grass (var 03, layer 2)" },
    { 13, 7746,   0,    0, PrimType::RepeatedMesh,  20.f, 22.f,   11, "Long/sneaking grass (layer 2)" },
    { 14, 7734, 7741, 7734, PrimType::NearMesh,    100.f,118.f,   -1, "" },
    { 15, 7739, 7741, 7739, PrimType::ZSpriteBatch,100.f,118.f,   -1, "" },
    { 16, 7725,    0, 7725, PrimType::ZSpriteBatch,100.f,118.f,   -1, "" },
    { 17, 7738, 7741, 7739, PrimType::ZSpriteBatch,100.f,118.f,   -1, "" },
    { 18, 7752,    0,    0, PrimType::RepeatedMesh,  36.f, 40.f,   23, "" },
    { 19, 7754,    0,    0, PrimType::RepeatedMesh,  36.f, 40.f,   17, "" },
    { 20, 7728,    0, 7728, PrimType::NearMesh,     36.f, 40.f,   -1, "" },
    { 21, 7729,    0, 7729, PrimType::NearMesh,     36.f, 40.f,   -1, "" },
    { 22, 7730,    0, 7730, PrimType::NearMesh,     36.f, 40.f,   -1, "" },
    { 23, 7740, 7741, 7739, PrimType::ZSpriteBatch,100.f,118.f,   -1, "" },
    { 24, 7734, 7741, 7734, PrimType::ZSpriteBatch,100.f,118.f,   -1, "Oak stump (distant)" },
};

FoliageType makeType(const Seed& s) {
    FoliageType t;
    t.paletteIndex   = s.palIdx;
    t.meshIdx        = s.meshIdx;
    t.zspriteMeshIdx = s.zsprite;
    t.shadowMeshIdx  = s.shadow;
    t.primType       = s.prim;
    t.fadeEnd        = s.fadeEnd;
    t.fadeStart      = s.fadeStart;
    t.instanceCount  = s.count;
    if (s.palIdx == 0) {
        // StartOakValeWest palette slot 0, byte-exact.
        t.runtimeSettings = {0x41a00000u, 0x41b00000u, 130u, 0u, 0u, 0u,
                             0u, 0u, 0u, 1u, 4u, 257u, 1u};
    } else if (s.palIdx == 15 || s.palIdx == 17) {
        // Retained for parsing/catalog fidelity. These declare primitive type 1
        // and must not be emitted by the type-0-only writer.
        t.runtimeSettings = {0x42c80000u, 0x42ec0000u, 128u, 0u, 0u, 0u,
                             0u, 0x42500000u, 0x42640000u, 2u, 1u, 1u, 0u};
    }
    if (const KnownMesh* m = lookupMesh(s.meshIdx)) {
        t.meshName     = m->meshName;
        t.gbankTexture = m->gbankTexture;
        t.category     = m->category;
        t.label        = (s.labelOverride && s.labelOverride[0]) ? s.labelOverride : m->label;
    } else {
        t.meshName = "MESH_#" + std::to_string(s.meshIdx);
        t.label    = (s.labelOverride && s.labelOverride[0]) ? s.labelOverride : t.meshName;
    }
    return t;
}
}  // namespace

const std::vector<FoliageType>& brushCatalog() {
    static const std::vector<FoliageType> catalog = [] {
        std::vector<FoliageType> v;
        v.reserve(sizeof(kSeeds) / sizeof(kSeeds[0]));
        for (const auto& s : kSeeds) v.push_back(makeType(s));
        return v;
    }();
    return catalog;
}

// --- Palette reader ---------------------------------------------------------
namespace {
constexpr size_t kRecordSize = 0x40;  // 64 bytes on disk

uint32_t rdU32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
float rdF32(const uint8_t* p) {
    float v;
    std::memcpy(&v, p, 4);
    return v;
}

bool fadeSane(float f) { return std::isfinite(f) && f >= 0.0f && f <= 100000.0f; }

// Does a run of `count` records starting at `rec` look like a real palette?
bool recordsValid(const uint8_t* data, size_t size, size_t rec, uint32_t count) {
    if (rec + static_cast<size_t>(count) * kRecordSize > size) return false;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* r = data + rec + static_cast<size_t>(i) * kRecordSize;
        const uint32_t meshIdx = rdU32(r + 0x00);
        if (meshIdx >= 20000) return false;                 // MBANK_ALLMESHES ~8112
        if (rdU32(r + 0x04) >= 20000) return false;          // zsprite idx
        if (rdU32(r + 0x08) >= 20000) return false;          // shadow idx
        if (!fadeSane(rdF32(r + 0x0c))) return false;        // FadeEnd
        if (!fadeSane(rdF32(r + 0x10))) return false;        // FadeStart
    }
    return true;
}

// Heuristic primType when reading an arbitrary chunk (the palette record does not
// store the primitive class -- it is per-instance). Grass/fern/flower/shrub are
// always repeated; trees/props default to near-mesh.
PrimType inferPrim(Category c) {
    return (c == Category::Tree) ? PrimType::NearMesh : PrimType::RepeatedMesh;
}

FoliageType parseRecord(const uint8_t* r, int palIdx) {
    FoliageType t;
    t.paletteIndex   = palIdx;
    t.meshIdx        = rdU32(r + 0x00);
    t.zspriteMeshIdx = rdU32(r + 0x04);
    t.shadowMeshIdx  = rdU32(r + 0x08);
    t.fadeEnd        = rdF32(r + 0x0c);
    t.fadeStart      = rdF32(r + 0x10);
    if (const KnownMesh* m = lookupMesh(t.meshIdx)) {
        t.meshName     = m->meshName;
        t.gbankTexture = m->gbankTexture;
        t.category     = m->category;
        t.label        = m->label;
    } else {
        t.meshName = "MESH_#" + std::to_string(t.meshIdx);
        t.label    = t.meshName;
    }
    t.primType = inferPrim(t.category);
    return t;
}
}  // namespace

Palette readPalette(const uint8_t* data, size_t size, std::optional<size_t> offset) {
    size_t head = 0;
    uint32_t count = 0;

    auto tryHead = [&](size_t o, uint32_t& outCount) -> bool {
        if (o + 5 > size) return false;
        const uint8_t flag = data[o];
        if (flag > 1) return false;
        const uint32_t c = rdU32(data + o + 1);
        if (c < 1 || c > 4096) return false;
        if (!recordsValid(data, size, o + 5, c)) return false;
        outCount = c;
        return true;
    };

    if (offset) {
        if (!tryHead(*offset, count))
            throw std::runtime_error("foliage: no coherent palette at the given offset");
        head = *offset;
    } else {
        // The palette is the trailing block of the common header, so prefer the
        // candidate whose records end exactly at the buffer end; otherwise take
        // the first coherent candidate.
        bool found = false, exact = false;
        for (size_t o = 0; o + 5 <= size; ++o) {
            uint32_t c = 0;
            if (!tryHead(o, c)) continue;
            const size_t end = o + 5 + static_cast<size_t>(c) * kRecordSize;
            const bool endsAtBuf = (end == size);
            if (!found || (endsAtBuf && !exact)) {
                head = o;
                count = c;
                found = true;
                exact = endsAtBuf;
                if (exact) break;
            }
        }
        if (!found)
            throw std::runtime_error("foliage: no local-detail palette found in record");
    }

    Palette pal;
    pal.offset = head;
    pal.entries.reserve(count);
    const uint8_t* base = data + head + 5;
    for (uint32_t i = 0; i < count; ++i)
        pal.entries.push_back(parseRecord(base + static_cast<size_t>(i) * kRecordSize,
                                          static_cast<int>(i)));
    return pal;
}

Palette readRecordFile(const std::filesystem::path& path, std::optional<size_t> offset) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("foliage: cannot open record file: " + path.string());
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    Palette pal = readPalette(buf.data(), buf.size(), offset);
    pal.source = path.string();
    return pal;
}

// --- baked instance enumeration --------------------------------------------
namespace {
// Terrain-height band a real instance Z must fall in (Fable world heights are
// small). Wide enough for any map, tight enough to reject float noise.
constexpr float kZlo = -50.0f;
constexpr float kZhi = 400.0f;

bool coordSane(float x, float y, float z, const ScanBounds& b) {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) &&
           x >= b.xlo && x <= b.xhi && y >= b.ylo && y <= b.yhi &&
           z > kZlo && z < kZhi;
}

// Decode every valid standard-LZO1X frame in `d` and invoke `onFrame(decoded)`.
// Mirrors ld_scan.c: try both [unc][comp] and [comp][unc] header orders, sanity
// gate, decode-safe, then skip past an accepted frame.
template <class Fn>
void forEachFrame(const std::vector<uint8_t>& d, int& framesDecoded, Fn&& onFrame) {
    auto u32 = [&](size_t o) {
        uint32_t v;
        std::memcpy(&v, d.data() + o, 4);
        return v;
    };
    const size_t n = d.size();
    for (size_t off = 0; off + 8 < n; off += 4) {
        const uint32_t a = u32(off), b = u32(off + 4);
        const uint32_t pairs[2][2] = {{a, b}, {b, a}};  // {unc, comp}
        for (int k = 0; k < 2; ++k) {
            const uint32_t unc = pairs[k][0], comp = pairs[k][1];
            if (comp < 32 || comp > 400000 || unc < 64 || unc > 4000000) continue;
            if (comp > unc) continue;
            if (off + 8 + static_cast<size_t>(comp) > n) continue;
            std::vector<uint8_t> out;
            try {
                out = forge::lzo::decompress(d.data() + off + 8, comp, unc);
            } catch (const std::exception&) {
                continue;  // wrong candidate; decompress_safe fails cleanly
            }
            ++framesDecoded;
            onFrame(out);
            // Resume on the scanner's 4-byte lattice. Compressed lengths are
            // not necessarily multiples of four; adding the raw length here
            // permanently shifted the old scan off all later aligned frames.
            off = ((off + 8 + comp + 3) & ~size_t(3)) - 4; // loop adds 4
            break;
        }
    }
}

// Recover the palette type# for a transform run via the RepeatedMesh grammar.
// A collection's first primitive body is
//   CObjectTypeCollection::Load: [u32 flag][u32 typeIndex][u32 primCount]
//   primitive: [u32 primType][bbox 0x18][sphere 0x10][u32 ObjectCount][u32 count2]
//              [ObjectCount x 0x10 arrayA][ObjectCount x 0x10 arrayB][...optional]
// so the two 16-byte arrays are contiguous and the header ends immediately before
// arrayA. Runtime capture proves arrayA=(cos(yaw)*scale,sin(yaw)*scale,0,0) and
// arrayB=(worldX,worldY,worldZ,scale). The detected world run is therefore arrayB;
// its header is one complete array earlier.
int bindType(const std::vector<uint8_t>& out, size_t o, size_t runLen) {
    auto u32 = [&](size_t p) { uint32_t v; std::memcpy(&v, out.data() + p, 4); return v; };
    auto tryHdr = [&](size_t hdrEnd) -> int {
        if (hdrEnd < 0x40) return -1;
        if (u32(hdrEnd - 8) != runLen) return -1;        // ObjectCount == N
        const uint32_t flag = u32(hdrEnd - 0x40);
        const uint32_t primType = u32(hdrEnd - 0x34);
        const uint32_t typeIndex = u32(hdrEnd - 0x3c);
        if (flag > 1 || primType > 2 || typeIndex >= 64) return -1;
        return static_cast<int>(typeIndex);
    };
    if (o < runLen * 16) return -1;
    return tryHdr(o - runLen * 16);
}

// Extract maximal 16-byte-stride runs (len >= 2) of in-bounds world records from
// one decoded frame; append their instances (each tagged with its bound type#).
// Returns instances added.
int extractRuns(const std::vector<uint8_t>& out, const ScanBounds& b,
                std::vector<Instance>& into) {
    auto f32 = [&](size_t o) {
        float v;
        std::memcpy(&v, out.data() + o, 4);
        return v;
    };
    auto u32 = [&](size_t o) {
        uint32_t v;
        std::memcpy(&v, out.data() + o, 4);
        return v;
    };
    auto world = [&](size_t o) {
        return o + 12 <= out.size() && coordSane(f32(o), f32(o + 4), f32(o + 8), b);
    };
    int added = 0;
    const size_t n = out.size();
    size_t o = 0;
    while (o + 16 <= n) {
        if (world(o)) {
            size_t p = o, c = 0;
            while (p + 16 <= n && world(p)) { ++c; p += 16; }
            if (c >= 2) {
                const int typeIdx = bindType(out, o, c);
                for (size_t i = 0; i < c; ++i) {
                    const size_t rec = o + i * 16;
                    Instance instance{f32(rec), f32(rec + 4), f32(rec + 8),
                                      u32(rec + 12), typeIdx};
                    instance.scale=f32(rec+12);
                    // The grammar-bound world run is Array B and its immediately
                    // preceding companion is Array A's proven scaled cos/sin pair.
                    if (typeIdx >= 0) {
                        const size_t a = o - c * 16 + i * 16;
                        const float ax = f32(a), ay = f32(a + 4);
                        instance.yawRadians = std::atan2(ay, ax);
                        instance.hasRotation = true;
                    }
                    into.push_back(instance);
                }
                added += static_cast<int>(c);
            }
            o = p;
        } else {
            o += 4;
        }
    }
    return added;
}

std::vector<uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("foliage: cannot open chunk: " + path.string());
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
}

// Derive the true world XY window from the local-detail node DIRECTORY, which is
// stored UNCOMPRESSED in the chunk header as a run of 0x24-byte records:
// [u32 FileBlockSize][6xf32 AABB][u32 TypeMask][u32 FileBlockPos]. The TypeMask
// is constant across the run, so we detect a >=3 record run at 0x24 stride
// sharing one mask with sane AABBs, and union their AABBs -> the exact extent the
// baked instances live inside. Byte-proven (LOCAL_DETAIL_RE.md). Returns nullopt
// if no such directory is present (e.g. a non-terrain chunk).
std::optional<ScanBounds> directoryBounds(const std::vector<uint8_t>& d) {
    auto u32 = [&](size_t o) { uint32_t v; std::memcpy(&v, d.data() + o, 4); return v; };
    auto f32 = [&](size_t o) { float v; std::memcpy(&v, d.data() + o, 4); return v; };
    const size_t stride = 0x24;
    auto validRec = [&](size_t r) -> bool {
        if (r + stride > d.size()) return false;
        const float mnx = f32(r + 4), mny = f32(r + 8), mnz = f32(r + 0x0c);
        const float mxx = f32(r + 0x10), mxy = f32(r + 0x14), mxz = f32(r + 0x18);
        if (!(std::isfinite(mnx) && std::isfinite(mny) && std::isfinite(mnz) &&
              std::isfinite(mxx) && std::isfinite(mxy) && std::isfinite(mxz)))
            return false;
        if (mnx > mxx || mny > mxy || mnz > mxz) return false;
        if (std::fabs(mnx) > 1e6f || std::fabs(mxx) > 1e6f ||
            std::fabs(mny) > 1e6f || std::fabs(mxy) > 1e6f) return false;
        if ((mxx - mnx) > 1e5f || (mxy - mny) > 1e5f) return false;
        return true;
    };
    for (size_t base = 0x1c; base + stride <= d.size(); base += 4) {
        const uint32_t mask = u32(base);   // TypeMask candidate; record start = base-0x1c
        if (mask == 0) continue;
        const size_t rec0 = base - 0x1c;
        size_t run = 0, r = rec0;
        while (r + stride <= d.size() && u32(r + 0x1c) == mask && validRec(r)) {
            ++run;
            r += stride;
        }
        if (run >= 3) {
            ScanBounds b{1e30f, -1e30f, 1e30f, -1e30f};
            size_t rr = rec0;
            for (size_t i = 0; i < run; ++i) {
                b.xlo = std::min(b.xlo, f32(rr + 4));
                b.xhi = std::max(b.xhi, f32(rr + 0x10));
                b.ylo = std::min(b.ylo, f32(rr + 8));
                b.yhi = std::max(b.yhi, f32(rr + 0x14));
                rr += stride;
            }
            return b;
        }
    }
    return std::nullopt;
}
}  // namespace

InstanceScan scanInstances(const std::filesystem::path& chunk,
                           std::optional<ScanBounds> bounds) {
    const std::vector<uint8_t> d = readFile(chunk);
    return scanInstances(d, chunk.string(), bounds);
}

InstanceScan scanInstances(const std::vector<uint8_t>& d, std::string source,
                           std::optional<ScanBounds> bounds) {

    // Auto-derive an XY window when none is supplied: a loose first pass (very
    // wide XY, z-band, run>=2) locates the instance cluster, then its AABB (with
    // a small pad) becomes the window for the scored pass.
    ScanBounds b;
    if (bounds) {
        b = *bounds;
    } else if (auto db = directoryBounds(d)) {
        // Primary: the byte-proven local-detail node directory gives the exact
        // extent the instances live in. Small pad for float edges.
        b = *db;
        b.xlo -= 4; b.xhi += 4; b.ylo -= 4; b.yhi += 4;
    } else {
        // Fallback (no directory found): a very wide XY window admits the real cluster plus
        // scattered float-noise runs. Real foliage packs densely (tens of points
        // per 16-unit cell); noise almost never lands 2+ points in one cell. So
        // bin the probe into 16-unit cells, keep only dense cells, and derive the
        // window from those -- this isolates the true cluster from the noise.
        ScanBounds loose{-1e6f, 1e6f, -1e6f, 1e6f};
        int fd = 0;
        std::vector<Instance> probe;
        forEachFrame(d, fd, [&](const std::vector<uint8_t>& out) {
            extractRuns(out, loose, probe);
        });
        if (probe.empty()) {
            InstanceScan empty;
            empty.source = source;
            return empty;
        }
        auto packCell = [](int32_t cx, int32_t cy) {
            return (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32) |
                   static_cast<uint32_t>(cy);
        };
        auto cellOf = [&](float vx, float vy) {
            return packCell(static_cast<int32_t>(std::floor(vx / 16.0f)),
                            static_cast<int32_t>(std::floor(vy / 16.0f)));
        };
        std::map<uint64_t, int> cellCount;
        for (const auto& p : probe) cellCount[cellOf(p.x, p.y)]++;

        // Seed from the densest cell and flood-fill through contiguous dense
        // neighbours. The real foliage carpet is spatially continuous, so it
        // forms one connected dense region; scattered float-noise cells sit below
        // the threshold and the fill stops at them.
        uint64_t seed = cellCount.begin()->first;
        int maxCount = 0;
        for (const auto& kv : cellCount)
            if (kv.second > maxCount) { maxCount = kv.second; seed = kv.first; }
        const int thresh = std::max(4, maxCount / 20);

        std::set<uint64_t> region;
        std::vector<uint64_t> stack{seed};
        region.insert(seed);
        while (!stack.empty()) {
            const uint64_t c = stack.back();
            stack.pop_back();
            const int32_t cx = static_cast<int32_t>(c >> 32);
            const int32_t cy = static_cast<int32_t>(c & 0xffffffff);
            for (int dx = -1; dx <= 1; ++dx)
                for (int dy = -1; dy <= 1; ++dy) {
                    if (!dx && !dy) continue;
                    const uint64_t nb = packCell(cx + dx, cy + dy);
                    auto it = cellCount.find(nb);
                    if (it != cellCount.end() && it->second >= thresh &&
                        region.insert(nb).second)
                        stack.push_back(nb);
                }
        }

        bool any = false;
        float xlo = 0, xhi = 0, ylo = 0, yhi = 0;
        for (const auto& p : probe) {
            if (region.find(cellOf(p.x, p.y)) == region.end()) continue;
            if (!any) { xlo = xhi = p.x; ylo = yhi = p.y; any = true; }
            xlo = std::min(xlo, p.x); xhi = std::max(xhi, p.x);
            ylo = std::min(ylo, p.y); yhi = std::max(yhi, p.y);
        }
        b = {xlo - 4, xhi + 4, ylo - 4, yhi + 4};
    }

    InstanceScan scan;
    scan.source = std::move(source);
    forEachFrame(d, scan.framesDecoded, [&](const std::vector<uint8_t>& out) {
        const size_t before = scan.instances.size();
        extractRuns(out, b, scan.instances);
        if (scan.instances.size() > before) ++scan.instanceFrames;
    });

    if (!scan.instances.empty()) {
        scan.minX = scan.maxX = scan.instances[0].x;
        scan.minY = scan.maxY = scan.instances[0].y;
        scan.minZ = scan.maxZ = scan.instances[0].z;
        std::set<uint64_t> cells;
        for (const auto& p : scan.instances) {
            scan.minX = std::min(scan.minX, p.x); scan.maxX = std::max(scan.maxX, p.x);
            scan.minY = std::min(scan.minY, p.y); scan.maxY = std::max(scan.maxY, p.y);
            scan.minZ = std::min(scan.minZ, p.z); scan.maxZ = std::max(scan.maxZ, p.z);
            const int64_t cx = static_cast<int64_t>(std::floor(p.x / 16.0f));
            const int64_t cy = static_cast<int64_t>(std::floor(p.y / 16.0f));
            cells.insert((static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32) |
                         static_cast<uint32_t>(cy));
        }
        scan.distinctCells = static_cast<int>(cells.size());

        // Per-type tally from the anchored bindings.
        std::map<int, int> byType;
        for (const auto& p : scan.instances) {
            if (p.paletteIndex >= 0) { byType[p.paletteIndex]++; ++scan.boundInstances; }
            else ++scan.unboundInstances;
        }
        for (const auto& kv : byType) scan.perType.push_back({kv.first, kv.second});
        std::sort(scan.perType.begin(), scan.perType.end(),
                  [](const TypeTally& a, const TypeTally& c) { return a.count > c.count; });
    }
    return scan;
}


std::map<uint32_t, MeshSphere> readMeshBoundingSpheres(
    const std::filesystem::path& graphicsBig) {
    const auto bank = big::File::open(graphicsBig);
    std::map<uint32_t, MeshSphere> out;
    for (const auto& b : bank.banks()) {
        if (b.name != "MBANK_ALLMESHES") continue;
        for (const auto& entry : b.entries) {
            // u32 flags, then f32 origin[10]: centre.xyz, radius, bbox min, max.
            // Short blobs are animation/other entries with no origin block.
            if (entry.subHeader.size() < 4 + 10 * 4) continue;
            float origin[10];
            std::memcpy(origin, entry.subHeader.data() + 4, sizeof(origin));
            bool finite = true;
            for (float v : origin)
                if (!std::isfinite(v)) finite = false;
            if (!finite || !(origin[3] > 0.0f) || origin[3] > 1e6f) continue;
            uint32_t polyCount = 0;
            // The descriptor stores nLOD at +0x2c and the first LOD span at
            // +0x30. Inside that span, each compiled C3DPrimitive2 header begins
            // with {vertexCount, triangleCount, faceVertexIndexCount, sVert,
            // flags}. Simple meshes store three indices per face; compiled
            // strip/degenerate representations can store fewer, but retail
            // foliage meshes keep the count within [faces, 3*faces].
            // GetTriangleCount sums primitive triangle counts; the collection
            // constructor stores 1 + that sum at +0x10 (0x02E4B31C..B4E9).
            if (entry.subHeader.size() >= 0x34 &&
                (entry.type == 1 || entry.type == 2 ||
                 entry.type == 4 || entry.type == 5)) {
                uint32_t lodCount = 0, firstLodSpan = 0;
                std::memcpy(&lodCount, entry.subHeader.data() + 0x2c, 4);
                std::memcpy(&firstLodSpan, entry.subHeader.data() + 0x30, 4);
                const auto payload = bank.entryData(entry);
                const size_t end = lodCount >= 1 && lodCount <= 8
                    ? std::min<size_t>(firstLodSpan, payload.size()) : 0;
                uint64_t triangles = 0;
                for (size_t at = 0; at + 20 <= end; ++at) {
                    uint32_t vertices, faces, indices, svert, flags;
                    std::memcpy(&vertices, payload.data() + at, 4);
                    std::memcpy(&faces, payload.data() + at + 4, 4);
                    std::memcpy(&indices, payload.data() + at + 8, 4);
                    std::memcpy(&svert, payload.data() + at + 12, 4);
                    std::memcpy(&flags, payload.data() + at + 16, 4);
                    if (vertices > 0 && vertices <= 65535 && faces > 0 && faces < 100000 &&
                        indices >= faces && uint64_t(indices) <= uint64_t(faces) * 3 &&
                        (svert == 4 || svert == 6 || svert == 20 || svert == 22) &&
                        flags <= 3) {
                        triangles += faces;
                        at += 19;
                    }
                }
                if (triangles > 0 && triangles < UINT32_MAX)
                    polyCount = static_cast<uint32_t>(triangles + 1);
            }
            out.emplace(entry.id, MeshSphere{
                origin[0], origin[1], origin[2], origin[3], polyCount});
        }
    }
    return out;
}
}  // namespace forge::foliage
