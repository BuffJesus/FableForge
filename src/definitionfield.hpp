#pragma once
#include "definitionwrite.hpp"
#include "forge/defdecode.hpp"
#include "forge/defedit.hpp"
#include <charconv>
#include <algorithm>
#include <cstring>
namespace albion::definitionwrite {
inline bool printableString(const forge::defdecode::DecodedField& field) {
    return field.type=="CCharString" && !field.value.empty() && field.value.size()<=128 && field.value.back()==0 &&
        std::all_of(field.value.begin(),field.value.end()-1,[](unsigned char c){return c>=32 && c<=126;});
}
inline bool printableWideString(const forge::defdecode::DecodedField& field) {
    if(field.type!="CWideString" || field.value.size()<2 || field.value.size()>256 || field.value.size()%2 ||
       field.value[field.value.size()-2]!=0 || field.value.back()!=0) return false;
    for(size_t i=0;i+2<field.value.size();i+=2)
        if(field.value[i]<32 || field.value[i]>126 || field.value[i+1]!=0) return false;
    return true;
}
inline bool fieldEditable(const forge::defdecode::DecodedField& field) {
    return !field.name.empty() && ((field.type=="bool" && field.value.size()==1) ||
        ((field.type=="int32" || field.type=="uint32" || field.type=="float" || field.type=="CDefString") && field.value.size()==4) ||
        (field.type=="C2DVector" && field.value.size()==8) || (field.type=="C3DVector" && field.value.size()==12) || (field.type=="CRGBColour" && field.value.size()==4) || printableString(field) || printableWideString(field));
}
inline std::string vectorValue(const float* values,size_t count) {
    std::string result;char buffer[64];
    for(size_t i=0;i<count;++i) {
        if(i) result+=',';
        const auto formatted=std::to_chars(buffer,buffer+sizeof buffer,values[i]);
        if(formatted.ec!=std::errc{}) throw std::runtime_error("Cannot format vector component.");
        result.append(buffer,formatted.ptr);
    }
    return result;
}
inline std::string colourValue(const int* values) {
    return std::to_string(values[0])+","+std::to_string(values[1])+","+std::to_string(values[2])+","+std::to_string(values[3]);
}
inline std::string fieldValue(const forge::defdecode::DecodedField& field) {
    if((field.type=="C2DVector" && field.value.size()==8) || (field.type=="C3DVector" && field.value.size()==12)) {
        float values[3]{};std::memcpy(values,field.value.data(),field.value.size());
        return vectorValue(values,field.value.size()/4);
    }
    if(field.type=="CRGBColour" && field.value.size()==4) {
        const int rgba[]={field.value[2],field.value[1],field.value[0],field.value[3]};return colourValue(rgba);
    }
    if(printableWideString(field)) {
        std::string text;for(size_t i=0;i+2<field.value.size();i+=2) text+=char(field.value[i]);return text;
    }
    if(printableString(field)) return std::string(field.value.begin(),field.value.end()-1);
    return forge::defdecode::formatValue(field);
}
inline bool replaceField(const std::filesystem::path& sourceNames,const std::filesystem::path& sourceBank,
                          const std::filesystem::path& destination,size_t index,const forge::bin::Entry& expected,
                          const std::vector<std::pair<uint32_t,std::string>>& expectedNames,
                          const forge::defschema::Schema& schema,const std::string& field,const std::string& value) {
    auto file=forge::bin::File::open(sourceNames,sourceBank);
    if(file.originalNames()!=expectedNames || index>=file.entries().size())
        throw std::runtime_error("Definition namespace changed; refresh before editing.");
    const auto& selected=file.entries()[index];
    if(selected.name!=expected.name || selected.definition!=expected.definition ||
       selected.indexInDefinition!=expected.indexInDefinition || selected.data!=expected.data)
        throw std::runtime_error("Definition changed on disk; refresh before editing.");
    const auto decoded=forge::defdecode::decode(selected,schema);
    const forge::defdecode::DecodedField* target=nullptr;
    for(const auto& candidate:decoded.fields) if(candidate.name==field) {
        if(target) throw std::runtime_error("Definition field name is ambiguous.");target=&candidate;
    }
    if(!target || !fieldEditable(*target)) throw std::runtime_error("Choose a confirmed editable field.");
    if((target->type=="CCharString" || target->type=="CWideString") && (value.size()>127 ||
       std::any_of(value.begin(),value.end(),[](unsigned char c){return c<32 || c>126;})))
        throw std::runtime_error("Definition text accepts up to 127 printable ASCII characters.");
    // Preserve the exact original representation when the draft is unchanged,
    // including noncanonical true bytes and existing NaN payload bits.
    if(fieldValue(*target)==value) return false;
    forge::defedit::setField(file,schema,index,field,value);
    if(target->type=="CDefString") {
        const auto updated=forge::defdecode::decode(file.entries()[index],schema);
        const auto found=std::find_if(updated.fields.begin(),updated.fields.end(),[&](const auto& candidate){return candidate.name==field;});
        if(found==updated.fields.end() || found->value.size()!=4) throw std::runtime_error("Name reference has no confirmed four-byte layout.");
        const auto& bytes=found->value;
        const uint32_t offset=uint32_t(bytes[0]) | (uint32_t(bytes[1])<<8) | (uint32_t(bytes[2])<<16) | (uint32_t(bytes[3])<<24);
        bool valid=offset==0xffffffffu;uint64_t position=4;
        for(const auto& name:expectedNames) {
            if(position<=0x7fffffffu && offset==position) valid=true;
            position+=name.second.size()+5;
        }
        if(!valid) throw std::runtime_error("Choose an existing name from this bank or no name.");
    }
    if(file.entries()[index].data==expected.data) return false;
    publish(file,sourceNames,destination,true);return true;
}
} // namespace albion::definitionwrite
