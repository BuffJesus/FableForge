#include "forge/lzo.hpp"

#include <cstring>

// Albion Atlas: forge::lzo is backed by the clean-room MIT LZO1X codec in
// src/lzo1x.cpp instead of GPL minilzo. compress999 (liblzo2's lzo1x_999
// optimiser) has no clean-room equivalent here; callers fall back to
// compress, whose LZO1X-1 class streams the retail loader decodes fine.
#include "lzo1x.hpp"

namespace forge::lzo {
namespace {

void putU32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(uint8_t(v & 0xFF));
    out.push_back(uint8_t((v >> 8) & 0xFF));
    out.push_back(uint8_t((v >> 16) & 0xFF));
    out.push_back(uint8_t((v >> 24) & 0xFF));
}

uint32_t getU32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
           (uint32_t(p[3]) << 24);
}

} // namespace

std::vector<uint8_t> compress(const uint8_t* data, size_t len) {
    return albion::lzo1x::compress(data, len);
}

std::vector<uint8_t> compress999(const uint8_t* data, size_t len) {
    // The optimal-parse encoder is within 1% of lzo1x_999 on retail frames.
    return albion::lzo1x::compress(data, len);
}

std::vector<uint8_t> decompress(const uint8_t* data, size_t len, size_t uncompLen) {
    std::vector<uint8_t> out(uncompLen);
    size_t outLen = uncompLen;
    const auto st = albion::lzo1x::decompress(data, len, out.data(), &outLen);
    if (st != albion::lzo1x::Status::Ok || outLen != uncompLen)
        throw std::runtime_error("forge::lzo: decompress failed / length mismatch");
    return out;
}

bool tryDecompress(const uint8_t* data, size_t len, std::vector<uint8_t>& out) {
    return tryDecompress(data, len, out.data(), out.size());
}

bool tryDecompress(const uint8_t* data, size_t len, uint8_t* out, size_t outLen) {
    size_t produced = outLen;
    const auto st = albion::lzo1x::decompress(data, len, out, &produced);
    return st == albion::lzo1x::Status::Ok && produced == outLen;
}

std::vector<uint8_t> decompressBounded(const uint8_t* data, size_t len,
                                       size_t maxOut) {
    std::vector<uint8_t> out(maxOut);
    size_t outLen = maxOut;
    const auto st = albion::lzo1x::decompress(data, len, out.data(), &outLen);
    // The chunked texture stream may pack trailing bytes after the marker and
    // may legitimately fill the bound exactly; only real corruption is fatal.
    using S = albion::lzo1x::Status;
    if (st != S::Ok && st != S::InputNotConsumed && st != S::OutputOverrun)
        throw std::runtime_error("forge::lzo: bounded decompress failed");
    out.resize(outLen);
    return out;
}

std::vector<uint8_t> compressFramed(const uint8_t* data, size_t len) {
    std::vector<uint8_t> body = compress(data, len);
    std::vector<uint8_t> out;
    out.reserve(body.size() + 8);
    putU32(out, uint32_t(len));         // uncompressed length
    putU32(out, uint32_t(body.size())); // compressed length
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

std::vector<uint8_t> compressFramed999(const uint8_t* data, size_t len) {
    std::vector<uint8_t> body = compress999(data, len);
    std::vector<uint8_t> out;
    out.reserve(body.size() + 8);
    putU32(out, uint32_t(len));
    putU32(out, uint32_t(body.size()));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

std::vector<uint8_t> decompressFramed(const std::vector<uint8_t>& frame, size_t& pos) {
    if (pos + 8 > frame.size())
        throw std::runtime_error("forge::lzo: truncated frame header");
    uint32_t uncompLen = getU32(frame.data() + pos);
    uint32_t compLen = getU32(frame.data() + pos + 4);
    if (pos + 8 + compLen > frame.size())
        throw std::runtime_error("forge::lzo: truncated frame body");
    std::vector<uint8_t> out = decompress(frame.data() + pos + 8, compLen, uncompLen);
    pos += 8 + compLen;
    return out;
}

} // namespace forge::lzo
