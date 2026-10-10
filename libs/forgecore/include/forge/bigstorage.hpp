#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace forge::bigstorage {
struct Storage {
    size_t banks = 0;
    size_t entries = 0;
    uint64_t payloadBytes = 0;
    uint64_t zeroPaddingBytes = 0;
};
// Strict observed BIGB envelope for PC graphics/texture, main particle and English text banks.
// Reuses big::File for entries; verifies every metadata extent, histogram,
// identity, aligned payload span and unowned zero byte. Throws on refusal.
// Entry info/payload semantics and cross-asset references require separate checks.
// This is not FMP review or whole-source admission. No file is modified.
Storage validateEnvelope(const std::filesystem::path& path);
}
