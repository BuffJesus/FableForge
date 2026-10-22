#pragma once
#include <cstdint>
#include <string>
#include <span>
#include <vector>
#include "forge/stbinfo.hpp"
#include "forge/stb.hpp"

namespace forge::stbstorage {
// Validate complete decoded terrain-frame storage domains, including nested
// range-coded geometry, water and edge arrays. Throws on unsupported/malformed
// layouts. Does not certify STB frame ownership, archive metadata, detail palettes,
// asset-ID destinations or geometry quality. Input bytes remain unchanged.
void validateForeground(const std::vector<uint8_t>& body);
// Supported background subset: one inline DXT1 texture, native detail-mode
// selection of shared/local indices, or water-only framing.
void validateBackground(const std::vector<uint8_t>& body);
// Replace only a background patch's single DXT1 surface. Preserve all geometry,
// edge/water data and unrelated header bytes verbatim. Allocation extents are
// bytes in the native patch header, so dimensions above 255 are refused unless
// allowExtent256: then 256 is stored as 0, which only TerrainExtent256.dll
// (tools/texture-runtime) allocates as 256. Retail never stores a zero extent.
std::vector<uint8_t> replaceBackgroundTexture(const std::vector<uint8_t>& body,
    uint16_t width, uint16_t height, const std::vector<uint8_t>& dxt1, bool allowExtent256 = false);
// Validate all three local-detail primitive layouts. Returns each collection's
// local palette index in storage order so an archive-level caller can validate
// the common-header palette separately; these are not global definition IDs.
std::vector<uint32_t> validateLocalDetail(const std::vector<uint8_t>& body);
// Validate a separate sea-entry payload (counts, range-coded XYZ floats and
// uint16 indices). Its BBB extra metadata requires separate archive review.
void validateSea(const std::vector<uint8_t>& body);

struct MapRecord {
    stbinfo::StaticMapInfoBlock info;
    uint32_t foregroundDirectory=0,backgroundRoot=0,backgroundSpan=0;
    uint32_t detailRootInRecord=0,detailRootSpan=0,detailPaletteCount=0;
};
// Supported retail common-record subset: version 1, contiguous descriptors,
// global texture IDs and numeric mesh palettes. logicalOffset is this record's
// offset within the common entry. Returns chunk pointers for separate ownership
// checks. Legacy generation edge/shore/checksum words are preserved, not used
// as spans into retail payloads.
MapRecord validateMapRecord(const std::vector<uint8_t>& record,uint32_t logicalOffset);
struct CommonMap {
    std::string name;
    uint32_t logicalOffset=0;
    MapRecord record;
};
// Validate the entire retail common entry: count, unique nonempty names,
// unique offsets and chunk IDs, followed by contiguous supported map records.
// Returns manifest order, which need not match physical record order. Chunk
// pointers and bank IDs still require archive-level ownership checks.
std::vector<CommonMap> validateCommon(const std::vector<uint8_t>& body);
struct BankEntry {
    stb::Entry entry; // crc is the native state-block CRC; devName is filename 0.
    uint32_t checksum=0;
    bool sea=false;
    uint32_t seaCutoutCRC=0;
};
struct BankMetadata {
    uint32_t alignment=0,maximumId=0,tableOffset=0;
    std::vector<BankEntry> entries;
};
// Review the exact 32-byte BBB header and entire trailing TOC, independently of
// payload decoding. Supported subset: format 115 / STB 174, finalized bank,
// one data type (0), one filename per entry, empty map/common info or sea v22
// info at stream position 0. Checks unique identities and disjoint aligned
// payload spans before the TOC; does not inspect gap bytes or payload contents.
BankMetadata validateBankMetadata(std::span<const uint8_t> header,
                                  std::span<const uint8_t> table,uint64_t fileSize);
struct ChunkStorage {
    size_t foregroundFrames=0,backgroundFrames=0,detailFrames=0;
    uint64_t decodedBytes=0,zeroPaddingBytes=0;
    uint64_t opaqueUnownedBytes=0;
};
// Follow native runtime pointers from a validated common record. Every live
// structure/frame must be bounded and disjoint; shared frames may have multiple
// owners only within the same domain. Decode and validate every owned frame,
// check detail palette indices and require zero bytes in all remaining gaps.
// Generation-only edge/shore/checksum pointers are intentionally not followed.
// This read-only supported-subset check does not validate external asset IDs.
ChunkStorage validateMapChunk(const std::vector<uint8_t>& chunk,
                              const std::vector<uint8_t>& record,uint32_t logicalOffset,
                              bool preserveUnownedStorage=false);
// preserveUnownedStorage retains preexisting bytes outside runtime-owned spans
// rather than requiring compact zero-filled gaps. Ownership/bounds checks stay
// strict; callers must preserve those opaque spans, not interpret or clear them.
struct BackgroundTextureEdit {
    size_t frameOffset=0;
    uint16_t width=0,height=0;
    std::vector<uint8_t> dxt1;
    bool allowExtent256=false;
};
// Transactional texture-only chunk rewrite: append changed frames on page
// boundaries, update all owning LOD references and zero their old framed spans.
// All other bytes and the supplied common record stay untouched. Validates
// complete chunk ownership before/after; unreferenced edits are refused.
std::vector<uint8_t> replaceBackgroundTextures(const std::vector<uint8_t>& chunk,
    const std::vector<uint8_t>& record,uint32_t logicalOffset,
    const std::vector<BackgroundTextureEdit>& edits);
struct BankStorage {
    size_t entries=0,maps=0,seas=0,paletteRows=0;
    uint64_t zeroBankPaddingBytes=0;
    ChunkStorage chunks;
};
// Complete read-only storage review for compact retail banks in the supported
// metadata/common/chunk subset. Every entry must belong to the common manifest
// or be a validated sea entry. Map names and IDs must agree; all bytes outside
// live bank payloads must be zero. Append-only banks retaining old tables in
// unreachable space are outside this subset. Does not certify asset resolution
// or gameplay and never rewrites the bank.
BankStorage validateBankStorage(const std::filesystem::path& path);
}
