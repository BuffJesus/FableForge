#pragma once
// Where a region's maps sit on its minimap texture: the engine's own transform,
// CTCInventoryMap::GetRelativePosOnMiniMap (FableWin 0x020931b0), which places
// the hero marker. A baked minimap must use the same transform or the marker
// drifts off the art.
//
//  * the box is the union of the region's OWNED maps (GetMaps), in world cells;
//  * a position becomes (u, v) in [0, 1] per axis, then the shorter axis is
//    scaled by short / long, so the map keeps its aspect ratio and sits
//    against the left and bottom edges;
//  * pixel (from the texture's top-left) = s * size * u + s * offX,
//                                          s * size * (1 - v) + s * offY,
//    s = MiniMapScale, (offX, offY) = MiniMapOffsetX/Y, both per region in the
//    WLD (the BWD stores the offsets as int32).
// Checked against the retail art: with each region's own scale and offsets the
// painted paths line up with the LEV's path cells (BanditCampPath1 IoU 0.43
// with its offset, 0.00 without; docs/VANILLA_EDITOR_INVENTORY.md section 10, "Minimap framing").
namespace forge::minimapframe {

struct Framing {
    float scale = 1.0f;
    float offsetX = 0.0f, offsetY = 0.0f;   // texture pixels (whole numbers: the BWD keeps int32)
};

// map-local (x, y) in a width x height box (world cells, y north) -> texture pixel
void toPixel(const Framing& f, float width, float height, float textureSize, float x, float y, float& px, float& py);
// the inverse: a texture pixel -> map-local (x, y); false when it falls outside the box
bool fromPixel(const Framing& f, float width, float height, float textureSize, float px, float py, float& x, float& y);
// Scale 1 with offsets that centre the map in the texture (retail leaves most regions at
// 0, 0, which pins a non-square map to the bottom-left, partly under the round vignette).
Framing centred(float width, float height, float textureSize = 256.0f);

} // namespace forge::minimapframe
