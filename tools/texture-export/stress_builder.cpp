// Development-only near-4-GiB texture workload, using the canonical native importer.
// Writes a fresh build directory. Retail input/export and installed game stay read-only.
#include "textureproject.hpp"
#include "texturefileio.hpp"
#include "terrainexport.hpp"
#include "forge/terraintex.hpp"
#include "forge/texturewrite.hpp"
#include "forge/modorder.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <bit>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

namespace fs=std::filesystem;
namespace tw=forge::texturewrite;
namespace tt=forge::terraintex;
namespace tp=albion::textureproject;
using Json=nlohmann::json;
using Key=std::pair<std::string,uint32_t>;
static void need(bool ok,const std::string& text) {if(!ok)throw std::runtime_error(text);}
static std::string hash(const std::vector<uint8_t>& data) {return forge::modorder::sha256OfBytes({reinterpret_cast<const char*>(data.data()),data.size()});}
static void write(const fs::path& path,const std::vector<uint8_t>& data) {
    std::ofstream out(path,std::ios::binary);albion::texturefileio::write(out,data);out.close();need(bool(out),"Could not write "+path.string());
}
struct Encoded {Key key;bool ok=false;unsigned scale=1;forge::big::Entry entry;std::string error;};

static Encoded encode(forge::big::Entry original,const Json& row,const fs::path& collection,const fs::path& scratch,unsigned requested) {
    Encoded result;result.key={row.at("bank").get<std::string>(),original.id};
    const auto imagePath=collection/fs::path(row.at("file").get<std::string>());
    try {
        need(forge::modorder::sha256Of(imagePath)==row.at("pngHash").get<std::string>(),"Source PNG changed");
        std::string error;const auto image=tw::loadImage(imagePath.string(),error);need(!image.rgba.empty(),error);
        const auto& allocation=row.at("texture").at("allocation");
        const auto extent=std::max(std::bit_ceil(allocation.at("width").get<unsigned>()),std::bit_ceil(allocation.at("height").get<unsigned>()));
        const auto scale=std::min(requested,extent?2048u/extent:1u);result.scale=scale;
        if(scale<=1) {result.error="Already at 2048 allocation limit";return result;}
        albion::terrainexport::Image resized;resized.width=unsigned(image.width)*scale;resized.height=unsigned(image.height)*scale;
        need(uint64_t(resized.width)*resized.height<=256ull*1024*1024,"Strip exceeds stress builder pixel limit");
        resized.rgba.resize(size_t(resized.width)*resized.height*4);
        for(unsigned y=0;y<resized.height;++y)for(unsigned x=0;x<resized.width;++x)
            std::memcpy(resized.rgba.data()+(size_t(y)*resized.width+x)*4,image.rgba.data()+(size_t(y/scale)*image.width+x/scale)*4,4);
        const auto png=scratch/(result.key.first+"-"+std::to_string(original.id)+".png");
        write(png,albion::terrainexport::encodePng(resized,false,1));
        forge::big::File one;one.addBank(result.key.first,0).entries.push_back(std::move(original));
        tt::ImportRequest request;request.png=png;request.entryName=row.at("name").get<std::string>();request.exactIdentity=true;
        request.entryId=result.key.second;request.subBank=result.key.first;request.format.clear();request.useImageSize=true;request.maxDimension=2048;
        request.frameStrip=one.banks()[0].entries[0].type==1;request.volumeStrip=one.banks()[0].entries[0].type==4;
        request.signedUv=(row.at("format").get<unsigned>()&255)==24;request.rawMip0=true;
        const auto imported=tt::applyImage(one,request);fs::remove(png);
        need(imported.ok,imported.validation.errors.empty()?"Native image import failed":imported.validation.errors.front());
        result.entry=std::move(one.banks()[0].entries[0]);result.ok=true;
    } catch(const std::exception& error) {result.error=error.what();}
    return result;
}

int main(int argc,char** argv) try {
    need(argc==6,"usage: stress_builder original.big exported-project fresh-output target-bytes workers");
    const auto source=fs::canonical(argv[1]),collection=fs::canonical(argv[2]),out=fs::absolute(argv[3]).lexically_normal();
    const uint64_t budget=std::stoull(argv[4],nullptr,0);const unsigned workers=std::stoul(argv[5]);
    need(out.parent_path()==fs::current_path()/"build" && !fs::exists(out),"Output must be fresh and directly under this checkout build");
    need(budget>0x80000000ull && budget<=0xffffffffull && workers>=1 && workers<=16,"Invalid archive budget or worker count");
    const auto sourceHash=forge::modorder::sha256Of(source);
    std::ifstream input(collection/"texture-project.json");Json manifest;input>>manifest;
    need(manifest.at("kind")=="FableForge texture project" && manifest.at("schema")==1,"Unsupported source project");
    auto original=forge::big::File::open(source),file=original;
    std::map<Key,forge::big::Entry*> destination;std::map<Key,const forge::big::Entry*> originals;
    for(auto& bank:file.banks())for(auto& entry:bank.entries)destination[{bank.name,entry.id}]=&entry;
    for(const auto& bank:original.banks())for(const auto& entry:bank.entries)originals[{bank.name,entry.id}]=&entry;
    fs::create_directory(out);fs::create_directory(out/"scratch-images");
    Json report={{"source",source.string()},{"sourceSha256",sourceHash},{"collection",collection.string()},{"targetBytes",budget},
        {"method","nearest-neighbour 2x baseline plus budgeted 4x/2048 upgrades; native DXT retained; raw mip0"},
        {"workers",workers},{"gameLaunched",false},{"entries",Json::array()},{"skipped",Json::array()},{"state","encoding"}};
    uint64_t size=tp::rebuiltArchiveSize(file);std::map<Key,unsigned> scales;
    auto save=[&] {report["currentArchiveBytes"]=size;std::ofstream output(out/"stress-build.json.tmp");output<<report.dump(2)<<'\n';output.close();fs::rename(out/"stress-build.json.tmp",out/"stress-build.json");};
    save();std::vector<Json> rows=manifest.at("entries").get<std::vector<Json>>();
    // Upgrade the assets most likely to be resident in the first world before
    // filling the remaining bank budget with other game textures.
    auto priority=[](const Json& row) {
        std::string name=row.at("name").get<std::string>();std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return char(std::toupper(c));});
        for(const char* term:{"OAKVALE","HERO","HUMAN","GROUND","BUILDING","GRASS","FOLIAGE"})if(name.find(term)!=std::string::npos)return 0;
        return 1;
    };
    for(unsigned requested:{2u,4u}) {
        if(requested==4)std::stable_sort(rows.begin(),rows.end(),[&](const auto& a,const auto& b){return priority(a)<priority(b);});
        report["requestedScale"]=requested;
        for(size_t at=0;at<rows.size();at+=workers) {
            std::vector<std::future<Encoded>> jobs;
            for(size_t i=at;i<std::min(rows.size(),at+workers);++i) {
                const auto row=rows[i];const Key key{row.at("bank").get<std::string>(),row.at("id").get<uint32_t>()};
                need(originals.contains(key),"Project entry missing from source bank");
                auto entry=*originals.at(key);entry.data=original.entryData(entry);
                jobs.push_back(std::async(std::launch::async,[entry=std::move(entry),row,collection,out,requested]() mutable {
                    return encode(std::move(entry),row,collection,out/"scratch-images",requested);
                }));
            }
            for(auto& job:jobs) {
                auto result=job.get();
                if(!result.ok) {if(requested==2)report["skipped"].push_back({{"bank",result.key.first},{"id",result.key.second},{"reason",result.error}});continue;}
                if(scales[result.key]>=result.scale)continue;
                auto& slot=*destination.at(result.key);auto previous=std::move(slot);slot=std::move(result.entry);
                const auto proposed=tp::rebuiltArchiveSize(file);
                if(proposed>budget) {slot=std::move(previous);continue;}
                scales[result.key]=result.scale;size=proposed;
            }
            if(at%128<workers || at+workers>=rows.size()) {
                report["processedInPass"]=std::min(rows.size(),at+workers);report["resizedEntries"]=scales.size();save();
                std::cout<<requested<<"x "<<std::min(rows.size(),at+workers)<<"/"<<rows.size()<<"; "<<size<<" archive bytes; "<<scales.size()<<" resized\n"<<std::flush;
            }
        }
    }
    need(size<=budget && size<=0xffffffffull,"Archive exceeds budget/32-bit format limit");
    report["state"]="serializing";save();auto bytes=file.serialize();need(bytes.size()==size,"Serialized size differs from prediction");
    const auto bankPath=out/"textures.big";write(bankPath,bytes);bytes.clear();bytes.shrink_to_fit();
    report["state"]="verifying";save();const auto actual=forge::big::File::open(bankPath);
    uint64_t payloadBytes=0;std::map<unsigned,unsigned> scaleCounts;
    need(actual.banks().size()==file.banks().size(),"Written bank count differs");
    for(size_t b=0;b<file.banks().size();++b) {
        const auto& expectedBank=file.banks()[b];const auto& observedBank=actual.banks()[b];
        need(expectedBank.name==observedBank.name && expectedBank.id==observedBank.id && expectedBank.entries.size()==observedBank.entries.size(),"Written bank identities differ");
        for(size_t i=0;i<expectedBank.entries.size();++i) {
            const auto& expected=expectedBank.entries[i];const auto& observed=observedBank.entries[i];
            const auto payload=file.entryData(expected);const auto expectedHash=hash(payload);payloadBytes+=payload.size();
            need(expected.id==observed.id && expected.name==observed.name && expected.type==observed.type && expected.magic==observed.magic &&
                expected.devFileType==observed.devFileType && expected.devCrc==observed.devCrc && expected.devSources==observed.devSources &&
                expected.subHeader==observed.subHeader && expectedHash==hash(actual.entryData(observed)),"Written texture changed unexpectedly");
            const Key key{expectedBank.name,expected.id};const unsigned scale=scales.contains(key)?scales.at(key):1;++scaleCounts[scale];
            report["entries"].push_back({{"bank",key.first},{"id",key.second},{"name",expected.name},{"scale",scale},
                {"payloadBytes",payload.size()},{"payloadSha256",expectedHash},{"headerSha256",hash(expected.subHeader)}});
        }
    }
    need(forge::modorder::sha256Of(source)==sourceHash,"Source archive changed during stress build");
    report["state"]="verified";report["archiveSha256"]=forge::modorder::sha256Of(bankPath);report["realTexturePayloadBytes"]=payloadBytes;
    report["metadataAndAlignmentBytes"]=size-payloadBytes;report["scaleCounts"]=Json::object();
    for(auto [scale,count]:scaleCounts)report["scaleCounts"][std::to_string(scale)]=count;
    report["sourceUnchanged"]=true;save();std::cout<<"VERIFIED "<<size<<" bytes; "<<payloadBytes<<" native texture payload bytes\n";
} catch(const std::exception& error) {std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
