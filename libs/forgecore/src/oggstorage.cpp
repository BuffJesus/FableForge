#include "forge/oggstorage.hpp"
#include <array>
#include <stdexcept>
#include <string>

namespace forge::oggstorage {
namespace {
// Xiph framing specification: https://www.xiph.org/ogg/doc/framing.html
constexpr auto crcTable = [] {
    std::array<uint32_t,256> table{};
    for (uint32_t i=0; i<256; ++i) {
        uint32_t value = i << 24;
        for (unsigned bit=0; bit<8; ++bit)
            value = (value << 1) ^ ((value & 0x80000000u) ? 0x04c11db7u : 0);
        table[i] = value;
    }
    return table;
}();
uint32_t word(std::span<const uint8_t> bytes, size_t at) {
    return uint32_t(bytes[at]) | (uint32_t(bytes[at+1]) << 8) |
        (uint32_t(bytes[at+2]) << 16) | (uint32_t(bytes[at+3]) << 24);
}
}
Storage validateEnvelope(std::span<const uint8_t> bytes) {
    Storage result;
    size_t at = 0;
    uint32_t sequence = 0;
    bool continued = false, ended = false;
    auto require = [&](bool condition, const char* reason) {
        if (!condition) throw std::runtime_error("oggstorage: byte " + std::to_string(at) + ": " + reason);
    };
    while (at < bytes.size()) {
        require(!ended, "bytes after end of logical stream (chaining unsupported)");
        const auto page = bytes.subspan(at);
        require(page.size() >= 27, "truncated page header");
        require(page[0]=='O' && page[1]=='g' && page[2]=='g' && page[3]=='S', "invalid page capture");
        require(page[4]==0 && !(page[5]&~7u), "unsupported page version/flags");
        const auto flags = page[5];
        require(bool(flags&2) == (result.pages==0), "invalid beginning-of-stream flag");
        require(bool(flags&1) == continued, "packet continuation mismatch");
        require(word(page,18)==sequence, "noncontiguous page sequence");
        if (result.pages==0) result.serial=word(page,14);
        require(word(page,14)==result.serial, "multiple logical streams unsupported");
        const size_t segments=page[26], header=27+segments;
        require(segments!=0, "empty page segment table unsupported");
        require(page.size()>=header, "truncated segment table");
        size_t payload=0, finished=0;
        for (size_t i=27; i<header; ++i) {
            payload+=page[i];
            finished+=page[i]<255;
        }
        require(payload<=page.size()-header, "truncated page payload");
        const bool noGranule = word(page,6)==0xffffffffu && word(page,10)==0xffffffffu;
        require(noGranule == (finished==0), "granule sentinel disagrees with completed packets");
        require(noGranule || !(page[13]&0x80), "unsupported negative granule position");
        uint32_t crc=0;
        for (size_t i=0; i<header+payload; ++i) {
            const uint8_t value = i>=22 && i<26 ? 0 : page[i];
            crc=(crc<<8)^crcTable[(crc>>24)^value];
        }
        require(crc==word(page,22), "page checksum mismatch");
        continued=page[header-1]==255;
        ended=bool(flags&4);
        require(!ended || !continued, "unfinished packet at end of stream");
        ++result.pages; result.packets+=finished; result.payloadBytes+=payload;
        ++sequence; at+=header+payload;
    }
    require(ended && result.pages!=0, "missing end of stream");
    return result;
}
}
