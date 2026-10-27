// Rigged export round trip: retail skinned mesh -> rigged .glb -> the strict
// skinned loader -> the real composer -> decode, compared with the original.
#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include "skinnedexport.hpp"
#include "meshimport.hpp"
#include "forge/big.hpp"
#include "forge/meshpreview.hpp"

namespace fs=std::filesystem;
using albion::terrainexport::Context;
static void require(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}

int main(int argc,char** argv)try {
    // --glb <graphics.big> <rigged.glb> <donor-mesh-id>: an authored rig (e.g. creature-fit
    // output) through the strict loader, donor mapping and the real composer.
    if(argc==5&&std::string_view(argv[1])=="--glb") {
        const auto graphics=forge::big::File::open(argv[2]);
        const uint32_t donorId=uint32_t(std::stoul(argv[4]));
        const auto donor=forge::meshpreview::readLod0(argv[2],donorId);
        const auto model=albion::meshimport::loadSkinnedModel(argv[3]);
        const auto mapped=albion::meshimport::mapSkinToDonor(model,donor.bones);
        std::vector<forge::meshcompose::Material> materials(model.prims.size());
        const auto entry=albion::meshimport::prepareSkinnedMesh(model,graphics,donorId,"MESH_FIT_CHECK",materials,false);
        const auto composed=forge::meshpreview::decodeLods(entry.data,entry.subHeader,5).at(0);
        size_t triangles=0;for(const auto& p:model.prims)triangles+=p.faces.size();
        std::cout<<nlohmann::json{{"glb",argv[3]},{"donor",donorId},{"mapped_primitives",mapped.size()},{"input_triangles",triangles},
            {"composed_triangles",composed.triangles.size()},{"composed_bones",composed.bones.size()},{"donor_bones",donor.bones.size()}}.dump()<<'\n';
        return composed.triangles.size()==triangles&&composed.bones.size()==donor.bones.size()?0:1;
    }
    // --retail <graphics.big> <out-dir> <mesh-id>...
    require(argc>=5&&std::string_view(argv[1])=="--retail","usage: --retail <graphics.big> <out-dir> <mesh-id>... | --glb <graphics.big> <rigged.glb> <donor-mesh-id>");
    const fs::path bank=argv[2],out=argv[3];
    const auto graphics=forge::big::File::open(bank);
    const Context textures;
    auto report=nlohmann::json::array();bool passed=true;
    for(int a=4;a<argc;++a) {
        const uint32_t id=uint32_t(std::stoul(argv[a]));
        const auto original=forge::meshpreview::readLod0(bank,id);
        std::vector<albion::terrainexport::Image> images;std::map<uint32_t,int> ids;std::vector<std::string> warnings;
        const auto mesh=albion::foliageexport::makeMesh(id,"MESH_"+std::to_string(id),"",original,false,textures,images,ids,warnings);
        const auto path=out/("rigged_"+std::to_string(id)+".glb");
        albion::skinnedexport::write(mesh,images,path);
        const auto model=albion::meshimport::loadSkinnedModel(path);
        const auto mapped=albion::meshimport::mapSkinToDonor(model,original.bones);
        std::vector<forge::meshcompose::Material> materials;
        for(const auto& part:mesh.parts) {
            forge::meshcompose::Material material;
            if(part.material>=0)material.diffuseId=original.materials[size_t(part.material)].diffuseTexture;
            materials.push_back(material);
        }
        const auto entry=albion::meshimport::prepareSkinnedMesh(model,graphics,id,"MESH_ROUNDTRIP_"+std::to_string(id),materials,false);
        const auto composed=forge::meshpreview::decodeLods(entry.data,entry.subHeader,5).at(0);
        // Match composed vertices to originals by position (cm, 0.01 grid).
        std::multimap<std::tuple<long,long,long>,size_t> grid;
        const auto key=[](float x,float y,float z){return std::tuple<long,long,long>{std::lround(x*100),std::lround(y*100),std::lround(z*100)};};
        for(size_t i=0;i<mesh.geometry.vertices.size();++i){const auto& v=mesh.geometry.vertices[i];grid.emplace(key(v.x,v.y,v.z),i);}
        size_t matched=0,uvSame=0,uvFlipped=0,weightsClose=0;
        // Exported triangles (makeMesh may drop a collision hull) and the vertices they use.
        size_t exportedTriangles=0;std::vector<bool> used(mesh.geometry.vertices.size()),covered(used.size());
        for(const auto& part:mesh.parts){exportedTriangles+=part.indices.size()/3;for(const auto i:part.indices)used[i]=true;}
        for(const auto& v:composed.vertices) {
            const auto [first,last]=grid.equal_range(key(v.x,v.y,v.z));
            if(first==last)continue;
            ++matched;bool same=false,flipped=false,close=false;
            for(auto it=first;it!=last;++it) {
                covered[it->second]=true;
                const auto& o=mesh.geometry.vertices[it->second];
                // Equal modulo the packer's whole-tile offset (wrap-equivalent).
                const auto wraps=[](float d){return std::abs(d-std::round(d))<1e-3f;};
                same|=wraps(o.u-v.u)&&wraps(o.v-v.v);
                flipped|=wraps(o.u-v.u)&&wraps((1-o.v)-v.v);
                float error=0;
                for(size_t k=0;k<4;++k) {
                    float wo=0,wn=0;
                    for(size_t j=0;j<4;++j){if(o.joints[j]==o.joints[k])wo+=o.weights[j];if(v.joints[j]==o.joints[k])wn+=v.weights[j];}
                    error=std::max(error,std::abs(wo-wn));
                }
                close|=error<=0.02f;
            }
            uvSame+=same;uvFlipped+=flipped;weightsClose+=close;
        }
        const nlohmann::json row{{"mesh",id},{"bones",original.bones.size()},{"mapped_primitives",mapped.size()},
            {"original_vertices",mesh.geometry.vertices.size()},{"composed_vertices",composed.vertices.size()},
            {"original_triangles",original.triangles.size()},{"composed_triangles",composed.triangles.size()},
            {"position_matched",matched},{"uv_same",uvSame},{"uv_flipped",uvFlipped},{"weights_within_0.02",weightsClose},
            {"composed_bones",composed.bones.size()},{"exported_triangles",exportedTriangles}};
        size_t uncovered=0;
        for(size_t i=0;i<used.size();++i)uncovered+=used[i]&&!covered[i];
        report.push_back(row);report.back()["uncovered_vertices"]=uncovered;
        passed&=!composed.vertices.empty()&&composed.triangles.size()==exportedTriangles&&uncovered==0&&
                matched==composed.vertices.size()&&uvSame==matched&&weightsClose==matched&&composed.bones.size()==original.bones.size();
    }
    std::cout<<nlohmann::json{{"passed",passed},{"meshes",report}}.dump(1)<<'\n';
    return passed?0:1;
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
