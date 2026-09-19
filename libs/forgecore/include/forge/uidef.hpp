#pragma once
// Edit CUIDef ("UI") entries — specifically the nested States[].GraphicIndex that
// selects a UI sprite's texture. Quest-card orb art lives here: the game.bin
// CUIDefs UI_QUEST_SPRITE_CORE/OPTIONAL/VIGNETTE differ only in
// States[0].GraphicIndex (= textures.big ids 5892/5894/5896). See
// FableTLC docs/QUEST_CARD_TEXTURE_BINDING.md.
//
// GraphicIndex is a uint32 NESTED inside the `States` field (a Vector<CUIStateDef>
// whose elements are themselves crc0-tag-anchored), so `defs set-field` (top-level
// tags only) cannot reach it — hence this helper.

#include <cstdint>
#include <string>
#include <vector>

#include "forge/bin.hpp"
#include "forge/defschema.hpp"

namespace forge::uidef {

struct AddSpriteResult {
    size_t index = 0;          // global entry index of the new UI/CUIDef
    std::string name;          // the new entry name
    std::string source;        // the cloned source entry name
    uint32_t graphicIndex = 0; // the texture id set on State[stateIndex]
};

struct CloneObjectModelResult {
    size_t donorIndex = 0;
    size_t index = 0;
    std::string donor;
    std::string name;
    uint32_t oldModelId = 0;
    uint32_t modelId = 0;
    std::vector<size_t> rewrittenSelfReferenceOffsets;
};

CloneObjectModelResult cloneObjectModel(bin::File& file,
                                        const defschema::Schema& schema,
                                        const std::string& donorName,
                                        const std::string& newName,
                                        uint32_t modelId);

// Clone UI/CUIDef `srcEntryName` (e.g. UI_QUEST_SPRITE_CORE), set
// States[stateIndex].GraphicIndex to `graphicIndex`, and append it as `newName`.
// The quest-card orb CUIDefs are leaf sprites (Children=0, no self back-ref), so a
// clone appends cleanly under DEF_LOAD_CONTRACT (crc0 name + dense index, nothing
// to retarget). Used to make a custom per-card sprite for the render detour.
// Mutates `file` in place; caller saves. Throws on a missing / non-UI source.
AddSpriteResult addSpriteClone(bin::File& file, const defschema::Schema& schema,
                               const std::string& srcEntryName,
                               const std::string& newName, uint32_t graphicIndex,
                               uint32_t stateIndex = 0);

// Return a copy of `payload` (a CUIDef entry) with States[stateIndex].GraphicIndex
// set to `graphicIndex`. `schema` must contain CUIDef. Throws std::runtime_error
// if the payload does not decode clean, has no States field, or has fewer than
// stateIndex+1 states. The payload keeps its exact length (a uint32 overwrite).
std::vector<uint8_t> setStateGraphicIndex(const std::vector<uint8_t>& payload,
                                          const defschema::Schema& schema,
                                          uint32_t stateIndex,
                                          uint32_t graphicIndex);

// Read States[stateIndex].GraphicIndex (for verification / display).
uint32_t getStateGraphicIndex(const std::vector<uint8_t>& payload,
                              const defschema::Schema& schema,
                              uint32_t stateIndex);

// Set the `Graphic` (CEngineGraphic) modelId of an OBJECT def (e.g. an
// OBJECT_QUEST_CARD): the mesh the object renders (graphics.big MBANK id). The
// CEngineGraphic value is [u32 type][u32 modelId][...]; this rewrites the modelId
// (2nd u32) in place. `schema` must contain the def type; caller passes the def
// type name (e.g. "CThingObjectDef" / "OBJECT"). Throws on non-clean decode or a
// missing Graphic field. Returns the modified payload (same length).
std::vector<uint8_t> setGraphicModelId(const std::vector<uint8_t>& payload,
                                       const defschema::DefType& def,
                                       uint32_t modelId);
uint32_t getGraphicModelId(const std::vector<uint8_t>& payload,
                           const defschema::DefType& def);

} // namespace forge::uidef
