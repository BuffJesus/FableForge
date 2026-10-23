#include "forge/creatureappearance.hpp"
#include "forge/defdecode.hpp"
#include <bit>
#include <stdexcept>
#include <string>
#include <utility>

namespace forge::creatureappearance {
namespace {
[[noreturn]] void fail(const char* reason) {
    throw std::runtime_error(std::string("Creature appearance: ") + reason);
}
class Reader {
    std::span<const uint8_t> bytes_;
    size_t total_ = 0;
public:
    size_t at = 3;
    std::vector<size_t> structuralWords;
    explicit Reader(std::span<const uint8_t> bytes) : bytes_(bytes) {}
    size_t take(size_t length) {
        if (at > bytes_.size() || length > bytes_.size()-at) fail("truncated prefix");
        const auto start = at;
        at += length;
        return start;
    }
    uint32_t word() {
        const auto start = take(4);
        uint32_t value = 0;
        for (size_t n = 0; n < 4; ++n) value |= uint32_t(bytes_[start+n]) << (8*n);
        return value;
    }
    bool flag() {
        const auto value = bytes_[take(1)];
        if (value > 1) fail("invalid serialized boolean");
        return value != 0;
    }
    void tag(uint32_t expected) {
        structuralWords.push_back(at);
        if (word() != expected) fail("unsupported control tag");
    }
    size_t count(size_t minimum) {
        structuralWords.push_back(at);
        const size_t value = word();
        if (value > 4096 || total_ > 65536-value || value > (bytes_.size()-at)/minimum)
            fail("invalid count");
        total_ += value;
        return value;
    }
    TextureMap textures() {
        const auto size = count(12);
        structuralWords.push_back(at);
        if (word() != size*12) fail("texture map byte size disagrees with count");
        TextureMap result;
        for (size_t n = 0; n < size; ++n) {
            TextureMorph row;
            row.offset = at;
            row.original = word(); row.replacement = word(); row.group = word();
            result.rows.push_back(row);
        }
        result.dirty = flag();
        return result;
    }
};
} // namespace

Prefix read(std::span<const uint8_t> bytes) {
    // FableWin 0289b880 reads three body groups, their texture maps, skeletal
    // rows, global texture map and color-group count. 028a55b0 checks 12-byte
    // texture rows. See HANDOFF_CREATURE_PROFILES_2026-10-23.md for byte/PDB evidence.
    Reader reader(bytes);
    Prefix result;
    reader.tag(defdecode::fieldTag("WoundMorphs"));
    result.wounds = reader.count(44); result.woundRows = reader.at;
    reader.take(result.wounds*44);
    reader.tag(0x01ab6cee);
    reader.tag(defdecode::fieldTag("RandomAppearanceMorph"));
    if (reader.count(4) != result.bodies.size()) fail("expected three body groups");
    for (auto& body : result.bodies) {
        const auto size = reader.count(17);
        for (size_t n = 0; n < size; ++n) {
            BodyModel model;
            model.offset = reader.at;
            model.mesh = reader.word(); model.priority = std::bit_cast<int32_t>(reader.word());
            model.textures = reader.textures();
            body.push_back(std::move(model));
        }
    }
    for (auto& map : result.bodyTextures) map = reader.textures();
    const auto size = reader.count(13);
    for (size_t n = 0; n < size; ++n) {
        SkeletalMorph morph;
        morph.offset = reader.at;
        morph.key = std::bit_cast<int32_t>(reader.word());
        morph.group = std::bit_cast<int32_t>(reader.word());
        morph.nameOffset = reader.word(); morph.allowVariation = reader.flag();
        result.skeletal.push_back(morph);
    }
    result.globalTextures = reader.textures();
    result.textureGroupCount = reader.word();
    reader.tag(defdecode::fieldTag("Expressions"));
    result.expressions = reader.count(12); result.expressionRows = reader.at;
    reader.take(result.expressions*12);
    reader.tag(0x01ab6d32);
    reader.tag(defdecode::fieldTag("Stats"));
    result.statsValueOffset = reader.at;
    result.structuralWords = std::move(reader.structuralWords);
    return result;
}
} // namespace forge::creatureappearance
