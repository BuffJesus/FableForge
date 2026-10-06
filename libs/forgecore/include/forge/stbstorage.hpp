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
}
