#include "forge/stbstorage.hpp"
#include "forge/stbbake.hpp"
#include "forge/lzo.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>

namespace forge::stbstorage {
namespace {
using Bytes=std::vector<uint8_t>;
void require(bool ok,const char* message) {
    if(!ok)throw std::runtime_error(std::string("stbstorage: ")+message);
}
void bounds(const Bytes& b,size_t at,size_t n) {
    require(at<=b.size() && n<=b.size()-at,"chunk span out of bounds");
}
uint32_t word(const Bytes& b,size_t at) {
    bounds(b,at,4);
    return uint32_t(b[at])|uint32_t(b[at+1])<<8|uint32_t(b[at+2])<<16|uint32_t(b[at+3])<<24;
}
struct Walk {
    const Bytes& raw;
    enum Kind { Foreground,Background,Detail };
    std::map<size_t,Kind> frames;
    std::map<size_t,size_t> spans;
    std::set<size_t> backgroundNodes,detailTables;
    void own(size_t at,size_t n) {
        bounds(raw,at,n);require(n!=0,"empty owned span");
        auto next=spans.lower_bound(at);
        require(next==spans.end() || at+n<=next->first,"overlapping chunk structures");
        require(next==spans.begin() || std::prev(next)->second<=at,"overlapping chunk structures");
        spans.emplace(at,at+n);
    }
    std::pair<size_t,size_t> block(uint32_t pos,uint32_t size,uint32_t offset) const {
        if(!pos && !size && !offset)return {0,0};
        require(pos && size && offset<size,"invalid chunk block triple");
        // Native triples are signed SLONGs.
        require(pos<=INT32_MAX && size<=INT32_MAX && offset<=INT32_MAX,"negative chunk block triple");
        bounds(raw,pos,size);return {size_t(pos)+offset,size_t(pos)+size};
    }
    std::pair<size_t,size_t> block(const Bytes& b,size_t at) const {
        return block(word(b,at),word(b,at+4),word(b,at+8));
    }
    size_t frame(size_t at,size_t limit,Kind kind) {
        require(at && at<=limit && limit<=raw.size() && limit-at>=8,"invalid frame owner span");
        const size_t compressed=word(raw,at+4);
        require(compressed && compressed<=limit-at-8,"frame exceeds owner block");
        const size_t length=8+compressed;
        const auto [it,inserted]=frames.emplace(at,kind);
        require(it->second==kind,"frame has conflicting storage domains");
        if(inserted)own(at,length);
        return length;
    }
    stbbake::BackgroundTreeHeader background(size_t at,size_t limit,size_t depth) {
        require(depth<=32,"background tree depth exceeds supported limit");
        require(at && at<=limit && limit<=raw.size(),"background header exceeds owner block");
        require(backgroundNodes.insert(at).second,"background tree cycle or reused header");
        size_t n=0;const auto h=stbbake::parseBackgroundTreeHeader(raw,at,&n);
        require(n<=limit-at,"background header exceeds owner block");own(at,n);
        require(h.firstNonSplitBand<=8,"unknown background band");
        const uint32_t right=uint32_t(h.mapX)+h.width,bottom=uint32_t(h.mapY)+h.height;
        require(right<=65536 && bottom<=65536,"background rectangle overflow");
        for(const auto& lod:h.lod) {
            const auto [offset,end]=block(uint32_t(lod.fileBlockPos),uint32_t(lod.fileBlockSize),uint32_t(lod.offsetIntoFileBlock));
            if(offset)frame(offset,end,Background);
        }
        const auto [children,end]=block(uint32_t(h.fileBlockPos),uint32_t(h.fileBlockSize),uint32_t(h.offsetIntoFileBlock));
        if(children) {
            const auto a=background(children,end,depth+1);
            const size_t second=children+47+13*a.lod.size();
            const auto b=background(second,end,depth+1);
            const bool splitX=a.mapX==h.mapX && a.mapY==h.mapY && b.mapY==h.mapY &&
                a.height==h.height && b.height==h.height && uint32_t(a.mapX)+a.width==b.mapX && uint32_t(b.mapX)+b.width==right;
            const bool splitY=a.mapX==h.mapX && a.mapY==h.mapY && b.mapX==h.mapX &&
                a.width==h.width && b.width==h.width && uint32_t(a.mapY)+a.height==b.mapY && uint32_t(b.mapY)+b.height==bottom;
            require(splitX || splitY,"background children do not partition parent");
        }
        return h;
    }
    void detail(const Bytes& headers,size_t at,size_t depth,bool inChunk) {
        require(depth<=32,"detail tree depth exceeds supported limit");bounds(headers,at,44);
        if(inChunk)own(at,44);
        auto [cursor,end]=block(headers,at+24);
        if(!cursor)return;
        require(detailTables.insert(cursor).second,"detail table cycle or reuse");
        require(end-cursor>=4,"truncated detail group count");
        const size_t groups=word(raw,cursor);own(cursor,4);cursor+=4;
        require(groups<=(end-cursor)/40,"detail groups exceed owner block");
        if(groups)own(cursor,groups*40);
        for(size_t i=0;i<groups;++i,cursor+=40) {
            const auto [offset,limit]=block(raw,cursor);
            frame(offset,limit,Detail);
        }
        // SaveHeader/SaveFileBlock: four U32 presence words and optional
        // 44-byte child headers, whose tables are in their own file blocks.
        for(int i=0;i<4;++i) {
            require(end-cursor>=4,"truncated detail child flag");
            const auto present=word(raw,cursor);own(cursor,4);cursor+=4;
            require(present<=1,"invalid detail child flag");
            if(present) {
                require(end-cursor>=44,"detail child header exceeds owner block");
                detail(raw,cursor,depth+1,true);cursor+=44;
            }
        }
    }
};
}
ChunkStorage validateMapChunk(const Bytes& raw,const Bytes& record,uint32_t logicalOffset) {
    const auto checked=validateMapRecord(record,logicalOffset);const auto& info=checked.info;
    require(!raw.empty() && raw[0]==1,"unknown retail chunk discriminator");
    Walk walk{raw};walk.own(0,1);
    // Native 02CC7860 reads exactly (width/16)*(height/16) 36-byte rows.
    const uint64_t count=uint64_t(info.mapWidth/16)*uint64_t(info.mapHeight/16);
    const size_t directory=checked.foregroundDirectory;
    bounds(raw,directory,0);require(count<=(raw.size()-directory)/36,"foreground directory exceeds chunk");
    walk.own(directory,size_t(count)*36);
    for(size_t i=0;i<count;++i) {
        const size_t at=directory+i*36,offset=word(raw,at),span=word(raw,at+4);
        if(!offset && !span)continue;
        require(offset && span,"incomplete foreground frame reference");bounds(raw,offset,span);
        float box[6];std::memcpy(box,raw.data()+at+8,24);
        for(int axis=0;axis<3;++axis)
            require(std::isfinite(box[axis]) && std::isfinite(box[axis+3]) && box[axis]<=box[axis+3],"invalid foreground bounds");
        require(walk.frame(offset,offset+span,Walk::Foreground)==span,"foreground frame span mismatch");
    }
    bounds(raw,checked.backgroundRoot,checked.backgroundSpan);
    const auto root=walk.background(checked.backgroundRoot,size_t(checked.backgroundRoot)+checked.backgroundSpan,0);
    require(root.mapX==0 && root.mapY==0 && root.width==info.mapWidth && root.height==info.mapHeight,
            "background root differs from common rectangle");
    const size_t descriptor=size_t(info.localDetailMapPtr)-logicalOffset;
    if(record.at(descriptor+8))walk.detail(record,checked.detailRootInRecord,0,false);
    ChunkStorage result;
    size_t end=0;
    auto padding=[&](size_t stop) {
        require(std::all_of(raw.begin()+end,raw.begin()+stop,[](uint8_t b){return b==0;}),"nonzero unowned chunk bytes");
        result.zeroPaddingBytes+=stop-end;
    };
    for(const auto& [at,stop]:walk.spans){padding(at);end=stop;}
    padding(raw.size());
    for(const auto& [at,kind]:walk.frames) {
        // Resource bound for this supported subset, checked before allocation.
        require(word(raw,at)<=64000000,"decoded terrain frame exceeds supported limit");
        size_t next=at;const auto body=lzo::decompressFramed(raw,next);
        result.decodedBytes+=body.size();
        if(kind==Walk::Foreground){validateForeground(body);++result.foregroundFrames;}
        else if(kind==Walk::Background){validateBackground(body);++result.backgroundFrames;}
        else {
            for(uint32_t index:validateLocalDetail(body))
                require(index<checked.detailPaletteCount,"detail collection exceeds common palette");
            ++result.detailFrames;
        }
    }
    return result;
}
}
