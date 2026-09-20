#include "forge/stbbake.hpp"

#include <climits>
#include <filesystem>

#include "forge/lzo.hpp"
#include "forge/rangecodec.hpp"


#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>

namespace forge::stbbake {

namespace {
void appendU16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(uint8_t(v)); out.push_back(uint8_t(v >> 8));
}
void appendU32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(uint8_t(v)); out.push_back(uint8_t(v >> 8));
    out.push_back(uint8_t(v >> 16)); out.push_back(uint8_t(v >> 24));
}
void appendF32(std::vector<uint8_t>& out, float v) {
    uint32_t bits = 0; std::memcpy(&bits, &v, sizeof(bits)); appendU32(out, bits);
}
} // namespace

std::vector<uint8_t> serializeGlobalTexturePalette(
    const std::vector<std::string>& names) {
    std::vector<uint8_t> out;
    out.push_back(0); // EBOOL: palette entries are global names, not inline data
    appendU32(out, static_cast<uint32_t>(names.size()));
    for (const auto& name : names) {
        if (name.find('\0') != std::string::npos)
            throw std::runtime_error("texture palette name contains NUL");
        out.insert(out.end(), name.begin(), name.end());
        out.push_back(0);
    }
    return out;
}

std::vector<uint8_t> serializeExternalTexturePalette() {
    return {1}; // EBOOL: mesh texture values are global bank entry IDs
}

std::vector<uint8_t> serializeEmptyLocalDetailPalette(bool external) {
    std::vector<uint8_t> out;
    out.push_back(external ? 1 : 0); // EBOOL
    appendU32(out, 0);              // no object collection types
    return out;
}

std::vector<uint8_t> serializeLandscapeRootDescriptor(
    const LandscapeRootDescriptor& d) {
    const int32_t fields[] = {
        d.texturePalettePos, d.texturePaletteSize, d.foregroundHeaderPos,
        d.backgroundRootPos, d.backgroundRootSize};
    for (int32_t value : fields)
        if (value < 0)
            throw std::runtime_error("negative landscape root descriptor field");
    std::vector<uint8_t> out;
    out.reserve(20);
    for (int32_t value : fields) appendU32(out, static_cast<uint32_t>(value));
    return out;
}

std::vector<uint8_t> serializeLocalDetailRootDescriptor(
    int32_t headerPos, int32_t headerSpan, bool present) {
    if (headerPos < 0 || headerSpan < 0)
        throw std::runtime_error("negative local-detail descriptor field");
    std::vector<uint8_t> out;
    appendU32(out, uint32_t(headerPos));
    appendU32(out, uint32_t(headerSpan));
    appendU32(out, present ? 1u : 0u);
    return out;
}

std::vector<uint8_t> serializeBackgroundTreeHeader(
    const BackgroundTreeHeader& h) {
    // Native SaveHeader asserts FirstBand >= LANDSCAPE_MIN_LOD_BAND (1).
    if (h.firstBand < 1 || h.firstBand > h.firstNonSplitBand || h.lastBand > 8)
        throw std::runtime_error("invalid background LOD band range");
    const size_t needed = h.firstNonSplitBand < 8
        ? size_t(h.lastBand - h.firstNonSplitBand) + 1 : 0;
    if (h.firstNonSplitBand < 8 && h.lastBand < h.firstNonSplitBand)
        throw std::runtime_error("invalid background non-split band range");
    if (h.lod.size() != needed)
        throw std::runtime_error("background LOD record count does not match band range");

    std::vector<uint8_t> out;
    if (h.width == 0 || h.height == 0)
        throw std::runtime_error("background tree node has empty map rectangle");
    appendU16(out, h.mapX); appendU16(out, h.mapY);
    appendU16(out, h.width); appendU16(out, h.height);
    out.push_back(h.firstBand); out.push_back(h.firstNonSplitBand);
    out.push_back(h.lastBand);
    appendU32(out, uint32_t(h.fileBlockPos));
    appendU32(out, uint32_t(h.fileBlockSize));
    appendU32(out, uint32_t(h.offsetIntoFileBlock));
    for (float v : h.aabb) appendF32(out, v);
    for (const auto& lod : h.lod) {
        out.push_back(lod.optimizedBandRemap);
        appendU32(out, uint32_t(lod.fileBlockPos));
        appendU32(out, uint32_t(lod.fileBlockSize));
        appendU32(out, uint32_t(lod.offsetIntoFileBlock));
    }
    return out;
}

BackgroundTreeHeader parseBackgroundTreeHeader(
    const std::vector<uint8_t>& bytes, size_t offset, size_t* consumed) {
    auto need = [&](size_t count) {
        if (offset + count > bytes.size())
            throw std::runtime_error("truncated background tree header");
    };
    auto read16 = [&](size_t at) -> uint16_t {
        return uint16_t(bytes[at]) | uint16_t(bytes[at + 1]) << 8;
    };
    auto read32 = [&](size_t at) -> uint32_t {
        return uint32_t(bytes[at]) | uint32_t(bytes[at + 1]) << 8 |
               uint32_t(bytes[at + 2]) << 16 | uint32_t(bytes[at + 3]) << 24;
    };
    need(47);
    const size_t start = offset;
    BackgroundTreeHeader h;
    h.mapX = read16(offset); h.mapY = read16(offset + 2);
    h.width = read16(offset + 4); h.height = read16(offset + 6);
    h.firstBand = bytes[offset + 8];
    h.firstNonSplitBand = bytes[offset + 9];
    h.lastBand = bytes[offset + 10];
    h.fileBlockPos = int32_t(read32(offset + 11));
    h.fileBlockSize = int32_t(read32(offset + 15));
    h.offsetIntoFileBlock = int32_t(read32(offset + 19));
    for (int i = 0; i < 6; ++i) {
        const uint32_t bits = read32(offset + 23 + size_t(i) * 4);
        std::memcpy(&h.aabb[i], &bits, 4);
    }
    offset += 47;
    if (h.width == 0 || h.height == 0 || h.firstBand < 1 ||
        h.firstBand > h.firstNonSplitBand || h.lastBand > 8 ||
        (h.firstNonSplitBand < 8 && h.lastBand < h.firstNonSplitBand))
        throw std::runtime_error("invalid background tree header bands");
    const size_t count = h.firstNonSplitBand < 8
        ? size_t(h.lastBand - h.firstNonSplitBand) + 1 : 0;
    need(count * 13);
    h.lod.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        BackgroundLodRecord lod;
        lod.optimizedBandRemap = bytes[offset];
        lod.fileBlockPos = int32_t(read32(offset + 1));
        lod.fileBlockSize = int32_t(read32(offset + 5));
        lod.offsetIntoFileBlock = int32_t(read32(offset + 9));
        h.lod.push_back(lod);
        offset += 13;
    }
    if (consumed) *consumed = offset - start;
    return h;
}

BackgroundTreeNode parseBackgroundTree(
    const std::vector<uint8_t>& chunk, size_t rootHeaderOffset) {
    std::set<size_t> active;
    std::set<size_t> seen;
    auto parseNode = [&](auto&& self, size_t headerOffset) -> BackgroundTreeNode {
        if (headerOffset >= chunk.size())
            throw std::runtime_error("background tree header offset is out of range");
        if (!active.insert(headerOffset).second)
            throw std::runtime_error("background tree contains a pointer cycle");
        if (!seen.insert(headerOffset).second)
            throw std::runtime_error("background tree reuses a child header");

        BackgroundTreeNode node;
        node.headerOffset = headerOffset;
        node.header = parseBackgroundTreeHeader(chunk, headerOffset);
        const auto& h = node.header;
        const uint32_t right = uint32_t(h.mapX) + uint32_t(h.width);
        const uint32_t bottom = uint32_t(h.mapY) + uint32_t(h.height);
        if (right > 0x10000u || bottom > 0x10000u)
            throw std::runtime_error("background tree map rectangle overflows UWORD space");

        // Most nodes expose their split through FirstBand <
        // FirstNonSplitBand, but shipped maps larger than 128 cells use an
        // (8,8,8) zero-LOD wrapper whose live child pointer is the only split
        // indicator. File-block ownership is authoritative for both forms.
        const bool hasChildren = h.fileBlockPos != 0 || h.fileBlockSize != 0 ||
                                 h.offsetIntoFileBlock != 0;
        if (hasChildren) {
            if (h.fileBlockPos <= 0 || h.fileBlockSize <= 0 ||
                h.offsetIntoFileBlock < 0 ||
                h.offsetIntoFileBlock >= h.fileBlockSize)
                throw std::runtime_error("split background node has invalid file-block reference");
            const int64_t blockEnd = int64_t(h.fileBlockPos) + h.fileBlockSize;
            const int64_t child0Offset = int64_t(h.fileBlockPos) + h.offsetIntoFileBlock;
            if (child0Offset < 0 || child0Offset >= blockEnd ||
                blockEnd > int64_t(chunk.size()))
                throw std::runtime_error("background child-header pair is out of range");
            size_t child0Size = 0;
            (void)parseBackgroundTreeHeader(
                chunk, static_cast<size_t>(child0Offset), &child0Size);
            const size_t child1Offset = static_cast<size_t>(child0Offset) + child0Size;
            if (int64_t(child1Offset) >= blockEnd)
                throw std::runtime_error("background second child header is out of range");
            node.children.push_back(self(self, static_cast<size_t>(child0Offset)));
            node.children.push_back(self(self, child1Offset));
            const auto& a = node.children[0].header;
            const auto& b = node.children[1].header;
            const bool splitX =
                a.mapX == h.mapX && a.mapY == h.mapY &&
                b.mapY == h.mapY && a.height == h.height && b.height == h.height &&
                uint32_t(a.mapX) + a.width == b.mapX &&
                uint32_t(b.mapX) + b.width == right;
            const bool splitY =
                a.mapX == h.mapX && a.mapY == h.mapY &&
                b.mapX == h.mapX && a.width == h.width && b.width == h.width &&
                uint32_t(a.mapY) + a.height == b.mapY &&
                uint32_t(b.mapY) + b.height == bottom;
            if (!splitX && !splitY)
                throw std::runtime_error(
                    "background child rectangles do not partition their parent");
        }
        active.erase(headerOffset);
        return node;
    };
    return parseNode(parseNode, rootHeaderOffset);
}

// Fixed tree shape for any map whose sides are multiples of 16 (the retail
// sizes run 32x32 .. 160x256). Rules read off every retail chunk
// (backgroundtreeinfo over 397 maps, 2026-09-16):
//  * a rectangle with a non-power-of-two side carries no LOD payload
//    (bands 8,8,8) and splits off the largest power of two below that side
//    (96 -> 64+32, 160 -> 128+32, 224 -> 128+96 -> 128+64+32);
//  * a power-of-two rectangle carries payload for bands firstNonSplit..last
//    and halves its longer side until the 16x16 leaves; sides of 8192 cells
//    and more only carry the coarsest band (retail 64x128/128x128: 1,7,7);
//  * the 32/64 band tables below are the ones the engine already accepted
//    in-game (ForgeTest64) and stay as they were.
BackgroundTreeNode buildBackgroundTreeShape(
    const terrain::Heightfield& hf, int worldX, int worldY) {
    const int mapWidth = hf.width(), mapHeight = hf.height();
    if (mapWidth < 16 || mapHeight < 16 || mapWidth % 16 != 0 || mapHeight % 16 != 0)
        throw std::invalid_argument("background tree shape requires sides that are multiples of 16");
    if (worldX < 0 || worldY < 0 || worldX + mapWidth > 0xffff ||
        worldY + mapHeight > 0xffff)
        throw std::invalid_argument("background tree world rectangle exceeds UWORD space");
    auto isPow2 = [](int v) { return v > 0 && (v & (v - 1)) == 0; };
    auto powBelow = [](int v) { int p = 1; while (p <= v / 2) p <<= 1; return p; };

    auto build = [&](auto&& self, int x, int y, int width, int height,
                     bool splitX, bool root) -> BackgroundTreeNode {
        BackgroundTreeNode node;
        auto& h = node.header;
        h.mapX = static_cast<uint16_t>(x);
        h.mapY = static_cast<uint16_t>(y);
        h.width = static_cast<uint16_t>(width);
        h.height = static_cast<uint16_t>(height);
        float minZ = std::numeric_limits<float>::max();
        float maxZ = -std::numeric_limits<float>::max();
        for (int py = y; py <= y + height; ++py)
            for (int px = x; px <= x + width; ++px) {
                minZ = std::min(minZ, hf.at(px, py));
                maxZ = std::max(maxZ, hf.at(px, py));
            }
        h.aabb[0] = float(worldX + x); h.aabb[1] = float(worldY + y); h.aabb[2] = minZ;
        h.aabb[3] = float(worldX + x + width);
        h.aabb[4] = float(worldY + y + height); h.aabb[5] = maxZ;
        const bool nonPowX = !isPow2(width), nonPowY = !isPow2(height);
        if (nonPowX || nonPowY) {
            // no payload; split the largest power of two off the non-power side
            h.firstBand = h.firstNonSplitBand = h.lastBand = 8;
            const bool cutX = nonPowX && (!nonPowY || width >= height);
            if (cutX) {
                const int cut = powBelow(width);
                node.children.push_back(self(self, x, y, cut, height, false, false));
                node.children.push_back(self(self, x + cut, y, width - cut, height, false, false));
            } else {
                const int cut = powBelow(height);
                node.children.push_back(self(self, x, y, width, cut, true, false));
                node.children.push_back(self(self, x, y + cut, width, height - cut, true, false));
            }
            return node;
        }
        h.firstBand = 1;
        const int area = width * height;
        if (area >= 8192) {
            h.firstNonSplitBand = 7; h.lastBand = 7;
        } else if (root) {
            h.firstNonSplitBand = area == 4096 ? 5 : area >= 2048 ? 4 : 3;
            h.lastBand = 7;
        } else if (area == 256) {
            h.firstNonSplitBand = 1; h.lastBand = 3;
        } else if (area <= 1024) {
            h.firstNonSplitBand = 3;
            h.lastBand = area == 512 ? 3 : 4;
        } else if (area == 2048) {
            h.firstNonSplitBand = 4; h.lastBand = 5;
        } else {   // 4096
            h.firstNonSplitBand = 5; h.lastBand = 7;
        }
        for (unsigned band = h.firstNonSplitBand; band <= h.lastBand; ++band) {
            BackgroundLodRecord lod;
            lod.optimizedBandRemap = static_cast<uint8_t>(band);
            h.lod.push_back(lod);
        }
        if (width > 16 || height > 16) {
            // halve the longer side (a square alternates, starting as the caller asked)
            const bool doX = width > height ? true : height > width ? false : splitX;
            if (doX) {
                const int half = width / 2;
                node.children.push_back(self(self, x, y, half, height, false, false));
                node.children.push_back(self(self, x + half, y, width - half, height, false, false));
            } else {
                const int half = height / 2;
                node.children.push_back(self(self, x, y, width, half, true, false));
                node.children.push_back(self(self, x, y + half, width, height - half, true, false));
            }
        }
        return node;
    };
    return build(build, 0, 0, mapWidth, mapHeight,
                 mapWidth >= mapHeight, true);
}

BackgroundTreeNode buildBackgroundTreeShape64(
    const terrain::Heightfield& hf, int worldX, int worldY) {
    if (hf.width() != 64 || hf.height() != 64)
        throw std::invalid_argument("background tree shape requires a 64x64 heightfield");
    return buildBackgroundTreeShape(hf, worldX, worldY);
}

namespace {

bool buildNativeLodLock(float errorScale, int maxExtent,
                        const std::vector<float>& values, int start, int length,
                        float offset, float availableRange, int minLockRange,
                        int& outLockRange) {
    if (length < minLockRange || length < 2 || (length & 1) != 0)
        throw std::invalid_argument("invalid native LOD lock range");
    const int half = length / 2;
    float firstPeak = 0.0f, secondPeak = 0.0f;
    for (int i = start; i < start + half; ++i)
        firstPeak = std::max(firstPeak, values.at(size_t(i)));
    for (int i = start + half; i < start + length; ++i)
        secondPeak = std::max(secondPeak, values.at(size_t(i)));
    firstPeak *= (float(length) * errorScale) / 2.0f;
    secondPeak *= (float(length) * errorScale) / 2.0f;
    const float peak = 2.0f * std::max(firstPeak, secondPeak);
    if (float(maxExtent) * availableRange > peak) {
        outLockRange = length;
        return true;
    }
    if (length == minLockRange) return false;

    auto groupedCost = [&](int begin) {
        float total = 0.0f;
        for (int group = begin; group < begin + half; group += minLockRange) {
            float maximum = 0.0f;
            for (int i = group; i < group + minLockRange; ++i)
                maximum = std::max(maximum, values.at(size_t(i)));
            total += float(minLockRange) * maximum;
        }
        return total * errorScale;
    };
    const float firstCost = groupedCost(start);
    const float secondCost = groupedCost(start + half);
    const int truncatedTotal = static_cast<int>(firstCost + secondCost);
    if (float(truncatedTotal) > float(maxExtent) * availableRange) return false;

    const float halfCapacity = float(maxExtent) * availableRange / 2.0f;
    if (halfCapacity <= firstPeak && secondPeak < halfCapacity) {
        const float secondRange = secondPeak / float(maxExtent);
        const float firstRange = availableRange - secondRange;
        if (firstCost < float(maxExtent) * firstRange) {
            int childRange = 0;
            if (buildNativeLodLock(errorScale, maxExtent, values, start, half,
                                   offset, firstRange, minLockRange, childRange)) {
                outLockRange = childRange;
                return true;
            }
        }
    } else if (firstPeak < halfCapacity) {
        const float firstRange = firstPeak / float(maxExtent);
        const float secondRange = availableRange - firstRange;
        if (secondCost < float(maxExtent) * secondRange) {
            int childRange = 0;
            if (buildNativeLodLock(errorScale, maxExtent, values, start + half,
                                   half, offset + firstRange, secondRange,
                                   minLockRange, childRange)) {
                outLockRange = childRange;
                return true;
            }
        }
    }

    const float firstUnits = firstCost / float(maxExtent);
    const float secondUnits = secondCost / float(maxExtent);
    const float firstRange = ((firstUnits + availableRange) - secondUnits) / 2.0f;
    const float secondRange = availableRange - firstRange;
    int firstLock = 0, secondLock = 0;
    if (!buildNativeLodLock(errorScale, maxExtent, values, start, half,
                            offset, firstRange, minLockRange, firstLock) ||
        !buildNativeLodLock(errorScale, maxExtent, values, start + half, half,
                            offset + firstRange, secondRange, minLockRange,
                            secondLock))
        return false;
    outLockRange = std::min(firstLock, secondLock);
    return true;
}

} // namespace

BackgroundTreeNode buildNativeAdaptiveBackgroundTreeShape(
    const terrain::Heightfield& hf, int worldX, int worldY,
    NativeBackgroundLodSettings settings, HeightSampler sampleHeight) {
    const int mapWidth = hf.width(), mapHeight = hf.height();
    if (mapWidth < 1 || mapHeight < 1 || settings.meshDetail <= 0.0f ||
        settings.firstLodZ <= 0.0f || settings.staticMapQuality < 0 ||
        settings.staticMapQuality > 8)
        throw std::invalid_argument("invalid adaptive background-tree input");
    if (worldX < 0 || worldY < 0 || worldX + mapWidth > 0xffff ||
        worldY + mapHeight > 0xffff)
        throw std::invalid_argument("adaptive background world rectangle exceeds UWORD space");

    std::vector<float> horizontal(size_t(mapWidth) * mapHeight);
    std::vector<float> vertical(size_t(mapWidth) * mapHeight);
    if (!sampleHeight) sampleHeight = clampedHeightSampler(hf);
    auto height = [&](int x, int y) { return sampleHeight(x, y); };
    auto edgeLength = [&](int ax, int ay, int bx, int by) {
        const float dz = height(ax, ay) - height(bx, by);
        return float(std::sqrt(static_cast<long double>(1.0f + dz * dz)));
    };
    for (int y = 0; y < mapHeight; ++y) {
        for (int x = 0; x < mapWidth; ++x) {
            const size_t at = size_t(y) * mapWidth + x;
            horizontal[at] = std::max(edgeLength(x,y,x+1,y),
                                      edgeLength(x,y+1,x+1,y+1));
            vertical[at] = std::max(edgeLength(x,y,x,y+1),
                                    edgeLength(x+1,y,x+1,y+1));
        }
    }
    float topologyDetail = settings.proceduralTextureDetail;
    for (int quality = settings.staticMapQuality; quality < 8; ++quality)
        topologyDetail *= 2.0f;
    auto lodZ = [&](int band) {
        if (band < 2) return 0.0f;
        return settings.firstLodZ * std::ldexp(1.0f, band - 2);
    };
    auto fillBounds = [&](BackgroundTreeHeader& h) {
        float minZ = std::numeric_limits<float>::max();
        float maxZ = -std::numeric_limits<float>::max();
        for (int py = h.mapY; py <= int(h.mapY + h.height); ++py)
            for (int px = h.mapX; px <= int(h.mapX + h.width); ++px) {
                minZ = std::min(minZ, hf.at(px, py));
                maxZ = std::max(maxZ, hf.at(px, py));
            }
        h.aabb[0] = float(worldX + h.mapX);
        h.aabb[1] = float(worldY + h.mapY);
        h.aabb[2] = minZ;
        h.aabb[3] = float(worldX + h.mapX + h.width);
        h.aabb[4] = float(worldY + h.mapY + h.height);
        h.aabb[5] = maxZ;
    };
    auto isPowerOfTwo = [](int value) {
        return value > 0 && (value & (value - 1)) == 0;
    };
    auto highestPowerBelow = [](int value) {
        int power = 1;
        while (power <= value / 2) power <<= 1;
        return power;
    };

    std::function<BackgroundTreeNode(int,int,int,int,int)> build;
    build = [&](int x, int y, int width, int heightCells,
                int maxBand) -> BackgroundTreeNode {
        BackgroundTreeNode node;
        auto& header = node.header;
        header.mapX = uint16_t(x); header.mapY = uint16_t(y);
        header.width = uint16_t(width); header.height = uint16_t(heightCells);
        fillBounds(header);

        const bool nonPowerX = !isPowerOfTwo(width);
        const bool nonPowerY = !isPowerOfTwo(heightCells);
        if (nonPowerX || nonPowerY) {
            header.firstBand = header.firstNonSplitBand = header.lastBand = 8;
            const bool splitX = nonPowerX &&
                (!nonPowerY || width >= heightCells);
            if (splitX) {
                const int cut = highestPowerBelow(width);
                node.children.push_back(build(x, y, cut, heightCells, maxBand));
                node.children.push_back(build(x + cut, y, width - cut,
                                              heightCells, maxBand));
            } else {
                const int cut = highestPowerBelow(heightCells);
                node.children.push_back(build(x, y, width, cut, maxBand));
                node.children.push_back(build(x, y + cut, width,
                                              heightCells - cut, maxBand));
            }
            return node;
        }

        std::vector<float> hValues(size_t(width), 0.0f);
        std::vector<float> vValues(size_t(heightCells), 0.0f);
        for (int py = y; py < y + heightCells; ++py)
            for (int px = x; px < x + width; ++px) {
                const size_t at = size_t(py) * mapWidth + px;
                hValues[size_t(px - x)] =
                    std::max(hValues[size_t(px - x)], horizontal[at]);
                vValues[size_t(py - y)] =
                    std::max(vValues[size_t(py - y)], vertical[at]);
            }

        header.firstBand = 1;
        header.lastBand = uint8_t(std::min(maxBand, 7));
        header.firstNonSplitBand = 8;
        int extentX = 32, extentY = 32;
        bool horizontalOk = false, verticalOk = false;
        int failedBand = 0;
        float decisionErrorScale = 0.0f;
        for (int band = header.lastBand; band >= header.firstBand; --band) {
            horizontalOk = verticalOk = false;
            if (band == 1 && (width > 16 || heightCells > 16)) {
                failedBand = band;
                break;
            }
            const float distance = std::max(settings.foregroundFade, lodZ(band));
            const float errorScale = 800.0f / (distance * 2.0f * topologyDetail);
            decisionErrorScale = errorScale;
            while (true) {
                const int minimum = std::clamp(
                    std::max(width, heightCells) * 16 / extentX, 1, width);
                int lockRange = width;
                horizontalOk = buildNativeLodLock(
                    errorScale, extentX, hValues, 0, width, 0.0f, 1.0f,
                    minimum, lockRange);
                if (horizontalOk || extentX == 128) break;
                extentX *= 2;
            }
            while (true) {
                const int minimum = std::clamp(
                    std::max(width, heightCells) * 16 / extentY, 1, heightCells);
                int lockRange = heightCells;
                verticalOk = buildNativeLodLock(
                    errorScale, extentY, vValues, 0, heightCells, 0.0f, 1.0f,
                    minimum, lockRange);
                if (verticalOk || extentY == 128) break;
                extentY *= 2;
            }
            if (!horizontalOk || !verticalOk) {
                failedBand = band;
                break;
            }
            header.firstNonSplitBand = uint8_t(band);
        }
        if (failedBand == 0) failedBand = header.firstBand - 1;
        if (header.firstNonSplitBand <= header.lastBand) {
            for (int band = header.firstNonSplitBand; band <= header.lastBand; ++band) {
                BackgroundLodRecord lod;
                lod.optimizedBandRemap = uint8_t(band);
                header.lod.push_back(lod);
            }
        }
        if (failedBand < header.firstBand) return node;

        bool splitX = !horizontalOk && width > 2;
        bool splitY = !verticalOk && heightCells > 2;
        if (width < 16 && heightCells > 16) { splitX = false; splitY = true; }
        else if (heightCells < 16 && width > 16) { splitX = true; splitY = false; }
        else if (splitX && splitY) {
            const int common = std::max(
                std::max(width, heightCells) * 16 / extentX,
                std::max(width, heightCells) * 16 / extentY);
            const int strideX = std::clamp(common, 1, width);
            const int strideY = std::clamp(common, 1, heightCells);
            float xCost = 0.0f, yCost = 0.0f;
            for (int i = 0; i < width; i += strideX) {
                float peak = 0.0f;
                for (int j = i; j < std::min(i + strideX, width); ++j)
                    peak = std::max(peak, hValues[size_t(j)]);
                xCost += decisionErrorScale * peak * float(strideX);
            }
            for (int i = 0; i < heightCells; i += strideY) {
                float peak = 0.0f;
                for (int j = i; j < std::min(i + strideY, heightCells); ++j)
                    peak = std::max(peak, vValues[size_t(j)]);
                yCost += decisionErrorScale * peak * float(strideY);
            }
            if (xCost <= yCost) splitX = false;
            else splitY = false;
        }
        const int childMaxBand = header.firstNonSplitBand;
        if (splitX) {
            const int half = width / 2;
            node.children.push_back(build(x, y, half, heightCells, childMaxBand));
            node.children.push_back(build(x + half, y, half, heightCells,
                                          childMaxBand));
        } else if (splitY) {
            const int half = heightCells / 2;
            node.children.push_back(build(x, y, width, half, childMaxBand));
            node.children.push_back(build(x, y + half, width, half,
                                          childMaxBand));
        }
        return node;
    };
    return build(0, 0, mapWidth, mapHeight, 7);
}

BackgroundTreeLayout layoutBackgroundTree(
    BackgroundTreeNode root, const terrain::Heightfield& hf,
    int worldX, int worldY, const InlineTexture& texture,
    size_t startOffset, size_t alignment,
    const BackgroundTextureProvider& provider) {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0)
        throw std::invalid_argument("background tree alignment must be a power of two");
    if (root.header.width != hf.width() || root.header.height != hf.height())
        throw std::invalid_argument("background tree layout dimensions differ from heightfield");

    BackgroundTreeLayout out;
    out.startOffset = startOffset;
    out.rootHeaderOffset = (startOffset + alignment - 1) & ~(alignment - 1);
    if (out.rootHeaderOffset > size_t(INT32_MAX))
        throw std::runtime_error("background root offset exceeds s32");

    auto headerSize = [](const BackgroundTreeNode& node) {
        return size_t(47) + node.header.lod.size() * 13;
    };
    size_t cursor = headerSize(root);
    auto assignTreeOffsets = [&](auto&& self, BackgroundTreeNode& node) -> void {
        if (node.children.empty()) {
            node.header.fileBlockPos = 0;
            node.header.fileBlockSize = 0;
            node.header.offsetIntoFileBlock = 0;
            return;
        }
        if (node.children.size() != 2)
            throw std::runtime_error("background authored node is not binary");
        self(self, node.children[0]);
        self(self, node.children[1]);
        node.header.fileBlockPos = static_cast<int32_t>(out.rootHeaderOffset);
        node.header.offsetIntoFileBlock = static_cast<int32_t>(cursor);
        cursor += headerSize(node.children[0]) + headerSize(node.children[1]);
    };
    assignTreeOffsets(assignTreeOffsets, root);
    out.treeBlockSpan = cursor;
    if (out.rootHeaderOffset + out.treeBlockSpan > size_t(INT32_MAX))
        throw std::runtime_error("background tree block exceeds s32");
    auto assignTreeSpan = [&](auto&& self, BackgroundTreeNode& node) -> void {
        if (!node.children.empty())
            node.header.fileBlockSize = static_cast<int32_t>(out.treeBlockSpan);
        for (auto& child : node.children) self(self, child);
    };
    assignTreeSpan(assignTreeSpan, root);

    struct PendingPayload {
        BackgroundLodRecord* lod = nullptr;
        std::vector<uint8_t> frame;
        size_t offset = 0;
    };
    std::vector<PendingPayload> payloads;
    auto buildPayloads = [&](auto&& self, BackgroundTreeNode& node) -> void {
        if (node.header.lod.empty()) {
            // a non-power-of-two rectangle (bands 8,8,8): structure only, like retail
            for (auto& child : node.children) self(self, child);
            return;
        }
        InlineTexture nodeTexture;
        if (provider)
            nodeTexture = provider(node.header.mapX, node.header.mapY,
                                   node.header.width, node.header.height, 0, 0);
        const auto body = buildBackgroundPatchRect(
            hf, node.header.mapX, node.header.mapY,
            node.header.width, node.header.height,
            worldX, worldY, nodeTexture.width ? nodeTexture : texture);
        const auto frame = forge::lzo::compressFramed(assemblePatchBody(body));
        for (size_t i = 0; i < node.header.lod.size(); ++i) {
            auto& lod = node.header.lod[i];
            lod.fileBlockPos = lod.fileBlockSize = lod.offsetIntoFileBlock = 0;
            // SavePatchesToTemporyStream emits a new frame only when vertex
            // count or either dynamic-template extent differs from the last
            // saved band. This authored path currently has one literal body
            // for every band, so all later records satisfy the proven remap
            // condition and retain a null file tuple.
            if (i == 0) {
                lod.optimizedBandRemap = node.header.firstNonSplitBand;
                payloads.push_back({&lod, frame, 0});
            } else {
                lod.optimizedBandRemap = node.header.lod[i - 1].optimizedBandRemap;
            }
        }
        for (auto& child : node.children) self(self, child);
    };
    buildPayloads(buildPayloads, root);

    size_t payloadCursor = (out.rootHeaderOffset + out.treeBlockSpan + alignment - 1) &
                           ~(alignment - 1);
    for (auto& payload : payloads) {
        payload.offset = payloadCursor;
        if (payload.offset > size_t(INT32_MAX) ||
            payload.frame.size() > size_t(INT32_MAX))
            throw std::runtime_error("background payload tuple exceeds s32");
        payload.lod->fileBlockPos = static_cast<int32_t>(payload.offset);
        payload.lod->fileBlockSize = static_cast<int32_t>(payload.frame.size());
        payloadCursor = (payloadCursor + payload.frame.size() + alignment - 1) &
                        ~(alignment - 1);
    }

    out.bytes.resize(out.rootHeaderOffset - startOffset, 0);
    const auto rootBytes = serializeBackgroundTreeHeader(root.header);
    out.bytes.insert(out.bytes.end(), rootBytes.begin(), rootBytes.end());
    auto emitChildHeaders = [&](auto&& self, const BackgroundTreeNode& node) -> void {
        if (node.children.empty()) return;
        self(self, node.children[0]);
        self(self, node.children[1]);
        for (const auto& child : node.children) {
            const auto bytes = serializeBackgroundTreeHeader(child.header);
            out.bytes.insert(out.bytes.end(), bytes.begin(), bytes.end());
        }
    };
    emitChildHeaders(emitChildHeaders, root);
    if (out.bytes.size() != out.rootHeaderOffset - startOffset + out.treeBlockSpan)
        throw std::runtime_error("background tree serialization span mismatch");
    for (const auto& payload : payloads) {
        const size_t wanted = payload.offset - startOffset;
        if (out.bytes.size() > wanted)
            throw std::runtime_error("background payload layout overlaps prior data");
        out.bytes.resize(wanted, 0);
        out.bytes.insert(out.bytes.end(), payload.frame.begin(), payload.frame.end());
    }
    out.root = std::move(root);
    out.payloadCount = payloads.size();
    return out;
}

std::vector<uint8_t> serializeLocalDetailQuadHeader(
    const LocalDetailQuadHeader& h) {
    std::vector<uint8_t> out;
    for (float v : h.sphere) appendF32(out, v);
    appendF32(out, h.maxFade);
    appendU32(out, h.primitiveMask);
    appendU32(out, uint32_t(h.fileBlockPos));
    appendU32(out, uint32_t(h.fileBlockSize));
    appendU32(out, uint32_t(h.offsetIntoFileBlock));
    for (uint16_t v : h.cellBounds) appendU16(out, v);
    return out;
}

// --- CLocalDetailPrimitiveRepeatedMesh::BuildSubSectionsAndObjectRemapTable --
// Port of FableWin 0x02EDF740 (public driver) and 0x02EDFB20 (private recursive
// worker). Every branch below is transcribed from that disassembly; the VA in a
// comment is the instruction the line came from.
namespace {

// x87 `fistp` with the default control word (round-half-to-even). The engine's
// two rounding helpers both use the *current* mode and nothing on this path
// sets it, so nearest-even is the working assumption -- see the UNRECOVERED
// note about the FPU control word in docs/FOLIAGE_LOCAL_DETAIL_RE.md.
// A non-finite or out-of-range operand stores the x87 integer indefinite,
// which is INT32_MIN; both bucket indices are clamped immediately afterwards,
// so a degenerate (zero-extent) bounding box lands every object in column 0
// exactly as the release build does.
int32_t x87Fistp(double x) {
    if (!(x >= -2147483648.0 && x <= 2147483647.0)) return INT32_MIN;
    const double r = std::nearbyint(x);
    if (!(r >= -2147483648.0 && r <= 2147483647.0)) return INT32_MIN;
    return int32_t(r);
}

// 0x018C4020: { fld [x]; fistp [n]; }
int32_t roundToInt(float x) { return x87Fistp(double(x)); }

// 0x01CDAB50: n = fistp(x + 0.5f); t = (float)(n - 1);
//             if (bits(x) == bits(t)) --n; return n;
int32_t ceilToInt(float x) {
    int32_t n = x87Fistp(double(x) + 0.5);
    const float t = float(n - 1);
    uint32_t bx = 0, bt = 0;
    std::memcpy(&bx, &x, 4);
    std::memcpy(&bt, &t, 4);
    if (bx == bt) --n;
    return n;
}

// 0x01D9FAF0 -> 0x0183F833: widen to double, CRT sqrt, narrow back to float.
float engineSqrt(float x) { return float(std::sqrt(double(x))); }

// 0x01B52910: sum of squares accumulated on the x87 stack, stored to a float
// temp, then engineSqrt.
float magnitude(float dx, float dy, float dz) {
    const float sum = float(double(dx) * dx + double(dy) * dy + double(dz) * dz);
    return engineSqrt(sum);
}

constexpr float kPlusHuge = 1e30f;   // [0x0434BB8C]
constexpr float kMinusHuge = -1e30f; // [0x0434BB88]
constexpr size_t kSubsectionScratch = 32; // CSubsectionElement scratch[32]

// 0x02EDFB20. `elements` is the bump array: the cursor is elements.size().
bool buildSubsectionsWorker(std::vector<SubsectionElement>& elements,
                            long first, long count, long leafThreshold,
                            const std::vector<SubsectionSphere>& spheres,
                            std::vector<uint8_t>& remap) {
    if (!(count > leafThreshold)) return false;              // 0x02EDFB4B

    int N = ceilToInt(engineSqrt(float(count)));             // 0x02EDFB57..0x02EDFB6C
    if (N < 1) N = 1;                                        // 0x02EDFB7B
    else if (N > 8) N = 8;                                   // 0x02EDFB90

    int gridCount[8][8];
    for (int a = 0; a < N; ++a)                              // 0x02EDFBA0
        for (int b = 0; b < N; ++b) gridCount[b][a] = 0;     // 0x02EDFC0E
    // bucket[depth][cx][cy]; depth capacity 32 == the engine's int[32][8][8].
    static_assert(kRepeatedMeshMaxBatch == 32, "bucket depth mirrors the engine");
    std::vector<std::array<std::array<int, 8>, 8>> bucket(kSubsectionScratch);

    float boxMinX = kPlusHuge, boxMinY = kPlusHuge;          // 0x02EDFC19/0x02EDFC25
    float boxMaxX = kMinusHuge, boxMaxY = kMinusHuge;        // 0x02EDFC31/0x02EDFC3D
    for (long i = first; i < first + count; ++i) {           // 0x02EDFC52
        const SubsectionSphere& s = spheres[remap[size_t(i)]];
        if (!(boxMinX <= s.x)) boxMinX = s.x;                // 0x02EDFCA5
        if (!(boxMinY <= s.y)) boxMinY = s.y;                // 0x02EDFCD9
        if (!(boxMaxX >= s.x)) boxMaxX = s.x;                // 0x02EDFD0D
        if (!(boxMaxY >= s.y)) boxMaxY = s.y;                // 0x02EDFD41
    }
    // The engine asserts boxMinX != boxMaxX / boxMinY != boxMaxY here (lines
    // 645/646). Release divides anyway; the NaN that produces is folded to
    // column 0 by x87Fistp + the clamp below, which is what this port does.

    for (long i = first; i < first + count; ++i) {           // 0x02EDFF36
        const int inst = remap[size_t(i)];
        const SubsectionSphere& s = spheres[size_t(inst)];
        const float fx = float((double(s.x - boxMinX) / double(boxMaxX - boxMinX))
                               * double(N - 1));             // 0x02EDFF8C..0x02EDFFB7
        int cx = roundToInt(fx);                             // 0x02EDFFC7
        const float fy = float((double(s.y - boxMinY) / double(boxMaxY - boxMinY))
                               * double(N - 1));             // 0x02EDFFD8..0x02EDFFFE
        int cy = roundToInt(fy);                             // 0x02EE0014
        if (cx < 0) cx = 0; else if (cx > N - 1) cx = N - 1;  // 0x02EE001F
        if (cy < 0) cy = 0; else if (cy > N - 1) cy = N - 1;  // 0x02EE0054
        const int depth = gridCount[cx][cy];
        if (size_t(depth) >= bucket.size())
            throw std::runtime_error("local-detail subsection grid cell overflow");
        bucket[size_t(depth)][size_t(cx)][size_t(cy)] = inst; // 0x02EE00C3
        gridCount[cx][cy] = depth + 1;                        // 0x02EE00F8
    }

    int nonEmpty = 0;                                         // 0x02EE0100
    uint8_t sectionList[4][kSubsectionScratch]{};
    int sectionSize[4] = {0, 0, 0, 0};                        // 0x02EE010A
    long remaining = count;                                   // 0x02EE0132
    for (int q = 0; q < 4; ++q) {                             // 0x02EE0144
        long quota = remaining;                               // 0x02EE016C (the q==3 path)
        if (q < 3) {                                          // 0x02EE0178
            const long v = remaining / (4 - q);               // 0x02EE0181 (signed idiv)
            if (v <= leafThreshold) {                         // 0x02EE0195
                quota = v;                                    // 0x02EE01AE
            } else {
                quota = leafThreshold;                        // 0x02EE01B6
                while (quota * 4 < remaining) quota *= 4;     // 0x02EE01BF
            }
        }
        int i = 0, j = 0;                                     // 0x02EE01E1/0x02EE01EB
        while (sectionSize[q] < quota && remaining > 0) {     // 0x02EE01F5
            int ii = i, jj = j;
            if (q & 1) ii = (N - 1) - i;                      // 0x02EE0233
            if (q & 2) jj = (N - 1) - j;                      // 0x02EE0253
            int& cell = gridCount[ii][jj];                    // 0x02EE0273
            while (cell > 0 && sectionSize[q] < quota) {      // 0x02EE0292
                --cell;                                       // 0x02EE02CB  (LIFO pop)
                const int inst = bucket[size_t(cell)][size_t(ii)][size_t(jj)];
                sectionList[q][sectionSize[q]] = uint8_t(inst); // 0x02EE031C
                ++sectionSize[q];                             // 0x02EE0335
                --remaining;                                  // 0x02EE0345
            }
            if (i > j) {                                      // 0x02EE0350
                ++j;                                          // 0x02EE0367
                if (i == j) i = 0;                            // 0x02EE037B
            } else {
                ++i;                                          // 0x02EE0390
                if (i == j + 1) {                             // 0x02EE039F
                    if (i == N) break;                        // 0x02EE03AD
                    j = 0;                                    // 0x02EE03B7
                }
            }
        }
        if (sectionSize[q] > 0) ++nonEmpty;                   // 0x02EE03CC
    }
    if (sectionSize[0] + sectionSize[1] + sectionSize[2] + sectionSize[3] != count)
        throw std::runtime_error("local-detail subsection split lost objects");
    if (!(nonEmpty > 1)) return false;                        // 0x02EE04D6

    if (elements.size() >= kSubsectionScratch)
        throw std::runtime_error("local-detail subsection element scratch overflow");
    const size_t me = elements.size();                        // 0x02EE04E6
    elements.emplace_back();                                  // cursor += 0x50, 0x02EE04F3
    long writeIdx = first;                                    // 0x02EE04FB

    for (int q = 0; q < 4; ++q) {                             // 0x02EE050D
        if (sectionSize[q] <= 0) {                            // 0x02EE0535
            SubsectionElement& e = elements[me];
            e.count[q] = 0;                                   // 0x02EE0BB9
            e.childOffset[q] = 0;                             // 0x02EE0BC6
            e.centreX[q] = e.centreY[q] = e.centreZ[q] = 0.0f; // 0x02EE0BD5
            e.radius[q] = 0.0f;                               // 0x02EE0C01
            e.startIndex[q] = 0;                              // 0x02EE0C0E
            continue;
        }
        const long sectionStart = writeIdx;                   // 0x02EE0549
        for (int k = 0; k < sectionSize[q]; ++k)              // 0x02EE055B
            remap[size_t(writeIdx++)] = sectionList[q][k];    // 0x02EE05AD
        const int n = sectionSize[q];
        elements[me].count[q] = uint8_t(n);                   // 0x02EE05E2
        elements[me].startIndex[q] = uint8_t(sectionStart);   // 0x02EE05F4

        float minX = kPlusHuge, minY = kPlusHuge, minZ = kPlusHuge;
        float maxX = kMinusHuge, maxY = kMinusHuge, maxZ = kMinusHuge;
        for (int k = 0; k < n; ++k) {                         // 0x02EE0648
            const SubsectionSphere& s = spheres[sectionList[q][k]];
            if (!(minX <= s.x - s.radius)) minX = s.x - s.radius; // 0x02EE06C6
            if (!(minY <= s.y - s.radius)) minY = s.y - s.radius; // 0x02EE0726
            if (!(minZ <= s.z - s.radius)) minZ = s.z - s.radius; // 0x02EE0787
            if (!(maxX >= s.x + s.radius)) maxX = s.x + s.radius; // 0x02EE07E7
            if (!(maxY >= s.y + s.radius)) maxY = s.y + s.radius; // 0x02EE0847
            if (!(maxZ >= s.z + s.radius)) maxZ = s.z + s.radius; // 0x02EE08A8
        }
        // midpoint via `fadd; fdiv qword 2.0` (0x02EE08E7/0x02EE0909/0x02EE092B)
        const float cX = float((double(minX) + double(maxX)) / 2.0);
        const float cY = float((double(minY) + double(maxY)) / 2.0);
        const float cZ = float((double(minZ) + double(maxZ)) / 2.0);
        float radius = 0.0f;                                  // fldz 0x02EE0958
        for (int k = 0; k < n; ++k) {                         // 0x02EE0969
            const SubsectionSphere& s = spheres[sectionList[q][k]];
            const float d = magnitude(s.x - cX, s.y - cY, s.z - cZ) + s.radius;
            if (d > radius) radius = d;                       // 0x02EE0A0D
        }
        elements[me].centreX[q] = cX;                         // 0x02EE0A27
        elements[me].centreY[q] = cY;                         // 0x02EE0A39
        elements[me].centreZ[q] = cZ;                         // 0x02EE0A4C
        elements[me].radius[q] = radius;                      // 0x02EE0A5F

        // Element-relative, computed BEFORE the recursion (0x02EE0A63).
        const size_t offset = elements.size() - me;
        if (!(offset > 0 && offset < 256))
            throw std::runtime_error("local-detail subsection child offset out of range");
        const bool hasChild = buildSubsectionsWorker(                // 0x02EE0B81
            elements, sectionStart, n, leafThreshold, spheres, remap);
        elements[me].childOffset[q] = hasChild ? uint8_t(offset) : uint8_t(0);
    }
    return true;                                              // 0x02EE0C17
}

} // namespace

SubsectionTable buildSubSectionsAndObjectRemapTable(
    const std::vector<SubsectionSphere>& spheres, long maxObjectsPerSubSection) {
    // assert(ObjectCount > 0 && ObjectCount < 256), line 581 @0x02EDF851.
    if (spheres.empty() || spheres.size() >= 256)
        throw std::invalid_argument(
            "subsection object count must satisfy 0 < count < 256");
    // The driver's scratch is CSubsectionElement[32] and the worker's grid /
    // section arrays are all 32 deep, with no runtime bound anywhere; the sole
    // retail caller enforces the cap with its own assert (line 875), so the
    // port enforces it here.
    if (spheres.size() > kRepeatedMeshMaxBatch)
        throw std::invalid_argument(
            "subsection object count exceeds the 32-instance batch cap");

    SubsectionTable out;
    out.remap.resize(spheres.size());
    for (size_t i = 0; i < spheres.size(); ++i)               // 0x02EDF93B
        out.remap[i] = uint8_t(i);
    out.elements.reserve(kSubsectionScratch);
    out.present = buildSubsectionsWorker(                     // 0x02EDF9BC
        out.elements, 0, long(spheres.size()), maxObjectsPerSubSection,
        spheres, out.remap);
    if (!out.present) {
        // Nothing is allocated and the remap stays the identity (0x02EDF9C6).
        out.elements.clear();
        for (size_t i = 0; i < spheres.size(); ++i) out.remap[i] = uint8_t(i);
    }
    return out;
}

std::vector<uint8_t> serializeSubsectionElements(
    const std::vector<SubsectionElement>& elements) {
    std::vector<uint8_t> out;
    out.reserve(elements.size() * 0x50);
    for (const auto& e : elements) {
        for (int q = 0; q < 4; ++q) appendF32(out, e.centreX[q]);
        for (int q = 0; q < 4; ++q) appendF32(out, e.centreY[q]);
        for (int q = 0; q < 4; ++q) appendF32(out, e.centreZ[q]);
        for (int q = 0; q < 4; ++q) appendF32(out, e.radius[q]);
        for (int q = 0; q < 4; ++q) out.push_back(e.count[q]);
        for (int q = 0; q < 4; ++q) out.push_back(e.startIndex[q]);
        for (int q = 0; q < 4; ++q) out.push_back(e.childOffset[q]);
        for (int q = 0; q < 4; ++q) out.push_back(e.tail[q]);
    }
    return out;
}

EmptyLocalDetailSection buildEmptyLocalDetailSection(
    size_t sectionOffset, size_t fileBlockAlignment,
    LocalDetailQuadHeader rootHeader) {
    if (fileBlockAlignment == 0 ||
        (fileBlockAlignment & (fileBlockAlignment - 1)) != 0)
        throw std::invalid_argument(
            "empty local-detail file-block alignment must be a power of two");
    if (sectionOffset > size_t(INT32_MAX))
        throw std::runtime_error("empty local-detail section offset exceeds s32");

    EmptyLocalDetailSection out;
    out.descriptorOffset = sectionOffset;
    out.bytes.resize(12, 0); // GenerateStaticMapEntry reserves this first.

    const size_t absoluteAfterDescriptor = sectionOffset + out.bytes.size();
    const size_t aligned = (absoluteAfterDescriptor + fileBlockAlignment - 1) &
                           ~(fileBlockAlignment - 1);
    if (aligned > size_t(INT32_MAX))
        throw std::runtime_error("empty local-detail file block exceeds s32");
    out.bytes.resize(aligned - sectionOffset, 0);
    out.fileBlockOffset = aligned;

    // CQuadTreeElement<CLocalDetailCacheMap>::SaveFileBlock for an empty root:
    // no object-cache groups, followed by four absent child markers.
    for (int i = 0; i < 5; ++i) appendU32(out.bytes, 0);
    rootHeader.fileBlockPos = static_cast<int32_t>(out.fileBlockOffset);
    rootHeader.fileBlockSize = 20;
    rootHeader.offsetIntoFileBlock = 0;

    out.headerOffset = sectionOffset + out.bytes.size();
    const auto header = serializeLocalDetailQuadHeader(rootHeader);
    const auto palette = serializeEmptyLocalDetailPalette(false);
    out.bytes.insert(out.bytes.end(), header.begin(), header.end());
    out.bytes.insert(out.bytes.end(), palette.begin(), palette.end());
    out.headerSpan = header.size() + palette.size();
    if (out.headerOffset > size_t(INT32_MAX) || out.headerSpan > size_t(INT32_MAX))
        throw std::runtime_error("empty local-detail header exceeds s32");

    const auto descriptor = serializeLocalDetailRootDescriptor(
        static_cast<int32_t>(out.headerOffset),
        static_cast<int32_t>(out.headerSpan), true);
    std::copy(descriptor.begin(), descriptor.end(), out.bytes.begin());
    return out;
}

namespace {

// Stock CEngineLocalDetailGenerator::GetCacheGroupInfo table. BuildThemes
// (0x02D29100) derives it from registered collection types by repeatedly taking
// the highest unassigned fade end, grouping types within 16.0 below it, and
// ORing their primitive masks. These stock results were
// independently observed over 15,418 retail groups with zero exceptions.
// CObjectCacheGroupCollection::BuildPrimitives (0x02E3C2A8/0x02E3C2CA) looks up
// BOTH the group header's fade (+0x1c) and mask (+0x20) from the group's
// CacheGroup id (+0x24); they are not free parameters. Recovered from 15,418
// retail groups with zero exceptions.
struct CacheGroupInfo { float fade; uint32_t mask; };
constexpr CacheGroupInfo kCacheGroupInfo[5] = {
    {210.0f, 4}, {118.0f, 5}, {85.0f, 4}, {48.0f, 3}, {23.0f, 3}};

// CObjectCacheGroupCollection. `objects` is the group's source-object list in
// AddObject order; the 64-entry cap is on its size (GetSourceObjectCount,
// 0x02E3CF60 -> grp+0x38, compared `cmp eax,0x40; jge` at 0x02E3CB9B).
struct DetailGroup {
    uint32_t cacheGroup = 0;
    std::vector<LocalDetailPlacement> objects;
    std::vector<uint8_t> frame;       // [u32 ulen][u32 clen][LZO]
    size_t uncompressedSize = 0;
    float sphere[4] = {};
    bool ownBlock = false;
    int32_t fbPos = 0, fbSize = 0, offIn = 0;
};

// CQuadTreeElement<CLocalDetailCacheMap>, 0x4C bytes in the engine.
struct DetailNode {
    uint16_t cellX = 0, cellY = 0, cellW = 0, cellH = 0;
    int child[4] = {-1, -1, -1, -1};
    std::vector<int> groups;          // list order; head is index 0
    float sphere[4] = {};
    float maxFade = 0.0f;
    uint32_t mask = 0;
    bool ownBlock = false;
    int32_t fbPos = 0, fbSize = 0, offIn = 0;
};

// The engine's HighestSetBitIndex helper (0x02C87570), used only as
// `1 << HighestSetBitIndex(size - 1)` = the largest power of two strictly below
// `size`.
int highestSetBitIndex(uint32_t v) {
    int index = 0;
    while (v >>= 1) ++index;
    return index;
}

// CQuadTreeElement::UpdateDynamicArea, FableWin 0x02E3F400, called with
// (cx*16, cy*16, 16, 16) by both of its callers -- CLocalDetailCacheMap::
// GenerateStaticMapEntry 0x02E3BFDC and UpdateModifiedDynamicAreas 0x02E3BE74.
// A node stops splitting when its cell EQUALS the requested area (0x02E3F545);
// otherwise it splits along whichever axis is still too large, with
// half = 1 << HighestSetBitIndex(extent - 1) (0x02E3F759 / 0x02E3FA58 /
// 0x02E3FD44) and quadrant bit0 = +X half (0x02E3FD99), bit1 = +Y half
// (0x02E3FDB4). Returns the index of the leaf covering (ax, ay, aw, ah).
int ensureLeaf(std::vector<DetailNode>& nodes, int at,
               long ax, long ay, long aw, long ah) {
    const long cx = nodes[size_t(at)].cellX, cy = nodes[size_t(at)].cellY;
    const long cw = nodes[size_t(at)].cellW, ch = nodes[size_t(at)].cellH;
    if (ax < cx || ay < cy || ax + aw > cx + cw || ay + ah > cy + ch)
        throw std::runtime_error("local-detail leaf area escapes its quadtree node");
    if (cw == aw && ch == ah) return at;   // 0x02E3F545: this node IS the leaf

    int q = 0;
    long kx = cx, ky = cy, kw = cw, kh = ch;
    if (cw == aw) {                        // 0x02E3F746: split in Y only
        const long half = 1L << highestSetBitIndex(uint32_t(ch - 1));
        q = (ay >= cy + half) ? 2 : 0;
        ky = (q & 2) ? cy + half : cy;
        kh = (q & 2) ? ch - half : half;
    } else if (ch == ah) {                 // 0x02E3FA45: split in X only
        const long half = 1L << highestSetBitIndex(uint32_t(cw - 1));
        q = (ax >= cx + half) ? 1 : 0;
        kx = (q & 1) ? cx + half : cx;
        kw = (q & 1) ? cw - half : half;
    } else {                               // 0x02E3FD44: split in both
        const long hw = 1L << highestSetBitIndex(uint32_t(cw - 1));
        const long hh = 1L << highestSetBitIndex(uint32_t(ch - 1));
        if (ax >= cx + hw) q |= 1;
        if (ay >= cy + hh) q |= 2;
        kx = (q & 1) ? cx + hw : cx;  kw = (q & 1) ? cw - hw : hw;
        ky = (q & 2) ? cy + hh : cy;  kh = (q & 2) ? ch - hh : hh;
    }
    if (nodes[size_t(at)].child[q] < 0) {  // ctor 0x02E38120
        DetailNode created;
        created.cellX = uint16_t(kx); created.cellY = uint16_t(ky);
        created.cellW = uint16_t(kw); created.cellH = uint16_t(kh);
        nodes.push_back(created);
        nodes[size_t(at)].child[q] = int(nodes.size()) - 1;
    }
    return ensureLeaf(nodes, nodes[size_t(at)].child[q], ax, ay, aw, ah);
}

// The contents of one CObjectCacheGroupCollection, uncompressed: the collection
// count followed by one collection per palette type present, each holding its
// primitives. This is the payload CObjectCacheGroupCollection::SaveContents
// (0x02E3D850) LZO-frames.
std::vector<uint8_t> encodeGroupContents(
    const std::vector<foliage::FoliageType>& palette,
    const std::vector<LocalDetailPlacement>& objects) {
    std::vector<std::vector<LocalDetailPlacement>> byType(palette.size());
    for (const auto& p : objects) byType[size_t(p.paletteIndex)].push_back(p);

    std::vector<uint8_t> decoded;
    uint32_t collectionCount = 0;
    for (const auto& list : byType) if (!list.empty()) ++collectionCount;
    // LoadContents reads this count before constructing collections.
    appendU32(decoded, collectionCount);
    for (size_t type = 0; type < byType.size(); ++type) {
        const auto& list = byType[type];
        if (list.empty()) continue;
        // This EBOOL is NeedsRenderUpdate (RenderUpdate 0x02E35C91 early-outs on
        // it), NOT a collection-enabled flag: retail sets it only for z-sprite
        // batches. Every type-1 grass collection sampled out of retail
        // Darkwood_3 (groups 0xb87ee / 0xb9140 / 0xbbb27, CacheGroup 4) carries
        // 0 and still renders.
        appendU32(decoded, palette[type].runtimeSettings[9] == 2 ? 1u : 0u);
        appendU32(decoded, static_cast<uint32_t>(type));
        const bool repeated = palette[type].runtimeSettings[9] == 1;
        if (repeated) {
            // Native grass is type-1 RepeatedMesh primitives holding two paired
            // C4DVector arrays. Retail proves A=(rotX,rotY,0,0), whose XY
            // magnitude equals B.w, and B=(worldX,worldY,worldZ,scale).
            //
            // ObjectCount per primitive is capped at MAX_BATCH_SIZE (32), NOT at
            // the format's 255: when a subsection table culls part of a batch the
            // renderer compacts the survivors into manager lane arrays strided
            // 0x80 bytes apart (32 floats), and retail's own generator splits
            // source objects by GetRepeatedMeshBatchSize. Retail's observed max
            // over 1,591 records is exactly 32.
            std::vector<std::vector<LocalDetailPlacement>> batches;
            for (size_t i = 0; i < list.size(); i += kRepeatedMeshMaxBatch)
                batches.emplace_back(
                    list.begin() + std::ptrdiff_t(i),
                    list.begin() + std::ptrdiff_t(std::min(i + kRepeatedMeshMaxBatch,
                                                           list.size())));
            appendU32(decoded, static_cast<uint32_t>(batches.size()));
            for (const auto& batch : batches) {
                appendU32(decoded, 1); // CLocalDetailPrimitiveRepeatedMesh
                float minX=batch[0].x, minY=batch[0].y, minZ=batch[0].z;
                float maxX=minX, maxY=minY, maxZ=minZ, maxScale=0.0f;
                for (const auto& p : batch) {
                    minX=std::min(minX,p.x); minY=std::min(minY,p.y); minZ=std::min(minZ,p.z);
                    maxX=std::max(maxX,p.x); maxY=std::max(maxY,p.y); maxZ=std::max(maxZ,p.z);
                    maxScale=std::max(maxScale,p.scale);
                }
                appendF32(decoded,minX); appendF32(decoded,minY); appendF32(decoded,minZ);
                appendF32(decoded,maxX); appendF32(decoded,maxY); appendF32(decoded,maxZ+2.0f);
                const float cx=(minX+maxX)*0.5f, cy=(minY+maxY)*0.5f, cz=(minZ+maxZ)*0.5f;
                const float rr=std::sqrt((maxX-minX)*(maxX-minX)*0.25f+
                                         (maxY-minY)*(maxY-minY)*0.25f+
                                         (maxZ-minZ)*(maxZ-minZ)*0.25f)+2.0f;
                appendF32(decoded,cx); appendF32(decoded,cy); appendF32(decoded,cz); appendF32(decoded,rr);
                appendU32(decoded, static_cast<uint32_t>(batch.size()));
                appendF32(decoded, maxScale);
                // Run the engine's own subsection builder FIRST: it returns the
                // destination->source permutation the instance arrays have to be
                // written in. BuildFromSourceMeshes (0x02EE17F8) writes A, B and
                // the landscape normals with the loop index as the DESTINATION
                // slot and remap[i] as the source object, and WindDelay inherits
                // that order because BuildWindDelays derives it from the already
                // permuted position array. Emitting the arrays in authoring order
                // while the table indexes permuted slots would scatter every
                // instance to another instance's position.
                // centre = objectMatrix.TransformPoint(mesh.boundingSphere.centre),
                // radius = mesh.boundingSphere.radius * scale (0x02EE1364..
                // 0x02EE13F9). The object matrix is a Z rotation scaled by the
                // instance scale plus the placement translation, so
                //   x' = c*mx - s*my + px,  y' = s*mx + c*my + py,
                //   z' = scale*mz + pz.
                // The mesh sphere is real data read from the mesh bank; when the
                // caller did not supply one we emit NO subsection table rather
                // than substitute a guessed radius. A null table is a proven-legal
                // retail configuration.
                const foliage::MeshSphere& meshSphere = palette[type].meshSphere;
                SubsectionTable subsections;
                if (meshSphere.known() && meshSphere.polyCountKnown()) {
                    std::vector<SubsectionSphere> instanceSpheres;
                    instanceSpheres.reserve(batch.size());
                    for (const auto& p : batch) {
                        const float c = std::cos(p.rotationRadians) * p.scale;
                        const float sn = std::sin(p.rotationRadians) * p.scale;
                        instanceSpheres.push_back(
                            {c * meshSphere.x - sn * meshSphere.y + p.x,
                             sn * meshSphere.x + c * meshSphere.y + p.y,
                             p.scale * meshSphere.z + p.z,
                             meshSphere.radius * p.scale});
                    }
                    const long leafThreshold = std::min<long>(
                        4, std::max<long>(1, 128 / meshSphere.polyCount));
                    subsections = buildSubSectionsAndObjectRemapTable(
                        instanceSpheres, leafThreshold);
                } else {
                    subsections.remap.resize(batch.size());
                    for (size_t i = 0; i < batch.size(); ++i)
                        subsections.remap[i] = static_cast<uint8_t>(i);
                }
                const auto& order = subsections.remap; // identity when absent
                for (size_t i = 0; i < batch.size(); ++i) {
                    const auto& p = batch[order[i]];
                    appendF32(decoded,std::cos(p.rotationRadians)*p.scale);
                    appendF32(decoded,std::sin(p.rotationRadians)*p.scale);
                    appendF32(decoded,0.0f); appendF32(decoded,0.0f);
                }
                for (size_t i = 0; i < batch.size(); ++i) {
                    const auto& p = batch[order[i]];
                    appendF32(decoded,p.x); appendF32(decoded,p.y);
                    appendF32(decoded,p.z); appendF32(decoded,p.scale);
                }
                // LandscapeNormalArray is WHOLE-ARRAY SoA: X[P] then Y[P] then
                // Z[P] with P = (count+3)&~3, not an interleaved triple per
                // instance. RenderSubPrimitive dereferences the base
                // unconditionally, so the presence byte must be 1. The pad slots
                // are retail's uninitialised allocator fill.
                decoded.push_back(1); // LandscapeNormalArray present
                const size_t paddedCount = (batch.size() + 3u) & ~size_t(3u);
                for (int axis = 0; axis < 3; ++axis) {
                    for (size_t i = 0; i < batch.size(); ++i) {
                        const auto& p = batch[order[i]];
                        appendF32(decoded, axis == 0 ? p.nx : (axis == 1 ? p.ny : p.nz));
                    }
                    for (size_t i = batch.size(); i < paddedCount; ++i)
                        appendU32(decoded, 0xCDCDCDCDu);
                }
                decoded.push_back(1); // WindDelayArray present
                // Every authored delay is 0, so the permutation is a no-op on
                // these bytes today; it is applied anyway so a future non-flat
                // wind table cannot silently desynchronise from the positions.
                for (size_t i = 0; i < batch.size(); ++i) {
                    (void)order[i];
                    decoded.push_back(0);
                }
                // The subsection table, straight out of the ported builder. The
                // engine's own worker declines to emit one when the batch is at
                // or below the leaf threshold or when the split collapses into a
                // single non-empty section; a null table is a proven-legal
                // retail configuration in that case (RenderSubPrimitive takes
                // the no-table path at 0x02ED0E25 and draws [0, ObjectCount) in
                // batchMax chunks), so that path is kept rather than forced.
                if (subsections.present) {
                    decoded.push_back(1);
                    appendU32(decoded,
                              static_cast<uint32_t>(subsections.elements.size()));
                    const auto table =
                        serializeSubsectionElements(subsections.elements);
                    decoded.insert(decoded.end(), table.begin(), table.end());
                } else {
                    decoded.push_back(0);
                }
            }
            continue;
        }
        appendU32(decoded, static_cast<uint32_t>(list.size()));
        for (const auto& p : list) {
            appendU32(decoded, 0); // CLocalDetailPrimitiveMesh
            const float r = std::max(0.5f, p.scale);
            appendF32(decoded, p.x - r); appendF32(decoded, p.y - r);
            appendF32(decoded, p.z); appendF32(decoded, p.x + r);
            appendF32(decoded, p.y + r); appendF32(decoded, p.z + 2.0f * r);
            appendF32(decoded, p.x); appendF32(decoded, p.y);
            appendF32(decoded, p.z + r); appendF32(decoded, 1.75f * r);
            const float c = std::cos(p.rotationRadians) * p.scale;
            const float s = std::sin(p.rotationRadians) * p.scale;
            const float matrix[12] = {
                c, s, 0.0f, -s, c, 0.0f, 0.0f, 0.0f, p.scale,
                p.x, p.y, p.z};
            for (float v : matrix) appendF32(decoded, v);
            appendF32(decoded, 1.0f);
        }
    }
    return decoded;
}

// C3DBoundingSphere::BuildFromSubSpheres, 0x0338B200. Build the
// radius-inflated AABB, use its midpoint, then take
// max(|inputCentre - centre| + inputRadius). The engine keeps the midpoint
// arithmetic on the x87 stack until the float store; double reproduces that
// evaluation more closely than an early float-rounded sum.
struct MergedSphere { bool any = false; };
void mergeSphere(MergedSphere& acc, const float* s, float* lo, float* hi) {
    if (s[3] <= 0.0f) return;
    for (int axis = 0; axis < 3; ++axis) {
        lo[axis] = acc.any ? std::min(lo[axis], s[axis] - s[3]) : s[axis] - s[3];
        hi[axis] = acc.any ? std::max(hi[axis], s[axis] + s[3]) : s[axis] + s[3];
    }
    acc.any = true;
}

// CQuadTreeElement::CalcBoundingSphereAndFadeDistanceFromChildren, 0x02E39420.
// It recursively processes children, then direct groups, gathering only
// non-zero-radius contributors. maxFade is MAX and mask is OR over the same
// contributors. The sphere port is verified within 0.00035 on all 198 retail
// nodes of Darkwood_3 / StartOakValeWest / Darkwood_9 / Darkwood_Filler_15;
// fade and mask are byte-exact on the same set.
void calcNodeBounds(std::vector<DetailNode>& nodes,
                    const std::vector<DetailGroup>& groups, int at) {
    nodes[size_t(at)].maxFade = 0.0f;
    nodes[size_t(at)].mask = 0;
    MergedSphere acc;
    float lo[3] = {}, hi[3] = {};
    for (int q = 0; q < 4; ++q) {
        const int c = nodes[size_t(at)].child[q];
        if (c < 0) continue;
        calcNodeBounds(nodes, groups, c);
        if (nodes[size_t(c)].sphere[3] <= 0.0f) continue;
        nodes[size_t(at)].maxFade =
            std::max(nodes[size_t(at)].maxFade, nodes[size_t(c)].maxFade);
        nodes[size_t(at)].mask |= nodes[size_t(c)].mask;
        mergeSphere(acc, nodes[size_t(c)].sphere, lo, hi);
    }
    for (int index : nodes[size_t(at)].groups) {
        const DetailGroup& g = groups[size_t(index)];
        if (g.sphere[3] <= 0.0f) continue;
        nodes[size_t(at)].maxFade =
            std::max(nodes[size_t(at)].maxFade, kCacheGroupInfo[g.cacheGroup].fade);
        nodes[size_t(at)].mask |= kCacheGroupInfo[g.cacheGroup].mask;
        mergeSphere(acc, g.sphere, lo, hi);
    }
    DetailNode& self = nodes[size_t(at)];
    if (!acc.any) {
        self.sphere[0] = self.sphere[1] = self.sphere[2] = self.sphere[3] = 0.0f;
        return;
    }
    for (int axis = 0; axis < 3; ++axis)
        self.sphere[axis] = float((double(lo[axis]) + double(hi[axis])) / 2.0);
    float radius = 0.0f;
    auto extend = [&](const float* s) {
        if (s[3] <= 0.0f) return;
        const float dx = s[0] - self.sphere[0];
        const float dy = s[1] - self.sphere[1];
        const float dz = s[2] - self.sphere[2];
        radius = std::max(radius, magnitude(dx, dy, dz) + s[3]);
    };
    for (int q = 0; q < 4; ++q)
        if (self.child[q] >= 0) extend(nodes[size_t(self.child[q])].sphere);
    for (int index : self.groups) extend(groups[size_t(index)].sphere);
    self.sphere[3] = radius;
}

// CQuadTreeElement::AssignFileBlocks, 0x02E3F230. A group whose estimated save
// size alone exceeds the manager limit takes its own block (0x02E3F292). The
// recovered generator caps a whole group at 64 source objects, so fixed-size
// type-1/type-2 records cannot reach that branch; preserve it because the
// native container is generic and loaded/static collections may. A node
// whose whole inline subtree estimate exceeds it (or the root, which is forced
// by GenerateStaticMapEntry 0x02E3C044) takes its own (0x02E3F314). The 0x40 at
// 0x02E3F303 is the node's own table: 44-byte header + u32 count + 4 child flags.
long assignFileBlocks(std::vector<DetailNode>& nodes,
                      std::vector<DetailGroup>& groups, int at, bool forceOwn,
                      long limit) {
    long total = 0;
    for (int index : nodes[size_t(at)].groups) {
        DetailGroup& g = groups[size_t(index)];
        // GetSaveSize (0x02E3D7A0) serializes each collection into an empty
        // memory stream, omitting SaveContents' leading u32 collection count,
        // then returns stream length + 0x28. `uncompressedSize` is the complete
        // SaveContents payload including that u32, hence the exact equivalent
        // is payload + 0x24.
        const long size = long(g.uncompressedSize) + 0x24;
        if (size > limit) g.ownBlock = true;
        else total += size;
    }
    for (int q = 0; q < 4; ++q)
        if (nodes[size_t(at)].child[q] >= 0)
            total += assignFileBlocks(nodes, groups, nodes[size_t(at)].child[q],
                                      false, limit);
    total += 0x40;
    if (total > limit || forceOwn) { nodes[size_t(at)].ownBlock = true; total = 0; }
    return total;
}

// A CDataOutputStream over a chunk-relative byte vector: absolute chunk offsets
// in, random-access rewrites (the two-pass SaveTree needs SetPosition), and an
// AlignTo that pads with zeros exactly like 0x030835D0.
struct DetailStream {
    std::vector<uint8_t>& bytes;
    size_t base = 0;      // absolute chunk offset of bytes[0]
    size_t cursor = 0;    // absolute
    void put(const uint8_t* p, size_t n) {
        const size_t off = cursor - base;
        if (bytes.size() < off + n) bytes.resize(off + n, 0);
        std::memcpy(bytes.data() + off, p, n);
        cursor += n;
    }
    void putU32(uint32_t v) {
        const uint8_t b[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16),
                              uint8_t(v >> 24)};
        put(b, 4);
    }
    void putU16(uint16_t v) {
        const uint8_t b[2] = {uint8_t(v), uint8_t(v >> 8)};
        put(b, 2);
    }
    void putF32(float v) { uint32_t bits = 0; std::memcpy(&bits, &v, 4); putU32(bits); }
    void setPosition(size_t absolute) {
        if (bytes.size() < absolute - base) bytes.resize(absolute - base, 0);
        cursor = absolute;
    }
    void alignTo(size_t alignment) {
        const size_t target = (cursor + alignment - 1) & ~(alignment - 1);
        while (cursor < target) { const uint8_t zero = 0; put(&zero, 1); }
    }
};

// CObjectCacheGroupCollection::SaveHeader, 0x02E3D5B0. Byte 0x27 is never
// written by the engine (its last store is h+0x26 at 0x02E3D716) and retail
// ships stack garbage there; this writer emits 0.
void saveGroupHeader(DetailStream& out, const DetailGroup& g, bool validate) {
    if (validate && !(g.fbPos > 0 && g.fbSize > 0 && g.offIn >= 0 && g.offIn < g.fbSize))
        throw std::runtime_error("local-detail group file-block triple is invalid");
    out.putU32(uint32_t(g.fbPos));
    out.putU32(uint32_t(g.fbSize));
    out.putU32(uint32_t(g.offIn));
    for (float v : g.sphere) out.putF32(v);
    out.putF32(kCacheGroupInfo[g.cacheGroup].fade);   // +0x1c
    out.putU32(kCacheGroupInfo[g.cacheGroup].mask);   // +0x20
    out.putU16(uint16_t(g.cacheGroup));               // +0x24
    const uint8_t tail[2] = {1, 0};                   // +0x26 flags, +0x27 unwritten
    out.put(tail, 2);
}

// CQuadTreeElement<CLocalDetailCacheMap>::SaveHeader, 0x02E3E680: sphere,
// maxFade, mask, then the file-block triple, then cellX/cellY/WIDTH/HEIGHT.
void saveNodeHeader(DetailStream& out, const DetailNode& n, bool validate) {
    if (validate && !(n.fbPos > 0 && n.fbSize > 0 && n.offIn >= 0 && n.offIn < n.fbSize))
        throw std::runtime_error("local-detail node file-block triple is invalid");
    for (float v : n.sphere) out.putF32(v);
    out.putF32(n.maxFade);
    out.putU32(n.mask);
    out.putU32(uint32_t(n.fbPos));
    out.putU32(uint32_t(n.fbSize));
    out.putU32(uint32_t(n.offIn));
    out.putU16(n.cellX); out.putU16(n.cellY);
    out.putU16(n.cellW); out.putU16(n.cellH);
}

void saveTree(DetailStream& out, std::vector<DetailNode>& nodes,
              std::vector<DetailGroup>& groups, int at, size_t alignment);

// CQuadTreeElement::SaveFileBlock, 0x02E3EB20. Inline group payloads first, then
// each inline child's subtree, then LAST this node's own table: u32 groupCount,
// that many 0x28 group headers, then four (u32 present [+ 44-byte child header])
// slots. Nothing inside a block is aligned or padded.
void saveFileBlock(DetailStream& out, std::vector<DetailNode>& nodes,
                   std::vector<DetailGroup>& groups, int at,
                   long fbPosArg, long fbSizeArg, bool final) {
    for (int index : nodes[size_t(at)].groups) {
        DetailGroup& g = groups[size_t(index)];
        if (g.ownBlock) continue;
        // CObjectCacheGroupCollection::SaveContents, 0x02E3D850: the group takes
        // its OWNER'S (fbPos, fbSize) verbatim -- confirmed on 612 retail groups
        // with zero exceptions -- and its offIn is where its payload landed.
        if (fbPosArg > 0) {
            g.fbPos = int32_t(fbPosArg);
            g.fbSize = int32_t(fbSizeArg);
            g.offIn = int32_t(long(out.cursor) - fbPosArg);
        } else {
            g.fbPos = int32_t(out.cursor);
            g.offIn = 0;
        }
        const size_t startedAt = out.cursor;
        out.put(g.frame.data(), g.frame.size());
        if (fbPosArg <= 0) g.fbSize = int32_t(out.cursor - startedAt);
    }
    for (int q = 0; q < 4; ++q) {
        const int c = nodes[size_t(at)].child[q];
        if (c >= 0 && !nodes[size_t(c)].ownBlock)
            saveFileBlock(out, nodes, groups, c, fbPosArg, fbSizeArg, final);
    }
    if (fbPosArg > 0) {
        nodes[size_t(at)].fbPos = int32_t(fbPosArg);
        nodes[size_t(at)].offIn = int32_t(long(out.cursor) - fbPosArg);
    } else {
        nodes[size_t(at)].fbPos = int32_t(out.cursor);
        nodes[size_t(at)].offIn = 0;
    }
    out.putU32(uint32_t(nodes[size_t(at)].groups.size()));
    for (int index : nodes[size_t(at)].groups)
        saveGroupHeader(out, groups[size_t(index)], final);
    for (int q = 0; q < 4; ++q) {
        const int c = nodes[size_t(at)].child[q];
        if (c < 0) { out.putU32(0); continue; }
        out.putU32(1);
        saveNodeHeader(out, nodes[size_t(c)], final);
    }
    nodes[size_t(at)].fbSize = (fbPosArg > 0)
        ? int32_t(fbSizeArg)
        : int32_t(out.cursor - size_t(nodes[size_t(at)].fbPos));
}

// CQuadTreeElement::SaveSubFileBlocks, 0x02E3EFE0: own-block groups first, then
// children depth-first in quadrant order.
void saveSubFileBlocks(DetailStream& out, std::vector<DetailNode>& nodes,
                       std::vector<DetailGroup>& groups, int at, size_t alignment) {
    for (int index : nodes[size_t(at)].groups) {
        DetailGroup& g = groups[size_t(index)];
        if (!g.ownBlock) continue;
        out.alignTo(alignment);
        g.fbPos = int32_t(out.cursor);
        g.offIn = 0;
        const size_t startedAt = out.cursor;
        out.put(g.frame.data(), g.frame.size());
        g.fbSize = int32_t(out.cursor - startedAt);
    }
    for (int q = 0; q < 4; ++q) {
        const int c = nodes[size_t(at)].child[q];
        if (c < 0) continue;
        if (nodes[size_t(c)].ownBlock) saveTree(out, nodes, groups, c, alignment);
        else saveSubFileBlocks(out, nodes, groups, c, alignment);
    }
}

// CQuadTreeElement::SaveTree, 0x02E3E890. Two passes over the same byte range:
// the first learns the block size, the second rewrites identical bytes with the
// real (fbPos, fbSize) triple -- and, because it runs after SaveSubFileBlocks,
// with the now-known triples of any own-block descendants.
void saveTree(DetailStream& out, std::vector<DetailNode>& nodes,
              std::vector<DetailGroup>& groups, int at, size_t alignment) {
    out.alignTo(alignment);
    const size_t start = out.cursor;
    saveFileBlock(out, nodes, groups, at, 0, 0, false);
    const size_t end = out.cursor;
    saveSubFileBlocks(out, nodes, groups, at, alignment);
    const size_t fin = out.cursor;
    out.setPosition(start);
    saveFileBlock(out, nodes, groups, at, long(start), long(end - start), true);
    if (out.cursor != end)
        throw std::runtime_error("local-detail two-pass file block changed size");
    out.setPosition(fin);
}

} // namespace

EmptyLocalDetailSection buildType0LocalDetailSection(
    size_t sectionOffset, size_t fileBlockAlignment,
    LocalDetailQuadHeader rootHeader,
    const std::vector<foliage::FoliageType>& palette,
    const std::vector<LocalDetailPlacement>& placements,
    LocalDetailCellGrid grid) {
    if (palette.empty() || placements.empty())
        return buildEmptyLocalDetailSection(sectionOffset, fileBlockAlignment, rootHeader);
    if (fileBlockAlignment == 0 ||
        (fileBlockAlignment & (fileBlockAlignment - 1)) != 0)
        throw std::invalid_argument("local-detail alignment must be a power of two");
    if (!(grid.unitsPerCell > 0.0f))
        throw std::invalid_argument("local-detail cell grid needs a positive cell size");

    for (const auto& p : placements) {
        if (p.paletteIndex < 0 || size_t(p.paletteIndex) >= palette.size())
            throw std::runtime_error("local-detail palette index out of range");
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
            !std::isfinite(p.rotationRadians) || !std::isfinite(p.scale) || p.scale <= 0)
            throw std::runtime_error("invalid local-detail transform");
    }

    // --- 1. Build the quadtree -----------------------------------------------
    // CLocalDetailCacheMap::GenerateStaticMapEntry (0x02E3BF00) walks the WHOLE
    // map in 16x16-cell leaves -- `for cy < height/16, cx < width/16:
    // root.UpdateDynamicArea(map, cx*16, cy*16, 16, 16, ...)` (0x02E3BFD6) --
    // over a root whose cell is (0, 0, mapWidth, mapHeight) (ctor 0x02E3ADAE).
    // Groups therefore exist ONLY on 16x16 leaves; internal nodes assert they
    // hold none (UpdateDynamicArea line 0x8BA at 0x02E3F66B).
    constexpr long kLeafCells = 16;   // both callers push 0x10, 0x10
    std::vector<DetailNode> nodes;
    DetailNode root;
    root.cellX = rootHeader.cellBounds[0]; root.cellY = rootHeader.cellBounds[1];
    root.cellW = rootHeader.cellBounds[2]; root.cellH = rootHeader.cellBounds[3];
    nodes.push_back(root);
    const long leafCountX = long(root.cellW) / kLeafCells;
    const long leafCountY = long(root.cellH) / kLeafCells;
    if (leafCountX <= 0 || leafCountY <= 0)
        throw std::runtime_error(
            "local-detail root cell is smaller than one 16x16 leaf: the engine's "
            "own generator loop would create no leaves and place nothing");

    // Bin every placement into its map cell. The engine bins spatially (a leaf
    // owns a 16x16 cell rectangle and GetPrimitivesFromMap 0x02E3DD50 walks its
    // own cells), so a writer has to know where cell (0,0) sits in world space.
    std::vector<std::vector<int>> cellObjects(size_t(root.cellW) * size_t(root.cellH));
    const long coveredW = leafCountX * kLeafCells, coveredH = leafCountY * kLeafCells;
    for (size_t i = 0; i < placements.size(); ++i) {
        const auto& p = placements[i];
        const long cx = long(std::floor((p.x - grid.worldOriginX) / grid.unitsPerCell));
        const long cy = long(std::floor((p.y - grid.worldOriginY) / grid.unitsPerCell));
        if (cx < long(root.cellX) || cy < long(root.cellY) ||
            cx >= long(root.cellX) + coveredW || cy >= long(root.cellY) + coveredH)
            throw std::runtime_error(
                "local-detail placement lies outside the map cells the engine's "
                "leaf loop covers");
        cellObjects[size_t(cy - root.cellY) * size_t(root.cellW) +
                    size_t(cx - root.cellX)].push_back(int(i));
    }

    // --- 2. Fill the leaves --------------------------------------------------
    std::vector<DetailGroup> groups;
    auto cacheGroupOf = [&](const LocalDetailPlacement& p) {
        const uint32_t cg = palette[size_t(p.paletteIndex)].runtimeSettings[10];
        if (cg >= 5)
            throw std::runtime_error(
                "local-detail CacheGroup id has no GetCacheGroupInfo row");
        return cg;
    };
    for (long ly = 0; ly < leafCountY; ++ly) {
        for (long lx = 0; lx < leafCountX; ++lx) {
            const long ax = long(root.cellX) + lx * kLeafCells;
            const long ay = long(root.cellY) + ly * kLeafCells;
            const int leaf = ensureLeaf(nodes, 0, ax, ay, kLeafCells, kLeafCells);
            // GetPrimitivesFromMap 0x02E3DF3B..0x02E3DFF2 sweeps the leaf's own
            // cells in Morton order, the high bit going to X, and emits that
            // cell's placements before moving on. The order matters because
            // groups are created on demand and PUSHED ON THE FRONT of the leaf's
            // list, so it fixes the on-disk group order.
            const long side = long(nodes[size_t(leaf)].cellW);
            for (long i = 0; i < side * side; ++i) {
                long dx = 0, dy = 0; bool toggle = true;
                for (long m = (side * side) / 2; m > 0; m >>= 1) {
                    if (toggle) { dx = (dx << 1) | ((i & m) ? 1 : 0); toggle = false; }
                    else        { dy = (dy << 1) | ((i & m) ? 1 : 0); toggle = true; }
                }
                const long cx = dx + long(nodes[size_t(leaf)].cellX);
                const long cy = dy + long(nodes[size_t(leaf)].cellY);
                // Retail synthesises this cell's placements here by blending up
                // to three themes; an authoring writer instead replays the
                // placements the caller put in this cell, in caller order.
                const auto& inCell =
                    cellObjects[size_t(cy - root.cellY) * size_t(root.cellW) +
                                size_t(cx - root.cellX)];
                for (int index : inCell) {
                    const auto& p = placements[size_t(index)];
                    const uint32_t cg = cacheGroupOf(p);
                    // AddObjectsFromLayerElement 0x02E3CB62: the first group in
                    // the leaf's list with a matching CacheGroup and fewer than
                    // 64 source objects; otherwise a new group PUSHED ON THE
                    // FRONT (0x02E3CD19..0x02E3CD37).
                    int target = -1;
                    for (int candidate : nodes[size_t(leaf)].groups)
                        if (groups[size_t(candidate)].cacheGroup == cg &&
                            groups[size_t(candidate)].objects.size() < 64) {
                            target = candidate;
                            break;
                        }
                    if (target < 0) {
                        DetailGroup created;
                        created.cacheGroup = cg;
                        groups.push_back(created);
                        target = int(groups.size()) - 1;
                        nodes[size_t(leaf)].groups.insert(
                            nodes[size_t(leaf)].groups.begin(), target);
                    }
                    groups[size_t(target)].objects.push_back(p);
                }
            }
        }
    }

    // --- 3. Prune empty nodes ------------------------------------------------
    // UpdateDynamicArea deletes a child that came back empty (0x02E3F9CA /
    // 0x02E3FCC9 -> CQuadTreeElement::IsValid 0x02E3A730). IsValid returns true
    // for static load info (+0x34 > 0), any child, or a local cache-group
    // collection (+0x04); it returns false only for a completely empty node.
    // During authoring no static load info exists yet, so this subtree-content
    // test is the exact writer-side polarity.
    auto prune = [&](auto&& self, int at) -> bool {
        bool any = !nodes[size_t(at)].groups.empty();
        for (int q = 0; q < 4; ++q) {
            const int c = nodes[size_t(at)].child[q];
            if (c < 0) continue;
            if (self(self, c)) any = true;
            else nodes[size_t(at)].child[q] = -1;
        }
        return any;
    };
    if (!prune(prune, 0))
        return buildEmptyLocalDetailSection(sectionOffset, fileBlockAlignment, rootHeader);

    // --- 4. Group payloads and spheres ---------------------------------------
    for (auto& g : groups) {
        if (g.objects.empty()) continue;
        const auto decoded = encodeGroupContents(palette, g.objects);
        g.uncompressedSize = decoded.size();
        g.frame = forge::lzo::compressFramed(decoded);
        // CObjectCacheGroupCollection::LoadHeader copies disk +0x0c..+0x1b to
        // object +0x08..+0x17. StaticUpdate @ 0x00BDEB50 subtracts the camera
        // XYZ from the first three floats and subtracts the fourth from the
        // resulting distance: a world-space bounding sphere, not C2DBounds.
        float minX = g.objects.front().x, minY = g.objects.front().y;
        float minZ = g.objects.front().z, maxX = minX, maxY = minY, maxZ = minZ;
        for (const auto& p : g.objects) {
            minX = std::min(minX, p.x); minY = std::min(minY, p.y);
            minZ = std::min(minZ, p.z);
            maxX = std::max(maxX, p.x); maxY = std::max(maxY, p.y);
            maxZ = std::max(maxZ, p.z + 2.0f);
        }
        g.sphere[0] = (minX + maxX) * 0.5f;
        g.sphere[1] = (minY + maxY) * 0.5f;
        g.sphere[2] = (minZ + maxZ) * 0.5f;
        const float dx = (maxX - minX) * 0.5f, dy = (maxY - minY) * 0.5f;
        const float dz = (maxZ - minZ) * 0.5f;
        g.sphere[3] = std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    calcNodeBounds(nodes, groups, 0);
    // CalcBoundingSphereAndFadeDistanceFromChildren owns the root sphere too;
    // do not preserve the caller's map-wide seed over the computed object bound.
    rootHeader.maxFade = nodes[0].maxFade;
    rootHeader.primitiveMask = nodes[0].mask;

    // --- 5. File blocks ------------------------------------------------------
    assignFileBlocks(nodes, groups, 0, /*forceOwn=*/true,
                     long(kLocalDetailMaxFileBlockSize));

    EmptyLocalDetailSection out;
    out.descriptorOffset = sectionOffset;
    out.bytes.resize(12, 0);   // GenerateStaticMapEntry reserves this first.
    DetailStream stream{out.bytes, sectionOffset, sectionOffset + out.bytes.size()};
    stream.alignTo(fileBlockAlignment);
    out.fileBlockOffset = stream.cursor;
    saveTree(stream, nodes, groups, 0, fileBlockAlignment);

    out.headerOffset = stream.cursor;
    rootHeader.fileBlockPos = nodes[0].fbPos;
    rootHeader.fileBlockSize = nodes[0].fbSize;
    rootHeader.offsetIntoFileBlock = nodes[0].offIn;
    const auto header = serializeLocalDetailQuadHeader(rootHeader);
    stream.put(header.data(), header.size());
    // Palette records below contain global mesh bank IDs.  The retail loader
    // interprets them as three strings when this discriminator is zero.
    {
        std::vector<uint8_t> tail;
        tail.push_back(1);
        appendU32(tail, static_cast<uint32_t>(palette.size()));
        for (const auto& type : palette) {
            appendU32(tail, type.meshIdx);
            appendU32(tail, type.zspriteMeshIdx);
            appendU32(tail, type.shadowMeshIdx);
            bool exact = false;
            for (uint32_t v : type.runtimeSettings) exact = exact || v != 0;
            if (exact) {
                for (uint32_t v : type.runtimeSettings) appendU32(tail, v);
            } else {
                appendF32(tail, type.fadeEnd);
                appendF32(tail, type.fadeStart);
                tail.resize(tail.size() + 0x2c, 0);
            }
        }
        stream.put(tail.data(), tail.size());
    }
    out.headerSpan = stream.cursor - out.headerOffset;
    if (out.headerOffset > size_t(INT32_MAX) || out.headerSpan > size_t(INT32_MAX))
        throw std::runtime_error("local-detail header exceeds s32");

    const auto descriptor = serializeLocalDetailRootDescriptor(
        static_cast<int32_t>(out.headerOffset),
        static_cast<int32_t>(out.headerSpan), true);
    std::copy(descriptor.begin(), descriptor.end(), out.bytes.begin());
    return out;
}

// Editor-authored multi-material foreground. Retail demonstrates that several
// layers can be composited per patch --
// Darkwood_3 frame 0 carries 4 layers with different texture tuples
// (4175/4175/0, 4185/4185/4304, ...) blended through the per-vertex `blend`
// byte, and varies cliffU/cliffV per vertex. A single opaque layer with
// blend=255 everywhere (buildSingleMaterialForeground) paints the whole map in
// one texture.
//
// This function's layer 0 is the opaque base. Each extra layer follows an
// explicit Forge policy: `slope` uses terrain gradient and `height` uses
// normalised elevation. These thresholds are not attributed to retail. Layers whose
// tuple has textures[0] == 0 are skipped, so callers can supply 1, 2 or 3.
std::vector<ForegroundFrame> buildLayeredForeground(
    const terrain::Heightfield& hf, int worldX, int worldY,
    const terrain::TerrainMaterialTuple& base,
    const terrain::TerrainMaterialTuple& slopeMaterial,
    const terrain::TerrainMaterialTuple& heightMaterial) {
    if (hf.width() <= 0 || hf.height() <= 0 ||
        hf.width() % 16 != 0 || hf.height() % 16 != 0)
        throw std::runtime_error("foreground heightfield dimensions must be positive multiples of 16");
    if (base.textures[0] == 0)
        throw std::runtime_error("foreground base texture id must be non-zero");

    std::array<bool, 512> allTriangles;
    allTriangles.fill(true);
    const terrain::LayerTopology topology = terrain::buildLayerTopology(allTriangles);
    if (!topology.fullPatch || topology.vertices.size() != 17u * 17u)
        throw std::runtime_error("full-patch topology generation failed");

    float minH = hf.at(0, 0), maxH = minH;
    for (int y = 0; y <= hf.height(); ++y)
        for (int x = 0; x <= hf.width(); ++x) {
            minH = std::min(minH, hf.at(x, y));
            maxH = std::max(maxH, hf.at(x, y));
        }
    const float span = std::max(1e-3f, maxH - minH);

    auto gradient = [&](int x, int y) {
        const int xl = std::max(0, x - 1), xr = std::min(hf.width(), x + 1);
        const int yn = std::max(0, y - 1), ys = std::min(hf.height(), y + 1);
        const float dx = (hf.at(xr, y) - hf.at(xl, y)) / float(xr - xl);
        const float dy = (hf.at(x, ys) - hf.at(x, yn)) / float(ys - yn);
        return std::sqrt(dx * dx + dy * dy);
    };
    // Engine-ported per-vertex data. The previous hand-made gradient (with a
    // 0.45f upward damping factor) was a symptom patch for terrain that read as
    // black; the actual cause was cliffU/cliffV carrying slope/height instead of
    // the direction-mask normal, which zeroes the blend table outside its
    // diamond. Both are now the engine's own algorithms.
    const HeightSampler sampleHeight = clampedHeightSampler(hf);
    auto normalAt = [&](int x, int y) {
        return packMapNormal(sampleHeight, x, y);
    };
    auto toByte = [](float v) {
        const float c = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        return static_cast<uint8_t>(c * 255.0f + 0.5f);
    };

    struct LayerSpec { const terrain::TerrainMaterialTuple* material; int weight; uint8_t mapping; };
    std::vector<LayerSpec> specs;
    specs.push_back({&base, 0, 0});
    // Mapping 0 is the upward-facing blend table. Retail emits cliff material
    // with mappings 1..4 so every horizontal normal direction has a non-zero
    // table; emitting only mapping 2 leaves the other three cliff faces hard
    // black. See Darkwood_3, where the same rock tuple is repeated for all four
    // directions on every steep patch.
    if (slopeMaterial.textures[0] != 0)
        for (uint8_t mapping = 1; mapping <= 4; ++mapping)
            specs.push_back({&slopeMaterial, 1, mapping});
    // Height is a material weight, not a direction. It remains upward-mapped.
    if (heightMaterial.textures[0] != 0) specs.push_back({&heightMaterial, 2, 0});

    std::vector<ForegroundFrame> frames;
    frames.reserve(size_t(hf.width() / 16) * size_t(hf.height() / 16));
    for (int patchY = 0; patchY < hf.height(); patchY += 16) {
        for (int patchX = 0; patchX < hf.width(); patchX += 16) {
            ForegroundFrame frame;
            for (const LayerSpec& spec : specs) {
                ForegroundLayer layer;
                layer.mappingDirection = spec.mapping;
                layer.textures[0] = spec.material->textures[0];
                layer.textures[1] = spec.material->textures[1];
                layer.textures[2] = spec.material->textures[2];
                layer.textureMaxSize = spec.material->textureMaxSize;
                layer.bumpMaxSize = spec.material->bumpMaxSize;
                layer.selfIllumination = spec.material->selfIllumination;
                layer.sharedIndexBuffer = topology.fullPatch;
                layer.polygonCount = static_cast<uint16_t>(topology.indices.size() - 2);
                if (!layer.sharedIndexBuffer) layer.indices = topology.indices;
                layer.vertices.reserve(topology.vertices.size());
                bool anyCoverage = spec.weight == 0;
                for (const auto& coord : topology.vertices) {
                    const int x = patchX + coord.x;
                    const int y = patchY + coord.y;
                    ForegroundVertex vertex;
                    vertex.x = static_cast<uint16_t>(worldX + x);
                    vertex.y = static_cast<uint16_t>(worldY + y);
                    vertex.height = hf.at(x, y);
                    vertex.packedNormal = normalAt(x, y);
                    const float norm = (hf.at(x, y) - minH) / span;
                    const float steep = gradient(x, y);
                    if (spec.weight == 0) {
                        vertex.blend = 255;
                    } else if (spec.weight == 1) {
                        // rock takes over as the gradient passes ~0.35 units/cell
                        const float w = (steep - 0.15f) / 0.35f;
                        vertex.blend = toByte(w);
                        if (vertex.blend > 8) anyCoverage = true;
                    } else {
                        const float w = (norm - 0.55f) / 0.35f;
                        vertex.blend = toByte(w);
                        if (vertex.blend > 8) anyCoverage = true;
                    }
                    // cliffU/cliffV are the X and Y of the direction-mask normal,
                    // NOT slope/height: the foreground vertex shader samples the
                    // blend table at (cliffU/255, cliffV/255) and the table is 0
                    // outside its diamond mask, so a wrong quantity renders black.
                    const DirectionNormal dir =
                        buildMapDirMask(sampleHeight, worldX, worldY, x, y);
                    vertex.cliffU = packDirMaskByte(dir.x);
                    vertex.cliffV = packDirMaskByte(dir.y);
                    layer.vertices.push_back(vertex);
                }
                if (!anyCoverage) continue; // layer contributes nothing here
                frame.layers.push_back(std::move(layer));
            }
            frames.push_back(std::move(frame));
        }
    }
    return frames;
}

std::vector<ForegroundFrame> buildSingleMaterialForeground(
    const terrain::Heightfield& hf, int worldX, int worldY,
    const terrain::TerrainMaterialTuple& material) {
    if (hf.width() <= 0 || hf.height() <= 0 ||
        hf.width() % 16 != 0 || hf.height() % 16 != 0)
        throw std::runtime_error("foreground heightfield dimensions must be positive multiples of 16");
    if (worldX < 0 || worldY < 0 ||
        worldX + hf.width() > 0xffff || worldY + hf.height() > 0xffff)
        throw std::runtime_error("foreground world coordinates exceed UWORD range");
    if (material.textures[0] == 0)
        throw std::runtime_error("foreground base texture id must be non-zero");

    std::array<bool, 512> allTriangles;
    allTriangles.fill(true);
    const terrain::LayerTopology topology = terrain::buildLayerTopology(allTriangles);
    if (!topology.fullPatch || topology.vertices.size() != 17u * 17u)
        throw std::runtime_error("full-patch topology generation failed");

    const HeightSampler sampleHeight = clampedHeightSampler(hf);
    auto normalAt = [&](int x, int y) {
        return packMapNormal(sampleHeight, x, y);
    };

    std::vector<ForegroundFrame> frames;
    frames.reserve(size_t(hf.width() / 16) * size_t(hf.height() / 16));
    for (int patchY = 0; patchY < hf.height(); patchY += 16) {
        for (int patchX = 0; patchX < hf.width(); patchX += 16) {
            ForegroundLayer layer;
            layer.mappingDirection = 0;
            layer.textures[0] = material.textures[0];
            layer.textures[1] = material.textures[1];
            layer.textures[2] = material.textures[2];
            layer.textureMaxSize = material.textureMaxSize;
            layer.bumpMaxSize = material.bumpMaxSize;
            layer.selfIllumination = material.selfIllumination;
            layer.sharedIndexBuffer = topology.fullPatch;
            layer.polygonCount = static_cast<uint16_t>(topology.indices.size() - 2);
            if (!layer.sharedIndexBuffer) layer.indices = topology.indices;
            layer.vertices.reserve(topology.vertices.size());
            for (const auto& coord : topology.vertices) {
                const int x = patchX + coord.x;
                const int y = patchY + coord.y;
                ForegroundVertex vertex;
                vertex.x = static_cast<uint16_t>(worldX + x);
                vertex.y = static_cast<uint16_t>(worldY + y);
                vertex.height = hf.at(x, y);
                vertex.packedNormal = normalAt(x, y);
                vertex.blend = 255;
                const DirectionNormal dir =
                    buildMapDirMask(sampleHeight, worldX, worldY, x, y);
                vertex.cliffU = packDirMaskByte(dir.x);
                vertex.cliffV = packDirMaskByte(dir.y);
                layer.vertices.push_back(vertex);
            }
            ForegroundFrame frame;
            frame.layers.push_back(std::move(layer));
            frames.push_back(std::move(frame));
        }
    }
    return frames;
}

std::vector<ForegroundFrame> buildThemedForeground(
    const terrain::Heightfield& hf, int worldX, int worldY,
    const std::vector<terrain::TerrainThemeMaterial>& themes,
    const ThemeBlendSampler& themeAt) {
    if (hf.width() <= 0 || hf.height() <= 0 ||
        hf.width() % 16 != 0 || hf.height() % 16 != 0)
        throw std::runtime_error("themed foreground dimensions must be positive multiples of 16");
    if (worldX < 0 || worldY < 0 || worldX + hf.width() > 0xffff ||
        worldY + hf.height() > 0xffff)
        throw std::runtime_error("themed foreground world coordinates exceed UWORD range");
    if (!themeAt) throw std::invalid_argument("themed foreground requires a theme sampler");

    static constexpr int triangleOffsets[2][2][3][2] = {
        {{{0,0},{1,0},{1,1}},{{0,0},{0,1},{1,1}}},
        {{{0,0},{1,0},{0,1}},{{1,0},{0,1},{1,1}}},
    };
    auto sameMaterial=[](const terrain::TerrainMaterialTuple& a,
                         const terrain::TerrainMaterialTuple& b) {
        return a.textures==b.textures && a.textureMaxSize==b.textureMaxSize &&
               a.bumpMaxSize==b.bumpMaxSize &&
               a.selfIllumination==b.selfIllumination;
    };
    const HeightSampler sampleHeight=clampedHeightSampler(hf);
    auto directionActive=[&](uint8_t direction,int x,int y) {
        const auto normal=buildMapDirMask(sampleHeight,worldX,worldY,x,y);
        const float topness=std::clamp(
            (std::asin(std::clamp(normal.z,-1.0f,1.0f)) /
                 (3.14159265358979323846f*0.5f)-0.5f)*4.0f,0.0f,1.0f);
        if(direction==0)return topness>0.0f;
        if(direction>4||topness==1.0f)return false;
        const float length=std::sqrt(normal.x*normal.x+normal.y*normal.y);
        if(length==0.0f)return false;
        static constexpr float dirs[5][2]={{0,0},{0,-1},{0,1},{-1,0},{1,0}};
        const float dot=std::clamp(normal.x/length*dirs[direction][0]+
                                   normal.y/length*dirs[direction][1],-1.0f,1.0f);
        const float sideness=std::clamp(
            1.0f-2.0f*(std::acos(dot)/(3.14159265358979323846f*0.5f)-0.25f),
            0.0f,1.0f);
        return (1.0f-topness)*sideness>0.0f;
    };
    struct Pass {
        terrain::TerrainMaterialTuple material;
        uint8_t direction=0;
        std::array<uint8_t,17*17> blend{};
        std::array<bool,512> triangles{};
    };
    std::vector<ForegroundFrame> frames;
    frames.reserve(size_t(hf.width()/16)*size_t(hf.height()/16));
    for(int patchY=0;patchY<hf.height();patchY+=16)
      for(int patchX=0;patchX<hf.width();patchX+=16) {
        std::vector<Pass> passes;
        for(int y=0;y<=16;++y)for(int x=0;x<=16;++x)
          for(const auto& contribution:terrain::buildThemeContributions(
                  themeAt(patchX+x,patchY+y),themes)) {
            auto pass=std::find_if(passes.begin(),passes.end(),[&](const Pass& value){
                return value.direction==contribution.mappingDirection &&
                       sameMaterial(value.material,contribution.material);
            });
            if(pass==passes.end()) {
                passes.push_back({});pass=std::prev(passes.end());
                pass->material=contribution.material;
                pass->direction=contribution.mappingDirection;
            }
            pass->blend[size_t(y)*17+x]=contribution.blend;
          }
        // CLandscapeLayerMesh compositing requires an opaque colour layer below
        // every sparse material/direction coat.  Theme blending can otherwise
        // partition all mapping-0 passes, leaving triangles for which no pass
        // produces colour (the black wedges seen in the BWSMarket import).
        // The first top-facing material is the deterministic patch base; its
        // original weights remain represented by the later material overlays.
        auto opaqueBase=std::find_if(passes.begin(),passes.end(),[](const Pass& pass){
            return pass.direction==0;
        });
        if(opaqueBase!=passes.end())opaqueBase->blend.fill(255);
        for(auto& pass:passes)
          for(int x=0;x<16;++x)for(int y=0;y<16;++y)for(int triangle=0;triangle<2;++triangle) {
            bool contributes=false;
            // Mapping 0 is the opaque/base terrain pass. It must retain full
            // geometric coverage even where the direction mask reads as a
            // cliff; the directional passes coat that base in the shader.
            // Culling base triangles by topness creates literal black holes on
            // steep authored terrain because no color pass reaches the pixel.
            bool faces=pass.direction==0;
            for(int corner=0;corner<3;++corner) {
                const int vx=x+triangleOffsets[(x^y)&1][triangle][corner][0];
                const int vy=y+triangleOffsets[(x^y)&1][triangle][corner][1];
                contributes|=pass.blend[size_t(vy)*17+vx]!=0;
                faces|=directionActive(pass.direction,patchX+vx,patchY+vy);
            }
            pass.triangles[size_t((x*16+y)*2+triangle)]=
                (opaqueBase != passes.end() && &pass == &*opaqueBase) ||
                (contributes && faces);
          }
        ForegroundFrame frame;
        for(const auto& pass:passes) {
            const auto topology=terrain::buildLayerTopology(pass.triangles);
            if(topology.vertices.empty())continue;
            ForegroundLayer layer;
            layer.mappingDirection=pass.direction;
            for(int i=0;i<3;++i)layer.textures[i]=pass.material.textures[i];
            layer.textureMaxSize=pass.material.textureMaxSize;
            layer.bumpMaxSize=pass.material.bumpMaxSize;
            layer.selfIllumination=pass.material.selfIllumination;
            layer.sharedIndexBuffer=topology.fullPatch;
            layer.polygonCount=uint16_t(topology.indices.size()-2);
            if(!layer.sharedIndexBuffer)layer.indices=topology.indices;
            layer.vertices.reserve(topology.vertices.size());
            for(const auto& coord:topology.vertices) {
                const int x=patchX+coord.x,y=patchY+coord.y;
                ForegroundVertex vertex;
                vertex.x=uint16_t(worldX+x);vertex.y=uint16_t(worldY+y);
                vertex.height=sampleHeight(x,y);vertex.packedNormal=packMapNormal(sampleHeight,x,y);
                vertex.blend=pass.blend[size_t(coord.y)*17+coord.x];
                const auto direction=buildMapDirMask(sampleHeight,worldX,worldY,x,y);
                vertex.cliffU=packDirMaskByte(direction.x);
                vertex.cliffV=packDirMaskByte(direction.y);
                layer.vertices.push_back(vertex);
            }
            frame.layers.push_back(std::move(layer));
        }
        frames.push_back(std::move(frame));
      }
    return frames;
}

const TerrainSectionPlacement* TerrainChunkLayout::find(
    std::string_view wanted) const {
    for (const auto& section : sections)
        if (section.name == wanted) return &section;
    return nullptr;
}

TerrainChunkLayout layoutTerrainSections(
    const std::vector<TerrainSectionInput>& inputs, uint8_t paddingByte) {
    TerrainChunkLayout out;
    for (const auto& input : inputs) {
        if (input.name.empty())
            throw std::runtime_error("terrain section needs a name");
        if (out.find(input.name))
            throw std::runtime_error("duplicate terrain section name: " + input.name);
        const size_t alignment = input.alignment == 0 ? 1 : input.alignment;
        if ((alignment & (alignment - 1)) != 0)
            throw std::runtime_error("terrain section alignment must be a power of two");
        const size_t offset = (out.bytes.size() + alignment - 1) & ~(alignment - 1);
        out.bytes.resize(offset, paddingByte);
        out.sections.push_back({input.kind, input.name, offset, input.bytes.size()});
        out.bytes.insert(out.bytes.end(), input.bytes.begin(), input.bytes.end());
    }
    return out;
}

stbinfo::StaticMapInfoBlock makeTerrainInfoBlock(
    const TerrainChunkLayout& layout,
    std::string_view landscapeSection,
    std::string_view localDetailSection,
    int32_t bankFileIndex,
    int32_t mapWidth, int32_t mapHeight,
    int32_t worldX, int32_t worldY,
    float minZ, float maxZ,
    int32_t versionID, int32_t quality) {
    const auto* landscape = layout.find(landscapeSection);
    const auto* detail = layout.find(localDetailSection);
    if (!landscape || !detail)
        throw std::runtime_error("terrain InfoBlock section is missing");
    if (mapWidth <= 0 || mapHeight <= 0 || minZ > maxZ)
        throw std::runtime_error("invalid terrain InfoBlock bounds");
    if (landscape->offset > size_t(INT32_MAX) || detail->offset > size_t(INT32_MAX) ||
        layout.bytes.size() > size_t(INT32_MAX))
        throw std::runtime_error("terrain chunk exceeds signed pointer range");

    stbinfo::StaticMapInfoBlock ib;
    ib.versionID = versionID;
    ib.bankFileIndex = bankFileIndex;
    ib.mapWidth = mapWidth;
    ib.mapHeight = mapHeight;
    ib.worldX = worldX;
    ib.worldY = worldY;
    ib.landscapeMapPtr = static_cast<int32_t>(landscape->offset);
    ib.localDetailMapPtr = static_cast<int32_t>(detail->offset);
    ib.quality = quality;
    ib.cameraMapBounds[0] = float(worldX);
    ib.cameraMapBounds[1] = float(worldY);
    ib.cameraMapBounds[2] = minZ;
    ib.cameraMapBounds[3] = float(worldX + mapWidth);
    ib.cameraMapBounds[4] = float(worldY + mapHeight);
    ib.cameraMapBounds[5] = maxZ;
    ib.headerEndPtr = static_cast<int32_t>(layout.bytes.size());
    return ib;
}

std::vector<uint8_t> buildTerrainCommonRecord(
    const stbinfo::StaticMapInfoBlock& chunkInfo,
    const std::vector<uint8_t>& chunk,
    bool includeLocalDetail,
    bool includeLandscape) {
    constexpr uint32_t kLandscape = 0x5c;
    constexpr uint32_t kLandscapeEnd = 0x70;
    constexpr uint32_t kLocalDetail = 0x71;
    constexpr uint32_t kLocalDetailEnd = 0x7d;
    if (chunkInfo.landscapeMapPtr < 0 || chunkInfo.localDetailMapPtr < 0)
        throw std::runtime_error("negative terrain control descriptor offset");
    const size_t landscape = size_t(chunkInfo.landscapeMapPtr);
    const size_t local = size_t(chunkInfo.localDetailMapPtr);
    if (landscape + 20 > chunk.size() || local + 12 > chunk.size())
        throw std::runtime_error("terrain control descriptor outside chunk");
    auto read32 = [&](size_t at) {
        uint32_t value = 0; std::memcpy(&value, chunk.data() + at, 4); return value;
    };
    const uint32_t foregroundPos = read32(landscape + 8);
    const uint32_t backgroundPos = read32(landscape + 12);
    const uint32_t backgroundSpan = read32(landscape + 16);
    const uint32_t localHeaderPos = read32(local);
    const uint32_t localHeaderSpan = read32(local + 4);
    const uint32_t localPresent = read32(local + 8);
    if (uint64_t(localHeaderPos) + localHeaderSpan > chunk.size())
        throw std::runtime_error("local-detail common header source outside chunk");
    const uint32_t recordEnd = kLocalDetailEnd + localHeaderSpan;

    auto info = chunkInfo;
    info.landscapeMapPtr = kLandscape;
    info.localDetailMapPtr = kLocalDetail;
    info.headerEndPtr = recordEnd;
    std::vector<uint8_t> record(recordEnd, 0);
    const auto encoded = stbinfo::writeInfoBlock(info);
    std::copy(encoded.begin(), encoded.end(), record.begin());
    std::memcpy(record.data() + kLandscape, &kLandscapeEnd, sizeof(kLandscapeEnd));
    const uint32_t landscapePresent = includeLandscape ? 1u : 0u;
    std::memcpy(record.data() + 0x60, &landscapePresent, 4);
    std::memcpy(record.data() + 0x64, &foregroundPos, 4);
    std::memcpy(record.data() + 0x68, &backgroundPos, 4);
    std::memcpy(record.data() + 0x6c, &backgroundSpan, 4);
    record[0x70] = 1;
    std::memcpy(record.data() + kLocalDetail, &kLocalDetailEnd, sizeof(kLocalDetailEnd));
    std::memcpy(record.data() + 0x75, &localHeaderSpan, 4);
    record[0x79] = includeLocalDetail && localPresent ? 1 : 0;
    std::copy_n(chunk.begin() + localHeaderPos, localHeaderSpan,
                record.begin() + kLocalDetailEnd);
    return record;
}

TerrainChunk64Result buildTerrainChunk64(
    const terrain::Heightfield& hf,
    int worldX, int worldY, int bankFileIndex,
    const terrain::TerrainMaterialTuple& foregroundMaterial,
    const std::vector<std::string>& foregroundTextureNames,
    const InlineTexture& backgroundTexture,
    size_t alignment,
    const terrain::TerrainMaterialTuple& slopeMaterial,
    const terrain::TerrainMaterialTuple& heightMaterial,
    bool authorFoliage,
    const std::filesystem::path& meshBank,
    bool foliageSubsections,
    const std::vector<terrain::TerrainThemeMaterial>& themeMaterials,
    const std::function<terrain::ThemeBlend(int,int)>& themeAt,
    const BackgroundTextureProvider& backgroundProvider) {
    if (alignment != 2048)
        throw std::invalid_argument(
            "terrain chunk currently requires FinalAlbion_RT.stb bank alignment 2048");
    const int mapWidth = hf.width(), mapHeight = hf.height();
    if (mapWidth < 16 || mapHeight < 16 || mapWidth % 16 != 0 || mapHeight % 16 != 0)
        throw std::invalid_argument("terrain chunk requires sides that are multiples of 16");
    if (authorFoliage && (mapWidth != 64 || mapHeight != 64))
        throw std::invalid_argument(
            "authored local detail is not yet proven for non-64x64 terrain");
    TerrainChunk64Result out;
    out.landscapeDescriptorOffset = 0x100;
    const size_t paletteOffset = 0;
    out.foregroundDirectoryOffset = 0x800;
    const auto palette = foregroundTextureNames.empty()
        ? serializeExternalTexturePalette()
        : serializeGlobalTexturePalette(foregroundTextureNames);
    if (paletteOffset + palette.size() > out.foregroundDirectoryOffset)
        throw std::runtime_error("foreground texture palette overlaps patch directory");

    if(bool(themeAt)!=!themeMaterials.empty())
        throw std::invalid_argument(
            "terrain chunk themed foreground needs both materials and sampler");
    const auto foreground = themeAt
        ? buildThemedForeground(hf,worldX,worldY,themeMaterials,themeAt)
        : (slopeMaterial.textures[0] != 0 || heightMaterial.textures[0] != 0)
            ? buildLayeredForeground(hf, worldX, worldY, foregroundMaterial,
                                     slopeMaterial, heightMaterial)
            : buildSingleMaterialForeground(hf, worldX, worldY, foregroundMaterial);
    // 36 bytes per 16x16 patch: a 64x64 map's directory fits the first page,
    // a 128x224 one (112 patches) runs past it like retail's does, so the
    // frames start on the next aligned boundary after the directory
    const size_t directoryBytes = (foreground.size() + 1) * 0x24;
    size_t frameStart = 0x1000;
    while (out.foregroundDirectoryOffset + directoryBytes > frameStart) frameStart += 0x1000;
    const auto foregroundLayout = layoutForegroundFrames(foreground, frameStart, alignment);
    const auto directory = generateQuadDir(foregroundLayout.entries);
    if (out.foregroundDirectoryOffset + directory.size() > frameStart)
        throw std::runtime_error("foreground directory exceeds its reserved pages");
    out.chunk.resize(frameStart, 0);
    std::copy(palette.begin(), palette.end(), out.chunk.begin() + paletteOffset);
    std::copy(directory.begin(), directory.end(),
              out.chunk.begin() + out.foregroundDirectoryOffset);
    out.chunk.insert(out.chunk.end(), foregroundLayout.bytes.begin(),
                     foregroundLayout.bytes.end());

    const size_t backgroundStart = (out.chunk.size() + alignment - 1) & ~(alignment - 1);
    out.chunk.resize(backgroundStart, 0);
    const auto treeShape = buildBackgroundTreeShape(hf, worldX, worldY);
    auto background = layoutBackgroundTree(
        treeShape, hf, worldX, worldY, backgroundTexture,
        backgroundStart, alignment, backgroundProvider);
    out.backgroundRootOffset = background.rootHeaderOffset;
    out.chunk.insert(out.chunk.end(), background.bytes.begin(), background.bytes.end());

    out.localDetailDescriptorOffset = out.chunk.size();
    LocalDetailQuadHeader localHeader;
    localHeader.cellBounds[2] = mapWidth; localHeader.cellBounds[3] = mapHeight;
    std::vector<foliage::FoliageType> foliagePalette;
    std::vector<LocalDetailPlacement> foliagePlacements;
    if (authorFoliage) {
        const auto& catalog = foliage::brushCatalog();
        if (catalog.size() < 18)
            throw std::runtime_error("built-in foliage catalog is incomplete");
        foliagePalette = {catalog[0]};
        // The subsection builder needs the blade mesh's authored bounding
        // sphere. Without a bank we leave it unknown and emit no table.
        if (!meshBank.empty()) {
            const auto spheres = foliage::readMeshBoundingSpheres(meshBank);
            for (auto& entry : foliagePalette) {
                const auto found = spheres.find(entry.meshIdx);
                if (found != spheres.end()) entry.meshSphere = found->second;
                // Keep the real sphere for primitive/group bounds while
                // suppressing only the subsection/remap table.
                if (!foliageSubsections) entry.meshSphere.polyCount = 0;
            }
        }
        uint32_t state = 0x5eed1234u;
        auto random01 = [&]() {
            state = state * 1664525u + 1013904223u;
            return float(state >> 8) / float(0x01000000u);
        };
        const HeightSampler foliageHeight = clampedHeightSampler(hf);
        // Grid step 2 gives 0.25 candidates per world unit squared before slope
        // rejection, which lands near retail Darkwood_3's measured 0.215/u2.
        // Step 3 plus the old 240-instance truncation covered only the southern
        // third of the map, and the palette's fade pair caps the draw radius at
        // about 21 world units, so partial coverage reads in game as no grass.
        for (int y = 2; y < 63; y += 2) {
            for (int x = 2; x < 63; x += 2) {
                const float z = hf.at(x, y);
                const float dx = hf.at(std::min(64, x + 1), y) -
                                 hf.at(std::max(0, x - 1), y);
                const float dy = hf.at(x, std::min(64, y + 1)) -
                                 hf.at(x, std::max(0, y - 1));
                if (std::sqrt(dx * dx + dy * dy) > 1.2f) continue;
                LocalDetailPlacement p;
                p.paletteIndex = 0;
                p.x = float(worldX + x) + (random01() - 0.5f) * 1.5f;
                p.y = float(worldY + y) + (random01() - 0.5f) * 1.5f;
                p.z = z;
                p.rotationRadians = random01() * 6.283185307f;
                p.scale = 0.009f + random01() * 0.006f;
                const DirectionNormal n = mapNormal(foliageHeight, x, y);
                p.nx = n.x; p.ny = n.y; p.nz = n.z;
                foliagePlacements.push_back(p);
            }
        }
        // Instances are split into <=32-instance primitives downstream, and a
        // primitive is culled as a unit by its bounding sphere, so order them by
        // 8x8-cell tile first: a batch drawn from one tile has a tight sphere,
        // while row-major order would give every batch a map-wide one.
        std::stable_sort(foliagePlacements.begin(), foliagePlacements.end(),
                         [worldX, worldY](const LocalDetailPlacement& a,
                                          const LocalDetailPlacement& b) {
                             const int ax = int(a.x - float(worldX)) / 8;
                             const int ay = int(a.y - float(worldY)) / 8;
                             const int bx = int(b.x - float(worldX)) / 8;
                             const int by = int(b.y - float(worldY)) / 8;
                             if (ay != by) return ay < by;
                             return ax < bx;
                         });
        // Retail root headers carry a world-space bounding sphere, not integer
        // cell bounds.  A zero/denormal sphere causes StaticUpdate to reject
        // the entire local-detail tree before any group payload is opened.
        localHeader.sphere[0] = float(worldX) + 32.0f;
        localHeader.sphere[1] = float(worldY) + 32.0f;
        float localMinZ = hf.at(0, 0), localMaxZ = localMinZ;
        for (int sy = 0; sy <= 64; ++sy)
            for (int sx = 0; sx <= 64; ++sx) {
                localMinZ = std::min(localMinZ, hf.at(sx, sy));
                localMaxZ = std::max(localMaxZ, hf.at(sx, sy));
            }
        localHeader.sphere[2] = (localMinZ + localMaxZ) * 0.5f;
        const float hz = (localMaxZ - localMinZ) * 0.5f;
        localHeader.sphere[3] = std::sqrt(32.0f * 32.0f + 32.0f * 32.0f + hz * hz);
        localHeader.maxFade = foliagePalette.front().fadeEnd;
        localHeader.primitiveMask = 2;
    }
    const auto localDetail = authorFoliage
        ? buildType0LocalDetailSection(
              out.localDetailDescriptorOffset, alignment, localHeader,
              foliagePalette, foliagePlacements,
              // One world unit per map cell: the placements above are authored
              // at worldX + x for cell x, and the chunk's cameraMapBounds span
              // worldX .. worldX + 64 for 64 cells.
              LocalDetailCellGrid{float(worldX), float(worldY), 1.0f})
        : buildEmptyLocalDetailSection(out.localDetailDescriptorOffset, alignment,
                                       localHeader);
    out.chunk.insert(out.chunk.end(), localDetail.bytes.begin(), localDetail.bytes.end());

    LandscapeRootDescriptor landscape;
    landscape.texturePalettePos = static_cast<int32_t>(paletteOffset);
    landscape.texturePaletteSize = static_cast<int32_t>(palette.size());
    landscape.foregroundHeaderPos = static_cast<int32_t>(out.foregroundDirectoryOffset);
    landscape.backgroundRootPos = static_cast<int32_t>(out.backgroundRootOffset);
    landscape.backgroundRootSize = static_cast<int32_t>(background.treeBlockSpan);
    const auto landscapeBytes = serializeLandscapeRootDescriptor(landscape);
    std::copy(landscapeBytes.begin(), landscapeBytes.end(),
              out.chunk.begin() + out.landscapeDescriptorOffset);

    float minZ = std::numeric_limits<float>::max();
    float maxZ = -std::numeric_limits<float>::max();
    for (int y = 0; y <= mapHeight; ++y)
        for (int x = 0; x <= mapWidth; ++x) {
            const float z = quantizeEngineHeight(hf.at(x, y));
            minZ = std::min(minZ, z);
            maxZ = std::max(maxZ, z);
        }
    out.info.versionID = 1;
    out.info.bankFileIndex = bankFileIndex;
    out.info.mapWidth = mapWidth; out.info.mapHeight = mapHeight;
    out.info.worldX = worldX; out.info.worldY = worldY;
    out.info.landscapeMapPtr = static_cast<int32_t>(out.landscapeDescriptorOffset);
    out.info.localDetailMapPtr = static_cast<int32_t>(out.localDetailDescriptorOffset);
    out.info.quality = 9;
    out.info.cameraMapBounds[0] = float(worldX);
    out.info.cameraMapBounds[1] = float(worldY);
    out.info.cameraMapBounds[2] = minZ;
    out.info.cameraMapBounds[3] = float(worldX + mapWidth);
    out.info.cameraMapBounds[4] = float(worldY + mapHeight);
    // Retail static-map records enclose the quantized landscape exactly at the
    // bottom and reserve 40 world units above its highest sample for camera
    // streaming.  Verified on Darkwood_Filler_08, StartOakValeWest,
    // Greatwood_1, and BowerstoneTavernCellar.
    setRetailCameraHeightBounds(out.info, minZ, maxZ);
    out.info.headerEndPtr = static_cast<int32_t>(out.chunk.size());
    return out;
}
namespace {

uint32_t getU32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
           (uint32_t(p[3]) << 24);
}

float getF32(const uint8_t* p) {
    float f;
    std::memcpy(&f, p, 4);
    return f;
}

} // namespace

size_t Chunk::frameBytes() const {
    size_t n = 0;
    for (const auto& s : segments)
        if (s.kind == SegKind::Frame) n += s.end - s.start;
    return n;
}
size_t Chunk::hdrBytes() const {
    size_t n = 0;
    for (const auto& s : segments)
        if (s.kind == SegKind::Hdr) n += s.end - s.start;
    return n;
}
size_t Chunk::padBytes() const {
    size_t n = 0;
    for (const auto& s : segments)
        if (s.kind == SegKind::Pad) n += s.end - s.start;
    return n;
}

Chunk parseChunk(const std::vector<uint8_t>& data) {
    Chunk c;
    c.raw = data;
    const size_t n = data.size();

    // 1) FRAME anchors: every confirmed standard-LZO1X block.
    std::vector<FramedBlock> frames = walkFramedBlocks(data);

    // 2) Emit segments left-to-right, filling inter-frame gaps as HDR/PAD.
    //    A gap that is entirely zero is PAD; otherwise it is HDR with any
    //    trailing zero run split off as PAD (mirrors chunk_parse.py).
    auto emitGap = [&](size_t s, size_t e) {
        if (s >= e) return;
        size_t k = e;
        while (k > s && data[k - 1] == 0) --k; // trailing zero run
        bool allZero = (k == s);
        if (allZero) {
            c.segments.push_back(Segment{s, e, SegKind::Pad, 0, 0});
        } else {
            c.segments.push_back(Segment{s, k, SegKind::Hdr, 0, 0});
            if (k < e) c.segments.push_back(Segment{k, e, SegKind::Pad, 0, 0});
        }
    };

    size_t cursor = 0;
    for (const auto& f : frames) {
        if (f.frameOffset > cursor) emitGap(cursor, f.frameOffset);
        size_t fend = f.frameOffset + 8 + f.compLen;
        c.frameIndices.push_back(c.segments.size());
        c.segments.push_back(
            Segment{f.frameOffset, fend, SegKind::Frame,
                    uint32_t(f.data.size()), f.compLen});
        cursor = fend;
    }
    if (cursor < n) emitGap(cursor, n);

    return c;
}

std::vector<uint8_t> reserialize(const Chunk& chunk) {
    // Every segment is preserved verbatim from the source bytes; with no edits
    // this reproduces the chunk exactly (the round-trip proof).
    std::vector<uint8_t> out(chunk.raw.size());
    for (const auto& s : chunk.segments)
        std::copy(chunk.raw.begin() + s.start, chunk.raw.begin() + s.end,
                  out.begin() + s.start);
    return out;
}

std::vector<uint8_t> decodeFrame(const Chunk& chunk, size_t frame) {
    if (frame >= chunk.frameIndices.size())
        throw std::out_of_range("forge::stbbake: frame index out of range");
    const Segment& s = chunk.segments[chunk.frameIndices[frame]];
    size_t pos = s.start;
    return forge::lzo::decompressFramed(chunk.raw, pos);
}

ForegroundFrame parseForegroundFrame(const std::vector<uint8_t>& body) {
    size_t pos = 0;
    auto need = [&](size_t count) {
        if (pos + count > body.size())
            throw std::runtime_error("foreground: truncated frame");
    };
    auto read8 = [&]() { need(1); return body[pos++]; };
    auto read16 = [&]() {
        need(2);
        const uint16_t value = uint16_t(body[pos]) | (uint16_t(body[pos + 1]) << 8);
        pos += 2;
        return value;
    };
    auto read32 = [&]() {
        need(4);
        const uint32_t value = getU32(body.data() + pos);
        pos += 4;
        return value;
    };

    ForegroundFrame frame;
    const uint16_t layerCount = read16();
    if (layerCount == 0 || layerCount > 64)
        throw std::runtime_error("foreground: invalid layer count");
    frame.layers.reserve(layerCount);
    for (uint16_t li = 0; li < layerCount; ++li) {
        ForegroundLayer layer;
        const uint16_t vertexCount = read16();
        layer.polygonCount = read16();
        layer.mappingDirection = read8();
        if (vertexCount == 0 || vertexCount > 4096 || layer.mappingDirection > 4)
            throw std::runtime_error("foreground: invalid layer header");
        for (uint32_t& texture : layer.textures) texture = read32();
        layer.sharedIndexBuffer = read8() != 0;
        layer.textureMaxSize = read32();
        layer.bumpMaxSize = read32();
        const uint32_t illuminationBits = read32();
        std::memcpy(&layer.selfIllumination, &illuminationBits, 4);
        layer.vertices.reserve(vertexCount);
        for (uint16_t vi = 0; vi < vertexCount; ++vi) {
            ForegroundVertex vertex;
            vertex.x = read16();
            vertex.y = read16();
            const uint32_t heightBits = read32();
            std::memcpy(&vertex.height, &heightBits, 4);
            vertex.packedNormal = read32();
            vertex.blend = read8();
            vertex.cliffU = read8();
            vertex.cliffV = read8();
            layer.vertices.push_back(vertex);
        }
        if (!layer.sharedIndexBuffer) {
            layer.indices.reserve(size_t(layer.polygonCount) + 2);
            for (size_t ii = 0; ii < size_t(layer.polygonCount) + 2; ++ii)
                layer.indices.push_back(read16());
        }
        frame.layers.push_back(std::move(layer));
    }
    frame.hasWater = read8() != 0;
    frame.waterPayload.assign(body.begin() + pos, body.end());
    return frame;
}

std::vector<uint8_t> serializeForegroundFrame(const ForegroundFrame& frame) {
    if (frame.layers.empty() || frame.layers.size() > 64)
        throw std::invalid_argument("foreground: invalid layer count");
    std::vector<uint8_t> out;
    auto put8 = [&](uint8_t value) { out.push_back(value); };
    auto put16 = [&](uint16_t value) {
        out.push_back(uint8_t(value)); out.push_back(uint8_t(value >> 8));
    };
    auto put32 = [&](uint32_t value) {
        out.push_back(uint8_t(value)); out.push_back(uint8_t(value >> 8));
        out.push_back(uint8_t(value >> 16)); out.push_back(uint8_t(value >> 24));
    };
    put16(static_cast<uint16_t>(frame.layers.size()));
    for (const auto& layer : frame.layers) {
        if (layer.vertices.empty() || layer.vertices.size() > 0xffff ||
            layer.mappingDirection > 4)
            throw std::invalid_argument("foreground: invalid layer");
        if (!layer.sharedIndexBuffer &&
            layer.indices.size() != size_t(layer.polygonCount) + 2)
            throw std::invalid_argument("foreground: index/polygon count mismatch");
        put16(static_cast<uint16_t>(layer.vertices.size()));
        put16(layer.polygonCount);
        put8(layer.mappingDirection);
        for (uint32_t texture : layer.textures) put32(texture);
        put8(layer.sharedIndexBuffer ? 1 : 0);
        put32(layer.textureMaxSize);
        put32(layer.bumpMaxSize);
        uint32_t illuminationBits = 0;
        std::memcpy(&illuminationBits, &layer.selfIllumination, 4);
        put32(illuminationBits);
        for (const auto& vertex : layer.vertices) {
            put16(vertex.x); put16(vertex.y);
            uint32_t heightBits = 0;
            std::memcpy(&heightBits, &vertex.height, 4);
            put32(heightBits);
            put32(vertex.packedNormal);
            put8(vertex.blend); put8(vertex.cliffU); put8(vertex.cliffV);
        }
        if (!layer.sharedIndexBuffer)
            for (uint16_t index : layer.indices) put16(index);
    }
    put8(frame.hasWater ? 1 : 0);
    out.insert(out.end(), frame.waterPayload.begin(), frame.waterPayload.end());
    return out;
}

PatchHeader parsePatchHeader(const std::vector<uint8_t>& b) {
    PatchHeader h;
    if (b.size() < 17) return h;
    auto rd16 = [&](size_t o) { return uint16_t(b[o] | (b[o + 1] << 8)); };
    h.pw = rd16(0);
    h.ph = rd16(2);
    h.coord0 = rd16(4);
    h.coord1 = rd16(6);
    h.isWaterOnly = b[8] != 0;
    h.detailMode = b[9];
    h.indexCount = rd16(10);
    h.vertexCount = rd16(12);
    h.texExtX = b[14];
    h.texExtY = b[15];
    h.isDXT = b[16] != 0;
    // The §3 grid invariant: a real foreground/local-detail patch has one vertex
    // per grid corner. Dead-on for the donor's patches (pw=ph=16 -> 17*17=289);
    // false for the tree-node background-LOD frames -> serves as the classifier.
    h.valid = !h.isWaterOnly &&
              (uint32_t(h.pw + 1) * uint32_t(h.ph + 1) == h.vertexCount);
    return h;
}

// --- Patch authoring primitives --------------------------------------------
uint32_t packNormal(float nx, float ny, float nz) {
    // 11/11/10 signed, matching EgoCore PackNormal (engine-loadable). Round to
    // nearest, then mask to field widths and pack ix | iy<<11 | iz<<22.
    auto r = [](float v) -> int { return int(std::floor(v + 0.5f)); };
    uint32_t ix = uint32_t(r(nx * 1023.0f)) & 0x7FF;
    uint32_t iy = uint32_t(r(ny * 1023.0f)) & 0x7FF;
    uint32_t iz = uint32_t(r(nz * 511.0f))  & 0x3FF;
    return ix | (iy << 11) | (iz << 22);
}

float quantizeEngineHeight(float rawHeight) {
    // QuantiseLandscapeHeight calls GFFloatToLongNear, whose complete body is
    // FLD; FISTP (FableWin 0x018C4020). Under the engine's normal x87 control
    // word this is round-to-nearest, ties-to-even -- unlike std::round, which
    // rounds halfway cases away from zero.
    return std::nearbyint(rawHeight * 128.0f) * (1.0f / 128.0f);
}

void setRetailCameraHeightBounds(stbinfo::StaticMapInfoBlock& info,
                                 float rawMinHeight, float rawMaxHeight) {
    if (!std::isfinite(rawMinHeight) || !std::isfinite(rawMaxHeight) ||
        rawMinHeight > rawMaxHeight)
        throw std::invalid_argument("invalid terrain camera height bounds");
    info.cameraMapBounds[2] = quantizeEngineHeight(rawMinHeight);
    info.cameraMapBounds[5] = quantizeEngineHeight(rawMaxHeight) + 40.0f;
}

HeightSampler clampedHeightSampler(const terrain::Heightfield& heightfield) {
    const terrain::Heightfield* hf = &heightfield;
    return [hf](int x, int y) {
        const int cx = std::min(std::max(x, 0), std::max(0, hf->width() - 1));
        const int cy = std::min(std::max(y, 0), std::max(0, hf->height() - 1));
        return quantizeEngineHeight(hf->at(cx, cy));
    };
}

namespace {

uint32_t floatBits(float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

float bitsFloat(uint32_t bits) {
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

const std::array<uint32_t, 128>& compilerInvSqrtTable() {
    static const std::array<uint32_t, 128> table = [] {
        std::array<uint32_t, 128> values{};
        for (uint32_t i = 0; i < values.size(); ++i) {
            const float sample = bitsFloat(0x3f000000u | (i << 17));
            // Fable.exe Math_InitializeCosineLookup @0x00A0DB60: FSQRT,
            // extended 1/sqrt, FSTP float, then rounded high mantissa byte.
            const float inverse = float(1.0L / std::sqrt((long double)sample));
            values[i] = ((floatBits(inverse) + 0x2000u) >> 15) & 0xffu;
        }
        return values;
    }();
    return table;
}

long double compilerFastInvSqrt(float squaredLength) {
    const uint32_t bits = floatBits(squaredLength);
    const uint32_t exponent = (bits >> 23) & 0xffu;
    const uint32_t seedExponent = ((0x17cu - exponent) & 0x3feu) << 22;
    const uint32_t tableBits =
        compilerInvSqrtTable()[(bits >> 17) & 0x7fu] << 15;
    const float seed = bitsFloat(seedExponent | tableBits);
    // Fable.exe C2DVector::Normalise @0x00A14540 keeps this Newton step in
    // x87 precision and stores only the multiplied components to float.
    const long double s = seed;
    const long double x = squaredLength;
    return (3.0L - s * s * x) * s * 0.5L;
}

std::pair<float, float> compilerNormalise2(float a, float b) {
    const float squared = float((long double)a * a + (long double)b * b);
    const long double inverse = compilerFastInvSqrt(squared);
    return {float((long double)a * inverse), float((long double)b * inverse)};
}

DirectionNormal compilerNormalise3(float x, float y, float z) {
    const float squared = float((long double)x * x +
                                (long double)y * y +
                                (long double)z * z);
    const long double inverse = compilerFastInvSqrt(squared);
    return {float((long double)x * inverse),
            float((long double)y * inverse),
            float((long double)z * inverse)};
}

} // namespace

DirectionNormal mapNormal(const HeightSampler& sampleHeight, int x, int y,
                          bool compilerFastNormals, bool egoProducerNormals) {
    // CMap::PeekMapNormal. The fixed run is two cells per axis; each axis slope
    // is normalised on its own before the axes are combined.
    auto normalise2 = [](float a, float b) {
        const long double la = a, lb = b;
        const long double len = std::sqrt(la * la + lb * lb);
        return std::pair<float, float>{float(la / len), float(lb / len)};
    };
    const auto normalizeAxis = (compilerFastNormals || egoProducerNormals)
        ? compilerNormalise2 : normalise2;
    const auto horizontal = normalizeAxis(
        sampleHeight(x - 1, y) - sampleHeight(x + 1, y), 2.0f);
    const auto vertical = normalizeAxis(
        sampleHeight(x, y - 1) - sampleHeight(x, y + 1), 2.0f);
    float nx = horizontal.first;
    float ny = vertical.first;
    float nz = egoProducerNormals ? 4.0f
                                  : horizontal.second * vertical.second;
    if (compilerFastNormals || egoProducerNormals)
        return compilerNormalise3(nx, ny, nz);
    const long double lnx = nx, lny = ny, lnz = nz;
    const long double invLen = 1.0L /
        std::sqrt(lnx * lnx + lny * lny + lnz * lnz);
    return DirectionNormal{float(lnx * invLen), float(lny * invLen),
                           float(lnz * invLen)};
}

NativeThresholdTrace traceNativeBackgroundLodThreshold(
    const HeightSampler& sampleHeight, int mapWidth, int mapHeight,
    int x, int y, float meshDetail, bool compilerFastNormals,
    bool egoProducerNormals) {
    if (!sampleHeight || mapWidth < 1 || mapHeight < 1 || meshDetail <= 0.0f)
        throw std::invalid_argument("invalid native threshold trace input");
    NativeThresholdTrace trace;
    uint32_t bit = 1;
    while ((((uint32_t(x) | uint32_t(y)) & bit) == 0) && bit != 0)
        bit <<= 1;
    if (bit == 0) {
        trace.boundarySentinel = true;
        trace.baseZThreshold = 1.0e30f;
        return trace;
    }
    if ((uint32_t(x) & bit) == 0) {
        trace.ax = x; trace.ay = y - int(bit);
        trace.bx = x; trace.by = y + int(bit);
    } else {
        int adx;
        int bdx = int(bit);
        if ((uint32_t(y) & bit) == 0) {
            adx = -int(bit); trace.ay = trace.by = y;
        } else {
            adx = -int(bit);
            if (((uint32_t(x) ^ uint32_t(y)) & (bit << 1)) != 0) {
                bdx = -int(bit); adx = int(bit);
            }
            trace.ay = y - int(bit); trace.by = y + int(bit);
        }
        trace.ax = x + adx; trace.bx = x + bdx;
    }
    if (trace.ax < 0 || trace.ay < 0 || trace.bx < 0 || trace.by < 0 ||
        trace.ax > mapWidth || trace.bx > mapWidth ||
        trace.ay > mapHeight || trace.by > mapHeight) {
        trace.boundarySentinel = true;
        trace.baseZThreshold = 1.0e30f;
        return trace;
    }
    trace.centerHeight = sampleHeight(x, y);
    const float aHeight = sampleHeight(trace.ax, trace.ay);
    const float bHeight = sampleHeight(trace.bx, trace.by);
    trace.midpointHeight = (aHeight + bHeight) / 2.0f;
    trace.midpointError = std::fabs(trace.midpointHeight - trace.centerHeight);
    trace.midpointTerm = trace.midpointError * 800.0f / meshDetail;
    const auto centerNormal = mapNormal(sampleHeight, x, y, compilerFastNormals,
                                        egoProducerNormals);
    const auto aNormal = mapNormal(sampleHeight, trace.ax, trace.ay,
                                   compilerFastNormals, egoProducerNormals);
    const auto bNormal = mapNormal(sampleHeight, trace.bx, trace.by,
                                   compilerFastNormals, egoProducerNormals);
    auto dot = [&](const DirectionNormal& n) {
        return centerNormal.x*n.x + centerNormal.y*n.y + centerNormal.z*n.z;
    };
    trace.dotA = dot(aNormal);
    trace.dotB = dot(bNormal);
    trace.normalError = 1.0f - std::min(trace.dotA, trace.dotB);
    const float dx = float(x - trace.ax), dy = float(y - trace.ay);
    trace.endpointDistance = float(std::sqrt(
        static_cast<long double>(dx) * dx + static_cast<long double>(dy) * dy));
    const float distanceError = float(
        static_cast<long double>(trace.endpointDistance) * trace.normalError);
    trace.normalTerm = distanceError * 800.0f / meshDetail;
    trace.baseZThreshold = std::max(trace.midpointTerm, trace.normalTerm);
    return trace;
}

std::vector<uint8_t> buildNativeBackgroundLodThresholds(
    const terrain::Heightfield& hf, NativeBackgroundLodSettings settings,
    HeightSampler sampleHeight,
    std::vector<NativeThresholdProvenance>* provenance) {
    if (settings.meshDetail <= 0.0f || settings.firstLodZ <= 0.0f)
        throw std::invalid_argument("native background LOD settings must be positive");
    if (settings.staticMapQuality < 0 || settings.staticMapQuality > 8)
        throw std::invalid_argument("native background static-map quality must be 0..8");

    float meshDetail = settings.meshDetail;
    for (int quality = settings.staticMapQuality; quality < 8; ++quality)
        meshDetail *= 2.0f;
    std::array<float, 8> lodZ{};
    float z = settings.firstLodZ;
    for (int band = 2; band < 8; ++band, z *= 2.0f) lodZ[band] = z;

    const int width = hf.width(), height = hf.height();
    std::vector<uint8_t> thresholds(size_t(width + 1) * size_t(height + 1), 0);
    if (provenance)
        provenance->assign(thresholds.size(), NativeThresholdProvenance{});
    auto index = [width](int x, int y) {
        return size_t(y) * size_t(width + 1) + size_t(x);
    };
    // CEngineMap::MapHasDataAtPoint (0x02CCCEE0) accepts coordinates below
    // the engine map's width/height only.  The LEV's trailing vertex row and
    // column are serialization overlap, not owned map samples.  With no
    // CEngineWorldMap neighbour available, PeekLandscapeHeight (0x02CCC750)
    // clamps to width-1/height-1.  A world-aware overload can replace this
    // fallback once the neighbouring LEVs are supplied to the baker.
    if (!sampleHeight) sampleHeight = clampedHeightSampler(hf);
    auto bandAtZ = [&](float value) {
        for (int band = 2; band < 8; ++band)
            if (value < lodZ[band]) return band - 1;
        return 7;
    };
    auto baseThreshold = [&](int x, int y) {
        return traceNativeBackgroundLodThreshold(
            sampleHeight, width, height, x, y, meshDetail,
            settings.compilerFastNormals,
            settings.egoProducerNormals).baseZThreshold;
    };

    std::function<void(int,int)> update = [&](int x, int y) {
        if (x < 0 || y < 0 || x > width || y > height) return;
        const size_t at = index(x, y);
        const int old = thresholds[at];
        int value = old;
        int parentX = -1, parentY = -1;
        NativeThresholdProvenance witness;
        if (old == 0) {
            const float baseZ = baseThreshold(x, y);
            if (provenance) (*provenance)[at].pointBaseZ = baseZ;
            value = std::max(0, bandAtZ(baseZ));
            if (value > old) {
                witness.parentX = witness.sourceX = x;
                witness.parentY = witness.sourceY = y;
                witness.pointBaseZ = baseZ;
                witness.sourceBaseZ = baseZ;
            }
        }
        uint32_t step = 1;
        if (x == 0 && y == 0) {
            if (old < value) {
                thresholds[at] = uint8_t(value);
                if (provenance) (*provenance)[at] = witness;
            }
            return;
        }
        while (((uint32_t(x) & step) == 0) && ((uint32_t(y) & step) == 0))
            step <<= 1;
        auto include = [&](int nx, int ny) {
            if (nx >= 0 && ny >= 0 && nx <= width && ny <= height) {
                const size_t ni = index(nx, ny);
                if (int(thresholds[ni]) > value) {
                    value = int(thresholds[ni]);
                    parentX = nx;
                    parentY = ny;
                    if (provenance) witness = (*provenance)[ni];
                }
            }
        };
        if ((uint32_t(x) & uint32_t(y) & step) == 0) {
            if (step > 1) {
                const int half = int(step / 2);
                include(x-half,y-half); include(x+half,y-half);
                include(x-half,y+half); include(x+half,y+half);
            }
        } else {
            const int s = int(step);
            include(x-s,y); include(x+s,y); include(x,y-s); include(x,y+s);
        }
        if (old >= value) return;
        thresholds[at] = uint8_t(value);
        if (provenance) {
            witness.pointBaseZ = (*provenance)[at].pointBaseZ;
            if (parentX >= 0) {
                witness.parentX = parentX;
                witness.parentY = parentY;
            }
            (*provenance)[at] = witness;
        }
        const int s = int(step);
        if ((uint32_t(x) & step) == 0) {
            update(x-s,y); update(x+s,y);
        } else if ((uint32_t(y) & step) == 0) {
            update(x,y-s); update(x,y+s);
        } else {
            update(x-s,y-s); update(x+s,y-s);
            update(x-s,y+s); update(x+s,y+s);
        }
    };
    for (int y = 0; y <= height; ++y)
        for (int x = 0; x <= width; ++x) update(x, y);
    return thresholds;
}

NativeBackgroundLodTopology buildNativeBackgroundLodTopology(
    const std::vector<uint8_t>& thresholdMap, int mapWidth, int mapHeight,
    int x, int y, int width, int height, uint8_t band,
    int maxSubsectionX, int maxSubsectionY) {
    if (mapWidth < 1 || mapHeight < 1 || x < 0 || y < 0 || width < 1 || height < 1 ||
        x + width > mapWidth || y + height > mapHeight)
        throw std::invalid_argument("native background LOD rectangle is out of range");
    if (band > 7 || maxSubsectionX < 1 || maxSubsectionY < 1)
        throw std::invalid_argument("invalid native background LOD band/subsection extent");
    if (thresholdMap.size() != size_t(mapWidth + 1) * size_t(mapHeight + 1))
        throw std::invalid_argument("native background LOD threshold-map size mismatch");

    NativeBackgroundLodTopology out;
    std::vector<int> ids(size_t(mapWidth + 1) * size_t(mapHeight + 1), -1);
    auto pointIndex = [mapWidth](int px, int py) {
        return size_t(py) * size_t(mapWidth + 1) + size_t(px);
    };
    auto vertex = [&](int px, int py) {
        int& id = ids[pointIndex(px, py)];
        if (id < 0) {
            if (out.vertices.size() >= 65535)
                throw std::runtime_error("native background LOD vertex count exceeds u16");
            id = static_cast<int>(out.vertices.size());
            out.vertices.push_back({uint16_t(px), uint16_t(py)});
        }
        return uint16_t(id);
    };
    auto triangle = [&](auto&& self, int ax, int ay, int bx, int by,
                        int cx, int cy) -> void {
        const int mx = (ax + bx) / 2, my = (ay + by) / 2;
        const bool indivisible = (mx == ax || mx == bx) && (my == ay || my == by);
        if (!indivisible && thresholdMap[pointIndex(mx, my)] >= band) {
            self(self, bx, by, cx, cy, mx, my);
            self(self, cx, cy, ax, ay, mx, my);
            return;
        }
        out.indices.push_back(vertex(ax, ay));
        out.indices.push_back(vertex(bx, by));
        out.indices.push_back(vertex(cx, cy));
    };
    auto rectangle = [&](auto&& self, int rx, int ry, int rw, int rh) -> void {
        if (rh < rw || maxSubsectionX < rw) {
            const int half = rw / 2;
            self(self, rx, ry, half, rh);
            self(self, rx + half, ry, half, rh);
        } else if (rw < rh || maxSubsectionY < rh) {
            const int half = rh / 2;
            self(self, rx, ry, rw, half);
            self(self, rx, ry + half, rw, half);
        } else if (rw == 1) {
            if ((rx & 1) == (ry & 1)) {
                triangle(triangle, rx,ry, rx,ry+1, rx+1,ry+1);
                triangle(triangle, rx,ry, rx+1,ry+1, rx+1,ry);
            } else {
                triangle(triangle, rx,ry, rx,ry+1, rx+1,ry);
                triangle(triangle, rx+1,ry+1, rx+1,ry, rx,ry+1);
            }
        } else {
            const int mx = rx + rw / 2, my = ry + rw / 2;
            triangle(triangle, rx+rw,ry, rx,ry, mx,my);
            triangle(triangle, rx,ry+rw, rx+rw,ry+rw, mx,my);
            triangle(triangle, rx,ry, rx,ry+rw, mx,my);
            triangle(triangle, rx+rw,ry+rw, rx+rw,ry, mx,my);
        }
    };
    rectangle(rectangle, x, y, width, height);
    return out;
}

uint32_t packMapNormal(const HeightSampler& sampleHeight, int x, int y) {
    const DirectionNormal n = mapNormal(sampleHeight, x, y);
    return packNormal(n.x, n.y, n.z);
}

DirectionNormal buildMapDirMask(const HeightSampler& sampleHeight,
                                int worldX, int worldY, int x, int y) {
    const float center = sampleHeight(x, y);
    const float left = sampleHeight(x - 1, y) - center;
    const float right = sampleHeight(x + 1, y) - center;
    const float up = sampleHeight(x, y - 1) - center;
    const float down = sampleHeight(x, y + 1) - center;
    DirectionNormal faces[8]{};
    size_t count = 0;
    if (((worldX + x) ^ (worldY + y)) & 1) {
        faces[0] = {-right, up, 1.0f};
        faces[1] = {-right, -down, 1.0f};
        faces[2] = {left, -down, 1.0f};
        faces[3] = {left, up, 1.0f};
        count = 4;
    } else {
        const float ul = sampleHeight(x - 1, y - 1) - center;
        const float ur = sampleHeight(x + 1, y - 1) - center;
        const float dl = sampleHeight(x - 1, y + 1) - center;
        const float dr = sampleHeight(x + 1, y + 1) - center;
        faces[0] = {up - ur, up, 1.0f};
        faces[1] = {-right, ur - right, 1.0f};
        faces[2] = {-right, right - dr, 1.0f};
        faces[3] = {down - dr, -down, 1.0f};
        faces[4] = {dl - down, -down, 1.0f};
        faces[5] = {left, left - dl, 1.0f};
        faces[6] = {left, ul - left, 1.0f};
        faces[7] = {ul - up, up, 1.0f};
        count = 8;
    }
    DirectionNormal sum{0.0f, 0.0f, 0.0f};
    for (size_t i = 0; i < count; ++i) {
        DirectionNormal& face = faces[i];
        const float invLength = 1.0f / std::sqrt(
            face.x * face.x + face.y * face.y + face.z * face.z);
        face.x *= invLength; face.y *= invLength; face.z *= invLength;
        auto penalty = [](float component) {
            return 0.5f / (std::abs(component) + 0.0625f) + 0.5294118f;
        };
        const float weight = penalty(face.x) * penalty(face.y) * penalty(face.z);
        sum.x += face.x * weight;
        sum.y += face.y * weight;
        sum.z += face.z * weight;
    }
    const float invLength = 1.0f / std::sqrt(
        sum.x * sum.x + sum.y * sum.y + sum.z * sum.z);
    sum.x *= invLength; sum.y *= invLength; sum.z *= invLength;
    return sum;
}

uint8_t packDirMaskByte(float component) {
    return uint8_t(int((component * 0.5f + 0.5f) * 255.0f));
}

std::vector<uint8_t> serializePatchVB(const std::vector<PatchVertex>& verts) {
    std::vector<uint8_t> out;
    out.reserve(verts.size() * 16);
    auto put16 = [&](uint16_t v) { out.push_back(uint8_t(v)); out.push_back(uint8_t(v >> 8)); };
    auto put32 = [&](uint32_t v) {
        out.push_back(uint8_t(v)); out.push_back(uint8_t(v >> 8));
        out.push_back(uint8_t(v >> 16)); out.push_back(uint8_t(v >> 24));
    };
    for (const PatchVertex& v : verts) {
        put16(v.gridX);
        put16(v.gridY);
        uint32_t hbits;
        std::memcpy(&hbits, &v.height, 4);
        put32(hbits);
        put32(v.packedNormal);
        put16(v.uv0);
        put16(v.uv1);
    }
    return out;
}

std::vector<uint8_t> rangeBlockRaw(const uint8_t* elems, size_t count, size_t stride) {
    std::vector<uint8_t> block = forge::rangecodec::encodeRaw(elems, count, stride);
    int32_t compLen = int32_t(block.size());
    std::vector<uint8_t> out;
    out.reserve(4 + block.size());
    out.push_back(uint8_t(compLen)); out.push_back(uint8_t(compLen >> 8));
    out.push_back(uint8_t(compLen >> 16)); out.push_back(uint8_t(compLen >> 24));
    out.insert(out.end(), block.begin(), block.end());
    return out;
}

std::vector<uint8_t> serializePatchHeader(const PatchHeader& h) {
    std::vector<uint8_t> b;
    b.reserve(17);
    auto put16 = [&](uint16_t v) { b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8)); };
    put16(h.pw);
    put16(h.ph);
    put16(h.coord0);
    put16(h.coord1);
    b.push_back(h.isWaterOnly ? 1 : 0);
    b.push_back(h.detailMode);
    put16(h.indexCount);
    put16(h.vertexCount);
    b.push_back(h.texExtX);
    b.push_back(h.texExtY);
    b.push_back(h.isDXT ? 1 : 0);
    return b;
}

// CLandscapeBackgroundPatch::Load allocates the patch texture with a level count
// of literal 1 (`push 1` at 0x00BE81D0), and
// CTexture::LoadFromDataStreamToPreallocatedSurface @0x009FB750 bounds its mip
// loop by that SURFACE level count (vtable +0x34, compared at 0x009FBA4D) --
// NOT by the 19-byte header's `levels` field, which it never reads back. Each
// level consumes exactly bpp*w*h/8 raw stream bytes (0x009FB865: imul w, imul h,
// shr 3), so the engine reads mip 0 and stops.
//
// Retail inline background textures match that exactly: a 64x64 DXT1 patch
// texture declares levels=1 and spans 19+2048 = 2067 bytes. Emitting a full mip
// chain leaves the extra levels unconsumed and shifts the stream cursor short of
// the vertex block, after which the loader reads the edge-strip counts and the
// hasWater EBOOL out of texture bytes -- observed as a bogus 31,171,392-byte
// count reaching CRangeCompressor::Decompress through the water sub-patch path.
InlineTexture singleLevelBackgroundTexture(const InlineTexture& texture) {
    InlineTexture out = texture;
    const bool isDXT = texture.pixelFormat0 == 3u ||
                       (texture.pixelFormat0 & 0xffu) == 3u;
    if (!isDXT)
        return out; // non-DXT background textures are not authored here; leave as-is.
    out.levels = 1;
    // The surface format is the hard-coded 'DXT1' fourcc pushed at 0x00BE8197,
    // i.e. 4 bits per pixel -> mip0 = 4*w*h/8 bytes.
    const size_t mip0 = (size_t(texture.width) * size_t(texture.height)) / 2;
    if (out.mipData.size() > mip0)
        out.mipData.resize(mip0);
    return out;
}

std::vector<uint8_t> serializeInlineTexture(const InlineTexture& texture) {
    std::vector<uint8_t> out;
    out.reserve(19 + texture.mipData.size());
    auto put16 = [&](uint16_t v) {
        out.push_back(uint8_t(v)); out.push_back(uint8_t(v >> 8));
    };
    auto put32 = [&](uint32_t v) {
        out.push_back(uint8_t(v)); out.push_back(uint8_t(v >> 8));
        out.push_back(uint8_t(v >> 16)); out.push_back(uint8_t(v >> 24));
    };
    put16(texture.width);
    put16(texture.height);
    out.push_back(texture.levels);
    put32(texture.pixelFormat0);
    put16(texture.pixelFormat1);
    put32(texture.usage);
    put32(texture.surfacePool);
    out.insert(out.end(), texture.mipData.begin(), texture.mipData.end());
    return out;
}

InlineTexture parseInlineTexture(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 19)
        throw std::invalid_argument("parseInlineTexture: stream shorter than 19-byte header");
    auto get16 = [&](size_t o) {
        return uint16_t(bytes[o]) | (uint16_t(bytes[o + 1]) << 8);
    };
    auto get32 = [&](size_t o) {
        return uint32_t(bytes[o]) | (uint32_t(bytes[o + 1]) << 8) |
               (uint32_t(bytes[o + 2]) << 16) | (uint32_t(bytes[o + 3]) << 24);
    };
    InlineTexture out;
    out.width = get16(0);
    out.height = get16(2);
    out.levels = bytes[4];
    out.pixelFormat0 = get32(5);
    out.pixelFormat1 = get16(9);
    out.usage = get32(11);
    out.surfacePool = get32(15);
    out.mipData.assign(bytes.begin() + 19, bytes.end());
    if (out.width == 0 || out.height == 0 || out.levels == 0 || out.mipData.empty())
        throw std::invalid_argument("parseInlineTexture: invalid dimensions, levels, or payload");
    return out;
}

std::vector<uint8_t> serializeEmptyPatchTrailer(uint16_t bound0,
                                                uint16_t bound1) {
    std::vector<uint8_t> out;
    out.reserve(85);
    auto put16 = [&](uint16_t v) {
        out.push_back(uint8_t(v)); out.push_back(uint8_t(v >> 8));
    };
    auto put32 = [&](uint32_t v) {
        out.push_back(uint8_t(v)); out.push_back(uint8_t(v >> 8));
        out.push_back(uint8_t(v >> 16)); out.push_back(uint8_t(v >> 24));
    };
    for (int edge = 0; edge < 4; ++edge) {
        put16(bound0);
        put16(bound1);
        out.push_back(0); // edge-strip EBOOL
        for (int array = 0; array < 4; ++array) put32(0);
    }
    out.push_back(0); // patch hasWater EBOOL
    return out;
}

PatchBody buildBackgroundPatch(const terrain::Heightfield& hf,
                               int patchX, int patchY, int patchSize,
                               int worldX, int worldY,
                               const InlineTexture& texture) {
    return buildBackgroundPatchRect(
        hf, patchX, patchY, patchSize, patchSize, worldX, worldY, texture);
}

PatchBody buildBackgroundPatchRect(const terrain::Heightfield& hf,
                                   int patchX, int patchY,
                                   int patchWidth, int patchHeight,
                                   int worldX, int worldY,
                                   const InlineTexture& texture) {
    if (patchWidth <= 0 || patchHeight <= 0 ||
        patchWidth > 0xffff || patchHeight > 0xffff || patchX < 0 || patchY < 0 ||
        patchX + patchWidth > hf.width() || patchY + patchHeight > hf.height() ||
        worldX + patchX < 0 || worldY + patchY < 0 ||
        worldX + patchX + patchWidth > 0xffff ||
        worldY + patchY + patchHeight > 0xffff)
        throw std::invalid_argument("buildBackgroundPatchRect: invalid patch bounds/counts");
    // the vertex/index counts that must fit u16 are those of the decimated
    // (<=16x16) grid below, not the world extent: retail carries 128x256 nodes
    PatchBody pb;
    pb.valid = true;
    pb.header.pw = static_cast<uint16_t>(patchWidth);
    pb.header.ph = static_cast<uint16_t>(patchHeight);
    // Retail background patches keep their header/trailer bounds map-local even
    // though the vertex grid is stored in world coordinates.  Feeding world
    // coordinates here makes the landscape cache index a 64-cell map at (3328,
    // 2304), which can walk its activation grid out of bounds.
    pb.header.coord0 = static_cast<uint16_t>(patchX);
    pb.header.coord1 = static_cast<uint16_t>(patchY);
    // LOD DECIMATION. detailMode==1 makes CLandscapeBackgroundPatch::Load
    // (0x00BE860A) index a shared index-buffer table as `table[pw*17 + ph]`, so
    // pw and ph must both be <= 16 -- a 16x16 patch is the largest entry
    // (16*17+16 = 288). Retail obeys this without exception: Darkwood_3 contains
    // only 16x16 (289 verts) and 16x8 (153 verts) patches, and a retail node
    // covering a 32x32 world rectangle still stores a 17x17 vertex grid.
    // Emitting pw=ph=64 for the root produced index 64*17+64 = 1152, read past
    // the table, and crashed at 0x00BE8626 (observed live, stage25).
    //
    // So a node wider than 16 cells is sampled with a stride instead: the world
    // rectangle is unchanged, the vertex grid is decimated to at most 16x16
    // subdivisions.
    const int subdivX = std::min(patchWidth, 16);
    const int subdivY = std::min(patchHeight, 16);
    if (subdivX <= 0 || subdivY <= 0 ||
        patchWidth % subdivX != 0 || patchHeight % subdivY != 0)
        throw std::invalid_argument(
            "buildBackgroundPatchRect: patch extent must be a whole multiple of its <=16 subdivision");
    const int strideX = patchWidth / subdivX;
    const int strideY = patchHeight / subdivY;
    pb.header.pw = static_cast<uint16_t>(subdivX);
    pb.header.ph = static_cast<uint16_t>(subdivY);
    pb.header.detailMode = 1;
    // indexCount is the STRIP length the shared index buffer will walk, not the
    // raw triangle count. Retail Darkwood_3 frame 35 (pw=ph=16, 289 verts) stores
    // indexCount=1085, which is exactly buildLayerTopology's strip size minus the
    // two priming indices -- the same value the foreground layer writes as its
    // polygonCount. Writing the raw 16*16*2 = 512 made the engine walk under half
    // the strip and render the terrain as bands with gaps (observed live).
    std::array<bool, 512> allTriangles;
    allTriangles.fill(true);
    const terrain::LayerTopology topology = terrain::buildLayerTopology(allTriangles);
    if (!topology.fullPatch ||
        topology.vertices.size() != size_t(subdivX + 1) * size_t(subdivY + 1) ||
        topology.indices.size() < 2)
        throw std::runtime_error(
            "buildBackgroundPatchRect: canonical topology does not match the subdivision grid");
    pb.header.indexCount = static_cast<uint16_t>(topology.indices.size() - 2);
    pb.header.vertexCount = static_cast<uint16_t>((subdivX + 1) * (subdivY + 1));
    pb.header.texExtX = texture.width > 255 ? 255 : static_cast<uint8_t>(texture.width);
    pb.header.texExtY = texture.height > 255 ? 255 : static_cast<uint8_t>(texture.height);
    pb.header.isDXT = texture.pixelFormat0 == 3u ||
                      (texture.pixelFormat0 & 0xffu) == 3u;
    pb.header.valid = true;
    pb.texture = serializeInlineTexture(singleLevelBackgroundTexture(texture));

    const HeightSampler sampleHeight = clampedHeightSampler(hf);
    auto normalAt = [&](int x, int y) {
        return packMapNormal(sampleHeight, x, y);
    };
    std::vector<PatchVertex> vertices;
    vertices.reserve(pb.header.vertexCount);
    // VERTEX ORDER IS PART OF THE CONTRACT. Background triangles are built from a
    // shared index buffer, so the VB must be stored in the engine's canonical
    // strip order, not row-major. Retail Darkwood_3 frame 35 (pw=ph=16, 289
    // verts) stores:
    //     (0,0) (1,0) (1,1) (0,1) (0,2) (1,2) (1,3) (0,3) (0,4) (1,4) ...
    // i.e. the serpentine first-touch order that buildLayerTopology already
    // generates for foreground layers (verified identical, 2026-08-22). Emitting
    // row-major here produced holes and giant spanning triangles in-game.
    for (const auto& coord : topology.vertices) {
        const int sx = coord.x, sy = coord.y;
        const int hx = patchX + sx * strideX, hy = patchY + sy * strideY;
        PatchVertex v;
        v.gridX = static_cast<uint16_t>(worldX + hx);
        v.gridY = static_cast<uint16_t>(worldY + hy);
        v.height = hf.at(hx, hy);
        v.packedNormal = normalAt(hx, hy);
        // Retail authored patches use the byte-scale 0..255 domain in these
        // UWORD texture-coordinate fields. The domain is the SUBDIVISION grid,
        // so a decimated parent still spans the full 0..255 range.
        v.uv0 = static_cast<uint16_t>((uint32_t(sx) * 255u) / uint32_t(subdivX));
        v.uv1 = static_cast<uint16_t>((uint32_t(sy) * 255u) / uint32_t(subdivY));
        vertices.push_back(v);
    }
    const auto vb = serializePatchVB(vertices);
    pb.vbBlock = rangeBlockRaw(vb.data(), vertices.size(), 16);
    // Background-patch indices are implicit.  Retail patch bodies transition
    // directly from the framed vertex block to four edge strips; indexCount is
    // the triangle count used by the loader to construct the regular grid.
    // Writing an explicit block here shifts the edge-strip cursor and makes the
    // loader interpret compressed index bytes as edge array counts.
    pb.ibBlock.clear();
    pb.trailer = serializeEmptyPatchTrailer(pb.header.coord0, pb.header.coord1);
    return pb;
}

std::vector<AuthoredBackgroundPatch> buildBackgroundPatchGrid(
    const terrain::Heightfield& hf, int worldX, int worldY,
    const InlineTexture& texture) {
    if (hf.width() != 64 || hf.height() != 64)
        throw std::invalid_argument("buildBackgroundPatchGrid: expected 64x64 heightfield");
    auto make = [&](int x, int y, int size, bool root) {
        AuthoredBackgroundPatch out;
        out.body = buildBackgroundPatch(hf, x, y, size, worldX, worldY, texture);
        float minZ = std::numeric_limits<float>::max();
        float maxZ = -std::numeric_limits<float>::max();
        for (int py = y; py <= y + size; ++py)
            for (int px = x; px <= x + size; ++px) {
                minZ = std::min(minZ, hf.at(px, py));
                maxZ = std::max(maxZ, hf.at(px, py));
            }
        out.aabb = {float(worldX + x), float(worldY + y), minZ,
                    float(worldX + x + size), float(worldY + y + size), maxZ};
        out.root = root;
        return out;
    };
    std::vector<AuthoredBackgroundPatch> out;
    out.reserve(16);
    for (int y = 0; y < 64; y += 16)
        for (int x = 0; x < 64; x += 16)
            out.push_back(make(x, y, 16, false));
    return out;
}

BackgroundFrameLayout layoutBackgroundFrames(
    const std::vector<AuthoredBackgroundPatch>& patches,
    size_t startOffset, size_t alignment) {
    if (patches.empty())
        throw std::invalid_argument("layoutBackgroundFrames: no patches");
    if (alignment == 0 || (alignment & (alignment - 1)) != 0)
        throw std::invalid_argument("layoutBackgroundFrames: alignment must be a power of two");
    BackgroundFrameLayout out;
    out.entries.reserve(patches.size());
    for (size_t i = 0; i < patches.size(); ++i) {
        const size_t absolute = startOffset + out.bytes.size();
        const size_t aligned = (absolute + alignment - 1) & ~(alignment - 1);
        out.bytes.resize(out.bytes.size() + (aligned - absolute), 0);
        const auto body = assemblePatchBody(patches[i].body);
        const auto frame = forge::lzo::compressFramed(body);
        if (aligned > UINT32_MAX || frame.size() > UINT32_MAX)
            throw std::runtime_error("layoutBackgroundFrames: frame position exceeds u32");
        QuadEntry entry;
        entry.flags = 0x00c92e01u;
        entry.frameOffset = static_cast<uint32_t>(aligned);
        entry.frameSpan = static_cast<uint32_t>(frame.size());
        std::copy(patches[i].aabb.begin(), patches[i].aabb.end(), entry.aabb);
        out.entries.push_back(entry);
        out.bytes.insert(out.bytes.end(), frame.begin(), frame.end());
    }
    return out;
}

BackgroundFrameLayout layoutForegroundFrames(
    const std::vector<ForegroundFrame>& frames,
    size_t startOffset, size_t alignment) {
    if (frames.empty())
        throw std::invalid_argument("layoutForegroundFrames: no frames");
    if (alignment == 0 || (alignment & (alignment - 1)) != 0)
        throw std::invalid_argument("layoutForegroundFrames: alignment must be a power of two");
    BackgroundFrameLayout out;
    out.entries.reserve(frames.size());
    for (const auto& foreground : frames) {
        if (foreground.layers.empty())
            throw std::runtime_error("layoutForegroundFrames: frame has no layers");
        float minX = std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();
        float minZ = std::numeric_limits<float>::max();
        float maxX = -std::numeric_limits<float>::max();
        float maxY = -std::numeric_limits<float>::max();
        float maxZ = -std::numeric_limits<float>::max();
        size_t vertexCount = 0;
        for (const auto& layer : foreground.layers)
            for (const auto& vertex : layer.vertices) {
                minX = std::min(minX, float(vertex.x));
                minY = std::min(minY, float(vertex.y));
                minZ = std::min(minZ, vertex.height);
                maxX = std::max(maxX, float(vertex.x));
                maxY = std::max(maxY, float(vertex.y));
                maxZ = std::max(maxZ, vertex.height);
                ++vertexCount;
            }
        if (vertexCount == 0)
            throw std::runtime_error("layoutForegroundFrames: frame has no vertices");
        const size_t absolute = startOffset + out.bytes.size();
        const size_t aligned = (absolute + alignment - 1) & ~(alignment - 1);
        out.bytes.resize(out.bytes.size() + aligned - absolute, 0);
        const auto frame = forge::lzo::compressFramed(serializeForegroundFrame(foreground));
        if (aligned > UINT32_MAX || frame.size() > UINT32_MAX)
            throw std::runtime_error("layoutForegroundFrames: frame tuple exceeds u32");
        QuadEntry entry;
        entry.flags = 0x00c92e01u;
        entry.frameOffset = static_cast<uint32_t>(aligned);
        entry.frameSpan = static_cast<uint32_t>(frame.size());
        entry.aabb[0] = minX; entry.aabb[1] = minY; entry.aabb[2] = minZ;
        entry.aabb[3] = maxX; entry.aabb[4] = maxY; entry.aabb[5] = maxZ;
        out.entries.push_back(entry);
        out.bytes.insert(out.bytes.end(), frame.begin(), frame.end());
    }
    return out;
}

std::size_t alignUp(std::size_t pos, std::size_t align) {
    if (align == 0) return pos;
    return ((pos + align - 1) / align) * align;
}

// --- Full patch-body segmentation + assembly ------------------------------
namespace {
int32_t rdS32(const std::vector<uint8_t>& b, size_t o) {
    return int32_t(uint32_t(b[o]) | (uint32_t(b[o + 1]) << 8) |
                   (uint32_t(b[o + 2]) << 16) | (uint32_t(b[o + 3]) << 24));
}
// A framed range block starting at `off` = [s32 len][len bytes]. Returns the block
// span length (4+len) if it decodes to exactly count*stride bytes, else 0.
size_t tryFramedBlock(const std::vector<uint8_t>& b, size_t off,
                      size_t count, size_t stride) {
    if (off + 4 > b.size()) return 0;
    int32_t len = rdS32(b, off);
    if (len <= 0 || off + 4 + size_t(len) > b.size()) return 0;
    try {
        std::vector<uint8_t> dec =
            forge::rangecodec::decode(&b[off + 4], size_t(len), count, stride);
        if (dec.size() != count * stride) return 0;
    } catch (const std::exception&) {
        return 0;
    }
    return 4 + size_t(len);
}

bool plausibleAdaptivePatchVB(const std::vector<uint8_t>& b, size_t off,
                              const PatchHeader& h) {
    const int32_t len = rdS32(b, off);
    if (len <= 0) return false;
    std::vector<uint8_t> vb;
    try {
        vb = forge::rangecodec::decode(&b[off + 4], size_t(len),
                                       h.vertexCount, 16);
    } catch (const std::exception&) {
        return false;
    }
    uint16_t minX = 0xffff, minY = 0xffff, maxX = 0, maxY = 0;
    std::set<std::pair<uint16_t, uint16_t>> points;
    for (size_t i = 0; i < h.vertexCount; ++i) {
        const uint8_t* p = &vb[i * 16];
        const uint16_t x = uint16_t(p[0] | (uint16_t(p[1]) << 8));
        const uint16_t y = uint16_t(p[2] | (uint16_t(p[3]) << 8));
        minX = std::min(minX, x); maxX = std::max(maxX, x);
        minY = std::min(minY, y); maxY = std::max(maxY, y);
        points.emplace(x, y);
    }
    return points.size() == h.vertexCount &&
           uint32_t(maxX) - minX == h.pw &&
           uint32_t(maxY) - minY == h.ph;
}

} // namespace

PatchBody parsePatchBody(const std::vector<uint8_t>& body) {
    PatchBody pb;
    if (body.size() < 10) return pb;
    pb.header = parsePatchHeader(body);
    pb.waterOnly = body[8] != 0;
    if (pb.waterOnly) {
        // 10-byte header, everything after is opaque (edge strips + water).
        pb.trailer.assign(body.begin() + 10, body.end());
        pb.valid = true;
        return pb;
    }
    if (body.size() < 17) return pb;
    const size_t hdrLen = 17;
    const size_t vc = pb.header.vertexCount;
    const size_t ic = pb.header.indexCount;
    // Verified search for the VB block start (texture is the variable span before it).
    for (size_t off = hdrLen; off + 4 <= body.size(); ++off) {
        size_t vbSpan = tryFramedBlock(body, off, vc, 16);
        if (!vbSpan) continue;
        if (!plausibleAdaptivePatchVB(body, off, pb.header)) continue;
        size_t after = off + vbSpan;
        // CLandscapeBackgroundPatch::Load reads a second framed CRangeCompressor
        // block only when this patch owns indices; other patches use the shared IB.
        // Do not require a Cartesian vertex grid here: adaptive LOD patches deliberately
        // serialize only the selected vertices, so vertexCount can be much smaller
        // than (pw+1)*(ph+1).
        size_t ibSpan = tryFramedBlock(body, after, ic * 3, 2);
        pb.texture.assign(body.begin() + hdrLen, body.begin() + off);
        pb.vbBlock.assign(body.begin() + off, body.begin() + after);
        if (ibSpan) {
            pb.ibBlock.assign(body.begin() + after, body.begin() + after + ibSpan);
            after += ibSpan;
        }
        pb.trailer.assign(body.begin() + after, body.end());
        pb.valid = true;
        return pb;
    }
    return pb; // VB not locatable (compressed-decode gap, or not a mesh patch)
}

std::vector<PatchVertex> decodePatchVertices(const PatchBody& pb) {
    if (!pb.valid || pb.waterOnly || pb.vbBlock.size() < 5)
        throw std::invalid_argument("decodePatchVertices: patch has no valid VB");
    const size_t vc = pb.header.vertexCount;
    const int32_t len = rdS32(pb.vbBlock, 0);
    if (len <= 0 || size_t(len) + 4 > pb.vbBlock.size())
        throw std::runtime_error("decodePatchVertices: invalid VB block length");
    const std::vector<uint8_t> vb = forge::rangecodec::decode(
        &pb.vbBlock[4], size_t(len), vc, 16);
    std::vector<PatchVertex> out(vc);
    for (size_t i = 0; i < vc; ++i) {
        const uint8_t* p = &vb[i * 16];
        PatchVertex& v = out[i];
        v.gridX = uint16_t(p[0] | (uint16_t(p[1]) << 8));
        v.gridY = uint16_t(p[2] | (uint16_t(p[3]) << 8));
        std::memcpy(&v.height, p + 4, 4);
        v.packedNormal = uint32_t(p[8]) | (uint32_t(p[9]) << 8) |
                         (uint32_t(p[10]) << 16) | (uint32_t(p[11]) << 24);
        v.uv0 = uint16_t(p[12] | (uint16_t(p[13]) << 8));
        v.uv1 = uint16_t(p[14] | (uint16_t(p[15]) << 8));
    }
    return out;
}

std::vector<uint8_t> assemblePatchBody(const PatchBody& pb,
                                       const std::vector<float>& newHeights) {
    std::vector<uint8_t> out;
    if (pb.waterOnly) {
        // 10-byte header (pw,ph,coord0,coord1,isWaterOnly,detailMode) + trailer.
        std::vector<uint8_t> h = serializePatchHeader(pb.header);
        out.insert(out.end(), h.begin(), h.begin() + 10);
        out.insert(out.end(), pb.trailer.begin(), pb.trailer.end());
        return out;
    }
    std::vector<uint8_t> hdr = serializePatchHeader(pb.header);
    out.insert(out.end(), hdr.begin(), hdr.end());
    out.insert(out.end(), pb.texture.begin(), pb.texture.end());

    if (!newHeights.empty()) {
        // Decode the existing VB, replace only the height f32 per vertex, RAW-reframe.
        const size_t vc = pb.header.vertexCount;
        if (newHeights.size() != vc)
            throw std::invalid_argument("assemblePatchBody: newHeights size != vertexCount");
        int32_t len = rdS32(pb.vbBlock, 0);
        std::vector<uint8_t> vb = forge::rangecodec::decode(
            &pb.vbBlock[4], size_t(len), vc, 16); // vc*16 bytes
        for (size_t i = 0; i < vc; ++i) {
            uint32_t bits;
            std::memcpy(&bits, &newHeights[i], 4);
            // record layout: [u16 gridX][u16 gridY][f32 height]... -> height at +4.
            vb[i * 16 + 4] = uint8_t(bits);
            vb[i * 16 + 5] = uint8_t(bits >> 8);
            vb[i * 16 + 6] = uint8_t(bits >> 16);
            vb[i * 16 + 7] = uint8_t(bits >> 24);
        }
        std::vector<uint8_t> nb = rangeBlockRaw(vb.data(), vc, 16);
        out.insert(out.end(), nb.begin(), nb.end());
    } else {
        out.insert(out.end(), pb.vbBlock.begin(), pb.vbBlock.end());
    }
    out.insert(out.end(), pb.ibBlock.begin(), pb.ibBlock.end());
    out.insert(out.end(), pb.trailer.begin(), pb.trailer.end());
    return out;
}

std::vector<uint8_t> assemblePatchBodyVertices(
    const PatchBody& pb, const std::vector<PatchVertex>& vertices) {
    if (!pb.valid || pb.waterOnly)
        throw std::invalid_argument("assemblePatchBodyVertices: invalid mesh patch");
    if (vertices.size() != pb.header.vertexCount)
        throw std::invalid_argument("assemblePatchBodyVertices: vertex count mismatch");
    std::vector<uint8_t> out = serializePatchHeader(pb.header);
    out.insert(out.end(), pb.texture.begin(), pb.texture.end());
    const std::vector<uint8_t> vb = serializePatchVB(vertices);
    const std::vector<uint8_t> block = rangeBlockRaw(vb.data(), vertices.size(), 16);
    out.insert(out.end(), block.begin(), block.end());
    out.insert(out.end(), pb.ibBlock.begin(), pb.ibBlock.end());
    out.insert(out.end(), pb.trailer.begin(), pb.trailer.end());
    return out;
}

std::vector<uint8_t> assemblePatchBodyVerticesFixedSpan(
    const PatchBody& pb, const std::vector<PatchVertex>& vertices) {
    if (!pb.valid || pb.waterOnly)
        throw std::invalid_argument("assemblePatchBodyVerticesFixedSpan: invalid mesh patch");
    if (vertices.size() != pb.header.vertexCount)
        throw std::invalid_argument("assemblePatchBodyVerticesFixedSpan: vertex count mismatch");
    if (pb.vbBlock.size() < 5)
        throw std::invalid_argument("assemblePatchBodyVerticesFixedSpan: donor VB block too small");

    const std::vector<uint8_t> vb = serializePatchVB(vertices);
    // CRange fields are a compression choice, not part of the decoded vertex
    // schema. Try layouts that respect the semantic 2/2/4/4/2/2 fields as well
    // as byte-splitting the two float/packed fields. A sculpted float height can
    // have a stable exponent byte but noisy mantissa bytes; encoding all four as
    // one integer needlessly turns that into a 32-bit numeric range and can
    // overflow an otherwise adequate donor allocation.
    const std::vector<std::vector<size_t>> layouts = {
        {2, 2, 4, 4, 2, 2},
        {1, 1, 1, 1, 4, 4, 2, 2},
        {2, 2, 2, 2, 4, 2, 2},
        {2, 2, 1, 1, 1, 1, 4, 2, 2},
        {2, 2, 4, 1, 1, 1, 1, 2, 2},
        {2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2}
    };
    std::vector<std::vector<uint8_t>> authoredCandidates;
    authoredCandidates.reserve(layouts.size() + 2);
    // The editor's own compressor first: it reproduces retail blocks byte-exact
    // from their decoded elements, so an unchanged block keeps its donor size.
    authoredCandidates.push_back(forge::rangecodec::encodeNative(vb.data(), vertices.size(), 16));
    for (const auto& layout : layouts) {
        authoredCandidates.push_back(forge::rangecodec::encodeFieldColumns(
            vb.data(), vertices.size(), 16, layout));
    }
    authoredCandidates.push_back(
        forge::rangecodec::encodeByteColumns(vb.data(), vertices.size(), 16));
    std::vector<uint8_t> packed = authoredCandidates.front();
    for (const auto& candidate : authoredCandidates)
        if (candidate.size() < packed.size()) packed = candidate;
    const size_t donorBlockLen = pb.vbBlock.size() - 4;
    const std::vector<uint8_t> donorVB = forge::rangecodec::decode(
        &pb.vbBlock[4], donorBlockLen, vertices.size(), 16);
    auto unchanged = [&](size_t offset, size_t length) {
        for (size_t row = 0; row < vertices.size(); ++row) {
            const size_t base = row * 16 + offset;
            if (!std::equal(vb.begin() + base, vb.begin() + base + length,
                            donorVB.begin() + base))
                return false;
        }
        return true;
    };
    // Reuse exact retail descriptors for any unchanged suffix. This is both
    // smaller and higher-fidelity for diagnostic edits such as a coordinate-
    // only retarget, while still falling back to the native encoder when donor
    // descriptor boundaries do not align with the requested field range.
    for (const auto& authoredPacked : authoredCandidates) {
        for (const auto& range : {std::pair<size_t, size_t>{4, 12}, {8, 8}, {12, 4}}) {
            if (!unchanged(range.first, range.second)) continue;
            try {
                std::vector<uint8_t> candidate = forge::rangecodec::spliceColumnRange(
                    authoredPacked, &pb.vbBlock[4], donorBlockLen, vertices.size(), 16,
                    range.first, range.second);
                if (candidate.size() < packed.size()) packed = std::move(candidate);
            } catch (const std::exception&) {
                // The donor optimizer may combine a field across this boundary.
            }
        }
    }
    if (packed.size() > donorBlockLen) {
        throw std::runtime_error("assemblePatchBodyVerticesFixedSpan: encoded VB " +
                                 std::to_string(packed.size()) + " exceeds donor span " +
                                 std::to_string(donorBlockLen));
    }
    packed.resize(donorBlockLen, 0); // decoder stops at 0x80; trailing bytes are inert

    std::vector<uint8_t> out = serializePatchHeader(pb.header);
    out.insert(out.end(), pb.texture.begin(), pb.texture.end());
    const uint32_t len = uint32_t(donorBlockLen);
    out.push_back(uint8_t(len));
    out.push_back(uint8_t(len >> 8));
    out.push_back(uint8_t(len >> 16));
    out.push_back(uint8_t(len >> 24));
    out.insert(out.end(), packed.begin(), packed.end());
    out.insert(out.end(), pb.ibBlock.begin(), pb.ibBlock.end());
    out.insert(out.end(), pb.trailer.begin(), pb.trailer.end());
    return out;
}

FrameEdit patchBodyToFrameEdit(size_t frameIndex, const PatchBody& pb,
                               const std::vector<float>& newHeights) {
    FrameEdit fe;
    fe.frameIndex = frameIndex;
    fe.newBody = assemblePatchBody(pb, newHeights);
    return fe;
}

std::vector<QuadEntry> parseQuadDir(const Chunk& chunk, size_t dirBase) {
    // The 0x24-byte entry array runs until the first all-zero terminator record.
    // A record whose frameOffset==0 && frameSpan==0 is the sentinel (the donor's
    // 5th slot is a zero record). Guard against a wrong base by bailing the moment
    // a record fails a coarse sanity gate on the very first entry.
    std::vector<QuadEntry> out;
    const std::vector<uint8_t>& d = chunk.raw;
    const size_t stride = 0x24;
    for (size_t i = 0;; ++i) {
        size_t o = dirBase + i * stride;
        if (o + stride > d.size()) break;
        QuadEntry e;
        e.dirOffset = o;
        e.frameOffset = getU32(&d[o + 0]);
        e.frameSpan = getU32(&d[o + 4]);
        for (int k = 0; k < 6; ++k) e.aabb[k] = getF32(&d[o + 8 + k * 4]);
        e.flags = getU32(&d[o + 32]);
        // terminator: an all-zero record ends the array. A record with a zero
        // frame pointer but live AABB/flags is a 16x16 cell WITHOUT a foreground
        // mesh (many fillers have them); it stays in the list with
        // frameOffset == 0 so AABB edits cover it and counts match the map.
        bool allZero = true;
        for (size_t k = 0; k < stride && allZero; ++k) allZero = d[o + k] == 0;
        if (allZero) break;
        out.push_back(e);
        if (out.size() > 4096) break; // never runs away
    }
    return out;
}

std::vector<uint8_t> generateQuadDir(const std::vector<QuadEntry>& entries) {
    std::vector<uint8_t> out;
    out.reserve((entries.size() + 1) * 0x24);
    auto append32 = [&](uint32_t v) {
        out.push_back(uint8_t(v));       out.push_back(uint8_t(v >> 8));
        out.push_back(uint8_t(v >> 16)); out.push_back(uint8_t(v >> 24));
    };
    auto appendF = [&](float f) { uint32_t v; std::memcpy(&v, &f, 4); append32(v); };
    for (const QuadEntry& e : entries) {
        append32(e.frameOffset);
        append32(e.frameSpan);
        for (int k = 0; k < 6; ++k) appendF(e.aabb[k]);
        append32(e.flags);
    }
    out.insert(out.end(), 0x24, uint8_t(0));
    return out;
}

size_t translateQuadDirXY(std::vector<uint8_t>& chunkBytes, float dx, float dy) {
    if (!std::isfinite(dx) || !std::isfinite(dy))
        throw std::invalid_argument("translateQuadDirXY: offsets must be finite");
    const Chunk parsed = parseChunk(chunkBytes);
    const std::vector<QuadEntry> entries = parseQuadDir(parsed);
    // aabb layout at dirOffset+8 is (minX, minY, minZ, maxX, maxY, maxZ);
    // indices 0/3 are X and 1/4 are Y.
    for (const auto& entry : entries) {
        for (const int index : {0, 1, 3, 4}) {
            const size_t at = entry.dirOffset + 8 + size_t(index) * 4;
            if (at + sizeof(float) > chunkBytes.size())
                throw std::runtime_error(
                    "translateQuadDirXY: directory entry exceeds chunk");
            float value = 0.0f;
            std::memcpy(&value, chunkBytes.data() + at, sizeof(value));
            value += (index == 0 || index == 3) ? dx : dy;
            std::memcpy(chunkBytes.data() + at, &value, sizeof(value));
        }
    }
    return entries.size();
}

size_t updateQuadDirZBounds(
    std::vector<uint8_t>& chunkBytes,
    const std::vector<std::pair<float, float>>& zBounds) {
    const Chunk parsed = parseChunk(chunkBytes);
    const std::vector<QuadEntry> entries = parseQuadDir(parsed);
    if (entries.size() != zBounds.size())
        throw std::invalid_argument(
            "updateQuadDirZBounds: one Z range is required per live entry");
    for (size_t i = 0; i < entries.size(); ++i) {
        const float minZ = zBounds[i].first;
        const float maxZ = zBounds[i].second;
        if (!std::isfinite(minZ) || !std::isfinite(maxZ) || minZ > maxZ)
            throw std::invalid_argument(
                "updateQuadDirZBounds: Z range must be finite and ordered");
        const size_t minAt = entries[i].dirOffset + 8 + 2 * 4;
        const size_t maxAt = entries[i].dirOffset + 8 + 5 * 4;
        if (maxAt + sizeof(float) > chunkBytes.size())
            throw std::runtime_error(
                "updateQuadDirZBounds: directory entry exceeds chunk");
        std::memcpy(chunkBytes.data() + minAt, &minZ, sizeof(minZ));
        std::memcpy(chunkBytes.data() + maxAt, &maxZ, sizeof(maxZ));
    }
    return entries.size();
}

RetargetResult retargetChunk(const std::vector<uint8_t>& donorRelocated) {
    RetargetResult r;
    Chunk c = parseChunk(donorRelocated);

    // Index every FRAME by its start offset and on-disk length so a quad entry can
    // be checked for wiring (offset lands on a frame, span equals its length).
    auto frameLenAt = [&](uint32_t off) -> long {
        for (size_t fi : c.frameIndices) {
            const Segment& s = c.segments[fi];
            if (s.start == off) return long(s.end - s.start);
        }
        return -1;
    };

    std::vector<QuadEntry> dir = parseQuadDir(c);
    r.consistent = !dir.empty();
    if (dir.empty())
        r.notes.push_back("quadtree directory empty at 0x7fc (base wrong or chunk malformed)");
    int okCount = 0;
    for (const QuadEntry& e : dir) {
        if (e.frameOffset == 0 && e.frameSpan == 0) continue;   // a cell without a foreground mesh
        long fl = frameLenAt(e.frameOffset);
        bool wired = (fl >= 0) && (uint32_t(fl) == e.frameSpan);
        bool aabbOk = true;
        for (int k = 0; k < 3; ++k) {
            if (!(e.aabb[k] <= e.aabb[k + 3]) || !std::isfinite(e.aabb[k]) ||
                !std::isfinite(e.aabb[k + 3]))
                aabbOk = false;
        }
        if (wired && aabbOk) {
            ++okCount;
        } else {
            r.consistent = false;
            char buf[160];
            std::snprintf(buf, sizeof buf,
                          "quad entry @0x%zx: foff=0x%x span=%u frameLen=%ld "
                          "wired=%d aabbOk=%d",
                          e.dirOffset, e.frameOffset, e.frameSpan, fl, int(wired),
                          int(aabbOk));
            r.notes.push_back(buf);
        }
    }
    {
        char buf[80];
        std::snprintf(buf, sizeof buf, "%d/%zu quad entries wire to real frames",
                      okCount, dir.size());
        r.notes.push_back(buf);
    }

    // Re-emit: the retarget itself does not mutate bytes (relocation was done
    // upstream on the placement floats); reserialize is the round-trip proof.
    r.chunk = reserialize(c);
    return r;
}

namespace {

void putU32le(std::vector<uint8_t>& out, size_t at, uint32_t v) {
    out[at + 0] = uint8_t(v);
    out[at + 1] = uint8_t(v >> 8);
    out[at + 2] = uint8_t(v >> 16);
    out[at + 3] = uint8_t(v >> 24);
}

} // namespace

EmitResult emitChunk(const Chunk& chunk, const EmitOptions& opt) {
    EmitResult r;

    // 1) Decide each frame's new on-disk bytes. A frame is either:
    //    - an authored edit (re-compress newBody, always),
    //    - Recompress (decode existing body, re-compress lzo1x_1), or
    //    - RawPassthrough (copy the existing on-disk frame verbatim).
    // We index edits by frame index (into frameIndices).
    const size_t nFrames = chunk.frameIndices.size();
    std::vector<std::vector<uint8_t>> editBody(nFrames);
    std::vector<char> hasEdit(nFrames, 0);
    for (const FrameEdit& e : opt.edits) {
        if (e.frameIndex >= nFrames) {
            r.notes.push_back("edit references frame index out of range; ignored");
            continue;
        }
        editBody[e.frameIndex] = e.newBody;
        hasEdit[e.frameIndex] = 1;
    }

    // new on-disk frame bytes, keyed by the segment index of that frame.
    std::vector<std::vector<uint8_t>> newFrameBytes(chunk.segments.size());
    // old->new start offset for each frame segment (for quad-dir rewiring).
    // maps old frame start -> {newStart, newSpan}
    struct FrameMove { size_t oldStart, oldSpan, newSpan; };
    std::vector<FrameMove> frameMoves;
    bool anyLogicalSizeChange = false;

    for (size_t fi = 0; fi < nFrames; ++fi) {
        size_t segIdx = chunk.frameIndices[fi];
        const Segment& s = chunk.segments[segIdx];
        size_t oldSpan = s.end - s.start;
        std::vector<uint8_t> onDisk;
        if (hasEdit[fi]) {
            if (editBody[fi].size() != s.uncompLen) anyLogicalSizeChange = true;
            onDisk = opt.highCompressionEdits
                ? forge::lzo::compressFramed999(editBody[fi])
                : forge::lzo::compressFramed(editBody[fi]);
        } else if (opt.codec == FrameCodec::Recompress) {
            std::vector<uint8_t> body = decodeFrame(chunk, fi);
            onDisk = forge::lzo::compressFramed(body);
        } else { // RawPassthrough
            onDisk.assign(chunk.raw.begin() + s.start, chunk.raw.begin() + s.end);
        }
        if (onDisk.size() != oldSpan || hasEdit[fi] ||
            opt.codec == FrameCodec::Recompress) {
            // count only frames whose bytes actually differ
            bool differs = onDisk.size() != oldSpan ||
                           !std::equal(onDisk.begin(), onDisk.end(),
                                       chunk.raw.begin() + s.start);
            if (differs) ++r.framesReencoded;
        }
        frameMoves.push_back(FrameMove{s.start, oldSpan, onDisk.size()});
        newFrameBytes[segIdx] = std::move(onDisk);
    }

    // 2) Re-lay all segments left-to-right, tracking the running byte delta so we
    //    know where each frame LANDS in the output (its new start offset).
    std::vector<uint8_t> out;
    out.reserve(chunk.raw.size());
    // old frame start -> new frame start (only for frame segments)
    std::vector<std::pair<size_t, size_t>> startMap; // (oldStart,newStart)
    startMap.reserve(nFrames);
    bool physicalLayoutFits = true;
    auto lastFrameNote = [&]() {
        std::string n; int shown = 0;
        for (const auto& m : frameMoves) {
            if (m.newSpan <= m.oldSpan) continue;
            char b[128];
            std::snprintf(b, sizeof b, " [frame @0x%zx grew %zu -> %zu]", m.oldStart, m.oldSpan, m.newSpan);
            n += b;
            if (++shown >= 8) { n += " ..."; break; }
        }
        return n;
    };

    for (const Segment& s : chunk.segments) {
        if (opt.preservePhysicalLayout && s.kind == SegKind::Pad) {
            if (out.size() > s.end) {
                physicalLayoutFits = false;
                r.notes.push_back("edited frame crosses the donor PAD allocation" + lastFrameNote());
            } else {
                out.insert(out.end(), s.end - out.size(), uint8_t(0));
            }
            continue;
        }
        if (opt.preservePhysicalLayout) {
            if (out.size() > s.start) {
                physicalLayoutFits = false;
                r.notes.push_back("edited frame overlaps the next donor segment" + lastFrameNote());
            } else {
                out.insert(out.end(), s.start - out.size(), uint8_t(0));
            }
        }
        // STEP 2: with frameAlign on, PAD segments are regenerated by alignment, not
        // carried — skip them here and pad up to the boundary before each frame.
        if (opt.frameAlign && s.kind == SegKind::Pad) continue;
        if (opt.frameAlign && s.kind == SegKind::Frame) {
            size_t aligned = alignUp(out.size(), opt.frameAlign);
            out.insert(out.end(), aligned - out.size(), uint8_t(0));
        }
        size_t newStart = out.size();
        if (s.kind == SegKind::Frame) {
            size_t segIdx = size_t(&s - chunk.segments.data());
            const std::vector<uint8_t>& fb = newFrameBytes[segIdx];
            startMap.emplace_back(s.start, newStart);
            out.insert(out.end(), fb.begin(), fb.end());
        } else {
            // HDR (and PAD when frameAlign==0) carried verbatim.
            out.insert(out.end(), chunk.raw.begin() + s.start,
                       chunk.raw.begin() + s.end);
        }
    }

    // 3) Rewire the background-LOD quadtree directory. Each live entry points at a
    //    frame by (frameOffset, frameSpan==8+compLen). Look each up in the move
    //    table by OLD start; write the frame's NEW start and NEW span. The quad
    //    dir lives in an HDR segment, already copied verbatim into `out`, so we
    //    patch `out` in place at the same absolute offset (HDR before the first
    //    moved frame never shifts; the donor quad dir at 0x7fc precedes 0x1000).
    auto lookupMove = [&](uint32_t oldOff) -> const FrameMove* {
        for (const FrameMove& m : frameMoves)
            if (m.oldStart == oldOff) return &m;
        return nullptr;
    };
    auto lookupNewStart = [&](size_t oldStart) -> long {
        for (const auto& pr : startMap)
            if (pr.first == oldStart) return long(pr.second);
        return -1;
    };

    std::vector<QuadEntry> dir = parseQuadDir(chunk);
    for (const QuadEntry& e : dir) {
        if (e.frameOffset == 0 && e.frameSpan == 0) continue;   // a cell without a foreground mesh
        const FrameMove* m = lookupMove(e.frameOffset);
        long newStart = lookupNewStart(e.frameOffset);
        if (!m || newStart < 0) {
            r.notes.push_back("quad entry points at a non-frame offset; left as-is");
            continue;
        }
        uint32_t newOff = uint32_t(newStart);
        uint32_t newSpan = uint32_t(m->newSpan);
        if (newOff != e.frameOffset || newSpan != e.frameSpan) {
            // The quad dir entry sits at e.dirOffset within an unshifted HDR
            // region, so its absolute offset in `out` is unchanged.
            if (e.dirOffset + 0x24 <= out.size()) {
                putU32le(out, e.dirOffset + 0, newOff);
                putU32le(out, e.dirOffset + 4, newSpan);
                ++r.quadEntriesRewired;
            } else {
                r.notes.push_back("quad dir offset past emitted chunk end");
            }
        }

    }

    // 4) Verdict.
    r.chunk = std::move(out);
    r.identity = (r.chunk == chunk.raw);
    // Physical-span changes are handled by quad rewiring. Only decoded-length
    // changes can invalidate the logical control-stream positions.
    bool anyUntrackedMove = false;
    for (const FrameMove& m : frameMoves) {
        if (m.newSpan == m.oldSpan) continue; // did not move size
        bool referenced = false;
        for (const QuadEntry& e : dir)
            if (e.frameOffset == m.oldStart) { referenced = true; break; }
        if (!referenced) anyUntrackedMove = true;
    }
    if (anyUntrackedMove && anyLogicalSizeChange)
        r.notes.push_back(
            "a re-encoded frame changed size but is NOT wired by the quadtree dir; "
            "foreground/local-detail frames are referenced by InfoBlock/subheader "
            "position dwords this emitter does not rebase — use RawPassthrough for a "
            "load-safe emit, or supply the rebased dwords (see NATIVE_STB_WRITER_SCOPE).");
    if (anyLogicalSizeChange)
        r.notes.push_back(
            "an edited frame changed decoded body length; logical InfoBlock "
            "positions may move, which is outside the same-topology writer");
    // InfoBlock positions are logical stream snapshots, not physical LZO offsets.
    // A compressed-span-only move is fully handled by quadtree rewiring.
    r.ok = physicalLayoutFits &&
           (r.identity || !anyLogicalSizeChange || opt.allowSameTopologyDecodedResize);
    return r;
}

std::vector<uint8_t> emitIdentity(const std::vector<uint8_t>& source) {
    Chunk c = parseChunk(source);
    EmitResult r = emitChunk(c, EmitOptions{});
    return r.chunk;
}

std::vector<FramedBlock> walkFramedBlocks(const std::vector<uint8_t>& chunk) {
    // Locate standard-LZO1X blocks in the SaveCompressed frame:
    //   [uncompLen u32LE][compLen u32LE][ lzo1x body : compLen bytes ]
    // A block is confirmed only when lzo1x_decompress_safe yields EXACTLY
    // uncompLen bytes with LZO_E_OK — the same honest gate the codec proof used,
    // so raw DXT/index/quadtree regions never masquerade as blocks. Confirmed
    // blocks are non-overlapping; we resume scanning past each hit.
    std::vector<FramedBlock> out;
    if (chunk.size() < 8) return out;

    // Grow-only, uninitialised scratch: thousands of garbage offsets pass the
    // gates below with a huge uncompLen, and a zero-filling resize per attempt
    // cost 23 GB of memset on one retail chunk (0.9 s of a 1.0 s foliage load).
    static thread_local std::unique_ptr<uint8_t[]> scratch;
    static thread_local size_t scratchCap = 0;
    const size_t n = chunk.size();
    for (size_t off = 0; off + 8 < n;) {
        uint32_t uncompLen = getU32(chunk.data() + off);
        uint32_t compLen = getU32(chunk.data() + off + 4);
        // plausibility gates (mirror codec_proof.c)
        // LZO1X is allowed to expand incompressible input slightly. Retail's
        // lzo1x_999 frames are normally smaller than their bodies, but authored
        // lzo1x_1 output can have compLen > uncompLen and is still fully valid.
        if (compLen < 16 || compLen > (n - off - 8) ||
            uncompLen > 64u * 1024 * 1024 ||
            uncompLen > compLen * 64u) {
            ++off;
            continue;
        }
        if (uncompLen > scratchCap) {
            scratchCap = std::max<size_t>(uncompLen, scratchCap * 2);
            scratch.reset(new uint8_t[scratchCap]);
        }
        if (forge::lzo::tryDecompress(chunk.data() + off + 8, compLen, scratch.get(), uncompLen)) {
            FramedBlock b;
            b.frameOffset = off;
            b.compLen = compLen;
            b.data.assign(scratch.get(), scratch.get() + uncompLen);
            out.push_back(std::move(b));
            off += 8 + compLen; // resume past the confirmed block
        } else {
            ++off;
        }
    }
    return out;
}

} // namespace forge::stbbake
