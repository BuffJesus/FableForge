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
            if(arg!="--install" && arg!="--out" && arg!="--seed" && arg!="--schema" && arg!="--neighbours") fail("unknown option "+arg);
            if(options.contains(arg)) fail("duplicate option "+arg);
            if(++i>=args.size() || args[i].empty() || args[i].starts_with("--")) fail("missing value for "+arg);
            options[arg]=args[i];
        } else positional.push_back(arg);
    }
    if(positional.size()!=2) fail("expected model.glb|.gltf|.obj and CREATURE");
    for(const char* option:{"--install","--out"}) if(!options.contains(option)) fail(std::string("required option ")+option);
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
    for(size_t i=0;i<fitted.parts.size();++i) {
        const auto& source=assembly.parts[i];const auto& part=fitted.parts[i];
        nlohmann::json row{{"body",source.body},{"alternative",source.alternative},{"donor_mesh",source.mesh},{"triangles",part.sourceTriangles}};
        if(part.geometry.triangles.empty()) {row["skipped"]="no input triangles nearest this part";report["parts"].push_back(row);continue;}
        std::vector<terrainexport::Image> images;std::map<uint32_t,int> ids;std::vector<std::string> warnings;
        const auto stem=positional[1]+"_body"+std::to_string(source.body)+"_alt"+std::to_string(source.alternative);
        const auto mesh=foliageexport::makeMesh(source.mesh,stem,stem,part.geometry,false,noTextures,images,ids,warnings);
        const auto path=output/(stem+".glb");
        skinnedexport::write(mesh,images,path);
        const auto written=path.u8string();
        row["file"]=std::string(written.begin(),written.end());
        report["parts"].push_back(row);
    }
    if(flags.contains("--json")) std::cout<<report.dump(1)<<'\n';
    else {
        for(const auto& row:report["parts"]) {
            if(row.contains("skipped")) std::cout<<"body "<<row["body"]<<" alt "<<row["alternative"]<<": skipped ("<<row["skipped"].get<std::string>()<<")\n";
            else std::cout<<"body "<<row["body"]<<" alt "<<row["alternative"]<<": "<<row["triangles"]<<" triangles -> "<<row["file"].get<std::string>()<<"\n";
        }
        std::cout<<"Each part carries its donor's skeleton; import it with creature-import --body/--alternative. Not yet seen in game.\n";
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
