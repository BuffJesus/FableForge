#pragma once
#include <cstdint>
#include <span>

namespace forge::meshstorage {
// Validate complete supported compiled-mesh Info and payload storage domains.
// Covers counted LODs, optional ghost LOD, local geometry/hierarchy, texture
// IDs, and supported compiled cloth. Unknown layouts throw std::runtime_error.
// Does not validate asset-ID destinations, geometry quality or full topology.
// Volumes, generators and exported cloth particles remain unsupported.
void validateStorage(std::span<const uint8_t> info,std::span<const uint8_t> payload);
}
