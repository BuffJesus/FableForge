#pragma once
// albion::stbwater -- the baked water surface writer: CWaterPatchMesh::Save per 16x16 patch
// from a map's water level grid (FableTLC docs/engine/WATER_RE.md; every constant below was
// audited against the 3,374 retail water patches with `forge water-audit --all`).
//
// A patch gets a block when any vertex of its 17x17 grid has an interpolated water level
// (the level grid already reaches 2 cells onto the bank, like PeekInterpolatedWaterHeight).
// Records: world x/y; z = floor((h - 0.1) * 256) / 256 where h is the vertex level, else
// the mean of the levels within +-2 cells INSIDE the patch, else ground - 1 (sunk); the two
// wave columns from world x/y in radians through the float 1/2pi and 2pi constants; depth
// from the quantised z against the ground the bake sees (pass the STB foreground vertex
// heights when re-baking a retail map, the LEV's otherwise); distToShore 0 and the twelve
// shore look-ups 0 (no foam: GetZeroedShoreLookUpArray).

#include <array>
#include <cstdint>
#include <vector>

#include "forge/stbbake.hpp"
#include "terrainexport.hpp"

namespace albion::stbwater {

struct MapInput {
    const terrainexport::WaterLevels* levels = nullptr;   // the map's level grid, levels->width x levels->height vertices
    const std::vector<float>* ground = nullptr;           // the same size, row-major vertex heights
    int worldX = 0, worldY = 0;                            // the map's WLD placement
    float bodySpan = 0;                                    // WaterBodySpan written into every patch header
};

// The bytes after the foreground frame's hasWater flag for the patch at map-local
// (patchX, patchY), or empty when the patch has no water.
std::vector<uint8_t> buildPatchPayload(const MapInput& in, int patchX, int patchY);

// The distant water of one background patch (CWaterGenerator::BuildStaticMapBackgroundBuffers,
// FableWin 0x02e067c0): every triangle of the patch's landscape mesh with a vertex within one
// cell of painted water contributes its vertices in first-touch order; a vertex is
// {u16 x, u16 y, f32 z = the interpolated level (a dry vertex takes a triangle mate's),
// f32 shore[12] (zero here)} for lakes / rivers / ice, {f32 x, y, z} for the sea types;
// WaterType = the most common type over the touched vertices. Returns the bytes from the
// trailer's water flag on (u8 1, u16 vertexCount, u16 polyCount, i32 type, i32 stride,
// i32 len + VB block, [i32 len + IB block]), or empty when no triangle touches water.
std::vector<uint8_t> buildBackgroundSubPatch(const MapInput& in, const forge::stbbake::PatchHeader& header,
                                             const std::vector<forge::stbbake::PatchVertex>& verts,
                                             const std::vector<std::array<uint16_t, 3>>& triangles);

// The wet region's bounding-box diagonal (retail's WaterBodySpan is constant per water body:
// 181.8 on the Guild lake, 1528 on Barrow Fields' river, 465 on the Hook Coast sea).
float bodySpan(const terrainexport::WaterLevels& levels);

// The record formulas alone (unit-tested): world x/y, the vertex level h (already resolved,
// > 0), the ground at the vertex, ice.
struct Record {
    uint16_t x = 0, y = 0;
    float z = 0;
    int16_t waveS = 0, waveC = 0, depth = 0;
};
Record makeRecord(int worldX, int worldY, float h, float ground, bool ice);

} // namespace albion::stbwater
