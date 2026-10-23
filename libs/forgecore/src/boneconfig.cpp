#include "forge/boneconfig.hpp"
#include "forge/bin.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace forge::boneconfig {
namespace {
[[noreturn]] void fail(const char* reason) {
    throw std::runtime_error(std::string("bone config: ")+reason);
}
std::string_view trim(std::string_view value) {
    const auto first=value.find_first_not_of(" \t");
    if(first==value.npos)return {};
    return value.substr(first,value.find_last_not_of(" \t")-first+1);
}
float decimal(std::string_view value) {
    value=trim(value);
    if(value.empty())fail("missing scale component");
    if(value.front()=='+') {
        value.remove_prefix(1);
        if(!value.empty() && (value.front()=='+' || value.front()=='-'))fail("invalid scale sign");
    }
    if(value.empty() || value.find_first_not_of("0123456789.eE+-")!=value.npos)
        fail("expected finite decimal scale");
    float result=0;
    const auto parsed=std::from_chars(value.data(),value.data()+value.size(),result);
    if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size() || !std::isfinite(result))
        fail("invalid or out-of-range scale");
    return result;
}
void validate(const Bone& bone) {
    if(bone.name.empty() || bone.name.size()>255 || trim(bone.name)!=bone.name ||
       std::any_of(bone.name.begin(),bone.name.end(),[](unsigned char ch) {
           return ch<32 || ch>126 || ch==':' || ch==';';
       }))fail("unsupported bone name");
    if(bone.crc!=bin::nameCrc(bone.name))fail("bone name/CRC mismatch");
    if(!std::all_of(bone.scale.begin(),bone.scale.end(),[](float v){return std::isfinite(v);}))
        fail("nonfinite scale");
}
}
Config read(std::string_view text) {
    constexpr std::string_view start="#Start_Bone_data\r\n", end="#End_bone_data\r\n";
    if(text.size()>1024*1024 || text.find('\0')!=text.npos)fail("oversized or NUL-containing input");
    const auto begin=text.find(start);
    if(begin==text.npos || (begin && text[begin-1]!='\n') || text.find(start,begin+start.size())!=text.npos)
        fail("missing, embedded or repeated start marker");
    const auto finish=text.find(end,begin+start.size());
    if(finish==text.npos || text.find(end,finish+end.size())!=text.npos)
        fail("missing or repeated end marker");
    Config result{begin+start.size(),finish,{}};
    std::unordered_set<uint32_t> keys;
    for(size_t at=result.bodyBegin;at<finish;) {
        const auto next=text.find("\r\n",at);
        if(next==text.npos || next>finish)fail("expected CRLF bone row");
        const auto line=text.substr(at,next-at);
        if(!trim(line).empty()) {
            if(result.rows.size()>=4096)fail("too many bone rows");
            const auto colon=line.find(':');
            if(colon==line.npos)fail("missing bone separator");
            Bone bone;bone.offset=at;bone.name=std::string(line.substr(0,colon));
            bone.crc=bin::nameCrc(bone.name);
            auto values=trim(line.substr(colon+1));
            if(values.empty() || values.back()!=';')fail("missing row terminator");
            values.remove_suffix(1);
            for(size_t axis=0;axis<3;++axis) {
                const auto comma=values.find(',');
                if((axis<2)==(comma==values.npos))fail("expected exactly three scale components");
                bone.scale[axis]=decimal(values.substr(0,comma));
                if(axis<2)values.remove_prefix(comma+1);
            }
            validate(bone);
            if(!keys.insert(bone.crc).second)fail("duplicate bone CRC is not supported");
            result.rows.push_back(std::move(bone));
        }
        at=next+2;
    }
    return result;
}
std::vector<Bone> prepare(const Config& config) {
    if(config.rows.size()>4096)fail("too many bone rows");
    std::unordered_set<uint32_t> keys;
    std::vector<Bone> result;
    for(const auto& bone:config.rows) {
        validate(bone);
        if(!keys.insert(bone.crc).second)fail("duplicate bone CRC is not supported");
        if(bone.scale!=std::array<float,3>{1,1,1})result.push_back(bone);
    }
    std::sort(result.begin(),result.end(),[](const Bone& a,const Bone& b){return a.crc<b.crc;});
    return result;
}
} // namespace forge::boneconfig
