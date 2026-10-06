#pragma once

#include "forge/defrelocate.hpp"
#include <span>

namespace forge::defnamespace {

using Name = std::pair<uint32_t, std::string>; // stored CRC, exact string
struct Input {
    std::span<const bin::Entry> entries;
    std::span<const Name> names;
};
struct Plan {
    // Identity/layout only. Payloads retain their source-local references and
    // MUST pass relocation and external-reference checks before serialization.
    std::vector<bin::Entry> entries;
    std::vector<Name> names;
    std::vector<defrelocate::Maps> sources;
};

// Preserve every baseline slot and name offset. Coalesce unique named additions
// by exact (definition,name); anonymous or duplicate-name additions belong to
// their source alone. Additions reusing baseline names also retain distinct IDs.
// Shared baseline identities must agree; shorter sources leave baseline intact.
// Maps contain changed IDs only. No input is mutated and no files are written.
// This plans game.bin IDs only, not script/frontend IDs or external references.
Plan allocate(Input baseline, std::span<const Input> sources);

} // namespace forge::defnamespace
