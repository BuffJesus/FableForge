#pragma once
// Clean-room LZO1X decompressor (MIT). Fable's STB chunks and texture streams
// are stock LZO1X; only decoding is needed, so this replaces the GPL minilzo
// in the shipped binaries. Verified byte-exact against minilzo on every frame
// of retail chunks by tests/test_lzo.cpp.

#include <cstddef>
#include <cstdint>

namespace albion::lzo1x {

enum class Status { Ok, InputOverrun, OutputOverrun, LookbehindOverrun, Corrupt, InputNotConsumed };

// Decodes `in[0..inLen)` into `out[0..*outLen)`. On entry *outLen is the
// output capacity; on return it is the number of bytes produced (also for
// OutputOverrun, where it equals the capacity). The stream must end with the
// LZO1X end marker; trailing input bytes yield InputNotConsumed but the output
// is still complete.
Status decompress(const uint8_t* in, size_t inLen, uint8_t* out, size_t* outLen);

const char* statusName(Status s);

} // namespace albion::lzo1x
