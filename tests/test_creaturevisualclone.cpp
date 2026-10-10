#include "forge/defedit.hpp"
#include "forge/defdecode.hpp"
#include "forge/temporarydirectory.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <nlohmann/json.hpp>

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
    }
    auto bad=source;auto data=root(false);put(data,9,6);bad.setEntryData(0,data);refuses(bad,clone);
    bad=source;data=body();put(data,15,1);bad.setEntryData(3,data);refuses(bad,clone);
    bad=source;data=appearance();put(data,15,UINT32_MAX);bad.setEntryData(4,data);refuses(bad,clone);
    bad=source;data=body();data.erase(data.begin()+10);bad.setEntryData(3,data);refuses(bad,clone);
    bad=source;bad.addEntry("CREATURE","NONCANONICAL",root(true));refuses(bad,clone);

    const auto original=source.entries();auto working=source;const auto result=clone(working);
    require(result.rootIndex==7&&result.bodyIndex==8&&result.appearanceIndex==9,"private append slots differ");
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
    require(read(namesPath)==oldNames&&read(bankPath)==oldBank,"private test changed original source pair");
    std::cout<<"Private Creature graph: selected ownership, shared gameplay, atomic refusal and paired roundtrip PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
