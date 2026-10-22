#include "forge/stbstorage.hpp"
#include "forge/stbbake.hpp"
#include "forge/rangecodec.hpp"
#include <algorithm>
#include <numeric>
#include <set>
#include <span>
#include <stdexcept>

namespace forge::stbstorage {
namespace {
struct Reader {
    std::span<const uint8_t> b;
    size_t p=0;
    size_t take(size_t n) {
        if(n>b.size()-p)throw std::runtime_error("stbstorage: truncated field");
        const auto at=p;p+=n;return at;
    }
    uint8_t byte() {return b[take(1)];}
    uint16_t half() {const auto at=take(2);return uint16_t(b[at])|uint16_t(b[at+1])<<8;}
    uint32_t word() {
        const auto at=take(4);
        return uint32_t(b[at])|uint32_t(b[at+1])<<8|uint32_t(b[at+2])<<16|uint32_t(b[at+3])<<24;
    }
    bool flag() {
        const auto v=byte();if(v>1)throw std::runtime_error("stbstorage: invalid boolean");return v!=0;
    }
    void range(size_t count,size_t stride) {
        const size_t n=word(),at=take(n);
        if(count>1000000 || stride>128 || count*stride>64000000)
            throw std::runtime_error("stbstorage: excessive range array");
        (void)rangecodec::decodeExact(b.data()+at,n,count,stride);
    }
    void finish() const {
        if(p!=b.size())throw std::runtime_error("stbstorage: trailing frame bytes");
    }
    void foregroundWater() {
        // CEngineWaterSubPatch::Load 02E673B0: coords, height, type and
        // 17x17 packed water vertices (PDB stride 66, numeric wave/depth fields).
        take(12);if(word()>8)throw std::runtime_error("stbstorage: unknown foreground water type");
        range(289,66);
    }
    void backgroundTrailer() {
        // CPatchTesselationEdgeStrip::Load 02E03980 and array helpers
        // 02E045F0/02E046F0/02E047F0/02E048F0.
        for(int side=0;side<4;++side) {
            take(4);flag();
            for(const size_t stride:{20,16,60,56}) {
                const auto count=word();if(count)range(count,stride);
            }
        }
        if(flag()) {
            const auto vertices=half(),triangles=half();
            const auto type=word(),stride=word();
            // Background water Load/Save 02E052B0/02E055E0.
            const size_t expected=(type==1||type==2||type==6||type==8)?56:(type>=3&&type<=5)?12:0;
            if(!expected || stride!=expected)throw std::runtime_error("stbstorage: unknown background water layout");
            range(vertices,stride);if(triangles)range(size_t(triangles)*3,2);
        }
    }
};
}
void validateForeground(const std::vector<uint8_t>& body) {
    const auto frame=stbbake::parseForegroundFrame(body);
    if(stbbake::serializeForegroundFrame(frame)!=body)
        throw std::runtime_error("stbstorage: foreground storage is not exact");
    Reader water{frame.waterPayload};
    if(frame.hasWater)water.foregroundWater();
    water.finish();
}
void validateBackground(const std::vector<uint8_t>& body) {
    // Native Load/Save 02CDF860/02CE3220. After loading vertices, Load
    // sets SharedIndexBuffer to (detailMode == 1), overriding the constructor.
    Reader c{body};c.take(8);const bool waterOnly=c.flag();const auto detailMode=c.byte();
    if(!waterOnly) {
        const auto triangles=c.half(),vertices=c.half();c.take(2);
        if(!c.flag())throw std::runtime_error("stbstorage: unsupported uncompressed inline texture");
        const auto textureStart=c.p;
        const auto width=c.half(),height=c.half();
        if(!width || !height || width%4 || height%4)
            throw std::runtime_error("stbstorage: unsupported inline texture dimensions");
        c.take(15+size_t(width)*height/2);
        const std::vector<uint8_t> bytes(body.begin()+textureStart,body.begin()+c.p);
        const auto texture=stbbake::parseInlineTexture(bytes);
        // CPixelFormatInit byte 0: Type:4, Linear:1, three unused bits.
        // Retail frames may retain nonzero unused bits (e.g. 0x04e3).
        if(texture.levels!=1 || (texture.pixelFormat0 & ~0xe0u)!=1027 || texture.pixelFormat1 ||
           texture.usage || texture.surfacePool!=1 || stbbake::serializeInlineTexture(texture)!=bytes)
            throw std::runtime_error("stbstorage: unsupported inline texture layout (levels="+
                std::to_string(texture.levels)+", format="+std::to_string(texture.pixelFormat0)+
                ", format1="+std::to_string(texture.pixelFormat1)+", usage="+
                std::to_string(texture.usage)+", pool="+std::to_string(texture.surfacePool)+")");
        c.range(vertices,16);if(detailMode!=1)c.range(size_t(triangles)*3,2);
    }
    c.backgroundTrailer();c.finish();
}
std::vector<uint8_t> replaceBackgroundTexture(const std::vector<uint8_t>& body,
    uint16_t width,uint16_t height,const std::vector<uint8_t>& dxt1,bool allowExtent256) {
    validateBackground(body);
    if(body[8])throw std::invalid_argument("stbstorage: water-only patch has no background texture");
    const uint16_t limit=allowExtent256?256:255;
    if(!width || !height || width>limit || height>limit || width%4 || height%4)
        throw std::invalid_argument(allowExtent256
            ? "stbstorage: background texture requires multiples of 4 up to 256"
            : "stbstorage: background texture requires multiples of 4 below 256; native allocation extents are one byte (256 needs TerrainExtent256)");
    if(dxt1.size()!=size_t(width)*height/2)
        throw std::invalid_argument("stbstorage: background DXT1 payload length mismatch");
    const auto oldWidth=uint16_t(body[17])|(uint16_t(body[18])<<8);
    const auto oldHeight=uint16_t(body[19])|(uint16_t(body[20])<<8);
    const size_t oldEnd=36+size_t(oldWidth)*oldHeight/2;
    std::vector<uint8_t> result(body.begin(),body.begin()+36);
    result[14]=uint8_t(width);result[15]=uint8_t(height); // 256 -> 0 (TerrainExtent256)
    result[17]=uint8_t(width);result[18]=uint8_t(width>>8);
    result[19]=uint8_t(height);result[20]=uint8_t(height>>8);
    result.insert(result.end(),dxt1.begin(),dxt1.end());
    result.insert(result.end(),body.begin()+oldEnd,body.end());
    validateBackground(result);
    return result;
}
std::vector<uint32_t> validateLocalDetail(const std::vector<uint8_t>& body) {
    Reader c{body};
    auto count=[&](size_t minimum) {
        const auto n=c.word();
        if(n>(body.size()-c.p)/minimum)throw std::runtime_error("stbstorage: local-detail count exceeds frame");
        return size_t(n);
    };
    const auto collections=count(12);std::vector<uint32_t> paletteIndices;
    paletteIndices.reserve(collections);
    // Collection Load 02E35E70; primitives 02EDD230, 02EDDEB0, 02EDF3D0.
    for(size_t collection=0;collection<collections;++collection) {
        if(c.word()>1)throw std::runtime_error("stbstorage: invalid render-update flag");
        paletteIndices.push_back(c.word());
        const auto primitives=count(44);
        for(size_t primitive=0;primitive<primitives;++primitive) {
            const auto kind=c.word();c.take(40); // AABB and bounding sphere
            if(kind==0) {c.take(52);continue;} // matrix and scale floats
            if(kind>2)throw std::runtime_error("stbstorage: unknown local-detail primitive");
            const auto objects=count(16);
            if(!objects)throw std::runtime_error("stbstorage: empty repeated object array");
            if(kind==2) {
                c.take(objects*68); // CObject matrix, scale, bounding sphere
                // Native allocates CObjectEntry in NEnginePrimitiveManagerRepeatedZSprites:
                // PDB size 16: WorldPos C3DVector + WorldSphereRadius float.
                c.take(objects*16);continue;
            }
            c.take(4+objects*32); // maximum scale and two vector arrays
            if(c.flag())c.take(((objects+3)&~size_t(3))*12); // padded normals
            if(c.flag())c.take(objects); // wind delays
            if(c.flag()) {
                const auto sections=count(80);
                if(!sections)throw std::runtime_error("stbstorage: empty subsection array");
                // Native Save 02EE1D70 writes CSubsectionElement[sections]. PDB:
                // 64 sphere-float bytes, 12 local count/index bytes, four Pad bytes.
                c.take(sections*80);
            }
        }
    }
    c.finish();return paletteIndices;
}
void validateSea(const std::vector<uint8_t>& body) {
    // CWaterSeaGenerator::LoadSea 02E5A8F0 / SaveSea 02E5AA70:
    // two signed counts, then an optional range block for each nonempty array.
    Reader c{body};const auto vertices=c.word(),indices=c.word();
    if(vertices)c.range(vertices,12);
    if(indices)c.range(indices,2);
    c.finish();
}
MapRecord validateMapRecord(const std::vector<uint8_t>& record,uint32_t logicalOffset) {
    if(record.size()<stbinfo::kInfoBlockSize)
        throw std::runtime_error("stbstorage: truncated common record");
    MapRecord result;result.info=stbinfo::readInfoBlock(record.data());const auto& info=result.info;
    if(info.versionID!=1 || info.bankFileIndex<=0 || info.mapWidth<=0 || info.mapHeight<=0 ||
       info.mapWidth%16 || info.mapHeight%16 || info.headerEndPtr<0 ||
       uint64_t(info.headerEndPtr)!=uint64_t(logicalOffset)+record.size())
        throw std::runtime_error("stbstorage: unsupported common record header");
    // ReadMapInfoBlock 02D5C230 / WriteMapInfoBlock 02D67EA0 establish all
    // stored fields. OpenRetailStaticMap 02D5BC40 does not use the development
    // checksum-block fields; do not reinterpret their retained values as IDs.
    const auto encoded=stbinfo::writeInfoBlock(info);
    if(!std::equal(encoded.begin(),encoded.end(),record.begin()))
        throw std::runtime_error("stbstorage: common info did not round-trip");
    Reader c{record};c.take(stbinfo::kInfoBlockSize);
    auto atLogical=[&](uint32_t pointer) {
        if(uint64_t(pointer)!=uint64_t(logicalOffset)+c.p)
            throw std::runtime_error("stbstorage: noncontiguous common record layout");
    };
    atLogical(uint32_t(info.landscapeMapPtr));
    const auto texturePos=c.word(),textureSize=c.word();
    result.foregroundDirectory=c.word();result.backgroundRoot=c.word();result.backgroundSpan=c.word();
    atLogical(texturePos);
    // CEngineTexturePalette::Load 02DEF110: external IDs need only this flag.
    if(textureSize!=1 || !c.flag())throw std::runtime_error("stbstorage: unsupported texture palette");
    atLogical(uint32_t(info.localDetailMapPtr));
    const auto detailPos=c.word();result.detailRootSpan=c.word();
    // OpenStaticMap 02E3B150 reads the complete 12-byte descriptor, then seeks
    // to the root and loads its 44-byte header and collection palette.
    // Retail OpenStaticMap 00BDF010 copies all 12 bytes but uses only the
    // first word for the seek; the final three bytes are opaque padding.
    // Some retail banks retain nonzero bytes there. Preserve, do not normalize.
    c.flag();c.take(3);
    atLogical(detailPos);result.detailRootInRecord=uint32_t(c.p);
    if(result.detailRootSpan!=record.size()-c.p)
        throw std::runtime_error("stbstorage: detail header span mismatch");
    c.take(44); // sphere/fade, mask, file-block triple and local cell rectangle
    const bool numericPalette=c.flag();
    result.detailPaletteCount=c.word();
    // Retail CObjectTypeCollectionPalette::Load reads the flag/count first
    // and never dispatches a type loader when count is zero. Generated empty
    // palettes retain flag=false; populated named palettes remain unsupported.
    if(!numericPalette && result.detailPaletteCount)
        throw std::runtime_error("stbstorage: populated named mesh palette is unsupported");
    if(result.detailPaletteCount>(record.size()-c.p)/64)
        throw std::runtime_error("stbstorage: detail palette exceeds record");
    for(uint32_t i=0;i<result.detailPaletteCount;++i) {
        // Collection type Load 02E46970: three mesh-bank IDs then the PDB
        // 52-byte PlainDataBlock (floats, alpha ref, primitive/cache enums,
        // five booleans and three unused padding bytes).
        c.take(12+36);
        if(c.word()>2)throw std::runtime_error("stbstorage: unknown palette primitive type");
        c.take(4);for(int j=0;j<5;++j)c.flag();c.take(3);
    }
    c.finish();return result;
}
std::vector<CommonMap> validateCommon(const std::vector<uint8_t>& body) {
    // OpenStaticMapsUsingCommonRetailHeader 02D62360 reads a signed count,
    // then ASCIIZ name / signed logical offset pairs before seeking records.
    Reader c{body};const auto count=c.word();
    if(count>(body.size()-c.p)/6)
        throw std::runtime_error("stbstorage: common manifest count exceeds entry");
    std::vector<CommonMap> maps;maps.reserve(count);std::set<std::string> names;
    for(uint32_t i=0;i<count;++i) {
        CommonMap map;
        for(uint8_t ch=c.byte();ch;ch=c.byte())map.name.push_back(char(ch));
        if(map.name.empty() || !names.insert(map.name).second)
            throw std::runtime_error("stbstorage: empty or duplicate common map name");
        map.logicalOffset=c.word();maps.push_back(std::move(map));
    }
    std::vector<size_t> order(maps.size());std::iota(order.begin(),order.end(),0);
    std::sort(order.begin(),order.end(),[&](size_t a,size_t b) {
        return maps[a].logicalOffset<maps[b].logicalOffset;
    });
    size_t next=c.p;std::set<int32_t> chunks;
    for(size_t i=0;i<order.size();++i) {
        auto& map=maps[order[i]];
        const size_t end=i+1<order.size()?maps[order[i+1]].logicalOffset:body.size();
        if(map.logicalOffset!=next || end<=next || end>body.size())
            throw std::runtime_error("stbstorage: common records overlap or leave gaps");
        const std::vector<uint8_t> bytes(body.begin()+next,body.begin()+end);
        map.record=validateMapRecord(bytes,map.logicalOffset);
        if(!chunks.insert(map.record.info.bankFileIndex).second)
            throw std::runtime_error("stbstorage: common records share a chunk ID");
        next=end;
    }
    c.p=next;c.finish();return maps;
}
BankMetadata validateBankMetadata(std::span<const uint8_t> header,
                                  std::span<const uint8_t> table,uint64_t fileSize) {
    // CBankFile ctor 02F8F860 / ReadFileInformation 02F99600; static-map
    // GetDataFormatVersion 02D5AE90. Header word +20 is the maximum index.
    Reader h{header};
    if(h.word()!=0x42424242u || h.word()!=115 || h.word()!=174 || h.word()!=1)
        throw std::runtime_error("stbstorage: unsupported bank header");
    BankMetadata result;result.alignment=h.word();result.maximumId=h.word();
    const auto count=h.word();result.tableOffset=h.word();h.finish();
    const uint64_t toc=result.tableOffset;
    if(!result.alignment || (result.alignment&(result.alignment-1)) ||
       toc<32 || toc%result.alignment || toc>fileSize || fileSize-toc!=table.size())
        throw std::runtime_error("stbstorage: invalid bank alignment or table span");
    Reader c{table};
    // ReadEntryList 02F9A180: data-type histogram followed by live entries.
    if(c.word()!=1 || c.word()!=0 || c.word()!=count || count>(table.size()-c.p)/44)
        throw std::runtime_error("stbstorage: unsupported bank data-type histogram");
    auto name=[&] {
        const auto n=c.word();const auto at=c.take(n);
        if(!n || std::find(table.begin()+at,table.begin()+at+n,0)!=table.begin()+at+n)
            throw std::runtime_error("stbstorage: unsupported empty or embedded-NUL bank name");
        return std::string(reinterpret_cast<const char*>(table.data()+at),n);
    };
    std::set<uint32_t> ids;std::set<std::string> names;
    result.entries.reserve(count);
    for(uint32_t i=0;i<count;++i) {
        if(c.word()!=42)throw std::runtime_error("stbstorage: invalid bank entry magic");
        BankEntry item;auto& entry=item.entry;
        entry.id=c.word();entry.type=c.word();entry.size=c.word();entry.offset=c.word();entry.crc=c.word();
        entry.name=name();item.checksum=c.word();
        if(c.word()!=1)throw std::runtime_error("stbstorage: unsupported bank filename count");
        entry.devName=name();const auto infoSize=c.word();const auto infoAt=c.take(infoSize);
        if(!entry.id || entry.id>result.maximumId || entry.type ||
           !ids.insert(entry.id).second || !names.insert(entry.name).second)
            throw std::runtime_error("stbstorage: invalid or duplicate bank entry identity");
        if(!entry.size || entry.offset<32 || entry.offset%result.alignment ||
           uint64_t(entry.offset)+entry.size>toc)
            throw std::runtime_error("stbstorage: invalid bank payload span");
        item.sea=entry.name.starts_with("__ENGINE_SEA_STATIC_MAP_BANK_FILE__");
        if(item.sea) {
            // SaveSea 02E5AA70 writes the size prefix; the PDB info block is
            // {version, stream position, cutout CRC}, saved by 02E5A5B0.
            Reader info{table.subspan(infoAt,infoSize)};
            if(info.word()!=12 || info.word()!=22 || info.word()!=0)
                throw std::runtime_error("stbstorage: unsupported sea bank info");
            item.seaCutoutCRC=info.word();info.finish();
        } else if(infoSize)throw std::runtime_error("stbstorage: unknown bank entry info");
        result.entries.push_back(std::move(item));
    }
    c.finish();
    std::vector<size_t> order(result.entries.size());std::iota(order.begin(),order.end(),0);
    std::sort(order.begin(),order.end(),[&](size_t a,size_t b) {
        return result.entries[a].entry.offset<result.entries[b].entry.offset;
    });
    uint64_t end=32;
    for(size_t i:order) {
        const auto& entry=result.entries[i].entry;
        if(entry.offset<end)throw std::runtime_error("stbstorage: overlapping bank payloads");
        end=uint64_t(entry.offset)+entry.size;
    }
    return result;
}
}
