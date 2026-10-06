#include "forge/stbstorage.hpp"
#include "forge/stbbake.hpp"
#include "forge/rangecodec.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using Bytes=std::vector<uint8_t>;
static void append(Bytes& b,const Bytes& a) {b.insert(b.end(),a.begin(),a.end());}
static void half(Bytes& b,uint16_t v) {b.push_back(uint8_t(v));b.push_back(uint8_t(v>>8));}
static void word(Bytes& b,uint32_t v) {for(unsigned i=0;i<4;++i)b.push_back(uint8_t(v>>(8*i)));}
static void range(Bytes& b,const Bytes& raw) {
    const auto block=forge::rangecodec::encodeRaw(raw);word(b,uint32_t(block.size()));append(b,block);
}
template<class F> static void refuses(F f) {
    try {f();}catch(const std::exception&) {return;}
    throw std::runtime_error("malformed terrain storage accepted");
}
int main() {try {
    using namespace forge::stbstorage;
    forge::stbbake::ForegroundFrame frame;
    forge::stbbake::ForegroundLayer layer;layer.sharedIndexBuffer=true;layer.vertices.resize(3);
    layer.textures[0]=15360;layer.vertices[0].height=15360;frame.layers.push_back(layer);
    auto dry=forge::stbbake::serializeForegroundFrame(frame);validateForeground(dry);
    auto suffix=dry;suffix.push_back(0);refuses([&]{validateForeground(suffix);});
    frame.hasWater=true;frame.waterPayload.resize(12);word(frame.waterPayload,1);
    range(frame.waterPayload,Bytes(289*66));
    const auto wet=forge::stbbake::serializeForegroundFrame(frame);const auto original=wet;
    validateForeground(wet);if(wet!=original)throw std::runtime_error("terrain review changed input");
    for(size_t n:{size_t(0),size_t(1),dry.size()-1,wet.size()-1})
        refuses([&]{validateForeground(Bytes(wet.begin(),wet.begin()+n));});
    frame.waterPayload[12]=9;refuses([&]{validateForeground(forge::stbbake::serializeForegroundFrame(frame));});
    frame.waterPayload[12]=1;frame.waterPayload.push_back(0);
    refuses([&]{validateForeground(forge::stbbake::serializeForegroundFrame(frame));});
    Bytes b;half(b,16);half(b,16);half(b,0);half(b,0);b.push_back(0);b.push_back(0);
    half(b,1);half(b,3);b.push_back(4);b.push_back(4);b.push_back(1);
    forge::stbbake::InlineTexture texture;texture.width=4;texture.height=4;texture.levels=1;
    texture.pixelFormat0=1027;texture.surfacePool=1;texture.mipData.resize(8);
    append(b,forge::stbbake::serializeInlineTexture(texture));
    const auto vb=b.size();range(b,Bytes(48));const auto ib=b.size();range(b,Bytes{0,0,1,0,2,0});
    const auto trailer=b.size();b.resize(b.size()+85);
    validateBackground(b);
    auto shared=b;shared[9]=1;shared.erase(shared.begin()+ib,shared.begin()+trailer);
    validateBackground(shared);
    auto wrongMode=shared;wrongMode[9]=0;refuses([&]{validateBackground(wrongMode);});
    wrongMode=b;wrongMode[9]=1;refuses([&]{validateBackground(wrongMode);});
    for(size_t n=0;n<b.size();++n)refuses([&]{validateBackground(Bytes(b.begin(),b.begin()+n));});
    suffix=b;suffix.push_back(0);refuses([&]{validateBackground(suffix);});
    auto broken=b;broken[8]=2;refuses([&]{validateBackground(broken);});
    broken=b;broken[16]=0;refuses([&]{validateBackground(broken);});
    broken=b;broken[17+5]=0;refuses([&]{validateBackground(broken);});
    broken=b;broken[vb]=50;broken.insert(broken.begin()+vb+4+49,0);
    refuses([&]{validateBackground(broken);});
    broken=b;broken[trailer+4]=2;refuses([&]{validateBackground(broken);});
    broken=b;broken[trailer+5]=255;refuses([&]{validateBackground(broken);});
    // Both native background water layouts, including water-only framing.
    for(uint32_t type:{1u,3u}) {
        Bytes water(10);water[8]=1;water.resize(10+84);water.push_back(1);
        half(water,3);half(water,1);word(water,type);word(water,type==1?56:12);
        range(water,Bytes(3*(type==1?56:12)));range(water,Bytes{0,0,1,0,2,0});
        validateBackground(water);
        water[10+85+8]=99;refuses([&]{validateBackground(water);});
    }
    // Every edge-array variant is range-framed and geometry-only.
    Bytes edges(10);edges[8]=1;
    for(int side=0;side<4;++side) {
        edges.resize(edges.size()+5);
        for(size_t stride:{20,16,60,56}) {word(edges,1);range(edges,Bytes(stride));}
    }
    edges.push_back(0);validateBackground(edges);
    Bytes detail;word(detail,1);word(detail,1);word(detail,15360);word(detail,3);
    word(detail,0);detail.resize(detail.size()+40+52);
    word(detail,1);detail.resize(detail.size()+40);word(detail,1);detail.resize(detail.size()+4+32);
    detail.push_back(1);detail.resize(detail.size()+4*12);detail.push_back(1);detail.push_back(7);
    detail.push_back(1);word(detail,1);detail.resize(detail.size()+76);word(detail,15360); // unused Pad bytes
    const auto spriteKind=detail.size();word(detail,2);detail.resize(detail.size()+40);word(detail,1);
    detail.resize(detail.size()+68);word(detail,15360);detail.resize(detail.size()+12);
    if(validateLocalDetail(detail)!=std::vector<uint32_t>{15360})throw std::runtime_error("local palette index changed");
    for(size_t n=0;n<detail.size();++n)refuses([&]{validateLocalDetail(Bytes(detail.begin(),detail.begin()+n));});
    auto badDetail=detail;badDetail.push_back(0);refuses([&]{validateLocalDetail(badDetail);});
    badDetail=detail;badDetail[spriteKind]=3;refuses([&]{validateLocalDetail(badDetail);});
    badDetail=detail;badDetail[4]=2;refuses([&]{validateLocalDetail(badDetail);});
    badDetail=detail;badDetail[0]=255;refuses([&]{validateLocalDetail(badDetail);});
    if(!validateLocalDetail(Bytes(4)).empty())throw std::runtime_error("empty detail frame changed");
    Bytes sea;word(sea,3);word(sea,3);range(sea,Bytes(36));range(sea,Bytes{0,0,1,0,2,0});
    validateSea(sea);validateSea(Bytes(8));
    for(size_t n=0;n<sea.size();++n)refuses([&]{validateSea(Bytes(sea.begin(),sea.begin()+n));});
    auto badSea=sea;badSea.push_back(0);refuses([&]{validateSea(badSea);});
    badSea=sea;for(size_t i=0;i<4;++i)badSea[i]=255;refuses([&]{validateSea(badSea);});
    badSea=sea;badSea[8]=38;badSea.insert(badSea.begin()+12+37,0);refuses([&]{validateSea(badSea);});
    // Independent wire-order common record, with one numeric asset palette row.
    Bytes record;
    for(uint32_t v:{1u,15360u,0u,16u,16u,0u,0u,0u,4096u+92,4096u+113,
                    10u,0u,0u,0u,0x12345678u,0x76543210u,0u,0u,0u,0u,0u,0u,4096u+238})word(record,v);
    word(record,4096+112);word(record,1);word(record,2048);word(record,4096);word(record,100);
    record.push_back(1);word(record,4096+125);word(record,113);word(record,1);
    record.resize(record.size()+44);record.push_back(1);word(record,1);
    word(record,15360);word(record,15361);word(record,15362);record.resize(record.size()+52);
    record.back()=0xab; // PDB padding is retained and ignored by the native reader.
    const auto reviewed=validateMapRecord(record,4096);
    if(reviewed.detailPaletteCount!=1 || reviewed.detailRootInRecord!=125 ||
       reviewed.info.bankFileIndex!=15360 || uint32_t(reviewed.info.checksumBlockFilePtr)!=0x12345678u)
        throw std::runtime_error("common record fields changed");
    for(size_t n=0;n<record.size();++n)refuses([&]{validateMapRecord(Bytes(record.begin(),record.begin()+n),4096);});
    refuses([&]{validateMapRecord(record,4097);});
    auto malformed=record;malformed[0]=2;refuses([&]{validateMapRecord(malformed,4096);});
    malformed=record;malformed[32]=0;refuses([&]{validateMapRecord(malformed,4096);});
    malformed=record;malformed[112]=0;refuses([&]{validateMapRecord(malformed,4096);});
    malformed=record;malformed[121]=2;refuses([&]{validateMapRecord(malformed,4096);});
    malformed=record;malformed[122]=1;refuses([&]{validateMapRecord(malformed,4096);});
    malformed=record;malformed[117]=0;refuses([&]{validateMapRecord(malformed,4096);});
    malformed=record;malformed[169]=0;refuses([&]{validateMapRecord(malformed,4096);});
    malformed=record;malformed[170]=2;refuses([&]{validateMapRecord(malformed,4096);});
    malformed=record;malformed[222]=3;refuses([&]{validateMapRecord(malformed,4096);});
    malformed=record;malformed[230]=2;refuses([&]{validateMapRecord(malformed,4096);});
    // Load/Save 02E46970/02E49ED0 use main, shadow, z-sprite on disk.
    // Distinct IDs catch a symmetric reader/writer swap; padding must survive.
    auto palette=forge::foliage::readPalette(record.data(),record.size(),169);
    const auto& entry=palette.entries.at(0);
    if(entry.meshIdx!=15360 || entry.shadowMeshIdx!=15361 || entry.zspriteMeshIdx!=15362 ||
       entry.runtimeSettings[12]!=0xab000000u)
        throw std::runtime_error("native foliage palette roles/settings lost");
    forge::stbbake::LocalDetailQuadHeader root;root.cellBounds[2]=root.cellBounds[3]=16;
    const auto emit=[&](const std::vector<forge::foliage::FoliageType>& types) {
        return forge::stbbake::buildType0LocalDetailSection(0,2048,root,types,{{}},{});
    };
    const auto rewritten=emit(palette.entries);
    if(!std::equal(record.begin()+169,record.end(),rewritten.bytes.begin()+rewritten.headerOffset+44))
        throw std::runtime_error("foliage palette wire bytes changed");
    forge::foliage::FoliageType authored;
    authored.meshIdx=101;authored.shadowMeshIdx=202;authored.zspriteMeshIdx=303;
    const auto generated=emit({authored});
    Bytes expected{1};word(expected,1);word(expected,101);word(expected,202);word(expected,303);
    expected.resize(69);
    if(!std::equal(expected.begin(),expected.end(),generated.bytes.begin()+generated.headerOffset+44))
        throw std::runtime_error("authored foliage palette roles reversed");
    const auto& tree=forge::foliage::brushCatalog().at(14);
    if(tree.meshIdx!=7734 || tree.shadowMeshIdx!=7741 || tree.zspriteMeshIdx!=7734)
        throw std::runtime_error("seed palette native role order changed");
    auto setWord=[](Bytes& b,size_t at,uint32_t v) {
        for(unsigned i=0;i<4;++i)b.at(at+i)=uint8_t(v>>(8*i));
    };
    auto relocateRecord=[&](uint32_t base,uint32_t chunk) {
        auto copy=record;
        for(const auto [at,relative]:{std::pair<size_t,uint32_t>{32,92},{36,113},
                                     {88,238},{92,112},{113,125}})
            setWord(copy,at,base+relative);
        setWord(copy,4,chunk);return copy;
    };
    // Manifest order A,B differs from physical order B,A.
    Bytes common;word(common,2);common.push_back('A');common.push_back(0);word(common,254);
    common.push_back('B');common.push_back(0);word(common,16);
    append(common,relocateRecord(16,7));append(common,relocateRecord(254,8));
    const auto commonOriginal=common;const auto maps=validateCommon(common);
    if(common!=commonOriginal || maps.size()!=2 || maps[0].name!="A" || maps[1].name!="B" ||
       maps[0].logicalOffset!=254 || maps[0].record.info.bankFileIndex!=8 ||
       maps[1].logicalOffset!=16 || maps[1].record.info.bankFileIndex!=7)
        throw std::runtime_error("common manifest order or ownership changed");
    for(size_t n=0;n<common.size();++n)refuses([&]{validateCommon(Bytes(common.begin(),common.begin()+n));});
    if(!validateCommon(Bytes(4)).empty())throw std::runtime_error("empty common manifest changed");
    auto badCommon=common;badCommon.push_back(0);refuses([&]{validateCommon(badCommon);});
    badCommon=common;setWord(badCommon,0,0xffffffffu);refuses([&]{validateCommon(badCommon);});
    badCommon=common;badCommon[4]=0;refuses([&]{validateCommon(badCommon);});
    badCommon=common;badCommon[10]='A';refuses([&]{validateCommon(badCommon);});
    for(uint32_t offset:{0u,15u,17u,254u,0xffffffffu}) {
        badCommon=common;setWord(badCommon,12,offset);refuses([&]{validateCommon(badCommon);});
    }
    badCommon=common;setWord(badCommon,16+88,253);refuses([&]{validateCommon(badCommon);});
    badCommon=common;setWord(badCommon,254+4,7);refuses([&]{validateCommon(badCommon);});
    refuses([&]{validateCommon(Bytes{1,0,0,0,'n','o','n','u','l',1,1,1,1});});
    std::cout<<"terrain storage checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
