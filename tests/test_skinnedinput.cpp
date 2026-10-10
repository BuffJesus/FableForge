#include "meshimport.hpp"
#include "forge/meshpreview.hpp"
#include "forge/big.hpp"
#include "creatureimport.hpp"
#include "creaturepublish.hpp"
#include "forge/temporarydirectory.hpp"
#include "forge/texturewrite.hpp"
#include "nlohmann/json.hpp"
#include <bit>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using json=nlohmann::json;
using Bytes=std::vector<uint8_t>;
namespace fs=std::filesystem;
static void require(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
static void word(Bytes& b,uint32_t v) {for(unsigned i=0;i<4;++i) b.push_back(uint8_t(v>>(8*i)));}
static void real(Bytes& b,float v) {word(b,std::bit_cast<uint32_t>(v));}
static void setReal(Bytes& b,size_t at,float v) {const auto bits=std::bit_cast<uint32_t>(v);for(unsigned i=0;i<4;++i) b.at(at+i)=uint8_t(bits>>(8*i));}
static void chunk(Bytes& b,uint32_t type,Bytes data) {while(data.size()%4) data.push_back(type==0x4e4f534a?' ':0);word(b,uint32_t(data.size()));word(b,type);b.insert(b.end(),data.begin(),data.end());}
struct Fixture {json doc;Bytes data;};
static Fixture fixture(bool shortJoints=false,size_t weightType=5126) {
    Fixture f;for(float p:{0.f,0.f,0.f,1.f,0.f,0.f,0.f,1.f,0.f}) real(f.data,p);
    for(uint16_t j:{0,0,0,0,1,0,0,0,0,1,0,0}) {f.data.push_back(uint8_t(j));if(shortJoints) f.data.push_back(uint8_t(j>>8));}
    const size_t weightOffset=f.data.size();
    if(weightType==5126) for(float w:{1.f,0.f,0.f,0.f,1.f,0.f,0.f,0.f,.5f,.5f,0.f,0.f}) real(f.data,w);
    else if(weightType==5121) for(uint8_t w:{255,0,0,0,255,0,0,0,128,127,0,0}) f.data.push_back(w);
    else for(uint16_t w:{65535,0,0,0,65535,0,0,0,32768,32767,0,0}) {f.data.push_back(uint8_t(w));f.data.push_back(uint8_t(w>>8));}
    const size_t matrixOffset=f.data.size();
    for(size_t joint=0;joint<4;++joint) for(size_t k=0;k<16;++k) real(f.data,k==12 && joint==0?-1.f:k==0 || k==5 || k==10 || k==15?1.f:0.f);
    f.doc={{"asset",{{"version","2.0"}}},{"buffers",{{{"byteLength",f.data.size()}}}},
        {"bufferViews",{{{"buffer",0},{"byteOffset",0},{"byteLength",36}},
            {{"buffer",0},{"byteOffset",36},{"byteLength",weightOffset-36}},
            {{"buffer",0},{"byteOffset",weightOffset},{"byteLength",matrixOffset-weightOffset}},
            {{"buffer",0},{"byteOffset",matrixOffset},{"byteLength",256}}}},
        {"accessors",{{{"bufferView",0},{"componentType",5126},{"count",3},{"type","VEC3"},{"min",{0,0,0}},{"max",{1,1,0}}},
            {{"bufferView",1},{"componentType",shortJoints?5123:5121},{"count",3},{"type","VEC4"}},
            {{"bufferView",2},{"componentType",weightType},{"count",3},{"type","VEC4"},{"normalized",weightType!=5126}},
            {{"bufferView",3},{"componentType",5126},{"count",4},{"type","MAT4"}}}},
        {"meshes",{{{"primitives",{{{"attributes",{{"POSITION",0},{"JOINTS_0",1},{"WEIGHTS_0",2}}}}}}}}},
        {"nodes",{{{"mesh",0},{"skin",0},{"translation",{8,0,0}},{"scale",{-1,2,1}}},
            {{"name","ROOT"},{"children",{2,3,4}}},{{"name","CHILD"},{"translation",{1,0,0}}},{{"name","OTHER"}},{{"name","LAST"}}}},
        {"skins",{{{"joints",{2,1,3,4}},{"skeleton",1},{"inverseBindMatrices",3}}}},
        {"scenes",{{{"nodes",{0,1}}}}},{"scene",0}};
    return f;
}
static void save(const fs::path& path,const Fixture& f) {
    const auto text=f.doc.dump();Bytes bytes;word(bytes,0x46546c67);word(bytes,2);word(bytes,0);
    chunk(bytes,0x4e4f534a,Bytes(text.begin(),text.end()));chunk(bytes,0x004e4942,f.data);
    const auto size=uint32_t(bytes.size());for(unsigned i=0;i<4;++i) bytes[8+i]=uint8_t(size>>(8*i));
    std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));require(bool(out),"fixture write failed");
}
static int donorGate(char** argv) {
    uint32_t id=0;const std::string idText=argv[3];
    const auto parsed=std::from_chars(idText.data(),idText.data()+idText.size(),id);
    require(parsed.ec==std::errc{} && parsed.ptr==idText.data()+idText.size(),"invalid donor ID");
    const auto archive=forge::big::File::open(argv[2]);const auto* bank=archive.findBank("MBANK_ALLMESHES");
    require(bank,"missing donor bank");
    const auto entry=std::find_if(bank->entries.begin(),bank->entries.end(),[&](const auto& e){return e.id==id;});
    require(entry!=bank->entries.end() && entry->type==5,"missing type-5 donor");
    const auto donor=forge::meshpreview::decodeLod0(archive.entryData(*entry),5);
    require(bool(donor.skeletonBlocks),"missing donor raw skeleton");
    const auto model=albion::meshimport::loadSkinnedModel(argv[4]);
    // Small independent definition graph; the donor's real skeleton/payload
    // remains lazy/read-only, with no full archive serialization or copies.
    const std::string schemaText=R"({
        "CREATURE":{"fields":[{"name":"Components","type":"uint32"},{"name":"Self","type":"CDefIndex"},{"name":"Other","type":"int32"}]},
        "CCreatureDef":{"fields":[{"name":"Link","type":"CDefIndex"},{"name":"Other","type":"int32"}]},
        "CAppearanceDef":{"fields":[{"name":"Link","type":"CDefIndex"},{"name":"Other","type":"int32"}]}
    })";
    const auto schema=forge::defschema::Schema::loadText(schemaText);
    const auto field=[](Bytes& bytes,const char* name,uint32_t value){word(bytes,forge::defdecode::fieldTag(name));word(bytes,value);};
    const auto root=[&](bool donorRoot) {
        Bytes bytes{1,0,1,3,0};
        for(const auto& [name,index]:std::array<std::pair<const char*,uint32_t>,3>{{{"CCreatureDef",3},{"CAppearanceDef",4},{"CBrainDef",5}}}) {
            word(bytes,forge::defdecode::fieldTag(name));word(bytes,index);word(bytes,donorRoot?1:0);
        }
        field(bytes,"Components",0);field(bytes,"Self",donorRoot?1:0);field(bytes,"Other",99);return bytes;
    };
    Bytes body{1,0,1};field(body,"WoundMorphs",0);word(body,0x01ab6cee);
    field(body,"RandomAppearanceMorph",1);word(body,1);
    for(uint32_t value:{id,0u,0u,0u}) word(body,value);body.push_back(0);
    body.insert(body.end(),27,0);word(body,0);body.insert(body.end(),13,0);
    field(body,"Expressions",0);word(body,0x01ab6d32);field(body,"Stats",0);
    field(body,"Link",1);field(body,"Other",99);
    Bytes appearance{1,0,1};field(appearance,"Link",1);field(appearance,"Other",99);
    forge::TemporaryDirectory definitionFixture(fs::temp_directory_path(),"creature-mesh-plan-");
    const auto namesPath=definitionFixture.path()/"names.bin",bankPath=definitionFixture.path()/"game.bin";
    const auto write=[&](const fs::path& path,const Bytes& bytes) {
        std::ofstream stream(path,std::ios::binary);stream.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
        require(bool(stream),"definition plan fixture write failed");
    };
    Bytes emptyNames;for(uint32_t value:{0u,0xa8e36c34u,0u,4u,0u})word(emptyNames,value);
    Bytes emptyBank(9);for(uint32_t value:{0u,1u,0u,0u,0u})word(emptyBank,value);
    write(namesPath,emptyNames);write(bankPath,emptyBank);
    auto definitions=forge::bin::File::open(namesPath,bankPath);
    definitions.addEntry("CREATURE","ANCESTOR",root(false));definitions.addEntry("CREATURE","DONOR",root(true));
    definitions.addEntry("OTHER","OTHER",{});definitions.addEntry("CCreatureDef","",body);
    definitions.addEntry("CAppearanceDef","",appearance);definitions.addEntry("CBrainDef","SHARED_BRAIN",{5,7});
    definitions.save(namesPath,bankPath);definitions=forge::bin::File::open(namesPath,bankPath);
    forge::big::File tinyGraphics;tinyGraphics.setMagic("BIGB");tinyGraphics.setVersion(archive.version());
    auto tinyEntry=*entry;tinyEntry.data=archive.entryData(*entry);
    tinyGraphics.addBank("MBANK_ALLMESHES",bank->id).entries.push_back(std::move(tinyEntry));
    forge::big::File tinyTextures;tinyTextures.setMagic("BIGB");
    forge::texturewrite::Image blue;blue.width=blue.height=4;
    for(size_t pixel=0;pixel<16;++pixel) blue.rgba.insert(blue.rgba.end(),{0,0,255,255});
    const auto builtBlue=forge::texturewrite::buildEntry(blue,forge::texturewrite::kFormatARGB);
    forge::big::Entry blueEntry;blueEntry.id=7;blueEntry.name="EXISTING";blueEntry.magic=42;blueEntry.devCrc=123;
    blueEntry.devSources={"retained source"};blueEntry.data=builtBlue.payload;blueEntry.subHeader=builtBlue.info;blueEntry.length=uint32_t(blueEntry.data.size());
    tinyTextures.addBank("GBANK_MAIN_PC",9).entries.push_back(std::move(blueEntry));
    const auto graphicsPath=definitionFixture.path()/"graphics.big",texturesPath=definitionFixture.path()/"textures.big";
    write(graphicsPath,tinyGraphics.serialize());write(texturesPath,tinyTextures.serialize());
    const albion::creatureimport::Sources sources{namesPath,bankPath,graphicsPath,texturesPath};
    const auto proof=albion::creatureimport::captureSources(sources);
    albion::creatureimport::MeshRequest request{1,0,0,"CREATURE_IMPORTED","IMPORTED_BODY","IMPORTED_APPEARANCE","MESH_IMPORTED_"+idText,true};
    const auto output=fs::absolute(argv[5]);require(!fs::exists(output),"donor output must be fresh");
    forge::meshcompose::Material material;material.name="native donor import test";
    std::vector<forge::big::Entry> meshes;
    for(bool compressed:{false,true}) {
        request.compress=compressed;
        const auto plan=albion::creatureimport::prepareMesh(definitions,archive,schema,request,model,{material});
        const auto& mesh=plan.graphics.findBank("MBANK_ALLMESHES")->entries.back();
        require(plan.definitions.entries().size()==9 && plan.clones.rootIndex==6 && plan.clones.bodyIndex==7 && plan.clones.appearanceIndex==8,
                "private mesh graph append order changed");
        auto expectedDefinitions=definitions;
        forge::defedit::cloneCreatureVisuals(expectedDefinitions,schema,1,request.rootName,request.bodyName,request.appearanceName);
        auto expectedBody=expectedDefinitions.entries()[7].data;
        for(size_t byte=0;byte<4;++byte) expectedBody.at(plan.binding.offset+byte)=uint8_t(mesh.id>>(8*byte));
        expectedDefinitions.setEntryData(7,std::move(expectedBody));
        for(size_t i=0;i<9;++i) {
            const auto& expected=expectedDefinitions.entries()[i];const auto& actual=plan.definitions.entries()[i];
            require(expected.data==actual.data && expected.name==actual.name && expected.definition==actual.definition &&
                    expected.indexInDefinition==actual.indexInDefinition,"private mesh plan changed unrelated definition bytes");
        }
        require(plan.binding.oldModelId==id && plan.binding.newModelId==mesh.id && plan.binding.offset==27 &&
                plan.graphics.findBank("MBANK_ALLMESHES")->entries.size()==bank->entries.size()+1 && definitions.entries().size()==6,
                "private mesh plan donor selection or source preservation changed");
        uint32_t maximum=0;for(const auto& existing:bank->entries) maximum=std::max(maximum,existing.id);
        require(mesh.id==maximum+1 && mesh.type==5 && mesh.magic==42 && mesh.devFileType==entry->devFileType &&
                mesh.length==mesh.data.size() && mesh.name=="MESH_IMPORTED_"+idText,"prepared entry metadata invalid");
        const auto readback=forge::meshpreview::decodeLod0(mesh.data,5);
        require(readback.skeletonBlocks && readback.skeletonBlocks->indices==donor.skeletonBlocks->indices &&
            readback.skeletonBlocks->names==donor.skeletonBlocks->names && readback.skeletonBlocks->bones==donor.skeletonBlocks->bones &&
            readback.skeletonBlocks->keyframes==donor.skeletonBlocks->keyframes && readback.skeletonBlocks->inverseBinds==donor.skeletonBlocks->inverseBinds,
            "imported donor spans changed");
        meshes.push_back(mesh);
        const auto publishRoot=definitionFixture.path()/(compressed?"compressed-publish":"stored-publish");
        const auto published=albion::creatureimport::publishMesh(sources,proof,publishRoot,"data/graphics/graphics.big",schema,request,model,{material});
        require(published.definitions.entries().size()==9 && published.binding.oldModelId==id && published.binding.newModelId==id+1 &&
                published.graphics.findBank("MBANK_ALLMESHES")->entries.size()==2,"coordinated publication result differs");
        albion::creatureimport::requireSourceProof(sources,proof);
        for(const auto& path:std::array<fs::path,3>{publishRoot/"data/CompiledDefs/names.bin",publishRoot/"data/CompiledDefs/game.bin",publishRoot/"data/graphics/graphics.big"})
            require(fs::is_regular_file(fs::path(path.string()+albion::backups::kCreatedSuffix)),"coordinated publication missing creation marker");
        std::vector<std::string> restoreNotes;std::string restoreError;
        require(albion::backups::restoreAll(publishRoot,true,restoreNotes,restoreError)==3 && restoreError.empty(),"coordinated created outputs did not Restore");
        require(!fs::exists(publishRoot/"data/CompiledDefs/game.bin") && !fs::exists(publishRoot/"data/graphics/graphics.big"),"Restore kept created creature files");
    }
    const auto originalNames=albion::definitionwrite::cloneBytes(namesPath);
    const auto staleRoot=definitionFixture.path()/"stale-publish";
    bool staleRefused=false;
    try {albion::creatureimport::publishMesh(sources,proof,staleRoot,"data/graphics/graphics.big",schema,request,model,{material},true,[&]{
        std::ofstream stream(namesPath,std::ios::binary|std::ios::app);stream.put('x');stream.close();require(bool(stream),"stale fixture update failed");
    });}catch(const std::exception& e){staleRefused=std::string(e.what()).find("source changed")!=std::string::npos;}
    write(namesPath,originalNames);
    require(staleRefused && !fs::exists(staleRoot/"data/CompiledDefs/game.bin") && !fs::exists(staleRoot/"data/graphics/graphics.big"),"stale coordinated publication installed files");
    albion::creatureimport::requireSourceProof(sources,proof);
    const auto destinationStaleRoot=definitionFixture.path()/"destination-stale-publish";
    bool destinationRefused=false;
    try {albion::creatureimport::publishMesh(sources,proof,destinationStaleRoot,"data/graphics/graphics.big",schema,request,model,{material},true,[&]{
        fs::create_directories(destinationStaleRoot/"data/CompiledDefs");write(destinationStaleRoot/"data/CompiledDefs/game.bin",Bytes{7,8,9});
    });}catch(const std::exception& e){destinationRefused=std::string(e.what()).find("destination changed")!=std::string::npos;}
    require(destinationRefused && albion::definitionwrite::cloneBytes(destinationStaleRoot/"data/CompiledDefs/game.bin")==Bytes{7,8,9} &&
            !fs::exists(destinationStaleRoot/"data/graphics/graphics.big"),"stale destination overwritten during creature publication");
    auto missingTexture=material;missingTexture.diffuseId=99;bool textureRefused=false;
    const auto textureRefusalRoot=definitionFixture.path()/"missing-texture-publish";
    try {albion::creatureimport::publishMesh(sources,proof,textureRefusalRoot,"data/graphics/graphics.big",schema,request,model,{missingTexture});}
    catch(const std::exception& e){textureRefused=std::string(e.what()).find("missing or ambiguous material texture ID")!=std::string::npos;}
    require(textureRefused && !fs::exists(textureRefusalRoot),"invalid material texture published");
    const auto existingTree=[&](const fs::path& root) {
        fs::create_directories(root/"data/CompiledDefs");fs::create_directories(root/"data/graphics/pc");
        fs::copy_file(namesPath,root/"data/CompiledDefs/names.bin");fs::copy_file(bankPath,root/"data/CompiledDefs/game.bin");
        fs::copy_file(graphicsPath,root/"data/graphics/graphics.big");fs::copy_file(texturesPath,root/"data/graphics/pc/textures.big");
        return albion::creatureimport::Sources{root/"data/CompiledDefs/names.bin",root/"data/CompiledDefs/game.bin",root/"data/graphics/graphics.big",root/"data/graphics/pc/textures.big"};
    };
    const auto inPlaceRoot=definitionFixture.path()/"in-place";const auto inPlaceSources=existingTree(inPlaceRoot);
    std::cout<<"publication in-place and Restore: "<<id<<std::endl;
    const auto inPlaceProof=albion::creatureimport::captureSources(inPlaceSources);
    const auto inPlace=albion::creatureimport::publishMesh(inPlaceSources,inPlaceProof,inPlaceRoot,"data/graphics/graphics.big",schema,request,model,{material});
    require(inPlace.graphics.entryData(inPlace.graphics.findBank("MBANK_ALLMESHES")->entries.front())==archive.entryData(*entry),"in-place result retained stale lazy offsets");
    std::vector<std::string> restoreNotes;std::string restoreError;
    require(albion::backups::restoreAll(inPlaceRoot,true,restoreNotes,restoreError)==3 && restoreError.empty(),"in-place creature Restore failed");
    albion::creatureimport::requireSourceProof(inPlaceSources,inPlaceProof);
#ifdef _WIN32
    const auto lockedRoot=definitionFixture.path()/"locked-output";const auto lockedSources=existingTree(lockedRoot);
    std::cout<<"publication locked-third-file rollback: "<<id<<std::endl;
    const auto lockedProof=albion::creatureimport::captureSources(lockedSources);
    HANDLE locked=INVALID_HANDLE_VALUE;bool rollbackRefused=false;
    try {albion::creatureimport::publishMesh(sources,proof,lockedRoot,"data/graphics/graphics.big",schema,request,model,{material},true,[&]{
        locked=CreateFileW(lockedSources.graphics.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        require(locked!=INVALID_HANDLE_VALUE,"cannot lock third publication target");
    });}catch(const std::exception& e){rollbackRefused=std::string(e.what()).find("creature publication:")!=std::string::npos;}
    if(locked!=INVALID_HANDLE_VALUE) CloseHandle(locked);
    require(rollbackRefused,"locked third publication target accepted");
    albion::creatureimport::requireSourceProof(lockedSources,lockedProof);
#endif
    const auto imagePath=fs::path(argv[4]).parent_path()/"diffuse.png";
    std::cout<<"publication texture append, Restore and refusal: "<<id<<std::endl;
    for(const auto& format:{"dxt1","dxt3","argb8888"}) {
        const auto append=albion::creatureimport::captureTexture(imagePath,"IMPORTED_DIFFUSE",format);
        const auto textureRoot=definitionFixture.path()/(std::string("texture-")+format);
        const auto published=albion::creatureimport::publishMesh(sources,proof,textureRoot,"data/graphics/graphics.big",schema,request,model,{material},true,{},&append);
        require(published.textures && published.textureId==8 && published.textures->findBank("GBANK_MAIN_PC")->entries.size()==2,
                "texture append result differs");
        const auto& texture=published.textures->findBank("GBANK_MAIN_PC")->entries.back();
        require(forge::terraintex::validateTextureEntry(texture.subHeader,published.textures->entryData(texture)).ok,"appended texture validation failed");
        const auto textureMesh=forge::meshpreview::decodeLod0(published.graphics.entryData(published.graphics.findBank("MBANK_ALLMESHES")->entries.back()),5);
        require(textureMesh.materials.size()==1 && textureMesh.materials[0].diffuseTexture==8,"new diffuse ID not bound to composed mesh");
        // Retain tiny published files for independent Python archive/texture
        // decoding before Restore removes the actual publication tree.
        const auto retained=fs::path(argv[4]).parent_path()/(std::string("texture-output-")+format);
        require(fs::create_directory(retained),"texture evidence output must be fresh");
        fs::copy_file(textureRoot/"data/graphics/pc/textures.big",retained/"textures.big");
        fs::copy_file(textureRoot/"data/graphics/graphics.big",retained/"graphics.big");
        restoreNotes.clear();restoreError.clear();
        require(albion::backups::restoreAll(textureRoot,true,restoreNotes,restoreError)==4 && restoreError.empty(),"four-file texture publication Restore failed");
        require(!fs::exists(textureRoot/"data/graphics/pc/textures.big"),"Restore kept newly created textures bank");
        albion::creatureimport::requireSourceProof(sources,proof);
    }
    auto append=albion::creatureimport::captureTexture(imagePath,"IMPORTED_DIFFUSE","argb8888");
    const auto pngBytes=albion::definitionwrite::cloneBytes(imagePath);bool imageStaleRefused=false;
    const auto imageStaleRoot=definitionFixture.path()/"image-stale";
    try {albion::creatureimport::publishMesh(sources,proof,imageStaleRoot,"data/graphics/graphics.big",schema,request,model,{material},true,[&]{
        std::ofstream stream(imagePath,std::ios::binary|std::ios::app);stream.put('x');stream.close();require(bool(stream),"image stale update failed");
    },&append);}catch(const std::exception& e){imageStaleRefused=std::string(e.what()).find("texture source changed")!=std::string::npos;}
    write(imagePath,pngBytes);require(imageStaleRefused && !fs::exists(imageStaleRoot/"data/graphics/pc/textures.big"),"stale image published a texture bank");
    const auto textureInPlaceRoot=definitionFixture.path()/"texture-in-place";
    const auto textureInPlaceSources=existingTree(textureInPlaceRoot);const auto textureInPlaceProof=albion::creatureimport::captureSources(textureInPlaceSources);
    const auto textureInPlace=albion::creatureimport::publishMesh(textureInPlaceSources,textureInPlaceProof,textureInPlaceRoot,"data/graphics/graphics.big",schema,request,model,{material},true,{},&append);
    require(textureInPlace.textures && textureInPlace.textures->entryData(textureInPlace.textures->findBank("GBANK_MAIN_PC")->entries.front())==tinyTextures.entryData(tinyTextures.findBank("GBANK_MAIN_PC")->entries.front()),
            "in-place texture return lost unchanged payload");
    restoreNotes.clear();restoreError.clear();
    require(albion::backups::restoreAll(textureInPlaceRoot,true,restoreNotes,restoreError)==4 && restoreError.empty(),"in-place four-file Restore failed");
    albion::creatureimport::requireSourceProof(textureInPlaceSources,textureInPlaceProof);
#ifdef _WIN32
    std::cout<<"publication texture fourth-file rollback: "<<id<<std::endl;
    const auto textureLockedRoot=definitionFixture.path()/"texture-locked-output";const auto textureLockedSources=existingTree(textureLockedRoot);
    const auto textureLockedProof=albion::creatureimport::captureSources(textureLockedSources);
    locked=INVALID_HANDLE_VALUE;bool textureRollbackRefused=false;
    try {albion::creatureimport::publishMesh(sources,proof,textureLockedRoot,"data/graphics/graphics.big",schema,request,model,{material},true,[&]{
        locked=CreateFileW(textureLockedSources.graphics.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        require(locked!=INVALID_HANDLE_VALUE,"cannot lock fourth publication target");
    },&append);}catch(const std::exception& e){textureRollbackRefused=std::string(e.what()).find("creature publication:")!=std::string::npos;}
    if(locked!=INVALID_HANDLE_VALUE) CloseHandle(locked);
    require(textureRollbackRefused,"locked fourth target accepted during texture publication");
    albion::creatureimport::requireSourceProof(textureLockedSources,textureLockedProof);
#endif
    append=albion::creatureimport::captureTexture(imagePath,"EXISTING");bool duplicateTextureRefused=false;
    try {albion::creatureimport::publishMesh(sources,proof,definitionFixture.path()/"duplicate-texture","data/graphics/graphics.big",schema,request,model,{material},true,{},&append);}
    catch(const std::exception& e){duplicateTextureRefused=std::string(e.what()).find("new texture name already exists")!=std::string::npos;}
    require(duplicateTextureRefused,"existing texture name accepted");
    const auto planRefuses=[&](const albion::creatureimport::MeshRequest& invalid,const albion::meshimport::Model& authored,const char* reason) {
        bool rejected=false;
        try {albion::creatureimport::prepareMesh(definitions,archive,schema,invalid,authored,{material});}
        catch(const std::exception& e) {rejected=std::string(e.what()).find(reason)!=std::string::npos;}
        require(rejected && definitions.entries().size()==6 && archive.findBank("MBANK_ALLMESHES")->entries.size()==bank->entries.size(),
                "private mesh refusal did not preserve source graph/bank");
    };
    auto invalidRequest=request;invalidRequest.bodyPart=99;planRefuses(invalidRequest,model,"absent body part or alternative");
    invalidRequest=request;invalidRequest.rootName="DONOR";planRefuses(invalidRequest,model,"name");
    invalidRequest=request;invalidRequest.meshName=entry->name;planRefuses(invalidRequest,model,"new mesh name already exists");
    const auto refuses=[&](const forge::big::File& candidate,const std::string& name,const char* reason) {
        bool rejected=false;
        try {albion::meshimport::prepareSkinnedMesh(model,candidate,id,name,{material});}
        catch(const std::exception& e) {rejected=std::string(e.what()).find(reason)!=std::string::npos;}
        require(rejected,"preparation guard did not refuse with expected reason");
    };
    refuses(archive,entry->name,"new mesh name already exists");
    refuses(archive,"MESH_bad","invalid new mesh name");
    auto bad=archive;bad.setMagic(std::string("B\0\0\0",4));refuses(bad,"MESH_TEST","requires BIGB");
    bad=archive;bad.banks().push_back(*bank);refuses(bad,"MESH_TEST","ambiguous MBANK_ALLMESHES");
    bad=archive;bad.findBank("MBANK_ALLMESHES")->entries.push_back(*entry);refuses(bad,"MESH_TEST","ambiguous donor ID");
    bad=archive;forge::big::Entry exhausted;exhausted.id=UINT32_MAX;
    bad.findBank("MBANK_ALLMESHES")->entries.push_back(exhausted);refuses(bad,"MESH_TEST","no free mesh entry ID");
    require(fs::create_directories(output),"cannot create fresh donor output");
    for(size_t i=0;i<meshes.size();++i) {
        const auto stem=i?"compressed":"stored";
        for(const auto& [suffix,bytes]:std::array<std::pair<std::string,const Bytes*>,2>{{{".mesh",&meshes[i].data},{".info",&meshes[i].subHeader}}}) {
            std::ofstream stream(output/(std::string(stem)+suffix),std::ios::binary);
            stream.write(reinterpret_cast<const char*>(bytes->data()),std::streamsize(bytes->size()));require(bool(stream),"donor output write failed");
        }
    }
    // Retain only these tiny fixture banks for the separate public CLI gate.
    // Never copy the original full native graphics/texture archives.
    const auto fixtureRoot=fs::path(argv[4]).parent_path()/"source-install";
    require(!fs::exists(fixtureRoot),"CLI fixture destination must be fresh");
    for(const auto& [from,relative]:std::array<std::pair<fs::path,fs::path>,4>{{
        {namesPath,"data/CompiledDefs/names.bin"},{bankPath,"data/CompiledDefs/game.bin"},
        {graphicsPath,"data/graphics/graphics.big"},{texturesPath,"data/graphics/pc/textures.big"}}}) {
        fs::create_directories((fixtureRoot/relative).parent_path());
        require(fs::copy_file(from,fixtureRoot/relative),"CLI fixture copy failed");
    }
    write(fs::path(argv[4]).parent_path()/"schema.json",Bytes(schemaText.begin(),schemaText.end()));
    std::cout<<"authored glTF -> donor remap -> stored/compressed readback PASS: "<<id<<'\n';return 0;
}
int main(int argc,char** argv) {
    if(argc!=1) {
        try {require(argc==6 && std::string(argv[1])=="--donor","expected --donor graphics.big ID source.gltf fresh-output-directory");return donorGate(argv);}
        catch(const std::exception& e) {std::cerr<<"donor input: "<<e.what()<<'\n';return 1;}
    }
    const auto dir=fs::temp_directory_path()/("FableForgeSkinInput-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        require(fs::create_directory(dir),"fixture directory already exists");
        require(fs::canonical(dir).parent_path()==fs::canonical(fs::temp_directory_path()),"fixture escaped temporary root");
        const auto path=dir/"skin.glb";size_t checks=0;
        // Independent point oracle: converting after applying M must agree
        // with applying the converted matrix to the converted point. Use an
        // asymmetric linear matrix to expose transpose/sign/axis mistakes.
        const std::array<float,16> source{2,3,5,0,7,11,13,0,17,19,23,0,29,31,37,1};
        const auto converted=albion::meshimport::inverseBindInFable(source);
        for(const std::array<double,3> point:std::array<std::array<double,3>,4>{{{0,0,0},{1,0,0},{0,1,0},{2,-3,4}}}) {
            std::array<double,3> transformed{};
            for(size_t row=0;row<3;++row) {
                transformed[row]=source[12+row];
                for(size_t column=0;column<3;++column) transformed[row]+=source[column*4+row]*point[column];
            }
            const std::array<double,3> fablePoint{100*point[0],-100*point[2],100*point[1]};
            const std::array<double,3> expected{100*transformed[0],-100*transformed[2],100*transformed[1]};
            for(size_t row=0;row<3;++row) {
                double actual=converted[row*4+3];
                for(size_t column=0;column<3;++column) actual+=converted[row*4+column]*fablePoint[column];
                require(actual==expected[row],"inverse bind coordinate conjugation mismatch");
            }
            ++checks;
        }
        for(size_t bad:{3u,7u,11u,15u,0u}) {
            auto invalid=source;invalid[bad]=bad==0?std::nanf(""):2.f;
            bool rejected=false;try {albion::meshimport::inverseBindInFable(invalid);}catch(const std::exception&) {rejected=true;}
            require(rejected,"invalid inverse bind conversion accepted");++checks;
        }
        const auto load=[&](const Fixture& f){save(path,f);return albion::meshimport::loadSkinnedModel(path);};
        const auto refuses=[&](const Fixture& f,const char* label) {bool failed=false;try {load(f);}catch(const std::exception&) {failed=true;}require(failed,label);++checks;};
        const auto authored=load(fixture());
        std::vector<forge::meshpreview::Bone> donor(2);
        donor[0].name="ROOT";donor[0].parent=-1;donor[1].name="CHILD";donor[1].parent=0;
        for(auto& bone:donor) bone.inverseBind={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        donor[1].inverseBind[3]=-100;
        const auto mapped=albion::meshimport::mapSkinToDonor(authored,donor);
        require(mapped.size()==1 && mapped[0].skin[0][0].bone==1 && mapped[0].skin[1][0].bone==0 &&
                mapped[0].skin[2][0].weight==.5 && mapped[0].geometry.verts[1].x==100,
                "donor remap changed geometry, weights or used source order");++checks;
        require(authored.skins[0].joints[0].parentJoint==1 && !authored.skins[0].joints[1].parentJoint,
                "nearest joint hierarchy not retained");++checks;
        const auto mappingRefuses=[&](const albion::meshimport::Model& input,const std::vector<forge::meshpreview::Bone>& bones,const char* label) {
            bool rejected=false;try {albion::meshimport::mapSkinToDonor(input,bones);}catch(const std::exception&) {rejected=true;}
            require(rejected,label);++checks;
        };
        auto badDonor=donor;badDonor[1].name="child";mappingRefuses(authored,badDonor,"case-folded joint accepted");
        badDonor=donor;badDonor.push_back(donor[1]);mappingRefuses(authored,badDonor,"ambiguous donor joint accepted");
        badDonor=donor;badDonor[0].parent=1;mappingRefuses(authored,badDonor,"cyclic donor accepted");
        badDonor=donor;badDonor[1].parent=2;mappingRefuses(authored,badDonor,"invalid donor parent accepted");
        badDonor=donor;badDonor[1].inverseBind[3]=-99;mappingRefuses(authored,badDonor,"wrong donor bind accepted");
        badDonor=donor;badDonor[1].inverseBind[0]=std::nanf("");mappingRefuses(authored,badDonor,"nonfinite donor bind accepted");
        badDonor=donor;badDonor[1].inverseBind[15]=0;mappingRefuses(authored,badDonor,"projective donor bind accepted");
        auto badSource=authored;badSource.skins[0].joints[2].name="CHILD";mappingRefuses(badSource,donor,"ambiguous source joint accepted");
        badSource=authored;badSource.skins[0].joints[0].parentJoint.reset();mappingRefuses(badSource,donor,"missing parent chain accepted");
        badSource=authored;badSource.skins[0].joints[0].parentJoint=2;mappingRefuses(badSource,donor,"wrong parent chain accepted");
        badSource=authored;badSource.skins[0].influences[0].clear();mappingRefuses(badSource,donor,"empty source weights silently root-bound");
        badSource=authored;badSource.skins[0].influences[0][0].bone=4;mappingRefuses(badSource,donor,"invalid source joint accepted");
        badSource=authored;badSource.skins[0].influences[0].push_back({0,.5});mappingRefuses(badSource,donor,"duplicate source influence accepted");
        // Non-joint scene nodes may sit between joints without changing the
        // effective joint hierarchy. Unused unmatched source joints are retained.
        auto throughNode=fixture();throughNode.doc["nodes"].push_back({{"children",{2}}});
        throughNode.doc["nodes"][1]["children"]={5,3,4};
        const auto throughModel=load(throughNode);
        require(throughModel.skins[0].joints[0].parentNode==5 && throughModel.skins[0].joints[0].parentJoint==1 &&
                albion::meshimport::mapSkinToDonor(throughModel,donor)[0].skin[0][0].bone==1,
                "non-joint ancestor broke donor mapping");++checks;
        auto posed=authored;posed.skins[0].joints[0].world[12]=900;
        require(albion::meshimport::mapSkinToDonor(posed,donor)[0].geometry.verts[1].x==100,"default pose mistaken for bind pose");++checks;
        auto childOnly=authored;for(auto& weights:childOnly.skins[0].influences) weights={{0,1}};
        auto partial=std::vector<forge::meshpreview::Bone>{donor[1]};partial[0].parent=-1;
        require(albion::meshimport::mapSkinToDonor(childOnly,partial)[0].skin[0][0].bone==0,"partial donor root rejected");++checks;
        badDonor=donor;badDonor[0].inverseBind[3]=10;mappingRefuses(childOnly,badDonor,"unweighted required ancestor bind not checked");
        badDonor=donor;badDonor[1].inverseBind[3]-=.0001f;
        require(albion::meshimport::mapSkinToDonor(authored,badDonor)[0].skin[0][0].bone==1,"float bind jitter rejected");++checks;
        for(bool shorts:{false,true}) for(size_t weights:{5126u,5121u,5123u}) {
            const auto m=load(fixture(shorts,weights));require(m.prims.size()==1 && m.skins.size()==1,"skin/primitive alignment");
            const auto& p=m.prims[0];const auto& s=m.skins[0];
            require(p.verts[1].x==100 && p.verts[2].z==100 && p.faces[0]==std::array<uint32_t,3>{0,1,2},"skinned node pose baked or winding flipped");
            require(s.sourceSkin==0 && s.meshNode==0 && s.skeletonRoot==1 && s.joints.size()==4,"skin identity lost");
            require(s.joints[0].name=="CHILD" && s.joints[0].sourceNode==2 && s.joints[0].parentNode==1 && s.joints[0].world[12]==1 && s.joints[0].inverseBind[12]==-1,"joint order/world/bind changed");
            require(s.influences[0].size()==1 && s.influences[0][0].bone==0 && s.influences[1][0].bone==1 && s.influences[2].size()==2,"joint ordinal/weights lost");
            require(std::abs(s.influences[2][0].weight+s.influences[2][1].weight-1)<1e-6,"normalized weights changed");++checks;
        }
        auto f=fixture();f.doc["skins"][0].erase("inverseBindMatrices");const auto identity=load(f);require(identity.skins[0].joints[0].inverseBind[12]==0 && identity.skins[0].joints[0].inverseBind[15]==1,"omitted inverse bind not identity");++checks;
        f=fixture();f.doc["skins"][0]["joints"]={2,2,3,4};refuses(f,"duplicate skin joint accepted");
        f=fixture();f.doc["skins"][0]["joints"][0]=99;refuses(f,"missing skin joint accepted");
        f=fixture();f.doc["scenes"][0]["nodes"]={0};refuses(f,"out-of-scene joints accepted");
        f=fixture();f.doc["nodes"][1].erase("children");f.doc["scenes"][0]["nodes"]={0,1,2,3,4};refuses(f,"disconnected joint roots accepted");
        f=fixture();f.doc["skins"][0]["skeleton"]=2;refuses(f,"non-common skeleton root accepted");
        f=fixture();f.doc["accessors"][3]["count"]=3;refuses(f,"short inverse bind accessor accepted");
        f=fixture();f.doc["accessors"][3]["type"]="VEC4";refuses(f,"wrong inverse bind shape accepted");
        f=fixture();setReal(f.data,96+12,1);refuses(f,"projective inverse bind accepted");
        f=fixture();f.data[36]=4;refuses(f,"joint outside skin.joints accepted");
        f=fixture();setReal(f.data,48,-1);refuses(f,"negative skin weight accepted");
        f=fixture();setReal(f.data,48,.5f);refuses(f,"weight sum error accepted");
        f=fixture();setReal(f.data,48,std::nanf(""));refuses(f,"nonfinite skin weight accepted");
        f=fixture();setReal(f.data,48,.5f);setReal(f.data,52,.5f);refuses(f,"duplicate positive joint influence accepted");
        f=fixture();f.doc["accessors"][1]["normalized"]=true;refuses(f,"normalized joints accepted");
        f=fixture();f.doc["accessors"][2]["count"]=2;refuses(f,"short weight accessor accepted");
        f=fixture();f.doc["meshes"][0]["primitives"][0]["attributes"].erase("WEIGHTS_0");refuses(f,"unpaired skin attributes accepted");
        f=fixture();f.doc["nodes"][0].erase("skin");refuses(f,"unskinned selected mesh accepted");
        f=fixture();f.doc.erase("scenes");refuses(f,"uninstantiated skin accepted");
        f=fixture();save(path,f);bool staticRefused=false;try {albion::meshimport::loadModel(path);}catch(const std::exception&) {staticRefused=true;}require(staticRefused,"static path silently stripped skin");++checks;
        f=fixture();f.doc["nodes"][0].erase("skin");save(path,f);require(albion::meshimport::loadModel(path).skins.empty(),"static path gained skin metadata");++checks;
        f=fixture();setReal(f.data,48,.25f);setReal(f.data,52,.25f);f.data[37]=1;
        const size_t jointOffset=f.data.size();f.data.insert(f.data.end(),{2,3,0,0,0,0,0,0,0,0,0,0});
        const size_t weightOffset=f.data.size();for(float v:{.25f,.25f,0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f}) real(f.data,v);
        f.doc["bufferViews"].push_back({{"buffer",0},{"byteOffset",jointOffset},{"byteLength",12}});
        f.doc["bufferViews"].push_back({{"buffer",0},{"byteOffset",weightOffset},{"byteLength",48}});
        f.doc["accessors"].push_back({{"bufferView",4},{"componentType",5121},{"count",3},{"type","VEC4"}});
        f.doc["accessors"].push_back({{"bufferView",5},{"componentType",5126},{"count",3},{"type","VEC4"}});
        f.doc["buffers"][0]["byteLength"]=f.data.size();
        auto& a=f.doc["meshes"][0]["primitives"][0]["attributes"];a["JOINTS_1"]=4;a["WEIGHTS_1"]=5;
        const auto multi=load(f);require(multi.skins[0].influences[0].size()==4 && !multi.notes.empty(),"higher weight set silently dropped");++checks;
        auto gap=f;auto& gapAttrs=gap.doc["meshes"][0]["primitives"][0]["attributes"];
        gapAttrs["JOINTS_2"]=4;gapAttrs["WEIGHTS_2"]=5;gapAttrs.erase("JOINTS_1");gapAttrs.erase("WEIGHTS_1");refuses(gap,"nonconsecutive skin sets accepted");
        auto duplicate=f;duplicate.data[jointOffset]=0;refuses(duplicate,"positive duplicate across weight sets accepted");
        auto fullDonor=donor;
        for(const auto& name:{"OTHER","LAST"}) {auto bone=donor[0];bone.name=name;bone.parent=0;fullDonor.push_back(bone);}
        const auto fourWeights=albion::meshimport::mapSkinToDonor(load(f),fullDonor);
        require(fourWeights[0].skin[0].size()==4 && fourWeights[0].skin[0][2].bone==2 && fourWeights[0].skin[0][3].weight==.25,
                "donor mapping prematurely reduced positive weights");++checks;
        auto malformed=f;malformed.doc["meshes"][0]["primitives"][0]["attributes"]["JOINTS_01"]=4;refuses(malformed,"leading-zero skin set accepted");
        f=fixture();f.doc["nodes"].push_back({{"mesh",0},{"skin",0},{"translation",{20,0,0}}});f.doc["scenes"][0]["nodes"].push_back(5);
        const auto repeated=load(f);require(repeated.prims.size()==2 && repeated.skins.size()==2 && repeated.skins[1].meshNode==5 && repeated.prims[1].verts[1].x==100,"repeated skinned instances lost identity or baked pose");++checks;
        f=fixture();const size_t sparseIndices=f.data.size();f.data.insert(f.data.end(),{1,2,0,0});const size_t sparseValues=f.data.size();f.data.insert(f.data.end(),{1,0,0,0,0,1,0,0});
        f.doc["buffers"][0]["byteLength"]=f.data.size();
        f.doc["bufferViews"].push_back({{"buffer",0},{"byteOffset",sparseIndices},{"byteLength",2}});
        f.doc["bufferViews"].push_back({{"buffer",0},{"byteOffset",sparseValues},{"byteLength",8}});
        f.doc["accessors"][1].erase("bufferView");f.doc["accessors"][1]["sparse"]={{"count",2},{"indices",{{"bufferView",4},{"componentType",5121}}},{"values",{{"bufferView",5}}}};
        const auto sparse=load(f);require(sparse.skins[0].influences[1][0].bone==1 && sparse.skins[0].influences[2][1].bone==1,"sparse joint attributes lost");++checks;
        f=fixture(false,5121);f.data[48]=254;refuses(f,"quantized weight sum error accepted");
        f=fixture();f.doc["bufferViews"][3]["byteStride"]=64;refuses(f,"interleaved inverse binds accepted");
        f=fixture();f.doc["buffers"][0]["uri"]="skin.bin";
        const auto external=dir/"skin.gltf";
        {std::ofstream out(dir/"skin.bin",std::ios::binary);out.write(reinterpret_cast<const char*>(f.data.data()),std::streamsize(f.data.size()));}
        {std::ofstream out(external);out<<f.doc.dump();}
        const auto gltf=albion::meshimport::loadSkinnedModel(external);require(gltf.skins[0].joints[0].inverseBind[12]==-1,"glTF sidecar skin metadata lost");++checks;
        fs::remove_all(dir);std::cout<<"skinned input: "<<checks<<" shape, encoding, scene, bind, weight and static-boundary checks PASS\n";return 0;
    }catch(const std::exception& e) {std::cerr<<"skinned input: "<<e.what()<<"; evidence "<<dir<<'\n';return 1;}
}
