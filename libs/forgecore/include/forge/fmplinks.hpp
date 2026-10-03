#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>
#include "forge/big.hpp"
#include "forge/bin.hpp"
#include "forge/defschema.hpp"

namespace forge::fmplinks {
struct Link { std::string path, target; };
using RecordLinks = std::map<std::string, std::vector<Link>>;

// Read inert NRBF data, never instantiate serialized framework objects.
std::vector<Link> readMetadata(std::span<const uint8_t> bytes);
RecordLinks readGameLinks(const big::File& package);

enum class Destination { GameEntry, NameOffset, NameCrc };
struct Location { size_t offset, part; Destination destination; };
Location locate(const bin::Entry& entry, const defschema::Schema& schema,
                const Link& link);

// Only identified four-byte references change; input and target banks are const.
// The composer may defer unavailable targets in unselected source versions.
std::vector<uint8_t> relink(const bin::Entry& entry, const bin::File& targets,
                           const defschema::Schema& schema,
                           std::span<const Link> links, bool deferMissing = false);
} // namespace forge::fmplinks
