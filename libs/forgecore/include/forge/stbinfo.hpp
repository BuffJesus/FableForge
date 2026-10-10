#pragma once
// forge::stbinfo — CStaticMapInfoBlock (the 0x5C-byte per-static-map header the STB
// COMMON_HEADER indexes). On-disk field order = the engine's ReadMapInfoBlock
// @0x02d5c230 call sequence / WriteMapInfoBlock @0x02d67ea0 permutation, ported
// from the proven stb_infoblock_baker.py (round-trips 398/398 donor records byte-exact).
//
// Landscape/local-detail/header-end pointers are common-entry offsets in the
// retail layout. Edge-height, shoreline and dependency-checksum spans describe a
// separate generation stream (UpdateStaticMapPass1 02D66C80); OpenRetailStaticMap
// 02D5BC40 does not copy/load them. Preserve these legacy words when relocating
// retail chunks; they are not bounds into the retail payload.

#include <cstdint>
#include <vector>

namespace forge::stbinfo {

constexpr std::size_t kInfoBlockSize = 0x5C; // 92 bytes: 17 scalars + 6-float box

struct StaticMapInfoBlock {
    std::int32_t  versionID = 0;
    std::int32_t  bankFileIndex = 0;
    std::int32_t  edgeHeightFileSize = 0; // generation stream
    std::int32_t  mapWidth = 0;
    std::int32_t  mapHeight = 0;
    std::int32_t  worldX = 0;
    std::int32_t  worldY = 0;
    std::int32_t  edgeHeightFilePtr = 0;   // generation stream
    std::int32_t  landscapeMapPtr = 0;     // retail common-entry offset
    std::int32_t  localDetailMapPtr = 0;   // retail common-entry offset
    std::int32_t  quality = 0;
    std::int32_t  shorePointArraySize = 0; // generation stream
    std::int32_t  shorePointArrayStart = 0; // generation stream
    std::uint32_t levelChecksum = 0;
    std::int32_t  checksumBlockFilePtr = 0; // generation stream
    std::int32_t  checksumBlockFileSize = 0; // generation stream
    float         cameraMapBounds[6] = {0,0,0,0,0,0}; // minX,minY,minZ,maxX,maxY,maxZ
    std::int32_t  headerEndPtr = 0;         // retail common-entry offset
};

// Read 0x5C bytes at `p` (caller ensures ≥0x5C readable) into the struct, in on-disk order.
StaticMapInfoBlock readInfoBlock(const std::uint8_t* p);

// Emit the struct as exactly 0x5C little-endian bytes in on-disk write order.
std::vector<std::uint8_t> writeInfoBlock(const StaticMapInfoBlock& b);

} // namespace forge::stbinfo
