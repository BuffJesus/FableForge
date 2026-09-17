#pragma once
// World-level editing: new levels cloned from a donor map, installed straight
// into the game's four world containers (BWD + WLD + WAD + STB) with the
// terrain chunk re-baked for the new origin. The install part is forgecore's
// worldinstall (the library form of `forge world install-level`); Atlas adds
// the re-bake, the .atlas-orig backups and the donor lookup.

#include <filesystem>
#include <string>
#include <vector>

namespace albion::editor {

struct DonorInfo {
    int width = 0, height = 0;             // map size in cells (world units)
    int worldX = 0, worldY = 0;            // the donor's own placement
    std::string owningRegion;              // WLD region that contains the donor
    int suggestedX = 0, suggestedY = 0;    // a free 32-aligned origin for a copy
    std::vector<std::string> regions;      // every region name, WLD order (slot 1..n)
};

// Reads the WLD/BWD/STB for the donor. Errors are returned, not thrown.
bool donorInfo(const std::filesystem::path& gameRoot, const std::string& donor, DonorInfo& out, std::string& error);

struct NewLevelRequest {
    std::string donor;          // existing level (stem)
    std::string name;           // new level stem: letters, digits, '_'
    std::string hostRegion;     // existing region to own the map; "" = dedicated region (unreachable past slot 141)
    int worldX = 0, worldY = 0; // 32-aligned origin
    bool rebakeChunk = true;    // re-bake the STB chunk for the new origin (recommended; otherwise donor geometry)
};

struct NewLevelResult {
    int mapSlot = 0;
    int worldX = 0, worldY = 0, width = 0, height = 0;
    std::vector<std::string> notes;
};

// One-time .atlas-orig backups of FinalAlbion.bwd/.wld/.wad and FinalAlbion_RT.stb,
// then the staged atomic install. The new level's LEV/TNG are the donor's
// current bytes (a loose donor .lev/.tng wins over the WAD copy, like the game).
bool createLevelFromDonor(const std::filesystem::path& gameRoot, const NewLevelRequest& request,
                          NewLevelResult& out, std::string& error);

} // namespace albion::editor
