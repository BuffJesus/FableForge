// Character fit: synthetic cases, plus a retail self-fit oracle where the donor's
// own parts, merged into one input mesh, must come back to their own parts with
// their own skin influences.
#include "forge/characterfit.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <nlohmann/json.hpp>

using namespace forge;
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}

static meshpreview::Geometry part(float z,uint16_t joint) {
    meshpreview::Geometry g;g.bones.resize(2);g.bones[0].name="ROOT";g.bones[1].name="B";g.boneCount=2;
    for(int i=0;i<4;++i){meshpreview::Vertex v;v.x=float(i%2);v.y=float(i/2);v.z=z;v.skinned=true;v.joints={joint,0,0,0};v.weights={1,0,0,0};g.vertices.push_back(v);}
    g.triangles.push_back({0,1,2,0});return g;
}

int main(int argc,char** argv)try {
    if(argc>=4&&std::string_view(argv[1])=="--retail") {
        // --retail <graphics.big> <part-mesh-id>...: the parts of one Creature look.
        std::vector<meshpreview::Geometry> donor;
        for(int a=3;a<argc;++a)donor.push_back(meshpreview::readLod0(argv[2],uint32_t(std::stoul(argv[a]))));
        std::vector<meshcompose::Primitive> input;std::vector<size_t> owner;
        for(size_t p=0;p<donor.size();++p) {
            meshcompose::Primitive prim;
            for(const auto& v:donor[p].vertices){prim.verts.push_back({v.x,v.y,v.z});prim.uvs.push_back({v.u,v.v});prim.normals.push_back({v.nx,v.ny,v.nz});}
            for(const auto& t:donor[p].triangles)prim.faces.push_back({t.a,t.b,t.c});
            input.push_back(std::move(prim));owner.push_back(p);
        }
        const auto result=characterfit::fit(input,donor);
        size_t own=0,total=0,exact=0,vertices=0;float worst=0;
        for(size_t p=0;p<result.parts.size();++p) {
            for(const auto& t:result.parts[p].geometry.triangles){++total;own+=owner[size_t(t.material)]==p;}
            for(const auto& v:result.parts[p].geometry.vertices) {
                // Compare with the donor vertex at the same position in this part.
                float best=1e30f;size_t at=0;
                for(size_t i=0;i<donor[p].vertices.size();++i){const auto& d=donor[p].vertices[i];
                    const float dist=std::hypot(d.x-v.x,d.y-v.y,d.z-v.z);if(dist<best){best=dist;at=i;}}
                if(best>1e-3f)continue;
                ++vertices;const auto& d=donor[p].vertices[at];float error=0;
                for(size_t k=0;k<4;++k){float a=0,b=0;for(size_t j=0;j<4;++j){if(d.joints[j]==d.joints[k])a+=d.weights[j];if(v.joints[j]==d.joints[k])b+=v.weights[j];}error=std::max(error,std::abs(a-b));}
                worst=std::max(worst,error);exact+=error<1e-4f;
            }
        }
        const nlohmann::json report{{"scale",result.scale},{"offset",result.offset},{"triangles",total},{"own_part",own},
            {"own_part_fraction",double(own)/double(total)},{"vertices_on_donor",vertices},{"weights_exact",exact},{"worst_weight_error",worst}};
        std::cout<<report.dump(1)<<'\n';
        return std::abs(result.scale-1)<1e-4f&&double(own)/double(total)>=0.99&&exact==vertices?0:1;
    }
    require(argc==1,"usage: [--retail <graphics.big> <part-mesh-id>...]");
    // Two parts stacked in Z with different joints; an input at twice the size is
    // aligned (scale 0.5), and each triangle lands on the part beside it.
    const std::vector<meshpreview::Geometry> donor{part(0,1),part(3,0)};
    meshcompose::Primitive prim;
    prim.verts={{0,0,0},{2,0,0},{0,2,0},{0,0,6},{2,0,6},{0,2,6}};
    prim.faces={{0,1,2},{3,4,5}};
    const auto result=characterfit::fit(std::span(&prim,1),donor);
    require(std::abs(result.scale-0.5f)<1e-6f,"height alignment differs");
    require(result.parts[0].geometry.triangles.size()==1&&result.parts[1].geometry.triangles.size()==1,"triangles were not split by nearest part");
    require(result.parts[0].geometry.vertices[0].joints[0]==1&&result.parts[1].geometry.vertices[0].joints[0]==0,"influences not taken from the owning part");
    require(result.parts[0].geometry.bones.size()==2&&result.parts[0].geometry.bones[1].name=="B","donor skeleton not kept");
    bool refused=false;try{characterfit::fit({},donor);}catch(const std::runtime_error&){refused=true;}
    require(refused,"empty input accepted");
    auto unskinned=donor;unskinned[0].vertices[0].skinned=false;refused=false;
    try{characterfit::fit(std::span(&prim,1),unskinned);}catch(const std::runtime_error&){refused=true;}
    require(refused,"unskinned donor accepted");
    std::cout<<"Character fit: PASS\n";return 0;
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
