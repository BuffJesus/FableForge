#include "forge/uidef.hpp"

#include <stdexcept>
#include <string>

#include "forge/defdecode.hpp"

namespace forge::uidef {
namespace {

uint32_t rdU32(const std::vector<uint8_t>& b, size_t p) {
    return uint32_t(b[p]) | uint32_t(b[p + 1]) << 8 | uint32_t(b[p + 2]) << 16 |
           uint32_t(b[p + 3]) << 24;
}
void wrU32(std::vector<uint8_t>& b, size_t p, uint32_t v) {
    b[p] = uint8_t(v);
    b[p + 1] = uint8_t(v >> 8);
    b[p + 2] = uint8_t(v >> 16);
    b[p + 3] = uint8_t(v >> 24);
}

defdecode::DecodedField* findStates(defdecode::Decoded& d) {
    for (auto& f : d.fields)
        if (f.name == "States") return &f;
    return nullptr;
}

// Locate the byte offset (within a States field value) of the GraphicIndex u32 for
// state `stateIndex`. The States value = [u32 count][CUIStateDef]*; each element is
// crc0-tag-anchored and begins with the GraphicIndex tag, so the (stateIndex)-th
// occurrence of that tag marks the target state. Returns the offset of the VALUE
// (4 bytes after the tag). Throws if out of range.
size_t graphicIndexValueOffset(const std::vector<uint8_t>& states, uint32_t stateIndex) {
    if (states.size() < 4) throw std::runtime_error("uidef: States value too small");
    const uint32_t count = rdU32(states, 0);
    if (stateIndex >= count)
        throw std::runtime_error("uidef: state index " + std::to_string(stateIndex) +
                                 " out of range (states=" + std::to_string(count) + ")");
    const uint32_t tag = defdecode::fieldTag("GraphicIndex");
    uint32_t seen = 0;
    for (size_t p = 4; p + 8 <= states.size(); ++p) {
        if (rdU32(states, p) == tag) {
            if (seen == stateIndex) return p + 4;  // value follows the 4-byte tag
            ++seen;
        }
    }
    throw std::runtime_error("uidef: GraphicIndex tag for state " +
                             std::to_string(stateIndex) + " not found");
}

const defschema::DefType& cuiDef(const defschema::Schema& schema) {
    const auto* def = schema.find("CUIDef");
    if (def == nullptr) throw std::runtime_error("uidef: schema has no CUIDef");
    return *def;
}

} // namespace

std::vector<uint8_t> setStateGraphicIndex(const std::vector<uint8_t>& payload,
                                          const defschema::Schema& schema,
                                          uint32_t stateIndex, uint32_t graphicIndex) {
    auto decoded = defdecode::decode(payload, cuiDef(schema));
    if (!decoded.clean())
        throw std::runtime_error("uidef: CUIDef did not decode clean — refusing to edit");
    auto* states = findStates(decoded);
    if (states == nullptr) throw std::runtime_error("uidef: entry has no States field");

    const size_t off = graphicIndexValueOffset(states->value, stateIndex);
    wrU32(states->value, off, graphicIndex);

    std::vector<uint8_t> out = defdecode::encode(decoded);
    if (out.size() != payload.size())
        throw std::runtime_error("uidef: edit changed payload size (expected in-place uint32)");
    return out;
}

AddSpriteResult addSpriteClone(bin::File& file, const defschema::Schema& schema,
                               const std::string& srcEntryName,
                               const std::string& newName, uint32_t graphicIndex,
                               uint32_t stateIndex) {
    const auto& entries = file.entries();
    size_t si = entries.size();
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].name == srcEntryName) { si = i; break; }
    if (si == entries.size())
        throw std::runtime_error("uidef: no source entry " + srcEntryName);
    if (entries[si].definition != "UI")
        throw std::runtime_error("uidef: source " + srcEntryName +
                                 " is not a UI/CUIDef entry");

    // Clone + retarget the sprite's texture before the append reallocates entries.
    std::vector<uint8_t> data =
        setStateGraphicIndex(entries[si].data, schema, stateIndex, graphicIndex);

    AddSpriteResult r;
    r.source = srcEntryName;
    r.name = newName;
    r.graphicIndex = graphicIndex;
    r.index = file.addEntry("UI", newName, std::move(data));
    return r;
}

CloneObjectModelResult cloneObjectModel(bin::File& file,
                                        const defschema::Schema& schema,
                                        const std::string& donorName,
                                        const std::string& newName,
                                        uint32_t modelId) {
    const auto& entries = file.entries();
    size_t donor = entries.size();
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].name == newName)
            throw std::runtime_error("uidef: destination name already exists: " + newName);
        if (entries[i].name == donorName) donor = i;
    }
    if (donor == entries.size())
        throw std::runtime_error("uidef: no donor entry " + donorName);
    if (entries[donor].definition != "OBJECT")
        throw std::runtime_error("uidef: donor is not an OBJECT: " + donorName);
    const auto* def = defdecode::resolveType(schema, entries[donor].definition,
                                             entries[donor].data);
    if (def == nullptr)
        throw std::runtime_error("uidef: could not resolve donor OBJECT type");

    CloneObjectModelResult result;
    result.donorIndex = donor;
    result.index = entries.size();
    result.donor = donorName;
    result.name = newName;
    result.modelId = modelId;
    result.oldModelId = getGraphicModelId(entries[donor].data, *def);
    std::vector<uint8_t> data = entries[donor].data;
    for (size_t p = 0; p + 4 <= data.size(); ++p) {
        if (rdU32(data, p) == donor) {
            wrU32(data, p, static_cast<uint32_t>(result.index));
            result.rewrittenSelfReferenceOffsets.push_back(p);
            p += 3;
        }
    }
    data = setGraphicModelId(data, *def, modelId);
    const size_t actual = file.addEntry("OBJECT", newName, std::move(data));
    if (actual != result.index)
        throw std::runtime_error("uidef: tail append index changed unexpectedly");
    return result;
}

std::vector<uint8_t> setGraphicModelId(const std::vector<uint8_t>& payload,
                                       const defschema::DefType& def, uint32_t modelId) {
    auto decoded = defdecode::decode(payload, def);
    if (!decoded.clean())
        throw std::runtime_error("uidef: OBJECT did not decode clean — refusing to edit");
    for (auto& f : decoded.fields) {
        if (f.name != "Graphic") continue;
        if (f.value.size() < 8)
            throw std::runtime_error("uidef: Graphic (CEngineGraphic) value too small");
        wrU32(f.value, 4, modelId);  // CEngineGraphic = [u32 type][u32 modelId][...]
        std::vector<uint8_t> out = defdecode::encode(decoded);
        if (out.size() != payload.size())
            throw std::runtime_error("uidef: edit changed payload size");
        return out;
    }
    throw std::runtime_error("uidef: def has no Graphic field");
}

uint32_t getGraphicModelId(const std::vector<uint8_t>& payload,
                           const defschema::DefType& def) {
    auto decoded = defdecode::decode(payload, def);
    if (!decoded.clean())
        throw std::runtime_error("uidef: OBJECT did not decode clean");
    for (auto& f : decoded.fields)
        if (f.name == "Graphic") {
            if (f.value.size() < 8)
                throw std::runtime_error("uidef: Graphic value too small");
            return rdU32(f.value, 4);
        }
    throw std::runtime_error("uidef: def has no Graphic field");
}

uint32_t getStateGraphicIndex(const std::vector<uint8_t>& payload,
                              const defschema::Schema& schema, uint32_t stateIndex) {
    auto decoded = defdecode::decode(payload, cuiDef(schema));
    if (!decoded.clean())
        throw std::runtime_error("uidef: CUIDef did not decode clean");
    auto* states = findStates(decoded);
    if (states == nullptr) throw std::runtime_error("uidef: entry has no States field");
    const size_t off = graphicIndexValueOffset(states->value, stateIndex);
    return rdU32(states->value, off);
}

} // namespace forge::uidef
