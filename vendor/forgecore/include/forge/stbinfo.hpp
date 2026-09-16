#pragma once
// forge::stbinfo — CStaticMapInfoBlock (the 0x5C-byte per-static-map header the STB
// COMMON_HEADER indexes). On-disk field order = the engine's ReadMapInfoBlock
// @0x02d5c230 call sequence / WriteMapInfoBlock @0x02d67ea0 permutation, ported
// from the proven stb_infoblock_baker.py (round-trips 398/398 donor records byte-exact).
//
// This is STEP 1 of the native terrain bake (docs/TERRAIN_NATIVE_BAKE.md): the header
// forge::stb::save() rewrites when a re-laid chunk's landscape/local-detail body
// pointers move. Pointer/size fields index into the compressed bodies (GATED) — only
// rewrite them in lock-step with an emitChunk rebase; the placement/quality scalars
// and CameraMapBounds are freely editable.

#include <cstdint>
#include <vector>

namespace forge::stbinfo {

constexpr std::size_t kInfoBlockSize = 0x5C; // 92 bytes: 17 scalars + 6-float box

struct StaticMapInfoBlock {
    std::int32_t  versionID = 0;
    std::int32_t  bankFileIndex = 0;
    std::int32_t  edgeHeightFileSize = 0;
    std::int32_t  mapWidth = 0;
    std::int32_t  mapHeight = 0;
    std::int32_t  worldX = 0;
    std::int32_t  worldY = 0;
    std::int32_t  edgeHeightFilePtr = 0;   // GATED (body offset)
    std::int32_t  landscapeMapPtr = 0;     // GATED (body offset) — moves on re-lay
    std::int32_t  localDetailMapPtr = 0;   // GATED (body offset) — moves on re-lay
    std::int32_t  quality = 0;
    std::int32_t  shorePointArraySize = 0;
    std::int32_t  shorePointArrayStart = 0;
    std::uint32_t levelChecksum = 0;
    std::int32_t  checksumBlockFilePtr = 0; // GATED
    std::int32_t  checksumBlockFileSize = 0; // GATED
    float         cameraMapBounds[6] = {0,0,0,0,0,0}; // minX,minY,minZ,maxX,maxY,maxZ
    std::int32_t  headerEndPtr = 0;         // GATED
};

// Read 0x5C bytes at `p` (caller ensures ≥0x5C readable) into the struct, in on-disk order.
StaticMapInfoBlock readInfoBlock(const std::uint8_t* p);

// Emit the struct as exactly 0x5C little-endian bytes in on-disk write order.
std::vector<std::uint8_t> writeInfoBlock(const StaticMapInfoBlock& b);

} // namespace forge::stbinfo
