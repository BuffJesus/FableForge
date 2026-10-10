#include "forge/stbstorage.hpp"
#include "forge/stbbake.hpp"
#include "forge/rangecodec.hpp"
#include "forge/stb.hpp"
#include "forge/lzo.hpp"
#include "forge/temporarydirectory.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
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
int main(int argc,char** argv) {try {
    if(argc==2) {
        const auto r=forge::stbstorage::validateBankStorage(argv[1]);
        const nlohmann::json report={{"entries",r.entries},{"maps",r.maps},{"seas",r.seas},
            {"palette_rows",r.paletteRows},{"bank_zero_padding_bytes",r.zeroBankPaddingBytes},
            {"foreground_frames",r.chunks.foregroundFrames},{"background_frames",r.chunks.backgroundFrames},
            {"detail_frames",r.chunks.detailFrames},{"decoded_bytes",r.chunks.decodedBytes},
            {"chunk_zero_padding_bytes",r.chunks.zeroPaddingBytes}};
        std::cout<<report.dump(2)<<'\n';return 0;
    }
    if(argc!=1)return 2;
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
    // Independent chunk wiring: one foreground row, one background LOD and
    // one local-detail group. Legacy generation pointers deliberately remain
    // outside this chunk. Frame owners, rather than byte scanning, drive decode.
    Bytes chunkRecord=record,chunk(1024);chunk[0]=1;
    setWord(chunkRecord,100,32);setWord(chunkRecord,104,80);setWord(chunkRecord,108,60);
    setWord(chunkRecord,125+24,160);setWord(chunkRecord,125+28,60);
    auto placeFrame=[&](Bytes& target,size_t at,const Bytes& body) {
        const auto encoded=forge::lzo::compressFramed(body.data(),body.size());
        if(at+encoded.size()>target.size())throw std::runtime_error("fixture frame too large");
        std::copy(encoded.begin(),encoded.end(),target.begin()+at);return uint32_t(encoded.size());
    };
    const auto fgSize=placeFrame(chunk,256,dry);
    setWord(chunk,32,256);setWord(chunk,36,fgSize);
    Bytes backgroundBody(95);backgroundBody[8]=1; // water-only with absent water
    const auto bgSize=placeFrame(chunk,512,backgroundBody);
    forge::stbbake::BackgroundTreeHeader backgroundHeader;
    backgroundHeader.width=backgroundHeader.height=16;
    backgroundHeader.firstBand=backgroundHeader.firstNonSplitBand=backgroundHeader.lastBand=1;
    backgroundHeader.lod.push_back({0,512,int32_t(bgSize),0});
    const auto backgroundBytes=forge::stbbake::serializeBackgroundTreeHeader(backgroundHeader);
    std::copy(backgroundBytes.begin(),backgroundBytes.end(),chunk.begin()+80);
    Bytes detailBody;word(detailBody,1);word(detailBody,0);word(detailBody,0);word(detailBody,0);
    const auto detailSize=placeFrame(chunk,768,detailBody);
    setWord(chunk,160,1);setWord(chunk,164,768);setWord(chunk,168,detailSize);
    const auto chunkOriginal=chunk,recordOriginal=chunkRecord;
    const auto chunkResult=validateMapChunk(chunk,chunkRecord,4096);
    if(chunkResult.foregroundFrames!=1 || chunkResult.backgroundFrames!=1 || chunkResult.detailFrames!=1 ||
       chunkResult.decodedBytes!=dry.size()+backgroundBody.size()+detailBody.size() ||
       chunkResult.zeroPaddingBytes!=1024-1-36-60-60-fgSize-bgSize-detailSize ||
       chunk!=chunkOriginal || chunkRecord!=recordOriginal)
        throw std::runtime_error("chunk ownership/count/preservation mismatch");
    // Every prefix cutting a live structure/frame refuses. Trailing zero
    // alignment is optional and therefore deliberately excluded from this loop.
    for(size_t n=0;n<768+detailSize;++n)
        refuses([&]{validateMapChunk(Bytes(chunk.begin(),chunk.begin()+n),chunkRecord,4096);});
    for(const auto [at,value]:{std::pair<size_t,uint32_t>{0,2},{32,0},{36,0},{36,fgSize-1},
        {32,0xffffffffu},{256,0xffffffffu},{260,0xffffffffu},{80+11,80},{80+15,60},
        {80+48,256},{160,0xffffffffu},{164,512},{168,detailSize-1},{204,2},{224,1},{1000,1}}) {
        auto bad=chunk;setWord(bad,at,value);refuses([&]{validateMapChunk(bad,chunkRecord,4096);});
    }
    for(const auto [at,value]:{std::pair<size_t,uint32_t>{100,0},{100,80},{104,32},{108,59},
        {125+24,0},{125+28,59},{125+32,60},{170,0}}) {
        auto bad=chunkRecord;setWord(bad,at,value);refuses([&]{validateMapChunk(chunk,bad,4096);});
    }
    // An out-of-palette index is valid local-detail storage but invalid here.
    auto badPaletteChunk=chunk;setWord(detailBody,8,1);
    setWord(badPaletteChunk,168,placeFrame(badPaletteChunk,768,detailBody));
    refuses([&]{validateMapChunk(badPaletteChunk,chunkRecord,4096);});
    // Sharing the same background frame across LODs is legal and counted once.
    auto sharedChunk=chunk,sharedRecord=chunkRecord;
    backgroundHeader.lastBand=2;backgroundHeader.lod.push_back(backgroundHeader.lod.front());
    const auto sharedHeader=forge::stbbake::serializeBackgroundTreeHeader(backgroundHeader);
    std::copy(sharedHeader.begin(),sharedHeader.end(),sharedChunk.begin()+80);setWord(sharedRecord,108,uint32_t(sharedHeader.size()));
    if(validateMapChunk(sharedChunk,sharedRecord,4096).backgroundFrames!=1)
        throw std::runtime_error("shared background frame counted twice");
    auto conflicting=chunk;setWord(conflicting,128,256);setWord(conflicting,132,fgSize);
    refuses([&]{validateMapChunk(conflicting,chunkRecord,4096);});
    // A two-child background tree exercises owner-block ends and geometry
    // partitioning independently of the frame storage checks.
    auto splitChunk=chunk,splitRecord=chunkRecord;splitChunk.resize(2048);
    std::copy(chunk.begin()+32,chunk.begin()+68,splitChunk.begin()+1100);
    std::fill(splitChunk.begin()+32,splitChunk.begin()+68,0);
    setWord(splitRecord,12,32);setWord(splitRecord,100,1100);
    auto splitHeader=backgroundHeader;splitHeader.width=32;splitHeader.lastBand=splitHeader.firstNonSplitBand=2;
    splitHeader.lod.resize(1);splitHeader.fileBlockPos=1280;splitHeader.fileBlockSize=120;
    auto childHeader=backgroundHeader;childHeader.lastBand=1;childHeader.lod.resize(1);
    auto putHeader=[&](size_t at,const auto& h) {
        const auto bytes=forge::stbbake::serializeBackgroundTreeHeader(h);
        std::copy(bytes.begin(),bytes.end(),splitChunk.begin()+at);
    };
    putHeader(80,splitHeader);putHeader(1280,childHeader);childHeader.mapX=16;putHeader(1340,childHeader);
    if(validateMapChunk(splitChunk,splitRecord,4096).backgroundFrames!=1)
        throw std::runtime_error("background child owners changed frame count");
    for(const auto [at,value]:{std::pair<size_t,uint32_t>{80+15,119},{1340,17},{1280+11,1280}}) {
        auto bad=splitChunk;setWord(bad,at,value);
        if(at==1280+11)setWord(bad,1280+15,120);
        refuses([&]{validateMapChunk(bad,splitRecord,4096);});
    }
    // A present detail child may have no table. Re-entering the parent's table
    // must refuse before recursion can loop or reinterpret its bytes.
    auto childChunk=chunk,childRecord=chunkRecord;
    // Move the foreground frame away from the expanded detail table.
    std::copy(chunk.begin()+256,chunk.begin()+256+fgSize,childChunk.begin()+900);
    std::fill(childChunk.begin()+256,childChunk.begin()+256+fgSize,0);
    setWord(childChunk,32,900);setWord(childChunk,204,1);setWord(childRecord,125+28,104);
    validateMapChunk(childChunk,childRecord,4096);
    setWord(childChunk,208+24,160);setWord(childChunk,208+28,104);
    refuses([&]{validateMapChunk(childChunk,childRecord,4096);});
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
    // BBB's header +20 is maximum entry ID, not live count + 1. Sparse
    // source IDs must remain addressable after one or several map appends.
    Bytes bank;
    for(uint32_t v:{0x42424242u,115u,174u,1u,64u,31u,3u,0u})word(bank,v);
    auto alignBank=[&]{bank.resize((bank.size()+63)/64*64);};
    alignBank();const auto chunkAt=uint32_t(bank.size());bank.resize(bank.size()+8,0x5a);
    alignBank();const auto secondChunkAt=uint32_t(bank.size());bank.resize(bank.size()+8,0x5a);
    alignBank();const auto commonAt=uint32_t(bank.size());append(bank,common);
    alignBank();const auto tableAt=uint32_t(bank.size());setWord(bank,28,tableAt);
    word(bank,1);word(bank,0);word(bank,3);
    auto sizedString=[&](const std::string& s){word(bank,uint32_t(s.size()));bank.insert(bank.end(),s.begin(),s.end());};
    auto entryRow=[&](uint32_t id,const std::string& name,uint32_t at,uint32_t length) {
        for(uint32_t v:{42u,id,0u,length,at,0u})word(bank,v);
        sizedString(name);word(bank,0);word(bank,1);sizedString(name);word(bank,0);
    };
    entryRow(7,"B",chunkAt,8);entryRow(8,"A",secondChunkAt,8);
    entryRow(31,"__STATIC_MAP_COMMON_HEADER__",commonAt,uint32_t(common.size()));
    const Bytes header(bank.begin(),bank.begin()+32),table(bank.begin()+tableAt,bank.end());
    const auto metadata=validateBankMetadata(header,table,bank.size());
    if(metadata.maximumId!=31 || metadata.entries.size()!=3 || metadata.entries[0].entry.devName!="B")
        throw std::runtime_error("native bank metadata changed");
    for(size_t n=0;n<header.size();++n)
        refuses([&]{validateBankMetadata(std::span(header).first(n),table,bank.size());});
    for(size_t n=0;n<table.size();++n)
        refuses([&]{validateBankMetadata(header,std::span(table).first(n),tableAt+n);});
    for(const auto [at,value]:{std::pair<size_t,uint32_t>{0,0},{4,114},{8,173},{12,0},
                              {16,0},{16,3},{20,30},{24,4},{28,0xffffffffu}}) {
        auto bad=header;setWord(bad,at,value);refuses([&]{validateBankMetadata(bad,table,bank.size());});
    }
    for(const auto [at,value]:{std::pair<size_t,uint32_t>{0,2},{4,1},{8,4},{12,41},
                              {16,0},{16,32},{20,1},{24,0xffffffffu},{28,65},
                              {36,0xffffffffu},{45,0},{45,2},{58+4,7},{58+16,64}}) {
        auto bad=table;setWord(bad,at,value);refuses([&]{validateBankMetadata(header,bad,tableAt+bad.size());});
    }
    auto suffixTable=table;suffixTable.push_back(0);
    refuses([&]{validateBankMetadata(header,suffixTable,tableAt+suffixTable.size());});
    // Independent sea metadata uses a length-prefixed 12-byte native info block.
    Bytes seaTable;word(seaTable,1);word(seaTable,0);word(seaTable,1);
    for(uint32_t v:{42u,1u,0u,8u,64u,0x12345678u})word(seaTable,v);
    const std::string seaName="__ENGINE_SEA_STATIC_MAP_BANK_FILE__test";
    auto seaString=[&]{word(seaTable,uint32_t(seaName.size()));seaTable.insert(seaTable.end(),seaName.begin(),seaName.end());};
    seaString();word(seaTable,0x87654321u);word(seaTable,1);seaString();word(seaTable,16);
    const auto seaInfoAt=seaTable.size();for(uint32_t v:{12u,22u,0u,0xfedcba98u})word(seaTable,v);
    auto seaHeader=header;setWord(seaHeader,20,1);setWord(seaHeader,24,1);setWord(seaHeader,28,128);
    const auto seaMetadata=validateBankMetadata(seaHeader,seaTable,128+seaTable.size());
    if(!seaMetadata.entries[0].sea || seaMetadata.entries[0].seaCutoutCRC!=0xfedcba98u ||
       seaMetadata.entries[0].checksum!=0x87654321u || seaMetadata.entries[0].entry.crc!=0x12345678u)
        throw std::runtime_error("sea bank metadata fields lost");
    for(const auto [at,value]:{std::pair<size_t,uint32_t>{seaInfoAt,11},{seaInfoAt+4,21},
                              {seaInfoAt+8,1},{seaInfoAt-4,15}}) {
        auto bad=seaTable;setWord(bad,at,value);refuses([&]{validateBankMetadata(seaHeader,bad,128+bad.size());});
    }
    forge::TemporaryDirectory scratch(std::filesystem::temp_directory_path(),"stb-sparse-append-");
    // A complete compact bank, with physical order differing from TOC order.
    // Build the BBB envelope independently of the production archive writer.
    Bytes completeCommon;word(completeCommon,1);completeCommon.push_back('A');completeCommon.push_back(0);word(completeCommon,10);
    auto completeRecord=chunkRecord;
    for(const auto [at,relative]:{std::pair<size_t,uint32_t>{32,92},{36,113},{88,238},{92,112},{113,125}})
        setWord(completeRecord,at,10+relative);
    setWord(completeRecord,4,7);append(completeCommon,completeRecord);
    Bytes compact;
    for(uint32_t v:{0x42424242u,115u,174u,1u,64u,31u,3u,0u})word(compact,v);
    compact.resize(64);const uint32_t mapAt=uint32_t(compact.size());append(compact,chunk);
    const uint32_t seaAt=uint32_t(compact.size());compact.resize(compact.size()+8);
    compact.resize((compact.size()+63)/64*64);const uint32_t manifestAt=uint32_t(compact.size());append(compact,completeCommon);
    compact.resize((compact.size()+63)/64*64);const uint32_t compactToc=uint32_t(compact.size());setWord(compact,28,compactToc);
    word(compact,1);word(compact,0);word(compact,3);
    auto compactName=[&](const std::string& name){word(compact,uint32_t(name.size()));compact.insert(compact.end(),name.begin(),name.end());};
    auto compactRow=[&](uint32_t id,const std::string& name,uint32_t at,uint32_t length,bool isSea) {
        const size_t row=compact.size();
        for(uint32_t v:{42u,id,0u,length,at,0u})word(compact,v);
        compactName(name);word(compact,0);word(compact,1);compactName(name);word(compact,isSea?16:0);
        if(isSea)for(uint32_t v:{12u,22u,0u,123u})word(compact,v);
        return row;
    };
    compactRow(31,"__STATIC_MAP_COMMON_HEADER__",manifestAt,uint32_t(completeCommon.size()),false);
    const auto mapRow=compactRow(7,"A",mapAt,uint32_t(chunk.size()),false);
    const auto seaRow=compactRow(8,seaName,seaAt,8,true);
    const auto compactPath=scratch.path()/"compact.stb";
    auto writeCompact=[&](const Bytes& bytes) {
        std::ofstream out(compactPath,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
        if(!out)throw std::runtime_error("cannot write compact test fixture");
    };
    writeCompact(compact);const auto complete=validateBankStorage(compactPath);
    if(complete.entries!=3 || complete.maps!=1 || complete.seas!=1 || complete.paletteRows!=1 ||
       complete.chunks.foregroundFrames!=1 || complete.chunks.backgroundFrames!=1 || complete.chunks.detailFrames!=1 ||
       complete.zeroBankPaddingBytes!=compactToc-32-chunk.size()-8-completeCommon.size())
        throw std::runtime_error("whole bank ownership mismatch");
    {std::ifstream in(compactPath,std::ios::binary);const Bytes after((std::istreambuf_iterator<char>(in)),{});
     if(after!=compact)throw std::runtime_error("whole bank review changed input");}
    for(const auto [at,value]:{std::pair<size_t,uint32_t>{32,1},{seaAt+8,1},{compactToc-4,1},
        {manifestAt+10+4,8},{mapRow+28,'B'},{mapRow+4,9},{seaAt,1},{mapAt+1000,1}}) {
        auto bad=compact;setWord(bad,at,value);writeCompact(bad);
        refuses([&]{validateBankStorage(compactPath);});
    }
    // A metadata-valid extra non-sea entry remains unowned and must refuse.
    auto unowned=compact;unowned[seaRow+28]='X';
    const size_t seaInfoSize=seaRow+40+2*seaName.size();
    setWord(unowned,seaInfoSize,0);unowned.resize(unowned.size()-16);writeCompact(unowned);
    refuses([&]{validateBankStorage(compactPath);});
    for(size_t n:{size_t(0),size_t(31),size_t(compactToc),compact.size()-1}) {
        writeCompact(Bytes(compact.begin(),compact.begin()+n));refuses([&]{validateBankStorage(compactPath);});
    }
    refuses([&]{validateBankStorage(scratch.path()/"missing.stb");});
    const auto source=scratch.path()/"source.stb",destination=scratch.path()/"appended.stb";
    {std::ofstream out(source,std::ios::binary);out.write(reinterpret_cast<const char*>(bank.data()),bank.size());}
    for(size_t added:{size_t(1),size_t(2)}) {
        std::vector<forge::stb::StaticMapAppend> additions;
        for(size_t i=0;i<added;++i)additions.push_back({"new"+std::to_string(i),"new"+std::to_string(i),Bytes(8,0xa5),record});
        forge::stb::appendStaticMaps(source,destination,additions);
        std::ifstream in(destination,std::ios::binary);const Bytes output((std::istreambuf_iterator<char>(in)),{});
        auto prefix=Bytes(output.begin(),output.begin()+bank.size());
        for(size_t at:{20,24,28})std::copy_n(bank.begin()+at,4,prefix.begin()+at);
        if(prefix!=bank)throw std::runtime_error("append changed original bank storage");
        const auto archive=forge::stb::Archive::open(destination);
        const auto tableOffset=archive.tableOffset();
        validateBankMetadata(std::span(output).first(32),std::span(output).subspan(tableOffset),output.size());
        const auto* commonEntry=archive.findEntry("__STATIC_MAP_COMMON_HEADER__");
        const auto appendedMaps=validateCommon(archive.read(*commonEntry));
        if(appendedMaps.size()!=2+added || archive.entries().back().id!=31+added ||
           output.at(20)!=31+added || output.at(21) || output.at(22) || output.at(23))
            throw std::runtime_error("append maximum entry ID does not cover sparse entries");
        for(size_t i=0;i<added;++i) {
            const auto* entry=archive.findEntry("new"+std::to_string(i));
            if(!entry || entry->id!=32+i || archive.read(*entry)!=Bytes(8,0xa5))
                throw std::runtime_error("sparse appended payload lost");
        }
    }
    std::cout<<"terrain storage checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
