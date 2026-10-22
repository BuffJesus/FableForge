#include "forge/egocore.hpp"
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <string_view>

namespace forge::egocore {
namespace {
std::string_view trim(std::string_view line) {
    const auto begin=line.find_first_not_of(" \t\r\n");
    if(begin==line.npos) return {};
    return line.substr(begin,line.find_last_not_of(" \t\r\n")-begin+1);
}
}
TextModSettings parseTextModSettings(const std::string& text,const std::string& firstRecord) {
    TextModSettings result;result.text=text;
    size_t pos=0;
    bool header=false, replaceSeen=false, deleteSeen=false;
    bool deleteContinuation=false;
    std::set<std::string> ids;
    auto addUids=[&](std::string_view list) {
        while(!list.empty()) {
            const auto first=list.find_first_not_of(" \t,;");
            if(first==list.npos) break;
            list.remove_prefix(first);
            const auto count=list.find_first_not_of("0123456789");
            const auto token=list.substr(0,count);
            uint64_t uid=0;
            const auto parsed=std::from_chars(token.data(),token.data()+token.size(),uid);
            if(token.empty() || parsed.ec!=std::errc() || parsed.ptr!=token.data()+token.size())
                throw std::runtime_error("level mod settings: invalid or overflowing DeleteUIDs value");
            if(count!=list.npos && list[count]!=' ' && list[count]!='\t' && list[count]!=',' && list[count]!=';')
                throw std::runtime_error("level mod settings: invalid DeleteUIDs separator");
            const auto canonical=std::to_string(uid);
            if(ids.insert(canonical).second) result.deleteUids.push_back(canonical);
            if(count==list.npos) break;
            list.remove_prefix(count);
        }
    };
    while(pos<text.size()) {
        auto end=text.find('\n',pos);if(end==text.npos) end=text.size();
        const auto next=end<text.size()?end+1:end;
        auto line=trim(std::string_view(text).substr(pos,end-pos));
        if(pos==0 && line.starts_with("\xef\xbb\xbf")) line=trim(line.substr(3));
        if(line.starts_with(firstRecord) && (line.size()==firstRecord.size() ||
            line[firstRecord.size()]==' ' || line[firstRecord.size()]=='\t')) {
            if(header) result.text=text.substr(pos);
            return result;
        }
        if(line=="[Settings]") {
            if(header) throw std::runtime_error("level mod settings: repeated header");
            header=true;result.hasHeader=true;pos=next;continue;
        }
        if(line.empty() || line.starts_with("//") || line.starts_with('#')) {pos=next;continue;}
        if(!header) return result; // ordinary game text; its format validator owns it
        if(const auto comment=line.find("//");comment!=line.npos) line=trim(line.substr(0,comment));
        if(line.starts_with("Replace")) {
            if(replaceSeen) throw std::runtime_error("level mod settings: repeated Replace directive");
            replaceSeen=true;
            deleteContinuation=false;
            auto value=trim(line.substr(7));
            if(value.empty() || value.front()!='=') throw std::runtime_error("level mod settings: expected Replace=<boolean>");
            value=trim(value.substr(1));
            if(value.ends_with(';')) value=trim(value.substr(0,value.size()-1));
            if(value=="true" || value=="1") result.replace=true;
            else if(value!="false" && value!="0") throw std::runtime_error("level mod settings: invalid Replace value");
        } else if(line.starts_with("DeleteUIDs:")) {
            if(deleteSeen) throw std::runtime_error("level mod settings: repeated DeleteUIDs directive");
            deleteSeen=true;
            deleteContinuation=true;addUids(trim(line.substr(11)));
        } else if(deleteContinuation && !line.empty() && std::string_view("0123456789,;").find(line.front())!=line.npos) {
            addUids(line);
        } else throw std::runtime_error("level mod settings: unsupported directive: "+std::string(line));
        pos=next;
    }
    if(header) result.text.clear(); // a deletion-only mod is valid
    return result;
}
TextModSettings parseTngModSettings(const std::string& text) {
    size_t header=std::string::npos,pos=0;
    std::string firstRecord="XXXSectionStart";
    auto directive=[](std::string_view line,std::string_view name) {
        return line.starts_with(name) && (line.size()==name.size() || line[name.size()]==' ' || line[name.size()]=='\t');
    };
    while(pos<text.size()) {
        const auto end=text.find('\n',pos);const auto stop=end==text.npos?text.size():end;
        auto line=trim(std::string_view(text).substr(pos,stop-pos));
        if(pos==0 && line.starts_with("\xef\xbb\xbf")) line=trim(line.substr(3));
        if(line=="[Settings]" && header==text.npos) header=pos;
        if(directive(line,"XXXSectionStart") || directive(line,"NewThing") ||
           (header!=text.npos && directive(line,"Version"))) {
            if(directive(line,"NewThing")) firstRecord="NewThing";
            else if(directive(line,"Version")) firstRecord="Version";
            break;
        }
        if(end==text.npos) break;
        pos=end+1;
    }
    if(header==text.npos) {TextModSettings result;result.text=text;return result;}
    auto result=parseTextModSettings(text.substr(header),firstRecord);
    result.text=text.substr(0,header)+result.text;
    return result;
}
} // namespace forge::egocore
