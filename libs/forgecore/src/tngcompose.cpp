#include "forge/tngcompose.hpp"
#include "forge/tng.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <optional>
#include <set>
#include <stdexcept>

namespace forge::tng {
namespace {
std::string lower(std::string text) {
    for(auto& c:text) c=char(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
std::string comparison(const std::string& text, bool metadata=false) {
    std::string out;
    size_t start=0;
    while(start<text.size()) {
        auto end=text.find('\n',start); if(end==text.npos) end=text.size();
        auto line=text.substr(start,end-start);
        if(!line.empty() && line.back()=='\r') line.pop_back();
        if(!metadata || line.find_first_not_of(" \t")!=line.npos) out+=line+'\n';
        start=end+1;
    }
    return out;
}
struct Object { std::string key, section, block; size_t begin=0,end=0; };
struct Chunk { std::string name, raw, signature; size_t begin=0,footer=0; };
struct Document {
    std::string prefix, suffix;
    std::map<std::string,Chunk> chunks;
    std::vector<std::string> sections, order;
    std::map<std::string,Object> objects;
};
Document read(const std::string& text) {
    Document doc;
    const auto file=File::parseText(text);
    const auto ranges=file.sectionRanges();
    std::vector<size_t> lines{0};
    for(size_t i=0;i<text.size();++i) if(text[i]=='\n') lines.push_back(i+1);
    if(lines.back()!=text.size()) lines.push_back(text.size());
    doc.prefix=text.substr(0,ranges.empty()?text.size():ranges.front().begin);
    if(!ranges.empty()) doc.suffix=text.substr(ranges.back().end);
    for(size_t i=0;i<ranges.size();++i) {
        const auto& r=ranges[i];
        const auto key=lower(r.name);
        doc.sections.push_back(key);
        doc.chunks.emplace(key,Chunk{r.name,text.substr(r.begin,
            (i+1<ranges.size()?ranges[i+1].begin:r.end)-r.begin),{},r.begin,r.footer-r.begin});
    }
    for(size_t i=0;i<file.things().size();++i) {
        const auto& thing=file.things()[i];
        if(thing.endLine<=thing.startLine || thing.endLine+1>=lines.size())
            throw std::runtime_error("tng: incomplete object in composition");
        for(const auto& c:thing.ctcBlocks) if(c.endLine<=c.startLine)
            throw std::runtime_error("tng: incomplete component in composition");
        const size_t begin=lines[thing.startLine],end=lines[thing.endLine+1];
        const auto owner=std::find_if(ranges.begin(),ranges.end(),[&](const auto& r){
            return begin>=r.begin && end<=r.footer;
        });
        if(owner==ranges.end()) throw std::runtime_error("tng: object outside a complete section in composition");
        auto block=file.thingBlockText(i);
        std::string key;
        if(const auto uid=thing.find("UID");uid && !uid->empty()) {
            uint64_t number=0;
            const auto parsed=std::from_chars(uid->data(),uid->data()+uid->size(),number);
            key="uid:"+(parsed.ec==std::errc() && parsed.ptr==uid->data()+uid->size()?std::to_string(number):*uid);
        }
        else {
            // Injective fallback: anonymous changes are additions, not guessed
            // matches. Hex keeps the choice key free of control characters.
            key="anon:";
            for(const unsigned char c:comparison(block)) {key+="0123456789abcdef"[c>>4];key+="0123456789abcdef"[c&15];}
        }
        if(!doc.objects.emplace(key,Object{key,lower(owner->name),block,begin,end}).second)
            throw std::runtime_error("tng: duplicate object identity in composition: "+key);
        doc.order.push_back(key);
    }
    for(auto& [name,chunk]:doc.chunks) {
        std::string skeleton=chunk.raw;
        for(auto i=doc.order.rbegin();i!=doc.order.rend();++i) {
            const auto& object=doc.objects.at(*i);
            if(object.section==name) skeleton.erase(object.begin-chunk.begin,object.end-object.begin);
        }
        chunk.signature=comparison(skeleton,true);
    }
    return doc;
}
bool same(const Object& a,const Object& b) {
    return a.section==b.section && comparison(a.block)==comparison(b.block);
}
}

TextComposition composeText(const std::string& baseline,const std::vector<TextLayer>& layers,
                            const std::map<std::string,std::string>& picks,bool reviewChoices,
                            const CompositionOptions& options) {
    TextComposition result;
    const auto base=read(baseline);
    const auto seed=options.seed?read(*options.seed):base;
    std::vector<Document> documents;
    std::set<std::string> labels;
    for(const auto& layer:layers) {
        if(layer.label.empty() || !labels.insert(layer.label).second)
            throw std::runtime_error("tng: ambiguous contributor label in composition");
        documents.push_back(read(layer.text));
    }
    auto sections=seed.sections;
    auto chunks=seed.chunks;
    std::map<std::string,std::string> metadata;
    std::string prefix=seed.prefix;
    std::string suffix=seed.suffix;
    std::optional<std::string> prefixChange;
    std::optional<std::string> suffixChange;
    std::map<std::string,std::vector<std::pair<std::string,Object>>> versions;
    auto order=seed.order;
    std::set<std::string> ordered(order.begin(),order.end());
    for(size_t i=0;i<documents.size();++i) {
        const auto& doc=documents[i];
        if(doc.suffix.find_first_not_of(" \t\r\n")!=doc.suffix.npos &&
           comparison(doc.suffix,true)!=comparison(base.suffix,true)) {
            const auto signature=comparison(doc.suffix,true);
            if(suffixChange && *suffixChange!=signature) result.metadataConflict=true;
            suffixChange=signature;suffix=doc.suffix;
        }
        if(doc.prefix.find_first_not_of(" \t\r\n")!=doc.prefix.npos &&
           comparison(doc.prefix,true)!=comparison(base.prefix,true)) {
            const auto signature=comparison(doc.prefix,true);
            if(prefixChange && *prefixChange!=signature) result.metadataConflict=true;
            prefixChange=signature;prefix=doc.prefix;
        }
        for(const auto& section:doc.sections) {
            const auto& chunk=doc.chunks.at(section);
            const auto original=base.chunks.find(section);
            if(original!=base.chunks.end() && original->second.signature==chunk.signature) continue;
            if(const auto old=metadata.find(section);old!=metadata.end() && old->second!=chunk.signature)
                result.metadataConflict=true;
            metadata[section]=chunk.signature;
            if(!chunks.contains(section)) sections.push_back(section);
            chunks[section]=chunk;
        }
        for(const auto& key:doc.order) {
            const auto& object=doc.objects.at(key);
            const auto original=base.objects.find(key);
            if(original!=base.objects.end() && same(original->second,object)) continue;
            if(ordered.insert(key).second) order.push_back(key);
            versions[key].push_back({layers[i].label,object});
        }
    }
    auto wanted=seed.objects;
    for(const auto& [key,values]:versions) {
        int chosen=int(values.size())-1;
        ThingConflict conflict;conflict.key=key;
        bool differs=false;
        for(const auto& [label,object]:values) {
            conflict.mods.push_back(label);
            differs=differs || !same(values.front().second,object);
        }
        if(const auto pick=picks.find(key);pick!=picks.end()) {
            conflict.overridden=true;
            if(pick->second=="vanilla") chosen=-1;
            else {
                const auto found=std::find_if(values.begin(),values.end(),[&](const auto& v){return v.first==pick->second;});
                if(found==values.end()) {
                    if(!reviewChoices) throw std::runtime_error("tng: unavailable object choice "+key+" from "+pick->second);
                    conflict.overridden=false;conflict.choiceUnavailable=true;
                } else chosen=int(found-values.begin());
            }
        }
        conflict.winner=chosen<0?"vanilla":values[size_t(chosen)].first;
        if(differs || conflict.overridden || conflict.choiceUnavailable) result.conflicts.push_back(std::move(conflict));
        if(chosen>=0) wanted[key]=values[size_t(chosen)].second;
        else if(const auto original=base.objects.find(key);original!=base.objects.end()) wanted[key]=original->second;
        else wanted.erase(key);
    }
    for(const auto& [key,labels]:options.removals) {
        if(!wanted.contains(key)) continue;
        if(picks.contains(key) && !reviewChoices)
            throw std::runtime_error("tng: object choice "+key+" is removed by DeleteUIDs; clear the choice or disable the removal");
        wanted.erase(key);++result.deleted;
        auto row=std::find_if(result.conflicts.begin(),result.conflicts.end(),[&](const auto& c){return c.key==key;});
        if(row==result.conflicts.end() && (versions.contains(key) || picks.contains(key))) {
            ThingConflict conflict;conflict.key=key;
            if(const auto v=versions.find(key);v!=versions.end())
                for(const auto& item:v->second) conflict.mods.push_back(item.first);
            result.conflicts.push_back(std::move(conflict));row=std::prev(result.conflicts.end());
        }
        if(row!=result.conflicts.end()) {
            row->winner="removed";row->removalMods=labels;row->overridden=false;
            row->choiceUnavailable=picks.contains(key);
        }
    }
    for(const auto& [key,object]:wanted) {
        if(!chunks.contains(object.section)) {
            const auto original=base.chunks.find(object.section);
            if(original==base.chunks.end()) throw std::runtime_error("tng: selected object has no section");
            sections.push_back(object.section);chunks[object.section]=original->second;
        }
        if(ordered.insert(key).second) order.push_back(key);
    }
    if(!sections.empty() && seed.chunks.empty()) {
        if(prefix.empty()) prefix=base.prefix;
        if(suffix.empty()) suffix=base.suffix;
    }
    result.text=prefix;
    for(const auto& name:sections) {
        const auto& chunk=chunks.at(name);
        const auto carrier=read(chunk.raw);
        struct Edit {size_t begin,end;std::string text;};
        std::vector<Edit> edits;
        std::set<std::string> present;
        for(const auto& key:carrier.order) {
            const auto& object=carrier.objects.at(key);
            const auto selected=wanted.find(key);
            const bool keep=selected!=wanted.end() && selected->second.section==name;
            edits.push_back({object.begin,object.end,keep?selected->second.block:std::string()});
            if(keep) present.insert(key);
        }
        std::string additions;
        for(const auto& key:order) {
            const auto selected=wanted.find(key);
            if(selected!=wanted.end() && selected->second.section==name && !present.contains(key)) {
                additions+=selected->second.block;
                if(!additions.ends_with('\n')) additions+='\n';
                present.insert(key);
            }
        }
        if(!additions.empty()) edits.push_back({chunk.footer,chunk.footer,std::move(additions)});
        std::sort(edits.begin(),edits.end(),[](const auto& a,const auto& b){return a.begin>b.begin;});
        auto raw=chunk.raw;
        for(const auto& edit:edits) raw.replace(edit.begin,edit.end-edit.begin,edit.text);
        result.text+=raw;
    }
    result.text+=suffix;
    // Validate the assembled ownership and identities, rather than trusting the
    // splice positions or accepting duplicate IDs hidden by a map container.
    (void)read(result.text);
    return result;
}
} // namespace forge::tng
