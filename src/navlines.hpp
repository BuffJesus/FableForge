#pragma once
// albion::navlines -- the blocking lines a placed object cuts into the
// navigation mesh, computed the way the engine does.
//
// Recovered from the debug build (FableWin, PDB-named; source file
// lib_physics_mesh_2_navigation.cpp):
//   CThingPhysical::GetNavCollisionLineList (0x01f21d20) asks its physics mesh
//   object for level N's lines; CPhysicsMeshObject::GetNavigationLineList
//   (0x017cbe97) takes each mesh-space line (x0,y0)-(x1,y1), makes the points
//   (x,y,0), transforms them by the thing's matrix and keeps world x/y.
//   CPhysicsMesh::CalculateNavigationLineInfo (0x033c79e0) /
//   CalculateLayeredNavigationLineInfo (0x033c7ab0): for level i = 0..8 the
//   hull's helper point named "NAV_LAYER_0" + (i+1) -- NAV_LAYER_01.. -- gives a
//   baseline height, and GenerateLineListFromBaseline (0x033c8240) slices the
//   hull there: the triangle whose centroid z is closest to the helper's z sets
//   the plane, and every triangle with vertices strictly on both sides of it
//   contributes its plane intersection as one line. (The engine then deletes the
//   sliced triangles from the collision mesh.)
// Retail: 1,204 of the 1,386 [PHYSICS] hulls in graphics.big carry NAV_LAYER
// helpers (`forge-tools mesh physics-scan`).

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "foliageexport.hpp"
#include "forge/big.hpp"

namespace albion::navlines {

struct Line { float x0 = 0, y0 = 0, x1 = 0, y1 = 0; };

struct HullMesh {
    std::vector<std::array<float, 3>> verts;
    std::vector<std::array<uint32_t, 3>> tris;
    std::vector<std::pair<std::string, std::array<float, 3>>> helpers;   // HLPR/HPNT name + position
};

struct HullOptions {
    bool applySubMeshTransform = true;   // TRFM of each SUBM applied to its vertices
};

// A decompressed 3DMF physics hull (>>>>3DMF ...), every SUBM/PRIM merged.
HullMesh decodeHull(const std::vector<uint8_t>& plain, const HullOptions& options);

// GenerateLineListFromBaseline: mesh-space lines at the baseline nearest `z`.
std::vector<Line> lineListFromBaseline(const HullMesh& mesh, float z);

// Lines per navigation level (index 0 = NAV_LAYER_01); empty when the hull has no helpers.
std::vector<std::vector<Line>> navigationLines(const HullMesh& mesh);

struct Hull {
    std::vector<std::vector<Line>> levels;
    std::array<float, 3> centre{0, 0, 0};   // bounding sphere of the hull's vertices (mesh space)
    float radius = 0;
};

// CalculateBoundingInfo (FableWin 0x030f8770): AABB centre and half-diagonal
// radius, not the distance to the farthest actual vertex.
Hull buildHull(const HullMesh& mesh);

// The detailed-area box a thing contributes (GetMapNavigationAreaInit: the physics
// bounding sphere's centre +- radius, clamped to the map) in map-local units.
struct Box { float x0 = 0, y0 = 0, x1 = 0, y1 = 0; };
Box detailBox(const Hull& hull, const foliageexport::Instance& instance, int mapWidth, int mapHeight);

// GoToHigherDetail (0x03286830): a NODE corner must be inside a detailed area.
// Called on the parent before subdivision, not on the resulting half-unit leaf.
bool requestsHigherDetail(const Box& node, const std::vector<Box>& detailedAreas);

// Render mesh id -> its [PHYSICS] hull (the Info blob's PhysicsIndex) -> lines, cached.
class HullCache {
public:
    HullCache(const forge::big::File& graphics, HullOptions options);
    const Hull* forRenderMesh(uint32_t meshId);   // nullptr: no physics hull
    const Hull* forPhysicsMesh(uint32_t physicsId); // explicit CDoorDef collision override
private:
    const forge::big::File& big_;
    HullOptions options_;
    std::map<uint32_t, const forge::big::Entry*> byId_;
    std::map<uint32_t, std::unique_ptr<Hull>> cache_;
};

// Mesh-space line -> map-local world line through the instance's frame (z = 0 in mesh space).
Line toWorld(const Line& line, const foliageexport::Instance& instance);

// 1 per unit cell of a W x H map whose 1x1 box a line touches.
std::vector<uint8_t> rasterise(const std::vector<Line>& lines, int width, int height);

} // namespace albion::navlines
