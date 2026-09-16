// forge::rangecodec — port of CRangeCompressor::Decompress @0x00f39ed0.
#include "forge/rangecodec.hpp"

#include <cstring>
#include <stdexcept>
#include <string>

namespace forge::rangecodec {
namespace {

// Width mask indexed by stride (local_b0): DAT_013ac944[stride] masks a value to
// `stride` bytes. stride ∈ {1,2,4}. (The engine reads *(u32*)(&DAT_013ac944 + b0*4).)
inline uint32_t widthMask(size_t stride) {
    switch (stride) {
        case 1: return 0x000000FFu;
        case 2: return 0x0000FFFFu;
        case 4: return 0xFFFFFFFFu;
        default: throw std::runtime_error("rangecodec: bad stride");
    }
}

// CalcShuffleOperations @0x00f39e50: expands a bitmask into (shift, mask) pairs.
// For each run of set bits in `bits`, emits one op: masks[i] = the run's field
// mask positioned at the low end, shifts[i] = how far to shift the source value
// left to land in that run. Returns the op count. Mirrors the decomp exactly.
int calcShuffle(uint32_t bits, uint32_t* masks, uint32_t* shifts) {
    int count = 0;
    int consumed = 0; // total set bits emitted so far (iVar4 in decomp)
    for (int i = 0; i < 32;) {
        if (bits & (1u << i)) {
            int j = i;
            uint32_t field = 0;
            while (j < 32 && (bits & (1u << j))) {
                field |= (1u << j);
                ++j;
            }
            masks[count] = field;
            shifts[count] = uint32_t(i - consumed);
            consumed += (j - i);
            ++count;
            i = j;
        } else {
            ++i;
        }
    }
    return count;
}

inline void storeElem(uint8_t* dst, uint32_t v, size_t stride) {
    if (stride == 4) std::memcpy(dst, &v, 4);
    else if (stride == 2) { uint16_t s = uint16_t(v); std::memcpy(dst, &s, 2); }
    else *dst = uint8_t(v);
}

} // namespace

std::vector<uint8_t> decode(const uint8_t* p, size_t blockLen, size_t count, size_t stride) {
    if (blockLen < 1) throw std::runtime_error("rangecodec: empty block");
    const uint8_t* end = p + blockLen;
    std::vector<uint8_t> out(count * stride);

    // RAW / stored fast path (flags == 0): verbatim element bytes follow.
    if (p[0] == 0) {
        size_t total = count * stride;
        if (blockLen < 1 + total) throw std::runtime_error("rangecodec: RAW block too short");
        std::memcpy(out.data(), p + 1, total);
        return out;
    }

    // Compressed path: a sequence of column descriptors, one per output column of
    // width `stride` (the decoder walks `param_4 += stride` per column). `desc` is
    // the descriptor flag byte; the loop ends when a descriptor has its high bit set.
    uint8_t desc = p[1];
    const uint8_t* q = p + 2;            // pbVar12
    uint8_t* colBase = out.data();       // param_4
    size_t columnBytes = 0;

    auto need = [&](const uint8_t* at, size_t n) {
        if (at + n > end) throw std::runtime_error("rangecodec: truncated stream");
    };

    while (true) {
        if (int8_t(desc) < 0) break;     // high bit set -> done

        size_t b0;                       // element byte-width for this column (local_b0)
        if (desc & 0x40) b0 = 4;
        else b0 = ((desc & 0x20) != 0) ? 2 : 1;
        if (columnBytes + b0 > stride)
            throw std::runtime_error("rangecodec: descriptor columns exceed stride");
        const uint32_t mask = widthMask(b0);

        need(q, 1);
        uint8_t bitW = *q;               // bVar2: bits-per-packed-value
        uint32_t vBits = bitW;
        ++q;                             // puVar13 = pbVar12 + 1

        uint32_t addBias = 0;            // local_ac
        uint8_t shiftBias = 0;           // bVar3
        uint32_t orBias = 0;             // local_a8
        int nShuffle = 0;                // local_a4
        uint32_t shMask[32] = {0};       // local_40
        uint32_t shShift[32] = {0};      // local_80

        auto readField = [&]() -> uint32_t {
            need(q, b0);
            uint32_t v;
            if (b0 == 4) { std::memcpy(&v, q, 4); }
            else if (b0 == 2) { uint16_t s; std::memcpy(&s, q, 2); v = s; }
            else { v = *q; }
            q += b0;
            return v;
        };

        if (desc & 0x11) addBias = readField();                  // bias (pre and/or post)
        if (desc & 0x02) { need(q, 1); shiftBias = *q; ++q; }    // left-shift after decode
        if (desc & 0x08) orBias = readField();                   // OR mask
        if (desc & 0x04) {                                        // shuffle/scatter
            uint32_t sbits = readField();
            nShuffle = calcShuffle(sbits, shMask, shShift);
        }

        uint32_t valMask = (bitW >= 32) ? 0xFFFFFFFFu : ((1u << (bitW & 31)) - 1);
        int bitPos = (vBits == 0) ? 0 : 32; // iVar9: force a fresh word load first
        uint32_t curWord = 0;               // local_9c

        uint8_t* dst = colBase;
        for (size_t row = 0; row < count; ++row) {
            uint32_t raw;
            if (vBits == 0x20) {
                need(q, 4); std::memcpy(&raw, q, 4); q += 4;
            } else {
                if (bitPos == 32) {
                    need(q, 4); std::memcpy(&curWord, q, 4); q += 4; bitPos = 0;
                }
                uint32_t got = curWord >> (bitPos & 31);
                bitPos += int(vBits);
                raw = got & valMask;
                if (bitPos > 32) {
                    need(q, 4); std::memcpy(&curWord, q, 4); q += 4;
                    raw = valMask & (got | (curWord << ((uint8_t(bitW) - uint8_t(bitPos)) + 32 & 31)));
                    bitPos -= 32;
                }
            }
            uint32_t v = raw;
            if (desc & 0x01) v = (v + addBias) & mask;           // pre-bias add
            v <<= (shiftBias & 31);
            if (desc & 0x04) {                                   // scatter through shuffle ops
                uint32_t scattered = 0;
                for (int k = 0; k < nShuffle; ++k)
                    scattered |= (v << (shShift[k] & 31)) & shMask[k];
                v = scattered;
            }
            v |= orBias;
            if (desc & 0x10) v = (v + addBias) & mask;            // post-bias add
            storeElem(dst, v, b0);
            dst += stride;                                        // param_3 stride
        }

        // Next descriptor byte, and advance the column base by this column's width.
        need(q, 1);
        desc = *q;
        colBase += b0;
        columnBytes += b0;
        q += 1;
    }
    if (columnBytes != stride)
        throw std::runtime_error("rangecodec: descriptor columns do not fill stride");
    return out;
}

std::vector<uint8_t> encodeRaw(const uint8_t* elems, size_t count, size_t stride) {
    std::vector<uint8_t> out;
    out.reserve(1 + count * stride);
    out.push_back(0x00);                 // flags == 0 -> RAW/stored
    out.insert(out.end(), elems, elems + count * stride);
    return out;
}

std::vector<uint8_t> encodeByteColumns(const uint8_t* elems, size_t count, size_t stride) {
    if (stride == 0) throw std::runtime_error("rangecodec: zero stride");

    std::vector<uint8_t> out;
    out.push_back(0x01); // any non-zero value selects the descriptor stream

    for (size_t column = 0; column < stride; ++column) {
        uint8_t lo = 0xFF;
        uint8_t hi = 0x00;
        for (size_t row = 0; row < count; ++row) {
            const uint8_t v = elems[row * stride + column];
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
        if (count == 0) lo = hi = 0;

        const unsigned range = unsigned(hi) - unsigned(lo);
        uint8_t bitWidth = 0;
        while (bitWidth < 8 && ((1u << bitWidth) - 1u) < range) ++bitWidth;

        // Width bits 0 => one-byte output column.  Pre-bias reconstructs
        // value=(packed+lo)&0xff and also represents constant columns at 0 bits.
        const uint8_t descriptor = lo == 0 ? 0x00 : 0x01;
        out.push_back(descriptor);
        out.push_back(bitWidth);
        if (descriptor & 0x01) out.push_back(lo);

        if (bitWidth == 0) continue;
        uint32_t word = 0;
        unsigned used = 0;
        auto flushWord = [&]() {
            out.push_back(uint8_t(word));
            out.push_back(uint8_t(word >> 8));
            out.push_back(uint8_t(word >> 16));
            out.push_back(uint8_t(word >> 24));
            word = 0;
            used = 0;
        };
        for (size_t row = 0; row < count; ++row) {
            uint32_t value = uint32_t(uint8_t(elems[row * stride + column] - lo));
            unsigned remaining = bitWidth;
            while (remaining != 0) {
                const unsigned room = 32 - used;
                const unsigned take = remaining < room ? remaining : room;
                const uint32_t mask = take == 32 ? 0xFFFFFFFFu : ((1u << take) - 1u);
                word |= (value & mask) << used;
                value >>= take;
                used += take;
                remaining -= take;
                if (used == 32) flushWord();
            }
        }
        if (used != 0) flushWord();
    }

    out.push_back(0x80); // signed-negative descriptor terminates the stream
    return out;
}

std::vector<uint8_t> encodeFieldColumns(const uint8_t* elems, size_t count, size_t stride,
                                        const std::vector<size_t>& fieldWidths) {
    size_t totalWidth = 0;
    for (size_t width : fieldWidths) {
        if (width != 1 && width != 2 && width != 4)
            throw std::runtime_error("rangecodec: field width must be 1, 2, or 4");
        totalWidth += width;
    }
    if (totalWidth != stride) throw std::runtime_error("rangecodec: field widths != stride");

    auto readValue = [](const uint8_t* p, size_t width) -> uint32_t {
        if (width == 4) { uint32_t v; std::memcpy(&v, p, 4); return v; }
        if (width == 2) { uint16_t v; std::memcpy(&v, p, 2); return v; }
        return *p;
    };
    auto appendValue = [](std::vector<uint8_t>& out, uint32_t v, size_t width) {
        for (size_t i = 0; i < width; ++i) out.push_back(uint8_t(v >> (i * 8)));
    };
    auto packValues = [](std::vector<uint8_t>& out, const std::vector<uint32_t>& values,
                         unsigned bitWidth) {
        if (bitWidth == 0) return;
        uint32_t word = 0;
        unsigned used = 0;
        auto flushWord = [&]() {
            out.push_back(uint8_t(word));
            out.push_back(uint8_t(word >> 8));
            out.push_back(uint8_t(word >> 16));
            out.push_back(uint8_t(word >> 24));
            word = 0;
            used = 0;
        };
        for (uint32_t value : values) {
            unsigned remaining = bitWidth;
            while (remaining != 0) {
                const unsigned room = 32 - used;
                const unsigned take = remaining < room ? remaining : room;
                const uint32_t mask = take == 32 ? 0xFFFFFFFFu : ((1u << take) - 1u);
                word |= (value & mask) << used;
                if (take != 32) value >>= take;
                else value = 0;
                used += take;
                remaining -= take;
                if (used == 32) flushWord();
            }
        }
        if (used != 0) flushWord();
    };

    std::vector<uint8_t> out;
    out.push_back(0x01);
    size_t columnOffset = 0;
    for (size_t width : fieldWidths) {
        const uint32_t mask = width == 4 ? 0xFFFFFFFFu : ((1u << (width * 8)) - 1u);
        uint32_t first = count == 0 ? 0 : readValue(elems + columnOffset, width);
        uint32_t varying = 0;
        uint32_t minimum = first;
        uint32_t maximum = first;
        for (size_t row = 1; row < count; ++row)
        {
            const uint32_t value = readValue(elems + row * stride + columnOffset, width);
            varying |= first ^ value;
            if (value < minimum) minimum = value;
            if (value > maximum) maximum = value;
        }
        varying &= mask;
        const uint32_t constantOnes = first & ~varying & mask;
        unsigned maskBitWidth = 0;
        for (uint32_t bits = varying; bits != 0; bits >>= 1) maskBitWidth += bits & 1u;
        const uint64_t numericRange = uint64_t(maximum) - uint64_t(minimum);
        unsigned rangeBitWidth = 0;
        while (rangeBitWidth < width * 8 &&
               ((uint64_t(1) << rangeBitWidth) - 1u) < numericRange)
            ++rangeBitWidth;
        const bool needsShuffle = varying != mask && varying != 0;
        const size_t maskSize = 2 + (constantOnes != 0 ? width : 0) +
            (needsShuffle ? width : 0) + ((count * maskBitWidth + 31) / 32) * 4;
        const size_t rangeSize = 2 + (minimum != 0 ? width : 0) +
            ((count * rangeBitWidth + 31) / 32) * 4;
        const bool useRange = rangeSize <= maskSize;
        const unsigned bitWidth = useRange ? rangeBitWidth : maskBitWidth;

        uint8_t descriptor = width == 4 ? 0x40 : (width == 2 ? 0x20 : 0x00);
        if (useRange) {
            if (minimum != 0) descriptor |= 0x01;
        } else {
            if (constantOnes != 0) descriptor |= 0x08;
            if (needsShuffle) descriptor |= 0x04;
        }
        out.push_back(descriptor);
        out.push_back(uint8_t(bitWidth));
        if (descriptor & 0x01) appendValue(out, minimum, width);
        if (descriptor & 0x08) appendValue(out, constantOnes, width);
        if (descriptor & 0x04) appendValue(out, varying, width);

        std::vector<uint32_t> packed(count, 0);
        for (size_t row = 0; row < count; ++row) {
            const uint32_t value = readValue(elems + row * stride + columnOffset, width);
            if (useRange) {
                packed[row] = value - minimum;
                continue;
            }
            if ((descriptor & 0x04) == 0) {
                packed[row] = value;
                continue;
            }
            uint32_t dense = 0;
            unsigned denseBit = 0;
            for (unsigned sourceBit = 0; sourceBit < width * 8; ++sourceBit) {
                const uint32_t sourceMask = uint32_t(1) << sourceBit;
                if ((varying & sourceMask) == 0) continue;
                if (value & sourceMask) dense |= uint32_t(1) << denseBit;
                ++denseBit;
            }
            packed[row] = dense;
        }
        packValues(out, packed, bitWidth);
        columnOffset += width;
    }
    out.push_back(0x80);
    return out;
}

std::vector<uint8_t> spliceColumnRange(const std::vector<uint8_t>& authored,
                                       const uint8_t* donor, size_t donorLen,
                                       size_t count, size_t stride,
                                       size_t replaceOffset, size_t replaceLength) {
    struct ColumnChunk { size_t column = 0, width = 0, begin = 0, end = 0; };
    auto parse = [&](const uint8_t* block, size_t len) {
        if (len < 2 || block[0] == 0)
            throw std::runtime_error("rangecodec: splice requires compressed descriptor streams");
        std::vector<ColumnChunk> chunks;
        size_t pos = 1;
        size_t column = 0;
        while (true) {
            if (pos >= len) throw std::runtime_error("rangecodec: truncated descriptor stream");
            const size_t begin = pos;
            const uint8_t desc = block[pos++];
            if (int8_t(desc) < 0) break;
            const size_t width = (desc & 0x40) ? 4 : ((desc & 0x20) ? 2 : 1);
            if (column + width > stride || pos >= len)
                throw std::runtime_error("rangecodec: invalid descriptor column");
            const uint8_t bitWidth = block[pos++];
            if (desc & 0x11) pos += width;
            if (desc & 0x02) pos += 1;
            if (desc & 0x08) pos += width;
            if (desc & 0x04) pos += width;
            const size_t dataBytes = bitWidth == 32
                ? count * 4
                : ((count * size_t(bitWidth) + 31) / 32) * 4;
            pos += dataBytes;
            if (pos > len) throw std::runtime_error("rangecodec: descriptor data truncated");
            chunks.push_back(ColumnChunk{column, width, begin, pos});
            column += width;
        }
        if (column != stride)
            throw std::runtime_error("rangecodec: descriptor columns do not fill splice stride");
        return chunks;
    };

    if (replaceOffset + replaceLength > stride)
        throw std::runtime_error("rangecodec: splice range exceeds stride");
    const auto authoredChunks = parse(authored.data(), authored.size());
    const auto donorChunks = parse(donor, donorLen);
    const size_t replaceEnd = replaceOffset + replaceLength;
    auto appendRange = [&](std::vector<uint8_t>& out, const uint8_t* block,
                           const std::vector<ColumnChunk>& chunks,
                           size_t beginColumn, size_t endColumn) {
        size_t covered = beginColumn;
        for (const auto& chunk : chunks) {
            if (chunk.column + chunk.width <= beginColumn || chunk.column >= endColumn) continue;
            if (chunk.column < beginColumn || chunk.column + chunk.width > endColumn)
                throw std::runtime_error("rangecodec: descriptor crosses splice boundary");
            if (chunk.column != covered)
                throw std::runtime_error("rangecodec: splice column gap");
            out.insert(out.end(), block + chunk.begin, block + chunk.end);
            covered += chunk.width;
        }
        if (covered != endColumn) throw std::runtime_error("rangecodec: incomplete splice range");
    };

    std::vector<uint8_t> out;
    out.push_back(0x01);
    appendRange(out, authored.data(), authoredChunks, 0, replaceOffset);
    appendRange(out, donor, donorChunks, replaceOffset, replaceEnd);
    appendRange(out, authored.data(), authoredChunks, replaceEnd, stride);
    out.push_back(0x80);
    return out;
}

std::vector<uint8_t> addColumnConstant(const uint8_t* block, size_t blockLen,
                                       size_t count, size_t stride,
                                       size_t columnOffset, size_t columnWidth,
                                       int32_t delta) {
    if (blockLen < 2 || block[0] == 0)
        throw std::runtime_error("rangecodec: bias edit requires descriptor stream");
    if ((columnWidth != 1 && columnWidth != 2 && columnWidth != 4) ||
        columnOffset + columnWidth > stride)
        throw std::runtime_error("rangecodec: invalid bias-edit column");

    std::vector<uint8_t> out(block, block + blockLen);
    const std::vector<uint8_t> before = decode(block, blockLen, count, stride);
    size_t pos = 1;
    size_t column = 0;
    bool found = false;
    std::string layout;
    while (pos < blockLen) {
        const uint8_t desc = out[pos++];
        if (int8_t(desc) < 0) break;
        const size_t width = (desc & 0x40) ? 4 : ((desc & 0x20) ? 2 : 1);
        layout += (layout.empty() ? "" : ",") + std::to_string(column) + ":" +
                  std::to_string(width) + "/" + std::to_string(desc);
        if (column + width > stride || pos >= blockLen)
            throw std::runtime_error("rangecodec: invalid bias-edit descriptor");
        const uint8_t bitWidth = out[pos++];
        const size_t biasPos = pos;
        if (desc & 0x11) pos += width;
        if (desc & 0x02) pos += 1;
        const size_t orBiasPos = pos;
        if (desc & 0x08) pos += width;
        const size_t shufflePos = pos;
        if (desc & 0x04) pos += width;
        const size_t dataBytes = bitWidth == 32
            ? count * 4
            : ((count * size_t(bitWidth) + 31) / 32) * 4;
        pos += dataBytes;
        if (pos > blockLen)
            throw std::runtime_error("rangecodec: truncated bias-edit descriptor");

        if (column == columnOffset && width == columnWidth) {
            const bool preAdd = (desc & 0x01) != 0;
            const bool postAdd = (desc & 0x10) != 0;
            const uint32_t mask = widthMask(width);
            if (preAdd != postAdd) {
                uint32_t bias = 0;
                for (size_t i = 0; i < width; ++i)
                    bias |= uint32_t(out[biasPos + i]) << (i * 8);
                bias = (bias + uint32_t(delta)) & mask;
                for (size_t i = 0; i < width; ++i)
                    out[biasPos + i] = uint8_t(bias >> (i * 8));
            } else if (!preAdd && (desc & 0x0c) == 0x0c) {
                uint32_t varyingMask = 0;
                for (size_t i = 0; i < width; ++i)
                    varyingMask |= uint32_t(out[shufflePos + i]) << (i * 8);
                varyingMask &= mask;
                uint32_t fixedBits = 0;
                for (size_t row = 0; row < count; ++row) {
                    uint32_t oldValue = 0;
                    for (size_t i = 0; i < width; ++i)
                        oldValue |= uint32_t(before[row * stride + column + i]) << (i * 8);
                    const uint32_t desired = (oldValue + uint32_t(delta)) & mask;
                    if ((desired & varyingMask) != (oldValue & varyingMask))
                        throw std::runtime_error(
                            "rangecodec: delta changes packed residual bits");
                    const uint32_t rowFixed = desired & ~varyingMask & mask;
                    if (row == 0) fixedBits = rowFixed;
                    else if (rowFixed != fixedBits)
                        throw std::runtime_error(
                            "rangecodec: delta needs row-dependent OR bias");
                }
                for (size_t i = 0; i < width; ++i)
                    out[orBiasPos + i] = uint8_t(fixedBits >> (i * 8));
            } else {
                throw std::runtime_error("rangecodec: target has ambiguous/no editable bias");
            }
            found = true;
        } else if (column < columnOffset && column + width > columnOffset) {
            throw std::runtime_error("rangecodec: descriptor crosses bias-edit boundary");
        }
        column += width;
    }
    if (!found)
        throw std::runtime_error("rangecodec: bias-edit column not found (layout " +
                                 layout + ")");

    const std::vector<uint8_t> after = decode(out.data(), out.size(), count, stride);
    const uint32_t mask = widthMask(columnWidth);
    auto readValue = [&](const std::vector<uint8_t>& bytes, size_t base) {
        uint32_t value = 0;
        for (size_t i = 0; i < columnWidth; ++i)
            value |= uint32_t(bytes[base + i]) << (i * 8);
        return value;
    };
    for (size_t row = 0; row < count; ++row) {
        const size_t base = row * stride;
        for (size_t i = 0; i < stride; ++i) {
            if (i >= columnOffset && i < columnOffset + columnWidth) continue;
            if (before[base + i] != after[base + i])
                throw std::runtime_error("rangecodec: bias edit changed another column");
        }
        const uint32_t expected = (readValue(before, base + columnOffset) +
                                   uint32_t(delta)) & mask;
        if (readValue(after, base + columnOffset) != expected)
            throw std::runtime_error("rangecodec: bias edit verification failed");
    }
    return out;
}

} // namespace forge::rangecodec
