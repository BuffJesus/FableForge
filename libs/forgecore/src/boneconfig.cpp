#include "forge/boneconfig.hpp"
#include "forge/bin.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <map>
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
namespace {
void validateSorted(std::span<const Bone> rows) {
    if(rows.size()>65536)fail("too many combined bones");
    for(size_t n=0;n<rows.size();++n) {
        validate(rows[n]);
        if(n && rows[n-1].crc>=rows[n].crc)fail("expected unique CRC-sorted target rows");
    }
}
}
std::vector<Bone> combine(std::span<const WeightedTarget> targets) {
    if(targets.size()>4096)fail("too many weighted targets");
    size_t count=0;
    std::map<uint32_t,Bone> accumulated;
    for(const auto& target:targets) {
        if(!std::isfinite(target.amount) || target.amount<0 || target.amount>1)
            fail("unsupported selected-target amplitude");
        validateSorted(target.rows);
        if(target.rows.size()>65536-count)fail("too many source bones");
        count+=target.rows.size();
        for(const auto& bone:target.rows) {
            auto [entry,inserted]=accumulated.try_emplace(bone.crc,bone);
            if(inserted)entry->second.scale={1,1,1};
            else if(entry->second.name!=bone.name)fail("cross-target bone CRC collision");
            for(size_t axis=0;axis<3;++axis) {
                // Each original vector method stores its result before the next
                // call. Volatile float stores also prevent multiply-add fusion.
                volatile float delta=bone.scale[axis]-1.0f;
                volatile float weighted=delta*target.amount;
                volatile float result=entry->second.scale[axis]+weighted;
                if(!std::isfinite(result))fail("combined scale overflow");
                entry->second.scale[axis]=result;
            }
        }
    }
    std::vector<Bone> result;
    result.reserve(accumulated.size());
    for(auto& entry:accumulated)result.push_back(std::move(entry.second));
    return result;
}
std::vector<std::array<float,3>> mapScales(std::span<const Bone> combined,
                                       std::span<const uint32_t> meshBoneCrcs) {
    validateSorted(combined);
    if(meshBoneCrcs.size()>65536)fail("too many mesh bones");
    std::vector<std::array<float,3>> result;
    result.reserve(meshBoneCrcs.size());
    for(const auto crc:meshBoneCrcs) {
        const auto found=std::lower_bound(combined.begin(),combined.end(),crc,
            [](const Bone& bone,uint32_t key){return bone.crc<key;});
        result.push_back(found!=combined.end() && found->crc==crc ? found->scale : std::array<float,3>{1,1,1});
    }
    return result;
}
} // namespace forge::boneconfig
