#pragma once
// Authoring-side placement of a new Thing into a level .tng.
//
// Everything here is derived from the shipping FinalAlbion TNG/LEV/WLD set, not
// assumed:
//
//  * TNGs carry NO header counters. `Version 2;` is followed directly by
//    `XXXSectionStart <name>;` blocks; adding a thing means adding its
//    NewThing..EndThing lines and nothing else. (Verified on
//    FinalAlbion/Greatwood_1.tng: 313 things, six sections, no count field.)
//  * Thing UIDs are 64-bit with a FIXED high dword 0xFFFFFE00 -- true for all
//    21,764 UIDs across all 401 retail FinalAlbion TNGs. The low dword is a
//    per-file counter and uniqueness is per FILE, not global (Greatwood_1 and
//    Greatwood_2 legitimately share 71 UIDs on unrelated things).
//  * Positions inside CTCPhysicsStandard are MAP-LOCAL: world = map origin
//    (MapX/MapY from the .wld) + local. One position unit is one .lev cell, so
//    local X spans [0, lev.width()] and local Y spans [0, lev.height()].
//  * PositionZ for a ground-resting thing is the bilinear terrain height of the
//    .lev cell grid at that local XY. Checked against Greatwood_1's own things:
//    the retail OBJECT_BW_SIGNPOST_PLAQUE_01 at (34.635742, 41.165283) stores
//    Z 39.847469 and the bilinear sample is 39.847826 (delta 0.00036).
//  * Orientation is a right-handed basis (RHSetForward*/RHSetUp*), never an
//    angle. Retail's up vector for a ground-planted prop is ~(0, 0, 0.999994).
//  * Floats are printed with six decimals and trailing zeros trimmed, keeping
//    at least one decimal ("0.0", "6000.0", "-0.000102", "96.063965").
//
// The emitted block reproduces retail's field order exactly, including the
// `Health` line that sits AFTER the CTC blocks -- which is why placement writes
// verbatim text through tng::File::insertThingBlock instead of round-tripping
// through the document model.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "forge/lev.hpp"
#include "forge/tng.hpp"

namespace forge::thingplacer {

// High dword shared by every retail thing UID.
inline constexpr uint64_t kUidHighDword = 0xFFFFFE00ull;
// Retail's up vector for a ground-planted prop.
inline constexpr float kGroundUpZ = 0.999994f;

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Placement {
    std::string thingType = "Object"; // NewThing <type>;
    std::string definitionType;       // required, unquoted
    std::string scriptName;           // empty -> `ScriptName NULL;`
    std::string scriptData;           // empty -> `ScriptData "NULL";`
    int player = 4;
    bool thingGamePersistent = true;
    bool thingLevelPersistent = true;

    Vec3 position;                       // MAP-LOCAL
    Vec3 forward{1.0f, 0.0f, 0.0f};      // right-handed basis
    Vec3 up{0.0f, 0.0f, kGroundUpZ};

    bool targetable = true;              // emits CTCTargeted
    std::string gameTextDefName;         // non-empty -> CTCActionUseReadable
    bool scriptedHook = false;           // CreateTC + CTCActionUseScriptedHook
    std::string hookSoundName;
    std::string hookAnimationName;

    std::optional<float> health = 6000.0f; // retail readable-sign value
    std::optional<uint64_t> uid;           // unset -> nextUid(file)
    std::string section = "NULL";          // XXXSectionStart <section>;
};

// Retail float spelling. Six decimals, trailing zeros trimmed, >= one decimal.
std::string formatFloat(float value);

// Lowest unused low-dword in the retail UID namespace for THIS file.
uint64_t nextUid(const tng::File& file);
// True when no thing in `file` already uses `uid`.
bool uidIsFree(const tng::File& file, uint64_t uid);
// True when no thing in `file` already uses this ScriptName (NULL is exempt --
// retail reuses NULL freely and a placement with a real name needs uniqueness
// only so GetThingWithScriptName resolves deterministically).
bool scriptNameIsFree(const tng::File& file, const std::string& scriptName);

// Right-handed forward vector for a yaw in degrees (0 = +X, CCW toward +Y).
Vec3 forwardFromYawDegrees(float degrees);

// Bilinear terrain height at a MAP-LOCAL XY, i.e. the value a ground-resting
// thing stores in PositionZ. Throws when the XY is outside the cell grid.
float terrainHeightAt(const lev::File& level, float localX, float localY);

// Map-local coordinates for a world position given the .wld map origin.
Vec3 worldToLocal(const Vec3& world, int mapX, int mapY);

struct MapTile {
    std::string levelName;
    int mapX = 0, mapY = 0;
    int width = 0, height = 0;
};

struct RoutedPosition {
    size_t tileIndex = 0;
    Vec3 local;
};

// Route an instance pivot to exactly one map using half-open tile bounds. This
// makes a pivot on a shared edge belong to the tile beginning at that edge and
// rejects gaps or overlapping map rectangles instead of guessing.
RoutedPosition routeWorldPosition(const Vec3& world,
                                  const std::vector<MapTile>& tiles);

// The exact NewThing..EndThing text (no surrounding blank lines).
std::string serialize(const Placement& placement,
                      const std::string& lineTerminator = "\r\n");

struct PlaceResult {
    size_t thingIndex = 0;
    uint64_t uid = 0;
    std::string block;
};

// Validates, assigns a UID when unset, and inserts the block into `file`.
PlaceResult place(tng::File& file, Placement placement);

// Convenience: a readable direction sign resting on the terrain.
//   `level` supplies PositionZ; pass nullptr to keep placement.position.z.
Placement readableSign(const std::string& definitionType,
                       const std::string& gameTextDefName,
                       float localX, float localY,
                       float yawDegrees,
                       const lev::File* level);

} // namespace forge::thingplacer
