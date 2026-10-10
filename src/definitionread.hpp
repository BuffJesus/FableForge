#pragma once
#include <optional>
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
