#include "forge/creaturepose.hpp"
#include <cstring>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <nlohmann/json.hpp>

using namespace forge::creaturepose;
static constexpr Matrix identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class F>static void refuses(F action){try{action();}catch(const std::runtime_error&){return;}throw std::runtime_error("invalid pose accepted");}
int main(int argc,char** argv)try {
    if(argc==4&&std::string_view(argv[1])=="--bind-mesh") {
        const auto id=std::stoull(argv[3]);require(id<=UINT32_MAX,"mesh ID overflow");
        const auto mesh=forge::meshpreview::readLod0(argv[2],uint32_t(id));
        const auto pose=bindPose(mesh);float deviation=0;
        for(const auto& matrix:pose.skin)for(size_t i=0;i<16;++i)
            deviation=std::max(deviation,std::abs(matrix[i]-identity[i]));
        std::cout<<nlohmann::json{{"mesh",id},{"bones",mesh.boneCount},{"vertices",mesh.vertices.size()},
            {"max_identity_deviation",deviation}}.dump()<<'\n';return 0;
    }
    if(argc==3&&std::string_view(argv[1])=="--oracle") {
        std::ifstream in(argv[2]);require(bool(in),"cannot read native pose receipt");
        const auto report=nlohmann::json::parse(in);size_t cases=0,skins=0;
        for(const auto& record:report.at("records")) {
            std::vector<Frame> frames;
            for(const auto& frame:record.at("frames"))frames.push_back({frame.at("rotation").get<std::array<float,4>>(),frame.at("position").get<std::array<float,3>>()});
            const auto parents=record.at("parents").get<std::vector<int32_t>>();
            const auto scales=record.at("scales").get<std::vector<std::array<float,3>>>();
            const auto binds=record.at("inverse_binds").get<std::vector<Matrix>>();
            const auto pose=evaluate(frames,parents,scales,binds);
            require(pose.global==record.at("final").get<std::vector<Matrix>>(),"native global pose differs");
            if(record.at("transposed").get<int>()) {
                require(pose.skin==record.at("skin").get<std::vector<Matrix>>(),"native inverse-bind composition differs");++skins;
            }
            ++cases;
        }
        std::cout<<nlohmann::json{{"cases",cases},{"skin_cases",skins}}.dump()<<'\n';return 0;
    }
    require(argc==1,"expected --oracle receipt or no arguments");
    std::vector<Frame> frames{{{0,0,0,1},{3,4,5}},{{0,0,0,1},{1,2,3}}};
    std::vector<int32_t> parents{-1,0};std::vector<Matrix> binds(2,identity);
    const std::vector<std::array<float,3>> scales{{2,3,4},{.5,.75,1.25}};
    const auto pose=evaluate(frames,parents,scales,binds);
    require(pose.global[0][3]==3&&pose.global[1][3]==5&&pose.global[1][7]==10&&pose.global[1][11]==17&&
            pose.global[1][0]==.5&&pose.global[1][5]==.75&&pose.global[1][10]==1.25&&pose.skin==pose.global,
            "parent displacement or independent geometry scale differs");
    auto invalidParents=parents;invalidParents[1]=1;refuses([&]{evaluate(frames,invalidParents,scales,binds);});
    refuses([&]{evaluate(frames,parents,std::span(scales).first(1),binds);});
    auto invalidBinds=binds;invalidBinds[0][12]=1;refuses([&]{evaluate(frames,parents,scales,invalidBinds);});
    auto invalidFrames=frames;invalidFrames[0].rotation[0]=std::numeric_limits<float>::quiet_NaN();
    refuses([&]{evaluate(invalidFrames,parents,scales,binds);});
    require(evaluate({}, {}, {}, {}).skin.empty(),"empty skeleton differs");
    forge::meshpreview::Geometry mesh;mesh.boneCount=2;mesh.bones.resize(2);mesh.skeletonBlocks.emplace();
    mesh.skeletonBlocks->keyframes.resize(96);
    for(size_t i=0;i<2;++i) {
        mesh.bones[i].parent=parents[i];mesh.bones[i].inverseBind=binds[i];
        const std::array<float,12> row{0,0,0,1,frames[i].position[0],frames[i].position[1],frames[i].position[2],0,7,8,9,0};
        std::memcpy(mesh.skeletonBlocks->keyframes.data()+48*i,row.data(),48);
    }
    const auto before=mesh.skeletonBlocks->keyframes;
    require(bindPose(mesh,scales).skin==pose.skin&&mesh.skeletonBlocks->keyframes==before,
            "raw bind frames must preserve input and ignore stored scaling");
    mesh.skeletonBlocks->keyframes.pop_back();refuses([&]{bindPose(mesh,scales);});
    std::cout<<"Creature pose: PASS\n";return 0;
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
