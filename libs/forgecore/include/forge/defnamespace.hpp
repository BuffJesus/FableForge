#pragma once

#include "forge/defrelocate.hpp"
#include <span>
#include <map>

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

struct NameLookupChange {
    uint32_t crc = 0;
    std::vector<uint32_t> sourceSlotsInPlan;
    std::vector<uint32_t> plannedSlots;
};
struct SourceNameLookups {
    size_t existingQueryGroups = 0;
    size_t addedQueryGroups = 0;
    std::vector<NameLookupChange> changed;
};
struct NameLookupAnalysis {
    // All compiled CRC candidates, including anonymous rows under CRC zero.
    // Slot order is diagnostic, not the native equal-key sorting order.
    std::map<uint32_t, std::vector<uint32_t>> plannedCandidates;
    // Ambiguities reached by nonempty names already present in the plan.
    // Other query strings can still collide with any plannedCandidates group.
    std::vector<uint32_t> ambiguousNamedCrcs;
    std::vector<SourceNameLookups> sources;
    // Empty queries bypass the native table and have no candidates here.
    std::span<const uint32_t> candidatesFor(std::string_view name) const;
};

// Check literal/stored name CRCs and source-map identity/injectivity before
// comparing candidate sets. Throws on inconsistent pools/plans. Does not choose
// duplicate winners or certify runtime class registration, payloads, external
// query coverage, source admission or serialization safety.
NameLookupAnalysis analyzeNameLookups(const Plan& plan, std::span<const Input> sources);

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
