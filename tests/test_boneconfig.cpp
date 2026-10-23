#include "forge/boneconfig.hpp"
#include "forge/bin.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <nlohmann/json.hpp>

static void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
template<class F> static void refuses(F action) {
    try {action();}catch(const std::runtime_error&) {return;}
    throw std::runtime_error("malformed bone config accepted");
}
static nlohmann::json rows(const std::vector<forge::boneconfig::Bone>& bones) {
    auto result=nlohmann::json::array();
    for(const auto& bone:bones)result.push_back({{"offset",bone.offset},{"name",bone.name},
        {"crc",bone.crc},{"scale",bone.scale}});
    return result;
}
int main(int argc,char** argv)try {
    using namespace forge::boneconfig;
    if(argc==3&&std::string_view(argv[1])=="--read") {
        std::ifstream in(argv[2],std::ios::binary);require(bool(in),"cannot open input");
        const std::string text(std::istreambuf_iterator<char>(in),{});
        const auto config=read(text);
        std::cout<<nlohmann::json{{"begin",config.bodyBegin},{"end",config.bodyEnd},
            {"rows",rows(config.rows)},{"prepared",rows(prepare(config))}}.dump()<<'\n';return 0;
    }
    require(argc==1,"expected --read file or no arguments");
    const std::string start="#Start_Bone_data\r\n",end="#End_bone_data\r\n";
    const std::string header="Creature_type: UNINTERPRETED;\r\n#Start_group_settings\r\nX: ignored;\r\n";
    const auto text=header+start+"Bip01 Spine: 1.0, 1, 1;\r\nBip01 R Foot: +1.1, .75, -2e-1;\r\n"+end+"unknown tail\r\n";
    const auto original=text;const auto config=read(text);const auto target=prepare(config);
    require(text==original&&config.rows.size()==2&&config.bodyBegin==header.size()+start.size()&&
        config.rows[0].offset==config.bodyBegin&&config.rows[0].name=="Bip01 Spine"&&
        config.rows[1].scale==std::array<float,3>{1.1f,.75f,-.2f}&&target.size()==1&&
        target[0].crc==forge::bin::nameCrc("Bip01 R Foot")&&config.rows.size()==2,
        "source rows/offsets or native identity pruning differ");
    require(read(start+end).rows.empty(),"empty bone section differs");
    const auto ordered=prepare(read(start+"Z: 2,2,2;\r\nA: 3,3,3;\r\n"+end));
    require(ordered.size()==2&&ordered[0].crc<ordered[1].crc,"target CRC order differs");
    for(const auto& bad:std::vector<std::string>{"",start,start+"X: 1,2,3;\n"+end,
        start+"X: 1,2;\r\n"+end,start+"X: 1,2,3,4;\r\n"+end,
        start+"X: 1,2,3\r\n"+end,start+"X: nan,2,3;\r\n"+end,
        start+"X: 1e99,2,3;\r\n"+end,start+"X: +-1,2,3;\r\n"+end,
        start+" X: 1,2,3;\r\n"+end,start+"X: 1,2,3;\r\nX: 2,2,2;\r\n"+end,
        start+"plumless: 2,2,2;\r\nbuckeroo: 3,3,3;\r\n"+end,
        start+end+start+end,std::string(1024*1024+1,'x'),start+"X"+std::string(1,'\0')+": 1,2,3;\r\n"+end})
        refuses([&]{read(bad);});
    auto damaged=config;damaged.rows[0].crc^=1;refuses([&]{prepare(damaged);});
    damaged=config;damaged.rows.push_back(config.rows[1]);refuses([&]{prepare(damaged);});
    std::cout<<"Bone config: PASS\n";return 0;
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
