#include "forge/defexternal.hpp"
#include "forge/terraintex.hpp"
#include "forge/textbig.hpp"
#include "forge/big.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <iostream>
#include <stdexcept>

using Bytes=std::vector<uint8_t>;
static void require(bool value,const char* reason) {if(!value)throw std::runtime_error(reason);}
static void word(Bytes& bytes,uint32_t value) {
    for(unsigned i=0;i<4;++i)bytes.push_back(uint8_t(value>>(i*8)));
}
static int audit(const std::filesystem::path& root) {
    nlohmann::json report{{"scope","read-only supported external BIG-entry layouts; not installation admission"},
        {"root",root.string()},{"files",nlohmann::json::array()}};
    for(const char* relative:{"data/graphics/pc/textures.big","data/graphics/pc/frontend.big",
                             "data/graphics/graphics.big","data/lang/English/text.big"}) {
        const auto file=forge::big::File::open(root/relative);
        nlohmann::json row{{"file",relative},{"accepted",0},{"refusals",nlohmann::json::array()}};
        for(const auto& bank:file.banks())for(const auto& entry:bank.entries) {
            const auto result=forge::defexternal::reviewBigEntry(bank.name,entry.type,entry.subHeader,file.entryData(entry));
            if(result.definitionIndependent) row["accepted"]=row["accepted"].get<size_t>()+1;
            else row["refusals"].push_back({{"bank",bank.name},{"type",entry.type},{"id",entry.id},
                {"name",entry.name},{"reason",result.reason}});
        }
        report["files"].push_back(std::move(row));
    }
    std::cout<<report.dump(2)<<'\n';return 0;
}
int main(int argc,char** argv) {try {
    if(argc==2)return audit(argv[1]);
    if(argc!=1)return 2;
    using forge::defexternal::reviewBigEntry;
    require(!reviewBigEntry("UNKNOWN",0,{},{}).definitionIndependent,"empty unknown entry accepted");
    require(!reviewBigEntry("MBANK_ALLMESHES",1,Bytes(64),{}).definitionIndependent,"mesh implicitly accepted");
    Bytes texture(34);texture[0]=8;texture[2]=8;texture[6]=8;texture[8]=8;texture[10]=1;
    texture[12]=forge::terraintex::kFormatDXT5;texture[16]=3;texture[17]=4;texture[20]=64;
    std::copy_n(forge::terraintex::pixelFormatTail(forge::terraintex::kFormatDXT5),6,texture.begin()+28);
    Bytes pixels(85);const auto original=texture;
    require(reviewBigEntry("GBANK_MAIN_PC",0,texture,pixels).definitionIndependent,"valid DXT5 refused");
    require(texture==original && pixels==Bytes(85),"review modified inputs");
    texture.push_back(0);
    require(!reviewBigEntry("GBANK_MAIN_PC",0,texture,pixels).definitionIndependent,"opaque texture info tail accepted");
    require(!reviewBigEntry("GBANK_MAIN_PC",99,original,pixels).definitionIndependent,"unknown texture type accepted");
    pixels.pop_back();require(!reviewBigEntry("GBANK_MAIN_PC",0,original,pixels).definitionIndependent,"short texture accepted");
    forge::textbig::Entry text;text.content="15360";text.identifier="SAMPLE";text.tags={{0,"ANIM:TEST"}};
    auto payload=forge::textbig::encode(text);
    require(reviewBigEntry("TEXT_ENGLISH_MAIN",0,Bytes(4),payload).definitionIndependent,"text content confused with definition IDs");
    payload.push_back(0);require(!reviewBigEntry("TEXT_ENGLISH_MAIN",0,Bytes(4),payload).definitionIndependent,"text tail accepted");
    text.type=1;text.groupMembers={15360,15361};payload=forge::textbig::encode(text);
    require(reviewBigEntry("TEXT_ENGLISH_MAIN",1,{},payload).definitionIndependent,"local text IDs treated as definition IDs");
    require(!reviewBigEntry("TEXT_ENGLISH_MAIN",1,Bytes(4),payload).definitionIndependent,"opaque group info accepted");
    payload.pop_back();require(!reviewBigEntry("TEXT_ENGLISH_MAIN",1,{},payload).definitionIndependent,"truncated group accepted");
    Bytes narrators;word(narrators,2);narrators.insert(narrators.end(),{'A',0,'B',0});
    require(reviewBigEntry("TEXT_ENGLISH_MAIN",2,narrators,{}).definitionIndependent,"narrator strings refused");
    narrators.pop_back();require(!reviewBigEntry("TEXT_ENGLISH_MAIN",2,narrators,{}).definitionIndependent,"unterminated narrator accepted");
    // Minimal complete raw 3DAF root with no tracks, then its observed zero footer.
    Bytes animation;word(animation,0x3e3e3e3e);animation.insert(animation.end(),{'3','D','A','F'});
    word(animation,0);word(animation,0);animation.insert(animation.end(),{'A','N','R','T'});
    word(animation,5);animation.push_back(0);word(animation,0);word(animation,0);
    require(reviewBigEntry("MBANK_ALLMESHES",9,Bytes(24),animation).definitionIndependent,"complete animation refused");
    require(!reviewBigEntry("MBANK_ALLMESHES",9,Bytes(25),animation).definitionIndependent,"opaque animation info accepted");
    animation[20]=100;require(!reviewBigEntry("MBANK_ALLMESHES",9,Bytes(24),animation).definitionIndependent,"malformed animation accepted");
    std::cout<<"external definition-domain review checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
