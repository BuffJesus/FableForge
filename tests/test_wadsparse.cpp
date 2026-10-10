#include "forge/wad.hpp"
#include "forge/wadstorage.hpp"
#include "forge/temporarydirectory.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
using Bytes=std::vector<uint8_t>;
static void word(Bytes& b,uint32_t v){for(unsigned i=0;i<4;++i)b.push_back(uint8_t(v>>(8*i)));}
static void set(Bytes& b,size_t at,uint32_t v){for(unsigned i=0;i<4;++i)b.at(at+i)=uint8_t(v>>(8*i));}
static uint32_t get(const Bytes& b,size_t at){uint32_t v=0;for(unsigned i=0;i<4;++i)v|=uint32_t(b.at(at+i))<<(8*i);return v;}
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static Bytes read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);check(bool(f),"read failed");return Bytes((std::istreambuf_iterator<char>(f)),{});}
static void write(const std::filesystem::path& p,const Bytes& b){std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),b.size());check(bool(f),"write failed");}
template<class F>void refuses(F f){try{f();}catch(const std::exception&){return;}throw std::runtime_error("overflowing append accepted");}
int main(int argc,char** argv){try {
    if(argc==2) {
        const auto review=forge::wadstorage::validateEnvelope(argv[1]);
        std::cout<<"{\"entries\":"<<review.metadata.entries.size()
                 <<",\"maximumId\":"<<review.metadata.maximumId
                 <<",\"zeroPaddingBytes\":"<<review.zeroPaddingBytes<<"}\n";return 0;
    }
    Bytes b;for(uint32_t v:{0x42424242u,115u,2u,1u,2048u,31u,2u,6144u})word(b,v);
    b.resize(2048);b.insert(b.end(),{1,2,3,4,5,6,7,8});
    b.resize(4096);b.insert(b.end(),{9,10,11,12,13,14,15,16});b.resize(6144);
    word(b,1);word(b,0);word(b,2);
    auto name=[&](const std::string& s){word(b,uint32_t(s.size()));b.insert(b.end(),s.begin(),s.end());};
    auto row=[&](uint32_t id,uint32_t at,const std::string& key){
        const auto start=b.size();for(uint32_t v:{42u,id,0u,8u,at,0u})word(b,v);
        name(key);word(b,0);word(b,1);name("");word(b,88);b.resize(b.size()+88);b.back()=0xa7;return start;
    };
    row(7,2048,"A.lev");const auto second=row(31,4096,"B.tng");
    forge::TemporaryDirectory scratch(std::filesystem::temp_directory_path(),"wad-sparse-");
    const auto src=scratch.path()/"source.wad",out=scratch.path()/"result.wad";write(src,b);
    const auto envelope=forge::wadstorage::validateEnvelope(src);
    check(envelope.metadata.maximumId==31 && envelope.metadata.entries.size()==2 &&
          envelope.zeroPaddingBytes==6096,"sparse envelope counts wrong");
    auto metadata=[&](const Bytes& bytes){
        return forge::wadstorage::validateMetadata(std::span(bytes).first(32),
                                                  std::span(bytes).subspan(6144),bytes.size());
    };
    for(size_t cut=6144;cut<b.size();++cut) {
        const Bytes shortBytes(b.begin(),b.begin()+cut);refuses([&]{metadata(shortBytes);});
    }
    for(const auto [at,value]:std::vector<std::pair<size_t,uint32_t>>{
            {20,6},{24,3},{6144+8,1},{second+4,7},{second+16,2048},
            {second+12,UINT32_MAX},{second+8,1}}) {
        auto bad=b;set(bad,at,value);refuses([&]{metadata(bad);});
    }
    auto bad=b;bad[6144+12+28]=0;refuses([&]{metadata(bad);});
    bad=b;bad[bad.size()-4]=2;refuses([&]{metadata(bad);});
    bad=b;bad.push_back(0);refuses([&]{metadata(bad);});
    bad=b;bad[32]=1;write(out,bad);refuses([&]{forge::wadstorage::validateEnvelope(out);});
    const auto original=forge::wad::Archive::open(src);
    check(original.entries().size()==2 && original.entries()[1].id==31,"sparse WAD live count confused with maximum ID");
    for(bool native:{false,true})for(size_t count:{size_t(1),size_t(2)}) {
        if(native) {
            std::vector<forge::wad::NativeEntry> rows;
            for(size_t i=0;i<count;++i)rows.push_back({"New"+std::to_string(i)+".lev",Bytes(5,uint8_t(i+1))});
            forge::wad::appendNativeEntries(src,rows,out);
        } else {
            std::vector<forge::wad::CloneEntry> rows;
            for(size_t i=0;i<count;++i)rows.push_back({"A.lev","Clone"+std::to_string(i)+".lev"});
            forge::wad::appendClonedEntries(src,rows,out);
        }
        const auto bytes=read(out);const auto archive=forge::wad::Archive::open(out);
        const auto reviewed=forge::wadstorage::validateMetadata(std::span(bytes).first(32),
            std::span(bytes).subspan(get(bytes,28)),bytes.size());
        check(reviewed.entries.size()==2+count && reviewed.entries.back().name==archive.entries().back().name,
              "appended metadata does not pass independent review");
        check(get(bytes,20)==31+count && get(bytes,24)==2+count,"appended WAD header identity/count mismatch");
        check(archive.entries().size()==2+count && archive.entries().back().id==31+count,"new WAD IDs lost");
        check(get(bytes,get(bytes,28)+8)==2+count,"WAD histogram lost live count");
        auto prefix=Bytes(bytes.begin(),bytes.begin()+6144);
        for(size_t at:{20,24,28})set(prefix,at,get(b,at));
        check(prefix==Bytes(b.begin(),b.begin()+6144),"append changed original WAD data");
        for(size_t i=0;i<2;++i)check(archive.read(archive.entries()[i])==original.read(original.entries()[i]) &&
            archive.entries()[i].info==original.entries()[i].info,"original WAD payload/metadata changed");
        for(size_t i=0;i<count;++i)check(archive.read(archive.entries()[2+i])==
            (native?Bytes(5,uint8_t(i+1)):Bytes{1,2,3,4,5,6,7,8}),"appended WAD payload changed");
    }
    forge::wad::repack(src,{},out);check(read(out)==b,"empty sparse repack changed bytes");
    for(size_t length:{size_t(8),size_t(19)}) {
        const Bytes replacement(length,0x7e);forge::wad::repack(src,{{"B.tng",replacement}},out);
        const auto bytes=read(out);const auto archive=forge::wad::Archive::open(out);
        check(get(bytes,20)==31 && get(bytes,24)==2,"repack changed WAD identity/count");
        check(archive.read(archive.entries()[0])==Bytes({1,2,3,4,5,6,7,8}) &&
              archive.read(archive.entries()[1])==replacement,"sparse repack changed wrong payload");
        if(length==8){auto expected=b;std::copy(replacement.begin(),replacement.end(),expected.begin()+4096);
            check(bytes==expected,"same-size repack changed unrelated bytes");}
    }
    auto overflow=b;set(overflow,20,UINT32_MAX);set(overflow,second+4,UINT32_MAX);write(src,overflow);
    const auto previous=read(out);
    refuses([&]{forge::wad::appendClonedEntries(src,{{"A.lev","overflow.lev"}},out);});
    check(read(out)==previous,"overflowing WAD append replaced output");
    std::cout<<"sparse WAD checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
