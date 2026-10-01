#include "particlepreview.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

// Preview behavior cross-checked against EgoCore (AeoN, MIT), commit 55bdc10,
// Particles/ParticleSimulator.h and ParticleRenderer.h. This implementation uses
// Forge's checked data model and explicit budgets; it does not claim RNG/native
// engine parity, scene collision, attachment or complete component simulation.

namespace albion::particlepreview {
namespace {
constexpr float Pi = 3.14159265358979323846f;
float finite(float value, float fallback = 0) { return std::isfinite(value) ? value : fallback; }
float bounded(float value, float lo, float hi) { return std::clamp(finite(value), lo, hi); }
float lerp(float a, float b, float t) { return a + (b-a)*t; }
using Quaternion=std::array<float,4>;
Quaternion normalized(Quaternion q) {
    double length=0;for(float v:q)length+=double(v)*v;
    if(!std::isfinite(length) || length<1e-20)return {0,0,0,1};
    const float inverse=float(1/std::sqrt(length));for(float& v:q)v*=inverse;
    return q;
}
// Hamilton product, matching native CQuaternion::operator*0339fa00.
Quaternion multiply(const Quaternion& a,const Quaternion& b) {
    return normalized({a[3]*b[0]+a[0]*b[3]+a[1]*b[2]-a[2]*b[1],
        a[3]*b[1]+a[1]*b[3]+a[2]*b[0]-a[0]*b[2],
        a[3]*b[2]+a[2]*b[3]+a[0]*b[1]-a[1]*b[0],
        a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2]});
}
Quaternion axisTurns(std::array<float,3> axis,float turns) {
    double length=0;for(float v:axis)length+=double(finite(v))*finite(v);
    if(length<1e-12 || !std::isfinite(length))return {0,0,0,1};
    const double half=std::remainder(double(finite(turns)),1.)*double(Pi);
    const float factor=float(std::sin(half)/std::sqrt(length));
    return normalized({finite(axis[0])*factor,finite(axis[1])*factor,finite(axis[2])*factor,float(std::cos(half))});
}
void normalize(std::array<float,3>& v) {
    const float length = std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
    if (length < .0001f) v={0,1,0};
    else for (auto& component:v) component/=length;
}
void singleOrbitPosition(const effects::SpriteSystem& s,double seconds,std::array<float,3>& position) {
    for(int axis=0;axis<3;++axis) position[axis]=bounded(s.offset[axis],-100000,100000);
    for(const auto& component:s.orbits) for(const auto& orbit:component.axes) {
        if(!orbit.enabled) continue;
        const double angle=std::remainder(double(finite(orbit.rotateStart))+seconds*bounded(orbit.rotateSpeed,-1000,1000),2*double(Pi));
        const double radius=std::clamp(double(finite(orbit.radius))+seconds*bounded(orbit.expand,-1000,1000),-100000.,100000.);
        position[0]=bounded(position[0]+float(std::cos(angle)*radius),-100000,100000);
        position[1]=bounded(position[1]+float(std::sin(angle)*radius),-100000,100000);
    }
}
}

void Simulation::warn(const std::string& message) {
    if (std::find(warnings_.begin(),warnings_.end(),message)==warnings_.end()) warnings_.push_back(message);
}
float Simulation::randomUnit() {
    // Explicit conversion avoids implementation-specific uniform_real_distribution.
    return float(random_() >> 8) * (1.f / 16777216.f);
}
float Simulation::meshRandomUnit() {
    return float(meshRandom_() >> 8) * (1.f / 16777216.f);
}
size_t Simulation::liveCount() const {
    size_t n=0; for (const auto& state:states_) n+=state.particles.size(); return n;
}

void Simulation::reset(const effects::Effect& effect) {
    states_.clear(); sprites_.clear(); meshes_.clear(); lightStates_.clear();lights_.clear(); warnings_.clear();
    random_.seed(1337);meshRandom_.seed(0x4d455348u);
    ticks_=0; accumulator_=0;
    for (const auto& item:effect.previewUnsupported) warn(item);
    if (!effect.parsedFully) warn("Effect is only partially decoded");
    for(const auto& light:effect.lights) {
        if(!light.enabled)continue;
        if(lightStates_.size()>=MaxLightComponents){warn("Preview light component limit reached (64)");break;}
        bool valid=true;
        const float values[]={light.lifeSecs,light.respawnDelaySecs,light.startTime,light.timelineSecs,light.radius,light.endRadius,light.radiusMinimum,
                              light.systemScale[0],light.systemScale[1],light.systemScale[2]};
        for(float value:values)valid=valid&&std::isfinite(value);
        if(!valid){warn("Non-finite light component omitted");continue;}
        LightState state;state.config=light;auto& l=state.config;
        bool changed=false;auto clamp=[&](float& value,float low,float high){const float next=std::clamp(value,low,high);changed=changed||next!=value;value=next;};
        clamp(l.lifeSecs,0,3600);clamp(l.respawnDelaySecs,0,3600);clamp(l.startTime,0,3600);clamp(l.timelineSecs,0,3600);
        clamp(l.radius,0,100000);clamp(l.endRadius,0,100000);clamp(l.radiusMinimum,0,1);
        for(float& scale:l.systemScale)clamp(scale,0,1000);
        const auto attenuation=std::clamp(l.attenuationFactor,-1,1000),fadeIn=std::clamp(l.fadeInEnd,0,1000),fadeOut=std::clamp(l.fadeOutBegin,0,1000);
        changed=changed||attenuation!=l.attenuationFactor||fadeIn!=l.fadeInEnd||fadeOut!=l.fadeOutBegin;
        l.attenuationFactor=attenuation;l.fadeInEnd=fadeIn;l.fadeOutBegin=fadeOut;
        if(changed)warn("Light values were bounded for isolated preview");
        if(l.positionParam)warn("Light position parameters are unavailable; local origin is used");
        state.alive=!l.useLife;lightStates_.push_back(std::move(state));
    }
    if(!lightStates_.empty())warn("Light volumes use local-origin fallback and system global alpha 1; scene illumination is not simulated");
    if(effect.emitter2D || effect.screenDisplacement || effect.readZ || effect.water || effect.preWater)
        warn("Screen-space, water and depth-reading effect passes are not previewed");
    if(effect.weatherMask) warn("Weather masks are not available in isolated preview");
    std::vector<State> inputs;
    for(const auto& sprite:effect.sprites) inputs.push_back({sprite});
    for(const auto& mesh:effect.meshes) {
        if(mesh.mesh<=0) {warn("A mesh system has no model");continue;}
        auto found=std::find_if(inputs.begin(),inputs.end(),[&](const State& state) {
            return mesh.systemIndex!=UINT32_MAX && state.config.systemIndex==mesh.systemIndex;
        });
        if(found==inputs.end()) {
            auto config=mesh.config;config.systemIndex=mesh.systemIndex;
            config.sprite=-1;
            inputs.push_back({config});found=inputs.end()-1;
        }
        if(found->meshes.size()<MaxMeshRenderers) found->meshes.push_back(mesh);
        else warn("Preview mesh renderer limit reached (16 per system)");
    }
    // Serialized order determines RNG consumption, including mesh-only systems.
    std::stable_sort(inputs.begin(),inputs.end(),[](const State& a,const State& b) {return a.config.systemIndex<b.config.systemIndex;});
    for (auto& input:inputs) {
        const auto& s=input.config;
        if (states_.size()>=MaxSystems) { warn("Preview system limit reached (64)"); break; }
        for (const auto& item:s.previewUnsupported) warn(item);
        if (s.sprite<0 && input.meshes.empty()) { warn("A sprite system has no texture"); continue; }
        if (!s.single && !s.emitter.present) { warn("A particle system has no active emitter"); continue; }
        const auto& e=s.emitter; const auto& u=s.update; const auto& r=s.render;
        if (e.hasSpline || e.type>2 || r.useSplinePoints) warn("Spline paths are not simulated");
        if (e.useTimeline) warn("Emitter timelines are not simulated");
        if (e.useParamDirection || e.positionParam || r.usePosition || u.useAccelerationParam)
            warn("External particle parameters are not available in isolated preview");
        if (u.collideGround || u.collideAnything) warn("Particle collisions are not simulated");
        if (u.wind!=0) warn("Scene wind is not simulated");
        if (u.randomisePosition || u.randomiseDistance) warn("Position randomisation updates are not simulated");
        if (u.systemAlphaFade || u.emissionFade) warn("System alpha/emission fades are not simulated");
        if (u.setOrientationDirection || u.setOrientationGame)
            warn("3D particle orientation is not simulated");
        if (u.createDecal || u.createDecalEmitter) warn("Collision decals are not simulated");
        if (u.stayWithEmitter || r.stayWithEmitter) warn("Emitter attachment is stationary in isolated preview");
        if (r.flicker) warn("Particle flicker is not simulated");
        if (r.trailTexture>0 && r.trailLength) warn("Particle trails are not previewed");
        if (r.crossed || r.crossedSprites>1 || (r.flags & 0x40000)) warn("Crossed sprites use a single billboard preview");
        if (e.radialBias!=0) warn("Emitter radial bias is not simulated");
        for(const auto& mesh:input.meshes) {
            if(mesh.useRenderSizeParam) warn("Mesh render-size parameters are unavailable; authored sizes are used");
            if(mesh.render.flicker) warn("Mesh flicker is not simulated");
            if(mesh.render.trailTexture>0 && mesh.render.trailLength) warn("Mesh trails are not previewed");
        }
        if(!input.meshes.empty() && (u.setOrientationDirection || u.setOrientationGame))
            warn("Direction/game mesh orientation is not simulated; initial and spin rotation are previewed");
        if(s.single && !input.meshes.empty()) {
            warn("Mesh renderers combined with persistent single sprites are not previewed");
            input.meshes.clear();
            if(s.sprite<0) continue;
        }
        states_.push_back(std::move(input));
        auto& state=states_.back();
        auto& config=state.config;
        if(config.orbits.size()>MaxForceComponents || config.attractors.size()>MaxForceComponents)
            warn("Preview force component limit reached (16 of each type per system)");
        config.orbits.resize(std::min(config.orbits.size(),MaxForceComponents));
        config.attractors.resize(std::min(config.attractors.size(),MaxForceComponents));
        for(const auto& component:config.orbits) {
            bool active=false;
            for(const auto& orbit:component.axes) if(orbit.enabled) {
                active=true;
                if(orbit.type!=0 || orbit.cycle || orbit.squeezeAngle!=0 || (orbit.squeezeScale!=0 && orbit.squeezeScale!=1) ||
                   orbit.rotateSpeedRandom!=0 || orbit.rotateStartRandom!=0)
                    warn("Orbit type, cycle, squeeze and random modulation are not simulated");
                if(!s.single && (orbit.radius!=0 || orbit.expand!=0 || orbit.rotateStart!=0))
                    warn("Moving particles use planar orbit forces, not authored radius/start trajectories");
            }
            if(active) {
                warn("Orbit preview uses the reference's XY plane approximation");
                if(component.centreParam) warn("Orbit centre parameters are unavailable; preview uses the local origin");
            }
        }
        for(auto& attractor:config.attractors) if(attractor.enabled) {
            if(s.single) warn("Attractor forces do not update persistent single sprites");
            if(attractor.useParamPosition) warn("Attractors requiring external positions are not simulated");
            if(attractor.points.size()>1) warn("Attractor preview uses only the first authored point");
            if(attractor.falloff<0 || attractor.falloff>2) warn("Attractor falloff uses the reference's approximate constant/linear/inverse-distance modes");
            if(attractor.points.size()>1) attractor.points.resize(1);
        }
        if (s.single) { spawn(state,1); state.burstDone=true; }
        else if (finite(e.startTime)<=0) {
            const size_t variation=std::min<uint32_t>(e.startCountRandom,MaxSystemParticles);
            const size_t count=std::min<uint32_t>(e.startCount,MaxSystemParticles)+size_t(randomUnit()*float(variation+1));
            spawn(state,count); state.burstDone=true;
        }
    }
    rebuild();
}

void Simulation::spawn(State& state,size_t count) {
    const auto& s=state.config; const auto& e=s.emitter; const auto& u=s.update;
    const size_t available=std::min(MaxSystemParticles-state.particles.size(),MaxParticles-liveCount());
    if (count>available) warn("Preview particle limit reached (4096 total / 2048 per system)");
    count=std::min(count,available);
    for (size_t i=0;i<count;++i) {
        Particle p;
        const float life=bounded(s.lifeSecs,.033333334f,3600.f);
        p.life=(!u.useParticleLife && u.present) ? 108000 : uint64_t(std::max(1.,std::ceil(double(life)*30.-1e-5)));
        std::array<float,3> outward{0,1,0};
        auto unit3=[&]() { const float a=randomUnit()*2*Pi,z=randomUnit()*2-1,rad=std::sqrt(std::max(0.f,1-z*z)); return std::array<float,3>{rad*std::cos(a),rad*std::sin(a),z}; };
        auto meshUnit3=[&]() {const float a=meshRandomUnit()*2*Pi,z=meshRandomUnit()*2-1,
            rad=std::sqrt(std::max(0.f,1-z*z));
            return std::array<float,3>{rad*std::cos(a),rad*std::sin(a),z};};
        if (!s.single && e.type==1) {
            const float a=randomUnit()*2*Pi;
            outward={std::cos(a),std::sin(a),0};
            if(e.orientationXZ) outward={outward[0],0,outward[1]};
            else if(e.orientationYZ) outward={0,outward[0],outward[1]};
        } else if (!s.single) outward=unit3();
        float radius=bounded(e.size,0,10000)*.5f;
        if(e.solid) radius*=e.type==1 ? std::sqrt(randomUnit()) : std::cbrt(randomUnit());
        for(int axis=0;axis<3;++axis) {
            p.position[axis]=bounded(s.offset[axis],-100000,100000);
            if(!s.single && (e.type==1 || e.type==2)) p.position[axis]+=outward[axis]*radius*(1+bounded(e.nonUniformScale[axis],-10,100)*.1f)*bounded(s.systemScale[axis],-1000,1000);
        }
        std::array<float,3> direction=outward;
        if(e.useRandom3DDirection) direction=unit3();
        else if(e.useRandom2DDirection) {
            const float a=randomUnit()*2*Pi; direction={std::cos(a),std::sin(a),0};
            if(e.orientationXZ) direction={direction[0],0,direction[1]};
            else if(e.orientationYZ) direction={0,direction[0],direction[1]};
        } else if(e.useCustomDirection) for(int axis=0;axis<3;++axis) direction[axis]=bounded(e.customDirection[axis],-10000,10000);
        else if(e.useForwardDirection) direction={0,1,0};
        normalize(direction);
        if(e.angularPerturbation) {
            auto perpendicular=std::array<float,3>{-direction[1],direction[0],0};
            if(std::abs(direction[2])>.99f) perpendicular={1,0,0};
            normalize(perpendicular);
            const std::array<float,3> cross{direction[1]*perpendicular[2]-direction[2]*perpendicular[1],direction[2]*perpendicular[0]-direction[0]*perpendicular[2],direction[0]*perpendicular[1]-direction[1]*perpendicular[0]};
            const float cone=randomUnit()*float(std::min(e.angularPerturbation,1023u))/1023.f*Pi*.5f,roll=randomUnit()*2*Pi;
            for(int axis=0;axis<3;++axis) direction[axis]=direction[axis]*std::cos(cone)+(perpendicular[axis]*std::cos(roll)+cross[axis]*std::sin(roll))*std::sin(cone);
        }
        const float minimum=bounded(e.minSpeed,-10000,10000),maximum=std::max(minimum,bounded(e.maxSpeed,-10000,10000));
        const float speed=lerp(minimum,maximum,randomUnit())*(e.oppositeDirection?-1.f:1.f);
        for(int axis=0;axis<3;++axis) p.velocity[axis]=s.single?0:direction[axis]*speed;
        p.angle=u.randomInitialRotation?randomUnit()*2*Pi:finite(u.initialRotation[2])+finite(s.render.initialAngle);
        p.angularVelocity=lerp(bounded(u.rotationMinSpeed,-1000,1000),bounded(u.rotationMaxSpeed,-1000,1000),randomUnit());
        if(!state.meshes.empty() && !s.single) {
            // Native UpdateAddParticle02f42899..02f42ae3: authored angles and
            // angular increments are turns; initial Q=(Qx*Qy)*Qz, delta on right.
            // Reuse the existing speed sample: adding mesh orientation must not
            // consume RNG or perturb the established sprite/emission sequence.
            p.orientation=u.randomInitialRotation?
                axisTurns(meshUnit3(),meshRandomUnit()):
                multiply(multiply(axisTurns({1,0,0},u.initialRotation[0]),
                    axisTurns({0,1,0},u.initialRotation[1])),axisTurns({0,0,1},u.initialRotation[2]));
            p.orientationChange=axisTurns(u.randomRotationAxis?meshUnit3():
                std::array<float,3>{u.rotationAxis[0],u.rotationAxis[1],u.rotationAxis[2]},
                p.angularVelocity);
        }
        if(s.single) { p.angle=finite(s.render.initialAngle); p.angularVelocity=0; }
        if(s.single && !s.orbits.empty()) singleOrbitPosition(s,time(),p.position);
        state.particles.push_back(p);
    }
}

void Simulation::tick() {
    const double before=time(); ++ticks_; const double now=time();
    updateLights();
    for(auto& state:states_) {
        const auto& s=state.config; const auto& u=s.update; const auto& e=s.emitter;
        const bool expired=u.useSystemLife && now>=std::max(0.f,finite(u.systemLife));
        for(auto& p:state.particles) {
            ++p.age;
            if(!s.single) {
                for(int axis=0;axis<3;++axis) {
                    float acceleration=bounded(u.acceleration[axis],-10000,10000)*bounded(u.accelerationScale,-100,100);
                    if(axis==2) acceleration-=9.82f*bounded(u.gravity,-100,100);
                    p.velocity[axis]=(p.velocity[axis]+acceleration*float(TickSeconds))*std::max(0.f,1-bounded(u.drag,0,1000)*float(TickSeconds));
                }
                bool forceApplied=false;
                for(const auto& attractor:s.attractors) {
                    if(!attractor.enabled || attractor.useParamPosition || !std::isfinite(attractor.radius) || attractor.radius<=0) continue;
                    std::array<float,3> delta{}; float distanceSquared=0;
                    for(int axis=0;axis<3;++axis) {
                        const float target=attractor.points.empty()?0:bounded(attractor.points[0][axis],-100000,100000);
                        delta[axis]=target-p.position[axis]; distanceSquared+=delta[axis]*delta[axis];
                    }
                    const float distance=std::sqrt(distanceSquared);
                    if(distance<=.0001f || distance>=attractor.radius) continue;
                    const float falloff=attractor.falloff==1?1-distance/attractor.radius:(attractor.falloff>=2?1/(1+distanceSquared):1);
                    const float force=bounded(attractor.force,-10000,10000)*falloff;
                    for(int axis=0;axis<3;++axis) p.velocity[axis]+=delta[axis]/distance*force*float(TickSeconds);
                    forceApplied=true;
                }
                for(const auto& component:s.orbits) for(const auto& orbit:component.axes) {
                    const float speed=bounded(orbit.rotateSpeed,-1000,1000);
                    if(!orbit.enabled || std::abs(speed)<=.001f || std::hypot(p.position[0],p.position[1])<=.01f) continue;
                    p.velocity[0]+=-p.position[1]*speed*float(TickSeconds);
                    p.velocity[1]+=p.position[0]*speed*float(TickSeconds);
                    forceApplied=true;
                }
                for(int axis=0;axis<3;++axis) {
                    if(forceApplied) p.velocity[axis]=bounded(p.velocity[axis],-100000,100000);
                    p.position[axis]+=p.velocity[axis]*float(TickSeconds);
                    if(forceApplied) p.position[axis]=bounded(p.position[axis],-100000,100000);
                }
            } else if(!s.orbits.empty()) {
                singleOrbitPosition(s,now,p.position);
            }
            p.angle+=p.angularVelocity*float(TickSeconds);
            if(!state.meshes.empty() && !s.single) p.orientation=multiply(p.orientation,p.orientationChange);
        }
        std::erase_if(state.particles,[&](const Particle& p){return expired || (!s.single && p.age>=p.life);});
        if(s.single || expired) continue;
        const double start=std::max(0.f,finite(e.startTime));
        if(now+1e-7<start) continue;
        if(!state.burstDone) {
            const size_t variation=std::min<uint32_t>(e.startCountRandom,MaxSystemParticles);
            spawn(state,std::min<uint32_t>(e.startCount,MaxSystemParticles)+size_t(randomUnit()*float(variation+1))); state.burstDone=true;
        }
        const double end=e.useLife?start+std::max(0.f,finite(e.lifeSecs)):std::numeric_limits<double>::infinity();
        const double interval=std::max(0.,std::min(now,end)-std::max(before,start));
        state.fractionalSpawn+=interval*bounded(s.perSecond,0,100000);
        const size_t count=size_t(std::floor(state.fractionalSpawn+1e-7));
        state.fractionalSpawn-=double(count);
        spawn(state,std::min<size_t>(count,256));
        if(count>256) warn("Preview spawn limit reached (256 per system per tick)");
    }
}

// Native CPSCLight::Update 02f52620: one independent timer per component.
// Light-only systems do not require emitters and never consume particle RNG.
void Simulation::updateLights() {
    for(auto& state:lightStates_) {
        const auto& l=state.config;
        if(!l.useLife){state.alive=true;continue;}
        if(!state.initialized){state.timer=-int64_t(std::nearbyint(double(l.startTime)*30));state.initialized=true;}
        const bool timelineAllows=!l.useTimeline || double(state.timer)/30+double(l.startTime)<l.timelineSecs;
        if(state.timer<0){if(timelineAllows)++state.timer;state.alive=false;continue;}
        const int64_t lifeTicks=int64_t(double(l.lifeSecs)*30);
        if(state.timer!=0 && (state.timer>lifeTicks || !state.alive)) {
            if(l.respawns && double(state.delayTimer)>=double(l.respawnDelaySecs)*30){state.timer=state.delayTimer=0;state.alive=true;}
            else {state.alive=false;state.delayTimer=std::min<int64_t>(state.delayTimer+1,108001);}
        } else if(timelineAllows){state.alive=true;++state.timer;}
    }
}
void Simulation::rebuildLights() {
    lights_.clear();lights_.reserve(lightStates_.size());
    for(const auto& state:lightStates_) {
        if(!state.alive)continue;
        const auto& l=state.config;DrawLight draw;draw.systemIndex=l.systemIndex;
        const float progress=l.useLife && l.lifeSecs>0?float(std::clamp(double(state.timer)/(double(l.lifeSecs)*30),0.,1.)):0;
        const uint8_t* first=l.useStartColour?l.colour:(l.useMidColour?l.midColour:(l.useEndColour?l.endColour:nullptr));
        const uint8_t* last=l.useEndColour?l.endColour:(l.useMidColour?l.midColour:(l.useStartColour?l.colour:nullptr));
        for(int channel=0;channel<3;++channel){const float start=first?first[channel]/255.f:1,end=last?last[channel]/255.f:1;
            const float middle=l.useMidColour?l.midColour[channel]/255.f:(start+end)*.5f;
            draw.colour[channel]=progress<.5f?lerp(start,middle,progress*2):lerp(middle,end,(progress-.5f)*2);}
        // Native SmoothFadeValue02f532d0 uses cosine in turns, equivalent to this S curve.
        auto smooth=[](float value){return .5f-.5f*std::cos(Pi*std::clamp(value,0.f,1.f));};
        const float fadeIn=l.fadeInEnd/1000.f,fadeOut=l.fadeOutBegin/1000.f;
        float fade=1;
        if(progress<fadeIn)fade*=smooth(progress/fadeIn);
        if(progress>fadeOut)fade*=smooth((1-progress)/(1-fadeOut));
        float radiusScale=1;
        if(fade<1){
            if(l.colourFade)for(int channel=0;channel<3;++channel){const float minimum=l.colourMinimum[channel]/255.f;
                draw.colour[channel]=std::max(minimum,draw.colour[channel]*lerp(minimum,1,fade));}
            if(l.radiusFade)radiusScale=lerp(l.radiusMinimum,1,fade);
        }
        if(l.scaleParticles)radiusScale*=std::max({l.systemScale[0],l.systemScale[1],l.systemScale[2]});
        draw.radius=lerp(l.radius,l.endRadius,progress)*radiusScale;
        draw.attenuationDistance=draw.radius*((float(l.attenuationFactor)+1)/1000.f);
        lights_.push_back(draw);
    }
}

void Simulation::advance(double seconds) {
    if(!std::isfinite(seconds) || seconds<=0) return;
    accumulator_+=std::min(seconds,.2);
    for(int i=0;i<6 && accumulator_+1e-10>=TickSeconds;++i) { tick(); accumulator_-=TickSeconds; }
    accumulator_=std::max(0.,accumulator_);
    rebuild();
}
void Simulation::seek(const effects::Effect& effect,double seconds) {
    if(!std::isfinite(seconds)) return;
    seconds=std::clamp(seconds,0.0,MaxSeekSeconds);
    reset(effect);
    const uint64_t target=uint64_t(std::floor(seconds/TickSeconds+1e-9));
    for(uint64_t i=0;i<target;++i) tick();
    accumulator_=std::clamp(seconds-time(),0.0,TickSeconds);
    rebuild();
}
void Simulation::step() { tick(); rebuild(); }

void Simulation::rebuild() {
    rebuildLights();
    sprites_.clear(); sprites_.reserve(liveCount());
    meshes_.clear();
    for(const auto& state:states_) {
        const auto& s=state.config; const auto& r=s.render;
        for(const auto& p:state.particles) {
            DrawSprite d; std::copy(p.position.begin(),p.position.end(),d.position);
            d.texture=s.sprite; d.angle=p.angle; d.blendMode=s.blendMode; d.blendOperation=int(r.blendOp);
            float progress=std::clamp(float(p.age)/float(p.life),0.f,1.f);
            float phase=progress;
            if(s.single || r.forceAnimation) {
                const double period=std::max(.033333333,double(finite(r.animationSecs,1)));
                phase=float(std::fmod(double(p.age)*TickSeconds,period)/period);
            }
            if(s.single) progress=phase;
            d.framePhase=phase;
            const uint8_t* first=r.useStartColour?s.colour:(r.useMidColour?r.midColour:(r.useEndColour?r.endColour:nullptr));
            const uint8_t* last=r.useEndColour?r.endColour:(r.useMidColour?r.midColour:first);
            float colourProgress=progress;
            if(r.useMidColour) {
                if(progress<.5f) {last=r.midColour;colourProgress=progress*2;}
                else {first=r.midColour;colourProgress=(progress-.5f)*2;}
            }
            for(int component=0;component<4;++component) d.colour[component]=lerp(first?first[component]/255.f:1.f,last?last[component]/255.f:1.f,colourProgress);
            const float startSize=bounded(s.hasAuthoredSizes?s.authoredStartSize:s.startSize,0,10000);
            const float endSize=bounded(s.hasAuthoredSizes?s.authoredEndSize:s.endSize,0,10000);
            float systemScale=s.hasAuthoredSizes?std::max({finite(s.systemScale[0]),finite(s.systemScale[1]),finite(s.systemScale[2])}):1;
            if(systemScale<=.0001f) systemScale=1;
            systemScale=std::min(systemScale,1000.f);
            const float size=r.sizeFade?std::max(0.f,lerp(startSize,endSize,progress)):startSize;
            d.size[0]=d.size[1]=size;
            auto fade=[&]() {
                const float in=std::clamp(float(r.fadeInEnd)/1000.f,0.f,1.f),out=std::clamp(float(r.fadeOutBegin)/1000.f,0.f,1.f);
                float value=1;
                if(in>0 && progress<in) value*=.5f-.5f*std::cos(Pi*progress/in);
                if(out<1 && progress>out) value*=.5f-.5f*std::cos(Pi*(1-progress)/(1-out));
                return std::clamp(value,0.f,1.f);
            };
            if(r.alphaFade) d.colour[3]*=lerp(bounded(r.alphaMinimum,0,1),1,fade());
            if(r.sizeFade) d.size[0]=d.size[1]=s.single?size*lerp(bounded(r.sizeMinimum,0,1),1,fade()):std::max(size,bounded(r.sizeMinimum,0,10000));
            d.size[0]*=systemScale; d.size[1]*=systemScale;
            if(s.sprite>=0) sprites_.push_back(d);
            for(const auto& mesh:state.meshes) {
                if(meshes_.size()>=MaxMeshDraws) {warn("Preview mesh draw limit reached (4096)");break;}
                DrawMesh draw;draw.mesh=mesh.mesh;draw.centredOnPosition=mesh.centredOnPosition;
                draw.blendMode=mesh.blendMode;draw.blendOperation=int(mesh.render.blendOp);
                std::copy(p.position.begin(),p.position.end(),draw.position);
                std::copy(p.orientation.begin(),p.orientation.end(),draw.orientation);
                const auto& mr=mesh.render;
                const uint8_t* a=mr.useStartColour?mesh.colour:(mr.useMidColour?mr.midColour:(mr.useEndColour?mr.endColour:nullptr));
                const uint8_t* b=mr.useEndColour?mr.endColour:(mr.useMidColour?mr.midColour:a);
                float t=progress;
                if(mr.useMidColour) {
                    if(progress<.5f) {b=mr.midColour;t=progress*2;}
                    else {a=mr.midColour;t=(progress-.5f)*2;}
                }
                for(int k=0;k<4;++k)draw.colour[k]=lerp(a?a[k]/255.f:1,b?b[k]/255.f:1,t);
                const float in=bounded(float(mr.fadeInEnd)/1000,0,1),out=bounded(float(mr.fadeOutBegin)/1000,0,1);
                float fade=1;
                if(in>0 && progress<in)fade*=.5f-.5f*std::cos(Pi*progress/in);
                if(out<1 && progress>out)fade*=.5f-.5f*std::cos(Pi*(1-progress)/(1-out));
                if(mr.alphaFade)draw.colour[3]*=lerp(bounded(mr.alphaMinimum,0,1),1,fade);
                const bool defaultStart=finite(mesh.size[0])<=.0001f && finite(mesh.size[1])<=.0001f && finite(mesh.size[2])<=.0001f;
                const bool defaultEnd=finite(mesh.endSize[0])<=.0001f && finite(mesh.endSize[1])<=.0001f && finite(mesh.endSize[2])<=.0001f;
                for(int axis=0;axis<3;++axis) {
                    const float start=defaultStart?1:bounded(mesh.size[axis],0,10000);
                    const float end=defaultEnd?start:bounded(mesh.endSize[axis],0,10000);
                    float scale=finite(s.systemScale[axis]);if(scale<=.0001f)scale=1;
                    // Reference mesh sizes interpolate even when the additional cosine fade is disabled.
                    draw.size[axis]=lerp(start,end,progress)*std::min(scale,1000.f);
                    if(mr.sizeFade)draw.size[axis]*=lerp(bounded(mr.sizeMinimum,0,1),1,fade);
                }
                meshes_.push_back(draw);
            }
        }
    }
}
} // namespace albion::particlepreview
