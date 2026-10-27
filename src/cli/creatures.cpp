#include "cli/common.hpp"
#include "creaturepublish.hpp"
#include "foliageexport.hpp"
#include "skinnedexport.hpp"
#include "terrainexport.hpp"
#include "forge/characterfit.hpp"
#include "forge/defschema.hpp"
#include "forge/meshpreview.hpp"
#include "nlohmann/json.hpp"
#include <charconv>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <set>

namespace albion::cli {
namespace fs = std::filesystem;
// Fit any humanoid model onto one Creature look's body parts and write one rigged
// .glb per part, carrying that part's own retail skeleton (forge::characterfit +
// skinnedexport). Each output re-imports with creature-import against its slot.
static int runCreatureFit(const Args& args) {
    const auto fail=[](const std::string& reason){throw std::runtime_error("creature-fit: "+reason);};
    std::map<std::string,std::string> options;std::set<std::string> flags;std::vector<std::string> positional;
    for(size_t i=1;i<args.size();++i) {
        const auto& arg=args[i];
        if(arg=="--no-align" || arg=="--json") {if(!flags.insert(arg).second) fail("duplicate option "+arg);}
        else if(arg.starts_with("--")) {
            if(arg!="--install" && arg!="--out" && arg!="--seed" && arg!="--schema" && arg!="--neighbours" &&
               arg!="--import" && arg!="--import-to") fail("unknown option "+arg);
            if(options.contains(arg)) fail("duplicate option "+arg);
            if(++i>=args.size() || args[i].empty() || args[i].starts_with("--")) fail("missing value for "+arg);
            options[arg]=args[i];
        } else positional.push_back(arg);
    }
    if(positional.size()!=2) fail("expected model.glb|.gltf|.obj and CREATURE");
    for(const char* option:{"--install","--out"}) if(!options.contains(option)) fail(std::string("required option ")+option);
    if(options.contains("--import")!=options.contains("--import-to")) fail("--import NAME and --import-to <root> go together");
    const std::string importName=options.contains("--import")?options.at("--import"):std::string();
    if(options.contains("--import") && (importName.size()>=60 || importName.empty() ||
       !std::all_of(importName.begin(),importName.end(),[](char c){return (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_';})))
        fail("--import NAME must be 1-59 uppercase letters, numbers or underscores");
    const auto number=[&](const std::string& key,uint64_t fallback,uint64_t maximum) {
        if(!options.contains(key)) return fallback;
        const auto& value=options.at(key);uint64_t parsed=0;
        const auto result=std::from_chars(value.data(),value.data()+value.size(),parsed);
        if(result.ec!=std::errc{} || result.ptr!=value.data()+value.size() || parsed>maximum) fail("invalid unsigned decimal "+key);
        return parsed;
    };
    const uint32_t seed=uint32_t(number("--seed",0,0xffffffffULL));
    forge::characterfit::Options fitOptions;
    fitOptions.neighbours=size_t(number("--neighbours",4,64));if(!fitOptions.neighbours) fail("--neighbours must be 1-64");
    fitOptions.align=!flags.contains("--no-align");
    const auto install=fs::absolute(fs::u8path(options.at("--install"))).lexically_normal();
    const auto output=fs::absolute(fs::u8path(options.at("--out"))).lexically_normal();
    auto graphics=install/"data/graphics/graphics.big";
    if(!fs::exists(graphics)) graphics=install/"data/graphics/pc/graphics.big";
    terrainexport::Context context;std::string error;
    if(!context.loadDefs(install,error)) fail(error);
    // The embedded schema lacks CCreatureDef; default to the repository/bundled copy.
    const auto schemaPath=options.contains("--schema")?fs::u8path(options.at("--schema")):fs::path("docs/re_reference/def_schema.json");
    if(!fs::is_regular_file(schemaPath)) fail("def schema not found; pass --schema docs/re_reference/def_schema.json");
    const auto schema=forge::defschema::Schema::load(schemaPath);
    const auto assembly=context.assembleCreature(positional[1],seed,graphics,&schema);
    std::vector<forge::meshpreview::Geometry> donor;
    for(const auto& part:assembly.parts) donor.push_back(forge::meshpreview::readLod0(graphics,part.mesh));
    const auto model=meshimport::loadModel(fs::u8path(positional[0]));
    const auto fitted=forge::characterfit::fit(model.prims,donor,fitOptions);
    fs::create_directories(output);
    nlohmann::json report{{"creature",positional[1]},{"seed",seed},{"scale",fitted.scale},{"offset",fitted.offset},{"parts",nlohmann::json::array()}};
    const terrainexport::Context noTextures;
    std::vector<int> slotOfMaterial;for(const auto& prim:model.prims)slotOfMaterial.push_back(prim.material);
    for(const auto& note:model.notes) std::cerr<<"note: "<<note<<'\n';
    for(size_t i=0;i<fitted.parts.size();++i) {
        const auto& source=assembly.parts[i];const auto& part=fitted.parts[i];
        nlohmann::json row{{"body",source.body},{"alternative",source.alternative},{"donor_mesh",source.mesh},{"triangles",part.sourceTriangles}};
        if(part.geometry.triangles.empty()) {row["skipped"]="no input triangles nearest this part";report["parts"].push_back(row);continue;}
        std::vector<terrainexport::Image> images;std::map<uint32_t,int> ids;std::vector<std::string> warnings;
        const auto stem=positional[1]+"_body"+std::to_string(source.body)+"_alt"+std::to_string(source.alternative);
        auto mesh=foliageexport::makeMesh(source.mesh,stem,stem,part.geometry,false,noTextures,images,ids,warnings);
        std::map<int,int> decoded;
        skinnedexport::attachInputTextures(mesh,images,slotOfMaterial,model.materialImages,decoded,warnings);
        for(const auto& warning:warnings) std::cerr<<"warning: "<<warning<<'\n';
        row["textures"]=images.size();
        const auto path=output/(stem+".glb");
        skinnedexport::write(mesh,images,path);
        const auto written=path.u8string();
        row["file"]=std::string(written.begin(),written.end());
        report["parts"].push_back(row);
    }
    if(!importName.empty()) {
        // One private creature, every fitted part in one staged transaction; each
        // part material's own texture is appended as <NAME>_B<b>A<a>_M<m>_DIFFUSE.
        const auto destination=fs::absolute(fs::u8path(options.at("--import-to"))).lexically_normal();
        const auto readPath=[&](const fs::path& relative){const auto staged=destination/relative;return fs::exists(staged)?staged:install/relative;};
        fs::path graphicsRelative="data/graphics/graphics.big";
        if(!fs::exists(destination/graphicsRelative) && fs::exists(destination/"data/graphics/pc/graphics.big")) graphicsRelative="data/graphics/pc/graphics.big";
        else if(!fs::exists(readPath(graphicsRelative))) graphicsRelative="data/graphics/pc/graphics.big";
        const creatureimport::Sources source{readPath("data/CompiledDefs/names.bin"),readPath("data/CompiledDefs/game.bin"),
                                            readPath(graphicsRelative),readPath("data/graphics/pc/textures.big")};
        const auto proof=creatureimport::captureSources(source);
        const auto donorIndex=context.definitionIndex(positional[1]);
        if(!donorIndex) fail("creature definition index not found");
        creatureimport::MeshRequest request;
        request.donorIndex=*donorIndex;request.rootName="CREATURE_"+importName;
        request.bodyName="FORGE_"+importName+"_BODY";request.appearanceName="FORGE_"+importName+"_APPEARANCE";request.compress=true;
        std::vector<creatureimport::PartRequest> parts;std::vector<creatureimport::TextureAppend> appends;
        std::vector<std::vector<std::optional<size_t>>> materialTexture;
        for(const auto& row:report["parts"]) {
            if(row.contains("skipped")) continue;
            const size_t body=row["body"],alternative=row["alternative"];
            const auto tag="B"+std::to_string(body)+"A"+std::to_string(alternative);
            creatureimport::PartRequest part{body,alternative,"MESH_"+importName+"_"+tag,
                meshimport::loadSkinnedModel(fs::u8path(row["file"].get<std::string>())),{}};
            std::vector<std::optional<size_t>> choices;
            for(size_t m=0;m<part.model.materialNames.size();++m) {
                forge::meshcompose::Material material;material.name=part.meshName+"_MAT"+std::to_string(m);
                std::optional<size_t> choice;
                if(m<part.model.materialImages.size() && !part.model.materialImages[m].empty()) {
                    // The rigged export embedded this texture as PNG; publish it from a file.
                    const auto png=output/(importName+"_"+tag+"_M"+std::to_string(m)+".png");
                    std::ofstream stream(png,std::ios::binary);
                    stream.write(reinterpret_cast<const char*>(part.model.materialImages[m].data()),std::streamsize(part.model.materialImages[m].size()));
                    stream.close();if(!stream) fail("cannot write texture "+png.string());
                    appends.push_back(creatureimport::captureTexture(png,importName+"_"+tag+"_M"+std::to_string(m)+"_DIFFUSE"));
                    choice=appends.size()-1;
                }
                part.materials.push_back(std::move(material));choices.push_back(choice);
            }
            parts.push_back(std::move(part));materialTexture.push_back(std::move(choices));
        }
        if(parts.empty()) fail("no fitted parts to import");
        const auto result=creatureimport::publishMeshes(source,proof,destination,graphicsRelative,schema,request,parts,appends,materialTexture);
        auto bindings=nlohmann::json::array();
        for(const auto& binding:result.bindings) bindings.push_back({{"donor_model_id",binding.oldModelId},{"mesh_id",binding.newModelId}});
        report["import"]={{"creature",request.rootName},{"root_index",result.clones.rootIndex},{"bindings",bindings},
                          {"texture_ids",result.textureIds},{"destination",destination.string()},{"runtime_compatibility_verified",false}};
    }
    if(flags.contains("--json")) std::cout<<report.dump(1)<<'\n';
    else {
        for(const auto& row:report["parts"]) {
            if(row.contains("skipped")) std::cout<<"body "<<row["body"]<<" alt "<<row["alternative"]<<": skipped ("<<row["skipped"].get<std::string>()<<")\n";
            else std::cout<<"body "<<row["body"]<<" alt "<<row["alternative"]<<": "<<row["triangles"]<<" triangles -> "<<row["file"].get<std::string>()<<"\n";
        }
        if(report.contains("import"))
            std::cout<<report["import"]["creature"].get<std::string>()<<" created at definition "<<report["import"]["root_index"]
                     <<" in "<<report["import"]["destination"].get<std::string>()<<" with "<<report["import"]["bindings"].size()<<" fitted parts.\n";
        std::cout<<"Each part carries its donor's skeleton. Not yet seen in game.\n";
    }
    return 0;
}

std::optional<int> runCreatures(const std::string& command,const Args& args) {
    if(command=="creature-fit") {
        if(args.size()==2 && args[1]=="--help") { usage(); return 0; }
        return runCreatureFit(args);
    }
    if(command!="creature-import") return {};
    if(args.size()==2 && args[1]=="--help") { usage(); return 0; }
    const auto fail=[](const std::string& reason){throw std::runtime_error("creature-import: "+reason);};
    std::map<std::string,std::string> options;std::set<std::string> flags;
    std::vector<std::string> positional;
    for(size_t i=1;i<args.size();++i) {
        const auto& arg=args[i];
        if(arg=="--stored" || arg=="--json") {
            if(!flags.insert(arg).second) fail("duplicate option "+arg);
        } else if(arg.starts_with("--")) {
            if(arg!="--install" && arg!="--out" && arg!="--schema" && arg!="--donor-index" && arg!="--body" &&
               arg!="--alternative" && arg!="--texture" && arg!="--texture-id" && arg!="--texture-format") fail("unknown option "+arg);
            if(options.contains(arg)) fail("duplicate option "+arg);
            if(++i>=args.size() || args[i].empty() || args[i].starts_with("--")) fail("missing value for "+arg);
            options.emplace(arg,args[i]);
        } else positional.push_back(arg);
    }
    if(positional.size()!=2) fail("expected model.glb|.gltf and NAME");
    for(const auto& option:{"--install","--out","--schema","--donor-index","--body","--alternative"})
        if(!options.contains(option)) fail(std::string("required option ")+option);
    if(options.contains("--texture") && options.contains("--texture-id")) fail("choose --texture or --texture-id");
    if(options.contains("--texture-format") && !options.contains("--texture")) fail("--texture-format requires --texture");
    const auto& name=positional[1];
    if(name.empty() || name.size()>=80 || !std::all_of(name.begin(),name.end(),[](char c){return (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_';}))
        fail("NAME must be 1-79 uppercase letters, numbers or underscores");
    const auto number=[&](const std::string& key,size_t maximum=std::numeric_limits<size_t>::max()) {
        const auto& value=options.at(key);size_t parsed=0;
        const auto result=std::from_chars(value.data(),value.data()+value.size(),parsed);
        if(result.ec!=std::errc{} || result.ptr!=value.data()+value.size() || parsed>maximum) fail("invalid unsigned decimal "+key);
        return parsed;
    };
    creatureimport::MeshRequest request;
    request.donorIndex=number("--donor-index");request.bodyPart=number("--body");request.alternative=number("--alternative");
    request.rootName="CREATURE_"+name;request.bodyName="FORGE_"+name+"_BODY";
    request.appearanceName="FORGE_"+name+"_APPEARANCE";request.meshName="MESH_"+name;request.compress=!flags.contains("--stored");
    const uint32_t existingTexture=options.contains("--texture-id")?uint32_t(number("--texture-id",UINT32_MAX)):0;
    const auto install=fs::absolute(fs::path(options.at("--install"))).lexically_normal();
    const auto output=fs::absolute(fs::path(options.at("--out"))).lexically_normal();
    const auto readPath=[&](const fs::path& relative){const auto staged=output/relative;return fs::exists(staged)?staged:install/relative;};
    fs::path graphicsRelative="data/graphics/graphics.big";
    if(!fs::exists(output/graphicsRelative) && fs::exists(output/"data/graphics/pc/graphics.big")) graphicsRelative="data/graphics/pc/graphics.big";
    else if(!fs::exists(readPath(graphicsRelative))) graphicsRelative="data/graphics/pc/graphics.big";
    const creatureimport::Sources source{readPath("data/CompiledDefs/names.bin"),readPath("data/CompiledDefs/game.bin"),
                                        readPath(graphicsRelative),readPath("data/graphics/pc/textures.big")};
    const auto proof=creatureimport::captureSources(source);
    const auto schema=forge::defschema::Schema::load(options.at("--schema"));
    const auto model=meshimport::loadSkinnedModel(positional[0]);
    std::vector<forge::meshcompose::Material> materials;
    for(size_t i=0;i<model.materialNames.size();++i) {
        forge::meshcompose::Material material;material.name=request.meshName+"_MAT"+std::to_string(i);
        material.diffuseId=existingTexture;materials.push_back(std::move(material));
    }
    std::optional<creatureimport::TextureAppend> texture;
    if(options.contains("--texture")) texture=creatureimport::captureTexture(options.at("--texture"),name+"_DIFFUSE",
        options.contains("--texture-format")?options.at("--texture-format"):"dxt1");
    const auto result=creatureimport::publishMesh(source,proof,output,graphicsRelative,schema,request,model,materials,true,{},texture?&*texture:nullptr);
    const nlohmann::json report{{"creature",request.rootName},{"root_index",result.clones.rootIndex},
        {"body_index",result.clones.bodyIndex},{"appearance_index",result.clones.appearanceIndex},
        {"body_part",request.bodyPart},{"alternative",request.alternative},{"donor_model_id",result.binding.oldModelId},
        {"mesh_id",result.binding.newModelId},{"texture_id",texture?result.textureId:existingTexture},
        {"notes",model.notes},
        {"other_body_slots_shared",true},{"animations_shared",true},{"runtime_compatibility_verified",false}};
    if(flags.contains("--json")) std::cout<<report.dump()<<'\n';
    else {
        std::cout<<request.rootName<<" created at definition "<<result.clones.rootIndex<<", mesh "<<result.binding.newModelId<<"\n";
        for(const auto& note:model.notes) std::cout<<"note: "<<note<<'\n';
        std::cout<<"Only the selected body part/alternative changed; other body slots and animations remain shared. Runtime compatibility is unverified.\n";
    }
    return 0;
}
} // namespace albion::cli
