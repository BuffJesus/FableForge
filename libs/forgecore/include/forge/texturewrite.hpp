#pragma once
// Native Fable texture writer: RGBA -> a textures.big entry (34-byte Info +
// chunked-LZO mip 0 + raw mips) in DXT1 / DXT3 / A8R8G8B8, mirroring
// FableTLC tools/texture_build.py (the validated Python reference) and
// EgoCore's TextureBuilder:
//   * allocated size is a power of two (the image is resampled to it; the
//     real size is kept in the Info), mips chain down to 4x4 for DXT / 1x1 for
//     ARGB with a 2x2 box filter;
//   * mip 0 is stored as ONE Fable chunk: LZO1X of the first len-3 bytes then
//     the 3 raw tail bytes, header [u16 clen] -- or the [0xFFFF][u32 clen]
//     escape whenever the RAW chunk is >= 64 KiB (retail writes it that way for
//     3082 of 3542 such GBANK_MAIN_PC mip-0 chunks; a short header there made
//     the engine drop an appended 256x256 minimap) -- mips 1+ are raw;
//   * Info = alloc w/h, depth 0, real w/h, frames 1, format, transparency,
//     mip count, flags, frameDataSize (raw mip-0 bytes), mipSize0 (on-disk
//     mip-0 bytes, 0 = stored raw), CPixelFormatInit tail.
// No Python, no external process. Validate the result with
// terraintex::validateTextureEntry().
#include <cstdint>
#include <string>
#include <vector>

namespace forge::texturewrite {

constexpr uint32_t kFormatARGB = 0x01;
constexpr uint32_t kFormatDXT1 = 0x1F;
constexpr uint32_t kFormatDXT3 = 0x20;

struct Image {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;   // width * height * 4
};

// PNG/JPG/BMP/TGA through stb_image. Empty image + error on failure.
Image loadImage(const std::string& path, std::string& error);
// Resample to (w, h): 2x2 box when halving exactly, bilinear otherwise.
Image resample(const Image& img, int w, int h);
Image downsample(const Image& img);   // one mip step (box filter; odd dims nearest)

uint32_t formatByName(const std::string& name);   // "dxt1" | "dxt3" | "argb8888"/"argb"/"a8r8g8b8"; 0 = unknown
int fullMipCount(int w, int h, uint32_t format);
size_t mipRawLength(uint32_t format, int w, int h);

std::vector<uint8_t> encodeDxt1(const Image& img);   // opaque four-colour mode
std::vector<uint8_t> encodeDxt3(const Image& img);   // explicit 4-bit alpha (rounded)
std::vector<uint8_t> encodeArgb(const Image& img);   // BGRA byte order
std::vector<uint8_t> encodeMip(uint32_t format, const Image& img);

// The Fable chunk framing of one raw block (see above).
std::vector<uint8_t> compressFableBlock(const std::vector<uint8_t>& raw);

struct Entry {
    std::vector<uint8_t> info;      // 34 bytes
    std::vector<uint8_t> payload;
    int mips = 0;
};
// `img` must already have the allocated (power-of-two) size; realW/realH are
// what the Info reports as the frame size (0 = the allocated size).
Entry buildEntry(const Image& img, uint32_t format, int realW = 0, int realH = 0, bool compressMip0 = true, int mips = 0);

} // namespace forge::texturewrite
