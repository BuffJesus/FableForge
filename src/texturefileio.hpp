#pragma once
#include <algorithm>
#include <cstdint>
#include <ostream>
#include <span>

namespace albion::texturefileio {
// Some Windows CRT-backed stream buffers use signed 32-bit results internally.
// Bound individual writes even though an archive and streamsize can exceed 2 GiB.
inline void write(std::ostream& out,std::span<const std::uint8_t> bytes) {
    constexpr std::size_t chunk=16u*1024*1024;
    while(!bytes.empty() && out) {
        const auto count=std::min(bytes.size(),chunk);
        out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(count));
        bytes=bytes.subspan(count);
    }
}
}
