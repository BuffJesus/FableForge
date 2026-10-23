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
struct LinkGroup { std::string path; std::vector<std::string> targets; };
using RecordLinks = std::map<std::string, std::vector<Link>>;
// Stable internal key for a blank-name record identified by its baseline index.
std::string anonymousKey(size_t index);

// Read inert NRBF data, never instantiate serialized framework objects.
std::vector<Link> readMetadata(std::span<const uint8_t> bytes);
// Inspection path: retain every named target in an NRBF link list. The writer
// still requires one target until that field's on-disk layout is identified.
std::vector<LinkGroup> readMetadataGroups(std::span<const uint8_t> bytes);
RecordLinks readGameLinks(const big::File& package);

enum class Destination { GameEntry, NameOffset, NameCrc, MeshEntry, TextureEntry, EffectEntry, TextEntry,
                         OptionalObjectEntry };
struct Location { size_t offset, part; Destination destination; };
struct CreaturePrefixLayout {
    struct BodyModel { size_t bodyPart, alternative, offset; };
    // RandomAppearanceMorph model words only, not facial-expression meshes.
    // Indices preserve empty body parts and the native alternative order.
    std::vector<BodyModel> bodyModels;
    std::vector<Location> references;
    std::vector<size_t> structuralWords; // validated four-byte tags/counts
};
// Enumerate verified references and structural words in a decoded prefix.
// Reuses the same counted-layout validation as FMP path lookup. Unclassified
// prefix bytes are deliberately omitted; malformed/unsupported layouts throw.
CreaturePrefixLayout creaturePrefixLayout(std::span<const uint8_t> prefix);
Location locate(const bin::Entry& entry, const defschema::Schema& schema,
                const Link& link);

// Only identified four-byte references change; input and target banks are const.
// The composer may defer unavailable targets in unselected source versions.
std::vector<uint8_t> relink(const bin::Entry& entry, const bin::File& targets,
                           const defschema::Schema& schema,
                           std::span<const Link> links, bool deferMissing = false,
                           const std::map<std::string, uint32_t>* meshIds = nullptr,
                           const std::map<std::string, uint32_t>* textureIds = nullptr,
                           const std::map<std::string, uint32_t>* effectIds = nullptr,
                           const std::map<std::string, uint32_t>* textIds = nullptr);
} // namespace forge::fmplinks
