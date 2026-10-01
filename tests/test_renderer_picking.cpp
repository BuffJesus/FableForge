// No window: WARP only supplies immutable uploaded geometry. Queries are CPU rays.
#include "renderer.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using albion::gui::Renderer;
namespace fe=albion::foliageexport;
namespace te=albion::terrainexport;
static void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
int main() {
    ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;
    const D3D_FEATURE_LEVEL requested[]={D3D_FEATURE_LEVEL_10_0};
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,requested,1,D3D11_SDK_VERSION,&device,nullptr,&context)))return 1;
    int result=0;
    try {
        Renderer renderer;check(renderer.init(device,context),"renderer initialization");
        fe::Scene scene;fe::Mesh mesh;
        mesh.geometry.vertices={{-2,-2,0},{2,-2,0},{0,2,0}};
        fe::SubMesh part;part.indices={0,1,2};mesh.parts.push_back(part);scene.meshes.push_back(mesh);
        auto add=[&](int owner,float z){fe::Instance i;i.mesh=0;i.thing=owner;i.x=10;i.y=20;i.z=z;i.hasMatrix=true;
            const float matrix[]={0,2,0,-3,0,0,0,0,.5f};std::copy(matrix,matrix+9,i.m);scene.instances.push_back(i);};
        add(33,3);add(10,8);add(10,7);add(22,6);add(-1,9);add(44,10);
        check(renderer.uploadThings(scene,te::UpAxis::Y),"upload support fixture");
        renderer.setInstanceVisible(5,false);
        const float origin[]={10,12,-20},down[]={0,-1,0};float t=0;
        check(renderer.pick(origin,down,t)==4 && std::abs(t-3)<1e-5f,"legacy picking must retain visible proxy behavior");
        check(renderer.pick(origin,down,t,{},true)==1 && std::abs(t-4)<1e-5f,"placed-only must exclude visual proxy");
        check(renderer.pick(origin,down,t,{10},true)==3 && std::abs(t-6)<1e-5f,"exclude selected owner and every child, select nearest transformed support");
        check(renderer.pick(origin,down,t,{10,22},true)==0 && std::abs(t-9)<1e-5f,"exclude entire multiselection");
        check(renderer.pick(origin,down,t,{10,22},false)==4,"proxy policy must remain optional");
        renderer.setInstanceVisible(3,false);
        check(renderer.pick(origin,down,t,{10},true)==0,"hidden support must not intercept ray");
        check(renderer.pick(origin,down,t,{10,33},true)==-1 && t==1e30f,"no hit resets distance");
        renderer.setInstanceVisible(3,true);
        const float moved[]={0,2,0,0,-3,0,0,0,0,0,.5f,0,10,20,5,1};renderer.setInstanceWorld(3,moved);
        check(renderer.pick(origin,down,t,{10},true)==3 && std::abs(t-7)<1e-5f,"updated instance transform participates in query");
        const float miss[]={100,12,-20};check(renderer.pick(miss,down,t,{},true)==-1,"ray outside transformed mesh");
        const float boxOnly[]={4.6f,12,-23.6f};check(renderer.pick(boxOnly,down,t,{},true)==-1,"AABB overlap alone is not a support surface");
        const float nonUnit[]={0,-2,0};check(renderer.pick(origin,nonUnit,t,{10},true)==3 && std::abs(t-3.5f)<1e-5f,"affine query retains unnormalized ray parameter");
        renderer.clearThings();check(renderer.pick(origin,down,t)==-1,"empty renderer query");
        std::cout<<"Renderer support picking passed\n";
    } catch(const std::exception& e){std::cerr<<e.what()<<"\n";result=1;}
    context->ClearState();context->Release();device->Release();return result;
}
