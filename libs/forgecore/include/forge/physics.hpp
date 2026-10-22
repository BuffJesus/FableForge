#pragma once

#include <cstdint>
#include <span>

namespace forge::physics {

// Validate supported type-3 graphics.big 3DMF physics storage. All chunk bodies
// must have known, bounded layouts; unknown extensions and compiled cloth refuse.
// Supports a checked subset of text cloth programs with local symbol/UV lookup.
// This validates storage, not geometry quality or binary local-index topology.
// The caller owns BIG bank/type/subheader checks. No input is modified.
void validateStorage(std::span<const uint8_t> payload);

} // namespace forge::physics
