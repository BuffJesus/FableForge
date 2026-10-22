#pragma once
// Native Fable texture writer: RGBA -> a textures.big entry (34-byte Info +
// chunked-LZO mip 0 + raw mips) in DXT1 / DXT3 / DXT5 / A8R8G8B8 / signed V8U8, mirroring
// FableTLC tools/texture_build.py (the validated Python reference) and
// EgoCore's TextureBuilder:
//   * allocated size is a power of two (the image is resampled to it; the
//     real size is kept in the Info), mips stop when the shorter side reaches
//     4 for DXT / 1 for ARGB or V8U8, with a 2x2 box filter;
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
constexpr uint32_t kFormatDXT5 = 0x23;
constexpr uint32_t kFormatV8U8 = 0x18;

struct Image {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;   // width * height * 4
    bool fromDds = false;
    uint32_t sourceFormat = 0;
    std::vector<std::vector<uint8_t>> sourceMips; // native block bytes, or normalized BGRA
};

// PNG/JPG/BMP/TGA through stb_image; legacy 2D DDS (DXT1/3/5, 32-bit RGBA/BGRA).
// DDS mip 0 is decoded; compatible authored mip bytes are retained on import.
// Resizing or format conversion regenerates the chain and reports that decision.
// Empty image + actionable error on failure. Cubes/volumes/DX10/bump DDS are refused.
Image loadImage(const std::string& path, std::string& error);
// Standard legacy 2D DDS, including legal 2x2/1x1 block-compressed tail mips.
// mipLevels=0 generates a complete chain. Explicit counts preserve the requested
// chain length; signed vectors, cube maps and volumes require separate writers.
std::vector<uint8_t> encodeDds(const Image& image, uint32_t format,
                             unsigned mipLevels=0, bool cutoutAlpha=false);
// Resample to (w, h): 2x2 box when halving exactly, bilinear otherwise.
Image resample(const Image& img, int w, int h);
Image downsample(const Image& img);   // one mip step (box filter; odd dims nearest)

uint32_t formatByName(const std::string& name);   // "dxt1" | "dxt3" | "argb8888"/"argb"/"a8r8g8b8"; 0 = unknown
int fullMipCount(int w, int h, uint32_t format);
size_t mipRawLength(uint32_t format, int w, int h);

// Cutout mode maps alpha <128 to transparent; opaque blocks retain four-colour mode.
std::vector<uint8_t> encodeDxt1(const Image& img, bool cutoutAlpha = false);
std::vector<uint8_t> encodeDxt3(const Image& img);   // explicit 4-bit alpha (rounded)
std::vector<uint8_t> encodeDxt5(const Image& img);   // interpolated alpha, both endpoint modes
// Explicit signed-vector image contract: R=U+128, G=V+128, B=128, A=255.
// Reject other blue/alpha values instead of silently discarding channels.
std::vector<uint8_t> encodeSignedUv(const Image& img);
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
// Compressed allocations must be at least 4x4; explicit mip counts cannot
// continue beyond the shorter-side limit.
// cutoutAlpha selects DXT1 one-bit alpha and native transparency class 2.
Entry buildEntry(const Image& img, uint32_t format, int realW = 0, int realH = 0, bool compressMip0 = true, int mips = 0, bool cutoutAlpha = false);
Entry buildMipEntry(int width,int height,uint32_t format,const std::vector<std::vector<uint8_t>>& mips,
                    int realW,int realH,bool compressMip0,bool cutoutAlpha=false);

} // namespace forge::texturewrite
