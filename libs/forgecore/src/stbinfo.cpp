// forge::stbinfo — CStaticMapInfoBlock read/write. On-disk order mirrors
// stb_infoblock_baker.py FIELDS (the ReadMapInfoBlock/WriteMapInfoBlock order).
#include "forge/stbinfo.hpp"

#include <cstring>

namespace forge::stbinfo {
namespace {
std::int32_t rdI(const std::uint8_t* p, std::size_t& o) {
    std::int32_t v;
    std::memcpy(&v, p + o, 4);
    o += 4;
    return v;
}
float rdF(const std::uint8_t* p, std::size_t& o) {
    float v;
    std::memcpy(&v, p + o, 4);
    o += 4;
    return v;
}
void wrI(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(std::uint8_t(v));
    out.push_back(std::uint8_t(v >> 8));
    out.push_back(std::uint8_t(v >> 16));
    out.push_back(std::uint8_t(v >> 24));
}
void wrF(std::vector<std::uint8_t>& out, float f) {
    std::uint32_t v;
    std::memcpy(&v, &f, 4);
    wrI(out, v);
}
} // namespace

StaticMapInfoBlock readInfoBlock(const std::uint8_t* p) {
    StaticMapInfoBlock b;
    std::size_t o = 0;
    b.versionID = rdI(p, o);
    b.bankFileIndex = rdI(p, o);
    b.edgeHeightFileSize = rdI(p, o);
    b.mapWidth = rdI(p, o);
    b.mapHeight = rdI(p, o);
    b.worldX = rdI(p, o);
    b.worldY = rdI(p, o);
    b.edgeHeightFilePtr = rdI(p, o);
    b.landscapeMapPtr = rdI(p, o);
    b.localDetailMapPtr = rdI(p, o);
    b.quality = rdI(p, o);
    b.shorePointArraySize = rdI(p, o);
    b.shorePointArrayStart = rdI(p, o);
    b.levelChecksum = std::uint32_t(rdI(p, o));
    b.checksumBlockFilePtr = rdI(p, o);
    b.checksumBlockFileSize = rdI(p, o);
    for (int i = 0; i < 6; ++i) b.cameraMapBounds[i] = rdF(p, o);
    b.headerEndPtr = rdI(p, o);
    return b; // o == 0x5C
}

std::vector<std::uint8_t> writeInfoBlock(const StaticMapInfoBlock& b) {
    std::vector<std::uint8_t> out;
    out.reserve(kInfoBlockSize);
    wrI(out, std::uint32_t(b.versionID));
    wrI(out, std::uint32_t(b.bankFileIndex));
    wrI(out, std::uint32_t(b.edgeHeightFileSize));
    wrI(out, std::uint32_t(b.mapWidth));
    wrI(out, std::uint32_t(b.mapHeight));
    wrI(out, std::uint32_t(b.worldX));
    wrI(out, std::uint32_t(b.worldY));
    wrI(out, std::uint32_t(b.edgeHeightFilePtr));
    wrI(out, std::uint32_t(b.landscapeMapPtr));
    wrI(out, std::uint32_t(b.localDetailMapPtr));
    wrI(out, std::uint32_t(b.quality));
    wrI(out, std::uint32_t(b.shorePointArraySize));
    wrI(out, std::uint32_t(b.shorePointArrayStart));
    wrI(out, b.levelChecksum);
    wrI(out, std::uint32_t(b.checksumBlockFilePtr));
    wrI(out, std::uint32_t(b.checksumBlockFileSize));
    for (int i = 0; i < 6; ++i) wrF(out, b.cameraMapBounds[i]);
    wrI(out, std::uint32_t(b.headerEndPtr));
    return out; // size == 0x5C
}

} // namespace forge::stbinfo
