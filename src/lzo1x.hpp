#pragma once
// Clean-room LZO1X codec (MIT). Fable's STB chunks and texture streams are
// stock LZO1X; this replaces the GPL minilzo in the shipped binaries. The
// decoder is verified byte-exact against minilzo on every frame of retail
// chunks, and the encoder's output is verified to decode identically through
// both decoders (tests/test_lzo.cpp).

#include <cstddef>
#include <cstdint>
#include <vector>

namespace albion::lzo1x {

enum class Status { Ok, InputOverrun, OutputOverrun, LookbehindOverrun, Corrupt, InputNotConsumed };

// Decodes `in[0..inLen)` into `out[0..*outLen)`. On entry *outLen is the
// output capacity; on return it is the number of bytes produced (also for
// OutputOverrun, where it equals the capacity). The stream must end with the
// LZO1X end marker; trailing input bytes yield InputNotConsumed but the output
// is still complete.
Status decompress(const uint8_t* in, size_t inLen, uint8_t* out, size_t* outLen);

const char* statusName(Status s);

// Encodes `in[0..inLen)` as an LZO1X stream (greedy single-slot hash matcher,
// LZO1X-1 class ratio, ending with the end marker). Any decoder that accepts
// the LZO1X grammar -- the retail engine's included -- decodes it; it does not
// reproduce minilzo's exact byte choices.
std::vector<uint8_t> compress(const uint8_t* in, size_t inLen);

} // namespace albion::lzo1x
