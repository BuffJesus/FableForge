#pragma once
#include "effects.hpp"
#include <array>
#include <random>

namespace albion::particlepreview {
// Full billboard dimensions in world units; the renderer applies frame aspect.
// Coordinates are Fable Z-up, angles radians, colour and animation phase 0..1.
struct DrawSprite {
    float position[3] = {};
    float size[2] = {1, 1};
    float angle = 0;
    float colour[4] = {1, 1, 1, 1};
    int32_t texture = -1;
    float framePhase = 0;
    int blendMode = 0, blendOperation = 0;
};
// Size includes system scale, but not mesh-radius normalization. The renderer
// applies the Fable basis (-Right,-Forward,Up) and optional bounds centring.
struct DrawMesh {
    int32_t mesh=-1;
    float position[3]={},size[3]={1,1,1},orientation[4]={0,0,0,1};
    float colour[4]={1,1,1,1};
    bool centredOnPosition=false;
    int blendMode=0,blendOperation=0;
};

// Component-level light, independent of emitted particles. These are native
// primitive radius parameters, not a reconstructed scene illumination curve.
struct DrawLight {
    float position[3]={},colour[4]={1,1,1,1};
    float radius=0,attenuationDistance=0;
    uint32_t systemIndex=UINT32_MAX;
};

// Deterministic editor preview, not an engine replacement. Pause is owned by the
// caller: stop calling advance(); step() always advances one fixed tick.
class Simulation {
public:
    static constexpr double TickSeconds = 1.0 / 30.0;
    static constexpr double MaxSeekSeconds = 300.0;
    static constexpr size_t MaxParticles = 4096, MaxSystemParticles = 2048, MaxSystems = 64;
    static constexpr size_t MaxForceComponents = 16;
    static constexpr size_t MaxLightComponents = 64;
    static constexpr size_t MaxMeshDraws = 4096, MaxMeshRenderers = 16;
    void reset(const effects::Effect& effect);
    void advance(double seconds);
    void seek(const effects::Effect& effect,double seconds);
    void step();
    double time() const { return double(ticks_) * TickSeconds; }
    size_t particleCount() const { return liveCount(); }
    size_t supportedSystems() const { return states_.size(); }
    const std::vector<std::string>& warnings() const { return warnings_; }
    const std::vector<DrawSprite>& sprites() const { return sprites_; }
    const std::vector<DrawMesh>& meshes() const { return meshes_; }
    const std::vector<DrawLight>& lights() const { return lights_; }
    size_t lightComponentCount() const { return lightStates_.size(); }
private:
    struct Particle {
        std::array<float, 3> position{}, velocity{};
        uint64_t age = 0, life = 30;
        float angle = 0, angularVelocity = 0;
        std::array<float,4> orientation{0,0,0,1},orientationChange{0,0,0,1};
    };
    struct State {
        effects::SpriteSystem config;
        std::vector<Particle> particles;
        double fractionalSpawn = 0;
        bool burstDone = false;
        std::vector<effects::MeshSystem> meshes;
    };
    struct LightState {
        effects::LightSystem config;
        int64_t timer=0,delayTimer=0;
        bool initialized=false,alive=false;
    };
    std::vector<LightState> lightStates_;
    std::vector<DrawLight> lights_;
    void updateLights();
    void rebuildLights();
    std::vector<State> states_;
    std::vector<DrawSprite> sprites_;
    std::vector<DrawMesh> meshes_;
    std::vector<std::string> warnings_;
    std::mt19937 random_{1337};
    std::mt19937 meshRandom_{0x4d455348u};
    uint64_t ticks_ = 0;
    double accumulator_ = 0;
    void tick();
    void rebuild();
    void spawn(State& state, size_t count);
    size_t liveCount() const;
    float randomUnit();
    float meshRandomUnit();
    void warn(const std::string& message);
};
} // namespace albion::particlepreview
