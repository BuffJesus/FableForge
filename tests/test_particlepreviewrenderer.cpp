#include "particlepreviewrenderer.hpp"
#include "particlepreview.hpp"
#include <forge/meshpreview.hpp>
#include "renderer.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <cstdlib>
#include <limits>
#include <vector>
using namespace albion;
namespace albion::gui {
void Camera::dir(float o[3])const{o[0]=-std::cos(pitch)*std::sin(yaw);o[1]=-std::sin(pitch);o[2]=-std::cos(pitch)*std::cos(yaw);}
void Camera::right(float o[3])const{o[0]=std::cos(yaw);o[1]=0;o[2]=-std::sin(yaw);}
void Camera::up(float o[3])const{float d[3],r[3];dir(d);right(r);o[0]=r[1]*d[2]-r[2]*d[1];o[1]=r[2]*d[0]-r[0]*d[2];o[2]=r[0]*d[1]-r[1]*d[0];if(o[1]<0)for(int k=0;k<3;++k)o[k]=-o[k];}
struct PreviewRender {
    ParticlePreviewRenderer& renderer;
    float background[3]={.025f,.035f,.05f};
    ID3D11ShaderResourceView* operator()(int width,int height,const Camera& camera,
        const std::vector<particlepreview::DrawSprite>& sprites) {
        return renderer.render(width,height,camera,sprites,background);
    }
    ID3D11ShaderResourceView* operator()(int width,int height,const Camera& camera,
        const std::vector<particlepreview::DrawSprite>& sprites,
        const std::vector<particlepreview::DrawMesh>& meshes) {
        return renderer.render(width,height,camera,sprites,meshes,background);
    }
};
}
#define CHECK(x) do{if(!(x)){std::cerr<<"line "<<__LINE__<<": " #x "\n";std::exit(1);}}while(false)
int main(){
 ID3D11Device* d=nullptr;ID3D11DeviceContext* c=nullptr;D3D_FEATURE_LEVEL level;D3D_FEATURE_LEVEL requested[]={D3D_FEATURE_LEVEL_10_0};
 CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,requested,1,D3D11_SDK_VERSION,&d,&level,&c)));
 {
 gui::ParticlePreviewRenderer r;CHECK(r.init(d,c));gui::Camera camera;camera.posZ=5;camera.yaw=camera.pitch=0;
 gui::PreviewRender render{r};
 gui::Camera gridCamera;gridCamera.posY=5;gridCamera.posZ=5;gridCamera.pitch=.55f;
 terrainexport::Image image;image.width=image.height=4;image.rgba.resize(64,0);for(int n=0;n<16;++n){image.rgba[n*4+(n<8?0:1)]=255;image.rgba[n*4+3]=255;}
 CHECK(r.setTexture(7,image,2));particlepreview::DrawSprite s;s.texture=7;s.size[0]=s.size[1]=2;s.blendMode=2;
 auto sample=[&](ID3D11ShaderResourceView* v,int x,int y){CHECK(v);ID3D11Resource* resource=nullptr;v->GetResource(&resource);ID3D11Texture2D* src=nullptr;CHECK(SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&src))));resource->Release();D3D11_TEXTURE2D_DESC td;src->GetDesc(&td);td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ID3D11Texture2D* staging=nullptr;CHECK(SUCCEEDED(d->CreateTexture2D(&td,nullptr,&staging)));c->CopyResource(staging,src);src->Release();D3D11_MAPPED_SUBRESOURCE map;CHECK(SUCCEEDED(c->Map(staging,0,D3D11_MAP_READ,0,&map)));auto p=static_cast<uint8_t*>(map.pData)+y*map.RowPitch+x*4;std::array<int,4> result{p[0],p[1],p[2],p[3]};c->Unmap(staging,0);staging->Release();return result;};
 auto snapshot=[&](ID3D11ShaderResourceView* v){CHECK(v);ID3D11Resource* resource=nullptr;v->GetResource(&resource);ID3D11Texture2D* src=nullptr;CHECK(SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&src))));resource->Release();D3D11_TEXTURE2D_DESC td;src->GetDesc(&td);td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ID3D11Texture2D* staging=nullptr;CHECK(SUCCEEDED(d->CreateTexture2D(&td,nullptr,&staging)));c->CopyResource(staging,src);src->Release();D3D11_MAPPED_SUBRESOURCE map;CHECK(SUCCEEDED(c->Map(staging,0,D3D11_MAP_READ,0,&map)));std::vector<uint8_t> result(size_t(td.Width)*td.Height*4);for(UINT y=0;y<td.Height;++y)std::memcpy(result.data()+size_t(y)*td.Width*4,static_cast<uint8_t*>(map.pData)+size_t(y)*map.RowPitch,size_t(td.Width)*4);c->Unmap(staging,0);staging->Release();return result;};
 auto changed=[](const std::vector<uint8_t>& a,const std::vector<uint8_t>& b){CHECK(a.size()==b.size());size_t pixels=0;for(size_t i=0;i<a.size();i+=4)if(std::abs(int(a[i])-int(b[i]))+std::abs(int(a[i+1])-int(b[i+1]))+std::abs(int(a[i+2])-int(b[i+2]))>40)++pixels;return pixels;};
 const auto gridOff=snapshot(render(128,128,gridCamera,{}));
 const auto gridOn=snapshot(r.render(128,128,gridCamera,{},render.background,true));
 CHECK(changed(gridOff,gridOn)>30);
 const float lightBackground[3]={.9f,.9f,.9f};
 const auto lightGridOff=snapshot(r.render(128,128,gridCamera,{},lightBackground));
 const auto lightGridOn=snapshot(r.render(128,128,gridCamera,{},lightBackground,true));
 CHECK(changed(lightGridOff,lightGridOn)>30);
 auto v=render(128,128,camera,{s});auto centre=sample(v,64,64);CHECK(centre[0]>250&&centre[1]<3&&centre[3]==255);CHECK(r.drawnSprites()==1);
 CHECK(sample(v,44,64)[0]>250);CHECK(sample(v,64,44)[0]<10); // authored frame is twice as wide as tall
 s.framePhase=1;centre=sample(render(128,128,camera,{s}),64,64);CHECK(centre[1]>250&&centre[0]<3);
 s.texture=999;centre=sample(render(128,128,camera,{s}),64,64);CHECK(r.missingTextures()==1&&r.drawnSprites()==0&&centre[0]<10);
 s.texture=-1;s.blendMode=3;s.colour[0]=.5f;s.colour[1]=s.colour[2]=0;s.colour[3]=.5f;
 centre=sample(render(128,128,camera,{s}),64,64);CHECK(centre[0]>=133&&centre[0]<=135&&centre[3]==255);
 s.blendMode=4;centre=sample(render(128,128,camera,{s}),64,64);CHECK(centre[0]>=130&&centre[0]<=132);
 // Additive RGB must not be attenuated by the already authored texture alpha.
 terrainexport::Image translucent;translucent.width=translucent.height=1;translucent.rgba={64,0,0,64};CHECK(r.setTexture(8,translucent));
 s.texture=8;s.blendMode=3;s.colour[0]=.5f;s.colour[3]=.25f;
 centre=sample(render(128,128,camera,{s}),64,64);CHECK(centre[0]>=21&&centre[0]<=23&&centre[3]==255);
 // Alpha blend still uses both sampled texture alpha and authored vertex alpha.
 s.blendMode=2;centre=sample(render(128,128,camera,{s}),64,64);CHECK(centre[0]>=9&&centre[0]<=11);
 // SOLID/BOOLEAN_ALPHA/ALPHA discard transparent and below-reference borders.
 translucent.rgba={64,0,0,15};CHECK(r.setTexture(9,translucent));s.texture=9;s.colour[0]=1;s.colour[3]=1;
 for(int mode:{0,1,2}){s.blendMode=mode;centre=sample(render(128,128,camera,{s}),64,64);CHECK(centre[0]==6&&centre[3]==255);}
 translucent.rgba[3]=16;CHECK(r.setTexture(9,translucent));s.blendMode=0;
 centre=sample(render(128,128,camera,{s}),64,64);CHECK(centre[0]>=127&&centre[0]<=129);
 // Additive authored RGB remains visible even with zero sampled texture alpha.
 translucent.rgba[3]=0;CHECK(r.setTexture(9,translucent));s.blendMode=3;
 centre=sample(render(128,128,camera,{s}),64,64);CHECK(centre[0]>=133&&centre[0]<=135&&centre[3]==255);
 s.size[0]=s.size[1]=.01f;std::vector<particlepreview::DrawSprite> many(16400,s);CHECK(render(32,32,camera,many));CHECK(r.drawnSprites()==16384&&r.droppedSprites()==16);
 // Asymmetric offset triangle: centring and each size axis are observable independently.
 forge::meshpreview::Geometry mesh;
 mesh.vertices={{1,0,-1,0,1,0,.2f,.2f},{4,0,-1,0,1,0,.2f,.2f},{1,0,1,0,1,0,.2f,.2f}};
 mesh.triangles={{0,1,2,-1}};CHECK(r.setMesh(20,mesh));CHECK(r.meshBytes()==72);
 CHECK(!r.meshUsesAuthoredBounds(20));CHECK(!r.meshUsesAuthoredBounds(999));
 CHECK(r.meshBoundsFactor(20,false)>2.3f);CHECK(r.meshBoundsFactor(20,true)==1);
 particlepreview::DrawMesh dm;dm.mesh=20;dm.centredOnPosition=true;
 dm.size[0]=dm.size[1]=dm.size[2]=std::sqrt(3.25f);dm.colour[0]=.5f;dm.colour[1]=dm.colour[2]=0;
 // Camera bounds use transformed geometry, not the largest scale as an isotropic sphere.
 {auto probe=dm;probe.size[1]*=100;float lo[3],hi[3];
 CHECK(r.meshFrameBounds(probe,lo,hi));CHECK(std::abs(lo[0]+1.5f)<1e-5f&&std::abs(hi[0]-1.5f)<1e-5f);
 CHECK(lo[1]==0&&hi[1]==0&&std::abs(lo[2]+1)<1e-5f&&std::abs(hi[2]-1)<1e-5f);
 probe.centredOnPosition=false;CHECK(r.meshFrameBounds(probe,lo,hi));CHECK(std::abs(lo[0]+4)<1e-5f&&std::abs(hi[0]+1)<1e-5f);
 probe.position[0]=10;probe.orientation[1]=probe.orientation[3]=std::sqrt(.5f);
 CHECK(r.meshFrameBounds(probe,lo,hi));CHECK(std::abs(lo[0]-9)<1e-5f&&std::abs(hi[0]-11)<1e-5f);
 CHECK(std::abs(lo[2]-1)<1e-5f&&std::abs(hi[2]-4)<1e-5f);
 probe.mesh=999;CHECK(!r.meshFrameBounds(probe,lo,hi));}
 auto meshImage=render(128,128,camera,{}, {dm});centre=sample(meshImage,70,70);
 CHECK(centre[0]>250&&centre[1]<3);CHECK(r.drawnMeshes()==1&&r.meshTriangles()==1);
 CHECK(sample(meshImage,85,70)[0]>250);
 dm.centredOnPosition=false;CHECK(sample(render(128,128,camera,{}, {dm}),70,70)[0]<10);
 dm.centredOnPosition=true;dm.size[0]*=.1f;CHECK(sample(render(128,128,camera,{}, {dm}),85,70)[0]<10);dm.size[0]*=10;
 dm.size[2]*=.1f;CHECK(sample(render(128,128,camera,{}, {dm}),70,75)[0]<10);dm.size[2]*=10;
 // Quaternion rotates the narrow axis into screen vertical (Fable Y-axis turn).
 dm.size[0]*=.1f;dm.orientation[1]=std::sqrt(.5f);dm.orientation[3]=std::sqrt(.5f);
 CHECK(sample(render(128,128,camera,{}, {dm}),75,66)[0]>250);
 dm.size[0]*=10;dm.orientation[1]=0;dm.orientation[3]=1;
 // Feed native-branch random quaternions through the actual WARP mesh pass.
 effects::Effect randomEffect;randomEffect.parsedFully=true;
 effects::MeshSystem randomMesh;randomMesh.mesh=20;randomMesh.systemIndex=0;
 auto& config=randomMesh.config;config.systemIndex=0;config.sprite=-1;
 config.emitter.present=true;config.emitter.startCount=1;config.lifeSecs=10;
 config.update.present=true;config.update.useParticleLife=true;
 config.update.randomInitialRotation=true;config.update.randomRotationAxis=true;
 config.update.rotationMinSpeed=config.update.rotationMaxSpeed=.25f;
 randomEffect.meshes.push_back(randomMesh);
 particlepreview::Simulation simulation;simulation.reset(randomEffect);
 CHECK(simulation.meshes().size()==1);
 const auto clearPixels=snapshot(render(128,128,camera,{},{}));
 const auto identityPixels=snapshot(render(128,128,camera,{}, {dm}));
 auto useOrientation=[&](const particlepreview::DrawMesh& source){std::copy(std::begin(source.orientation),
     std::end(source.orientation),std::begin(dm.orientation));};
 useOrientation(simulation.meshes()[0]);
 const auto randomPixels=snapshot(render(128,128,camera,{}, {dm}));
 CHECK(changed(clearPixels,randomPixels)>10 && changed(identityPixels,randomPixels)>10);
 simulation.step();useOrientation(simulation.meshes()[0]);
 const auto spunPixels=snapshot(render(128,128,camera,{}, {dm}));
 CHECK(changed(randomPixels,spunPixels)>10);
 simulation.reset(randomEffect);useOrientation(simulation.meshes()[0]);
 CHECK(snapshot(render(128,128,camera,{}, {dm}))==randomPixels);
 dm.orientation[0]=dm.orientation[1]=dm.orientation[2]=0;dm.orientation[3]=1;
 // Authored sphere takes precedence over bounds computed from vertex extrema.
 auto authored=mesh;forge::meshpreview::BoundingSphere sphere;
 sphere.centre[0]=2.5f;sphere.radius=2*std::sqrt(3.25f);authored.boundingSphere=sphere;
 CHECK(r.setMesh(20,authored));CHECK(r.meshUsesAuthoredBounds(20));
 CHECK(std::abs(r.meshBoundsFactor(20,false)-(1+2.5f/sphere.radius))<.00001f);
 meshImage=render(128,128,camera,{}, {dm});CHECK(sample(meshImage,85,70)[0]<10);CHECK(sample(meshImage,70,70)[0]>250);
 // A deliberately off-centre sphere changes the centring transform, not just camera framing.
 sphere.centre[0]=1;authored.boundingSphere=sphere;CHECK(r.setMesh(20,authored));
 meshImage=render(128,128,camera,{}, {dm});CHECK(sample(meshImage,70,70)[0]<10);CHECK(sample(meshImage,55,70)[0]>250);
 sphere.source=forge::meshpreview::BoundingSphere::Source::Descriptor;authored.boundingSphere=sphere;CHECK(r.setMesh(20,authored));CHECK(r.meshUsesAuthoredBounds(20));
 CHECK(sample(render(128,128,camera,{}, {dm}),55,70)[0]>250);
 // A caller cannot inject invalid authored bounds: retain the finite geometry fallback.
 sphere.radius=INFINITY;authored.boundingSphere=sphere;CHECK(r.setMesh(20,authored));CHECK(!r.meshUsesAuthoredBounds(20));
 CHECK(sample(render(128,128,camera,{}, {dm}),85,70)[0]>250);
 sphere.radius=std::numeric_limits<float>::denorm_min();authored.boundingSphere=sphere;CHECK(r.setMesh(20,authored));CHECK(!r.meshUsesAuthoredBounds(20));
 sphere.radius=1;sphere.centre[0]=std::numeric_limits<float>::max();authored.boundingSphere=sphere;CHECK(r.setMesh(20,authored));CHECK(!r.meshUsesAuthoredBounds(20));
 sphere.centre[0]=1;sphere.radius=std::numeric_limits<float>::max();authored.boundingSphere=sphere;CHECK(r.setMesh(20,authored));CHECK(!r.meshUsesAuthoredBounds(20));
 // Finite inputs whose composed transform overflows are rejected before issuing GPU work.
 auto hugeDraw=dm;hugeDraw.size[0]=std::numeric_limits<float>::max();
 render(32,32,camera,{}, {hugeDraw});CHECK(r.drawnMeshes()==0&&r.droppedMeshes()==1);
 CHECK(r.setMesh(20,mesh));CHECK(!r.meshUsesAuthoredBounds(20));
 // Material is a vector index, independent of its serialized material ID.
 forge::meshpreview::Material mat;mat.id=123;mat.diffuseTexture=8;mesh.materials={mat};mesh.triangles[0].material=0;CHECK(r.setMesh(21,mesh));dm.mesh=21;dm.blendMode=3;dm.colour[3]=.5f;
 centre=sample(render(128,128,camera,{}, {dm}),70,70);CHECK(centre[0]>=13&&centre[0]<=15); // sampled alpha contributes once
 // Mesh diffuse sharing an animated sprite atlas must never leak frame 1.
 mesh.materials[0].diffuseTexture=7;dm.blendMode=0;dm.colour[3]=1;
 for(float uv:{.00001f,.99999f}){for(auto& vertex:mesh.vertices)vertex.v=uv;CHECK(r.setMesh(21,mesh));
     centre=sample(render(128,128,camera,{}, {dm}),70,70);CHECK(centre[0]>250&&centre[1]<3);}
 dm.mesh=20;dm.blendMode=2;dm.colour[3]=1;
 particlepreview::DrawSprite front;front.size[0]=front.size[1]=4;front.position[1]=-1;front.colour[0]=front.colour[2]=0;front.colour[1]=.5f;
 centre=sample(render(128,128,camera,{front},{dm}),70,70);CHECK(centre[1]>250&&centre[0]<3);
 front.position[1]=1;centre=sample(render(128,128,camera,{front},{dm}),70,70);CHECK(centre[0]>250&&centre[1]<3);
 auto bad=mesh;bad.triangles[0].a=999;CHECK(!r.setMesh(20,bad));CHECK(r.meshBytes()==144); // failed replacement preserves resident
 bad=mesh;bad.vertices[0].x=INFINITY;CHECK(!r.setMesh(99,bad));
 dm.mesh=999;render(32,32,camera,{}, {dm});CHECK(r.missingMeshes()==1);
 dm.mesh=20;std::vector<particlepreview::DrawMesh> meshMany(4100,dm);render(16,16,camera,{},meshMany);CHECK(r.drawnMeshes()==4096&&r.droppedMeshes()==4);
 for(int id=100;id<162;++id)CHECK(r.setMesh(id,mesh));CHECK(!r.setMesh(999,mesh));
 r.clearMeshes();CHECK(r.meshBytes()==0);render(32,32,camera,{}, {dm});CHECK(r.missingMeshes()==1&&r.drawnMeshes()==0);
 r.clear();CHECK(render(64,64,camera,{}));CHECK(r.drawnSprites()==0);
 }
 c->ClearState();c->Release();d->Release();std::cout<<"WARP FL10 sprite and mesh rendering passed\n";
}
