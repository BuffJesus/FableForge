#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace forge::levstorage {
struct Review {
    uint32_t width=0,height=0;
    size_t heightCells=0,gameCells=0,navigationSections=0,navigationNodes=0;
    // Used palette names and sound names, retaining order and duplicates.
    // Numeric palette words are nonzero presence gates; native LoadFromFile
    // replaces them by lookup results. Callers must resolve these symbols.
    std::vector<std::string> groundThemes,atmosThemes,soundThemes;
};
// Complete read-only storage review for level v4, map header v8/v9, cell-map
// version 3, extra boundary cells, empty script brushes and navigation v5..8.
// Throws on unsupported/malformed storage. Does not certify named-definition
// resolution, geometry, navigation graph behavior or runtime compatibility.
Review validate(const std::vector<uint8_t>& bytes);
}
