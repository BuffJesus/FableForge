#include "forge/bspatch.hpp"

#include <cstring>
#include <stdexcept>

#include "forge/bunzip.hpp"

namespace forge::bspatch {
namespace {

// bsdiff "offtin": 8-byte little-endian sign-magnitude integer. The high bit of
// the last byte is the sign (1 = negative), the rest is the magnitude.
int64_t offtin(const uint8_t* b) {
    uint64_t y = 0;
    for (int i = 7; i >= 0; --i) {
        y <<= 8;
        y |= (i == 7) ? (b[i] & 0x7F) : b[i];
    }
    return (b[7] & 0x80) ? -(int64_t)y : (int64_t)y;
}

} // namespace

Header readHeader(const std::vector<uint8_t>& patch) {
    if (patch.size() < 32) throw std::runtime_error("bspatch: patch too small");
    if (std::memcmp(patch.data(), "BSDIFF40", 8) != 0)
        throw std::runtime_error("bspatch: bad magic (expected BSDIFF40)");
    Header h;
    h.ctrlLen = (uint64_t)offtin(&patch[8]);
    h.diffLen = (uint64_t)offtin(&patch[16]);
    h.newSize = (uint64_t)offtin(&patch[24]);
    if ((int64_t)h.ctrlLen < 0 || (int64_t)h.diffLen < 0 || (int64_t)h.newSize < 0)
        throw std::runtime_error("bspatch: corrupt header lengths");
    if (32 + h.ctrlLen + h.diffLen > patch.size())
        throw std::runtime_error("bspatch: header lengths exceed patch size");
    return h;
}

std::vector<uint8_t> apply(const std::vector<uint8_t>& oldFile,
                           const std::vector<uint8_t>& patch) {
    Header h = readHeader(patch);

    const uint8_t* base = patch.data();
    const size_t ctrlOff = 32;
    const size_t diffOff = ctrlOff + h.ctrlLen;
    const size_t extraOff = diffOff + h.diffLen;

    std::vector<uint8_t> ctrl = bunzip::decompress(base + ctrlOff, h.ctrlLen);
    std::vector<uint8_t> diff = bunzip::decompress(base + diffOff, h.diffLen);
    std::vector<uint8_t> extra = bunzip::decompress(base + extraOff, patch.size() - extraOff);

    std::vector<uint8_t> out;
    out.resize((size_t)h.newSize);

    size_t ctrlPos = 0, diffPos = 0, extraPos = 0;
    int64_t oldpos = 0, newpos = 0;

    while (newpos < (int64_t)h.newSize) {
        if (ctrlPos + 24 > ctrl.size())
            throw std::runtime_error("bspatch: control block underrun");
        int64_t addLen = offtin(&ctrl[ctrlPos]);
        int64_t extraLen = offtin(&ctrl[ctrlPos + 8]);
        int64_t seek = offtin(&ctrl[ctrlPos + 16]);
        ctrlPos += 24;

        if (addLen < 0 || extraLen < 0 ||
            newpos + addLen > (int64_t)h.newSize ||
            newpos + addLen + extraLen > (int64_t)h.newSize)
            throw std::runtime_error("bspatch: control triple out of range");

        // Add: new = diff + old (byte-wise), for addLen bytes.
        if (diffPos + addLen > diff.size())
            throw std::runtime_error("bspatch: diff block underrun");
        for (int64_t i = 0; i < addLen; ++i) {
            uint8_t ob = (oldpos + i >= 0 && oldpos + i < (int64_t)oldFile.size())
                             ? oldFile[(size_t)(oldpos + i)]
                             : 0;
            out[(size_t)(newpos + i)] = (uint8_t)(diff[diffPos + (size_t)i] + ob);
        }
        newpos += addLen;
        oldpos += addLen;
        diffPos += (size_t)addLen;

        // Copy extraLen bytes verbatim from the extra block.
        if (extraPos + extraLen > extra.size())
            throw std::runtime_error("bspatch: extra block underrun");
        std::memcpy(&out[(size_t)newpos], &extra[extraPos], (size_t)extraLen);
        newpos += extraLen;
        extraPos += (size_t)extraLen;

        oldpos += seek;
    }

    return out;
}

} // namespace forge::bspatch
