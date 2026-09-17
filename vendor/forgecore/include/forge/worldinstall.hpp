#pragma once
// Install a new level into a Fable install IN PLACE, from a donor: the library
// form of `forge world install-level`. All four world containers are wired in
// one staged, atomic step -- BWD + WLD (map slot, and either a dedicated new
// region or membership in an existing host region), WAD (the donor's .lev/.tng
// entries cloned under the new name, optionally with custom bytes) and STB (the
// terrain chunk appended with an origin-patched common record).
//
// Region ownership: the engine's region vector is capped at the vanilla 141
// entries (live probe, 2026-08), so a DEDICATED region past that is never
// reachable -- a new level should normally be attached to an existing host
// region (`hostRegion`), which is what the editor defaults to.
//
// Geometry: the STB chunk is what the game draws. Reusing the donor's chunk at
// the new origin renders mismatched/white unless it was re-baked for that
// origin first (`stbbake::bakeHeightfield` with worldX/worldY = the new origin);
// pass the result as `chunkBytes`.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace forge::worldinstall {

struct Request {
    std::filesystem::path gameRoot;     // the install (data/Levels/... underneath)
    std::string donorLevelName;         // e.g. "StartOakValeWest"
    std::string newLevelName;           // bare stem; must be unused everywhere
    int worldX = 0, worldY = 0;         // origin, 32-aligned
    std::string hostRegion;             // existing region that owns the map; "" = dedicated new region
    std::string regionName;             // dedicated region only (default: newLevelName)
    std::string regionDisplayName;      // dedicated region only
    std::string regionDef;              // dedicated region only
    bool loadedOnProximity = false;
    bool isSea = false;
    bool allowOverlap = false;          // let the new box overlap existing map boxes
    std::vector<uint8_t> levBytes;      // empty = the donor's WAD entry
    std::vector<uint8_t> tngBytes;      // empty = the donor's WAD entry
    std::vector<uint8_t> chunkBytes;    // empty = the donor's chunk (see the geometry note)
    std::string backupSuffix = ".bak";  // one-time copies of the four containers; "" = none
};

struct Result {
    int mapSlot = 0;
    int regionSlot = 0;                 // 0 when attached to a host region
    int left = 0, top = 0, right = 0, bottom = 0;
    uint64_t mapUid = 0;
    bool chunkRetargeted = false;
    std::vector<std::string> notes;
};

// Throws std::runtime_error with the install untouched on any validation or
// staging failure; a failure during the final commit is reported with the
// backup paths in the message.
Result installLevel(const Request& request);

// Free 32-aligned origin for a donor-sized box: the first slot scanning right
// of every existing map on the donor's row band, then further rows. Never
// overlaps an existing map box.
struct Origin { int x = 0, y = 0; };
Origin suggestOrigin(const std::filesystem::path& gameRoot, const std::string& donorLevelName);

} // namespace forge::worldinstall
