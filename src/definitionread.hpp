#pragma once
#include <optional>
#include <algorithm>
#include "forge/bin.hpp"
#include "forge/defdecode.hpp"
namespace albion::definitionread {
// Confirmed placement/theme families only. Other integers or categories do not
// imply a writable reference, even when a browser can display their group.
inline const char* groupFamily(const forge::bin::Entry& owner) {
    if(owner.definition=="OBJECT" || owner.definition=="BUILDING" || owner.definition=="CREATURE") return "THING_GROUP";
    if(owner.definition=="ENGINE_THEME") return "ENGINE_THEME_GROUP";
    return nullptr;
}
inline bool groupChoice(const forge::bin::File& file,const forge::bin::Entry& owner,size_t index) {
    const auto family=groupFamily(owner);
    return family && index<file.entries().size() && file.entries()[index].definition==family;
}
// Native CParentDefClassBase stores counted {class CRC, DefIndex, original
// parent} triples after three flag bytes. GetSubDef resolves the first index
// by class key. This read-only link does not establish private ownership.
inline std::optional<size_t> creatureVisualTarget(const forge::bin::File& file,
        const forge::bin::Entry& entry,const forge::defdecode::Decoded& decoded,std::string_view kind) {
    if(entry.definition!="CREATURE" || (kind!="CCreatureDef" && kind!="CAppearanceDef") ||
       !decoded.clean() || entry.data.size()<5 || decoded.fields.empty() ||
       decoded.fields.front().name!="Components" ||
       std::any_of(decoded.fields.begin(),decoded.fields.end(),[](const auto& field){return !field.tagOk;})) return std::nullopt;
    const size_t count=size_t(entry.data[3]) | size_t(entry.data[4])<<8;
    const size_t end=5+12*count;
    if(!count || end>entry.data.size() || decoded.prefixLen!=end ||
       forge::defdecode::encode(decoded)!=entry.data) return std::nullopt;
    const auto word=[&](size_t at) {
        return uint32_t(entry.data[at]) | uint32_t(entry.data[at+1])<<8 |
            uint32_t(entry.data[at+2])<<16 | uint32_t(entry.data[at+3])<<24;
    };
    size_t matches=0;uint32_t index=0;
    for(size_t row=0;row<count;++row) if(word(5+12*row)==forge::defdecode::fieldTag(kind)) {
        ++matches;index=word(9+12*row);
    }
    if(matches!=1 || index>=file.entries().size() || file.entries()[index].definition!=kind) return std::nullopt;
    return size_t(index);
}
// Game-bank GroupDef stores a runtime entry index. The existing placement palette
// resolves it only when the target definition is a *_GROUP type.
inline std::optional<size_t> groupTarget(const forge::bin::File& file,const forge::defdecode::Decoded& decoded) {
    if(!decoded.clean()) return std::nullopt;
    const forge::defdecode::DecodedField* group=nullptr;
    for(const auto& field:decoded.fields) if(field.name=="GroupDef") {
        if(group) return std::nullopt;group=&field;
    }
    if(!group || group->type!="int32" || group->value.size()!=4) return std::nullopt;
    const auto& value=group->value;
    const uint32_t index=uint32_t(value[0])|uint32_t(value[1])<<8|uint32_t(value[2])<<16|uint32_t(value[3])<<24;
    if(index>=file.entries().size()) return std::nullopt;
    const auto& type=file.entries()[index].definition;
    if(type.size()<=6 || type.compare(type.size()-6,6,"_GROUP")!=0) return std::nullopt;
    return size_t(index);
}
// Native TransferBinaryOut (0200af00) emits kind:u32, bank index:u32,
// AnimStep:f32, RenderSizeX:f32 and alpha:byte. Restrict this link to the
// confirmed OBJECT Graphic field; other graphics may refer to textures.
inline std::optional<uint32_t> objectModelId(const forge::bin::Entry& entry,const forge::defdecode::Decoded& decoded,size_t fieldIndex) {
    if(entry.definition!="OBJECT" || !decoded.clean() || fieldIndex>=decoded.fields.size()) return std::nullopt;
    const auto& field=decoded.fields[fieldIndex];
    if(field.name!="Graphic" || field.type!="CEngineGraphic" || field.value.size()!=17) return std::nullopt;
    const auto& value=field.value;
    return uint32_t(value[4])|uint32_t(value[5])<<8|uint32_t(value[6])<<16|uint32_t(value[7])<<24;
}
// Match the six texture fields consumed by terraintex::ThemeLibrary. Other
// integers and graphic structures do not imply a texture reference.
inline std::optional<uint32_t> themeTextureId(const forge::bin::Entry& entry,const forge::defdecode::Decoded& decoded,size_t fieldIndex) {
    if(entry.definition!="ENGINE_THEME" || !decoded.clean() || fieldIndex>=decoded.fields.size()) return std::nullopt;
    const auto& field=decoded.fields[fieldIndex];
    if(field.type!="int32" || field.value.size()!=4 ||
       (field.name!="BaseTexture" && field.name!="BackgroundTexture" && field.name!="BaseBumpMap" &&
        field.name!="CliffBaseTexture" && field.name!="CliffBackgroundTexture" && field.name!="CliffBumpMap")) return std::nullopt;
    const auto& value=field.value;
    const uint32_t id=uint32_t(value[0])|uint32_t(value[1])<<8|uint32_t(value[2])<<16|uint32_t(value[3])<<24;
    return id>0 && id<=0x7fffffffu?std::optional<uint32_t>{id}:std::nullopt;
}
// A CDefString is a signed byte offset into the owning names table. Null
// and negative references are not record IDs and do not resolve to a name.
inline std::optional<uint32_t> nameOffset(const forge::defdecode::Decoded& decoded,size_t fieldIndex) {
    if(!decoded.clean() || fieldIndex>=decoded.fields.size()) return std::nullopt;
    const auto& field=decoded.fields[fieldIndex];
    if(field.type!="CDefString" || field.value.size()!=4) return std::nullopt;
    const auto& value=field.value;
    const uint32_t offset=uint32_t(value[0])|uint32_t(value[1])<<8|uint32_t(value[2])<<16|uint32_t(value[3])<<24;
    return offset>=4 && offset<=0x7fffffffu?std::optional<uint32_t>{offset}:std::nullopt;
}
} // namespace albion::definitionread
