#include "forge/navpatch.hpp"
#include <iostream>
#include <stdexcept>
using Bytes=std::vector<uint8_t>;
static void word(Bytes& b,uint32_t v){for(unsigned i=0;i<4;++i)b.push_back(uint8_t(v>>(i*8)));}
static void set(Bytes& b,size_t at,uint32_t v){for(unsigned i=0;i<4;++i)b.at(at+i)=uint8_t(v>>(i*8));}
template<class F>void refuses(F f){try{f();}catch(const std::exception&){return;}throw std::runtime_error("invalid navigation storage accepted");}
int main(int argc,char**argv){try {
    using forge::navmesh::validateNavigationStorage;
    if(argc==2) {
        size_t maps=0,sections=0,nodes=0;
        for(const auto& entry:std::filesystem::recursive_directory_iterator(argv[1])) {
            if(!entry.is_regular_file() || entry.path().extension()!=".lev")continue;
            try {
                const auto file=forge::lev::File::open(entry.path());
                const auto nav=validateNavigationStorage(file.originalBytes(),file.navigationOffset(),file.width(),file.height());
                ++maps;sections+=nav.sections.size();for(const auto& section:nav.sections)nodes+=section.nodes.size();
            }catch(const std::exception& e){throw std::runtime_error(entry.path().string()+": "+e.what());}
        }
        std::cout<<"maps="<<maps<<" sections="<<sections<<" nodes="<<nodes<<'\n';return maps?0:1;
    }
    if(argc!=1)return 2;
    for(uint32_t version:{5u,6u,7u,8u}) {
        Bytes b(16);word(b,36);word(b,1);word(b,4);b.insert(b.end(),{'N','U','L','L'});word(b,36);
        word(b,0);word(b,version);word(b,0x42000000);word(b,0x42000000);word(b,2);
        word(b,1);word(b,15360);word(b,15361);const size_t positionLayer=b.size();word(b,0);
        word(b,1);const size_t nodeCount=b.size();word(b,1);
        const size_t flags=b.size();if(version>5)b.push_back(1);
        b.insert(b.end(),{1,0,1,0,0});word(b,0x41800000);word(b,0x41800000);
        const size_t index=b.size();word(b,15360);const size_t region=b.size();word(b,1);
        if(version>6)b.push_back(0x80);
        const size_t neighbours=b.size();word(b,1);word(b,15360);
        if(version>5){word(b,1);word(b,15360);word(b,15361);}
        word(b,0xacbdef12);set(b,36,uint32_t(b.size()));
        const auto original=b;const auto nav=validateNavigationStorage(b,16,32,32);
        if(b!=original || nav.sections.size()!=1 || nav.sections[0].nodes[0].index!=15360)
            throw std::runtime_error("navigation review changed identity/storage");
        for(size_t n=0;n<b.size();++n)refuses([&]{validateNavigationStorage(Bytes(b.begin(),b.begin()+n),16,32,32);});
        for(const auto [at,value]:{std::pair<size_t,uint32_t>{16,35},{20,2},{24,5000},{32,37},
            {36,uint32_t(b.size()-1)},{40,9},{44,0x7fc00000},{positionLayer,1},{nodeCount,0xffffffffu},
            {index,0},{region,2},{neighbours,4097},{neighbours+4,15361}}) {
            auto bad=b;set(bad,at,value);refuses([&]{validateNavigationStorage(bad,16,32,32);});
        }
        auto bad=b;bad[flags]=2;refuses([&]{validateNavigationStorage(bad,16,32,32);});
        bad=b;bad.push_back(0);refuses([&]{validateNavigationStorage(bad,16,32,32);});
        refuses([&]{validateNavigationStorage(b,SIZE_MAX,32,32);});
        refuses([&]{validateNavigationStorage(b,16,64,32);});
    }
    Bytes empty;word(empty,8);word(empty,0);
    if(!validateNavigationStorage(empty,0,32,32).sections.empty())throw std::runtime_error("empty directory changed");
    empty.push_back(0);refuses([&]{validateNavigationStorage(empty,0,32,32);});
    // A saved but unbuilt navigation section can retain a layer-zero action
    // point while reporting no layers, nodes or regions.
    Bytes unbuilt;word(unbuilt,20);word(unbuilt,1);word(unbuilt,4);
    unbuilt.insert(unbuilt.end(),{'N','U','L','L'});word(unbuilt,20);
    word(unbuilt,68);word(unbuilt,8);word(unbuilt,0x42000000);word(unbuilt,0x42000000);
    word(unbuilt,0);word(unbuilt,1);word(unbuilt,15360);word(unbuilt,15361);word(unbuilt,0);
    word(unbuilt,0);word(unbuilt,0);word(unbuilt,0xacbdef12);
    validateNavigationStorage(unbuilt,0,32,32);
    set(unbuilt,52,1);refuses([&]{validateNavigationStorage(unbuilt,0,32,32);});
    std::cout<<"navigation storage checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
