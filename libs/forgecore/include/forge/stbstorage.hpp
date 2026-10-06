#pragma once
#include <cstdint>
#include <vector>
#include "forge/stbinfo.hpp"

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
// checks. Legacy development checksum words are preserved, not used as spans.
MapRecord validateMapRecord(const std::vector<uint8_t>& record,uint32_t logicalOffset);
}
