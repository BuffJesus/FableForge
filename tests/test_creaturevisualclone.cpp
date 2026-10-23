#include "forge/defedit.hpp"
#include "forge/defdecode.hpp"
#include "forge/temporarydirectory.hpp"
#include "forge/fmplinks.hpp"
#include "definitionclone.hpp"
#include "definitionbody.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <nlohmann/json.hpp>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using Bytes=std::vector<uint8_t>;
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void word(Bytes& out,uint32_t value){for(size_t i=0;i<4;++i)out.push_back(uint8_t(value>>(8*i)));}
static void put(Bytes& out,size_t offset,uint32_t value){for(size_t i=0;i<4;++i)out.at(offset+i)=uint8_t(value>>(8*i));}
static void field(Bytes& out,const char* name,uint32_t value){word(out,forge::defdecode::fieldTag(name));word(out,value);}
static bool same(const forge::bin::Entry& a,const forge::bin::Entry& b){return a.definition==b.definition&&a.name==b.name&&a.indexInDefinition==b.indexInDefinition&&a.data==b.data;}
static void write(const std::filesystem::path& path,const Bytes& bytes){std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());require(bool(out),"fixture write failed");}
static Bytes read(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);require(bool(in),"fixture read failed");return Bytes(std::istreambuf_iterator<char>(in),{});}
static Bytes root(bool donor){
    Bytes out{1,0,1,3,0};
    word(out,forge::defdecode::fieldTag("CCreatureDef"));word(out,3);word(out,0);
    word(out,forge::defdecode::fieldTag("CAppearanceDef"));word(out,4);word(out,donor ? 1 : 0);
    word(out,forge::defdecode::fieldTag("CBrainDef"));word(out,5);word(out,donor ? 1 : 0);
    field(out,"Components",0);field(out,"Self",donor ? 1 : 0);field(out,"Other",99);
    return out;
}
static Bytes body(){Bytes out{1,0,1};field(out,"Link",1);field(out,"Other",99);return out;}
static Bytes appearance(){
    Bytes out{1,0,1};field(out,"Link",1);
    word(out,forge::defdecode::fieldTag("Animation"));
    for(const auto value:{0u,4u,1u,3u,UINT32_MAX})word(out,value);
    word(out,forge::defdecode::fieldTag("Tail"));out.push_back(0);return out;
}

static int retail(char** argv){
    namespace fs=std::filesystem;
    const auto sourceRoot=fs::weakly_canonical(fs::path(argv[2]));
    const auto output=fs::weakly_canonical(fs::path(argv[4]));
    const auto relative=output.lexically_relative(sourceRoot);
    require(!fs::exists(output)&&!relative.empty()&&*relative.begin()=="..","private retail output must be fresh and outside the native install");
    const auto folder=sourceRoot/"data"/"CompiledDefs";
    auto file=forge::bin::File::open(folder/"names.bin",folder/"game.bin");
    const auto originals=file.entries();auto expected=originals;const auto names=file.originalNames();
    const auto schema=forge::defschema::Schema::load(argv[3]);nlohmann::json rows=nlohmann::json::array();
    for(size_t i=0;i<originals.size();++i){
        if(originals[i].definition!="CREATURE")continue;
        try{
            const auto result=forge::defedit::cloneCreatureVisuals(file,schema,i,
                "FORGE_PRIVATE_API_ROOT_"+std::to_string(i),"FORGE_PRIVATE_API_BODY_"+std::to_string(i),
                "FORGE_PRIVATE_API_APPEARANCE_"+std::to_string(i));
            require(result.rootIndex==expected.size()&&result.bodyIndex==expected.size()+1&&result.appearanceIndex==expected.size()+2,"private retail append order differs");
            for(size_t n=0;n<3;++n)expected.push_back(file.entries()[result.rootIndex+n]);
            rows.push_back({{"donor",i},{"root",result.rootIndex},{"body",result.bodyIndex},{"appearance",result.appearanceIndex}});
        }catch(const std::runtime_error& error){
            require(file.entries().size()==expected.size(),"private retail refusal changed count");
            for(size_t n=0;n<expected.size();++n)require(same(expected[n],file.entries()[n]),"private retail refusal changed a record");
            rows.push_back({{"donor",i},{"refused",error.what()}});
        }
    }
    require(file.entries().size()==expected.size()&&file.originalNames()==names,"private retail memory namespace differs");
    for(size_t i=0;i<expected.size();++i)require(same(expected[i],file.entries()[i]),"private retail prior record changed");
    const auto outFolder=output/"data"/"CompiledDefs";fs::create_directories(outFolder);
    file.save(outFolder/"names.bin",outFolder/"game.bin");
    const auto reopened=forge::bin::File::open(outFolder/"names.bin",outFolder/"game.bin");
    require(reopened.entries().size()==expected.size(),"private retail reopen count differs");
    for(size_t i=0;i<expected.size();++i)require(same(expected[i],reopened.entries()[i]),"private retail reopen record differs");
    std::cout<<nlohmann::json{{"source_entries",originals.size()},{"entries",file.entries().size()},{"records",rows}}.dump()<<'\n';return 0;
}
int main(int argc,char** argv)try{
    using namespace forge;
    if(argc==4&&std::string_view(argv[1])=="--visual-graph-audit") {
        const auto folder=std::filesystem::path(argv[2])/"data"/"CompiledDefs";
        const auto file=bin::File::open(folder/"names.bin",folder/"game.bin");
        const auto schema=defschema::Schema::load(argv[3]);
        auto rows=nlohmann::json::array();
        for(size_t i=0;i<file.entries().size();++i) {
            if(file.entries()[i].definition!="CREATURE")continue;
            try {
                const auto graph=defedit::resolveCreatureVisuals(file,schema,i);
                rows.push_back({{"root",i},{"body",graph.bodyIndex},{"appearance",graph.appearanceIndex},
                    {"count",graph.componentCount},{"body_row",graph.bodyRow},{"appearance_row",graph.appearanceRow}});
            } catch(const std::runtime_error& error) {rows.push_back({{"root",i},{"refused",error.what()}});}
        }
        std::cout<<nlohmann::json{{"records",rows}}.dump()<<'\n';return 0;
    }
    if(argc==9 && std::string_view(argv[1])=="--body-publish") {
        namespace fs=std::filesystem;
        const auto native=fs::weakly_canonical(fs::path(argv[2]));
        const auto output=fs::weakly_canonical(fs::path(argv[4]));
        const auto relative=output.lexically_relative(native);
        require(!fs::exists(output)&&!relative.empty()&&*relative.begin()=="..","body output must be fresh and outside source install");
        const auto folder=native/"data"/"CompiledDefs";
        const auto names=folder/"names.bin",bank=folder/"game.bin",graphicsPath=native/"data"/"graphics"/"graphics.big";
        const auto schema=defschema::Schema::load(argv[3]);
        const auto graphics=big::File::open(graphicsPath);auto plan=bin::File::open(names,bank);
        const auto bankBytes=read(bank),nameBytes=read(names);
        const size_t entry=std::stoull(argv[5]),bodyPart=std::stoull(argv[6]),alternative=std::stoull(argv[7]);
        const auto model=std::stoull(argv[8]);require(model<=UINT32_MAX,"model ID overflow");
        const auto result=defedit::setCreatureBodyModel(plan,schema,entry,bodyPart,alternative,uint32_t(model),graphics);
        const albion::definitionwrite::CreatureBodyContract proof{schema,graphics,bankBytes,nameBytes,entry,bodyPart,alternative,uint32_t(model)};
        albion::definitionwrite::publishCreatureBody(plan,names,bank,graphicsPath,output/"data"/"CompiledDefs"/"game.bin",proof);
        require(read(bank)==bankBytes&&read(names)==nameBytes,"body test changed native source banks");
        std::cout<<nlohmann::json{{"entry",entry},{"body_part",bodyPart},{"alternative",alternative},
            {"offset",result.offset},{"old_id",result.oldModelId},{"new_id",result.newModelId}}.dump()<<'\n';return 0;
    }
    if(argc==5&&std::string_view(argv[1])=="--retail-audit")return retail(argv);
    require(argc==1,"expected --retail-audit root schema fresh-output or no arguments");
    const auto schema=defschema::Schema::loadText(R"({
        "CREATURE":{"fields":[{"name":"Components","type":"uint32"},{"name":"Self","type":"CDefIndex"},{"name":"Other","type":"int32"}]},
        "CCreatureDef":{"fields":[{"name":"Link","type":"CDefIndex"},{"name":"Other","type":"int32"}]},
        "CAppearanceDef":{"fields":[{"name":"Link","type":"CDefIndex"},{"name":"Animation","type":"CAnimationSet"},{"name":"Tail","type":"bool"}]}
    })");
    TemporaryDirectory temp(std::filesystem::temp_directory_path(),"creature-visual-clone-");
    const auto namesPath=temp.path()/"names.bin",bankPath=temp.path()/"game.bin";
    Bytes names;for(const auto value:{0u,0xa8e36c34u,0u,4u,0u})word(names,value);
    Bytes bank(9);for(const auto value:{0u,1u,0u,0u,0u})word(bank,value);
    write(namesPath,names);write(bankPath,bank);
    auto seed=bin::File::open(namesPath,bankPath);
    seed.addEntry("CREATURE","ANCESTOR",root(false));seed.addEntry("CREATURE","1",root(true));
    seed.addEntry("OTHER","OTHER",{});seed.addEntry("CCreatureDef","",body());
    seed.addEntry("CAppearanceDef","",appearance());seed.addEntry("CBrainDef","SHARED_GAMEPLAY",{5,7});
    seed.addEntry("CCreatureDef","SECOND_BODY",body());seed.save(namesPath,bankPath);
    auto source=bin::File::open(namesPath,bankPath);
    const auto oldNames=read(namesPath),oldBank=read(bankPath);const auto oldPool=source.originalNames();
    auto refuses=[&](bin::File candidate,auto action){
        const auto before=candidate.entries();const auto pool=candidate.originalNames();bool denied=false;
        try{action(candidate);}catch(const std::runtime_error&){denied=true;}
        require(denied,"invalid private visual clone accepted");
        require(candidate.entries().size()==before.size(),"private refusal changed count");
        for(size_t i=0;i<before.size();++i)require(same(before[i],candidate.entries()[i]),"private refusal changed a record");
        require(candidate.originalNames()==pool,"private refusal changed loaded names");
    };
    auto clone=[&](bin::File& candidate){return defedit::cloneCreatureVisuals(candidate,schema,1,"NEW_ROOT","NEW_BODY","NEW_APPEARANCE");};
    const auto graph=defedit::resolveCreatureVisuals(source,schema,1);
    require(graph.rootIndex==1&&graph.bodyIndex==3&&graph.appearanceIndex==4&&
            graph.componentCount==3&&graph.bodyRow==0&&graph.appearanceRow==1,
            "read-only visual ownership graph differs");
    require(source.originalNames()==oldPool&&read(namesPath)==oldNames&&read(bankPath)==oldBank,
            "read-only visual resolution changed source files/pool");
    refuses(source,[&](auto& candidate){defedit::resolveCreatureVisuals(candidate,schema,99);});
    refuses(source,[&](auto& candidate){defedit::resolveCreatureVisuals(candidate,schema,3);});
    refuses(source,[&](auto& candidate){defedit::cloneCreatureVisuals(candidate,schema,99,"R","B","A");});
    refuses(source,[&](auto& candidate){defedit::cloneCreatureVisuals(candidate,schema,3,"R","B","A");});
    for(const auto& invalid:std::vector<std::string>{"","1","BAD\nNAME",std::string(128,'A'),std::string("BAD\xff",4)})
        for(size_t slot=0;slot<3;++slot)refuses(source,[&](auto& candidate){
            std::vector<std::string> newNames{"R","B","A"};newNames[slot]=invalid;
            defedit::cloneCreatureVisuals(candidate,schema,1,newNames[0],newNames[1],newNames[2]);
        });
    refuses(source,[&](auto& candidate){defedit::cloneCreatureVisuals(candidate,schema,1,"SAME","SAME","A");});
    require(bin::nameCrc("plumless")==bin::nameCrc("buckeroo"),"CRC collision fixture changed");
    refuses(source,[&](auto& candidate){defedit::cloneCreatureVisuals(candidate,schema,1,"plumless","buckeroo","A");});
    for(const auto mutation:std::vector<std::pair<size_t,uint32_t>>{
        {3,4},{5,defdecode::fieldTag("CAppearanceDef")},{9,4},{9,UINT32_MAX},
        {13,99},{13,5},{25,99},{61,1}}){
        auto bad=source;auto data=root(true);
        if(mutation.first==3)data[3]=uint8_t(mutation.second);else put(data,mutation.first,mutation.second);
        bad.setEntryData(1,data);refuses(bad,clone);
        if(mutation.first!=61)
            refuses(bad,[&](auto& candidate){defedit::resolveCreatureVisuals(candidate,schema,1);});
        else require(defedit::resolveCreatureVisuals(bad,schema,1).bodyIndex==3,
                     "graph resolution must not claim relocation validation of unrelated fields");
    }
    auto bad=source;auto data=root(false);put(data,9,6);bad.setEntryData(0,data);refuses(bad,clone);
    bad=source;data=body();put(data,15,1);bad.setEntryData(3,data);refuses(bad,clone);
    bad=source;data=appearance();put(data,15,UINT32_MAX);bad.setEntryData(4,data);refuses(bad,clone);
    bad=source;data=body();data.erase(data.begin()+10);bad.setEntryData(3,data);refuses(bad,clone);
    bad=source;bad.addEntry("CREATURE","NONCANONICAL",root(true));refuses(bad,clone);

    const auto original=source.entries();auto working=source;const auto result=clone(working);
    require(result.rootIndex==7&&result.bodyIndex==8&&result.appearanceIndex==9,"private append slots differ");
    const auto privateGraph=defedit::resolveCreatureVisuals(working,schema,result.rootIndex);
    require(privateGraph.bodyIndex==result.bodyIndex&&privateGraph.appearanceIndex==result.appearanceIndex,
            "private clone ownership resolves through shared graph checks");
    auto rootExpected=root(true);put(rootExpected,9,8);put(rootExpected,13,7);put(rootExpected,21,9);put(rootExpected,25,7);put(rootExpected,53,7);
    auto bodyExpected=body();put(bodyExpected,7,7);auto appearanceExpected=appearance();put(appearanceExpected,7,7);
    require(working.entries()[7].data==rootExpected,"private root ownership/delta differs");
    require(working.entries()[8].data==bodyExpected&&working.entries()[9].data==appearanceExpected,"private component delta differs");
    require(working.entries()[7].indexInDefinition==2&&working.entries()[8].indexInDefinition==2&&working.entries()[9].indexInDefinition==1,"private ordinals differ");
    require(std::equal(original[1].data.begin()+29,original[1].data.begin()+41,working.entries()[7].data.begin()+29),"shared gameplay triple changed");
    for(size_t i=0;i<original.size();++i)require(same(original[i],working.entries()[i]),"private clone changed prior slot");
    require(working.originalNames()==oldPool&&read(namesPath)==oldNames&&read(bankPath)==oldBank,"private memory clone changed source files/pool");
    const auto outNames=temp.path()/"output-names.bin",outBank=temp.path()/"output-game.bin";
    working.save(outNames,outBank);const auto reopened=bin::File::open(outNames,outBank);
    require(reopened.entries().size()==working.entries().size(),"private reopen count differs");
    for(size_t i=0;i<working.entries().size();++i)require(same(working.entries()[i],reopened.entries()[i]),"private reopen record differs");
    const auto poolBytes=read(outNames);require(std::equal(oldNames.begin()+20,oldNames.end(),poolBytes.begin()+20),"private output altered loaded name records");
    auto again=working;const auto second=defedit::cloneCreatureVisuals(again,schema,7,"SECOND_ROOT","SECOND_PRIVATE_BODY","SECOND_PRIVATE_APPEARANCE");
    require(second.rootIndex==10&&second.bodyIndex==11&&second.appearanceIndex==12,"private clone of clone failed");
    namespace fs=std::filesystem;
    const albion::definitionwrite::CreatureVisualCloneContract contract{schema,1,"NEW_ROOT","NEW_BODY","NEW_APPEARANCE"};
    const auto published=temp.path()/"published"/"game.bin";
    auto publish=[&](const auto& candidate,const auto& destination,const auto* proof){
        albion::definitionwrite::publishClone(candidate,namesPath,bankPath,destination,true,nullptr,{},proof);
    };
    auto deniedPublication=[&](const auto& candidate,const auto* proof){
        bool denied=false;try{publish(candidate,published,proof);}catch(const std::runtime_error&){denied=true;}
        require(denied,"invalid private publisher plan accepted");
        require(!fs::exists(published.parent_path()),"private publisher refusal created output");
        require(read(namesPath)==oldNames&&read(bankPath)==oldBank,"private publisher refusal changed source");
    };
    deniedPublication(working,static_cast<const albion::definitionwrite::CreatureVisualCloneContract*>(nullptr));
    for(size_t slot=0;slot<3;++slot){
        auto tampered=working;auto bytes=tampered.entries()[7+slot].data;bytes.back()^=1;
        tampered.setEntryData(7+slot,bytes);deniedPublication(tampered,&contract);
    }
    auto wrongContract=contract;wrongContract.donorIndex=0;deniedPublication(working,&wrongContract);
    auto wrongNameContract=contract;wrongNameContract.bodyName="DIFFERENT_BODY";deniedPublication(working,&wrongNameContract);
    bool conflictDenied=false;
    try{albion::definitionwrite::publishClone(working,namesPath,bankPath,published,true,&schema,1,&contract);}
    catch(const std::runtime_error&){conflictDenied=true;}
    require(conflictDenied&&!fs::exists(published.parent_path()),"conflicting private/shared contracts wrote output");
    auto priorTampered=working;priorTampered.setEntryData(5,{8,9});deniedPublication(priorTampered,&contract);
    publish(working,published,&contract);
    const auto publishedBank=bin::File::open(published.parent_path()/"names.bin",published);
    albion::definitionwrite::verifyCloneRecords(working,publishedBank,working.entries().size());
    for(const auto& target:{published,published.parent_path()/"names.bin"})
        require(fs::is_regular_file(fs::path(target.string()+albion::backups::kCreatedSuffix)),"private fresh publication lacks creation marker");
    bool repeatDenied=false;try{publish(working,published,&contract);}catch(const std::runtime_error&){repeatDenied=true;}
    require(repeatDenied,"private publisher overwrote an incompatible appended namespace");
    const auto existing=temp.path()/"existing"/"game.bin";fs::create_directories(existing.parent_path());
    write(existing,oldBank);write(existing.parent_path()/"names.bin",oldNames);publish(working,existing,&contract);
    require(read(fs::path(existing.string()+albion::backups::kOrigSuffix))==oldBank&&
        read(fs::path((existing.parent_path()/"names.bin").string()+albion::backups::kOrigSuffix))==oldNames,
        "private paired publication did not retain both original files");
    const auto failed=temp.path()/"recovery-refusal"/"game.bin";fs::create_directories(failed.parent_path());
    write(fs::path(failed.string()+albion::backups::kCreatedSuffix),{1});
    bool recoveryDenied=false;try{publish(working,failed,&contract);}catch(const std::runtime_error&){recoveryDenied=true;}
    require(recoveryDenied&&!fs::exists(failed)&&!fs::exists(failed.parent_path()/"names.bin"),"orphan recovery record did not prevent paired creation");
    const auto alias=temp.path()/"hardlink-refusal"/"game.bin";fs::create_directories(alias.parent_path());
    fs::create_hard_link(bankPath,alias);
    bool aliasDenied=false;try{publish(working,alias,&contract);}catch(const std::runtime_error&){aliasDenied=true;}
    require(aliasDenied&&read(alias)==oldBank&&!fs::exists(alias.parent_path()/"names.bin"),"hardlinked source alias did not refuse before writes");
    // In-place publication is confined to another independent fixture pair.
    const auto inPlace=temp.path()/"in-place"/"game.bin";fs::create_directories(inPlace.parent_path());
    const auto inPlaceNames=inPlace.parent_path()/"names.bin";write(inPlace,oldBank);write(inPlaceNames,oldNames);
    albion::definitionwrite::publishClone(working,inPlaceNames,inPlace,inPlace,true,nullptr,{},&contract);
    albion::definitionwrite::verifyCloneRecords(working,bin::File::open(inPlaceNames,inPlace),working.entries().size());
    require(read(fs::path(inPlace.string()+albion::backups::kOrigSuffix))==oldBank&&
        read(fs::path(inPlaceNames.string()+albion::backups::kOrigSuffix))==oldNames,"private in-place pair lacks exact originals");
    require(read(namesPath)==oldNames&&read(bankPath)==oldBank,"private test changed original source pair");
    {
        const auto bindingSchema=defschema::Schema::loadText(R"({"CCreatureDef":{"fields":[{"name":"InitialActions","type":"uint32"}]}})");
        Bytes payload{1,0,1};field(payload,"WoundMorphs",0);word(payload,0x01ab6cee);
        field(payload,"RandomAppearanceMorph",3);word(payload,1);
        const size_t modelAt=payload.size();word(payload,10);word(payload,0);word(payload,0);word(payload,0);payload.push_back(0);
        word(payload,0);word(payload,0);
        payload.insert(payload.end(),27,0);word(payload,0);payload.insert(payload.end(),13,0);
        field(payload,"Expressions",0);word(payload,0x01ab6d32);field(payload,"Stats",0);field(payload,"InitialActions",0);
        auto baseline=source;baseline.setEntryData(3,payload);
        const auto folder=temp.path()/"body-source";fs::create_directories(folder);
        const auto bodyNames=folder/"names.bin",bodyBank=folder/"game.bin",graphicsPath=folder/"graphics.big";
        baseline.save(bodyNames,bodyBank);
        big::File assets;auto& meshBank=assets.addBank("MBANK_ALLMESHES",0);
        for(const auto id:{10u,20u}) {big::Entry mesh;mesh.id=id;mesh.type=5;mesh.name=std::to_string(id);mesh.data={1};meshBank.entries.push_back(mesh);}
        write(graphicsPath,assets.serialize());const auto graphics=big::File::open(graphicsPath);
        const auto bankBytes=read(bodyBank),nameBytes=read(bodyNames);
        baseline=bin::File::open(bodyNames,bodyBank);auto plan=baseline;
        defedit::setCreatureBodyModel(plan,bindingSchema,3,0,0,20,graphics);
        const albion::definitionwrite::CreatureBodyContract proof{bindingSchema,graphics,bankBytes,nameBytes,3,0,0,20};
        const auto fresh=temp.path()/"body-fresh"/"game.bin";
        const auto refuseBody=[&](const bin::File& candidate,const auto& contract,const fs::path& destination) {
            const bool hadBank=fs::exists(destination),hadNames=fs::exists(destination.parent_path()/"names.bin");
            const auto previousBank=hadBank?read(destination):Bytes{};
            const auto previousNames=hadNames?read(destination.parent_path()/"names.bin"):Bytes{};
            bool refused=false;
            try{albion::definitionwrite::publishCreatureBody(candidate,bodyNames,bodyBank,graphicsPath,destination,contract);}
            catch(const std::runtime_error&){refused=true;}
            require(refused,"invalid body publication accepted");
            require(hadBank==fs::exists(destination)&&hadNames==fs::exists(destination.parent_path()/"names.bin"),"body refusal changed output existence");
            if(hadBank)require(read(destination)==previousBank,"body refusal changed prior output bank");
            if(hadNames)require(read(destination.parent_path()/"names.bin")==previousNames,"body refusal changed prior output names");
        };
        auto wrongProof=proof;wrongProof.modelId=10;refuseBody(plan,wrongProof,fresh);
        auto wrongAlternative=proof;wrongAlternative.alternative=99;refuseBody(plan,wrongAlternative,fresh);
        auto extra=plan;extra.addEntry("OTHER","UNAUTHORIZED",{});refuseBody(extra,proof,fresh);
        auto oldNamesChanged=nameBytes;oldNamesChanged[0]^=1;write(bodyNames,oldNamesChanged);
        refuseBody(plan,proof,fresh);write(bodyNames,nameBytes);
        const auto hardlinked=temp.path()/"body-hardlink"/"game.bin";fs::create_directories(hardlinked.parent_path());
        fs::create_hard_link(bodyBank,hardlinked);refuseBody(plan,proof,hardlinked);
        const auto incompatible=temp.path()/"body-destination"/"data"/"CompiledDefs"/"game.bin";
        fs::create_directories(incompatible.parent_path());write(incompatible,bankBytes);write(incompatible.parent_path()/"names.bin",nameBytes);
        const auto incompatibleGraphics=incompatible.parent_path().parent_path()/"graphics"/"graphics.big";
        fs::create_directories(incompatibleGraphics.parent_path());
        auto changedDirectory=assets;changedDirectory.banks()[0].entries[1].type=1;
        write(incompatibleGraphics,changedDirectory.serialize());refuseBody(plan,proof,incompatible);
        require(read(bodyBank)==bankBytes&&read(bodyNames)==nameBytes,"negative body routes altered source");
        auto bad=plan;auto badPayload=bad.entries()[3].data;badPayload.back()^=1;bad.setEntryData(3,badPayload);
        bool denied=false;try{albion::definitionwrite::publishCreatureBody(bad,bodyNames,bodyBank,graphicsPath,fresh,proof);}
        catch(const std::runtime_error&){denied=true;}
        require(denied&&!fs::exists(fresh.parent_path()),"tampered body plan created output");
        albion::definitionwrite::publishCreatureBody(plan,bodyNames,bodyBank,graphicsPath,fresh,proof);
        albion::definitionwrite::verifyEntries(plan,bin::File::open(fresh.parent_path()/"names.bin",fresh));
        auto expected=payload;put(expected,modelAt,20);require(plan.entries()[3].data==expected,"body publication delta differs");
        require(read(fresh.parent_path()/"names.bin")==nameBytes,"body publication normalized original names");
        require(fs::exists(fs::path(fresh.string()+albion::backups::kCreatedSuffix)),"body fresh publication lacks recovery marker");
        for(const auto& path:{fresh,fresh.parent_path()/"names.bin"}) {
            albion::backups::Entry recovery;recovery.file=path;
            recovery.backup=fs::path(path.string()+albion::backups::kCreatedSuffix);
            recovery.kind=albion::backups::Kind::Created;recovery.created=true;
            std::string error;require(albion::backups::restore(recovery,false,error),"body created restore failed");
            require(!fs::exists(path)&&!fs::exists(recovery.backup),"body created restore left file or marker");
        }
        const auto late=temp.path()/"late-body"/"game.bin";bool callbackCalled=false;
        denied=false;try {
            albion::definitionwrite::publish(plan,bodyNames,late,true,[&] {
                callbackCalled=true;require(!fs::exists(late),"callback ran after installation");
                throw std::runtime_error("late fixture refusal");
            });
        }catch(const std::runtime_error&){denied=true;}
        require(denied&&callbackCalled&&!fs::exists(late)&&!fs::exists(late.parent_path()/"names.bin"),
                "late publication refusal installed outputs");
#ifdef _WIN32
        // Hold a prepared file without FILE_SHARE_DELETE. Installation has
        // already renamed earlier banks when this Windows rename must fail.
        for(const auto& lockedName:{std::string("names.bin"),std::string("game.bin.forge-created")}) {
            const auto rollback=temp.path()/("body-rollback-"+lockedName)/"game.bin";
            HANDLE held=INVALID_HANDLE_VALUE;bool lockReached=false;denied=false;std::string failure;
            try {
                albion::definitionwrite::publish(plan,bodyNames,rollback,true,[&] {
                    for(const auto& child:fs::directory_iterator(rollback.parent_path())) {
                        if(!child.is_directory() || !child.path().filename().string().starts_with("forge-def-prepare-"))continue;
                        const auto prepared=child.path()/"new"/lockedName;
                        held=CreateFileW(prepared.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
                        require(held!=INVALID_HANDLE_VALUE,"could not lock prepared rollback fixture");
                        lockReached=true;break;
                    }
                    require(lockReached,"prepared rollback fixture missing");
                });
            }catch(const std::runtime_error& error){denied=true;failure=error.what();}
            if(held!=INVALID_HANDLE_VALUE)CloseHandle(held);
            require(denied&&lockReached,"locked later publication file did not refuse");
            require(failure.find("rename")!=std::string::npos && failure.find(lockedName)!=std::string::npos,
                    "rollback fixture did not fail at the intended later rename");
            for(const auto& name:{"game.bin","names.bin","game.bin.forge-created","names.bin.forge-created"})
                require(!fs::exists(rollback.parent_path()/name),"partial body publication rollback left installed file");
            require(read(bodyBank)==bankBytes&&read(bodyNames)==nameBytes,"body rollback altered source banks");
        }
#endif
        auto stale=bankBytes;stale[0]^=1;write(bodyBank,stale);
        denied=false;try{albion::definitionwrite::publishCreatureBody(plan,bodyNames,bodyBank,graphicsPath,temp.path()/"stale-body"/"game.bin",proof);}
        catch(const std::runtime_error&){denied=true;}
        require(denied,"stale body bank accepted");write(bodyBank,bankBytes);
        auto staleGraphics=assets;staleGraphics.banks()[0].entries[1].type=1;write(graphicsPath,staleGraphics.serialize());
        denied=false;try{albion::definitionwrite::publishCreatureBody(plan,bodyNames,bodyBank,graphicsPath,temp.path()/"stale-graphics"/"game.bin",proof);}
        catch(const std::runtime_error&){denied=true;}
        require(denied,"stale graphics directory accepted");write(graphicsPath,assets.serialize());
        albion::definitionwrite::publishCreatureBody(plan,bodyNames,bodyBank,graphicsPath,bodyBank,proof);
        require(read(fs::path(bodyBank.string()+albion::backups::kOrigSuffix))==bankBytes,"body in-place original differs");
        require(read(bodyNames)==nameBytes,"body in-place namespace changed");
        albion::backups::Entry original;original.file=bodyBank;
        original.backup=fs::path(bodyBank.string()+albion::backups::kOrigSuffix);
        std::string restoreError;require(albion::backups::restore(original,false,restoreError),"body original restore failed");
        require(read(bodyBank)==bankBytes&&read(bodyNames)==nameBytes,"body restore did not recover exact source pair");
    }
    std::cout<<"Private Creature graph and typed body publication PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
