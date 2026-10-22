#include "forge/wldmod.hpp"
#include "forge/egocore.hpp"
#include "forge/modchoices.hpp"
#include "forge/tng.hpp"
#include "forge/worldmerge.hpp"
#include "textformat.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

namespace forge::worldmerge {
namespace {
namespace tf=forge::textformat;
std::string lower(std::string text) {
    for(auto& c:text) if(c>='A' && c<='Z') c=char(c-'A'+'a');
    return text;
}
std::string level(std::string name) {
    for(auto& c:name) if(c=='/') c='\\';
    return wldLevelName(name);
}
std::string mapKey(const std::string& name) {return modchoices::filePathKey(level(name));}
std::string quote(const std::string& text) {
    if(text.find_first_of("\"\r\n\0",0,4)!=text.npos) throw std::runtime_error("WLD: unsupported quoted value");
    return '"'+text+'"';
}
std::string number(float value) {
    if(!std::isfinite(value)) throw std::runtime_error("WLD: non-finite layout value");
    std::ostringstream out;out<<std::setprecision(std::numeric_limits<float>::max_digits10)<<value;return out.str();
}
template<class T> T integer(const std::string& text,const std::string& key) {
    T value{};const auto result=std::from_chars(text.data(),text.data()+text.size(),value);
    if(result.ec!=std::errc() || result.ptr!=text.data()+text.size())
        throw std::runtime_error("WLD: invalid or overflowing "+key);
    return value;
}
float real(const std::string& text,const std::string& key,bool offset=false) {
    float value=0;const auto result=std::from_chars(text.data(),text.data()+text.size(),value);
    if(result.ec!=std::errc() || result.ptr!=text.data()+text.size() || !std::isfinite(value) ||
       (offset && (double(value)<-2147483648.0 || double(value)>=2147483648.0)))
        throw std::runtime_error("WLD: invalid or out-of-range "+key);
    return value;
}
struct Block {std::string type,key,raw;int index=0;size_t begin=0,end=0;};
struct Document {std::string text;std::vector<Block> blocks;std::vector<std::string> quests;bool hasQuests=false;};
Document read(const std::string& text) {
    Document doc;doc.text=text;
    std::string active;size_t offset=0,start=0;int id=0;
    std::set<int> mapIds,regionIds;std::set<std::string> names,fields;
    for(const auto& line:tf::splitRawLines(text)) {
        std::string key,value;
        if(!tf::parseLogicalLine(line,key,value) || key.starts_with("//") || key.starts_with('#')) {offset+=line.size();continue;}
        const auto k=lower(key);
        if(k=="newmap" || k=="newregion" || k=="start_initial_quests") {
            if(!active.empty()) throw std::runtime_error("WLD: nested record");
            active=k;start=offset;fields.clear();id=0;
            if(k!="start_initial_quests") {
                id=integer<int>(value,key);
                if(id<=0 || !(k=="newmap"?mapIds:regionIds).insert(id).second)
                    throw std::runtime_error("WLD: duplicate or nonpositive "+key+" ID");
            } else {
                if(doc.hasQuests) throw std::runtime_error("WLD: duplicate initial quest block");
                doc.hasQuests=true;
            }
        } else if(k=="endmap" || k=="endregion" || k=="end_initial_quests") {
            const auto expected=k=="endmap"?"newmap":k=="endregion"?"newregion":"start_initial_quests";
            if(active!=expected || !value.empty()) throw std::runtime_error("WLD: unmatched or malformed record end");
            if(active!="start_initial_quests") {
                auto raw=text.substr(start,offset+line.size()-start);
                const auto parsed=wld::File::parseText(raw);
                const auto identity=active=="newmap"?mapKey(parsed.maps().at(0).levelName):lower(parsed.regions().at(0).regionName);
                if(identity.empty() || !names.insert(active+"|"+identity).second)
                    throw std::runtime_error("WLD: duplicate or empty record identity");
                doc.blocks.push_back({active,identity,std::move(raw),id,start,offset+line.size()});
            }
            active.clear();
        } else if(active=="start_initial_quests") {
            auto quest=key;
            if(quest.starts_with("[DELETE]")) quest.erase(0,8);
            if(!value.empty() || !tng::isUnquotedIdentifier(quest)) throw std::runtime_error("WLD: invalid initial quest entry");
            doc.quests.push_back(key);
        } else {
            if(active.empty() && k!="mapuidcount" && k!="thingmanageruidcount")
                throw std::runtime_error("WLD: unsupported top-level directive "+key);
            const bool scalar=k=="mapx" || k=="mapy" || k=="levelname" || k=="levelscriptname" || k=="mapuid" ||
                k=="issea" || k=="loadedonplayerproximity" || k=="regionname" || k=="newdisplayname" || k=="regiondef" ||
                k=="appearonworldmap" || k=="minimapgraphic" || k=="minimapscale" || k=="minimapoffsetx" || k=="minimapoffsety" ||
                k=="worldmapoffsetx" || k=="worldmapoffsety" || k.starts_with("minimapregionexittextoffset") || k.starts_with("namegraphicoffset");
            if(!active.empty() && scalar && !fields.insert(k).second)
                throw std::runtime_error("WLD: duplicate scalar directive "+key);
            if(k=="mapx" || k=="mapy") (void)integer<int32_t>(value,key);
            else if(k=="mapuidcount") {
                if(integer<int32_t>(value,key)<0) throw std::runtime_error("WLD: negative MapUIDCount");
            }
            else if(k=="mapuid" || k=="thingmanageruidcount") (void)integer<uint32_t>(value,key);
            else if(k=="issea" || k=="loadedonplayerproximity") {
                if(!tf::equalsIgnoreCase(value,"TRUE") && !tf::equalsIgnoreCase(value,"FALSE"))
                    throw std::runtime_error("WLD: invalid boolean "+key);
            } else if(k=="minimapscale" || k.starts_with("minimapregionexittextoffset") || k.starts_with("namegraphicoffset"))
                (void)real(value,key);
            else if(k=="minimapoffsetx" || k=="minimapoffsety" || k=="worldmapoffsetx" || k=="worldmapoffsety")
                (void)real(value,key,true);
        }
        offset+=line.size();
    }
    if(!active.empty()) throw std::runtime_error("WLD: incomplete record");
    return doc;
}
egocore::TextModSettings clean(const std::string& text) {
    std::string marker="NewMap";
    for(const auto& line:tf::splitRawLines(text)) {
        std::string key,value;if(!tf::parseLogicalLine(line,key,value)) continue;
        const auto k=lower(key);
        if(k=="newmap" || k=="newregion" || k=="mapuidcount" || k=="thingmanageruidcount" || k=="start_initial_quests") {
            marker=key+(k=="start_initial_quests"?";":"");break;
        }
    }
    const auto settings=egocore::parseTextModSettings(text,marker);
    if(settings.replace) throw std::runtime_error("WLD Replace=true needs coordinated world/terrain replacement; not supported");
    if(!settings.deleteUids.empty()) throw std::runtime_error("WLD: DeleteUIDs is an object directive, not a world directive");
    return settings;
}
std::string eolOf(const std::string& text) {return text.find("\r\n")!=text.npos?"\r\n":"\n";}
// Change one property/list while retaining comments, unknown lines and all
// unchanged field spellings. Empty values remove a presence-only directive.
std::string set(std::string raw,const std::string& spelling,const std::vector<std::string>& values) {
    auto lines=tf::splitRawLines(raw);const auto eol=eolOf(raw);
    size_t at=lines.size();
    for(size_t i=0;i<lines.size();) {
        std::string key,value;
        if(tf::parseLogicalLine(lines[i],key,value) && tf::equalsIgnoreCase(key,spelling)) {
            at=std::min(at,i);lines.erase(lines.begin()+std::ptrdiff_t(i));
        } else ++i;
    }
    if(at==lines.size() && !lines.empty()) at=lines.size()-1;
    std::vector<std::string> added;
    for(const auto& value:values) added.push_back(spelling+(value.empty()?"":" "+value)+";"+eol);
    lines.insert(lines.begin()+std::ptrdiff_t(at),added.begin(),added.end());
    raw.clear();for(const auto& line:lines) raw+=line;return raw;
}
struct Field {std::string spelling;std::vector<std::string> values;};
using Fields=std::map<std::string,Field>;
void field(Fields& out,const std::string& name,const std::string& value) {out[lower(name)]={name,{value}};}
Fields mapFields(const wld::Map& map) {
    Fields out;
    field(out,"NewMap",std::to_string(map.index));field(out,"MapX",std::to_string(map.mapX));field(out,"MapY",std::to_string(map.mapY));
    field(out,"LevelName",quote(level(map.levelName)));field(out,"LevelScriptName",quote(map.levelScriptName));
    field(out,"MapUID",std::to_string(map.mapUid));field(out,"IsSea",map.isSea?"TRUE":"FALSE");
    field(out,"LoadedOnPlayerProximity",map.loadedOnPlayerProximity?"TRUE":"FALSE");return out;
}
wld::Map project(const bwd::MapInfo& map,int index) {
    if(map.mapUid>std::numeric_limits<uint32_t>::max()) throw std::runtime_error("WLD: compiled MapUID cannot be represented by the text reader");
    wld::Map out;out.index=index;out.mapX=map.left;out.mapY=map.top;out.levelName=level(map.levelName);
    out.levelScriptName=map.scriptName;out.mapUid=uint32_t(map.mapUid);out.isSea=map.isSea!=0;out.loadedOnPlayerProximity=map.loadedOnProximity!=0;return out;
}
Fields regionFields(const wld::Region& region) {
    Fields out;field(out,"NewRegion",std::to_string(region.index));field(out,"RegionName",quote(region.regionName));
    field(out,"NewDisplayName",quote(region.displayName));field(out,"RegionDef",quote(region.regionDef));
    out["minimapgraphic"]={"MiniMapGraphic",region.minimapGraphic.empty()?std::vector<std::string>{}:std::vector<std::string>{region.minimapGraphic}};
    field(out,"MiniMapScale",number(region.minimapScale));
    field(out,"MiniMapOffsetX",std::to_string(int32_t(region.miniMapOffsetX)));field(out,"MiniMapOffsetY",std::to_string(int32_t(region.miniMapOffsetY)));
    field(out,"WorldMapOffsetX",std::to_string(int32_t(region.worldMapOffsetX)));field(out,"WorldMapOffsetY",std::to_string(int32_t(region.worldMapOffsetY)));
    out["appearonworldmap"]={"AppearOnWorldMap",region.appearOnWorldMap?std::vector<std::string>{""}:std::vector<std::string>{}};
    for(const auto& [key,list]:std::vector<std::pair<std::string,std::vector<std::string>>>{{"ContainsMap",region.containsMaps},{"SeesMap",region.seesMaps}}) {
        Field entry;entry.spelling=key;for(const auto& name:list) entry.values.push_back(quote(level(name)));out[lower(key)]=std::move(entry);
    }
    for(const auto& exit:region.exitTextOffsets) {
        field(out,"MiniMapRegionExitTextOffsetX["+exit.name+"]",number(exit.x));
        field(out,"MiniMapRegionExitTextOffsetY["+exit.name+"]",number(exit.y));
    }
    return out;
}
wld::Region project(const bwd::Region& region,int index,const bwd::File& world) {
    wld::Region out;out.index=index;out.regionName=region.name;out.displayName=region.displayName;out.regionDef=region.regionDef;
    out.minimapGraphic=region.minimapGraphic;out.appearOnWorldMap=region.onWorldMap!=0;std::memcpy(&out.minimapScale,region.minimapScale,4);
    auto offset=[](int32_t value) {
        const auto f=float(value);
        if(double(f)<-2147483648.0 || double(f)>=2147483648.0 || int64_t(f)!=value)
            throw std::runtime_error("WLD: compiled layout offset cannot be represented by the text reader");
        return f;
    };
    out.miniMapOffsetX=offset(region.mmOffX);out.miniMapOffsetY=offset(region.mmOffY);out.worldMapOffsetX=offset(region.wmOffX);out.worldMapOffsetY=offset(region.wmOffY);
    auto names=[&](const auto& slots,auto& result) {for(const auto slot:slots) {
        if(slot<1 || size_t(slot)>world.maps().size()) throw std::runtime_error("WLD: compiled region has an invalid map reference");
        result.push_back(level(world.maps()[size_t(slot-1)].levelName));
    }};
    names(region.contains,out.containsMaps);names(region.sees,out.seesMaps);
    for(const auto& exit:region.exits) {
        wld::Region::ExitTextOffset e;e.name=exit.name;std::memcpy(&e.x,exit.vec,4);std::memcpy(&e.y,exit.vec+4,4);out.exitTextOffsets.push_back(e);
    }
    return out;
}
std::string canonical(const Fields& fields,const std::string& start,const std::string& finish) {
    std::string out;
    const auto& header=fields.at(lower(start));out+=header.spelling+" "+header.values.at(0)+";\r\n";
    for(const auto& [key,entry]:fields) if(key!=lower(start))
        for(const auto& value:entry.values) out+=entry.spelling+(value.empty()?"":" "+value)+";\r\n";
    return out+finish+";\r\n";
}
std::string synchronize(const Block& block,const Fields& desired) {
    const auto parsed=wld::File::parseText(block.raw);
    auto current=block.type=="newmap"?mapFields(parsed.maps().at(0)):regionFields(parsed.regions().at(0));
    auto out=block.raw;
    for(const auto& [key,entry]:current)
        if(!desired.contains(key) && key.starts_with("minimapregionexittextoffset")) out=set(out,entry.spelling,{});
    for(const auto& [key,entry]:desired) {
        auto old=current.find(key);bool same=old!=current.end() && old->second.values==entry.values;
        if(old!=current.end() && (key=="levelname" || key=="containsmap" || key=="seesmap")) {
            auto a=old->second.values,b=entry.values;
            for(auto& name:a) name=mapKey(tf::stripQuotes(name));for(auto& name:b) name=mapKey(tf::stripQuotes(name));same=a==b;
            // WLD values are relative to Data/Levels. Accept full/slash-aliased
            // source paths, but emit a relative value for the game/compiler.
            if(key=="levelname") {
                const auto& name=parsed.maps().at(0).levelName;
                if(name!=level(name)) same=false;
            } else {
                const auto& region=parsed.regions().at(0);
                const auto& names=key=="containsmap"?region.containsMaps:region.seesMaps;
                for(const auto& name:names) if(name!=level(name)) same=false;
            }
        }
        if(!same) out=set(out,entry.spelling,entry.values);
    }
    return out;
}
std::string regionSkeleton(const bwd::File& world) {
    // These map records exist only to resolve names in compileFromWld's
    // canonical region conversion. Their dummy geometry is never published.
    std::string out;
    for(size_t i=0;i<world.maps().size();++i)
        out+="NewMap "+std::to_string(i+1)+";\nMapX 0;\nMapY 0;\nLevelName "+quote(level(world.maps()[i].levelName))+";\nEndMap;\n";
    return out;
}
}

std::vector<std::string> declaredWldMapNames(const std::string& text) {
    const auto cleaned=clean(text);(void)read(cleaned.text);
    std::vector<std::string> names;
    const auto parsed=wld::File::parseText(cleaned.text);
    for(const auto& map:parsed.maps()) names.push_back("Data\\Levels\\"+level(map.levelName));
    return names;
}

WldSource prepareWldSource(const bwd::File& baseline,const std::string& text,const bwd::DimSource& dimensions,
                           const bwd::File* compiledSource,const std::vector<std::string>& availableMaps) {
    WldSource result;const auto settings=clean(text);result.text=settings.text;
    const auto doc=read(result.text);const auto parsed=wld::File::parseText(result.text);
    std::set<int> maps,regions;for(const auto& m:parsed.maps()) maps.insert(m.index);for(const auto& r:parsed.regions()) regions.insert(r.index);
    auto contiguous=[](const auto& ids) {int next=1;for(const auto id:ids) if(id!=next++) return false;return true;};
    result.sparse=settings.hasHeader || maps.size()<baseline.maps().size() || regions.size()<baseline.regions().size() || !contiguous(maps) || !contiguous(regions);
    if(compiledSource) {
        result.world=*compiledSource;
        for(size_t i=0;i<compiledSource->maps().size();++i)
            result.sourceLevels[int(i+1)]=compiledSource->maps()[i].levelName;
        if(result.sparse) for(const auto& map:parsed.maps()) {
            const bwd::MapInfo* found=nullptr;
            for(const auto& candidate:compiledSource->maps()) if(mapKey(candidate.levelName)==mapKey(map.levelName)) {
                if(found) throw std::runtime_error("sparse WLD map is ambiguous in its paired BWD: "+map.levelName);
                found=&candidate;
            }
            if(!found) throw std::runtime_error("sparse WLD names a map absent from its paired BWD: "+map.levelName);
            result.sourceLevels[map.index]=found->levelName;
        }
        return result;
    }
    auto dims=[&](const std::string& name,int& width,int& height) {
        bool found=dimensions && dimensions(name,width,height);
        if(!found) for(const auto& m:baseline.maps()) if(mapKey(m.levelName)==mapKey(name)) {
            const int64_t w=int64_t(m.right)-m.left,h=int64_t(m.bottom)-m.top;
            if(w<1 || h<1 || w>std::numeric_limits<int>::max() || h>std::numeric_limits<int>::max()) return false;
            width=int(w);height=int(h);found=true;break;
        }
        if(!found) return false;
        if(width<1 || height<1) throw std::runtime_error("WLD: invalid map dimensions");
        for(const auto& m:parsed.maps()) if(mapKey(m.levelName)==mapKey(name)) {
            const int64_t right=int64_t(m.mapX)+width,bottom=int64_t(m.mapY)+height;
            if(right>std::numeric_limits<int32_t>::max() || bottom>std::numeric_limits<int32_t>::max())
                throw std::runtime_error("WLD: map bounds overflow");
        }
        return true;
    };
    if(!result.sparse) {
        std::string normalized;size_t pos=0;
        for(const auto& block:doc.blocks) {
            normalized+=result.text.substr(pos,block.begin-pos);auto raw=block.raw;
            const auto record=wld::File::parseText(raw);
            if(block.type=="newmap") raw=set(raw,"LevelName",{quote(level(record.maps().at(0).levelName))});
            else raw.clear(); // regions resolve separately against the name catalog
            normalized+=raw;pos=block.end;
        }
        normalized+=result.text.substr(pos);
        result.world=bwd::compileFromWld(wld::File::parseText(normalized),dims);
        for(size_t i=0;i<result.world.maps().size();++i) result.sourceLevels[int(i+1)]=result.world.maps()[i].levelName;
    } else {
        result.world=baseline;
        for(size_t i=0;i<baseline.maps().size();++i) result.sourceLevels[int(i+1)]=baseline.maps()[i].levelName;
        for(const auto& block:doc.blocks) if(block.type=="newmap") {
            const auto normalized=set(set(block.raw,"NewMap",{"1"}),"LevelName",{quote(level(wld::File::parseText(block.raw).maps()[0].levelName))});
            const auto one=bwd::compileFromWld(wld::File::parseText(normalized),dims).maps().at(0);
            auto found=std::find_if(result.world.maps().begin(),result.world.maps().end(),[&](const auto& m){return mapKey(m.levelName)==block.key;});
            auto compiled=one;
            if(found!=result.world.maps().end()) {
                compiled.levelName=found->levelName;compiled.used=found->used;compiled.flag2=found->flag2;*found=compiled;
            } else result.world.maps().push_back(compiled);
            result.sourceLevels[block.index]=compiled.levelName;
        }
    }
    auto referenceWorld=result.world;
    std::set<std::string> referenceKeys;
    for(const auto& map:referenceWorld.maps()) referenceKeys.insert(mapKey(map.levelName));
    for(const auto& name:availableMaps) if(referenceKeys.insert(mapKey(name)).second) {
        bwd::MapInfo reference;reference.levelName=name;referenceWorld.maps().push_back(std::move(reference));
    }
    const auto skeleton=regionSkeleton(referenceWorld);
    std::vector<Block> regionBlocks;
    for(const auto& block:doc.blocks) if(block.type=="newregion") regionBlocks.push_back(block);
    if(!result.sparse) std::sort(regionBlocks.begin(),regionBlocks.end(),[](const auto& a,const auto& b){return a.index<b.index;});
    for(const auto& block:regionBlocks) {
        auto raw=set(block.raw,"NewRegion",{"1"});
        for(const auto& name:{"ContainsMap","SeesMap"}) {
            const auto p=wld::File::parseText(block.raw).regions().at(0);
            std::vector<std::string> values;
            for(const auto& item:std::string(name)=="ContainsMap"?p.containsMaps:p.seesMaps) values.push_back(quote(level(item)));
            raw=set(raw,name,values);
        }
        const auto converted=bwd::compileFromWld(wld::File::parseText(skeleton+raw),[](const auto&,int& w,int& h){w=h=1;return true;});
        auto compiled=converted.regions().at(0);
        RegionMapNames references;bool external=false;
        auto nameReferences=[&](std::vector<int32_t>& slots,std::vector<std::string>& names) {
            for(const auto slot:slots) {
                names.push_back(referenceWorld.maps().at(size_t(slot-1)).levelName);
                if(size_t(slot)>result.world.maps().size()) external=true;
            }
            // Discovery-only slots must not escape into a standalone BWD.
            std::erase_if(slots,[&](const auto slot){return size_t(slot)>result.world.maps().size();});
        };
        nameReferences(compiled.contains,references.contains);nameReferences(compiled.sees,references.sees);
        auto found=std::find_if(result.world.regions().begin(),result.world.regions().end(),[&](const auto& r){return lower(r.name)==block.key;});
        const auto index=size_t(found-result.world.regions().begin());
        if(found!=result.world.regions().end()) {compiled.name=found->name;compiled.creatureGen=found->creatureGen;compiled.soundThemes=found->soundThemes;*found=compiled;}
        else result.world.regions().push_back(compiled);
        if(external) result.regionReferences[index]=std::move(references);
    }
    return result;
}

std::string renderWldSources(const std::string& baseline,const std::vector<std::string>& sources,const bwd::File& world) {
    const auto base=read(baseline);std::map<std::string,Block> carriers;
    std::map<std::string,std::string> gaps;
    for(size_t i=0;i<base.blocks.size();++i) {
        const auto& block=base.blocks[i];const auto key=block.type+'|'+block.key;carriers[key]=block;
        if(i+1<base.blocks.size()) gaps[key]=baseline.substr(block.end,base.blocks[i+1].begin-block.end);
    }
    std::string prefix=baseline.substr(0,base.blocks.empty()?baseline.size():base.blocks.front().begin);
    const std::string suffix=base.blocks.empty()?std::string():baseline.substr(base.blocks.back().end);
    auto quests=base.quests;
    std::vector<std::string> questOperations;
    for(const auto& text:sources) {
        const auto doc=read(text);
        for(const auto& block:doc.blocks) carriers[block.type+'|'+block.key]=block;
        for(const auto& operation:doc.quests) {
            questOperations.push_back(operation);
            if(operation.starts_with("[DELETE]")) std::erase(quests,operation.substr(8));
            else if(std::find(quests.begin(),quests.end(),operation)==quests.end()) quests.push_back(operation);
        }
    }
    if(quests!=base.quests) {
        auto lines=tf::splitRawLines(prefix);const auto eol=eolOf(prefix);
        for(const auto& operation:questOperations) {
            const bool remove=operation.starts_with("[DELETE]");
            const auto quest=remove?operation.substr(8):operation;
            bool active=false,exists=false;size_t end=lines.size();
            for(size_t i=0;i<lines.size();) {
                std::string key,value;if(!tf::parseLogicalLine(lines[i],key,value)) {++i;continue;}
                if(tf::equalsIgnoreCase(key,"START_INITIAL_QUESTS")) {active=true;++i;continue;}
                if(tf::equalsIgnoreCase(key,"END_INITIAL_QUESTS")) {end=i;break;}
                if(active && key==quest) {
                    exists=true;
                    if(remove) {lines.erase(lines.begin()+std::ptrdiff_t(i));continue;}
                }
                ++i;
            }
            if(!remove && !exists) {
                if(end<lines.size()) lines.insert(lines.begin()+std::ptrdiff_t(end),quest+';'+eol);
                else lines.insert(lines.begin(),{"START_INITIAL_QUESTS;"+eol,quest+';'+eol,"END_INITIAL_QUESTS;"+eol});
            }
        }
        prefix.clear();for(const auto& line:lines) prefix+=line;
    }
    std::string out=prefix;
    auto append=[&](const std::string& key,const Fields& fields,const std::string& start,const std::string& end) {
        if(!out.empty() && !out.ends_with('\n')) out+='\n';
        if(const auto source=carriers.find(key);source!=carriers.end()) out+=synchronize(source->second,fields);
        else out+=canonical(fields,start,end);
        if(const auto gap=gaps.find(key);gap!=gaps.end()) out+=gap->second;
    };
    for(size_t i=0;i<world.maps().size();++i) append("newmap|"+mapKey(world.maps()[i].levelName),mapFields(project(world.maps()[i],int(i+1))),"NewMap","EndMap");
    for(size_t i=0;i<world.regions().size();++i) append("newregion|"+lower(world.regions()[i].name),regionFields(project(world.regions()[i],int(i+1),world)),"NewRegion","EndRegion");
    out+=suffix;(void)read(out);return out;
}
} // namespace forge::worldmerge
