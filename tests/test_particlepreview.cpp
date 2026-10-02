#include "particlepreview.hpp"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>

using namespace albion;
namespace {
effects::Effect fixture() {
    effects::Effect effect; effect.parsedFully=true;
    effects::SpriteSystem s; s.sprite=7; s.system="fixture";
    s.emitter.present=true; s.emitter.useCustomDirection=true; s.emitter.customDirection[2]=1;
    s.emitter.minSpeed=s.emitter.maxSpeed=3; s.emitter.startCount=2;
    s.update.present=true; s.update.useParticleLife=true; s.lifeSecs=1;
    s.perSecond=30; s.startSize=2; s.endSize=4; s.render.useStartColour=true;
    s.render.sizeFade=true;
    s.render.useEndColour=true; s.colour[0]=0; s.render.endColour[0]=255;
    effect.sprites.push_back(s); return effect;
}
bool equal(const std::vector<particlepreview::DrawSprite>& a,const std::vector<particlepreview::DrawSprite>& b) {
    if(a.size()!=b.size()) return false;
    for(size_t i=0;i<a.size();++i) {
        for(int axis=0;axis<3;++axis) if(a[i].position[axis]!=b[i].position[axis]) return false;
        for(int channel=0;channel<4;++channel) if(a[i].colour[channel]!=b[i].colour[channel]) return false;
        if(a[i].angle!=b[i].angle || a[i].size[0]!=b[i].size[0] || a[i].framePhase!=b[i].framePhase) return false;
    }
    return true;
}
}
int main(int argc,char** argv) {
    int checks=0,failures=0;
    auto check=[&](bool okay,const char* message){++checks;if(!okay){++failures;std::fprintf(stderr,"particle preview: %s\n",message);}};
    particlepreview::Simulation a,b;
    auto effect=fixture(); a.reset(effect); b.reset(effect);
    check(a.particleCount()==2 && a.time()==0,"reset burst");
    for(int i=0;i<12;++i) a.advance(1./30.);
    for(int i=0;i<48;++i) b.advance(1./120.);
    check(a.time()==b.time() && equal(a.sprites(),b.sprites()),"split-frame deterministic state");
    const auto saved=a.sprites(); a.reset(effect); for(int i=0;i<12;++i) a.step();
    check(equal(saved,a.sprites()),"reset and explicit step reproduce state");
    a.seek(effect,0.4);b.reset(effect);for(int i=0;i<12;++i)b.step();
    check(a.time()==b.time() && equal(a.sprites(),b.sprites()),
          "seek reproduces stepped particle state");
    a.seek(effect,0.45);b.step();b.step();
    a.advance(1./60.);
    check(a.time()==b.time() && equal(a.sprites(),b.sprites()),
          "seek retains fractional tick for resumed playback");
    a.seek(effect,0.4);
    check(std::abs(a.sprites()[0].position[2]-1.2f)<1e-5,"normal velocity movement");
    check(std::abs(a.sprites()[0].size[0]-2.8f)<1e-5 && std::abs(a.sprites()[0].colour[0]-.4f)<1e-5,"size and colour timeline");
    auto randomEffect=fixture(); randomEffect.sprites[0].emitter.type=2;
    randomEffect.sprites[0].emitter.size=5; randomEffect.sprites[0].emitter.solid=true;
    randomEffect.sprites[0].emitter.useRandom3DDirection=true;
    randomEffect.sprites[0].update.randomInitialRotation=true;
    b.reset(randomEffect); for(int i=0;i<10;++i) b.step(); const auto randomSaved=b.sprites();
    b.reset(randomEffect); for(int i=0;i<10;++i) b.step();
    check(equal(randomSaved,b.sprites()),"seed reproduces randomized positions and angles");
    check(b.sprites()[0].position[0]!=b.sprites()[1].position[0],"randomized particles have distinct positions");
    const auto paused=a.sprites(); const double pausedTime=a.time();
    a.advance(0); a.advance(-1); a.advance(std::numeric_limits<double>::infinity());
    check(equal(paused,a.sprites()) && a.time()==pausedTime,"paused and invalid elapsed preserve state");
    a.step(); check(std::abs(a.time()-pausedTime-1./30.)<1e-10,"paused single step");
    a.reset(effect); a.advance(1000); check(std::abs(a.time()-.2)<1e-10,"elapsed clamp six ticks");
    effect=fixture(); effect.sprites[0].emitter.startTime=.2f; effect.sprites[0].perSecond=0;
    a.reset(effect);
    check(a.estimatedDuration() && std::abs(*a.estimatedDuration()-1.2)<.04,"finite preview length includes delayed burst and particle tail");
    a.reset(effect); for(int i=0;i<5;++i) a.step(); check(a.particleCount()==0,"delay prevents burst");
    a.step(); a.step(); check(a.particleCount()==2,"delayed burst starts");
    for(int i=0;i<30;++i) a.step(); check(a.particleCount()==0,"particles expire without respawn");
    effect=fixture(); effect.sprites[0].emitter.startCount=0; effect.sprites[0].emitter.useLife=true; effect.sprites[0].emitter.lifeSecs=.1f;
    a.reset(effect);
    check(a.estimatedDuration() && *a.estimatedDuration()>=1.1 && *a.estimatedDuration()<1.14,"finite emitter length includes last particles after emission ends");
    a.reset(effect); for(int i=0;i<15;++i) a.step(); check(a.particleCount()==3,"finite emitter stops continuous emission");
    effect=fixture(); effect.sprites[0].update.gravity=1; effect.sprites[0].update.accelerationScale=1; effect.sprites[0].update.acceleration[0]=3;
    a.reset(effect); a.step(); check(a.sprites()[0].position[0]>0 && a.sprites()[0].position[2]<.1f,"acceleration and gravity affect motion");
    effect=fixture(); effect.sprites[0].single=true; effect.sprites[0].render.animationSecs=1;
    a.reset(effect);check(!a.estimatedDuration(),"persistent single has no finite preview length");
    a.reset(effect); for(int i=0;i<45;++i) a.step(); check(a.particleCount()==1 && std::abs(a.sprites()[0].framePhase-.5f)<1e-5,"persistent single sprite loops");
    effect.sprites[0].update.useSystemLife=true; effect.sprites[0].update.systemLife=.1f;
    a.reset(effect);check(a.estimatedDuration() && *a.estimatedDuration()<.14,"system lifetime bounds persistent single");
    a.reset(effect); for(int i=0;i<4;++i) a.step(); check(a.particleCount()==0,"single sprite respects system lifetime");
    effect=fixture(); effect.sprites[0].emitter.startCount=100000; effect.sprites[0].perSecond=100000;
    effect.sprites.resize(80,effect.sprites[0]); a.reset(effect);
    check(a.particleCount()==particlepreview::Simulation::MaxParticles && a.supportedSystems()==particlepreview::Simulation::MaxSystems,"global and system caps");
    for(int i=0;i<60;++i) a.step(); check(a.particleCount()<=particlepreview::Simulation::MaxParticles && !a.warnings().empty(),"caps sustained with diagnostics");
    effect=fixture(); effect.sprites[0].emitter.present=false; a.reset(effect);
    check(a.particleCount()==0 && a.supportedSystems()==0,"disabled or absent emitter does not invent particles");
    effect.sprites.clear(); a.reset(effect); check(a.sprites().empty() && a.warnings().empty(),"empty effect clears state");
    effect=fixture(); effect.sprites[0].previewUnsupported={"CPSCOrbit"}; effect.sprites[0].update.collideGround=true;
    a.reset(effect); check(a.warnings().size()>=2,"unsupported active components reported");
    effect=fixture(); effect.sprites[0].emitter.startCount=0; effect.sprites[0].perSecond=0;
    a.reset(effect); a.step(); check(a.particleCount()==0,"zero rate has no invented particle");
    effect=fixture(); effect.sprites[0].render.trailTexture=0; effect.sprites[0].render.trailLength=1000;
    a.reset(effect); check(a.warnings().empty(),"zero trail texture id means absent despite stored length");
    effect.sprites[0].render.trailTexture=42; a.reset(effect);
    check(a.warnings().size()==1 && a.warnings()[0]=="Particle trails are not previewed","active trail texture is reported");
    effect=fixture(); effect.sprites[0].hasAuthoredSizes=true;
    effect.sprites[0].authoredStartSize=2; effect.sprites[0].authoredEndSize=4;
    effect.sprites[0].systemScale[0]=.25f; effect.sprites[0].systemScale[1]=.5f; effect.sprites[0].systemScale[2]=2;
    effect.sprites[0].startSize=.5f; effect.sprites[0].endSize=1;
    a.reset(effect); check(a.sprites()[0].size[0]==4,"preview uses max XYZ authored scale without doubling export X scale");
    // Normal and persistent-sprite size policies differ in the pinned reference.
    effect=fixture(); effect.sprites[0].render.sizeFade=false; a.reset(effect);
    for(int i=0;i<15;++i) a.step(); check(a.sprites()[0].size[0]==2,"disabled size fade keeps authored start size");
    effect.sprites[0].render.sizeFade=true; effect.sprites[0].render.sizeMinimum=3.5f;
    a.reset(effect); a.step(); check(a.sprites()[0].size[0]==3.5f,"normal size fade clamps absolute minimum without cosine");
    effect=fixture(); effect.sprites[0].single=true; effect.sprites[0].render.animationSecs=1;
    effect.sprites[0].render.fadeInEnd=1000; effect.sprites[0].render.fadeOutBegin=1000;
    a.reset(effect); for(int i=0;i<15;++i) a.step();
    check(std::abs(a.sprites()[0].size[0]-1.5f)<1e-5,"single sprite size retains cosine fade");
    effect.sprites[0].render.initialAngle=.25f; effect.sprites[0].update.initialRotation[2]=1;
    effect.sprites[0].update.rotationMinSpeed=effect.sprites[0].update.rotationMaxSpeed=2;
    a.reset(effect); for(int i=0;i<5;++i) a.step(); check(a.sprites()[0].angle==.25f,"single sprite angle ignores normal particle rotation");

    auto forceFixture=[] {auto result=fixture(); auto& s=result.sprites[0]; s.perSecond=0; s.emitter.startCount=1;
        s.emitter.minSpeed=s.emitter.maxSpeed=0; s.offset[0]=1; return result;};
    effect=forceFixture(); effect.sprites[0].orbits.resize(1);
    auto& orbit=effect.sprites[0].orbits[0].axes[0]; orbit.enabled=true; orbit.rotateSpeed=3;
    a.reset(effect); a.step(); check(std::abs(a.sprites()[0].position[1]-1.f/300.f)<1e-6,"orbit creates tangential acceleration");
    check(a.sprites()[0].position[0]==1 && a.sprites()[0].position[2]==0,"orbit does not rotate position directly");
    for(int i=0;i<9;++i) a.step(); const auto orbitSaved=a.sprites();
    b.reset(effect); for(int i=0;i<40;++i) b.advance(1./120.);
    check(equal(orbitSaved,b.sprites()),"orbit split-frame determinism");
    effect.sprites[0].orbits[0].axes[0].enabled=false; a.reset(effect); a.step();
    check(a.sprites()[0].position[1]==0,"disabled orbit has no influence");
    effect.sprites[0].orbits[0].axes[0].enabled=true; effect.sprites[0].orbits[0].axes[0].rotateSpeed=-3;
    a.reset(effect); a.step(); check(a.sprites()[0].position[1]<0,"negative orbit reverses tangent");
    effect.sprites[0].single=true; auto& singleOrbit=effect.sprites[0].orbits[0].axes[0];
    singleOrbit.radius=2; singleOrbit.rotateSpeed=3.14159265f; singleOrbit.expand=1;
    a.reset(effect); check(a.sprites()[0].position[0]==3,"single orbit starts at radius plus offset");
    for(int i=0;i<30;++i) a.step();
    check(std::abs(a.sprites()[0].position[0]+2)<1e-5 && std::abs(a.sprites()[0].position[1])<1e-5,"single orbit analytic expansion without cumulative drift");

    effect=forceFixture(); effect.sprites[0].attractors.resize(1);
    auto& attractor=effect.sprites[0].attractors[0]; attractor.enabled=true; attractor.radius=2; attractor.force=9;
    a.reset(effect); a.step(); check(std::abs(a.sprites()[0].position[0]-.99f)<1e-6,"constant attractor accelerates toward origin");
    attractor.falloff=1; a.reset(effect); a.step(); check(std::abs(a.sprites()[0].position[0]-.995f)<1e-6,"linear attractor falloff");
    attractor.falloff=2; a.reset(effect); a.step(); check(std::abs(a.sprites()[0].position[0]-.995f)<1e-6,"inverse-distance attractor falloff");
    attractor.falloff=0; attractor.force=-9; a.reset(effect); a.step(); check(a.sprites()[0].position[0]>1,"negative attractor force repels");
    attractor.force=9; attractor.points={{{2,0,0}}}; a.reset(effect); a.step();
    check(a.sprites()[0].position[0]>1,"authored attractor target controls direction");
    attractor.radius=1; a.reset(effect); a.step(); check(a.sprites()[0].position[0]==1,"radius boundary is excluded");
    attractor.radius=.5f; a.reset(effect); a.step(); check(a.sprites()[0].position[0]==1,"outside radius unaffected");
    attractor.radius=2; attractor.enabled=false; a.reset(effect); a.step(); check(a.sprites()[0].position[0]==1,"internal attractor enabled flag respected");
    attractor.enabled=true; attractor.useParamPosition=true; a.reset(effect); a.step();
    check(a.sprites()[0].position[0]==1 && !a.warnings().empty(),"external target skipped with diagnostic");
    attractor.useParamPosition=false; attractor.points={{{1,0,0}}}; a.reset(effect); a.step();
    check(a.sprites()[0].position[0]==1,"coincident attractor avoids division by zero");
    attractor.points={{{0,0,0}},{{3,0,0}}}; a.reset(effect); b.reset(effect);
    for(int i=0;i<10;++i) a.step(); for(int i=0;i<40;++i) b.advance(1./120.);
    check(equal(a.sprites(),b.sprites()) && !a.warnings().empty(),"multi-point first-target approximation deterministic and disclosed");
    for(int i=0;i<20;++i) a.step(); check(a.particleCount()==0,"forces preserve particle lifetime expiration");
    effect.sprites[0].attractors.resize(100,effect.sprites[0].attractors[0]);
    a.reset(effect); for(int i=0;i<10;++i) a.step();
    check(std::isfinite(a.sprites()[0].position[0]) && !a.warnings().empty(),"force component work bounded with diagnostics");
    effect=fixture();effect.sprites[0].systemIndex=4;effect.sprites[0].perSecond=0;
    effects::MeshSystem mesh;mesh.mesh=99;mesh.systemIndex=4;mesh.config=effect.sprites[0];
    mesh.size[0]=2;mesh.size[1]=3;mesh.size[2]=4;
    mesh.endSize[0]=4;mesh.endSize[1]=5;mesh.endSize[2]=6;
    mesh.render.useStartColour=true;mesh.render.useEndColour=true;mesh.colour[0]=0;
    mesh.centredOnPosition=true;mesh.blendMode=3;mesh.render.blendOp=2;
    effect.meshes.push_back(mesh);a.reset(effect);
    check(a.supportedSystems()==1 && a.particleCount()==2 && a.sprites().size()==2 && a.meshes().size()==2,"mixed renderers share one particle population");
    a.step();check(a.meshes()[0].position[2]==a.sprites()[0].position[2],"mixed renderer positions are identical");
    effect.sprites.clear();a.reset(effect);for(int i=0;i<15;++i)a.step();
    check(a.supportedSystems()==1 && a.particleCount()==2 && a.sprites().empty() && a.meshes().size()==2,"mesh-only emission is visible and counted");
    check(std::abs(a.meshes()[0].size[0]-3)<1e-6 && std::abs(a.meshes()[0].colour[0]-.5f)<1e-6,"mesh size interpolation and colour fade");
    check(a.meshes()[0].orientation[3]==1 && a.meshes()[0].centredOnPosition && a.meshes()[0].blendOperation==2,"mesh transform and blend contract");
    effect.meshes[0].config.systemScale[0]=2;effect.meshes[0].config.systemScale[1]=3;effect.meshes[0].config.systemScale[2]=4;
    effect.meshes[0].render.sizeFade=true;effect.meshes[0].render.alphaFade=true;
    effect.meshes[0].render.fadeInEnd=1000;effect.meshes[0].render.fadeOutBegin=1000;
    a.reset(effect);for(int i=0;i<15;++i)a.step();
    check(std::abs(a.meshes()[0].size[0]-3)<1e-5 && std::abs(a.meshes()[0].size[1]-6)<1e-5 && std::abs(a.meshes()[0].colour[3]-.5f)<1e-5,"mesh cosine fades and per-axis system scale");
    auto second=effect.meshes[0];second.systemIndex=5;second.config.systemIndex=5;second.config.offset[0]=20;
    effect.meshes.push_back(second);a.reset(effect);
    check(a.supportedSystems()==2 && a.particleCount()==4 && a.meshes()[2].position[0]==20,"repeated system names retain separate identities");
    effect.meshes[0].config.emitter.startCount=4096;effect.meshes[1].config.emitter.startCount=4096;
    for(int i=0;i<20;++i)effect.meshes.push_back(effect.meshes[0]);a.reset(effect);
    check(a.particleCount()==4096 && a.meshes().size()==particlepreview::Simulation::MaxMeshDraws && !a.warnings().empty(),"shared particle and mesh renderer/draw caps enforced");
    effect=fixture();mesh.config=effect.sprites[0];mesh.config.systemIndex=4;
    mesh.config.update.randomRotationAxis=true;
    mesh.config.update.rotationMinSpeed=mesh.config.update.rotationMaxSpeed=.25f;
    mesh.systemIndex=4;
    effect.sprites.clear();effect.meshes={mesh};a.reset(effect);
    a.step();
    const auto randomAxisPose=a.meshes()[0].orientation;
    const float randomAxisVector=std::hypot(randomAxisPose[0],randomAxisPose[1],randomAxisPose[2]);
    check(randomAxisVector>.5f && std::abs(randomAxisPose[3]-std::sqrt(.5f))<1e-5f,
          "random mesh spin axis applies the sampled quarter turn");
    a.reset(effect);a.step();
    check(a.meshes()[0].orientation[0]==randomAxisPose[0] &&
          a.meshes()[0].orientation[1]==randomAxisPose[1] &&
          a.meshes()[0].orientation[2]==randomAxisPose[2],
          "random mesh spin axis resets deterministically");
    effect.meshes[0].mesh=0;a.reset(effect);
    check(a.particleCount()==0 && a.meshes().empty() && !a.warnings().empty(),"mesh bank ID zero is an absent-model sentinel");
    a.reset(fixture());check(a.meshes().empty(),"reset releases previous mesh outputs");
    auto orientationFixture=[&] {
        auto fx=fixture();auto& s=fx.sprites[0];s.systemIndex=0;s.perSecond=0;s.emitter.startCount=1;s.lifeSecs=100;
        effects::MeshSystem m;m.mesh=99;m.systemIndex=0;m.config=s;fx.meshes={m};return fx;
    };
    const float rootHalf=std::sqrt(.5f);
    for(int axis=0;axis<3;++axis) {
        effect=orientationFixture();effect.sprites[0].update.initialRotation[axis]=.25f;a.reset(effect);
        check(std::abs(a.meshes()[0].orientation[axis]-rootHalf)<1e-6 && std::abs(a.meshes()[0].orientation[3]-rootHalf)<1e-6,"authored quarter turn around each coordinate axis");
    }
    effect=orientationFixture();effect.sprites[0].update.initialRotation[0]=effect.sprites[0].update.initialRotation[1]=.25f;
    a.reset(effect);check(std::abs(a.meshes()[0].orientation[0]-.5f)<1e-6 && std::abs(a.meshes()[0].orientation[1]-.5f)<1e-6 &&
        std::abs(a.meshes()[0].orientation[2]-.5f)<1e-6 && std::abs(a.meshes()[0].orientation[3]-.5f)<1e-6,"native initial quaternion order is Qx times Qy times Qz");
    effect=orientationFixture();auto& spin=effect.sprites[0].update;spin.initialRotation[0]=.25f;spin.rotationAxis[2]=7;
    spin.rotationMinSpeed=spin.rotationMaxSpeed=.25f;a.reset(effect);a.step();
    check(std::abs(a.meshes()[0].orientation[0]-.5f)<1e-6 && std::abs(a.meshes()[0].orientation[1]+.5f)<1e-6 &&
        std::abs(a.meshes()[0].orientation[2]-.5f)<1e-6,"spin delta is normalized-axis turns per tick multiplied on right");
    spin.rotationAxis[2]=0;a.reset(effect);for(int i=0;i<17;++i)a.step();
    check(std::abs(a.meshes()[0].orientation[0]-rootHalf)<1e-6 && a.meshes()[0].orientation[2]==0,"zero authored axis has identity increment");
    spin.rotationAxis[2]=1;spin.rotationMinSpeed=spin.rotationMaxSpeed=-.003f;
    a.reset(effect);for(int i=0;i<2000;++i)a.step();double norm=0;for(float v:a.meshes()[0].orientation)norm+=double(v)*v;
    check(std::abs(norm-1)<1e-6,"long-running quaternion rotation stays normalized");
    effect=orientationFixture();effect.sprites[0].update.randomInitialRotation=true;effect.sprites[0].update.rotationMinSpeed=-.1f;
    effect.sprites[0].update.rotationMaxSpeed=.1f;
    effect.sprites[0].update.randomRotationAxis=true;
    effect.sprites[0].perSecond=30;
    a.reset(effect);const auto randomStart=a.meshes()[0].orientation;
    a.reset(effect);
    check(a.meshes()[0].orientation[0]==randomStart[0] &&
          a.meshes()[0].orientation[1]==randomStart[1] &&
          a.meshes()[0].orientation[2]==randomStart[2] &&
          std::hypot(randomStart[0],randomStart[1],randomStart[2])>.05f,
          "random initial mesh orientation is nontrivial and repeatable");
    auto spriteOnly=effect;spriteOnly.meshes.clear();b.reset(spriteOnly);
    for(int i=0;i<20;++i){a.step();b.step();}
    check(equal(a.sprites(),b.sprites()),"mesh orientation preserves all existing sprite RNG positions angles and colours");
    const auto orientation=a.meshes()[0];a.reset(effect);for(int i=0;i<80;++i)a.advance(1./120.);
    check(std::abs(a.meshes()[0].orientation[2]-orientation.orientation[2])<1e-6,"mesh orientation split-frame determinism");
    {
        auto timing=fixture();
        a.reset(timing);check(!a.estimatedDuration(),"continuous emission keeps an explicit preview window");
        timing.sprites[0].emitter.startCount=0;timing.sprites[0].perSecond=0;
        a.reset(timing);check(a.estimatedDuration()==0,"empty system adds no artificial preview tail");
        timing.sprites.clear();effects::LightSystem light;light.useLife=true;light.lifeSecs=.2f;light.startTime=.5f;
        timing.lights.push_back(light);a.reset(timing);
        check(a.estimatedDuration() && *a.estimatedDuration()>.7 && *a.estimatedDuration()<.8,"light-only preview includes delay and independent timer");
        timing.lights[0].respawns=true;a.reset(timing);check(!a.estimatedDuration(),"respawning lights keep a preview window");
        timing.lights[0].respawns=false;timing.lights[0].useTimeline=true;timing.lights[0].timelineSecs=.6f;
        a.reset(timing);check(!a.estimatedDuration(),"a stalled light timeline must not be treated as a finite end");
        a.seek(fixture(),.17);check(std::abs(a.position()-.17)<1e-10,"timeline position retains fractional tick on seek");
    }
    if(argc>1) {
        std::string error;
        const std::filesystem::path install=argv[1];
        check(effects::openBank(install,error),"retail effects bank opens");
        size_t randomStart=0,randomAxis=0,finiteEffects=0;
        std::string firstStart,firstAxis,singleAxis;
        for(const auto& name:effects::entryNames(install)) {
            const auto retail=effects::byName(install,name);
            if(!retail || !retail->parsedFully) continue;
            a.reset(*retail);
            if(const auto duration=a.estimatedDuration();duration && *duration>0 && *duration<4 && !retail->sprites.empty()) {
                if(finiteEffects<8) std::printf("retail short effect: %s %.6f s\n",name.c_str(),*duration);
                ++finiteEffects;
            }
            for(const auto& mesh:retail->meshes) {
                if(mesh.config.update.randomInitialRotation) {
                    ++randomStart;if(firstStart.empty()) firstStart=name;
                }
                if(mesh.config.update.randomRotationAxis) {
                    ++randomAxis;if(firstAxis.empty()) firstAxis=name;
                    if(singleAxis.empty() && retail->meshes.size()==1 &&
                       mesh.config.emitter.startCount>0 &&
                       mesh.config.emitter.startTime<=0 &&
                       (mesh.size[0]>1 || mesh.size[1]>1 || mesh.size[2]>1))
                        singleAxis=name;
                }
            }
        }
        std::printf("retail random mesh orientation: %zu initial (%s), %zu axis (%s)\n",
                    randomStart,firstStart.c_str(),randomAxis,firstAxis.c_str());
        std::printf("retail single-mesh random axis: %s\n",singleAxis.c_str());
        check(randomStart>0 && randomAxis>0,"retail random mesh orientation flags are represented");
    }
    std::printf("particle preview: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
