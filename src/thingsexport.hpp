#pragma once
// albion::thingsexport -- the hand-placed objects of one map (fences, walls,
// rocks, lamps, crates, buildings, chests...) as mesh instances.
//
// Source: data/Levels/FinalAlbion/<Map>.tng (loose; the WAD copy otherwise), a
// text file of NewThing blocks. Each thing names a DEFINITION (e.g.
// OBJECT_OAKVALE_FENCE_01); the compiled def in game.bin carries a Graphic
// field whose second dword is the MBANK_ALLMESHES model id. A thing may also
// override the mesh by name (GraphicOverride) and scale it (ObjectScale).
// Position and orientation come from the CTCPhysicsStandard / Navigator block:
// PositionX/Y/Z (map-local), RHSetForward*, RHSetUp*. The engine's own
// CEngineInternalPrimitiveMeshBase::CalcObjectMatrix (retail 0x00bebaa0, debug
// build 0x02ee3a00) composes a mesh vertex as
//   world = pos + lx*(-right) + ly*(-forward) + lz*up,  right = forward x up,
// with lx/ly/lz the mesh vertex scaled by 0.01 * ObjectScale (meshes are in cm).
// Cross-checked 2026-09-16 on the Arena (oval pit axis, N/S corridors, gates,
// audience ring and billboard facing all agree, and match MINIMAP_ARENA) and on
// Oakvale (fence segments join end-to-end). The old ChocolateBox-derived guess
// (-lx*forward + ly*right) was a 90-degree yaw off.
//
// Composite objects: a mesh's 3ds-Max dummies named "CREATEOBJECT <def>" /
// "CREATEBUILDING <def>" spawn child things at the dummy transform (the engine's
// CTCMeshAutomaticEntityCreator). Doors, windows, weathervanes, the Arena's
// entrances and stand sections, chained cave/hall interiors all come from these,
// not from the .tng; they are followed recursively (depth 4).
//
// Honest boundaries:
//   * Creatures (AICreature) are skinned meshes; LOD0 is exported in bind pose
//     only when `Options::creatures` is set.
//   * Markers, camera points, region exits, particle emitters have no Graphic
//     and are skipped (counted).
//   * Only LOD0; no animation, no lights, no scripts.

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "foliageexport.hpp"
#include "terrainexport.hpp"

namespace albion::thingsexport {

struct Options {
    std::filesystem::path gameRoot;
    bool textures = true;
    bool creatures = false;
    terrainexport::UpAxis up = terrainexport::UpAxis::Y;
    // Added to every position (TNG positions are map-local). Use the map's
    // WLD MapX/MapY to place several maps in one world.
    float originX = 0.0f, originY = 0.0f;
    std::function<void(const std::string&)> log;
};

struct Stats {
    int things = 0;          // NewThing blocks in the file
    int placed = 0;          // instances exported
    int noGraphic = 0;       // def has no model (markers, cameras, emitters...)
    int noDef = 0;           // DefinitionType not in game.bin
    int noMesh = 0;          // model id not in graphics.big / undecodable
    int noPosition = 0;      // no physics block
    int skippedCreatures = 0;
    int childThings = 0;     // CREATEOBJECT / CREATEBUILDING dummies seen in placed meshes
    int childPlaced = 0;     // ...of which produced an instance
    int childParticles = 0;  // CREATEPARTICLE dummies (effects, not exported)
};

// Rows of `m` are the world images of the mesh-local x/y/z axes (times `scale`):
// world = pos + lx*m[0..2] + ly*m[3..5] + lz*m[6..8], i.e. CalcObjectMatrix's rows
// {-right, -forward, up}. `forward`/`up` need not be normalised; a degenerate frame
// falls back to +X forward / +Z up.
void thingBasis(const float forward[3], const float up[3], float scale, float m[9]);

// Produces a foliageexport::Scene (same instance/mesh model, rootName "Things")
// so every writer and the preview renderer work unchanged.
foliageexport::Scene load(const std::string& mapName, const Options& options,
                          const terrainexport::Context& context, Stats* stats = nullptr);

} // namespace albion::thingsexport
