#include "backups.hpp"
#include "gtg.hpp"
#include "pendingbanks.hpp"
#include "forge/tng.hpp"
#include "forge/tngcompose.hpp"
#include "forge/egocore.hpp"
#include "forge/bwd.hpp"
#include "forge/modchoices.hpp"
#include "forge/worldmerge.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>

namespace albion::editor {
namespace fs = std::filesystem;

namespace {
std::string readText(const fs::path& p, std::string& error) {
    std::ifstream in(p, std::ios::binary);
    if (!in) { error = "cannot open " + p.string(); return {}; }
    std::stringstream ss; ss << in.rdbuf();
    if (in.bad()) { error = "cannot read " + p.string(); return {}; }
    return ss.str();
}

bool writeText(const fs::path& p, const std::string& text, std::string& error) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) { error = "cannot write " + p.string(); return false; }
    out << text;
    out.close();
    if (!out) { error = "cannot finish writing " + p.string(); return false; }
    return true;
}

bool backupOnce(const fs::path& p, std::string& error) { return albion::backups::backupOnce(p, error); }   // <file>.forge-orig, once

std::string fmt(float v) {
    char b[48];
    std::snprintf(b, sizeof b, "%.6f", v);
    std::string s = b;
    // retail prints the shortest form with at least one decimal: "0.0", "33.785801"
    while (s.size() > 1 && s.back() == '0') s.pop_back();
    if (s.back() == '.') s += '0';
    return s;
}

std::string physics(const float pos[3], const float forward[2], const std::string& n) {
    const double fl = std::hypot(double(forward[0]), double(forward[1]));
    const float fx = fl > 1e-6 ? float(forward[0] / fl) : 0.0f, fy = fl > 1e-6 ? float(forward[1] / fl) : 1.0f;
    return "StartCTCPhysicsStandard;" + n + "PositionX " + fmt(pos[0]) + ";" + n + "PositionY " + fmt(pos[1]) + ";" + n + "PositionZ " + fmt(pos[2]) + ";" + n +
           "RHSetForwardX " + fmt(fx) + ";" + n + "RHSetForwardY " + fmt(fy) + ";" + n + "RHSetForwardZ 0.0;" + n + "RHSetUpX 0.0;" + n + "RHSetUpY 0.0;" + n + "RHSetUpZ 1.0;" + n + "EndCTCPhysicsStandard;" + n;
}

void movePhysics(forge::tng::File& body, size_t index, const float pos[3], const float forward[2]) {
    if (!body.things()[index].findCtc("CTCPhysicsStandard")) body.addCtcBlock(index, "CTCPhysicsStandard", {});
    const double length = std::hypot(double(forward[0]), double(forward[1]));
    const float fx = length > 1e-6 ? float(forward[0] / length) : 0.0f;
    const float fy = length > 1e-6 ? float(forward[1] / length) : 1.0f;
    const std::pair<const char*, float> fields[] = {
        {"PositionX", pos[0]}, {"PositionY", pos[1]}, {"PositionZ", pos[2]},
        {"RHSetForwardX", fx}, {"RHSetForwardY", fy}, {"RHSetForwardZ", 0},
        {"RHSetUpX", 0}, {"RHSetUpY", 0}, {"RHSetUpZ", 1}
    };
    for (const auto& [key, value] : fields) body.setCtcProperty(index, "CTCPhysicsStandard", key, fmt(value));
}

std::string entranceBlock(uint64_t uid, const float pos[3], const float forward[2], const std::string& n) {
    return "NewThing Thing;" + n + "Player 4;" + n + "UID " + std::to_string(uid) + ";" + n + "DefinitionType \"REGION_ENTRANCE_POINT\";" + n + "ScriptName NULL;" + n + "ScriptData \"NULL\";" + n +
           "ThingGamePersistent FALSE;" + n + "ThingLevelPersistent FALSE;" + n + physics(pos, forward, n) +
           "StartCTCDRegionEntrance;" + n + "Active TRUE;" + n + "EndCTCDRegionEntrance;" + n + "EndThing;" + n + n;
}

std::string startBlock(uint64_t uid, const std::string& script, const float pos[3], const float forward[2], const std::string& n) {
    return "NewThing Holy Site;" + n + "Player 4;" + n + "UID " + std::to_string(uid) + ";" + n + "DefinitionType \"HOLY_SITE_PLAYER_START\";" + n + "ScriptName " + script + ";" + n + "ScriptData \"NULL\";" + n +
           "ThingGamePersistent FALSE;" + n + "ThingLevelPersistent FALSE;" + n + physics(pos, forward, n) + "Health 100.0;" + n + "EndThing;" + n + n;
}

// [start, end) of the thing block that contains `needle` inside `body`, or npos.
std::pair<size_t, size_t> blockAround(const std::string& body, size_t at) {
    const size_t start = body.rfind("NewThing ", at);
    if (start == std::string::npos) return {std::string::npos, std::string::npos};
    size_t end = body.find("EndThing;", at);
    if (end == std::string::npos) return {std::string::npos, std::string::npos};
    end += 9;
    while (end < body.size() && (body[end] == '\n' || body[end] == '\r')) ++end;   // its line end + the blank line after
    return {start, end};
}

bool directive(std::string_view line, std::string_view name) {
    const auto begin = line.find_first_not_of(" \t\r");
    if (begin == std::string_view::npos) return false;
    line.remove_prefix(begin);
    return line.starts_with(name) && (line.size() == name.size() ||
        std::string_view(" \t\r;").find(line[name.size()]) != std::string_view::npos);
}

void validateForWrite(const GtgFile& file, const std::string& text) {
    if (file.serialize() != text) throw std::runtime_error("FinalAlbion.gtg does not round-trip; not touching it");
    if (file.sections.empty()) throw std::runtime_error("FinalAlbion.gtg has no complete map sections");
    std::istringstream tail(file.tail);
    std::string line;
    while (std::getline(tail, line))
        if (directive(line, "NEWMAP") || directive(line, "ENDMAP"))
            throw std::runtime_error("FinalAlbion.gtg has an incomplete map section");
    int previous = 0;
    for (const auto& section : file.sections) {
        if (section.slot <= previous) throw std::runtime_error("FinalAlbion.gtg map slots must be positive, unique and ascending");
        previous = section.slot;
        std::istringstream lines(section.body);
        bool inSection = false;
        while (std::getline(lines, line)) {
            if (directive(line, "NEWMAP") || directive(line, "ENDMAP"))
                throw std::runtime_error("FinalAlbion.gtg has nested map sections");
            if (directive(line, "XXXSectionStart")) {
                if (inSection) throw std::runtime_error("FinalAlbion.gtg has nested thing sections");
                inSection = true;
            } else if (directive(line, "XXXSectionEnd")) {
                if (!inSection) throw std::runtime_error("FinalAlbion.gtg has an unmatched thing section end");
                inSection = false;
            } else if (directive(line, "NewThing") && !inSection)
                throw std::runtime_error("FinalAlbion.gtg has a thing outside its section");
        }
        if (inSection) throw std::runtime_error("FinalAlbion.gtg has an incomplete thing section");
        const auto body = forge::tng::File::parseText(section.body);
        for (const auto& thing : body.things()) {
            if (thing.endLine <= thing.startLine) throw std::runtime_error("FinalAlbion.gtg has an incomplete thing");
            for (const auto& ctc : thing.ctcBlocks)
                if (ctc.endLine <= ctc.startLine) throw std::runtime_error("FinalAlbion.gtg has an incomplete component");
        }
    }
}
}  // namespace

GtgFile GtgFile::parse(const std::string& text) {
    GtgFile f;
    f.eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    size_t pos = 0;
    while (true) {
        const size_t n = text.find("NEWMAP ", pos);
        if (n == std::string::npos) { f.tail = text.substr(pos); break; }
        const size_t eol = text.find('\n', n);
        if (eol == std::string::npos) { f.tail = text.substr(pos); break; }
        GtgSection s;
        s.slot = std::atoi(text.c_str() + n + 7);
        size_t end = text.find("ENDMAP", eol + 1);
        std::string footerEol;
        while (end != std::string::npos) {
            if (text[end-1] == '\n') {
                if (text.compare(end+6, 2, "\r\n") == 0) footerEol = "\r\n";
                else if (text.compare(end+6, 1, "\n") == 0) footerEol = "\n";
                if (!footerEol.empty()) break;
            }
            end = text.find("ENDMAP", end+6);
        }
        if (end == std::string::npos) { f.tail = text.substr(pos); break; }
        s.body = text.substr(eol + 1, end - (eol + 1));
        s.headerEol = eol > n && text[eol-1] == '\r' ? "\r\n" : "\n";
        s.footerEol = footerEol;
        f.sections.push_back(std::move(s));
        pos = end + 6 + footerEol.size();
    }
    return f;
}

std::string GtgFile::serialize() const {
    std::string out;
    for (const auto& s : sections) out += "NEWMAP " + std::to_string(s.slot) +
        (s.headerEol.empty() ? eol : s.headerEol) + s.body + "ENDMAP" +
        (s.footerEol.empty() ? eol : s.footerEol);
    out += tail;
    return out;
}

const GtgSection* GtgFile::find(int slot) const {
    for (const auto& s : sections) if (s.slot == slot) return &s;
    return nullptr;
}

std::string remapGtgMapSlots(const std::string& text, const forge::bwd::File& sourceWorld,
                             const forge::bwd::File& composedWorld) {
    std::map<int,std::string> levels;
    std::set<std::string> names;
    for(size_t i=0;i<sourceWorld.maps().size();++i) {
        auto name=sourceWorld.maps()[i].levelName;
        for(auto& c:name) c=c=='/'?'\\':char(std::tolower(static_cast<unsigned char>(c)));
        if(name.empty() || !names.insert(name).second) throw std::runtime_error("GTG map remap: ambiguous source level identity");
        levels.emplace(int(i+1),sourceWorld.maps()[i].levelName);
    }
    return remapGtgMapSlots(text,levels,composedWorld);
}

std::string remapGtgMapSlots(const std::string& text,const std::map<int,std::string>& sourceLevels,
                             const forge::bwd::File& composedWorld) {
    auto key = [](std::string name) {
        for (auto& c : name) c = c == '/' ? '\\' : char(std::tolower(static_cast<unsigned char>(c)));
        return name;
    };
    std::map<std::string, int> destinations;
    for (size_t i = 0; i < composedWorld.maps().size(); ++i) {
        const auto name = key(composedWorld.maps()[i].levelName);
        if (name.empty() || !destinations.emplace(name, int(i + 1)).second)
            throw std::runtime_error("GTG map remap: ambiguous composed level identity");
    }
    auto file = GtgFile::parse(text);
    validateForWrite(file, text);
    bool changed = false;
    for (auto& section : file.sections) {
        const auto source=sourceLevels.find(section.slot);
        if (source==sourceLevels.end() || source->second.empty())
            throw std::runtime_error("GTG map remap: source slot " + std::to_string(section.slot) + " has no map");
        const auto target = destinations.find(key(source->second));
        if (target == destinations.end())
            throw std::runtime_error("GTG map remap: composed world has no map " + source->second);
        changed = changed || section.slot != target->second;
        section.slot = target->second;
    }
    if (!changed) return text;
    std::sort(file.sections.begin(), file.sections.end(), [](const auto& a, const auto& b) { return a.slot < b.slot; });
    const auto serialized=file.serialize();validateForWrite(file,serialized);return serialized;
}

GtgMapLayer prepareGtgMod(const std::string& label,const std::string& text,
                         const forge::bwd::File& sourceWorld,const forge::bwd::File& composedWorld) {
    const auto settings=forge::egocore::parseTextModSettings(text,"NEWMAP");
    GtgMapLayer layer;layer.label=label;layer.replace=settings.replace;
    layer.hasHeader=settings.hasHeader;layer.deleteUids=settings.deleteUids;
    if(!settings.text.empty()) layer.text=remapGtgMapSlots(settings.text,sourceWorld,composedWorld);
    else if(!settings.hasHeader || settings.replace)
        throw std::runtime_error("GTG mod has no complete map sections");
    return layer;
}

GtgMapLayer prepareGtgMod(const std::string& label,const std::string& text,
                         const std::map<int,std::string>& sourceLevels,const forge::bwd::File& composedWorld) {
    const auto settings=forge::egocore::parseTextModSettings(text,"NEWMAP");
    GtgMapLayer layer;layer.label=label;layer.replace=settings.replace;
    layer.hasHeader=settings.hasHeader;layer.deleteUids=settings.deleteUids;
    if(!settings.text.empty()) layer.text=remapGtgMapSlots(settings.text,sourceLevels,composedWorld);
    else if(!settings.hasHeader || settings.replace) throw std::runtime_error("GTG mod has no complete map sections");
    return layer;
}

std::string prepareWholeGtgMod(const std::string& text,const std::string& baseline) {
    const auto settings=forge::egocore::parseTextModSettings(text,"NEWMAP");
    if(!settings.hasHeader) return text;
    if(settings.replace && settings.text.empty()) throw std::runtime_error("GTG replacement has no complete map sections");
    const auto gameText=settings.text.empty()?baseline:settings.text;
    if(gameText.empty()) return {};
    auto file=GtgFile::parse(gameText);validateForWrite(file,gameText);
    if(!settings.replace && !settings.deleteUids.empty()) {
        forge::tng::CompositionOptions options;
        for(const auto& uid:settings.deleteUids) options.removals["uid:"+uid]={"selected file"};
        for(auto& section:file.sections)
            section.body=forge::tng::composeText(section.body,{}, {},false,options).text;
    }
    return file.serialize();
}

GtgMapMerge mergeGtgMaps(const std::string& baseline, const std::vector<GtgMapLayer>& layers,
                         const forge::bwd::File& composedWorld,
                         const std::map<std::string, std::string>& picks, bool reviewChoices) {
    GtgMapMerge result;
    GtgFile out = GtgFile::parse(baseline);
    if (!baseline.empty()) validateForWrite(out, baseline);
    std::map<int, std::string> names;
    std::set<std::string> identities;
    for (size_t i=0; i<composedWorld.maps().size(); ++i) {
        const auto name=forge::modchoices::filePathKey(
            forge::worldmerge::wldLevelName(composedWorld.maps()[i].levelName));
        if (name.empty() || name=="@tail" || !identities.insert(name).second)
            throw std::runtime_error("GTG map merge: ambiguous level identity");
        names.emplace(int(i+1),name);
    }
    const auto base=out;
    for(const auto& section:base.sections)
        if(!names.contains(section.slot))
            throw std::runtime_error("GTG map merge: unknown baseline slot "+std::to_string(section.slot));
    struct Version { std::string label; GtgSection section; };
    std::map<int,std::vector<Version>> versions;
    std::vector<std::pair<std::string,std::string>> tails;
    std::set<std::string> labels;
    size_t first=0;
    for(size_t i=0;i<layers.size();++i) {
        const auto& layer=layers[i];
        if (layer.label.empty() || !labels.insert(layer.label).second)
            throw std::runtime_error("GTG map merge: duplicate or empty contributor label");
        if(!layer.text.empty()) validateForWrite(GtgFile::parse(layer.text),layer.text);
        else if(!layer.hasHeader || layer.replace)
            throw std::runtime_error("GTG map merge: no complete map sections");
        if(layer.replace) {
            first=i;out=GtgFile::parse(layer.text);result.replacementMod=layer.label;
        }
    }
    const auto seed=out;
    std::map<std::string,std::vector<std::string>> removals;
    for(size_t i=first;i<layers.size();++i) {
        const auto& layer=layers[i];
        if(!layer.replace) for(const auto& uid:layer.deleteUids) removals["uid:"+uid].push_back(layer.label);
        if(layer.text.empty()) continue;
        const auto file=GtgFile::parse(layer.text);
        if (baseline.empty() && versions.empty()) out.eol=file.eol;
        for (const auto& section:file.sections) {
            if (!names.contains(section.slot))
                throw std::runtime_error("GTG map merge: unknown composed slot "+std::to_string(section.slot));
            const auto* original=base.find(section.slot);
            if (!original || original->body!=section.body)
                versions[section.slot].push_back({layer.label,section});
        }
        if (file.tail!=base.tail && file.tail.find_first_not_of(" \t\r\n")!=std::string::npos)
            tails.emplace_back(layer.label,file.tail);
    }
    std::set<int> deletedSlots;
    auto objectChoices=[&](const std::string& name) {
        std::map<std::string,std::string> choices;
        if(!picks.contains(name)) for(const auto& [key,label]:picks)
            if(key.starts_with(name+"|")) choices.emplace(key.substr(name.size()+1),label);
        return choices;
    };
    auto addConflicts=[&](const forge::tng::TextComposition& composed,int slot,const std::string& name) {
        for(const auto& conflict:composed.conflicts) {
            GtgMapConflict row;
            row.key=name+"|"+conflict.key;row.map=conflict.key+"  "+name;row.slot=slot;
            row.mods=conflict.mods;row.winner=conflict.winner;row.removalMods=conflict.removalMods;
            row.overridden=conflict.overridden;row.choiceUnavailable=conflict.choiceUnavailable;
            result.conflicts.push_back(std::move(row));
        }
        result.deleted+=composed.deleted;
    };
    auto select=[&](const std::string& key,const std::string& display,int slot,
                    const std::vector<std::pair<std::string,std::string>>& values) -> int {
        int chosen=int(values.size())-1;
        GtgMapConflict row;row.key=key;row.map=display;row.slot=slot;
        bool differs=false;
        for(const auto& [label,value]:values) {
            row.mods.push_back(label);
            differs=differs || value!=values.front().second;
        }
        if(const auto pick=picks.find(key);pick!=picks.end()) {
            row.overridden=true;
            if(pick->second=="vanilla") chosen=-1;
            else {
                const auto found=std::find_if(values.begin(),values.end(),[&](const auto& v){return v.first==pick->second;});
                if(found==values.end()) {
                    if(!reviewChoices) throw std::runtime_error("mods GTG choice: "+key+
                        " selects unavailable contributor "+pick->second+"; review or clear this saved choice");
                    row.choiceUnavailable=true;row.overridden=false;
                } else chosen=int(found-values.begin());
            }
        }
        row.winner=chosen<0 ? "vanilla" : values[size_t(chosen)].first;
        if(differs || row.overridden || row.choiceUnavailable) result.conflicts.push_back(std::move(row));
        return chosen;
    };
    for(const auto& [slot,items]:versions) {
        std::vector<std::pair<std::string,std::string>> values;
        for(const auto& item:items) values.emplace_back(item.label,item.section.body);
        const auto& name=names.at(slot);
        const bool exists=base.find(slot)!=nullptr;
        if(picks.contains(name)) {
            const int chosen=select(name,name,slot,values);
            if(chosen<0) {
                if(exists) out.sectionFor(slot)=*base.find(slot);
                else std::erase_if(out.sections,[&](const auto& s){return s.slot==slot;});
                continue;
            }
            out.sectionFor(slot)=items[size_t(chosen)].section;
        } else {
            std::vector<forge::tng::TextLayer> objectLayers;
            for(const auto& item:items) objectLayers.push_back({item.label,item.section.body});
            const auto objectPicks=objectChoices(name);
            forge::tng::CompositionOptions options;options.removals=removals;
            if(!result.replacementMod.empty()) options.seed=seed.find(slot)?seed.find(slot)->body:std::string();
            forge::tng::TextComposition composed;
            try {
                composed=forge::tng::composeText(exists?base.find(slot)->body:std::string(),
                                                objectLayers,objectPicks,reviewChoices,options);
            } catch(const std::exception& e) {
                throw std::runtime_error("GTG things for "+name+": "+e.what());
            }
            if(composed.metadataConflict) {
                (void)select(name,name+" metadata",slot,values);
                result.conflicts.back().winner="merged";
            }
            addConflicts(composed,slot,name);deletedSlots.insert(slot);
            if(!result.replacementMod.empty() && !seed.find(slot) && composed.text.empty()) continue;
            auto section=seed.find(slot)?*seed.find(slot):exists?*base.find(slot):items.back().section;
            section.body=std::move(composed.text);
            out.sectionFor(slot)=std::move(section);
        }
        if(exists) ++result.changed;else ++result.added;
    }
    if(!tails.empty()) {
        const int chosen=select("@tail","FinalAlbion.gtg trailing metadata",0,tails);
        if(chosen>=0) out.tail=tails[size_t(chosen)].second;
        else out.tail=base.tail;
    }
    if(!removals.empty()) for(auto& section:out.sections) if(!deletedSlots.contains(section.slot)) {
        const auto found=names.find(section.slot);
        if(found==names.end()) throw std::runtime_error("GTG deletion: unknown map slot");
        forge::tng::CompositionOptions options;options.removals=removals;
        const auto composed=forge::tng::composeText(section.body,{},objectChoices(found->second),reviewChoices,options);
        section.body=composed.text;addConflicts(composed,section.slot,found->second);
    }
    result.text=out.serialize();
    if(!result.text.empty()) validateForWrite(out,result.text);
    return result;
}

GtgSection& GtgFile::sectionFor(int slot) {
    auto it = std::find_if(sections.begin(), sections.end(), [&](const GtgSection& s) { return s.slot >= slot; });
    if (it != sections.end() && it->slot == slot) return *it;
    GtgSection s;
    s.slot = slot;
    s.body = "Version 2;" + eol + eol;
    return *sections.insert(it, std::move(s));
}

uint64_t GtgFile::maxUid() const {
    uint64_t best = 0;
    for (const auto& s : sections) {
        size_t p = 0;
        while ((p = s.body.find("\nUID ", p)) != std::string::npos) {
            best = std::max(best, std::strtoull(s.body.c_str() + p + 5, nullptr, 10));
            p += 5;
        }
    }
    return best;
}

std::optional<RegionEntrance> entranceOf(const fs::path& gameRoot, int slot, std::string& error) {
    const std::string text = readText(gameRoot / "data" / "Levels" / "FinalAlbion.gtg", error);
    if (text.empty()) return std::nullopt;
    const GtgFile f = GtgFile::parse(text);
    const GtgSection* s = f.find(slot);
    if (!s) return std::nullopt;
    const size_t at = s->body.find("DefinitionType \"REGION_ENTRANCE_POINT\"");
    if (at == std::string::npos) return std::nullopt;
    const auto [b0, b1] = blockAround(s->body, at);
    if (b0 == std::string::npos) return std::nullopt;
    const std::string block = s->body.substr(b0, b1 - b0);
    RegionEntrance e;
    auto num = [&](const char* key) { const size_t k = block.find(key); return k == std::string::npos ? 0.0f : float(std::atof(block.c_str() + k + std::strlen(key))); };
    e.pos[0] = num("PositionX "); e.pos[1] = num("PositionY "); e.pos[2] = num("PositionZ ");
    e.forward[0] = num("RHSetForwardX "); e.forward[1] = num("RHSetForwardY ");
    const size_t hsp = s->body.find("DefinitionType \"HOLY_SITE_PLAYER_START\"");
    if (hsp != std::string::npos) {
        const size_t sn = s->body.find("ScriptName ", hsp);
        if (sn != std::string::npos) { const size_t se = s->body.find(';', sn); e.startScript = s->body.substr(sn + 11, se - sn - 11); }
    }
    return e;
}

bool setRegionEntrance(const fs::path& gameRoot, int slot, const std::string& levelName,
                       const float pos[3], const float forward[2], std::vector<std::string>& notes, std::string& error) {
    try {
        if (slot <= 0) { error = "unknown WLD map slot"; return false; }
        for (int i = 0; i < 3; ++i) if (!std::isfinite(pos[i])) { error = "entrance position must be finite"; return false; }
        for (int i = 0; i < 2; ++i) if (!std::isfinite(forward[i])) { error = "entrance direction must be finite"; return false; }
        if (levelName.empty() || levelName.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos) {
            error = "entrance level name must contain only letters, digits and underscores"; return false;
        }
        const fs::path path = gameRoot / "data" / "Levels" / "FinalAlbion.gtg";
        const std::string text = readText(path, error);
        if (text.empty()) return false;
        GtgFile f = GtgFile::parse(text);
        validateForWrite(f, text);
        std::string note;
        GtgSection& s = f.sectionFor(slot);
        const std::string script = levelName + "HSP";
        uint64_t uid = f.maxUid();
        std::string things;
        auto body = forge::tng::File::parseText(s.body);
        std::optional<size_t> hsp;
        for (size_t i = 0; i < body.things().size(); ++i) {
            const auto& thing = body.things()[i];
            auto name = thing.scriptName();
            if (name.size() >= 2 && name.front() == '"' && name.back() == '"') name = name.substr(1, name.size() - 2);
            if (name != script) continue;
            if (hsp || thing.definitionType() != "HOLY_SITE_PLAYER_START") {
                error = "entrance script name is ambiguous or belongs to another definition"; return false;
            }
            hsp = i;
        }
        if (hsp) {
            // Only the adjacent preceding entrance belongs to this authored pair.
            // Move its physics fields through the TNG editor, keeping custom data/UIDs.
            const size_t h0 = s.body.find(body.thingBlockText(*hsp));
            bool paired = false;
            if (*hsp > 0 && body.things()[*hsp - 1].definitionType() == "REGION_ENTRANCE_POINT") {
                const size_t e0 = s.body.find(body.thingBlockText(*hsp - 1));
                paired = blockAround(s.body, e0).second == h0;
            }
            if (!paired && uid == std::numeric_limits<uint64_t>::max()) { error = "no free entrance UID"; return false; }
            movePhysics(body, *hsp, pos, forward);
            if (paired) movePhysics(body, *hsp - 1, pos, forward);
            s.body = body.serialize();
            if (!paired) s.body.insert(s.body.find(body.thingBlockText(*hsp)), entranceBlock(++uid, pos, forward, f.eol));
            note = ("FinalAlbion.gtg: moved the region entrance of slot " + std::to_string(slot) + " (" + levelName + ") to (" + fmt(pos[0]) + ", " + fmt(pos[1]) + ", " + fmt(pos[2]) + ")");
        } else {
            const std::string& n = f.eol;
            if (uid > std::numeric_limits<uint64_t>::max() - 2) { error = "not enough free entrance UIDs"; return false; }
            const uint64_t entranceUid = ++uid;
            const uint64_t startUid = ++uid;
            things = entranceBlock(entranceUid, pos, forward, n) + startBlock(startUid, script, pos, forward, n);
            const size_t end = s.body.find("XXXSectionEnd;");
            if (end == std::string::npos) s.body += "XXXSectionStart NULL;" + n + n + things + "XXXSectionEnd;" + n + n + n;
            else s.body.insert(end, things);
            note = ("FinalAlbion.gtg: region entrance + " + script + " added for slot " + std::to_string(slot) + " at (" + fmt(pos[0]) + ", " + fmt(pos[1]) + ", " + fmt(pos[2]) + ")");
        }
        detail::PendingBanks pending(path.parent_path(), ".forge-gtg-write-");
        if (!writeText(pending.prepare(path.filename()), f.serialize(), error)) return false;
        if (!backupOnce(path, error) || !pending.install(false, error)) return false;
        notes.push_back(std::move(note));
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

}  // namespace albion::editor
