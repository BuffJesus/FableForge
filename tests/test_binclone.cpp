#include "forge/bin.hpp"
#include "forge/defedit.hpp"
#include "forge/temporarydirectory.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
using Bytes=std::vector<uint8_t>;
static void check(bool ok,const char* why){if(!ok) throw std::runtime_error(why);}
static void word(Bytes& b,uint32_t v){for(int i=0;i<4;++i)b.push_back(uint8_t(v>>(8*i)));}
static void write(const std::filesystem::path& p,const Bytes& b){std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),b.size());check(bool(f),"fixture write");}
static Bytes read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);check(bool(f),"fixture read");return Bytes(std::istreambuf_iterator<char>(f),{});}
static bool same(const forge::bin::Entry& a,const forge::bin::Entry& b){return a.name==b.name&&a.definition==b.definition&&a.indexInDefinition==b.indexInDefinition&&a.data==b.data;}
int main(){try{
 forge::TemporaryDirectory temp(std::filesystem::temp_directory_path(),"bin-clone-");auto names=temp.path()/"names.bin",bank=temp.path()/"game.bin";
 Bytes pool;for(auto v:{0u,0xa8e36c34u,2u,32u,0u})word(pool,v);
 for(int i=0;i<2;++i){word(pool,forge::bin::nameCrc("POOL_ONLY"));pool.insert(pool.end(),{'P','O','O','L','_','O','N','L','Y',0});}
 Bytes raw(9);for(auto v:{0u,1u,0u,0u,0u})word(raw,v);write(names,pool);write(bank,raw);
 auto seed=forge::bin::File::open(names,bank);seed.addEntry("OPAQUE","DONOR",{0,255,7,0,3});seed.addEntry("OTHER","1",{41,42});seed.addEntry("OPAQUE","",{8,9});seed.addEntry("OPAQUE","DUP",{10});seed.addEntry("OPAQUE","DUP",{11});seed.save(names,bank);
 auto file=forge::bin::File::open(names,bank);const auto originals=file.entries();const auto oldPool=file.originalNames();const auto oldNames=read(names);const auto oldBank=read(bank);
 auto refused=[&](auto action){const auto before=file.entries();bool denied=false;try{action();}catch(const std::exception&){denied=true;}check(denied,"invalid clone accepted");check(file.entries().size()==before.size(),"refusal changed count");for(size_t i=0;i<before.size();++i)check(same(before[i],file.entries()[i]),"refusal mutated record");check(file.originalNames()==oldPool,"refusal changed pool");};
 refused([&]{forge::defedit::cloneEntry(file,size_t(99),"NEW");});
 for(const auto& name:std::vector<std::string>{"","DONOR","DUP",std::string(128,'A'),"BAD\nNAME",std::string("BAD\0NAME",8),std::string("BAD\xff",4)})refused([&]{forge::defedit::cloneEntry(file,size_t(0),name);});
 refused([&]{forge::defedit::cloneEntry(file,"", "NEW");});refused([&]{forge::defedit::cloneEntry(file,"MISSING", "NEW");});refused([&]{forge::defedit::cloneEntry(file,"DUP", "NEW");});
 auto a=forge::defedit::cloneEntry(file,size_t(0),"NEW");check(a==originals.size(),"clone global slot");check(file.entries()[a].indexInDefinition==4&&file.entries()[a].data==originals[0].data,"clone ordinal or payload");
 auto b=forge::defedit::cloneEntry(file,"1","NEW_NUMERIC");check(file.entries()[b].definition=="OTHER"&&file.entries()[b].data==originals[1].data&&file.entries()[b].indexInDefinition==1,"numeric donor name was treated as index");
 auto c=forge::defedit::cloneEntry(file,size_t(2),"POOL_ONLY");check(file.entries()[c].data==originals[2].data,"unnamed donor clone");
 check(file.originalNames()==oldPool&&read(names)==oldNames&&read(bank)==oldBank,"memory clone wrote files or changed loaded pool");
 auto outNames=temp.path()/"new-names.bin",outBank=temp.path()/"new-game.bin";file.save(outNames,outBank);auto reopened=forge::bin::File::open(outNames,outBank);
 check(reopened.entries().size()==file.entries().size(),"roundtrip count");for(size_t i=0;i<file.entries().size();++i)check(same(file.entries()[i],reopened.entries()[i]),"roundtrip record");
 auto outPool=read(outNames);check(outPool.size()>oldNames.size()&&std::equal(oldNames.begin()+20,oldNames.end(),outPool.begin()+20),"loaded pool body was not exact prefix");check(reopened.originalNames().size()==oldPool.size()+2,"existing unused pool name was unnecessarily appended");
 for(size_t i=0;i<originals.size();++i)check(same(originals[i],reopened.entries()[i]),"clone changed prior slot");
 auto bad=oldBank;bad[21]=9;write(temp.path()/"bad.bin",bad);auto noncanonical=forge::bin::File::open(names,temp.path()/"bad.bin");bool denied=false;try{noncanonical.cloneEntry(0,"NEW_BAD");}catch(const std::exception&){denied=true;}check(denied&&noncanonical.entries().size()==originals.size(),"noncanonical ordinals accepted");
 std::cout<<"Clone bytes, canonical slots, namespace refusal and paired roundtrip: PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
