#include "forge/levstorage.hpp"
#include "forge/wad.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using Bytes=std::vector<uint8_t>;
static void word(Bytes& b,uint32_t v){for(unsigned i=0;i<4;++i)b.push_back(uint8_t(v>>(8*i)));}
static void set(Bytes& b,size_t at,uint32_t v){for(unsigned i=0;i<4;++i)b.at(at+i)=uint8_t(v>>(8*i));}
template<class F>void refuses(F f){try{f();}catch(const std::exception&){return;}throw std::runtime_error("invalid LEV storage accepted");}
int main(int argc,char**argv){try {
    using forge::levstorage::validate;
    if(argc==2 || (argc==3 && std::string_view(argv[2])=="--symbols")) {
        const bool symbols=argc==3;
        nlohmann::json report={{"maps",0},{"height_cells",0},{"game_cells",0},{"navigation_sections",0},
            {"navigation_nodes",0},{"ground_names",0},{"atmos_names",0},{"sound_names",0}};
        if(symbols)report["symbols"]=nlohmann::json::array();
        auto review=[&](const Bytes& b,const std::string& name) {
            try {
                const auto r=validate(b);
                if(symbols)report["symbols"].push_back({{"file",name},
                    {"ground",r.groundThemes},{"atmosphere",r.atmosThemes},{"sound",r.soundThemes}});
                for(const auto& [key,count]:std::vector<std::pair<std::string,size_t>>{
                    {"maps",1},{"height_cells",r.heightCells},{"game_cells",r.gameCells},
                    {"navigation_sections",r.navigationSections},{"navigation_nodes",r.navigationNodes},
                    {"ground_names",r.groundThemes.size()},{"atmos_names",r.atmosThemes.size()},{"sound_names",r.soundThemes.size()}})
                    report[key]=report[key].get<size_t>()+count;
            }catch(const std::exception& e){throw std::runtime_error(name+": "+e.what());}
        };
        if(std::filesystem::is_regular_file(argv[1])) {
            // Payload-only audit: the WAD envelope has a separate contract.
            const auto wad=forge::wad::Archive::open(argv[1]);
            for(const auto& entry:wad.entries())if(std::filesystem::path(entry.name).extension()==".lev")
                review(wad.read(entry),entry.name);
        } else for(const auto& entry:std::filesystem::recursive_directory_iterator(argv[1])) {
            if(!entry.is_regular_file() || entry.path().extension()!=".lev")continue;
            std::ifstream in(entry.path(),std::ios::binary);if(!in)throw std::runtime_error("cannot read level");
            const Bytes b((std::istreambuf_iterator<char>(in)),{});review(b,entry.path().string());
        }
        std::cout<<report.dump(2)<<'\n';return report["maps"].get<size_t>()?0:1;
    }
    if(argc!=1)return 2;
    for(uint8_t version:{8,9}) {
        Bytes b;word(b,25);b.push_back(4);word(b,25);word(b,15360);word(b,0);word(b,15361);word(b,0);
        word(b,version==8?67614:67618);b.push_back(version);
        word(b,15360);word(b,15361);word(b,32);word(b,32);b.push_back(1);
        auto palette=[&](const std::string& name,uint32_t value) {
            const size_t at=b.size();b.insert(b.end(),name.begin(),name.end());b.resize(at+128);word(b,value);
            b.resize(at+256*132);return at;
        };
        const size_t ground=palette("GROUND",15360);word(b,3);word(b,2);
        const size_t atmos=palette("ATMOS",15361);if(version==9)word(b,15360);
        word(b,5);b.insert(b.end(),{'S','O','U','N','D'});
        const size_t cells=b.size();
        for(size_t i=0;i<33*33;++i){word(b,21);b.push_back(7);word(b,15360);b.resize(b.size()+12);}
        const size_t game=b.size();
        for(size_t i=0;i<8*8;++i){word(b,11);b.push_back(1);b.resize(b.size()+5);b.push_back(1);}
        const size_t brush=b.size();set(b,13,uint32_t(brush));word(b,33801);b.push_back(1);word(b,0);palette("",0);
        const size_t nav=b.size();set(b,21,uint32_t(nav));word(b,uint32_t(nav+8));word(b,0);
        const auto original=b;const auto r=validate(b);
        if(b!=original || r.width!=32 || r.height!=32 || r.heightCells!=1089 || r.gameCells!=64 ||
            r.groundThemes!=std::vector<std::string>{"GROUND"} || r.atmosThemes!=std::vector<std::string>{"ATMOS"} ||
            r.soundThemes!=std::vector<std::string>{"SOUND"} || r.navigationSections)
            throw std::runtime_error("LEV storage domain/count/preservation mismatch");
        for(size_t n:{size_t(0),size_t(24),size_t(46),ground+127,atmos+131,cells-1,cells,game-1,brush-1,brush,nav-1,b.size()-1})
            refuses([&]{validate(Bytes(b.begin(),b.begin()+n));});
        for(const auto [at,value]:{std::pair<size_t,uint32_t>{0,24},{5,26},{13,uint32_t(brush+1)},
            {21,uint32_t(nav+1)},{25,100},{38,0},{38,0xffffffffu},{ground+33792,4},{ground+33796,129},
            {cells,20},{game,10},{brush,33800},{brush+5,1}}) {
            auto bad=b;set(bad,at,value);refuses([&]{validate(bad);});
        }
        for(size_t at:{size_t(4),size_t(29),size_t(46),cells+4,game+4,brush+4}) {
            auto bad=b;bad[at]=255;refuses([&]{validate(bad);});
        }
        auto bad=b;bad[ground+7]='X';refuses([&]{validate(bad);});
        bad=b;std::fill_n(bad.begin()+ground,128,'X');refuses([&]{validate(bad);});
        bad=b;bad[ground]=0;refuses([&]{validate(bad);});
        bad=b;bad.push_back(0);refuses([&]{validate(bad);});
        bad=b;bad[game+10]=2;refuses([&]{validate(bad);});
        for(uint8_t sentinel:{128,255}){auto negative=b;negative[game+10]=sentinel;validate(negative);}
        // Only zero/nonzero presence matters to native palette lookup. Keep
        // stale cached values and unrelated UID/CRC/float words byte-exact.
        auto moved=b;set(moved,ground+128,0xffffffffu);set(moved,atmos+128,1);
        if(validate(moved).groundThemes!=r.groundThemes || validate(moved).atmosThemes!=r.atmosThemes)
            throw std::runtime_error("cached palette values changed symbolic identities");
        set(moved,ground+128,0);if(!validate(moved).groundThemes.empty())throw std::runtime_error("unused palette slot admitted");
    }
    std::cout<<"LEV storage checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
