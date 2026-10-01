// The Textures tab's model: browse textures.big by bank, decode one entry for a
// preview, export it as PNG, replace it from a PNG (same slot, allocated size kept, the
// entry's own pixel format) or add a new one. Writes go through forgecore's native
// texturewrite (the 0.14 importer, validated in-game) with a one-time .forge-orig backup
// and are refused while the game runs.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "terrainexport.hpp"

namespace albion::texbrowse {

struct TextureRow {
    std::string bank;      // e.g. GBANK_MAIN_PC, GUI_...
    std::string name;      // DevSymbolName (the key for replace/export; often a bracketed dev path)
    std::string label;     // what to show: the file stem of a "[\DEV\...\NAME.TGA]" name, else the name
    uint32_t id = 0;
    int width = 0, height = 0;      // real frame size
    int allocWidth = 0, allocHeight = 0;
    std::string format;    // DXT1 / DXT3 / ARGB8888 / <code>
    uint32_t bytes = 0;    // payload length
    int mips = 0;
};

// Every entry of every bank, in file order (a retail textures.big has ~6,300).
std::vector<TextureRow> listTextures(const std::filesystem::path& texturesBig, std::string& error);

// Decode an entry's first mip to RGBA.
bool decodeTexture(const std::filesystem::path& texturesBig, const std::string& entryName,
                   terrainexport::Image& out, std::string& error);

// Authored sprite frames, cropped out of allocation padding and stacked vertically.
// Supports sheets and repeated raw mip chains; a non-reconstructable animation
// reports an error instead of treating the complete sheet as one particle.
struct SpriteTexture {
    terrainexport::Image image;
    uint32_t frames = 0;
};
bool decodeSpriteFrames(const std::vector<uint8_t>& info, const std::vector<uint8_t>& payload,
                        SpriteTexture& out, std::string& error);
bool decodeSpriteTexture(const std::filesystem::path& texturesBig, const std::string& entryName,
                         SpriteTexture& out, std::string& error);

bool exportPng(const std::filesystem::path& texturesBig, const std::string& entryName,
               const std::filesystem::path& png, std::string& error);

// Replace `entryName` (any bank) from a PNG/JPG/TGA: resampled to the slot's allocated
// size, encoded in the slot's format, mips rebuilt, then validated against the retail
// entry contract. `notes` gets what happened.
bool replaceTexture(const std::filesystem::path& gameRoot, const std::string& entryName,
                    const std::filesystem::path& image, std::vector<std::string>& notes, std::string& error);

// Append a new entry to `bank` (default GBANK_MAIN_PC) as `format` (dxt1 | dxt3 |
// argb8888). The new id is returned through `idOut`.
bool addTexture(const std::filesystem::path& gameRoot, const std::string& bank, const std::string& entryName,
                const std::filesystem::path& image, const std::string& format, uint32_t& idOut,
                std::vector<std::string>& notes, std::string& error);

}  // namespace albion::texbrowse
