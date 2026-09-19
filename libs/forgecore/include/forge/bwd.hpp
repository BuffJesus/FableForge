#pragma once
// Reader/writer for FinalAlbion.bwd — the COMPILED (binary) world map the retail
// engine actually loads when userst.ini `UseCompiledWorldFiles` is TRUE (the
// retail default). The text FinalAlbion.wld is only parsed when that var is
// FALSE, so editing the .wld alone never reaches runtime; the .bwd must be
// regenerated to match.
//
// Layout is byte-for-byte the oracle proven in tools/wld_bwd.py (FableTLC) and
// grounded in the retail deserializers:
//   CWorldMap::LoadFromFile   0x00507c30 (gate DAT_013b8618 "UseCompiledWorldFiles")
//   LoadWorldFromBinaryFile   0x00507650 (reads the .bwd)
//   CMapInfo::LoadBinary      0x004fb4f0 (map record)
//   CRegion::LoadBinary       0x006bc510 (region record)
//
// File layout (little-endian):
//   u32 mapCount                 // slots 0..mapCount-1; slot 0 is NEVER stored
//   (mapCount-1) x MapInfo       // slots 1..mapCount-1, in slot order
//   u32 regionCount              // slots 0..regionCount-1; slot 0 NEVER stored
//   (regionCount-1) x Region
//
// A "presized string" is u32 length + raw bytes (no NUL).

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "forge/wld.hpp"

namespace forge::bwd {

// One CMapInfo record (CMapInfo::LoadBinary 0x4fb4f0). Field order below matches
// the on-disk read order, not the in-memory struct offsets.
struct MapInfo {
    std::string levelName;   // "Data\\Levels\\FinalAlbion\\X.lev"
    std::string scriptName;  // "X"
    uint8_t used = 1;
    uint8_t loadedOnProximity = 0;
    uint8_t isSea = 0;
    int32_t left = 0;    // boxLeft  (MapX)
    int32_t right = 0;   // boxRight
    int32_t top = 0;     // boxTop   (MapY)
    int32_t bottom = 0;  // boxBottom
    uint8_t flag2 = 1;   // second used/placement flag
    uint64_t mapUid = 0;
};

// One CRegion record (CRegion::LoadBinary 0x6bc510).
struct RegionExit {
    std::string name;
    uint8_t vec[8] = {0, 0, 0, 0, 0, 0, 0, 0}; // {f32 x, f32 y} kept as raw bytes
};

struct Region {
    std::vector<int32_t> contains;     // map slot indices this region owns
    std::vector<int32_t> sees;         // map slot indices this region can see
    std::string name;
    std::string displayName;
    std::string regionDef;             // resolved via GetDefGlobalIndexFromName; often ""
    std::string minimapGraphic;
    uint8_t onWorldMap = 0;
    uint8_t creatureGen = 1;
    uint8_t soundThemes = 1;
    uint8_t minimapScale[4] = {0x00, 0x00, 0x80, 0x3f}; // raw f32 bytes, default 1.0f
    int32_t mmOffX = 0;
    int32_t mmOffY = 0;
    int32_t wmOffX = 0;
    int32_t wmOffY = 0;
    std::vector<RegionExit> exits;
};

// Parameters for authoring a brand-new custom level + its owning region WITHOUT
// cloning any existing record. The map/region are synthesized from scratch with
// engine-accepted defaults (matched to the loaded ForgeTest slot 399 / region
// 142 records). `levelName` is the bare stem (e.g. "MyLevel"); the full LEV path
// and script name are derived. Empty region fields default from the level name.
struct NewLevel {
    std::string levelName;                 // bare stem, e.g. "MyLevel"
    int32_t left = 0, top = 0, right = 0, bottom = 0; // world box
    bool loadedOnProximity = false;
    bool isSea = false;
    std::string regionName;                // default: levelName
    std::string regionDisplayName;         // default: regionName
    std::string regionDef;                 // default: "" (matches loaded ForgeTest)
    std::string minimapGraphic;            // default: ""
    uint64_t mapUid = 0;                   // 0 => max(existing)+1
    // Set true to also register the new map in an EXISTING region's contains[]
    // list (a "sees"/host attach). Off by default — the new dedicated region is
    // the clean path; host-attach is the legacy region-95 workaround.
    std::string alsoContainInRegion;       // region NAME, or "" for none
};

// Result of a synthesis: 1-based world slots the engine addresses by.
struct AssignedSlots {
    int mapSlot = 0;     // e.g. 399
    int regionSlot = 0;  // e.g. 142; 0 if no new region was created
};

struct OwnershipAudit {
    // Index 0 is unused; indices 1..N are compiled map slots.
    std::vector<int> ownerCounts;
    // Pairs of 1-based region slot and invalid referenced map slot.
    std::vector<std::pair<size_t, int32_t>> invalidReferences;
};

class File {
public:
    static File parse(const std::filesystem::path& path);
    static File parse(const std::vector<uint8_t>& data);

    const std::vector<MapInfo>& maps() const { return maps_; }     // index 0 == slot 1
    const std::vector<Region>& regions() const { return regions_; } // index 0 == slot 1
    std::vector<MapInfo>& maps() { return maps_; }
    std::vector<Region>& regions() { return regions_; }

    // 1-based slot count as the engine sees it (stored count + phantom slot 0).
    int mapCount() const { return static_cast<int>(maps_.size()) + 1; }
    int regionCount() const { return static_cast<int>(regions_.size()) + 1; }

    const MapInfo* findMap(std::string_view levelNameOrScript) const;
    Region* findRegion(std::string_view regionName);
    const Region* findRegion(std::string_view regionName) const;
    uint64_t maxMapUid() const;
    OwnershipAudit auditOwnership() const;

    // Low-level append (caller supplies a fully-formed record). Returns the
    // assigned 1-based slot.
    int addMap(MapInfo map);
    int addRegion(Region region);

    // High-level, no-clone authoring: synthesize a fresh map + dedicated region
    // from `spec` and append both. This is the one-call path a typical user
    // needs to add a custom level.
    AssignedSlots addLevel(const NewLevel& spec);

    // Byte-identical to the parsed input while unmodified.
    std::vector<uint8_t> serialize() const;
    void write(const std::filesystem::path& path) const;

private:
    std::vector<MapInfo> maps_;
    std::vector<Region> regions_;
};

// Fills width/height (in world units) for a BWD level path
// ("Data\\Levels\\FinalAlbion\\X.lev"); returns false if unknown. Typically
// backed by the STB CStaticMapInfoBlock (forge::stbinfo mapWidth/mapHeight) or
// the LEV dimensions.
using DimSource =
    std::function<bool(const std::string& levelName, int& width, int& height)>;

// PHASE 3 — full text->binary compile: build a complete .bwd from the text
// .wld (the single source of truth) plus a dimension source for map sizes (the
// only field the .wld does not carry). No donor .bwd required. Fields not present
// in the .wld are set to the retail-constant defaults verified across the whole
// world (used=1, flag2=1, region creatureGen=1, soundThemes=1). Region
// contains/sees LevelName references are resolved to map slot indices.
//
// This is proven byte-exact against the retail FinalAlbion.bwd when fed the
// retail .wld + STB dims (see tests/test_bwd.cpp).
File compileFromWld(const forge::wld::File& wld, const DimSource& dims);

} // namespace forge::bwd
