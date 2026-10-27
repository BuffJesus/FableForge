#include "forge/defedit.hpp"
#include "forge/defdecode.hpp"
#include <iostream>
#include <stdexcept>
#include <nlohmann/json.hpp>
using Bytes=std::vector<uint8_t>;
static void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static void word(Bytes& data,uint32_t value){for(int i=0;i<4;++i)data.push_back(uint8_t(value>>(8*i)));}
static Bytes payload(){
    Bytes data{1,0,1,2,0};
    for(auto value:{1u,2u,1u,0x5678u,1u,0u})word(data,value);
    word(data,forge::defdecode::fieldTag("Components"));word(data,0);word(data,0);
    word(data,forge::defdecode::fieldTag("GroupDef"));word(data,2);
    return data;
}
static bool same(const forge::bin::Entry& a,const forge::bin::Entry& b){return a.definition==b.definition && a.name==b.name && a.indexInDefinition==b.indexInDefinition && a.data==b.data;}
static int retail(char** argv){
    namespace fs=std::filesystem;
    const auto sourceRoot=fs::weakly_canonical(fs::path(argv[1]));
    const auto output=fs::weakly_canonical(fs::path(argv[3]));
    require(!fs::exists(output),"retail output must be a fresh owned directory");
    const auto relative=output.lexically_relative(sourceRoot);
    require(!relative.empty() && *relative.begin()=="..","retail output must be outside native install");
    const auto source=sourceRoot/"data"/"CompiledDefs";
    auto file=forge::bin::File::open(source/"names.bin",source/"game.bin");
    const auto originals=file.entries();const auto names=file.originalNames();
    const auto schema=forge::defschema::Schema::load(argv[2]);
    nlohmann::json rows=nlohmann::json::array();
    for(size_t i=0;i<originals.size();++i){
        if(originals[i].definition!="CREATURE")continue;
        const auto before=file.entries().size();
        try{
            const auto appended=forge::defedit::cloneCreature(file,schema,i,"CREATURE_API_PROBE_"+std::to_string(i));
            rows.push_back({{"donor",i},{"appended",appended}});
        }catch(const std::runtime_error& error){
            require(file.entries().size()==before,"retail refusal mutated entry count");
            rows.push_back({{"donor",i},{"refused",error.what()}});
        }
    }
    require(file.originalNames()==names,"memory cloning changed loaded names");
    for(size_t i=0;i<originals.size();++i)require(same(originals[i],file.entries()[i]),"retail clone changed prior record");
    const auto folder=output/"data"/"CompiledDefs";fs::create_directories(folder);
    file.save(folder/"names.bin",folder/"game.bin");
    const auto reopened=forge::bin::File::open(folder/"names.bin",folder/"game.bin");
    require(reopened.entries().size()==file.entries().size(),"retail reopen count differs");
    for(size_t i=0;i<file.entries().size();++i)require(same(file.entries()[i],reopened.entries()[i]),"retail reopen record differs");
    std::cout<<nlohmann::json{{"source_entries",originals.size()},{"entries",file.entries().size()},{"records",rows}}.dump()<<'\n';
    return 0;
}
int main(int argc,char** argv)try{
    if(argc==4)return retail(argv);
    require(argc==1,"expected root schema fresh-output or no arguments");
    const auto schema=forge::defschema::Schema::loadText(R"({"CREATURE":{"fields":[{"name":"Components","type":"?"},{"name":"GroupDef","type":"int32"}]}})");
    forge::bin::File file;file.addEntry("OTHER","OTHER",{});file.addEntry("CREATURE","DONOR",payload());file.addEntry("CCreatureDef","SHARED",{5,7});
    const auto originals=file.entries();const auto pool=file.originalNames();
    auto refuses=[&](auto action){const auto before=file.entries();bool denied=false;try{action();}catch(const std::exception&){denied=true;}require(denied,"invalid creature clone accepted");require(file.entries().size()==before.size(),"refusal changed count");for(size_t i=0;i<before.size();++i)require(same(before[i],file.entries()[i]),"refusal mutated record");require(file.originalNames()==pool,"refusal changed pool");};
    refuses([&]{forge::defedit::cloneCreature(file,schema,99,"NEW");});
    refuses([&]{forge::defedit::cloneCreature(file,schema,2,"NEW");});
    for(const auto& name:std::vector<std::string>{"","DONOR","BAD\nNAME",std::string(128,'A')})
        refuses([&]{forge::defedit::cloneCreature(file,schema,1,name);});
    auto bad=payload();bad[3]=3;file.setEntryData(1,bad);
    refuses([&]{forge::defedit::cloneCreature(file,schema,1,"BAD_COUNT");});
    bad=payload();bad[33]=1;file.setEntryData(1,bad); // Unknown Components count equals donor index.
    refuses([&]{forge::defedit::cloneCreature(file,schema,1,"UNKNOWN_MATCH");});
    bad=payload();bad.erase(bad.begin()+5,bad.begin()+29);bad[3]=0;file.setEntryData(1,bad);
    refuses([&]{forge::defedit::cloneCreature(file,schema,1,"EMPTY_COMPONENTS");});
    file.setEntryData(1,payload());
    const auto appended=forge::defedit::cloneCreature(file,schema,1,"CREATURE_NEW");
    require(appended==3 && file.entries()[appended].indexInDefinition==1,"creature append identity differs");
    auto expected=payload();expected[13]=3;expected[21]=3;
    require(file.entries()[appended].data==expected,"self-only delta differs");
    for(size_t i=0;i<originals.size();++i)require(same(originals[i],file.entries()[i]),"clone changed prior record");
    require(file.originalNames()==pool,"clone changed loaded pool");
    const auto exact=forge::defedit::cloneEntry(file,size_t(1),"EXACT");
    require(file.entries()[exact].data==payload(),"generic exact clone contract changed");
    std::cout<<"Creature verified self-only clone, shared refs and pre-mutation refusal: PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
