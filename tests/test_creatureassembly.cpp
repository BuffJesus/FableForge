#include "forge/creatureassembly.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include "forge/bin.hpp"
#include "forge/defdecode.hpp"
#include "forge/defedit.hpp"
#include "forge/defschema.hpp"

using namespace forge;
namespace fs=std::filesystem;
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static std::string readText(const fs::path& path) {
    std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("cannot read "+path.string());
    std::ostringstream text;text<<in.rdbuf();return text.str();
}
// Write every part in one OBJ: parts share model space, as native draws them.
static void writeObj(const fs::path& path,const creatureassembly::Assembly& assembly) {
    std::ofstream out(path);size_t base=1;
    for(const auto& part:assembly.parts) {
        out<<"o part_"<<part.body<<"_"<<part.alternative<<"_mesh_"<<part.mesh<<"\n";
        for(const auto& v:part.posed.vertices)out<<"v "<<v.x<<' '<<v.y<<' '<<v.z<<"\n";
        for(const auto& t:part.posed.triangles)out<<"f "<<base+t.a<<' '<<base+t.b<<' '<<base+t.c<<"\n";
        base+=part.posed.vertices.size();
    }
}
int main(int argc,char** argv)try {
    // --retail <install> <schema> [seed] [obj-dir]: assemble every CCreatureDef read-only.
    // --roots <install> <schema> [seed]: per CREATURE root, whether it carries
    // CTCRandomAppearanceMorph and what assembling its visual graph does.
    const bool roots=argc>=4&&std::string_view(argv[1])=="--roots";
    if(argc>=4&&(roots||std::string_view(argv[1])=="--retail")) {
        const fs::path root=argv[2];const auto folder=root/"data"/"CompiledDefs";
        const auto schema=defschema::Schema::load(argv[3]);
        const uint32_t seed=argc>=5?uint32_t(std::stoul(argv[4],nullptr,0)):0x12345678u;
        const auto source=bin::File::open(folder/"names.bin",folder/"game.bin");
        std::map<uint32_t,std::string> names;uint64_t offset=4;
        for(const auto& [crc,name]:source.originalNames()){names.emplace(uint32_t(offset),name);offset+=name.size()+5;}
        std::map<uint32_t,meshpreview::Geometry> meshes;
        const creatureassembly::Sources sources{
            [&](uint32_t at){const auto found=names.find(at);if(found==names.end())throw std::runtime_error("skeletal name is not a pool boundary");return found->second;},
            [&](const std::string& name){return readText(root/"data"/"Bones"/name);},
            [&](uint32_t id){
                auto found=meshes.find(id);
                if(found==meshes.end())found=meshes.emplace(id,meshpreview::readLod0(root/"data"/"graphics"/"graphics.big",id)).first;
                return found->second;}};
        auto rows=nlohmann::json::array();std::map<std::string,size_t> outcomes;
        if(roots) {
            for(size_t i=0;i<source.entries().size();++i) {
                const auto& entry=source.entries()[i];if(entry.definition!="CREATURE")continue;
                std::string key;bool morph=false;
                try {
                    const auto decoded=defdecode::decode(entry,schema);
                    const auto field=std::find_if(decoded.fields.begin(),decoded.fields.end(),[](const auto& f){return f.name=="Components";});
                    if(!decoded.clean()||field==decoded.fields.end())throw std::runtime_error("runtime Components unavailable");
                    for(const auto at:defdecode::runtimeComponentNameOffsets(*field))
                        morph|=names.contains(at)&&names.at(at)=="CTCRandomAppearanceMorph";
                    const auto graph=defedit::resolveCreatureVisuals(source,schema,i);
                    const auto body=defdecode::decode(source.entries()[graph.bodyIndex],schema);
                    creatureassembly::assemble(body.prefix,seed,false,sources);
                    key="assembled";
                } catch(const std::exception& error){key=error.what();}
                ++outcomes[(morph?"morph | ":"no morph | ")+key];
            }
            std::cout<<nlohmann::json{{"seed",seed},{"outcomes",outcomes}}.dump(1)<<'\n';return 0;
        }
        for(size_t i=0;i<source.entries().size();++i) {
            const auto& entry=source.entries()[i];if(entry.definition!="CCreatureDef")continue;
            nlohmann::json row{{"index",i},{"name",entry.name}};
            try {
                const auto decoded=defdecode::decode(entry,schema);
                if(!decoded.clean()||defdecode::encode(decoded)!=entry.data)throw std::runtime_error("unclean component decode");
                const auto assembly=creatureassembly::assemble(decoded.prefix,seed,false,sources);
                auto parts=nlohmann::json::array();size_t vertices=0;
                for(const auto& part:assembly.parts) {
                    auto replacements=nlohmann::json::array(),channels=nlohmann::json::array();
                    for(const auto& r:part.textures)replacements.push_back({r.original,r.replacement});
                    for(const auto& m:part.posed.materials)
                        channels.push_back({m.diffuseTexture,m.bumpTexture,m.reflectionTexture,m.alphaMapTexture});
                    parts.push_back({{"mesh",part.mesh},{"bones",part.boneScales.size()},{"scaled_bones",part.scaledBones},
                                     {"vertices",part.posed.vertices.size()},{"textures",part.textures.size()},
                                     {"replacements",replacements},{"material_textures",channels}});
                    vertices+=part.posed.vertices.size();
                }
                row["parts"]=parts;row["targets"]=assembly.skeletalTargets;row["vertices"]=vertices;
                const auto merged=creatureassembly::merge(assembly);
                require(merged.vertices.size()==vertices,"merged vertex count differs");
                size_t materials=0,retextured=0,replacements=0;
                for(const auto& part:assembly.parts) {
                    materials+=part.posed.materials.size();replacements+=part.textures.size();
                    for(size_t m=0;m<part.posed.materials.size();++m) {
                        const auto& before=part.posed.materials[m];const auto& after=merged.materials[materials-part.posed.materials.size()+m];
                        retextured+=before.diffuseTexture!=after.diffuseTexture||before.bumpTexture!=after.bumpTexture;
                    }
                }
                require(merged.materials.size()==materials,"merged material count differs");
                for(const auto& t:merged.triangles)
                    require(t.a<merged.vertices.size()&&t.b<merged.vertices.size()&&t.c<merged.vertices.size()&&
                            t.material<int32_t(merged.materials.size()),"merged triangle out of range");
                row["materials"]=materials;row["retextured"]=retextured;row["replacements"]=replacements;
                ++outcomes["assembled"];
                if(argc>=6)writeObj(fs::path(argv[5])/("creature_"+std::to_string(i)+".obj"),assembly);
            } catch(const std::exception& error) {row["refused"]=error.what();++outcomes[error.what()];}
            rows.push_back(std::move(row));
        }
        std::cout<<nlohmann::json{{"seed",seed},{"outcomes",outcomes},{"records",rows}}.dump(1)<<'\n';return 0;
    }
    require(argc==1,"expected --retail <install> <schema> [seed] [obj-dir] or no arguments");
    const creatureassembly::Sources none{[](uint32_t)->std::string{throw std::runtime_error("unused");},
        [](const std::string&)->std::string{throw std::runtime_error("unused");},
        [](uint32_t)->meshpreview::Geometry{throw std::runtime_error("unused");}};
    bool refused=false;
    try{creatureassembly::assemble({}, 0, false, creatureassembly::Sources{});}catch(const std::runtime_error&){refused=true;}
    require(refused,"missing resolvers must refuse");refused=false;
    try{creatureassembly::assemble({}, 0, false, none);}catch(const std::runtime_error&){refused=true;}
    require(refused,"empty creature prefix must refuse");
    std::cout<<"Creature assembly: PASS\n";return 0;
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
