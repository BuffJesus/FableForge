#pragma once
// Payload and Info codec for dialogue.big type-1 LIPSYNC entries. The BIG
// container and sub-bank routing are handled by forge::big.

#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "forge/big.hpp"

namespace forge::lipsync {

struct Viseme {
    uint8_t id = 0;
    std::string symbol;
};

struct Key {
    uint8_t id = 0;
    uint8_t weight = 0;
};

using Frame = std::vector<Key>;

struct Entry {
    std::vector<Viseme> dictionary;
    uint32_t fps = 43;
    std::vector<Frame> frames;
    // Store the Info float's bits so an untouched entry round-trips exactly.
    uint32_t durationBits = 0;
    float duration() const { return std::bit_cast<float>(durationBits); }
    void setDuration(float seconds) { durationBits = std::bit_cast<uint32_t>(seconds); }
};

struct PoseWeight {
    uint8_t id = 0;
    std::string symbol;
    float weight = 0;
};

struct Pose {
    std::vector<PoseWeight> visemes;
    float restWeight = 1.0f;
    size_t frame = 0;
};

// Sample adjacent frame keys on the lip sync clock. Unknown key IDs are
// ignored; unused weight belongs to the closed-mouth resting pose. If keys
// exceed full weight, normalize them to one full pose.
Pose sample(const Entry& entry, double seconds);

// Frame edits use the six phonemes supported by the retail head presets.
// Existing dictionary IDs and untouched frame bytes are preserved.
uint8_t ensureViseme(Entry& entry, const std::string& symbol);
void setWeight(Entry& entry, size_t frame, const std::string& symbol, uint8_t weight);
void removeWeight(Entry& entry, size_t frame, const std::string& symbol);
void insertFrameAfter(Entry& entry, size_t frame);
void eraseFrame(Entry& entry, size_t frame);

// Structural decoder. Throws std::runtime_error for a truncated/malformed
// payload or an Info block other than one little-endian f32.
Entry decode(std::span<const uint8_t> raw, std::span<const uint8_t> info);
std::vector<uint8_t> encode(const Entry& entry);
std::vector<uint8_t> encodeInfo(const Entry& entry);

struct UpsertResult {
    uint32_t id = 0;
    std::string name;
    std::string bank;
    bool added = false;
};

// Edit/add a type-1 record in one explicitly named LIPSYNC sub-bank. The ID
// is the paired dialogue .lut sound index and may occur in another sub-bank.
// New records clone a same-bank donor's metadata and derive its name prefix.
// This only changes the in-memory archive; callers choose a scratch/mod output.
UpsertResult upsert(big::File& file, const std::string& bankName,
                    uint32_t soundId, const Entry& value);

struct ArchiveEdit {
    std::string bankName;
    uint32_t soundId=0;
    Entry value;
};

// Write edited entries to a new BIG. The temporary archive is reopened and
// checked before rename. Existing destination files are never overwritten.
void writeScratchArchive(const std::filesystem::path& source,
                         const std::filesystem::path& destination,
                         std::span<const ArchiveEdit> edits);

} // namespace forge::lipsync
