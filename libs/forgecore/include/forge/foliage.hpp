#pragma once
#include <array>
#include <filesystem>
#include <map>
// forge::foliage -- named local-detail scenery palette. Fable's terrain-driven
// grass/foliage/trees are NOT hand-placed TNG props; they are a procedural,
// theme-keyed generator whose BAKED output is a per-map "object type collection
// palette" -- a table of scenery TYPES (mesh + fade + primitive class) that the
// per-node instance runs reference by index.
//
// The palette lives in the STB __STATIC_MAP_COMMON_HEADER__ record (the buffer
// `forge stb record` extracts), NOT in the .lev body. Its grammar was recovered
// byte-exact from CLocalDetailObjectCollectionType::Load @0xBE27B0 and verified
// against StartOakValeWest (see FableTLC work/local_detail_re/FOLIAGE_NAMES.md):
//
//   palette := [u8 flag] [u32 count] [ count x Record(64 bytes) ]
//   Record  := +0x00 u32 MeshIdx          (0-based MBANK_ALLMESHES index)
//              +0x04 u32 ZSpriteMeshIdx    (distant-impostor mesh, or same/none)
//              +0x08 u32 ShadowMeshIdx
//              +0x0c f32 FadeEnd           (near cutoff distance)
//              +0x10 f32 FadeStart         (full-detail distance)
//              +0x28 f32 ZSpriteFadeStart
//              +0x2c f32 ZSpriteFadeEnd
//              +0x30 u32 FadeLODShift
//              (remaining bytes to 0x40 = ability/theme runtime fields)
//
// This module gives authoring pickers (CLI + the GUI foliage brush) a NAMED
// palette instead of raw type#/meshIdx, resolves mesh indices to human labels
// via an embedded scenery-mesh table, and reads a real palette back from a chunk.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace forge::foliage {

// Rendering class of a scenery type (LOCAL_DETAIL_PRIMITIVE_TYPE, dispatch
// @0xBDE600). Determines how the type scatters and draws.
enum class PrimType : uint8_t {
    NearMesh = 0,      // CLocalDetailPrimitiveMesh -- single/near placement
    RepeatedMesh = 1,  // CLocalDetailPrimitiveRepeatedMesh -- instanced grass carpet
    ZSpriteBatch = 2,  // CLocalDetailPrimitiveMeshZSpriteBatch -- distant billboard tree
};

// Coarse foliage family, for grouping the brush palette by what it looks like.
enum class Category : uint8_t { Grass, Flower, Shrub, Fern, Tree, Prop, Unknown };

const char* primTypeName(PrimType t);
const char* categoryName(Category c);

// One scenery type = one palette slot. `paletteIndex` is the type# the per-node
// instance runs reference; `instanceCount` (>=0) is the proven per-type placement
// count where known, or -1 when it lives in a separate pass (tree types).
// A mesh's authored bounding sphere in LOCAL mesh units, read from the mesh
// bank's Info descriptor (u32 flags, then f32 origin[10]; origin[0..2] is the
// centre and origin[3] the radius). It is stored data, not derived: no simple
// function of the bounding box reproduces it across MBANK_ALLMESHES (tested on
// all 3,295 compiled meshes; the best candidate formula matches 5.3%).
//
// The engine feeds this sphere to the subsection builder as
// centre = objectMatrix.TransformPoint(mesh.centre), radius = mesh.radius*scale
// (0x02EE1364..0x02EE13F9). Verified against retail: solving radius/scale out of
// five StartOakValeWest collections' baked subsection bytes reproduces
// origin[3] exactly for MESH_DANDELIONFLOWERS_01 (59.9947), MESH_POPPY_01
// (88.1004), MESH_BRAMBLE_THICK (99.0810), MESH_BRACKEN_FLATBUSH_GREEN_01
// (143.9263) and MESH_BRACKEN_BUSH_GREEN (145.9899).
struct MeshSphere {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float radius = 0.0f;  // 0 == unknown; callers must not substitute a guess
    // CLocalDetailObjectCollectionType::PeekPolyCount returns one plus the
    // loaded C3DMesh2 triangle count. Zero means the payload was not decoded.
    uint32_t polyCount = 0;
    bool known() const { return radius > 0.0f; }
    bool polyCountKnown() const { return polyCount > 0; }
};

// Mesh bounding spheres by dense MBANK_ALLMESHES id. Entries whose Info blob is
// too short to carry the origin block (animations and similar) are omitted.
std::map<uint32_t, MeshSphere> readMeshBoundingSpheres(
    const std::filesystem::path& graphicsBig);

struct FoliageType {
    int         paletteIndex = -1;
    uint32_t    meshIdx = 0;
    uint32_t    zspriteMeshIdx = 0;
    uint32_t    shadowMeshIdx = 0;
    PrimType    primType = PrimType::RepeatedMesh;
    float       fadeStart = 0.0f;
    float       fadeEnd = 0.0f;
    std::string meshName;      // resolved MBANK_ALLMESHES name, or "MESH_#<idx>"
    std::string gbankTexture;  // GBANK_MAIN_PC texture the mesh samples, "" if unknown
    std::string label;         // human label ("Dry grass (var 02)")
    Category    category = Category::Unknown;
    int         instanceCount = -1;
    // Exact 0x34-byte runtime settings tail following the three mesh IDs.
    // Preserved so authored palettes retain draw flags and primitive masks.
    std::array<uint32_t, 13> runtimeSettings{};
    // Populated from the mesh bank when one is available. Unknown by default --
    // the subsection builder needs a real sphere and must emit no table rather
    // than invent one.
    MeshSphere meshSphere{};
};

// A parsed palette read from a chunk record.
struct Palette {
    std::string              source;   // where it came from (path / "built-in")
    size_t                   offset = 0;  // byte offset of the [flag][count] head
    std::vector<FoliageType> entries;
};

// The built-in NAMED brush palette -- the recovered StartOakValeWest local-detail
// set, fully bound to mesh/texture names. This is what the foliage brush offers
// as its default type list until a live per-map palette is read. Stable order by
// paletteIndex (0..24).
const std::vector<FoliageType>& brushCatalog();

// Embedded scenery-mesh name/texture table. Resolves a raw MBANK_ALLMESHES index
// to a known foliage mesh so a palette read from an arbitrary chunk can be named
// without shipping the whole 8112-entry graphics.big bank. Returns nullptr if the
// index isn't a known scenery mesh.
struct KnownMesh {
    uint32_t    meshIdx;
    const char* meshName;
    const char* gbankTexture;  // "" when the mesh has no diffuse (physics/bump-only)
    const char* label;
    Category    category;
};
const KnownMesh* lookupMesh(uint32_t meshIdx);

// Parse a local-detail palette out of a raw STB-common-header record buffer.
// If `offset` is given, the [u8 flag][u32 count][records] head is read there;
// otherwise the head is auto-located (the palette is the trailing block of the
// header, so the candidate whose records end exactly at the buffer end wins).
// Throws std::runtime_error if no coherent palette is found. Mesh names are
// resolved via lookupMesh(); unknown indices become "MESH_#<idx>".
Palette readPalette(const uint8_t* data, size_t size,
                    std::optional<size_t> offset = std::nullopt);

// Convenience: read a palette from a file written by `forge stb record`.
Palette readRecordFile(const std::filesystem::path& path,
                       std::optional<size_t> offset = std::nullopt);

// --- baked instance enumeration --------------------------------------------
// One placed scenery instance. The baked per-instance record is 16 bytes:
// Runtime capture proves Array B is [f32 x][f32 y][f32 z][f32 scale] and its
// immediately preceding Array A record is [cos(yaw)*scale][sin(yaw)*scale][0][0].
// Positions are world coordinates.
struct Instance {
    float    x = 0.0f;
    float    y = 0.0f;
    float    z = 0.0f;
    uint32_t packedRotScale = 0;
    int      paletteIndex = -1;  // scenery type# this instance belongs to, -1 if unbound
    float    yawRadians = 0.0f;
    bool     hasRotation = false;
    float    scale = 0.0f;  // proven Array B .w; zero when unavailable/unbound
};

struct ScanBounds {
    float xlo, xhi, ylo, yhi;
};

// Per-scenery-type instance tally recovered by anchored binding.
struct TypeTally {
    int paletteIndex = -1;
    int count = 0;
};

// Result of scanning one terrain chunk for baked local-detail instances.
struct InstanceScan {
    std::string           source;
    int                   framesDecoded = 0;   // valid standard-LZO1X frames
    int                   instanceFrames = 0;  // frames holding >=1 instance run
    std::vector<Instance> instances;
    float minX = 0, maxX = 0, minY = 0, maxY = 0, minZ = 0, maxZ = 0;
    int                   distinctCells = 0;   // 16x16 world cells with foliage
    // Per-type counts (paletteIndex -> instance count), sorted by count desc, plus
    // how many instances could/couldn't be bound to a scenery type.
    std::vector<TypeTally> perType;
    int                   boundInstances = 0;
    int                   unboundInstances = 0;
};

// Enumerate baked local-detail instances by scanning the chunk for every valid
// standard-LZO1X frame (both header orders), decoding it, and extracting maximal
// 16-byte-stride runs of world-coordinate records. This sidesteps the file-block
// stream indirection (the physical "path B" that recovered StartOakValeWest's
// 4,414 instances). If `bounds` is given the XY window is used verbatim (z is
// gated to a terrain band); otherwise a bounds window is auto-derived from the
// data. Pure read -- never writes.
InstanceScan scanInstances(const std::filesystem::path& chunk,
                           std::optional<ScanBounds> bounds = std::nullopt);
// Same scanner for a chunk already resident in memory (for STB-backed GUI
// scenes, avoiding a temporary extracted file).
InstanceScan scanInstances(const std::vector<uint8_t>& chunk,
                           std::string source,
                           std::optional<ScanBounds> bounds = std::nullopt);

}  // namespace forge::foliage
