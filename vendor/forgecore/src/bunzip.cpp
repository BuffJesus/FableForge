#include "forge/bunzip.hpp"

#include <cstring>
#include <stdexcept>

namespace forge::bunzip {
namespace {

struct BitReader {
    const uint8_t* p;
    size_t len;
    size_t bytePos = 0;
    uint32_t bitBuf = 0;
    int bitCnt = 0;

    BitReader(const uint8_t* d, size_t n) : p(d), len(n) {}

    // Read n bits MSB-first (bzip2 is big-endian bit order). n in [0,32].
    uint32_t bits(int n) {
        while (bitCnt < n) {
            if (bytePos >= len) throw std::runtime_error("bunzip: truncated stream");
            bitBuf = (bitBuf << 8) | p[bytePos++];
            bitCnt += 8;
        }
        bitCnt -= n;
        return n == 0 ? 0u : (bitBuf >> bitCnt) & ((n == 32) ? 0xFFFFFFFFu : ((1u << n) - 1));
    }
    uint32_t bit() { return bits(1); }
};

// One Huffman table decoded from bzip2's canonical (delta) code lengths.
struct Huff {
    int minLen = 0, maxLen = 0;
    int limit[25] = {};   // per code length, largest code value + 1
    int base[25] = {};
    int perm[258] = {};   // symbol at sorted position
    int count[25] = {};
};

void buildHuff(Huff& h, const uint8_t* lengths, int nSym) {
    int minLen = 32, maxLen = 0;
    for (int i = 0; i < nSym; ++i) {
        if (lengths[i] > maxLen) maxLen = lengths[i];
        if (lengths[i] < minLen) minLen = lengths[i];
    }
    h.minLen = minLen;
    h.maxLen = maxLen;

    // perm[]: symbols sorted by (length, symbol).
    int pp = 0;
    for (int len = minLen; len <= maxLen; ++len)
        for (int sym = 0; sym < nSym; ++sym)
            if (lengths[sym] == len) h.perm[pp++] = sym;

    for (int i = 0; i <= maxLen + 1; ++i) h.count[i] = 0;
    for (int i = 0; i < nSym; ++i) h.count[lengths[i] + 1]++;
    for (int i = 1; i <= maxLen + 1; ++i) h.count[i] += h.count[i - 1];

    int vec = 0;
    for (int len = minLen; len <= maxLen; ++len) {
        vec += h.count[len + 1] - h.count[len];
        h.limit[len] = vec - 1;
        vec <<= 1;
    }
    for (int len = minLen + 1; len <= maxLen; ++len)
        h.base[len] = ((h.limit[len - 1] + 1) << 1) - h.count[len];
}

int decodeSym(BitReader& br, const Huff& h) {
    int len = h.minLen;
    int code = (int)br.bits(len);
    while (true) {
        if (len > h.maxLen) throw std::runtime_error("bunzip: bad huffman code");
        if (code <= h.limit[len]) break;
        code = (code << 1) | (int)br.bit();
        ++len;
    }
    int idx = code - h.base[len];
    if (idx < 0 || idx >= 258) throw std::runtime_error("bunzip: huffman index oob");
    return h.perm[idx];
}

} // namespace

std::vector<uint8_t> decompress(const uint8_t* data, size_t len) {
    BitReader br(data, len);

    if (br.bits(8) != 'B' || br.bits(8) != 'Z' || br.bits(8) != 'h')
        throw std::runtime_error("bunzip: bad magic (expected BZh)");
    int level = (int)br.bits(8) - '0';
    if (level < 1 || level > 9)
        throw std::runtime_error("bunzip: bad block-size level");
    const int blockSize = level * 100000;

    std::vector<uint8_t> out;

    while (true) {
        // Block header: 48-bit magic.
        uint32_t hi = br.bits(24), lo = br.bits(24);
        if (hi == 0x314159 && lo == 0x265359) {
            // compressed block
        } else if (hi == 0x177245 && lo == 0x385090) {
            br.bits(32); // stream CRC
            break;       // end of stream
        } else {
            throw std::runtime_error("bunzip: bad block magic");
        }

        br.bits(32);              // block CRC (not verified)
        if (br.bit()) throw std::runtime_error("bunzip: randomized blocks unsupported");
        uint32_t origPtr = br.bits(24);

        // Symbol map: which of the 256 byte values are present.
        uint16_t used16 = (uint16_t)br.bits(16);
        uint8_t seqToUnseq[256];
        int nInUse = 0;
        for (int i = 0; i < 16; ++i) {
            if (used16 & (0x8000 >> i)) {
                uint16_t bits16 = (uint16_t)br.bits(16);
                for (int j = 0; j < 16; ++j)
                    if (bits16 & (0x8000 >> j)) seqToUnseq[nInUse++] = (uint8_t)(i * 16 + j);
            }
        }
        if (nInUse == 0) throw std::runtime_error("bunzip: empty symbol map");
        const int alphaSize = nInUse + 2; // + RUNA/RUNB collapse... actually +2 for EOB & RUNB

        // Selectors.
        int nGroups = (int)br.bits(3);
        if (nGroups < 2 || nGroups > 6) throw std::runtime_error("bunzip: bad group count");
        int nSelectors = (int)br.bits(15);
        std::vector<uint8_t> selMtf((size_t)nSelectors);
        for (int i = 0; i < nSelectors; ++i) {
            int j = 0;
            while (br.bit()) { if (++j >= nGroups) throw std::runtime_error("bunzip: bad selector"); }
            selMtf[i] = (uint8_t)j;
        }
        // MTF-decode selectors.
        uint8_t pos[6];
        for (int i = 0; i < nGroups; ++i) pos[i] = (uint8_t)i;
        std::vector<uint8_t> selector((size_t)nSelectors);
        for (int i = 0; i < nSelectors; ++i) {
            int v = selMtf[i];
            uint8_t tmp = pos[v];
            for (int k = v; k > 0; --k) pos[k] = pos[k - 1];
            pos[0] = tmp;
            selector[i] = tmp;
        }

        // Huffman tables (code lengths delta-coded).
        Huff tables[6];
        for (int g = 0; g < nGroups; ++g) {
            uint8_t lengths[258];
            int curr = (int)br.bits(5);
            for (int s = 0; s < alphaSize; ++s) {
                while (br.bit()) { if (br.bit()) --curr; else ++curr; }
                if (curr < 1 || curr > 20) throw std::runtime_error("bunzip: bad code length");
                lengths[s] = (uint8_t)curr;
            }
            buildHuff(tables[g], lengths, alphaSize);
        }

        // Decode MTF/RLE2 values into the BWT byte array.
        const int EOB = alphaSize - 1;
        std::vector<uint32_t> tt;
        tt.reserve((size_t)blockSize);
        int counts[256] = {};
        uint8_t mtf[256];
        for (int i = 0; i < nInUse; ++i) mtf[i] = seqToUnseq[i];

        int groupNo = -1, groupPos = 0;
        const Huff* h = nullptr;
        int runLen = 0;
        int64_t runBit = 0;

        auto nextSym = [&]() -> int {
            if (groupPos == 0) {
                if (++groupNo >= nSelectors) throw std::runtime_error("bunzip: selector overrun");
                groupPos = 50;
                h = &tables[selector[groupNo]];
            }
            --groupPos;
            return decodeSym(br, *h);
        };

        while (true) {
            int sym = nextSym();
            if (sym == EOB) break;
            if (sym == 0 || sym == 1) {
                // RUNA/RUNB run-length of the front byte (mtf[0]).
                if (runLen == 0) runBit = 0;
                runLen += (sym + 1) << runBit;
                ++runBit;
                continue;
            }
            if (runLen > 0) {
                uint8_t b = mtf[0];
                counts[b] += runLen;
                for (int k = 0; k < runLen; ++k) tt.push_back(b);
                runLen = 0;
            }
            // MTF value: symbol index sym-1 into the mtf list.
            int idx = sym - 1;
            uint8_t b = mtf[idx];
            for (int k = idx; k > 0; --k) mtf[k] = mtf[k - 1];
            mtf[0] = b;
            counts[b]++;
            tt.push_back(b);
        }
        if (runLen > 0) {
            uint8_t b = mtf[0];
            counts[b] += runLen;
            for (int k = 0; k < runLen; ++k) tt.push_back(b);
        }

        const uint32_t nblock = (uint32_t)tt.size();
        if (origPtr >= nblock) throw std::runtime_error("bunzip: origPtr oob");

        // Inverse BWT. Build the T vector: cumulative base per byte value, then
        // link each position to the next in original order.
        int base[257];
        base[0] = 0;
        for (int i = 0; i < 256; ++i) base[i + 1] = base[i] + counts[i];
        std::vector<uint32_t> next(nblock);
        {
            int cum[256];
            for (int i = 0; i < 256; ++i) cum[i] = base[i];
            for (uint32_t i = 0; i < nblock; ++i) {
                uint8_t b = (uint8_t)tt[i];
                next[cum[b]++] = i;
            }
        }

        // Walk the BWT and apply the final RLE (RLE1: 4 equal bytes + count).
        uint32_t tPos = next[origPtr];
        uint32_t remaining = nblock;
        int runCount = 0;
        int lastByte = -1;
        while (remaining-- > 0) {
            uint8_t b = (uint8_t)tt[tPos];
            tPos = next[tPos];
            if (runCount == 4) {
                // b is the RLE length: append b copies of lastByte.
                for (int k = 0; k < b; ++k) out.push_back((uint8_t)lastByte);
                runCount = 0;
                lastByte = -1;
                continue;
            }
            if ((int)b == lastByte) ++runCount; else { runCount = 1; lastByte = b; }
            out.push_back(b);
        }
    }

    return out;
}

} // namespace forge::bunzip
