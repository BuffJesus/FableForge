#pragma once
// The retail CNavQuadTree as stored in a LEV's navigation sections, parsed
// node-for-node (all layers, switchable leaves, blocked root markers) so an
// edit can touch only the cells it changed and keep everything else -- the
// half-cell leaves carved around placed objects, stacked layers, door
// (switchable) nodes and region ids -- exactly as the retail build wrote it.
//
// Byte layout (lib_nav_quad_tree.cpp, FableWin CNavQuadTree::SaveToFile /
// LoadFromFile, CANavQuadTreeNode::SaveToFile and the leaf overrides):
//   block: u32 blockEnd, u32 version(8), f32 width, f32 height, u32 regionCount,
//          u32 positionCount, positionCount x {f32 x, f32 y, i32 data},
//          u32 layerCount, i32 total, then exactly `total` records
//   record: u8 switchable, u8 root, u8 blocked; a blocked root marker stops
//           here (3 bytes, no index). Otherwise u8 leaf, u8 level, u8 layer,
//           f32 cx, f32 cy, i32 index, then
//           internal: i32 child[4]   ((-x,-y) (+x,-y) (-x,+y) (+x,+y); 0 = none)
//           leaf:     i32 region, u8 preference, i32 n, n x i32 neighbour index,
//                     switchable: u32 m, m x u64 thing UID
//   u32 0xACBDEF12
// The loader reads `total` records in a flat loop, appends root records to the
// current layer's root array (rootsPerLayer = (w/32)*(h/32) slots per layer),
// and resolves child/neighbour indices through a map -- so indices only need to
// be unique, and `total` must equal the record count.
// Region ids are the connected components of the leaf-neighbour graph with the
// switchable (door) leaves removed; switchable leaves carry the outer region
// and a closed one carries region 0 with no neighbours.

#include "forge/lev.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace forge::navmesh {

struct RetailNode {
    bool marker = false;       // blocked root: the 3-byte record
    bool switchable = false;   // CNavSwitchableLeafNode; `uids` are the switching things
    bool root = false;
    bool blocked = false;      // switchable leaf currently closed
    bool leaf = false;
    uint8_t level = 0;         // 0 = 32x32 root ... 5 = one cell, 6 = half a cell
    uint8_t layer = 0;
    float cx = 0, cy = 0;      // map-local centre
    int32_t index = 0;
    int32_t children[4] = {0, 0, 0, 0};
    int32_t region = 0;
    uint8_t preference = 0x80; // 0x00 preferred path, 0x80 normal, 0x40 on raised layers
    std::vector<int32_t> neighbours;
    std::vector<uint64_t> uids;
};

struct RetailSection {
    std::string name;
    uint32_t version = 8;
    float width = 0, height = 0;
    uint32_t regionCount = 0;
    std::vector<uint8_t> positions;   // raw 12-byte CNavigationPosition records
    uint32_t layerCount = 1;
    std::vector<RetailNode> nodes;    // file order
};

struct RetailNav {
    std::vector<RetailSection> sections;
};

// Parse every navigation section of the LEV. Throws on a malformed block.
RetailNav parseNavigation(const lev::File& file);

// Rebuild the LEV bytes: everything before navigationOffset() is the file's
// original bytes; the directory and every section are re-emitted (offsets and
// the blockEnd chain recomputed). With an unmodified RetailNav the output is
// byte-identical to the input for every retail LEV.
std::vector<uint8_t> emitNavigation(const lev::File& file, const RetailNav& nav);

struct WalkabilityPatchResult {
    size_t cellsBlocked = 0;    // cells whose leaves were removed
    size_t cellsOpened = 0;     // cells that received a new leaf
    size_t cellsSkipped = 0;    // opened cells that already had coverage (an object carve)
    size_t leavesRemoved = 0;
    size_t leavesAdded = 0;
    size_t nodesSplit = 0;      // larger leaves split so a single cell could be removed
    size_t regionsAdded = 0;
    size_t regionsMerged = 0;
};

// Apply a walkability change to layer 0 of every section: for each (x, y) in
// `cells`, file.walkableAt(x, y) is the new state. Blocked cells lose their
// leaves (bigger leaves are split first), opened cells get one full-cell leaf
// (preferred-path aware) hooked into the tree, neighbour lists of the touched
// leaves are recomputed geometrically, and region ids are re-derived so a
// split region gets a new id and a bridged pair keeps the larger's id.
// Everything else (other layers, door nodes, untouched neighbour order, node
// indices) is preserved.
WalkabilityPatchResult patchWalkability(RetailNav& nav, const lev::File& file,
                                        const std::vector<std::pair<int, int>>& cells);

} // namespace forge::navmesh
