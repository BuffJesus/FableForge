#pragma once
// Internal helpers shared by the Lionhead text formats (.tng, .wld):
// lines of "Key Value;" grouped by NewX/EndX block markers.

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace forge::textformat {

inline bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

inline std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

inline std::string stripQuotes(std::string value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

// Splits text into lines, each keeping its own terminator, so concatenating
// them reproduces the input byte-for-byte.
inline std::vector<std::string> splitRawLines(const std::string& text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, end - start + 1));
        start = end + 1;
    }
    return lines;
}

// Raw line -> logical "Key" / "rest of line" with whitespace and the trailing
// ';' removed. Returns false for blank lines.
inline bool parseLogicalLine(std::string_view raw, std::string& key, std::string& value) {
    std::string_view logical = trim(raw);
    if (!logical.empty() && logical.back() == ';') {
        logical = trim(logical.substr(0, logical.size() - 1));
    }
    if (logical.empty()) return false;

    size_t split = 0;
    while (split < logical.size() &&
           !std::isspace(static_cast<unsigned char>(logical[split]))) {
        ++split;
    }
    key = std::string(logical.substr(0, split));
    value = std::string(trim(logical.substr(split)));
    return true;
}

} // namespace forge::textformat
