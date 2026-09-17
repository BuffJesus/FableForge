#pragma once
// Relocating a baked terrain chunk: every world-space coordinate the static-map
// bank stores for a map is absolute (the debug editor bakes with the map's
// WLD origin), so moving a map means translating all of them by (dx, dy):
//   - the foreground patch directory AABBs (quad dir) and the foreground layer
//     vertices + their water mesh (CWaterPatchMesh, u16 grid);
//   - every background patch: vertex grid (u16), the four tessellation edge
//     strips (start coordinate + CVertex arrays), the water sub-patch vertices;
//   - the background-LOD tree node AABBs (CLandscapeBackgroundTreeNode);
//   - the local-detail (foliage) quadtree: node/group bounding spheres, and
//     every cache-group frame's primitive boxes/spheres/matrices/instances;
//   - the common record's local-detail root sphere.
// Grammars: FableWin Save functions (CLandscapeBackgroundPatch::Save 0x2ce3220,
// CPatchTesselationEdgeStrip::Save 0x2e03a80, CEngineWaterBackgroundSubPatch::
// Save 0x2e055e0, CWaterPatchMesh::Save 0x2e68bf0, CLandscapeBackgroundTreeNode
// ::SaveHeader 0x2deabe0, CLocalDetailCacheMap::CQuadTreeElement::SaveFileBlock
// 0x2e3eb20 + SaveHeader 0x2e3e680, CObjectCacheGroupCollection::SaveHeader
// 0x2e3d5b0, the three CLocalDetailPrimitive*::Save) and forgecore's writers.
// Range-coded vertex blocks are decoded, shifted and re-encoded with the
// editor's own compressor (rangecodec::encodeNative); LZO frames are
// re-compressed into their original slots.
//
// audit() walks the same structures read-only and reports every coordinate
// outside the map's box: the grammar check that runs over all retail maps.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace albion::editor {

struct RelocateReport {
    int foregroundFrames = 0, patchFrames = 0, groupFrames = 0, treeNodes = 0, detailNodes = 0, detailGroups = 0;
    int rangeBlocks = 0, rangeBlocksResized = 0;
    int unclassifiedFrames = 0;
    uint64_t digest = 0;                 // FNV-1a of every decoded record/body in walk order (see auditChunk)
    std::vector<float> sites;            // every coordinate visited, walk order
    std::vector<uint8_t> siteIsX;        // 1 = an X coordinate, 0 = Y
    std::vector<std::string> notes;
    std::vector<std::string> issues;     // audit: coordinates outside the box / grammar breaks
};

// Translate chunk + common record in place. Returns false with `error` on a
// grammar break or when a re-encoded frame no longer fits its slot.
bool relocateChunk(std::vector<uint8_t>& chunk, std::vector<uint8_t>& record, int dx, int dy,
                   RelocateReport& report, std::string& error);

// Ride the ground: every local-detail (foliage) point gets dz(worldX, worldY)
// added to its Z (mesh matrices, repeated-mesh instances, z-sprites); node,
// group and primitive bounds follow their centre and grow by `zSlack` (pass the
// largest |dz| over the map). Ground/patch data is re-encoded unchanged. The
// chunk may grow (LZO frames re-laid), so write it back with the size-aware
// replace.
bool reseatFoliageZ(std::vector<uint8_t>& chunk, std::vector<uint8_t>& record, std::function<float(float, float)> dz,
                    float zSlack, RelocateReport& report, std::string& error);

// Read-only walk: every coordinate must lie within [x0-slack, x0+w+slack] etc.
bool auditChunk(const std::vector<uint8_t>& chunk, const std::vector<uint8_t>& record,
                int x0, int y0, int w, int h, RelocateReport& report, std::string& error);

} // namespace albion::editor
