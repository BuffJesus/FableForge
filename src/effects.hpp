#pragma once
// albion::effects -- the particle effects bank (data/Misc/pc/effects.big,
// bank PARTICLE_MAIN_PC) reduced to what a static export can show.
//
// One entry = one serialized CParticleEmitter: a list of systems, each a list
// of components (emitter / update / render / light ...). The full grammar is
// the one FableTLC's tools/parse_effects.py and EgoCore's ParticleParser.h read
// byte-exact (1165/1165 retail entries); it is ported here because every
// component must be walked to reach the next one (no lengths, only 0x7B / 0x26
// terminators). What we keep per effect:
//   * sprite systems (CPSCRenderSprite / CPSCSingleSprite): sprite texture id,
//     start colour, start/end render size, blend mode, rate, particle life --
//     enough for a tinted crossed-quad proxy the size the flame would be;
//   * mesh systems (CPSCRenderMesh): mesh id + size (counted, not proxied);
//   * lights (CPSCLight): colour + world radius -> glTF point light.
// Payloads are uncompressed. Effects are referenced BY NAME from mesh
// CREATEPARTICLE dummies and TNG PARTICLE_EMITTER_PLACEABLE things.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace albion::effects {

struct SpriteSystem {
    std::string system;
    int32_t sprite = -1;        // textures.big GBANK_MAIN_PC id (-1 = none)
    uint8_t colour[4] = {255, 255, 255, 255};   // start colour RGBA
    float startSize = 0, endSize = 0;           // world units
    int blendMode = 0;          // 3 = additive in retail data
    float perSecond = 0, lifeSecs = 0;
    float offset[3] = {0, 0, 0};                // CPSCUpdateNormal ParticleSystemOffset
    bool single = false;        // CPSCSingleSprite
};
struct MeshSystem { std::string system; int32_t mesh = -1; float size[3] = {1, 1, 1}; uint8_t colour[4] = {255, 255, 255, 255}; };
struct LightSystem { std::string system; uint8_t colour[4] = {255, 255, 255, 255}; float radius = 0; };

struct Effect {
    uint32_t id = 0;
    std::string name;           // bank entry name (upper case, e.g. BRAZIERFIREFINAL)
    std::string displayName;    // inside the payload, e.g. BrazierFireFinal
    int systems = 0;
    std::vector<SpriteSystem> sprites;
    std::vector<MeshSystem> meshes;
    std::vector<LightSystem> lights;
    bool parsedFully = false;   // false when an unknown component class stopped the walk
};

bool openBank(const std::filesystem::path& gameRoot, std::string& err);
bool bankOpen();
// Case-insensitive lookup by entry name; nullptr when unknown. Parsed lazily and cached.
const Effect* byName(const std::string& name);
size_t entryCount();
// Every effect name in the bank (upper case, sorted); empty until openBank.
std::vector<std::string> entryNames();

} // namespace albion::effects
