#include "particlepreview.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace albion;
static int checks=0;
static void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
static bool close(float a,float b){return std::abs(a-b)<1e-4f;}
static effects::Effect fixture(effects::LightSystem light={}){effects::Effect e;e.parsedFully=true;e.lights.push_back(light);return e;}
static bool sameSprites(const std::vector<particlepreview::DrawSprite>& a,const std::vector<particlepreview::DrawSprite>& b){
    if(a.size()!=b.size())return false;
    for(size_t i=0;i<a.size();++i){for(int k=0;k<3;++k)if(a[i].position[k]!=b[i].position[k])return false;
        for(int k=0;k<4;++k)if(a[i].colour[k]!=b[i].colour[k])return false;
        if(a[i].angle!=b[i].angle||a[i].size[0]!=b[i].size[0]||a[i].framePhase!=b[i].framePhase)return false;}
    return true;
}
int main(){try{
    std::vector<uint8_t> data;auto u32=[&](uint32_t n){for(int k=0;k<4;++k)data.push_back(uint8_t(n>>(k*8)));};
    auto f32=[&](float f){uint32_t n;std::memcpy(&n,&f,4);u32(n);};auto str=[&](const char* text){do{data.push_back(uint8_t(*text));}while(*text++);};
    u32(100);str("Light retention");data.insert(data.end(),7+5*4+4+5,0);u32(1);str("lights");const auto systemEnabled=data.size();data.push_back(1);data.push_back(1);f32(2);f32(3);f32(4);u32(1);
    str("CPSCLight");u32(0);const auto componentEnabled=data.size();data.push_back(1);const auto body=data.size();
    u32(17);for(float value:{5.f,2.f,3.f,12.f,7.f,9.f})f32(value);u32(499);u32(100);u32(900);f32(.25f);u32(0x44332211);
    const uint8_t flags[]={1,1,0,1,1,0,1,0,1,1,1};data.insert(data.end(),std::begin(flags),std::end(flags));
    u32(0x88776655);u32(0xccbbaa99);u32(0xffeeddcc);check(data.size()-body==71,"light payload size");data.push_back(0x7b);data.push_back(0x26);
    auto decoded=effects::decode(data);check(decoded.parsedFully&&decoded.lights.size()==1,"light parser complete");
    const auto& l=decoded.lights[0];check(l.radius==7&&l.colour[0]==0x77&&l.colour[3]==0x88,"legacy export radius/colour retained");
    check(l.systemIndex==0&&l.positionParam==17&&l.scaleParticles&&l.systemScale[2]==4,"light identity and system scale");
    check(l.lifeSecs==5&&l.respawnDelaySecs==2&&l.startTime==3&&l.timelineSecs==12&&l.endRadius==9,"light timing and end radius retained");
    check(l.attenuationFactor==499&&l.fadeInEnd==100&&l.fadeOutBegin==900&&l.radiusMinimum==.25f&&l.colourMinimum[0]==0x33,"light fade fields retained");
    check(l.radiusFade&&l.useLife&&!l.enabled&&l.respawns&&l.colourFade&&!l.useStartColour&&l.useMidColour&&!l.useEndColour&&l.useFadeColour&&l.useTimeline&&l.hasInitializedTime,"all light flags retained");
    check(l.midColour[0]==0xbb&&l.endColour[0]==0xee&&decoded.previewUnsupported.empty(),"light colours and supported classification");
    data[componentEnabled]=0;check(effects::decode(data).lights.empty(),"disabled component excluded");data[componentEnabled]=1;data[systemEnabled]=0;check(effects::decode(data).lights.empty(),"disabled system excluded");
    data.pop_back();check(!effects::decode(data).parsedFully,"disabled truncated component still checked");
    particlepreview::Simulation a,b;effects::LightSystem light;light.radius=2;light.endRadius=4;light.attenuationFactor=499;
    a.reset(fixture(light));check(a.lightComponentCount()==1&&a.lights().size()==1&&a.particleCount()==0&&a.supportedSystems()==0,"light exists independently of emitter/particles");
    check(a.lights()[0].radius==2&&a.lights()[0].attenuationDistance==1,"native radius and attenuation distance");
    light.colour[3]=0;light.startTime=99;light.useTimeline=true;light.timelineSecs=0;a.reset(fixture(light));a.step();
    check(a.lights().size()==1&&a.lights()[0].colour[0]==1&&a.lights()[0].colour[3]==1,"unlimited life ignores timer/start and colour alpha");
    light.enabled=false;a.reset(fixture(light));check(a.lightComponentCount()==0&&a.lights().empty(),"internal light enabled flag");
    light={};light.radius=2;light.endRadius=4;light.useLife=true;light.lifeSecs=1;light.startTime=.1f;light.hasInitializedTime=true;
    a.reset(fixture(light));for(int k=0;k<3;++k)a.step();check(a.lights().empty(),"fresh runtime resets serialized initialized flag and delays light");a.step();check(a.lights().size()==1&&close(a.lights()[0].radius,2+2.f/30),"light starts after delay ticks");
    for(int k=0;k<29;++k)a.step();check(close(a.lights()[0].radius,4),"light radius reaches authored end");a.step();check(a.lights().size()==1,"native lifetime comparison includes final tick");a.step();check(a.lights().empty(),"light expires independently");
    light.startTime=0;light.lifeSecs=0;light.respawns=true;light.respawnDelaySecs=0;a.reset(fixture(light));a.step();check(a.lights().size()==1,"zero lifespan remains bounded one tick");a.step();check(a.lights().size()==1,"zero-delay respawn resets component timer");
    light.respawnDelaySecs=.1f;a.reset(fixture(light));a.step();for(int k=0;k<3;++k)a.step();check(a.lights().empty(),"respawn waits delay ticks");a.step();check(a.lights().empty(),"fractional delay uses native float threshold");a.step();check(a.lights().size()==1,"respawn after delay threshold");
    light={};light.useLife=true;light.lifeSecs=2;light.radius=2;light.endRadius=6;light.useStartColour=light.useMidColour=light.useEndColour=true;
    const uint8_t red[]={255,0,0,0},green[]={0,255,0,0},blue[]={0,0,255,0};std::copy(red,red+4,light.colour);std::copy(green,green+4,light.midColour);std::copy(blue,blue+4,light.endColour);
    a.reset(fixture(light));for(int k=0;k<15;++k)a.step();check(close(a.lights()[0].colour[0],.5)&&close(a.lights()[0].colour[1],.5)&&close(a.lights()[0].radius,3),"light uses linear colour/radius interpolation");
    for(int k=0;k<15;++k)a.step();check(a.lights()[0].colour[1]==1&&a.lights()[0].colour[0]==0,"light midpoint");
    for(int k=0;k<30;++k)a.step();check(a.lights()[0].colour[2]==1,"light endpoint");
    light.useStartColour=light.useEndColour=false;light.useLife=false;a.reset(fixture(light));check(a.lights()[0].colour[1]==1&&a.lights()[0].colour[0]==0,"missing endpoint uses enabled middle");
    light.useMidColour=false;a.reset(fixture(light));check(a.lights()[0].colour[0]==1&&a.lights()[0].colour[2]==1,"no colours uses white");
    light={};light.radius=light.endRadius=8;light.useLife=true;light.lifeSecs=2;light.radiusFade=light.colourFade=true;light.fadeInEnd=500;light.fadeOutBegin=500;light.radiusMinimum=.25f;
    light.colourMinimum[0]=51;light.colourMinimum[1]=0;light.colourMinimum[2]=0;a.reset(fixture(light));for(int k=0;k<15;++k)a.step();
    check(close(a.lights()[0].radius,5)&&close(a.lights()[0].colour[0],.6f)&&close(a.lights()[0].colour[1],.5f),"cosine radius and colour minimum fades");
    for(int k=0;k<30;++k)a.step();check(close(a.lights()[0].radius,5),"cosine fade out");
    light.useLife=false;light.radiusFade=light.colourFade=false;light.scaleParticles=true;light.systemScale[0]=2;light.systemScale[1]=4;light.systemScale[2]=3;a.reset(fixture(light));check(a.lights()[0].radius==32,"light dimension scaling uses largest system axis");
    light.scaleParticles=false;a.reset(fixture(light));check(a.lights()[0].radius==8,"scale particles disabled");
    light.useLife=true;light.useTimeline=true;light.timelineSecs=.5f;light.lifeSecs=2;light.radius=2;light.endRadius=6;a.reset(fixture(light));for(int k=0;k<100;++k)a.step();check(a.lights().size()==1&&close(a.lights()[0].radius,3),"timeline freezes component clock rather than killing light");
    auto frozenAtStart=light;frozenAtStart.timelineSecs=0;b.reset(fixture(frozenAtStart));for(int k=0;k<5;++k)b.step();check(b.lights().empty(),"timeline zero skips both alive assignment and increment at fresh start");
    const auto saved=a.lights()[0];a.reset(fixture(light));for(int k=0;k<400;++k)a.advance(1./120.);check(a.lights().size()==1&&a.lights()[0].radius==saved.radius,"reset and split-frame light determinism");
    light.radius=std::numeric_limits<float>::quiet_NaN();a.reset(fixture(light));check(a.lights().empty()&&!a.warnings().empty(),"nonfinite light omitted");
    light={};light.radius=1e30f;light.attenuationFactor=INT32_MAX;a.reset(fixture(light));check(a.lights()[0].radius==100000&&std::isfinite(a.lights()[0].attenuationDistance),"hostile light range bounded");
    auto many=fixture(light);many.lights.resize(100,light);a.reset(many);check(a.lightComponentCount()==64&&a.lights().size()==64,"light component cap");
    effects::Effect mixed;mixed.parsedFully=true;effects::SpriteSystem sprite;sprite.sprite=7;sprite.emitter.present=true;sprite.emitter.startCount=3;sprite.emitter.type=2;sprite.emitter.solid=true;sprite.emitter.size=2;sprite.perSecond=30;sprite.lifeSecs=1;
    mixed.sprites.push_back(sprite);auto withLights=mixed;withLights.lights.resize(3,light);a.reset(mixed);b.reset(withLights);for(int k=0;k<30;++k){a.step();b.step();}
    check(a.particleCount()==b.particleCount()&&sameSprites(a.sprites(),b.sprites()),"adding lights preserves population and sprite RNG stream");
    std::cout<<"light preview: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"light preview: "<<e.what()<<"\n";return 1;}}
