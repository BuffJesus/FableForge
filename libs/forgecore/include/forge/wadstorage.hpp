#pragma once
#include "forge/wad.hpp"
#include <span>

namespace forge::wadstorage {
struct Metadata {
    uint32_t alignment=0,maximumId=0,tableOffset=0;
    std::vector<wad::Entry> entries;
};
// Strict supported retail BBB/WAD metadata subset. Does not inspect payloads
// or gaps, resolve symbolic names, or certify archive paths for extraction.
// Entry timestamp is the native checksum; dependencies are filename strings.
// CFileInformation contains dates, a boolean and three uninterpreted padding
// bytes, not definition IDs. Preserve its raw bytes. Terminal string NULs are
// accepted but removed from returned names; embedded NULs are rejected.
Metadata validateMetadata(std::span<const uint8_t> header,
                          std::span<const uint8_t> table,uint64_t fileSize);
struct Storage {
    Metadata metadata;
    uint64_t zeroPaddingBytes=0;
};
// Compact archive envelope only: validate metadata and every unowned byte.
// Retained nonzero dead payloads/tables are unsupported. Entry contents still
// require their own validators (LEV, TNG, etc.). Never writes the input.
Storage validateEnvelope(const std::filesystem::path& path);
}
