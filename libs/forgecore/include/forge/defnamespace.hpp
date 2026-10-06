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

struct Contributor {
    uint32_t targetSlot;
    // Identity/ordinal in the planned namespace; payload relocated using only
    // this contributor's source map. This is not a selected winning version.
    bin::Entry entry;
};
struct Prepared {
    Plan allocation;
    // Outer index is the source, inner index its original game.bin slot.
    // Absent rows in shorter sources stay absent; no placeholder contributors.
    std::vector<std::vector<Contributor>> sources;
};

// Allocate and relocate every game.bin contributor before comparing versions.
// Refuses unresolved payload candidates without changing any input. The
// allocation's payloads remain source-local: use sources for version selection.
// This does not certify sibling banks or external files and does not authorize
// serialization; those checks must precede composer admission separately.
Prepared prepare(Input baseline, std::span<const Input> sources,
                 const defschema::Schema& schema);

} // namespace forge::defnamespace
