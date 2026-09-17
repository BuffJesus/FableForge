#pragma once
// forge::terraintex - the two halves of custom terrain texturing:
//
//  (1) THEME -> TEXTURE RESOLUTION (the link the Paint tool was missing).
//      A LEV cell stores three ground-theme PALETTE SLOTS (+10/+11/+12), not
//      texture ids. The LEV 256-entry palette record is
//      `char name[128]; u32 value`, and that `value` is the ENGINE_THEME
//      DEFINITION INDEX in data/CompiledDefs/game.bin - the same index the
//      engine resolves through CDefinitionManager in BuildThemes @0x00b687d0
//      (FableTLC docs/TERRAIN_RENDER_FIX.md, docs/NATIVE_TERRAIN_STATUS.md).
//      Decoding that def with the retail CEngineThemeDef schema yields the two
//      material triples the baked STB foreground layers carry:
//        base  = [BaseTexture,      BackgroundTexture,      BaseBumpMap ]
//        cliff = [CliffBaseTexture, CliffBackgroundTexture, CliffBumpMap]
//      Those int32s are textures.big GBANK_MAIN_PC ENTRY IDs.
//      Verified on FinalAlbion/Greatwood_1.lev: every triple baked into the
//      retail STB chunk is reproduced by a decoded theme def (e.g.
//      GROUND_FOREST_LEAVES base (4226,4226,0), PATH_COBBLES_IRREGULAR_ET base
//      (4118,4216,0)).
//
//  (2) TEXTURE ENTRY VALIDATION for imported PNGs. The writer itself is the
//      proven FableTLC tool `tools/texture_build.py` (Route B; validated on
//      227/230 retail entries, FableTLC docs/TEXTURE_WRITER.md) and we SHELL
//      OUT to it rather than re-deriving DXT/LZO rules here. What this module
//      adds natively is an independent CHECKER of the on-disk contract, so an
//      import can be proven correct without trusting the tool that wrote it:
//        Info(34) = CGraphicHeader(28) + CPixelFormatInit(6)
//        payload  = mip0 (Fable chunked-LZO when MipSize0>0, else raw)
//                   followed by mips 1..n-1 STORED RAW.
//
// Nothing here invents engine fields: every offset below is the retail layout
// recorded in FableTLC docs/TEXTURE_WRITER.md and def_schema.json.

#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "forge/bin.hpp"
#include "forge/defschema.hpp"
#include "forge/lev.hpp"
#include "forge/terrain.hpp"

namespace forge::terraintex {

// ---------------------------------------------------------------- pixel format
constexpr uint32_t kFormatARGB = 0x01;  // A8R8G8B8
constexpr uint32_t kFormatDXT1 = 0x1F;
constexpr uint32_t kFormatDXT3 = 0x20;

// CPixelFormatInit tail bytes {Type,ColourDepth,R,G,B,A} per format.
const uint8_t* pixelFormatTail(uint32_t format);   // nullptr if unknown
const char* pixelFormatName(uint32_t format);      // "DXT1" / "DXT3" / "A8R8G8B8"
// Uncompressed byte length of one linear mip surface.
size_t mipRawLength(uint32_t format, uint32_t w, uint32_t h);

struct TextureInfo {
    uint16_t allocWidth = 0, allocHeight = 0, depth = 0;
    uint16_t frameWidth = 0, frameHeight = 0, frameCount = 0;
    uint32_t pixelFormat = 0;
    uint8_t transparency = 0, mipLevels = 0, flags = 0, padding = 0;
    uint32_t frameDataSize = 0;   // uncompressed mip-0 size
    uint32_t mipSize0 = 0;        // on-disk mip-0 region size; 0 => all raw
    uint8_t tail[6] = {};         // CPixelFormatInit
};

bool parseTextureInfo(const uint8_t* data, size_t len, TextureInfo& out,
                      std::string& error);

struct TextureValidation {
    bool ok = false;                       // no hard errors
    TextureInfo info;
    std::vector<std::string> errors;       // contract violations
    std::vector<std::string> warnings;     // unusual but retail-observed
    size_t consumed = 0;                   // payload bytes accounted for
    size_t mip0RegionSize = 0;             // measured on-disk mip-0 region
    size_t chunkCount = 0;                 // chunked-LZO chunks in mip 0
    std::vector<size_t> mipRawSizes;       // expected raw size per mip level
};

// GPU-ready first mip. Block-compressed formats remain BC bytes; BGRA8 is
// returned in the byte order D3D11 expects for DXGI_FORMAT_B8G8R8A8_UNORM.
enum class PreviewFormat { BC1, BC2, BGRA8 };
struct DecodedMip0 {
    bool ok = false;
    uint32_t width = 0, height = 0;
    PreviewFormat format = PreviewFormat::BC1;
    std::vector<uint8_t> bytes;
    std::string error;
};

DecodedMip0 decodeMip0(const std::vector<uint8_t>& info,
                       const std::vector<uint8_t>& payload);

// Verify one texture entry against the on-disk writer contract. `info` is the
// entry 34-byte Info blob (the BIG entry subHeader), `payload` the entry data.
TextureValidation validateTextureEntry(const std::vector<uint8_t>& info,
                                       const std::vector<uint8_t>& payload);

// ------------------------------------------------------------- theme resolver
struct ThemeTextures {
    // index 0 = Base/CliffBase, 1 = Background/CliffBackground, 2 = bump map
    uint32_t base[3] = {0, 0, 0};
    uint32_t cliff[3] = {0, 0, 0};
};

struct ThemeEntry {
    uint32_t defIndex = 0;      // global game.bin entry index
    std::string name;           // e.g. GROUND_FOREST_LEAVES
    ThemeTextures textures;
    bool decoded = false;       // schema decode produced all six fields
    // Water: the engine's per-cell
    // water depth is sum(blend * WaterHeight) over the cell's theme slots and a
    // cell has water when any slot's WaterType != 0 (CEngineMap::PeekWaterDepth /
    // PeekHasWaterFast).
    float waterHeight = 0.0f;
    int32_t waterType = 0;
};

class ThemeLibrary {
public:
    // Index every ENGINE_THEME entry of a compiled-def file.
    static ThemeLibrary load(const bin::File& defs, const defschema::Schema& schema);
    // Convenience: open <gameRoot>/data/CompiledDefs/{names.bin,game.bin}.
    static ThemeLibrary loadFromRoot(const std::filesystem::path& gameRoot,
                                     const std::filesystem::path& schemaPath,
                                     const std::string& binName = "game.bin");

    const std::vector<ThemeEntry>& themes() const { return themes_; }
    const ThemeEntry* byDefIndex(uint32_t index) const;
    const ThemeEntry* byName(std::string_view name) const;
    // Every textures.big id any theme references (0 excluded: 0 = "none").
    std::set<uint32_t> referencedTextures() const;

private:
    std::vector<ThemeEntry> themes_;
};

struct PaletteSlotResolution {
    int slot = 0;                 // LEV palette slot (the byte in a cell)
    std::string paletteName;      // name as stored in the LEV palette
    uint32_t defIndex = 0;        // palette record `value`
    bool resolved = false;        // def index found and decoded
    bool nameMatches = false;     // palette name == def name (sanity cross-check)
    std::string defName;
    ThemeTextures textures;
    size_t cellCount = 0;         // cells whose slot-0..2 reference this slot
};

// Resolve every populated palette slot of a level. Slots with an empty name are
// skipped (unused palette rows).
std::vector<PaletteSlotResolution> resolvePalette(const lev::File& level,
                                                  const ThemeLibrary& library);

// Same resolution, shaped for the STB baker: index = LEV palette slot.
std::vector<terrain::TerrainThemeMaterial> paletteMaterials(
    const lev::File& level, const ThemeLibrary& library);

// ---------------------------------------------------------- PNG import driver
// Honest boundary: the encoder is FableTLC tools/texture_build.py. We invoke
// it and then check its output with validateTextureEntry() above.
struct ImportRequest {
    std::filesystem::path png;
    std::filesystem::path srcBig;
    std::filesystem::path outBig;
    std::string entryName;                 // slot to replace, or new entry name
    std::string subBank = "GBANK_MAIN_PC"; // only used when adding
    bool add = false;                      // false = replace an existing slot
    std::string format = "dxt1";           // dxt1 | dxt3 | argb8888
    std::string dims;                      // "WxH" for --dims (add only)
    // Store mip 0 uncompressed (--raw-mip0). Normally unnecessary: the reason an
    // appended 256x256 minimap once failed to draw was the chunk header form
    // (the engine needs the 0xFFFF+u32 escape for >= 64 KiB raw chunks, fixed
    // in lionhead_lz_compress.py), not the LZO stream itself.
    bool rawMip0 = false;
    std::filesystem::path python;          // default: "python"
    std::filesystem::path toolsDir;        // dir holding texture_build.py
};

struct ImportResult {
    bool ok = false;
    std::string command;
    std::string output;                    // combined stdout/stderr of the tool
    int exitCode = -1;
    uint32_t entryId = 0;                  // resolved id in the OUTPUT big
    std::string bankName;
    TextureValidation validation;
};

// Locate texture_build.py: explicit toolsDir, else FABLETLC_TOOLS env var, else
// the known FableTLC checkout. Returns an empty path when not found.
std::filesystem::path findTextureBuilder(const std::filesystem::path& toolsDir);

ImportResult importPng(const ImportRequest& request);

// Validate an entry that already lives in a BIG.
TextureValidation validateBigEntry(const std::filesystem::path& bigPath,
                                   std::string_view entryName,
                                   std::string* bankOut = nullptr,
                                   uint32_t* idOut = nullptr);

} // namespace forge::terraintex
