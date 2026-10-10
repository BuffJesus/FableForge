#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace forge::oggstorage {
struct Storage {
    size_t pages = 0;
    size_t packets = 0;
    uint64_t payloadBytes = 0;
    uint32_t serial = 0;
};
// Strict single logical stream with nonempty page segment tables, contiguous
// sequence numbers, complete packet framing, BOS/EOS and verified page CRCs.
// Chaining/multiplexing and empty pages are outside this diagnostic subset.
// Codec packets, codec-specific granule semantics and playback remain unchecked.
// Throws on refusal; never mutates the input.
Storage validateEnvelope(std::span<const uint8_t> bytes);
}
