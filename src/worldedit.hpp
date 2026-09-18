#pragma once
// World-level editing: new levels cloned from a donor map, installed straight
// into the game's four world containers (BWD + WLD + WAD + STB) with the
// terrain chunk translated to the new origin (src/stbrelocate). The install part is forgecore's
// worldinstall (the library form of `forge world install-level`); Atlas adds
// the re-bake, the .atlas-orig backups and the donor lookup.

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace forge::terraintex { class ThemeLibrary; }

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

// Own region for a new level. Two ways: a NEW region slot (`dedicated`; there
// is no engine cap -- proven 2026-09-17 with region 146 -- but saves cache the
// region table, so the region is only complete on a game started after it was
// added), or taking over a retail *filler* slot (a region that only owns
// decorative, never-entered maps): its maps are re-owned by another filler and
// the slot is renamed, which existing saves do see. Both get the level's
// minimap texture and display name.
struct OwnRegion {
    bool wanted = false;
    bool dedicated = false;     // new slot instead of a filler take-over
    std::string takeOver;       // filler region to repurpose (see reusableRegions)
    std::string mergeInto;      // filler region that receives its maps
    std::string displayName;    // shown on the map screen; default = level name
    std::string regionDef;      // "" like the retail fillers
    bool minimap = true;        // bake a MINIMAP_<NAME> texture into textures.big
};

struct ReusableRegion { int slot = 0; std::string name; int maps = 0; };
// Filler regions (no def, no minimap graphic) that can be taken over, sorted
// by map count; the first is the suggested victim, the last the merge target.
std::vector<ReusableRegion> reusableRegions(const std::filesystem::path& gameRoot, std::string& error);

// Called from the job's thread at each stage boundary with a short label
// ("baking the minimap", "writing FinalAlbion.wad"); the GUI shows it on the
// busy button. Optional everywhere.
using ProgressFn = std::function<void(const std::string&)>;   // (also declared in overworld.hpp; identical)

struct NewLevelRequest {
    ProgressFn progress;
    std::string donor;          // existing level (stem)
    std::string name;           // new level stem: letters, digits, '_'
    std::string hostRegion;     // existing region to own the map; "" = a dedicated region (complete on a new game)
    OwnRegion ownRegion;        // wins over hostRegion when wanted
    int worldX = 0, worldY = 0; // 32-aligned origin
    bool rebakeChunk = true;    // re-bake the STB chunk for the new origin (recommended; otherwise donor geometry)
};

struct NewLevelResult {
    int mapSlot = 0;
    int worldX = 0, worldY = 0, width = 0, height = 0;
    std::vector<std::string> notes;
};

// A level authored from scratch: a flat heightfield of any retail size (sides
// multiples of 16, the shipped maps run 32x32 .. 160x256) carrying one ground
// theme, every cell walkable, an empty .tng, and a terrain chunk assembled by
// forgecore's builder (no donor geometry -- the path that is known to render
// in-game; cloned donor chunks currently draw white). The LEV skeleton
// (header, palette) comes from `templateLevel`, a retail map of the same size
// ("" = the first retail map of that size); the ground theme is one of that
// palette's slots.
struct BlankLevelRequest {
    ProgressFn progress;
    std::string name;
    std::string hostRegion;
    int worldX = 0, worldY = 0;
    int width = 64, height = 64;
    std::string templateLevel;   // retail map of exactly width x height whose palette/header are reused; "" = auto
    OwnRegion ownRegion;
    int themeSlot = -1;          // palette slot of the ground theme (-1 = the template's most used)
    float groundHeight = 20.0f;  // flat height of the plane
    uint16_t backgroundRgb565 = 0x4C89;   // distant-LOD colour (a mid green)
};

// Palette of a retail level (slot -> ground theme name, empty = unused), for the picker.
bool templatePalette(const std::filesystem::path& gameRoot, const std::string& level, std::vector<std::string>& names, std::string& error);
// The retail map sizes in the world (BWD boxes), each with one template map of that size.
struct MapSize { int width = 0, height = 0; std::string templateLevel; int count = 0; };
std::vector<MapSize> retailMapSizes(const std::filesystem::path& gameRoot, std::string& error);

bool createBlankLevel(const std::filesystem::path& gameRoot, const BlankLevelRequest& request,
                      const forge::terraintex::ThemeLibrary& library, NewLevelResult& out, std::string& error);

// Bake the level's minimap (top-down albedo + hillshade, north up, the retail
// circular vignette) from its LEV bytes, append it to textures.big as a
// 256x256 DXT3 entry MINIMAP_<NAME> and register the name in the
// PLAYER_GUI defs (registerMinimapGraphic) so the engine resolves it. Uses the
// FableTLC texture writer through forgecore's import driver. One-time
// .atlas-orig backups of textures.big and game.bin/names.bin. `entryName` may
// name the entry (default MINIMAP_<NAME>); an existing entry of that name is
// replaced.
bool bakeMinimapTexture(const std::filesystem::path& gameRoot, const std::string& levelName,
                        const std::vector<uint8_t>& levBytes, const forge::terraintex::ThemeLibrary* library,
                        std::string& entryName, std::vector<std::string>& notes, std::string& error);

// Retail resolves a region's MiniMapGraphic name through the PLAYER_GUI def's
// MiniMapGraphics map (name -> GBANK_MAIN_PC id; CTCInventoryBase::
// GetMiniMapGraphic, FableWin 0x236e25f), NOT through the bank's TOC symbols:
// a renamed or appended texture is invisible until it is registered there.
// Adds/updates `name` -> `id` in PLAYER_GUI_PC (and PLAYER_GUI_DEFAULT when it
// carries the map) of data/CompiledDefs/game.bin, one-time .atlas-orig backups.
bool registerMinimapGraphic(const std::filesystem::path& gameRoot, const std::string& name, uint32_t id,
                            std::vector<std::string>& notes, std::string& error);

// A ground theme from your own texture: the PNG goes into textures.big
// (GBANK_MAIN_PC, appended, DXT1; the terrain layers reference it by that
// global id) and a new ENGINE_THEME def is appended to game.bin as a copy of
// `donor` with its base/background (and cliff) textures pointed at the new
// entry (bump maps cleared). Existing def indices are untouched (append only).
// One-time .atlas-orig backups of textures.big, names.bin and game.bin.
struct CustomThemeRequest {
    std::filesystem::path png;        // the ground texture (power-of-two square, e.g. 512x512)
    std::filesystem::path cliffPng;   // optional: a different texture for steep faces (default: the same)
    std::string name;                 // ENGINE_THEME name, e.g. GROUND_MY_MOSS (A-Z 0-9 _)
    std::string donor = "GROUND_GRASS";   // the ENGINE_THEME whose other fields are copied
};
struct CustomThemeResult {
    uint32_t defIndex = 0;            // the new ENGINE_THEME's global def index (for the LEV palette)
    uint32_t baseTexture = 0, cliffTexture = 0;   // GBANK_MAIN_PC ids
    std::vector<std::string> notes;
};
bool createCustomTheme(const std::filesystem::path& gameRoot, const CustomThemeRequest& req,
                       CustomThemeResult& out, std::string& error);

// One-time .atlas-orig backups of FinalAlbion.bwd/.wld/.wad and FinalAlbion_RT.stb,
// then the staged atomic install. The new level's LEV/TNG are the donor's
// current bytes (a loose donor .lev/.tng wins over the WAD copy, like the game).
bool createLevelFromDonor(const std::filesystem::path& gameRoot, const NewLevelRequest& request,
                          NewLevelResult& out, std::string& error);

} // namespace albion::editor
