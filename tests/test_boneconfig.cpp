#include "forge/boneconfig.hpp"
#include "forge/bin.hpp"
#include <bit>
#include <fstream>
#include <iostream>
#include <limits>
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
    if(argc==3&&std::string_view(argv[1])=="--scale-math-audit") {
        std::ifstream in(argv[2]);require(bool(in),"cannot open arithmetic vectors");
        const auto cases=nlohmann::json::parse(in);auto output=nlohmann::json::array();
        for(const auto& item:cases) {
            Bone base{0,"Bone",forge::bin::nameCrc("Bone"),item.at("base").get<std::array<float,3>>()};
            Bone source=base;source.scale=item.at("source").get<std::array<float,3>>();
            const std::array<WeightedTarget,2> targets{{{{&base,1},1},{{&source,1},item.at("weight").get<float>()}}};
            const auto combined=combine(targets);
            output.push_back(std::array<uint32_t,3>{std::bit_cast<uint32_t>(combined[0].scale[0]),
                std::bit_cast<uint32_t>(combined[0].scale[1]),std::bit_cast<uint32_t>(combined[0].scale[2])});
        }
        std::cout<<output.dump()<<'\n';return 0;
    }
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
    const auto one=prepare(read(start+"Bone: 2,3,4;\r\n"+end));
    const auto two=prepare(read(start+"Bone: 3,2,.5;\r\n"+end));
    const std::array<WeightedTarget,2> layers{{{one,.5f},{two,.25f}}};
    const auto combined=combine(layers);
    require(combined.size()==1&&combined[0].scale==std::array<float,3>{2,2.25f,2.375f},
            "weighted targets must add identity-relative deltas in parameter order");
    const std::array<uint32_t,3> mesh{forge::bin::nameCrc("Missing"),one[0].crc,one[0].crc};
    const auto mapped=mapScales(combined,mesh);
    require(mapped.size()==3&&mapped[0]==std::array<float,3>{1,1,1}&&mapped[1]==combined[0].scale&&
            mapped[2]==mapped[1]&&mapScales({},mesh)[1]==mapped[0],
            "mesh-local slots map by CRC with identity for missing keys");
    const std::array<WeightedTarget,1> zero{{{one,0}}};
    require(combine(zero)[0].scale==std::array<float,3>{1,1,1},"zero weight retains identity bone entry");
    const std::array<WeightedTarget,1> negative{{{one,-.1f}}};refuses([&]{combine(negative);});
    auto unsorted=one;unsorted.push_back(one[0]);
    const std::array<WeightedTarget,1> duplicate{{{unsorted,1}}};refuses([&]{combine(duplicate);});
    refuses([&]{mapScales(unsorted,mesh);});
    Bone collisionA{0,"plumless",forge::bin::nameCrc("plumless"),{2,2,2}};
    Bone collisionB{0,"buckeroo",forge::bin::nameCrc("buckeroo"),{3,3,3}};
    const std::array<WeightedTarget,2> collisions{{{{&collisionA,1},1},{{&collisionB,1},1}}};
    refuses([&]{combine(collisions);});
    Bone big{0,"Bone",one[0].crc,{16777216,16777216,16777216}};
    Bone minus=big;minus.scale={-16777216,-16777216,-16777216};
    Bone small=big;small.scale={2,2,2};
    const std::array<WeightedTarget,3> orderA{{{{&big,1},1},{{&minus,1},1},{{&small,1},1}}};
    const std::array<WeightedTarget,3> orderB{{{{&big,1},1},{{&small,1},1},{{&minus,1},1}}};
    require(combine(orderA)[0].scale[0]==1&&combine(orderB)[0].scale[0]==0,
            "float32 accumulation must preserve parameter order");
    Bone huge=big;huge.scale.fill(std::numeric_limits<float>::max());
    const std::array<WeightedTarget,2> overflow{{{{&huge,1},1},{{&huge,1},1}}};
    refuses([&]{combine(overflow);});
    std::cout<<"Bone config: PASS\n";return 0;
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
