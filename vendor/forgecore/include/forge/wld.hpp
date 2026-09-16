#pragma once
// Parser for FinalAlbion.wld — the world file tying levels together. Text
// format observed from the shipping file:
//   START_INITIAL_QUESTS; <quest names;> END_INITIAL_QUESTS;
//   MapUIDCount N; ThingManagerUIDCount N;
//   NewMap <index>; MapX/MapY/LevelName/LevelScriptName/MapUID/IsSea/
//     LoadedOnPlayerProximity ...; EndMap;
//   NewRegion <index>; RegionName/NewDisplayName/RegionDef/AppearOnWorldMap/
//     MiniMap*/WorldMap*/ContainsMap*/SeesMap* ...; EndRegion;
// Raw lines are kept for byte-identical serialization.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace forge::wld {

struct Map {
    int index = 0;
    int mapX = 0;
    int mapY = 0;
    std::string levelName;       // e.g. FinalAlbion\LookoutPoint.lev (quotes stripped)
    std::string levelScriptName; // quotes stripped
    uint32_t mapUid = 0;
    bool isSea = false;
    bool loadedOnPlayerProximity = false;
};

struct Region {
    int index = 0;
    std::string regionName;  // quotes stripped
    std::string displayName; // NewDisplayName, quotes stripped
    std::string regionDef;   // quotes stripped
    bool appearOnWorldMap = false;
    std::vector<std::string> containsMaps; // LevelName values, quotes stripped
    std::vector<std::string> seesMaps;

    // Lossless fields required to compile the binary .bwd
    // (forge::bwd::compileFromWld). Captured from the text but not needed by the
    // WLD text editor; serialization stays byte-identical via the raw lines.
    std::string minimapGraphic;                 // MiniMapGraphic (token)
    float minimapScale = 1.0f;                   // MiniMapScale
    float miniMapOffsetX = 0.0f, miniMapOffsetY = 0.0f;   // MiniMapOffsetX/Y
    float worldMapOffsetX = 0.0f, worldMapOffsetY = 0.0f; // WorldMapOffsetX/Y
    float nameGraphicOffsetX = 0.0f, nameGraphicOffsetY = 0.0f;
    struct ExitTextOffset { std::string name; float x = 0.0f, y = 0.0f; };
    std::vector<ExitTextOffset> exitTextOffsets; // MiniMapRegionExitTextOffsetX/Y[name]
};

class File {
public:
    static File parse(const std::filesystem::path& path);
    static File parseText(std::string text, std::string sourceName = {});

    const std::vector<std::string>& initialQuests() const { return initialQuests_; }
    const std::vector<Map>& maps() const { return maps_; }
    const std::vector<Region>& regions() const { return regions_; }
    int mapUidCount() const { return mapUidCount_; }
    const std::string& source() const { return source_; }

    const Map* findMap(std::string_view levelName) const;
    const Region* findRegion(std::string_view regionName) const;

    // Editor mutations modeled after CEditWorldMap. Existing text is retained
    // byte-for-byte; new map blocks are inserted before the first region and
    // new regions append at EOF. index/mapUid == 0 selects max(existing)+1.
    // Explicit IDs are recommended for reproducible projects.
    const Map& addMap(Map map);
    const Region& addRegion(Region region);

    // Add a WLD LevelName to a region's ContainsMap list and, optionally, its
    // SeesMap list. Duplicate references are ignored. The level must already
    // exist in this world.
    void addMapToRegion(std::string_view regionName,
                        std::string_view levelName,
                        bool visible = true);

    // Enforce the retail one-owner partition for an existing map: remove its
    // ContainsMap reference from every region and add it to exactly one target.
    // SeesMap visibility references are intentionally retained.
    void setMapOwner(std::string_view regionName, std::string_view levelName);

    // Rewrite MapX/MapY for an existing map while preserving every unrelated
    // source line byte-for-byte.
    void relocateMap(std::string_view levelName, int mapX, int mapY);

    // Byte-identical to the parsed input while unmodified.
    std::string serialize() const;

private:
    std::string source_;
    std::vector<std::string> rawLines_;
    std::vector<std::string> initialQuests_;
    std::vector<Map> maps_;
    std::vector<Region> regions_;
    int mapUidCount_ = 0;
};

} // namespace forge::wld
