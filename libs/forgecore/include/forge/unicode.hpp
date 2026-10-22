#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace forge::unicode {
// Shared strict conversion for native NUL-terminated UTF-16LE text.
inline std::string readUtf16z(const std::vector<uint8_t>& b, size_t& p) {
    std::string out;
    bool terminated=false;
    while (p < b.size() && b.size() - p >= 2) {
        uint32_t c = uint32_t(b[p]) | uint32_t(b[p + 1]) << 8;
        p += 2;
        if (c == 0) { terminated=true; break; }
        uint32_t cp = c;
        if (c >= 0xD800 && c <= 0xDBFF) {
            if (p >= b.size() || b.size() - p < 2)
                throw std::runtime_error("unicode: truncated UTF-16 surrogate");
            const uint32_t low = uint32_t(b[p]) | uint32_t(b[p + 1]) << 8;
            if (low < 0xDC00 || low > 0xDFFF)
                throw std::runtime_error("unicode: invalid UTF-16 surrogate");
            p += 2;
            cp = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
        } else if (c >= 0xDC00 && c <= 0xDFFF) {
            throw std::runtime_error("unicode: unexpected UTF-16 low surrogate");
        }
        if (cp < 0x80) {
            out.push_back(char(cp));
        } else if (cp < 0x800) {
            out.push_back(char(0xC0 | (cp >> 6)));
            out.push_back(char(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(char(0xE0 | (cp >> 12)));
            out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(char(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(char(0xF0 | (cp >> 18)));
            out.push_back(char(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(char(0x80 | (cp & 0x3F)));
        }
    }
    if (!terminated) throw std::runtime_error("unicode: unterminated UTF-16 content");
    return out;
}

inline void appendUtf16z(std::vector<uint8_t>& out, std::string_view utf8) {
    if (utf8.find('\0') != std::string::npos)
        throw std::runtime_error("unicode: embedded NUL in text content");
    size_t p = 0;
    while (p < utf8.size()) {
        const uint8_t lead = (uint8_t)utf8[p++];
        uint32_t cp = 0;
        unsigned continuation = 0;
        if (lead < 0x80) {
            cp = lead;
        } else if ((lead & 0xE0) == 0xC0) {
            cp = lead & 0x1F;
            continuation = 1;
        } else if ((lead & 0xF0) == 0xE0) {
            cp = lead & 0x0F;
            continuation = 2;
        } else if ((lead & 0xF8) == 0xF0) {
            cp = lead & 0x07;
            continuation = 3;
        } else {
            throw std::runtime_error("unicode: invalid UTF-8 lead byte");
        }
        for (unsigned i = 0; i < continuation; ++i) {
            if (p >= utf8.size() || ((uint8_t)utf8[p] & 0xC0) != 0x80)
                throw std::runtime_error("unicode: invalid UTF-8 continuation");
            cp = (cp << 6) | ((uint8_t)utf8[p++] & 0x3F);
        }
        if ((continuation == 1 && cp < 0x80) ||
            (continuation == 2 && cp < 0x800) ||
            (continuation == 3 && cp < 0x10000) ||
            cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            throw std::runtime_error("unicode: invalid UTF-8 code point");

        auto put16 = [&](uint16_t v) {
            out.push_back(uint8_t(v));
            out.push_back(uint8_t(v >> 8));
        };
        if (cp <= 0xFFFF) {
            put16((uint16_t)cp);
        } else {
            cp -= 0x10000;
            put16((uint16_t)(0xD800 + (cp >> 10)));
            put16((uint16_t)(0xDC00 + (cp & 0x3FF)));
        }
    }
    out.push_back(0);
    out.push_back(0);
}
} // namespace forge::unicode
