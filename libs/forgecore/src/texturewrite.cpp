#include "forge/texturewrite.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <limits>
#include <stdexcept>

#include "forge/lzo.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_PIC
#define STBI_NO_PNM
#include "stb/stb_image.h"

namespace forge::texturewrite {
Image loadDds(FILE* input);

namespace {

const uint8_t kTailDXT1[6] = {0x03, 0x04, 0, 0, 0, 0};
const uint8_t kTailDXT3[6] = {0x02, 0x08, 0, 0, 0, 0};
const uint8_t kTailDXT5[6] = {0x05, 0x08, 0, 0, 0, 0};
const uint8_t kTailV8U8[6] = {0x04, 0x10, 8, 8, 0, 0};
const uint8_t kTailARGB[6] = {0x01, 0x20, 8, 8, 8, 8};

void put16(std::vector<uint8_t>& v, uint16_t x) { v.push_back(uint8_t(x)); v.push_back(uint8_t(x >> 8)); }
void put32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back(uint8_t(x >> (8 * i))); }

uint16_t pack565(int r, int g, int b) { return uint16_t(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); }
void unpack565(uint16_t c, int& r, int& g, int& b) {
    r = ((c >> 11) & 31) * 255 / 31; g = ((c >> 5) & 63) * 255 / 63; b = (c & 31) * 255 / 31;
}

// One 4x4 colour block (px = 16 RGB triples) -> c0, c1 (c0 > c1: four-colour
// mode) and the 2-bit index word. Endpoints from the block's principal axis,
// then one least-squares refinement of the endpoints given the indices (the
// same shape as texture_build.py's _encode_color_blocks, minus the numpy).
void encodeColorBlock(const int px[16][3], uint16_t& c0Out, uint16_t& c1Out, uint32_t& bitsOut) {
    float mean[3] = {0, 0, 0};
    for (int i = 0; i < 16; ++i) for (int k = 0; k < 3; ++k) mean[k] += float(px[i][k]);
    for (float& m : mean) m /= 16.0f;
    float cov[3][3] = {};
    for (int i = 0; i < 16; ++i)
        for (int a = 0; a < 3; ++a)
            for (int b = 0; b < 3; ++b) cov[a][b] += (float(px[i][a]) - mean[a]) * (float(px[i][b]) - mean[b]);
    float axis[3] = {1, 1, 1};
    for (int it = 0; it < 8; ++it) {
        float n[3] = {0, 0, 0};
        for (int a = 0; a < 3; ++a) for (int b = 0; b < 3; ++b) n[a] += cov[a][b] * axis[b];
        const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (len < 1e-6f) break;
        for (int a = 0; a < 3; ++a) axis[a] = n[a] / len;
    }
    float e0[3], e1[3];
    {
        float lo = 1e9f, hi = -1e9f; int loP = 0, hiP = 0;
        for (int i = 0; i < 16; ++i) {
            const float d = (float(px[i][0]) - mean[0]) * axis[0] + (float(px[i][1]) - mean[1]) * axis[1] + (float(px[i][2]) - mean[2]) * axis[2];
            if (d < lo) { lo = d; loP = i; }
            if (d > hi) { hi = d; hiP = i; }
        }
        for (int k = 0; k < 3; ++k) { e0[k] = float(px[hiP][k]); e1[k] = float(px[loP][k]); }
    }
    uint16_t bestC0 = 0, bestC1 = 0; uint32_t bestBits = 0; long long bestErr = -1;
    for (int it = 0; it < 3; ++it) {
        uint16_t c0 = pack565(std::clamp(int(std::lround(e0[0])), 0, 255), std::clamp(int(std::lround(e0[1])), 0, 255), std::clamp(int(std::lround(e0[2])), 0, 255));
        uint16_t c1 = pack565(std::clamp(int(std::lround(e1[0])), 0, 255), std::clamp(int(std::lround(e1[1])), 0, 255), std::clamp(int(std::lround(e1[2])), 0, 255));
        if (c0 < c1) std::swap(c0, c1);
        if (c0 == c1) { if (c1 > 0) --c1; else c0 = 1; }   // keep the four-colour mode on flat blocks
        int pal[4][3];
        unpack565(c0, pal[0][0], pal[0][1], pal[0][2]);
        unpack565(c1, pal[1][0], pal[1][1], pal[1][2]);
        for (int k = 0; k < 3; ++k) { pal[2][k] = (2 * pal[0][k] + pal[1][k]) / 3; pal[3][k] = (pal[0][k] + 2 * pal[1][k]) / 3; }
        uint32_t bits = 0; long long err = 0; int idx[16];
        for (int i = 0; i < 16; ++i) {
            int best = 0; long long bestD = 1LL << 40;
            for (int c = 0; c < 4; ++c) {
                const long long dr = px[i][0] - pal[c][0], dg = px[i][1] - pal[c][1], db = px[i][2] - pal[c][2];
                const long long d = dr * dr + dg * dg + db * db;
                if (d < bestD) { bestD = d; best = c; }
            }
            idx[i] = best; err += bestD;
            bits |= uint32_t(best) << (2 * i);
        }
        if (bestErr < 0 || err < bestErr) { bestErr = err; bestC0 = c0; bestC1 = c1; bestBits = bits; }
        if (it == 2) break;
        // least-squares endpoints for these indices (weights 1, 2/3, 1/3, 0 on e0)
        const float w0[4] = {1.0f, 0.0f, 2.0f / 3.0f, 1.0f / 3.0f};
        float a00 = 0, a01 = 0, a11 = 0, b0[3] = {0, 0, 0}, b1[3] = {0, 0, 0};
        for (int i = 0; i < 16; ++i) {
            const float wa = w0[idx[i]], wb = 1.0f - wa;
            a00 += wa * wa; a01 += wa * wb; a11 += wb * wb;
            for (int k = 0; k < 3; ++k) { b0[k] += wa * float(px[i][k]); b1[k] += wb * float(px[i][k]); }
        }
        const float det = a00 * a11 - a01 * a01;
        if (std::fabs(det) < 1e-4f) break;
        for (int k = 0; k < 3; ++k) {
            e0[k] = std::clamp((a11 * b0[k] - a01 * b1[k]) / det, 0.0f, 255.0f);
            e1[k] = std::clamp((a00 * b1[k] - a01 * b0[k]) / det, 0.0f, 255.0f);
        }
    }
    c0Out = bestC0; c1Out = bestC1; bitsOut = bestBits;
}

void blockPixels(const Image& img, int bx, int by, int px[16][3], int alpha[16]) {
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            const int sx = std::min(bx + x, img.width - 1), sy = std::min(by + y, img.height - 1);
            const uint8_t* p = &img.rgba[(size_t(sy) * size_t(img.width) + size_t(sx)) * 4];
            px[y * 4 + x][0] = p[0]; px[y * 4 + x][1] = p[1]; px[y * 4 + x][2] = p[2];
            alpha[y * 4 + x] = p[3];
        }
}

} // namespace

Image loadImage(const std::string& path, std::string& error) {
    Image out;
    int w = 0, h = 0, n = 0;
    // stb's UTF-8 filename support is MSVC-only. Open a native Windows path
    // ourselves so the MinGW build reads the same image as the folder picker.
#ifdef _WIN32
    FILE* opened = _wfopen(std::filesystem::path(path).c_str(), L"rb");
#else
    FILE* opened = std::fopen(path.c_str(), "rb");
#endif
    if (!opened) { error = "cannot read image " + path + ": cannot open file"; return out; }
    const std::unique_ptr<FILE, decltype(&std::fclose)> input(opened, &std::fclose);
    unsigned char magic[4]={};const auto read=std::fread(magic,1,4,input.get());std::rewind(input.get());
    if(read==4 && std::memcmp(magic,"DDS ",4)==0) {
        try {return loadDds(input.get());}
        catch(const std::exception& e) {error="cannot read image "+path+": "+e.what();return out;}
    }
    unsigned char* data = stbi_load_from_file(input.get(), &w, &h, &n, 4);
    if (!data) { error = std::string("cannot read image ") + path + ": " + (stbi_failure_reason() ? stbi_failure_reason() : "unknown"); return out; }
    out.width = w; out.height = h;
    out.rgba.assign(data, data + size_t(w) * size_t(h) * 4);
    stbi_image_free(data);
    return out;
}

Image downsample(const Image& img) {
    Image out;
    out.width = std::max(1, img.width >> 1); out.height = std::max(1, img.height >> 1);
    out.rgba.resize(size_t(out.width) * size_t(out.height) * 4);
    const bool even = (img.width == 1 || img.width % 2 == 0) &&
                      (img.height == 1 || img.height % 2 == 0);
    const int sampleWidth = std::min(2, img.width), sampleHeight = std::min(2, img.height);
    const int sampleCount = sampleWidth * sampleHeight;
    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x) {
            uint8_t* q = &out.rgba[(size_t(y) * size_t(out.width) + size_t(x)) * 4];
            if (even) {
                for (int k = 0; k < 4; ++k) {
                    int s = 0;
                    for (int dy = 0; dy < sampleHeight; ++dy)
                        for (int dx = 0; dx < sampleWidth; ++dx) s += img.rgba[((size_t(2 * y + dy) * size_t(img.width)) + size_t(2 * x + dx)) * 4 + size_t(k)];
                    q[k] = uint8_t((s + sampleCount / 2) / sampleCount);
                }
            } else {
                const int sx = std::min(2 * x, img.width - 1), sy = std::min(2 * y, img.height - 1);
                std::memcpy(q, &img.rgba[(size_t(sy) * size_t(img.width) + size_t(sx)) * 4], 4);
            }
        }
    return out;
}

Image resample(const Image& img, int w, int h) {
    if (img.width == w && img.height == h) return img;
    if (img.width == 2 * w && img.height == 2 * h) return downsample(img);
    Image out; out.width = w; out.height = h;
    out.rgba.resize(size_t(w) * size_t(h) * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            // bilinear on pixel centres
            const float fx = (float(x) + 0.5f) * float(img.width) / float(w) - 0.5f;
            const float fy = (float(y) + 0.5f) * float(img.height) / float(h) - 0.5f;
            const int x0 = std::clamp(int(std::floor(fx)), 0, img.width - 1), y0 = std::clamp(int(std::floor(fy)), 0, img.height - 1);
            const int x1 = std::min(x0 + 1, img.width - 1), y1 = std::min(y0 + 1, img.height - 1);
            const float tx = std::clamp(fx - float(x0), 0.0f, 1.0f), ty = std::clamp(fy - float(y0), 0.0f, 1.0f);
            uint8_t* q = &out.rgba[(size_t(y) * size_t(w) + size_t(x)) * 4];
            for (int k = 0; k < 4; ++k) {
                auto at = [&](int xx, int yy) { return float(img.rgba[(size_t(yy) * size_t(img.width) + size_t(xx)) * 4 + size_t(k)]); };
                const float v = (at(x0, y0) * (1 - tx) + at(x1, y0) * tx) * (1 - ty) + (at(x0, y1) * (1 - tx) + at(x1, y1) * tx) * ty;
                q[k] = uint8_t(std::clamp(int(std::lround(v)), 0, 255));
            }
        }
    return out;
}

uint32_t formatByName(const std::string& name) {
    std::string n; for (char c : name) n += char(std::tolower(static_cast<unsigned char>(c)));
    if (n == "dxt1") return kFormatDXT1;
    if (n == "dxt3") return kFormatDXT3;
    if (n == "argb8888" || n == "argb" || n == "a8r8g8b8") return kFormatARGB;
    return 0;
}

int fullMipCount(int w, int h, uint32_t format) {
    const int minDim = (format == kFormatARGB || format == kFormatV8U8) ? 1 : 4;
    int mips = 1;
    // Native CalcDefaultMipMapCount (030DA750) stops at the shorter side.
    // Keeping a one-pixel side while shrinking the other also breaks the
    // loader's quarter-size/shifted-dimension progression for raw formats.
    while (w > minDim && h > minDim) { ++mips; w >>= 1; h >>= 1; }
    return mips;
}

size_t mipRawLength(uint32_t format, int w, int h) {
    if (format == kFormatV8U8) return size_t(w) * size_t(h) * 2;
    if (format == kFormatARGB) return size_t(w) * size_t(h) * 4;
    const size_t blocks = size_t(std::max(1, (w + 3) / 4)) * size_t(std::max(1, (h + 3) / 4));
    return blocks * (format == kFormatDXT1 ? 8 : 16);
}

std::vector<uint8_t> encodeDxt1(const Image& img, bool cutoutAlpha) {
    std::vector<uint8_t> out;
    out.reserve(mipRawLength(kFormatDXT1, img.width, img.height));
    for (int by = 0; by < std::max(4, img.height); by += 4)
        for (int bx = 0; bx < std::max(4, img.width); bx += 4) {
            int px[16][3], alpha[16];
            blockPixels(img, bx, by, px, alpha);
            uint16_t c0, c1; uint32_t bits;
            const bool transparent = cutoutAlpha && std::any_of(alpha, alpha+16, [](int a) { return a < 128; });
            if (!transparent) {
                encodeColorBlock(px, c0, c1, bits);
            } else {
                const auto opaque = std::find_if(alpha, alpha+16, [](int a) { return a >= 128; });
                if (opaque == alpha+16) {
                    c0 = c1 = 0; bits = 0xffffffff;
                } else {
                    // Transparent texels' RGB must not pull the fitted endpoints
                    // towards an arbitrary background colour.
                    for (int i=0;i<16;++i)
                        if (alpha[i]<128) std::copy_n(px[opaque-alpha],3,px[i]);
                    encodeColorBlock(px,c0,c1,bits);
                    std::swap(c0,c1); // c0 <= c1 selects three colours plus transparent.
                    int palette[3][3];
                    unpack565(c0,palette[0][0],palette[0][1],palette[0][2]);
                    unpack565(c1,palette[1][0],palette[1][1],palette[1][2]);
                    for (int k=0;k<3;++k) palette[2][k]=(palette[0][k]+palette[1][k])/2;
                    bits=0;
                    for (int i=0;i<16;++i) {
                        int best=3, distance=std::numeric_limits<int>::max();
                        if (alpha[i]>=128) for (int colour=0;colour<3;++colour) {
                            int d=0;
                            for (int k=0;k<3;++k) { const int delta=px[i][k]-palette[colour][k]; d+=delta*delta; }
                            if (d<distance) { distance=d; best=colour; }
                        }
                        bits|=uint32_t(best)<<(2*i);
                    }
                }
            }
            put16(out, c0); put16(out, c1); put32(out, bits);
        }
    return out;
}

std::vector<uint8_t> encodeDxt3(const Image& img) {
    std::vector<uint8_t> out;
    out.reserve(mipRawLength(kFormatDXT3, img.width, img.height));
    for (int by = 0; by < std::max(4, img.height); by += 4)
        for (int bx = 0; bx < std::max(4, img.width); bx += 4) {
            int px[16][3], alpha[16];
            blockPixels(img, bx, by, px, alpha);
            uint64_t aword = 0;
            // BC2 decoding expands each nibble by 17, so round to that grid.
            for (int i = 0; i < 16; ++i) aword |= uint64_t((alpha[i] + 8) / 17) << (4 * i);
            for (int i = 0; i < 8; ++i) out.push_back(uint8_t(aword >> (8 * i)));
            uint16_t c0, c1; uint32_t bits;
            encodeColorBlock(px, c0, c1, bits);
            put16(out, c0); put16(out, c1); put32(out, bits);
        }
    return out;
}

// BC3 offers eight interpolated alpha values, or six plus exact 0/255.
// Compare both endpoint modes by squared error; colour stays four-colour BC1.
std::vector<uint8_t> encodeDxt5(const Image& img) {
    if (img.width <= 0 || img.height <= 0 ||
        uint64_t(img.width)*img.height*4 != img.rgba.size())
        throw std::invalid_argument("Invalid DXT5 image dimensions or pixel data");
    std::vector<uint8_t> out;
    out.reserve(mipRawLength(kFormatDXT5,img.width,img.height));
    for (int by=0;by<img.height;by+=4) for (int bx=0;bx<img.width;bx+=4) {
        int px[16][3], alpha[16];blockPixels(img,bx,by,px,alpha);
        const auto bounds=std::minmax_element(alpha,alpha+16);
        int innerLo=255,innerHi=0;
        for (int a:alpha) if (a!=0 && a!=255) {innerLo=std::min(innerLo,a);innerHi=std::max(innerHi,a);}
        if (innerLo>innerHi) innerLo=innerHi=0;
        uint32_t bestError=std::numeric_limits<uint32_t>::max();
        uint64_t bestBits=0;int best0=0,best1=0;
        auto consider=[&](int a0,int a1) {
            int palette[8]={a0,a1};
            if (a0>a1) for(int i=2;i<8;++i) palette[i]=((8-i)*a0+(i-1)*a1)/7;
            else {for(int i=2;i<6;++i) palette[i]=((6-i)*a0+(i-1)*a1)/5;palette[6]=0;palette[7]=255;}
            uint32_t error=0;uint64_t bits=0;
            for(int i=0;i<16;++i) {
                int nearest=0,distance=65536;
                for(int j=0;j<8;++j) {const int delta=alpha[i]-palette[j],sq=delta*delta;
                    if(sq<distance) {distance=sq;nearest=j;}}
                error+=uint32_t(distance);bits|=uint64_t(nearest)<<(3*i);
            }
            if(error<bestError) {bestError=error;bestBits=bits;best0=a0;best1=a1;}
        };
        consider(*bounds.second,*bounds.first);
        consider(*bounds.first,*bounds.second);
        consider(innerLo,innerHi);
        out.push_back(uint8_t(best0));out.push_back(uint8_t(best1));
        for(int i=0;i<6;++i) out.push_back(uint8_t(bestBits>>(8*i)));
        uint16_t c0,c1;uint32_t bits;encodeColorBlock(px,c0,c1,bits);
        put16(out,c0);put16(out,c1);put32(out,bits);
    }
    return out;
}

std::vector<uint8_t> encodeSignedUv(const Image& img) {
    if(img.width<=0 || img.height<=0 || uint64_t(img.width)*img.height*4!=img.rgba.size())
        throw std::invalid_argument("Invalid signed U/V image dimensions or pixel data");
    std::vector<uint8_t> out;out.reserve(size_t(img.width)*img.height*2);
    for(size_t at=0;at<img.rgba.size();at+=4) {
        if(img.rgba[at+2]!=128 || img.rgba[at+3]!=255)
            throw std::invalid_argument("Signed U/V images require blue=128 and alpha=255; red/green encode signed U/V plus 128");
        // XOR flips the sign bit: shifted unsigned [0,255] -> two's-complement
        // signed [-128,127], preserving every representable vector component.
        out.push_back(img.rgba[at]^128);out.push_back(img.rgba[at+1]^128);
    }
    return out;
}

std::vector<uint8_t> encodeArgb(const Image& img) {
    std::vector<uint8_t> out(img.rgba.size());
    for (size_t i = 0; i + 3 < img.rgba.size(); i += 4) {
        out[i] = img.rgba[i + 2]; out[i + 1] = img.rgba[i + 1]; out[i + 2] = img.rgba[i]; out[i + 3] = img.rgba[i + 3];
    }
    return out;
}

std::vector<uint8_t> encodeMip(uint32_t format, const Image& img) {
    if (format == kFormatDXT1) return encodeDxt1(img);
    if (format == kFormatDXT3) return encodeDxt3(img);
    if (format == kFormatDXT5) return encodeDxt5(img);
    if (format == kFormatARGB) return encodeArgb(img);
    if (format == kFormatV8U8) return encodeSignedUv(img);
    return {};
}

std::vector<uint8_t> compressFableBlock(const std::vector<uint8_t>& raw) {
    if (raw.size() <= 3) return raw;
    const std::vector<uint8_t> body(raw.begin(), raw.end() - 3);
    const auto comp = forge::lzo::compress(body);
    std::vector<uint8_t> out;
    const bool escape = raw.size() >= 0x10000 || comp.size() >= 0xFFFF;
    if (escape) { put16(out, 0xFFFF); put32(out, uint32_t(comp.size())); }
    else put16(out, uint16_t(comp.size()));
    out.insert(out.end(), comp.begin(), comp.end());
    out.insert(out.end(), raw.end() - 3, raw.end());
    return out;
}

Entry buildEntry(const Image& img, uint32_t format, int realW, int realH, bool compressMip0, int mips, bool cutoutAlpha) {
    if (cutoutAlpha && format != kFormatDXT1) throw std::invalid_argument("cutout encoding requires DXT1");
    if ((format == kFormatDXT1 || format == kFormatDXT3 || format == kFormatDXT5) && (img.width < 4 || img.height < 4))
        throw std::invalid_argument("compressed texture allocation must be at least 4x4");
    const int maximumMips = fullMipCount(img.width, img.height, format);
    if (mips <= 0) mips = maximumMips;
    if (mips > maximumMips) throw std::invalid_argument("mip count exceeds the supported texture dimensions");
    std::vector<Image> chain; chain.push_back(img);
    for (int i = 1; i < mips; ++i) chain.push_back(downsample(chain.back()));
    std::vector<std::vector<uint8_t>> encoded;
    for(const auto& mip:chain) encoded.push_back(cutoutAlpha?encodeDxt1(mip,true):encodeMip(format,mip));
    return buildMipEntry(img.width,img.height,format,encoded,realW,realH,compressMip0,cutoutAlpha);
}

Entry buildMipEntry(int width,int height,uint32_t format,const std::vector<std::vector<uint8_t>>& rawMips,
                    int realW,int realH,bool compressMip0,bool cutoutAlpha) {
    if(cutoutAlpha && format!=kFormatDXT1) throw std::invalid_argument("cutout encoding requires DXT1");
    if(width<=0 || height<=0 || width>65535 || height>65535 || rawMips.empty() ||
       (format!=kFormatARGB && format!=kFormatV8U8 && (width<4 || height<4)) ||
       rawMips.size()>size_t(fullMipCount(width,height,format)) ||
       (format!=kFormatARGB && format!=kFormatDXT1 && format!=kFormatDXT3 && format!=kFormatDXT5 && format!=kFormatV8U8))
        throw std::invalid_argument("Unsupported supplied mip chain");
    Entry e;const auto mips=int(rawMips.size());e.mips=mips;
    uint32_t frameDataSize = 0, mipSize0 = 0;
    for (int i = 0; i < mips; ++i) {
        const auto& raw=rawMips[size_t(i)];
        if(raw.size()!=mipRawLength(format,std::max(1,width>>i),std::max(1,height>>i)))
            throw std::invalid_argument("Supplied mip size does not match its dimensions");
        if (i == 0) {
            frameDataSize = uint32_t(raw.size());
            if (compressMip0 && raw.size() > 3) {
                const auto blk = compressFableBlock(raw);
                mipSize0 = uint32_t(blk.size());
                e.payload.insert(e.payload.end(), blk.begin(), blk.end());
            } else {
                mipSize0 = 0;   // engine rule: MipSize0 == 0 => mip 0 stored raw
                e.payload.insert(e.payload.end(), raw.begin(), raw.end());
            }
        } else e.payload.insert(e.payload.end(), raw.begin(), raw.end());
    }
    const uint8_t* tail = format == kFormatDXT1 ? kTailDXT1 : format == kFormatDXT3 ? kTailDXT3 : format == kFormatDXT5 ? kTailDXT5 : format == kFormatV8U8 ? kTailV8U8 : kTailARGB;
    const uint8_t transparency = cutoutAlpha ? 2 : (format == kFormatDXT1 || format == kFormatV8U8) ? 0 : format == kFormatDXT5 ? 3 : 1;
    auto& info = e.info;
    put16(info, uint16_t(width)); put16(info, uint16_t(height)); put16(info, 0);
    put16(info, uint16_t(realW > 0 ? realW : width)); put16(info, uint16_t(realH > 0 ? realH : height)); put16(info, 1);
    put32(info, format);
    info.push_back(transparency); info.push_back(uint8_t(mips)); info.push_back(0); info.push_back(0);
    put32(info, frameDataSize); put32(info, mipSize0);
    info.insert(info.end(), tail, tail + 6);
    return e;
}

} // namespace forge::texturewrite
