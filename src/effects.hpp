#pragma once
// albion::effects -- the particle effects bank (data/Misc/pc/effects.big,
// bank PARTICLE_MAIN_PC), decoded for static exports and bounded particle previews.
//
// One entry = one serialized CParticleEmitter: a list of systems, each a list
// of components (emitter / update / render / light ...). The full grammar is
// the one FableTLC's tools/parse_effects.py and EgoCore's ParticleParser.h read
// byte-exact (1165/1165 retail entries); it is ported here because every
// component must be walked to reach the next one (no lengths, only 0x7B / 0x26
// terminators). What we keep per effect:
//   * sprite systems (CPSCRenderSprite / CPSCSingleSprite): sprite texture id,
//     start colour, start/end render size, blend mode, rate, particle life --
//     plus emitter, normal update and renderer parameters for animated previews;
//   * mesh systems (CPSCRenderMesh): static fields plus shared emitter/update
//     configuration, renderer fades and serialized system identity for previews;
//   * lights (CPSCLight): colour + world radius -> glTF point light.
// Static export fields retain their existing units and scaling. Unsupported
// active preview components are reported separately; decoding does not imply
// that every component can be simulated.
// Payloads are uncompressed. Effects are referenced BY NAME from mesh
// CREATEPARTICLE dummies and TNG PARTICLE_EMITTER_PLACEABLE things.

#include <cstdint>
#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace albion::effects {

// Preview parameters retained from the checked component stream. Names and
// quantization were cross-checked against EgoCore 55bdc10 ParticleParser.h.
// Positions, directions and acceleration remain in Fable's Z-up coordinates.
struct EmitterParams {
    bool present=false;
    uint32_t positionParam=0,directionParam=0,startCount=0,startCountRandom=0,type=0;
    bool solid=false,useLife=false,useTimeline=false,orientationXY=false,orientationXZ=false,orientationYZ=false;
    bool useCustomDirection=false,useOutwardDirection=false,useParamDirection=false,oppositeDirection=false;
    bool useForwardDirection=false,useRandom2DDirection=false,useRandom3DDirection=false;
    uint32_t angularPerturbation=0;
    float customDirection[3]={0,0,1};
    float size=0,radialBias=0,minSpeed=0,maxSpeed=0,timelineSecs=0,lifeSecs=0,startTime=0;
    float nonUniformScale[3]={0,0,0}; // encoded adjustment; neutral is zero
    bool hasSpline=false;
};
struct UpdateParams {
    bool present=false;
    uint32_t fadeInEnd=0,fadeOutBegin=0;
    bool useParticleLife=false,randomRotationAxis=false,randomInitialRotation=false,useAccelerationParam=false;
    bool stayWithEmitter=false,useSystemLife=false,systemAlphaFade=false,emissionFade=false;
    bool collideGround=false,collideAnything=false,dieOnCollision=false,useAttractors=false;
    bool randomisePosition=false,randomiseDistance=false,setOrientationDirection=false,setOrientationGame=false;
    bool createDecal=false,createDecalEmitter=false;
    std::string decalEmitter;
    float systemLife=0,wind=0,gravity=0,drag=0,accelerationScale=0;
    float initialRotation[3]={},rotationMinSpeed=0,rotationMaxSpeed=0,bounce=0;
    float systemAlphaMinimum=0,randomiseDistanceScale=0,emissionMinimum=0;
    float acceleration[3]={},rotationAxis[3]={},randomiseSpeed[3]={},randomiseScale[3]={};
    uint32_t accelerationParam=0,orientationParam=0;
};
struct RenderParams {
    uint8_t midColour[4]={255,255,255,255},endColour[4]={255,255,255,255};
    uint32_t blendOp=0,flags=0,fadeInEnd=0,fadeOutBegin=0,crossedSprites=0;
    uint32_t trailBlendMode=0,trailBlendOp=0,trailLength=0,flickerMinAlpha=0,flickerMinSize=0;
    int32_t trailTexture=-1,alignment=0,stayWithEmitterFactor=0;
    float alphaMinimum=0,sizeMinimum=0,animationSecs=0,trailWidth=0,flickerBias=0,flickerSpeed=0,initialAngle=0;
    bool useStartColour=false,useMidColour=false,useEndColour=false,alphaFade=false,sizeFade=false,flicker=false,forceAnimation=false;
    bool selfIlluminating=false,rotateAroundCentre=false,faceMe2D=false,faceMe3D=false,crossed=false,stayWithEmitter=false;
    bool usePosition=false,useSplinePoints=false;
    uint32_t positionParam=0;
};

struct OrbitData {
    int32_t type=0;
    bool enabled=false,cycle=false;
    float radius=0,expand=0,cycleTime=0,squeezeScale=0,squeezeAngle=0;
    float rotateSpeed=0,rotateStart=0,rotateSpeedRandom=0,rotateStartRandom=0;
};
struct OrbitParams { uint32_t centreParam=0; OrbitData axes[3]; };
struct AttractorParams {
    bool enabled=false,useParamPosition=false;
    int32_t falloff=0;
    uint32_t positionParam=0,positionParamName=0;
    float radius=0,force=0;
    std::vector<std::array<float,3>> points;
};

struct SpriteSystem {
    std::string system;
    uint32_t systemIndex=UINT32_MAX; // serialized identity; names need not be unique
    int32_t sprite = -1;        // textures.big GBANK_MAIN_PC id (-1 = none)
    uint8_t colour[4] = {255, 255, 255, 255};   // start colour RGBA
    float startSize = 0, endSize = 0;           // world units
    float authoredStartSize = 0, authoredEndSize = 0; // unscaled renderer sizes for preview
    bool hasAuthoredSizes = false;
    int blendMode = 0;          // 3 = additive in retail data
    float perSecond = 0, lifeSecs = 0;
    float offset[3] = {0, 0, 0};                // CPSCUpdateNormal ParticleSystemOffset
    bool single = false;        // CPSCSingleSprite
    EmitterParams emitter;
    UpdateParams update;
    RenderParams render;
    std::vector<OrbitParams> orbits;
    std::vector<AttractorParams> attractors;
    std::vector<std::string> previewUnsupported;
    bool scaleParticles=false;
    float systemScale[3]={1,1,1}; // legacy start/end sizes already include X scale
};
struct MeshSystem {
    // Legacy static export fields retain their original values.
    std::string system; int32_t mesh = -1; float size[3] = {1, 1, 1}; uint8_t colour[4] = {255, 255, 255, 255};
    uint32_t systemIndex=UINT32_MAX;
    SpriteSystem config; // final emitter/update/force state, independent of component order
    RenderParams render;
    float endSize[3]={};
    bool centredOnPosition=false,useRenderSizeParam=false;
    uint32_t renderSizeParam=0;
    uint8_t trailStartColour[4]={},trailMidColour[4]={},trailEndColour[4]={};
    bool trailUseStartColour=false,trailUseMidColour=false,trailUseEndColour=false;
    int blendMode=0;
};
struct LightSystem {
    // Keep legacy static-export colour/radius unchanged; runtime fields follow.
    std::string system; uint8_t colour[4]={255,255,255,255}; float radius=0;
    uint32_t systemIndex=UINT32_MAX,positionParam=0;
    float lifeSecs=5,respawnDelaySecs=0,startTime=0,timelineSecs=0,endRadius=0;
    int32_t attenuationFactor=0,fadeInEnd=0,fadeOutBegin=1000;
    float radiusMinimum=0;
    uint8_t colourMinimum[4]={0,0,0,255},midColour[4]={255,255,255,255},endColour[4]={255,255,255,255};
    bool radiusFade=false,useLife=false,enabled=true,respawns=false,colourFade=false;
    bool useStartColour=true,useMidColour=false,useEndColour=false,useFadeColour=false,useTimeline=false,hasInitializedTime=false;
    bool scaleParticles=false;
    float systemScale[3]={1,1,1};
};

struct Effect {
    uint32_t id = 0;
    std::string name;           // bank entry name (upper case, e.g. BRAZIERFIREFINAL)
    std::string displayName;    // inside the payload, e.g. BrazierFireFinal
    int systems = 0;
    std::vector<SpriteSystem> sprites;
    std::vector<MeshSystem> meshes;
    std::vector<LightSystem> lights;
    bool parsedFully = false;   // false when an unknown component class stopped the walk
    std::vector<std::string> previewUnsupported; // active components outside supported isolated preview
    bool emitter2D=false,preWater=false,water=false,zWrite=false,continuous=false,screenDisplacement=false,readZ=false;
    float maxSpawnDistance=0,maxDrawDistance=0,fadeOutStart=0,fadeInEnd=0,fadeInStart=0;
    int32_t priority=0;
    bool dieOffscreen=false,offscreenUpdate=false,weatherMask=false,dithering=false,boundsOnce=false;
};

// Roots retain independent file snapshots. Owning Effect handles survive bank
// replacement; use reload=true for an explicit browser refresh.
bool openBank(const std::filesystem::path& gameRoot, std::string& err, bool reload = false);
// Decode a standalone payload without opening/caching an installed bank.
Effect decode(const std::vector<uint8_t>& payload);
bool bankOpen();
bool bankOpen(const std::filesystem::path& gameRoot);
// Case-insensitive lookup by entry name; nullptr when unknown. Parsed lazily and cached.
std::shared_ptr<const Effect> byName(const std::string& name);
std::shared_ptr<const Effect> byName(const std::filesystem::path& gameRoot, const std::string& name);
size_t entryCount();
// Every effect name in the bank (upper case, sorted); empty until openBank.
std::vector<std::string> entryNames();
std::vector<std::string> entryNames(const std::filesystem::path& gameRoot);

} // namespace albion::effects
