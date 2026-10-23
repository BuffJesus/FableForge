#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace forge::creatureappearance {
struct TextureMorph {
    size_t offset = 0;
    uint32_t original = 0, replacement = 0, group = 0;
};
struct TextureMap {
    std::vector<TextureMorph> rows;
    bool dirty = false;
};
struct BodyModel {
    size_t offset = 0;
    uint32_t mesh = 0;
    int32_t priority = 0;
    TextureMap textures;
};
struct SkeletalMorph {
    size_t offset = 0;
    int32_t key = 0, group = 0;
    uint32_t nameOffset = 0;
    bool allowVariation = false;
};
struct Prefix {
    size_t wounds = 0, woundRows = 0, expressions = 0, expressionRows = 0;
    std::array<std::vector<BodyModel>, 3> bodies;
    std::array<TextureMap, 3> bodyTextures;
    std::vector<SkeletalMorph> skeletal;
    TextureMap globalTextures;
    uint32_t textureGroupCount = 0;
    size_t statsValueOffset = 0;
    std::vector<size_t> structuralWords;
};
// Read a decoded CCreatureDef prefix through the Stats tag. Offsets refer to
// the original prefix; input/unknown Stats bytes are untouched. Counts, table
// byte sizes, booleans and control tags are checked before a result escapes.
// Name offsets still require validation against the owning bank's names pool.
// This is a serialized layout, not a selected/assembled or animated creature.
Prefix read(std::span<const uint8_t> bytes);
} // namespace forge::creatureappearance
