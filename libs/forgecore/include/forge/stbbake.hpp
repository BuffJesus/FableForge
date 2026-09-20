#pragma once
// forge::stbbake — native emitter/decoder for STB baked-landscape bank chunks.
//
// Ground truth (workflow wf_w7497f17h, 2026-07-20): there is NO load-time terrain
// bake from the .lev. Every one of the ~398 ".lev"-named STB entries is a
// pre-baked landscape MESH payload (a CStaticMapBankFile). The retail render path
// reads that chunk; if a map's bank entry is missing/unset the engine draws
// BuildDefaultSection = THE VOID (why ForgeTest is empty). So owning custom
// visible terrain == owning this chunk emitter.
//
// Codec wall retired (proven, work/terrain_path/codec_foundation):
//  - outer patch codec = stock LZO1X, framed [uncompLen u32LE][compLen u32LE][body]
//    (forge::lzo). Engine writes _999; we emit _1; same standard decoder.
//  - CRangeCompressor (VB/IB) = self-describing bit-packer with a flags==0 RAW
//    fallback → we emit raw blocks, no encoder parity needed to render.
//  - CTexture::SaveToDataStream = 19-byte header + raw DXT (not a codec).
//
// Reference: CLandscapeBackgroundPatch::Save @0x02ce3220 / SaveCompressed
// @0x02ce30c0 (D:/Documents/FableTLC/ghidra_out/landscape_savecomp_decomp.log);
// FableWin UpdateStaticMapPass1 @0x02d66c80 is the byte-exact bake oracle.

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "forge/stbinfo.hpp"
#include "forge/terrain.hpp"
#include "forge/foliage.hpp"

namespace forge::stbbake {

// Write-side palette grammars recovered from FableWin's native static-map
// generator.  With external/global resources (`external == false`), the
// texture palette is [EBOOL 0][s32 count][NUL-terminated names...].  An empty
// local-detail object palette is the same prefix with count zero.  These are
// constructors: no bytes are inherited from a retail terrain chunk.
std::vector<uint8_t> serializeGlobalTexturePalette(
    const std::vector<std::string>& names);
std::vector<uint8_t> serializeExternalTexturePalette();
std::vector<uint8_t> serializeEmptyLocalDetailPalette(bool external = false);

// The 20-byte CEngineLandscapeMap root descriptor patched after generation:
// texture palette position/span, foreground patch-header position, and the
// background root file-block position/span. All positions are absolute within
// the static-map chunk before STB entry rebasing.
struct LandscapeRootDescriptor {
    int32_t texturePalettePos = 0;
    int32_t texturePaletteSize = 0;
    int32_t foregroundHeaderPos = 0;
    int32_t backgroundRootPos = 0;
    int32_t backgroundRootSize = 0;
};
std::vector<uint8_t> serializeLandscapeRootDescriptor(
    const LandscapeRootDescriptor& descriptor);

// The 12-byte descriptor reserved by GenerateStaticMapEntry before SaveTree:
// absolute header position, header+palette span, and an EBOOL-present dword.
std::vector<uint8_t> serializeLocalDetailRootDescriptor(
    int32_t headerPos, int32_t headerSpan, bool present = true);

struct BackgroundLodRecord {
    uint8_t optimizedBandRemap = 0;
    int32_t fileBlockPos = 0;
    int32_t fileBlockSize = 0;
    int32_t offsetIntoFileBlock = 0;
};

// Exact SaveHeader(CLandscapeBackgroundTreeNode) field order.  LOD records are
// emitted inclusively for [firstNonSplitBand,lastBand] when that range is below
// band 8.  This owns the background-tree control bytes needed by a donor-free
// chunk instead of preserving an opaque donor header.
struct BackgroundTreeHeader {
    uint16_t mapX = 0, mapY = 0, width = 0, height = 0;
    uint8_t firstBand = 0, firstNonSplitBand = 0, lastBand = 0;
    int32_t fileBlockPos = 0, fileBlockSize = 0, offsetIntoFileBlock = 0;
    float aabb[6] = {};
    std::vector<BackgroundLodRecord> lod;
};
std::vector<uint8_t> serializeBackgroundTreeHeader(
    const BackgroundTreeHeader& header);
BackgroundTreeHeader parseBackgroundTreeHeader(
    const std::vector<uint8_t>& bytes, size_t offset = 0,
    size_t* consumed = nullptr);

struct BackgroundTreeNode {
    BackgroundTreeHeader header;
    size_t headerOffset = 0;
    std::vector<BackgroundTreeNode> children;
};

// Parse the native recursive background tree. A live child file-block tuple
// denotes a split; this includes large-map (8,8,8) zero-LOD wrapper roots.
// FileBlockPos + OffsetIntoFileBlock points at two adjacent child headers. The parser rejects cycles, truncated child
// pairs, invalid map rectangles, and out-of-range file-block references.
BackgroundTreeNode parseBackgroundTree(
    const std::vector<uint8_t>& chunk, size_t rootHeaderOffset);

// Build a deterministic 64x64 authored split tree: X at 64x64, then alternate
// axes down to sixteen 16x16 leaves. Node rectangles are map-local; AABBs use
// world coordinates and exact heightfield extrema. LOD records carry the
// retail 64/32/16 band schedule but remain unwired until file-block layout.
BackgroundTreeNode buildBackgroundTreeShape64(
    const terrain::Heightfield& heightfield, int worldX, int worldY);
// Authored rectangular variants proven by shipped roots: 32x32, 32x64,
// 64x32, and 64x64. Remainder roots split along their 64-unit axis.
BackgroundTreeNode buildBackgroundTreeShape(
    const terrain::Heightfield& heightfield, int worldX, int worldY);

struct NativeBackgroundLodSettings {
    float meshDetail = 2.0f;
    float proceduralTextureDetail = 3.0f;
    float foregroundFade = 48.0f;
    float firstLodZ = 64.0f;
    int staticMapQuality = 8;
    // Diagnostic only: reproduce Fable.exe's table-seeded/x87 vector
    // normalisation. The default remains the stronger current retail oracle
    // until this mode is independently validated.
    bool compilerFastNormals = false;
    // Diagnostic for the distinct ego_r static-map producer: its
    // PeekMapNormal uses a fixed 2*2 Z term after the two slope normalisations.
    bool egoProducerNormals = false;
};

// Diagnostic witness for threshold propagation. `parent` is the strictly
// higher dependency selected when this point was promoted; `source` is the
// base-threshold point at the end of that chain. Equal-valued dependencies are
// intentionally not assigned causal significance because the engine stores
// only the resulting byte threshold.
struct NativeThresholdProvenance {
    int parentX = -1, parentY = -1;
    int sourceX = -1, sourceY = -1;
    float pointBaseZ = 0.0f;
    float sourceBaseZ = 0.0f;
};

// Dynamic-template tree producer recovered from
// BuildTreeSubdivideIntoPowersOf2 + BuildTree + the recursive LOD lock test.
// This overload models the no-observing-map-set path used by the corpus
// oracle; visibility-distance band suppression remains a separate gate.
BackgroundTreeNode buildNativeAdaptiveBackgroundTreeShape(
    const terrain::Heightfield& heightfield, int worldX, int worldY,
    NativeBackgroundLodSettings settings = {},
    std::function<float(int x, int y)> sampleHeight = {});

// Retail CEngineLandscapeLODMap threshold producer. The returned byte grid is
// X-fast and contains (width+1)*(height+1) entries. Defaults are the literal
// CEngineLandscapeRenderer constructor values; qualities below 8 double the
// mesh-detail divisor once per level, matching GenerateStaticMapEntry.
std::vector<uint8_t> buildNativeBackgroundLodThresholds(
    const terrain::Heightfield& heightfield,
    NativeBackgroundLodSettings settings = {},
    std::function<float(int x, int y)> sampleHeight = {},
    std::vector<NativeThresholdProvenance>* provenance = nullptr);

struct NativeBackgroundLodTopology {
    std::vector<std::array<uint16_t, 2>> vertices;
    std::vector<uint16_t> indices;
};

// Retail adaptive midpoint topology before CacheOptimizeBuffers and edge-fan
// stitching. Coordinates are map-local. maxSubsectionX/Y are the proven
// dynamic-template +0x10/+0x12 fields for this band.
NativeBackgroundLodTopology buildNativeBackgroundLodTopology(
    const std::vector<uint8_t>& thresholdMap, int mapWidth, int mapHeight,
    int x, int y, int width, int height, uint8_t band,
    int maxSubsectionX, int maxSubsectionY);

struct BackgroundTreeLayout {
    std::vector<uint8_t> bytes; // starts at requested startOffset, including leading alignment
    BackgroundTreeNode root;    // fully wired copy
    size_t startOffset = 0;
    size_t rootHeaderOffset = 0;
    size_t treeBlockSpan = 0;
    size_t payloadCount = 0;
};

struct InlineTexture;

// Emit the authored tree in native SaveRootFileBlockAndChildren order. The
// current authored path has one literal rectangular body per node; following
// SavePatchesToTemporyStream, its first band owns the payload and equal later
// bands remap to it with null file tuples. Tree and owning payload file-block
// positions are absolute chunk offsets and page aligned.
// Per-node background texture: called with the node's map-local cell rect
// (x, y, w, h) and, when an existing texture is being replaced in place, the
// size it must keep (texW, texH; 0 = the provider's choice -- retail leaves are
// mostly 64x64 DXT1, single mip). Returning an empty texture (width 0) keeps
// the fallback / existing texture.
using BackgroundTextureProvider = std::function<InlineTexture(int x, int y, int w, int h, int texW, int texH)>;
BackgroundTreeLayout layoutBackgroundTree(
    BackgroundTreeNode root,
    const terrain::Heightfield& heightfield,
    int worldX, int worldY,
    const InlineTexture& texture,
    size_t startOffset, size_t alignment = 2048,
    const BackgroundTextureProvider& provider = {});

// Exact 0x2C CLocalDetailCacheMap quadtree header written by FableWin.  Empty
// maps still carry this spatial/file-block header even when their object palette
// count is zero.
struct LocalDetailQuadHeader {
    // CBoundingSphere (centre xyz + radius), followed by the maximum fade
    // distance and the primitive-type mask used to cull the whole tree.
    float sphere[4] = {};
    float maxFade = 0.0f;
    uint32_t primitiveMask = 0;
    int32_t fileBlockPos = 0, fileBlockSize = 0, offsetIntoFileBlock = 0;
    uint16_t cellBounds[4] = {};
};
std::vector<uint8_t> serializeLocalDetailQuadHeader(
    const LocalDetailQuadHeader& header);

// Complete donor-free empty local-detail section in the exact native write
// order: reserve the 12-byte root descriptor, align and write the root file
// block ([groupCount=0][four childPresent=0]), then append the 0x2C root
// header and empty object palette before backfilling the descriptor. Offsets in
// the result and header are absolute chunk offsets; `sectionOffset` is where
// the returned byte vector will be inserted in the final STB entry.
struct EmptyLocalDetailSection {
    std::vector<uint8_t> bytes;
    size_t descriptorOffset = 0;
    size_t fileBlockOffset = 0;
    size_t headerOffset = 0;
    size_t headerSpan = 0;
};
EmptyLocalDetailSection buildEmptyLocalDetailSection(
    size_t sectionOffset, size_t fileBlockAlignment,
    LocalDetailQuadHeader rootHeader);

struct LocalDetailPlacement {
    int paletteIndex = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float rotationRadians = 0.0f;
    float scale = 1.0f;
    // Terrain surface normal under the instance, sampled with the engine's own
    // CMap::PeekMapNormal (see mapNormal). Type-1 RepeatedMesh primitives store
    // these in a whole-array SoA block and ProcessLightingSW turns each one into
    // that instance's vertex-shader lighting constant, so a degenerate normal
    // renders the blade ambient-only.
    float nx = 0.0f, ny = 0.0f, nz = 1.0f;
};

// CEnginePrimitiveManagerRepeatedStaticMeshes::MAX_BATCH_SIZE. A type-1
// primitive may hold at most this many instances. See docs
// FORGETEST64_DARK_TERRAIN_AND_FOLIAGE.md section 8 for why the format's
// ObjectCount<256 assert is NOT the operative bound.
inline constexpr size_t kRepeatedMeshMaxBatch = 32;

// --- CLocalDetailPrimitiveRepeatedMesh subsection builder -------------------
// A direct port of the engine's own
// CLocalDetailPrimitiveRepeatedMesh::BuildSubSectionsAndObjectRemapTable --
// the public driver at FableWin 0x02EDF740 and the private recursive worker at
// 0x02EDFB20 (Engine_Local_Detail_Primitives.cpp, asserts on lines 580/581/
// 645/646/756/829). Nothing here is fitted to retail output: the grid
// resolution, the corner sweep, the quotas, the pre-order emission and the
// relative child offsets are all transcribed from that disassembly. See
// docs/FOLIAGE_LOCAL_DETAIL_RE.md and the 2026-08-22 subsection RE notes.

// C3DBoundingSphere, 16 bytes. Retail feeds one per source object, built as
// centre = objectMatrix.TransformPoint(mesh.boundingSphere.centre) and
// radius = mesh.boundingSphere.radius * object.scale (0x02EE1364..0x02EE13F9).
struct SubsectionSphere {
    float x = 0.0f, y = 0.0f, z = 0.0f, radius = 0.0f;
};

// NEnginePrimitiveManagerRepeatedStaticMeshes::CSubsectionElement, 0x50 bytes,
// structure-of-arrays over four quadrant children.
//   +0x00 centreX[4] +0x10 centreY[4] +0x20 centreZ[4] +0x30 radius[4]
//   +0x40 count[4]   +0x44 startIndex[4] +0x48 childOffset[4] +0x4C unwritten
// startIndex is an ABSOLUTE index into the remap array; childOffset is the
// distance in ELEMENTS from this element to the child's, 0 for a leaf.
struct SubsectionElement {
    float centreX[4]{}, centreY[4]{}, centreZ[4]{}, radius[4]{};
    uint8_t count[4]{}, startIndex[4]{}, childOffset[4]{};
    // The engine never writes 0x4C..0x4F; retail ships uninitialised stack
    // residue there (0000ff07, 00008b00, leftover floats...). It is not
    // derivable from any algorithm, so the port writes zeros and a byte diff
    // against retail must mask these four bytes per element.
    uint8_t tail[4]{};
};

struct SubsectionTable {
    // False when the worker bailed (count <= maxObjectsPerSubSection, or fewer
    // than two non-empty sections). Retail then allocates nothing and leaves
    // the remap an identity permutation -- the writer emits presence byte 0.
    bool present = false;
    std::vector<SubsectionElement> elements;
    // remap[destination] = source instance index. Always sized to the input;
    // identity when !present.
    std::vector<uint8_t> remap;
};

// The public driver, 0x02EDF740. `spheres` is one entry per source object in
// authoring order; `maxObjectsPerSubSection` is retail's
// min(4, max(1, 128 / PeekPolyCount())) leaf threshold, forwarded unchanged
// through every recursion level. Throws if the input violates the engine's own
// bounds (0 < count < 256, and the 32-element scratch array the driver uses).
SubsectionTable buildSubSectionsAndObjectRemapTable(
    const std::vector<SubsectionSphere>& spheres,
    long maxObjectsPerSubSection);

// elements.size() * 0x50 bytes in the engine's in-memory (and on-disk) order.
std::vector<uint8_t> serializeSubsectionElements(
    const std::vector<SubsectionElement>& elements);

// The leaf threshold is min(4, max(1, 128 / polyCount)); a foliage blade is a
// handful of triangles, which saturates the upper clamp, so 4. Measured against
// retail this is not a free knob: intersecting the admissible thresholds per
// collection type over 1,524 baked tables leaves exactly one common value per
// type, and it is only ever 1 or 4 -- what the formula predicts.
//
// The instance sphere radius that used to sit here as an assumed 100.0f is
// GONE. It is real authored data: the mesh bank's Info origin[3]
// (forge::foliage::readMeshBoundingSpheres). A writer with no mesh bank emits no
// subsection table instead of guessing.
inline constexpr long kLocalDetailLeafThreshold = 4;

// Where map cell (0,0) sits in world space. The engine bins local-detail
// placements SPATIALLY -- CLocalDetailCacheMap::GenerateStaticMapEntry
// (0x02E3BF00) drives CQuadTreeElement::UpdateDynamicArea (0x02E3F400) over
// every 16x16-CELL rectangle of the map, and a leaf's
// GetPrimitivesFromMap (0x02E3DD50) only ever emits objects for its own cells --
// so a writer holding world-space placements has to be told the cell grid.
// Retail chunks are one world unit per cell: buildTerrainChunk64 emits its
// cameraMapBounds as worldX .. worldX + mapWidth for mapWidth cells.
struct LocalDetailCellGrid {
    float worldOriginX = 0.0f, worldOriginY = 0.0f;
    float unitsPerCell = 1.0f;
};

// CQuadTreeElement::AssignFileBlocks (0x02E3F230) compares a node's estimated
// inline save size against CEngineLocalDetailGenerator::GetMaxFileBlockSize
// (0x02E3F3E0 -> generator+0x80).
//
// The generator constructor writes 0x8000 directly to +0x80 at 0x02D27014.
// Retail data independently brackets it to (30289, 33513] across four maps.
inline constexpr size_t kLocalDetailMaxFileBlockSize = 32768;

EmptyLocalDetailSection buildType0LocalDetailSection(
    size_t sectionOffset, size_t fileBlockAlignment,
    LocalDetailQuadHeader rootHeader,
    const std::vector<foliage::FoliageType>& palette,
    const std::vector<LocalDetailPlacement>& placements,
    LocalDetailCellGrid grid);

enum class TerrainSectionKind {
    RootControl,
    ForegroundFrame,
    TexturePalette,
    BackgroundFrame,
    BackgroundTree,
    LocalDetail,
    Tail
};

struct TerrainSectionInput {
    TerrainSectionKind kind = TerrainSectionKind::Tail;
    std::string name;
    std::vector<uint8_t> bytes;
    size_t alignment = 1;
};

struct TerrainSectionPlacement {
    TerrainSectionKind kind = TerrainSectionKind::Tail;
    std::string name;
    size_t offset = 0;
    size_t size = 0;
};

// Deterministic section planner for a newly-authored chunk.  It starts from an
// empty byte vector, applies each explicit alignment, and never parses/copies a
// retail chunk.  Frame/control serializers can use the first pass's placements
// to wire their offsets, then run the same layout again for byte-stable output.
struct TerrainChunkLayout {
    std::vector<uint8_t> bytes;
    std::vector<TerrainSectionPlacement> sections;
    const TerrainSectionPlacement* find(std::string_view name) const;
};
TerrainChunkLayout layoutTerrainSections(
    const std::vector<TerrainSectionInput>& inputs,
    uint8_t paddingByte = 0);

// Build the common-header InfoBlock from generated placement/bounds data.  The
// three pointer fields are section offsets from `layout`; absent edge/shore/
// checksum data are emitted in their canonical zero form.
stbinfo::StaticMapInfoBlock makeTerrainInfoBlock(
    const TerrainChunkLayout& layout,
    std::string_view landscapeSection,
    std::string_view localDetailSection,
    int32_t bankFileIndex,
    int32_t mapWidth, int32_t mapHeight,
    int32_t worldX, int32_t worldY,
    float minZ, float maxZ,
    int32_t versionID = 1,
    int32_t quality = 9);

// Emit the canonical empty-control-stream common-header record for a newly
// authored terrain map. appendStaticMap rebases its record-relative positions.
std::vector<uint8_t> buildTerrainCommonRecord(
    const stbinfo::StaticMapInfoBlock& chunkInfo,
    const std::vector<uint8_t>& chunk,
    bool includeLocalDetail = true,
    bool includeLandscape = true);

struct TerrainChunk64Result {
    std::vector<uint8_t> chunk;
    stbinfo::StaticMapInfoBlock info;
    size_t landscapeDescriptorOffset = 0;
    size_t foregroundDirectoryOffset = 0;
    size_t backgroundRootOffset = 0;
    size_t localDetailDescriptorOffset = 0;
};

// Assemble a complete 32/64 rectangular static-map chunk from authored inputs
// only. The legacy function name is retained for API compatibility. Authored
// foliage currently remains restricted to 64x64. The
// foreground uses generated palette indices/names; the background owns its
// inline texture payload. No donor chunk is accepted by this API.
TerrainChunk64Result buildTerrainChunk64(
    const terrain::Heightfield& heightfield,
    int worldX, int worldY, int bankFileIndex,
    const terrain::TerrainMaterialTuple& foregroundMaterial,
    const std::vector<std::string>& foregroundTextureNames,
    const InlineTexture& backgroundTexture,
    // Native callers pass CEngineWorldMap::GetBankFileAlignment, which delegates
    // to CBankFile::GetAlignment (bank+0xC0). Retail FinalAlbion_RT.stb is 2048.
    size_t alignment = 2048,
    // Optional extra foreground layers, blended by slope and by height. Leave
    // textures[0] == 0 to emit the single-layer foreground.
    const terrain::TerrainMaterialTuple& slopeMaterial = {},
    const terrain::TerrainMaterialTuple& heightMaterial = {},
    bool authorFoliage = false,
    // Path to a mesh bank (data/graphics/graphics.big). Foliage subsection
    // tables need the referenced mesh's authored bounding sphere; without a bank
    // the writer emits no table rather than guess a radius.
    const std::filesystem::path& meshBank = {},
    bool foliageSubsections = true,
    // Supplying both arguments selects the recovered retail LEV theme path.
    // Leaving them empty preserves the explicit single/slope/highland profiles.
    const std::vector<terrain::TerrainThemeMaterial>& themeMaterials = {},
    const std::function<terrain::ThemeBlend(int,int)>& themeAt = {},
    // Per-node distant-LOD textures (baked albedo) instead of the one backgroundTexture.
    const BackgroundTextureProvider& backgroundProvider = {});

// One LZO-framed block decoded out of a bank chunk: its uncompressed payload plus
// where the frame sat, so a round-trip can re-emit byte-identically.
struct FramedBlock {
    size_t frameOffset = 0;  // offset of the [uncompLen][compLen] header in the chunk
    uint32_t compLen = 0;    // on-disk compressed body length
    std::vector<uint8_t> data; // decoded (uncompressed) bytes
};

// Walk a whole bank-chunk payload and decode every standard-LZO1X framed block in
// it (the SaveCompressed layout). Non-block regions (raw DXT / index / quadtree)
// are skipped. This is the decoder half — proven against retail chunks — and the
// basis for the round-trip validation of the emitter.
std::vector<FramedBlock> walkFramedBlocks(const std::vector<uint8_t>& chunk);

// --- Full segment model (ports chunk_parse.py) -----------------------------
// Every byte of a bank chunk is exactly one segment: a LZO FRAME, an opaque
// structured HDR (block directory / sub-headers / palettes / foreground array),
// or zero PAD. This classification is what lets an emitter preserve the opaque
// HDR/quadtree bytes from a donor verbatim and rewrite only the mesh FRAMEs.

enum class SegKind { Frame, Hdr, Pad };

struct Segment {
    size_t start = 0;
    size_t end = 0; // exclusive
    SegKind kind = SegKind::Pad;
    uint32_t uncompLen = 0; // FRAME only
    uint32_t compLen = 0;   // FRAME only
};

struct Chunk {
    std::vector<uint8_t> raw;           // the original bytes (segments index into this)
    std::vector<Segment> segments;      // sorted, gap-free, covers [0, raw.size())
    std::vector<size_t> frameIndices;   // indices into `segments` that are Frames

    size_t frameBytes() const;
    size_t hdrBytes() const;
    size_t padBytes() const;
};

// Classify 100% of a chunk's bytes into ordered FRAME/HDR/PAD segments. Frames
// are found by the honest decode-to-exact-length gate; inter-frame gaps become
// HDR (non-zero, with any trailing zeros split off as PAD) or PAD (all zero).
Chunk parseChunk(const std::vector<uint8_t>& data);

// Re-emit the chunk from its segment model. With no edits this is byte-identical
// to Chunk::raw — the round-trip proof that the parse is complete and lossless.
std::vector<uint8_t> reserialize(const Chunk& chunk);

// --- Frame contents (CLandscapeBackgroundPatch::Save grammar) --------------
// Decompress one FRAME segment's LZO body back to the raw Save stream. `frame`
// is an index into Chunk::frameIndices (0 = first frame). Throws if out of range.
std::vector<uint8_t> decodeFrame(const Chunk& chunk, size_t frame);

struct ForegroundVertex {
    uint16_t x = 0, y = 0;
    float height = 0.0f;
    uint32_t packedNormal = 0;
    uint8_t blend = 0, cliffU = 0, cliffV = 0;
};

struct ForegroundLayer {
    uint16_t polygonCount = 0;
    uint8_t mappingDirection = 0;
    uint32_t textures[3] = {};
    bool sharedIndexBuffer = false;
    uint32_t textureMaxSize = 0;
    uint32_t bumpMaxSize = 0;
    float selfIllumination = 0.0f;
    std::vector<ForegroundVertex> vertices;
    std::vector<uint16_t> indices;
};

struct ForegroundFrame {
    std::vector<ForegroundLayer> layers;
    bool hasWater = false;
    std::vector<uint8_t> waterPayload;
};

// Build a complete foreground grid directly from an authored heightfield.  One
// full-coverage layer is emitted per 16x16 patch using an explicit global
// material tuple; no donor frame topology, vertices, indices, or textures are
// inspected.  This is the minimal empty-map authoring profile.
// Editor-authored material profile: an opaque base plus optional slope/highland
// coats. Its thresholds are a Forge policy, not a recovered retail theme bake.
// Use buildThemedForeground for the recovered LEV contribution/topology path.
// Tuples with textures[0] == 0 are skipped.
std::vector<ForegroundFrame> buildLayeredForeground(
    const terrain::Heightfield& heightfield,
    int worldX, int worldY,
    const terrain::TerrainMaterialTuple& base,
    const terrain::TerrainMaterialTuple& slopeMaterial,
    const terrain::TerrainMaterialTuple& heightMaterial);

std::vector<ForegroundFrame> buildSingleMaterialForeground(
    const terrain::Heightfield& heightfield,
    int worldX, int worldY,
    const terrain::TerrainMaterialTuple& material);

// Retail ReadThemesAndCreateLayers -> BuildLayerMesh foreground assembler.
// themeAt returns the literal three-slot blend for a heightfield vertex.
// Unlike buildLayeredForeground, this contains no authored slope/height rules.
using ThemeBlendSampler = std::function<terrain::ThemeBlend(int x, int y)>;
std::vector<ForegroundFrame> buildThemedForeground(
    const terrain::Heightfield& heightfield,
    int worldX, int worldY,
    const std::vector<terrain::TerrainThemeMaterial>& themes,
    const ThemeBlendSampler& themeAt);

// CLandscapeLayerMesh::LoadForeground/Save frame codec. Parsing rejects bodies
// that do not consume exactly; serialization preserves the 15-byte vertex tail
// order Blend, CliffU, CliffV used on disk.
ForegroundFrame parseForegroundFrame(const std::vector<uint8_t>& body);
std::vector<uint8_t> serializeForegroundFrame(const ForegroundFrame& frame);

// The §3 header at the start of a foreground/local-detail patch Save stream.
// (The four page-aligned background-LOD frames use a different CLandscapeBackground
// TreeNode header and will NOT validate here — that's the classifier.)
struct PatchHeader {
    uint16_t pw = 0;          // patch grid width  (subdiv count X)
    uint16_t ph = 0;          // patch grid height (subdiv count Y)
    uint16_t coord0 = 0;      // map-local patch origin X
    uint16_t coord1 = 0;
    bool isWaterOnly = false; // if true, no mesh (VB/IB/texture skipped)
    uint8_t detailMode = 0;   // 1 = full-detail patch
    uint16_t indexCount = 0;  // triangle count (implicit for background patches)
    uint16_t vertexCount = 0; // VB holds vertexCount verts
    uint8_t texExtX = 0;
    uint8_t texExtY = 0;
    bool isDXT = false;
    bool valid = false;       // (pw+1)*(ph+1)==vertexCount held (real §3 patch)
};

// Parse the §3 header from a decoded frame body. Sets valid=true only when the
// grid invariant (pw+1)*(ph+1)==vertexCount holds (proven dead-on for foreground
// patches; false for the tree-node background-LOD frames).
PatchHeader parsePatchHeader(const std::vector<uint8_t>& frameBody);

// --- Patch AUTHORING primitives (the native terrain bake write side) --------
// A landscape patch vertex as serialized in the VB (16 bytes on disk, per
// CLandscapeBackgroundPatch::Save @0x02ce3220): [u16 gridX][u16 gridY]
// [f32 height][u32 packedNormal][u16 uv0][u16 uv1]. Height is the authored world
// Z (PeekLandscapeHeight); packedNormal is the 11/11/10 DEC3N-style dword.
struct PatchVertex {
    uint16_t gridX = 0;
    uint16_t gridY = 0;
    float    height = 0.0f;
    uint32_t packedNormal = 0;
    uint16_t uv0 = 0;
    uint16_t uv1 = 0;
};

// Pack a unit normal into the engine's 11/11/10 dword (x*1023, y*1023, z*511;
// masks 0x7FF/0x7FF/0x3FF; ix | iy<<11 | iz<<22). Byte-identical to EgoCore's
// PackNormal, which the retail engine loads correctly (verified in the mesh RE).
uint32_t packNormal(float nx, float ny, float nz);

// --- Engine-ported terrain normal / direction mask --------------------------
// Height lookups are in MAP-LOCAL CELL coordinates. CMap::PeekLandscapeHeight
// addresses cells (0..width-1), not the serialized extra vertex row/column, so
// x==width belongs to an adjacent map; callers supply the resolution policy
// (neighbour LEV or clamp).
using HeightSampler = std::function<float(int x, int y)>;

// The engine's in-memory landscape height is the 1/128-quantised LEV value.
float quantizeEngineHeight(float rawHeight);

// Retail CameraMapBounds use the quantized terrain minimum and reserve 40
// world units above the quantized terrain maximum for camera streaming.
void setRetailCameraHeightBounds(stbinfo::StaticMapInfoBlock& info,
                                 float rawMinHeight, float rawMaxHeight);

// Clamped PeekLandscapeHeight over a standalone heightfield: out-of-map samples
// clamp to the last real cell, matching the engine when no neighbour is loaded.
HeightSampler clampedHeightSampler(const terrain::Heightfield& heightfield);

// CMap::PeekMapNormal: each two-cell axis slope is normalised BEFORE the axes
// are combined; a plain height gradient is only equivalent on shallow slopes.
uint32_t packMapNormal(const HeightSampler& sampleHeight, int x, int y);

struct DirectionNormal { float x = 0.0f, y = 0.0f, z = 0.0f; };

// The unit normal packMapNormal packs, for callers that need it unquantised
// (the type-1 LandscapeNormalArray stores raw floats).
DirectionNormal mapNormal(const HeightSampler& sampleHeight, int x, int y,
                          bool compilerFastNormals = false,
                          bool egoProducerNormals = false);

struct NativeThresholdTrace {
    int ax = 0, ay = 0, bx = 0, by = 0;
    float centerHeight = 0.0f;
    float midpointHeight = 0.0f;
    float midpointError = 0.0f;
    float midpointTerm = 0.0f;
    float dotA = 0.0f, dotB = 0.0f;
    float normalError = 0.0f;
    float endpointDistance = 0.0f;
    float normalTerm = 0.0f;
    float baseZThreshold = 0.0f;
    bool boundarySentinel = false;
};

// Instruction-ordered companion to CalculateBaseZThreshold @0x02E014E0.
// meshDetail is the already quality-adjusted divisor used for this bake.
NativeThresholdTrace traceNativeBackgroundLodThreshold(
    const HeightSampler& sampleHeight, int mapWidth, int mapHeight,
    int x, int y, float meshDetail, bool compilerFastNormals = false,
    bool egoProducerNormals = false);

// CEngineLandscapeMeshBuilder::BuildMapDirMask (retail 0x009BF540 / FableWin
// 0x02CAE270). Vertices with world (x^y)&1 == 0 accumulate eight surrounding
// triangle normals, odd vertices four; each face normal is normalised then
// weighted by the product over XYZ of 0.5/(|c|+0.0625) + 0.5294118; the weighted
// sum is normalised. Verified byte-exact against retail (2,251/2,251 CliffU and
// CliffV records, tools/compare_retail_direction_mask.py).
DirectionNormal buildMapDirMask(const HeightSampler& sampleHeight,
                                int worldX, int worldY, int x, int y);

// CliffU/CliffV encoding: int((component * 0.5 + 0.5) * 255), TRUNCATING.
uint8_t packDirMaskByte(float component);

// Serialize N patch vertices to the on-disk 16-bytes/vertex VB blob (little-endian).
std::vector<uint8_t> serializePatchVB(const std::vector<PatchVertex>& verts);

// Frame an element blob as a CRangeCompressor block the patch writer wraps:
// [s32 compLen LE][ 0x00 RAW flag ][ count*stride element bytes ]. This is the
// exact framing CLandscapeBackgroundPatch::Save emits (WriteSLONG(compLen);
// WriteData) with CRangeCompressor's flags==0 stored fallback.
std::vector<uint8_t> rangeBlockRaw(const uint8_t* elems, size_t count, size_t stride);

// Serialize the 17-byte §3 patch header (inverse of parsePatchHeader): the
// leading fields a non-water patch body begins with, up through isDXT.
std::vector<uint8_t> serializePatchHeader(const PatchHeader& h);

// Exact 19-byte header written by CTexture::SaveToDataStream in FableWin.
// Pixel-format fields are kept explicit because they are engine values rather
// than inferred from a donor texture. `mipData` follows the header verbatim.
struct InlineTexture {
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t levels = 1;
    uint32_t pixelFormat0 = 0;
    uint16_t pixelFormat1 = 0;
    uint32_t usage = 0;
    uint32_t surfacePool = 1; // D3DPOOL_MANAGED / editor's normal saved texture
    std::vector<uint8_t> mipData;
};

std::vector<uint8_t> serializeInlineTexture(const InlineTexture& texture);
InlineTexture parseInlineTexture(const std::vector<uint8_t>& bytes);

// Reduce a DXT1 background texture to the single mip the engine actually reads.
// CLandscapeBackgroundPatch::Load allocates a 1-level surface and
// CTexture::LoadFromDataStreamToPreallocatedSurface bounds its mip loop by that
// surface level count, so any extra levels stay in the stream and shift the
// patch-body cursor. Non-DXT textures are returned unchanged.
InlineTexture singleLevelBackgroundTexture(const InlineTexture& texture);

// Canonical no-edge/no-water trailer written by four
// CPatchTesselationEdgeStrip::Save calls followed by EBOOL hasWater=false.
// Each empty strip is [u16 bound0][u16 bound1][EBOOL flag][4*s32 zero-count].
std::vector<uint8_t> serializeEmptyPatchTrailer(uint16_t bound0 = 0,
                                                uint16_t bound1 = 0);

struct PatchBody;
// Build one complete non-water background patch from authored data only.
// patchX/patchY/patchSize and the patch-header coordinates are map-local cells;
// worldX/worldY place the emitted UWORD vertices. The texture is emitted inline
// and no donor tail is retained.
PatchBody buildBackgroundPatch(const terrain::Heightfield& heightfield,
                               int patchX, int patchY, int patchSize,
                               int worldX, int worldY,
                               const InlineTexture& texture);
PatchBody buildBackgroundPatchRect(const terrain::Heightfield& heightfield,
                                   int patchX, int patchY,
                                   int patchWidth, int patchHeight,
                                   int worldX, int worldY,
                                   const InlineTexture& texture);

// --- Full patch-body model (decompressed FRAME body) -----------------------
// The complete non-water body layout, confirmed byte-for-byte against
// CLandscapeBackgroundPatch::Save @0x02ce3220 (FableWin editor donor,
// docs/TERRAIN_NATIVE_BAKE.md §"Save body layout"):
//   [17B header] [inline texture] [VB block] [IB block?] [edge strips + water]
// where VB/IB block = [s32 compLen][CRangeCompressor block]; the texture and the
// trailing edge-strips/water region are opaque spans (kept verbatim on a height
// edit — the ForgeTest "same topology, same theme" case). A water-only patch
// (header.isWaterOnly) is a 10-byte header + opaque trailer, no texture/VB/IB.
struct PatchBody {
    PatchHeader header;
    std::vector<uint8_t> texture;  // opaque texture span (verbatim)
    std::vector<uint8_t> vbBlock;  // framed [s32 len][range block]
    std::vector<uint8_t> ibBlock;  // framed [s32 len][range block]; empty if absent
    std::vector<uint8_t> trailer;  // edge strips + water sub-patch (verbatim)
    bool waterOnly = false;
    bool valid = false;            // false if the VB block couldn't be located
};

struct AuthoredBackgroundPatch {
    PatchBody body;
    std::array<float, 6> aabb{}; // minX,minY,minZ,maxX,maxY,maxZ
    bool root = false;
};

// Canonical 64x64 background set: sixteen 16x16 leaf patches. The tree root is
// metadata, not a synthetic 64x64 patch (measured on retail Darkwood_3).
std::vector<AuthoredBackgroundPatch> buildBackgroundPatchGrid(
    const terrain::Heightfield& heightfield, int worldX, int worldY,
    const InlineTexture& texture);

// Decode the patch's framed range-compressed VB into its 16-byte vertex records.
// This is the read-side companion to serializePatchVB and preserves the authored
// grid coordinates needed to map a LEV heightfield onto donor topology.
std::vector<PatchVertex> decodePatchVertices(const PatchBody& pb);

// The patch's triangles as index triples into decodePatchVertices' order: the local index
// buffer's strip when the patch has one (degenerate steps dropped), else the full pw x ph grid.
std::vector<std::array<uint16_t, 3>> patchTriangles(const PatchBody& pb, const std::vector<PatchVertex>& verts);

// Offset of the water flag byte in a background patch trailer (past the four
// CPatchTesselationEdgeStrips), or SIZE_MAX when the trailer does not parse.
size_t trailerWaterFlagOffset(const std::vector<uint8_t>& trailer);

// Segment a decompressed FRAME body into its Save-order spans. The texture region
// is variable-length and not self-terminating from the header alone, so the VB
// block is located by a VERIFIED search: each candidate offset after the header is
// tried as [s32 len][block] and accepted only when decode() yields exactly
// vertexCount*16 unique coordinate pairs whose bounds span pw/ph; a following local
// IB is decoded when present (other patches use the shared index buffer). Adaptive
// meshes intentionally omit grid points, so (pw+1)*(ph+1) is not a valid test. The compressed
// descriptor grammar is matched to FableWin CRangeCompressor::Decompress and its
// GetBlockSizeFromFlags/ReadVarSizedUInt helpers.
PatchBody parsePatchBody(const std::vector<uint8_t>& body);

// Compose a complete decompressed patch body in exact Save order. When `newHeights`
// is non-empty it must have header.vertexCount entries: the VB block is rebuilt from
// pb's existing VB (grid/normal/uv preserved) with only the per-vertex height (f32)
// replaced, then RAW-range-framed. Otherwise pb.vbBlock is emitted verbatim (identity).
std::vector<uint8_t> assemblePatchBody(const PatchBody& pb,
                                       const std::vector<float>& newHeights = {});

// Rebuild the patch with a complete authored VB. Topology/count, texture, IB and
// trailer remain donor-derived; grid X/Y, height, normal and UV may be replaced.
std::vector<uint8_t> assemblePatchBodyVertices(
    const PatchBody& pb, const std::vector<PatchVertex>& vertices);

// Rebuild a complete authored VB while preserving the donor VB block span
// exactly.  A compact CRange descriptor stream is padded after its terminator to
// the original block length.  Keeping the decoded outer-frame body length fixed
// preserves every later logical file-block reference in the retail chunk.
std::vector<uint8_t> assemblePatchBodyVerticesFixedSpan(
    const PatchBody& pb, const std::vector<PatchVertex>& vertices);

// --- Foreground patch-header directory -------------------------------------
// A flat array of 0x24-byte CEngineLandscapePatch::SaveHeader records living in
// the first HDR block (retail base 0x800). Each entry is {u32 frameOffset,
// u32 frameSpan, 6*f32 AABB, u32 flags} (every retail entry: frameSpan == 8 + compLen at frameOffset). This is the FOREGROUND patch
// directory, not the variable-length background-tree control structure. The
// array is terminated by one entirely zero record.
struct QuadEntry {
    size_t dirOffset = 0;    // where this 0x24 record sits in the chunk
    uint32_t flags = 0;
    uint32_t frameOffset = 0; // chunk offset of the wired FRAME
    uint32_t frameSpan = 0;   // on-disk frame length (8 + compLen)
    float aabb[6] = {0,0,0,0,0,0}; // minX,minY,minZ,maxX,maxY,maxZ (donor order)
};

struct BackgroundFrameLayout {
    std::vector<uint8_t> bytes;
    std::vector<QuadEntry> entries;
};

// LZO-frame authored CLandscapeLayerMesh foreground records and produce their
// native 0x24 SaveHeader directory entries. AABBs are derived from serialized
// vertices; no donor directory or frame allocation is consulted.
BackgroundFrameLayout layoutForegroundFrames(
    const std::vector<ForegroundFrame>& frames,
    size_t startOffset, size_t alignment = 2048);

// LZO-frame authored leaf patches at absolute `startOffset`, padding every
// frame to `alignment`, and generate the matching 16-entry quad directory.
BackgroundFrameLayout layoutBackgroundFrames(
    const std::vector<AuthoredBackgroundPatch>& patches,
    size_t startOffset, size_t alignment = 2048);

// Locate the foreground patch-header directory and parse its live entries up to
// the first all-zero terminator. `dirBase` is the native 0x800 base.
std::vector<QuadEntry> parseQuadDir(const Chunk& chunk, size_t dirBase = 0x800);

// STEP 5: inverse of parseQuadDir — serialize the live entries as the 0x24-byte
// record array [flags, frameOffset, frameSpan, 6×f32 aabb] followed by a single
// all-zero 0x24 terminator record (parseQuadDir stops at frameOffset==span==0).
// Gate: generateQuadDir(parseQuadDir(donor)) reproduces the donor's entry bytes.
std::vector<uint8_t> generateQuadDir(const std::vector<QuadEntry>& entries);

// Rewrite only the minZ/maxZ members of the live 0x24-byte directory entries,
// preserving flags, frame wiring, X/Y bounds, terminator, and every other byte.
// The runtime copies these AABBs into CEngineLandscapePatch as well as its
// background nodes, so authored foreground heights must remain enclosed by them.
// Throws unless one finite ordered (minZ,maxZ) pair is supplied per live entry.
// Translate every live quad-directory AABB in X and Y by (dx, dy). Required
// when a baked chunk is relocated to a new map origin: retail keeps every AABB
// inside its own map's InfoBlock box (10257/10257 live entries, zero
// exceptions), so leaving donor coordinates in place corrupts the directory.
size_t translateQuadDirXY(std::vector<uint8_t>& chunkBytes, float dx, float dy);

size_t updateQuadDirZBounds(
    std::vector<uint8_t>& chunkBytes,
    const std::vector<std::pair<float, float>>& zBounds);

// --- Retarget (the WHITE-landscape workstream) -----------------------------
// A donor chunk already geometry-relocated to ForgeTest coords still has to have
// its background-LOD quadtree directory re-wired so every entry's frameOffset/
// frameSpan matches where its frame actually landed after relocation, and its
// AABB min<=max holds. retargetChunk verifies that wiring and re-emits the chunk;
// the returned report says whether the chunk is load-consistent. This is the
// offline gate before anyone deploys a retargeted chunk.
struct RetargetResult {
    std::vector<uint8_t> chunk;   // re-emitted bytes (== input when already consistent)
    bool consistent = false;      // true iff every quad entry wires to a real frame
    std::vector<std::string> notes;
};
RetargetResult retargetChunk(const std::vector<uint8_t>& donorRelocated);

// --- EMIT / ENCODE side (the write path) -----------------------------------
// parseChunk+reserialize prove we can round-trip a chunk byte-exact (identity).
// The emitter goes further: it assembles a *new* chunk from the segment model
// with the frame bodies re-encoded, re-laying the frames end-to-end and rewiring
// every dependent offset (the background-LOD quadtree directory) so the result is
// a self-consistent, engine-loadable chunk even when a re-encoded frame changed
// size. This is what lets forge OWN a custom terrain chunk natively instead of
// donor-swapping raw bytes.
//
// Frame re-encoding modes (both emit a valid standard-LZO1X frame the retail
// loader walks; no encoder parity with lzo1x_999 is needed to render):
//   RawPassthrough — re-emit each frame's on-disk body byte-for-byte. With no
//                    body edits this reproduces the source chunk EXACTLY (the
//                    identity round-trip: parse -> emit -> parse == input).
//   Recompress     — re-compress each decoded frame body with lzo1x_1. Bytes
//                    differ from retail (worse ratio) but decode identically, and
//                    the quad dir is rewired to the new frame offsets/spans.
enum class FrameCodec { RawPassthrough, Recompress };

// One authored edit: replace the decoded (uncompressed) body of frame `frameIndex`
// (an index into Chunk::frameIndices) with `newBody`. The frame is re-compressed
// (lzo1x_1) regardless of the global codec, and all downstream frames + the quad
// dir are re-laid to absorb the size delta. Bodies not listed are carried through
// per the chosen FrameCodec.
struct FrameEdit {
    size_t frameIndex = 0;
    std::vector<uint8_t> newBody;
};

struct EmitOptions {
    FrameCodec codec = FrameCodec::RawPassthrough;
    std::vector<FrameEdit> edits; // optional authored frame-body replacements
    // STEP 2 (AlignData): when non-zero, FRAME segments are re-laid on this byte
    // boundary (donor bank page = 0x1000). PAD segments (always zero-fill) are then
    // dropped and regenerated as the alignment gap, so a size-changing edit keeps
    // every frame page-aligned. 0 = legacy behavior (carry PAD verbatim, no realign).
    std::size_t frameAlign = 0;
    // Same topology can still change the decoded outer-frame body length when an
    // inner CRangeCompressor block is emitted in its documented RAW mode. The
    // native heightfield path opts into that representation explicitly; callers
    // performing arbitrary layout edits must leave this false.
    bool allowSameTopologyDecodedResize = false;
    // Use the retail writer's lzo1x_999 codec for edited frames.  This is needed
    // by fixed-span terrain authoring so each frame stays within its donor page.
    bool highCompressionEdits = false;
    // Preserve every donor segment start.  Edited frames may consume bytes from
    // the following PAD segment but may not cross its end.  This keeps all
    // physical file-block references stable without needing to discover/rebase
    // every foreground and local-detail pointer.
    bool preservePhysicalLayout = false;
};

// Round `pos` up to the next multiple of `align` (align a power of two or any >0).
std::size_t alignUp(std::size_t pos, std::size_t align);

// Convenience: turn an authored PatchBody into a FrameEdit for emitChunk (newBody is
// the assembled decompressed body; emitChunk applies the outer LZO frame).
FrameEdit patchBodyToFrameEdit(size_t frameIndex, const PatchBody& pb,
                               const std::vector<float>& newHeights = {});

struct EmitResult {
    std::vector<uint8_t> chunk;          // the assembled chunk bytes
    bool ok = false;                     // false if a precondition failed
    bool identity = false;               // true iff chunk == source (byte-exact)
    size_t framesReencoded = 0;          // frames whose on-disk bytes changed
    size_t quadEntriesRewired = 0;       // quad dir entries whose offset/span moved
    std::vector<std::string> notes;
};

// Assemble a chunk from a parsed segment model. HDR/PAD segments are preserved
// verbatim; FRAME segments are re-encoded per `opt`. When a frame's on-disk size
// changes, every later segment shifts by the running delta and the background-LOD
// quadtree directory (parseQuadDir) is rewired so each live entry's frameOffset/
// frameSpan again points at its frame. InfoBlock body positions belong to the
// editor's logical control stream, not physical LZO-frame offsets. Physical span
// changes therefore need no InfoBlock rebase. Replacements must keep their decoded
// body lengths unchanged; broader topology/layout edits are rejected.
EmitResult emitChunk(const Chunk& chunk, const EmitOptions& opt = {});

// Convenience: parse `source` and emit it back with RawPassthrough and no edits.
// Guarantees a byte-identical result for any chunk parseChunk fully covers — the
// canonical round-trip proof for the write path.
std::vector<uint8_t> emitIdentity(const std::vector<uint8_t>& source);

} // namespace forge::stbbake
