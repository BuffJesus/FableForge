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

Selection select(const Prefix& prefix, uint32_t seed, bool decapitated) {
    const auto validate = [](const TextureMap& map) {
        if (map.dirty) fail("dirty texture map sorting is not yet supported");
        for (size_t n=1;n<map.rows.size();++n)
            if (map.rows[n-1].original>map.rows[n].original) fail("unordered clean texture map");
    };
    for (const auto& body:prefix.bodies)
        for (const auto& model:body) validate(model.textures);
    for (const auto& map:prefix.bodyTextures) validate(map);
    validate(prefix.globalTextures);
    for (size_t n=1;n<prefix.skeletal.size();++n)
        if (prefix.skeletal[n-1].key>prefix.skeletal[n].key) fail("unordered skeletal map");
    const auto random = [&](uint32_t bound) {
        seed=std::rotr(uint32_t(seed*0x24a1u+0x24dfu),13);
        return bound ? seed%bound : 0u;
    };
    const auto textures = [&](const TextureMap& map, uint32_t color,
                              std::vector<TextureReplacement>& output) {
        for (size_t first=0;first<map.rows.size();) {
            size_t end=first+1;
            while(end<map.rows.size() && map.rows[end].original==map.rows[first].original)++end;
            uint32_t count=0;
            for(size_t n=first;n<end;++n)
                if(map.rows[n].group==0 || map.rows[n].group==color)++count;
            if(!count)fail("texture key has no replacement for selected color group");
            auto choice=random(count);
            for(size_t n=first;n<end;++n) {
                if(map.rows[n].group!=0 && map.rows[n].group!=color)continue;
                if(choice--==0) {
                    output.push_back({map.rows[n].original,map.rows[n].replacement});
                    break;
                }
            }
            first=end;
        }
    };
    Selection result;
    const auto skeletal = [&](const SkeletalMorph& morph) {
        float amount=1.0f;
        if(morph.allowVariation) {
            random(0);
            // Original GFFloatRandom(1): fmod(uint32_seed,65536)/65536.
            // Every possible result is exactly representable as a float.
            amount=float(seed&0xffffu)/65536.0f;
        }
        result.skeletal.push_back({morph.nameOffset,amount});
    };
    // FableWin 0289ac90/0289ad40/0289b110/0289b300: one shared evolving
    // seed, skeletal before body/color selection, global textures last.
    for(size_t first=0;first<prefix.skeletal.size();) {
        size_t end=first+1;
        const auto key=prefix.skeletal[first].key;
        while(end<prefix.skeletal.size() && prefix.skeletal[end].key==key)++end;
        if(key==0) {
            for(size_t n=first;n<end;++n)
                if(prefix.skeletal[n].allowVariation || random(100)>=50)skeletal(prefix.skeletal[n]);
        } else if(random(100)>50) {
            const auto choice=random(uint32_t(end-first));
            skeletal(prefix.skeletal[first+choice]);
        }
        first=end;
    }
    const auto color=prefix.textureGroupCount ? random(prefix.textureGroupCount)+1 : 0;
    for(size_t body=0;body<prefix.bodies.size();++body) {
        if(prefix.bodies[body].empty())continue;
        const auto alternative=random(uint32_t(prefix.bodies[body].size()));
        const auto& model=prefix.bodies[body][alternative];
        BodyPart part{body,alternative,model.mesh,{}};
        textures(model.textures,color,part.textures);
        textures(prefix.bodyTextures[body],color,part.textures);
        // The head consumes all normal draws even when it is not emitted.
        if(!decapitated || body!=0)result.parts.push_back(std::move(part));
    }
    textures(prefix.globalTextures,0,result.globalTextures);
    result.seedAfter=seed;
    return result;
}
} // namespace forge::creatureappearance
