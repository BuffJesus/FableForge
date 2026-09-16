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
// PositionX/Y/Z (map-local), RHSetForward*, RHSetUp*. The mesh composes as
//   world = pos + (-lx)*forward + ly*right + lz*up,  right = forward x up,
// with lx/ly/lz the mesh vertex scaled by 0.01 * ObjectScale (meshes are in cm).
// That composition is the one FableForge verified against ChocolateBox.
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
};

// Produces a foliageexport::Scene (same instance/mesh model, rootName "Things")
// so every writer and the preview renderer work unchanged.
foliageexport::Scene load(const std::string& mapName, const Options& options,
                          const terrainexport::Context& context, Stats* stats = nullptr);

} // namespace albion::thingsexport
