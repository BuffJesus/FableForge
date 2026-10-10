#pragma once

#include "forge/bin.hpp"
#include "forge/defschema.hpp"
#include <map>

namespace forge::defrelocate {

// Source-local identities -> composed identities. Missing keys mean unchanged.
// Game indices and names.bin offsets are independent domains even when their
// numeric values happen to agree. Null (UINT32_MAX) cannot be reassigned.
struct Maps {
    std::map<uint32_t, uint32_t> gameEntries;
    std::map<uint32_t, uint32_t> nameOffsets;
};
enum class Domain { GameEntry, NameOffset };
struct Rewrite {
    size_t offset;
    Domain domain;
    uint32_t before, after;
    std::string path;
};
struct Candidate {
    size_t offset;
    uint32_t value;
};
struct Analysis {
    std::vector<Rewrite> rewrites;
    std::vector<Candidate> unresolved;
};

// Inspect one decoded-bank payload. Recognizes OBJECT/BUILDING/CREATURE CDefs listings,
// verified hero-ability, brain-behaviour, shop-stock and appearance animation
// reference layouts, and
// explicitly typed CDefIndex/CDefString scalar/vector fields. Other integer
// fields are not inferred to be references. Every-byte candidate scanning is
// a conservative rejection guard, never a source of rewrite locations.
// This is not an audit of references in other containers or runtime code.
Analysis analyze(const bin::Entry& entry, const defschema::Schema& schema,
                 const Maps& maps);

// Returns a copy with only verified reference words changed. Any unresolved
// candidate refuses the entire operation; entry and maps always stay const.
std::vector<uint8_t> relocate(const bin::Entry& entry, const defschema::Schema& schema,
                              const Maps& maps);

} // namespace forge::defrelocate
