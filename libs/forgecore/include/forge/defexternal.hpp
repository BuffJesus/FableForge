#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace forge::defexternal {

struct Review {
    bool definitionIndependent = false;
    std::string reason;
};

// Review complete known BIG entry layouts for independence from game.bin IDs
// and names.bin offsets. No raw-value absence or file hash implies acceptance.
// Unknown banks/types, opaque layouts and malformed inputs return false.
// Scope: supported PC textures, English text, compiled meshes, 3DAF animation
// and 3DMF physics.
// Does not certify archive metadata, other entries, asset-ID links, or a source
// installation. A composer must account for every external file separately.
Review reviewBigEntry(std::string_view bank, uint32_t type,
                      const std::vector<uint8_t>& subHeader,
                      const std::vector<uint8_t>& payload);

} // namespace forge::defexternal
