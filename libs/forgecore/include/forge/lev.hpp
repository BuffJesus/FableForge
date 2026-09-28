#pragma once
// Reader for Fable TLC .lev terrain files. Byte layout recovered from the
// decompiled FableMod.LEV (ChocolateBox DecompiledDLLs), the library that
// SilverChest.LevBridge uses — our theme-grid ground truth:
//   LEVHeader   (25B): u32 headerSize, u16 version(==6404), u8 pad[3],
//                      u32 reserved1, u32 obsOffset, u32 reserved2, u32 navOffset
//   LEVMapHeader(22B): u8 size, u8 mapVersion(==8), u8 pad[3] (pad[2] is a
//                      sub-version, 8 or 9), u32 uidLo, u32 uidHi,
//                      i32 width, i32 height, u8 flag
//   256 ground themes: char name[128] + u32 value
//   u32 fileVersion, u32 soundCount, 256 atmos themes (char name[128] + u32
//     value: the ENVIRONMENT themes painted per game-map cell),
//   (sub-version 9 only) u32 extra,
//   (soundCount-1) x { u32 len, char name[len] } SOUND_THEME names (index 0 =
//     the implicit NULL),
//   cells: (height+1)*(width+1) x 21B -- u32 21 + u8 7 record header, float
//     heightRaw at +5 (world height = raw * 2048), theme indices at +10..12,
//     slot-0/1 strengths at +13..14 (slot 2 is the implicit remainder), walkable
//     +15, camera-passable +16, old sound (fileVersion < 3) +17, shore point +19,
//     preferred path +20,
//   (fileVersion > 1) game-map grid: (width/4)*(height/4) x 11B CGameMapCell,
//     one per 4x4 height cells -- u32 11 + u8 1 header, atmos slots +5..7,
//     atmos strengths +8..9 (slot 2 the remainder), sound index +10 -- ending
//     exactly at obsOffset,
//   obstacle data up to navOffset, then navigation sections.
// The atmos palette, sound list and game-map grid: FableWin CMap::LoadFromFile
// 0x022327b0 / SaveToFile 0x02234c60, CMap::Get/SetSoundAt 0x02238d20/0x02238d90,
// PDB CGameMapCell (checked over the dev tree's 404 levels).

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace forge::lev {

// A ground theme is the map's palette slot: `value` is the GLOBAL game.bin def
// entry index of an ENGINE_THEME, and `name` is only the editor's label. The
// per-cell bytes at +10..+12 store the palette SLOT, and CMap turns that into a
// def index through this table -- see CMap::AddThemeDefIndexToPalette /
// CMap::GetThemePaletteIndexFromThemeDefIndex in the FableWin symbols.
//
// Because the index is global, adding or removing any def in game.bin shifts it,
// and most shipped LEVs carry indices from an older game.bin: 377 of the 398 in
// the stock WAD have stale slots (shifts of +3, +8, +205...; Greatwood_1 is one
// of the few current ones, GROUND_GRASS_NO_LOCAL_DETAIL -> 1917). The engine does
// not use the stored index: CMap::LoadFromFile (FableWin 0x022327b0) looks every
// used slot's NAME up (GetDefGlobalIndexFromName), keeps it when it is an
// ENGINE_THEME and leaves the slot unused otherwise. Resolve by name first.
struct GroundTheme {
    std::string name; // e.g. GROUND_GRASS_NO_LOCAL_DETAIL
    uint32_t value = 0;
};

// Supplied by the caller so this reader stays free of any def-bank dependency.
// `indexOf` resolves a theme name to its global def entry index in the TARGET
// install; `typeOf` names the def a raw index actually points at, purely so the
// audit can say "this points at a CREATURE" instead of "this is wrong".
struct DefIndexTable {
    std::function<bool(const std::string& name, uint32_t& index)> indexOf;
    std::function<std::string(uint32_t index)> typeOf;
};

struct ThemePaletteIssue {
    int slot = 0;
    std::string name;
    uint32_t stored = 0;
    uint32_t expected = 0;
    std::string storedDefType; // what `stored` currently resolves to, if known
    bool resolvable = false;   // false => the target install has no such theme
};

struct NavSectionInfo {
    std::string name;
    uint32_t offset = 0;
};

class File {
public:
    static File open(const std::filesystem::path& path);

    // Cell grid dimensions: (width()+1) x (height()+1) cells.
    int width() const { return width_; }
    int height() const { return height_; }
    int cellsX() const { return width_ + 1; }
    int cellsY() const { return height_ + 1; }

    uint64_t uid() const { return uid_; }
    const std::vector<GroundTheme>& groundThemes() const { return groundThemes_; }
    // The SOUND_THEME names of the game-map grid (index i+1 in a cell; 0 = none).
    const std::vector<std::string>& themes() const { return themes_; }
    const std::vector<std::string>& soundThemes() const { return themes_; }
    // The 256-slot atmos (ENVIRONMENT theme) palette; a slot named "" is unused.
    const std::vector<GroundTheme>& atmosThemes() const { return atmosThemes_; }
    // Name a free / existing atmos slot (patches the preserved bytes, size unchanged).
    void setAtmosTheme(size_t slot, const std::string& name, uint32_t value);

    // --- the game-map grid (environment + sound themes, one cell per 4x4) ---
    bool hasGameMap() const { return gameMapOffset_ != 0; }
    int gameMapWidth() const { return width_ / 4; }
    int gameMapHeight() const { return height_ / 4; }
    struct AtmosBlend { std::array<uint8_t, 3> indices{}; std::array<uint8_t, 3> strengths{255, 0, 0}; };
    AtmosBlend atmosAt(int gx, int gy) const;
    // strengths must sum to 255 (slot 2 is stored as the remainder)
    void setAtmosAt(int gx, int gy, const AtmosBlend& blend);
    // Appends a SOUND_THEME name to the map's list (or finds it) and returns its cell
    // index (1-based). The list sits before the cells, so everything after it moves
    // by 4 + name length; every absolute offset the engine stores is shifted with it:
    // header BrushDataOffset (+13) and NavigationSectionsOffset (+21), the nav table's
    // end, each section's offset and each section's leading end-of-section u32
    // (CMap::LoadBrushesFromFile 0x02236060, CreateNavMap 0x01c8d2e0,
    // CNavQuadTree::Save/LoadFromFile 0x03289b50/0x0328a760), plus NoSoundThemeEntries.
    // Throws past 127 names (the cell byte is a signed char).
    uint8_t addSoundTheme(const std::string& name);
    uint8_t soundAt(int gx, int gy) const;         // 0 = none, else soundThemes()[i - 1]
    void setSoundAt(int gx, int gy, uint8_t index);
    const std::vector<NavSectionInfo>& navSections() const { return navSections_; }
    const std::string& source() const { return source_; }
    uint32_t navigationOffset() const { return navOffset_; }
    const std::vector<uint8_t>& originalBytes() const { return originalBytes_; }

    // World-space terrain height (raw cell float * 2048, matching FableMod).
    float heightAt(int x, int y) const;
    // Change only the height float in this cell. save() copies the original LEV
    // and patches the cell grid, preserving every opaque section byte-for-byte.
    void setHeightAt(int x, int y, float worldHeight);
    void save(const std::filesystem::path& path) const;
    size_t cellDataOffset() const { return cellsOffset_; }
    bool walkableAt(int x, int y) const;
    void setWalkableAt(int x, int y, bool walkable);
    // +16: the camera may pass (vanilla Survey > Passability "Camera passability";
    // CMap::SaveToFile writes camera-passable OR walkable, so walkable implies it)
    bool cameraPassableAt(int x, int y) const;
    void setCameraPassableAt(int x, int y, bool passable);
    bool preferredPathAt(int x, int y) const;
    void setPreferredPathAt(int x, int y, bool preferred);
    // index 0..2: up to three blended ground themes per cell.
    uint8_t themeIndexAt(int x, int y, int index) const;
    uint8_t themeStrengthAt(int x, int y, int index) const;
    // Replace all three theme slots atomically. Strengths must sum to 255; slots
    // 0/1 are stored at +13/+14 and slot 2 is their implicit remainder.
    void setThemeBlendAt(int x, int y,
                         const std::array<uint8_t, 3>& indices,
                         const std::array<uint8_t, 3>& strengths);
    // The blend layer with the highest strength (SilverChest's convention for
    // "the" theme of a cell). Returns the layer index 0..2.
    int dominantLayerAt(int x, int y) const;
    // Name of the cell's dominant ground theme.
    const std::string& themeNameAt(int x, int y) const;

    // --- ground-theme palette integrity -------------------------------------
    // Set one palette slot's def index. Patches the preserved original bytes so
    // save() emits it; same size, no other byte moves.
    void setGroundThemeValue(size_t slot, uint32_t defIndex);
    // Populate or replace a complete fixed-width palette entry. `name` must fit
    // the retail 128-byte NUL-terminated field.
    void setGroundTheme(size_t slot, const std::string& name, uint32_t defIndex);
    // Every named slot whose stored index is not what the target install says it
    // should be, plus any slot naming a theme the install does not have.
    std::vector<ThemePaletteIssue> auditThemePalette(const DefIndexTable& defs) const;
    // Rewrite every resolvable wrong slot to the target install's index.
    // Returns the number of slots changed. Unresolvable slots are left alone --
    // guessing an index would be worse than an honest validation failure.
    size_t rebaseThemePalette(const DefIndexTable& defs);

private:
    const uint8_t* cell(int x, int y) const;
    uint8_t* cell(int x, int y);
    void checkCell(int x, int y) const;

    std::string source_;
    int width_ = 0;
    int height_ = 0;
    uint64_t uid_ = 0;
    uint32_t navOffset_ = 0;
    std::vector<GroundTheme> groundThemes_;
    std::vector<std::string> themes_;
    std::vector<GroundTheme> atmosThemes_;
    size_t atmosPaletteOffset_ = 0;
    size_t gameMapOffset_ = 0;          // 0 = the file has no game-map grid
    uint8_t* gameMapCell(int gx, int gy);
    const uint8_t* gameMapCell(int gx, int gy) const;
    std::vector<NavSectionInfo> navSections_;
    std::vector<uint8_t> cells_; // 21 bytes per cell, row-major
    std::vector<uint8_t> originalBytes_;
    size_t cellsOffset_ = 0;
};

} // namespace forge::lev
