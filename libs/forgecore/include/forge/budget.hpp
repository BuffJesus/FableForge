#pragma once
// The budget survey: what an area of a map costs the renderer -- things,
// triangles, vertices and texture memory -- after the vanilla editor's Surveys >
// Engine tab (FableWin CEditControlCentre::GetEngineSurveyPrimitiveStats
// 0x02050cb0 / GetEngineSurveyPrimitiveThingStats 0x0204fde0, dialog
// CSurveyDialog::DisplayEngineSurveyStatistics 0x028d4c10).
//
// Vanilla's rules, kept here:
//  * include flags per thing kind: local detail 0x10, buildings 1, creatures 2,
//    objects 4, others 8 (IsEngineSurveyIncluding*);
//  * every surveyed thing with a graphic counts as a thing, tallied per definition
//    (the "included things" map);
//  * primitive stats are collected into maps keyed by primitive, so a mesh used by
//    many things counts once -- unless "Count all duplications"
//    (IsEngineSurveyCountingAllObjectInstances), which sums them per instance;
//  * texture memory is per distinct texture.
// Vanilla reads the live renderer; here the numbers come from the banks: a mesh's
// LOD0 triangles / vertices (meshpreview) and a texture's full mip chain as the
// GPU holds it (textureMemory below).
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "forge/terraintex.hpp"

namespace forge::budget {

enum Kind : unsigned {
    kBuildings = 1u,
    kCreatures = 2u,
    kObjects = 4u,
    kOthers = 8u,
    kLocalDetail = 0x10u,   // plants and grass baked into the map (STB local detail)
    kAll = 0x1Fu,
};

// The kind of a .tng thing type (NewThing <type>).
Kind kindOfThingType(const std::string& type);

struct Item {
    std::string name;       // definition (things) or mesh name (local detail)
    Kind kind = kObjects;
    uint32_t mesh = 0;      // MBANK_ALLMESHES id; 0 = no graphic (not counted, like vanilla)
};

struct MeshCost {
    bool ok = false;        // false: the mesh could not be read (listed under problems)
    std::string name;
    uint32_t triangles = 0, vertices = 0;
    std::vector<uint32_t> textures;   // every texture id its materials use
};

struct TextureCost {
    bool ok = false;
    std::string name;
    uint64_t bytes = 0;
    uint32_t width = 0, height = 0;
    std::string format;
};

struct Options {
    unsigned include = kAll;
    bool countAllDuplications = false;
};

struct Line {               // one row of a breakdown
    std::string name;
    uint64_t count = 0;     // things (per definition) / instances (per mesh)
    uint64_t triangles = 0, vertices = 0, bytes = 0;
};

struct Report {
    uint64_t things = 0, triangles = 0, vertices = 0, textureBytes = 0;
    uint64_t skippedNoGraphic = 0;    // surveyed things without a graphic
    std::vector<Line> definitions;    // things per definition, most first
    std::vector<Line> meshes;         // triangles per mesh, heaviest first
    std::vector<Line> textures;       // bytes per texture, heaviest first
    std::vector<std::string> problems;
};

// GPU bytes of a texture: every mip level of every frame (and depth slice), at the
// format's raw size (DXT blocks stay compressed, as on the card).
uint64_t textureMemory(const terraintex::TextureInfo& info);

using MeshLookup = std::function<MeshCost(uint32_t meshId)>;
using TextureLookup = std::function<TextureCost(uint32_t textureId)>;

Report survey(const std::vector<Item>& items, const MeshLookup& mesh, const TextureLookup& texture,
              const Options& options);

// "12.4 MB", "640 KB", "512 B".
std::string formatBytes(uint64_t bytes);
// The report as plain text (vanilla's "Save stats to file").
std::string toText(const Report& report, const std::string& title, const Options& options);

} // namespace forge::budget
