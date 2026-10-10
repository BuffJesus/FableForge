#include "forge/defexternal.hpp"
#include "forge/animation.hpp"
#include "forge/physics.hpp"
#include "forge/meshstorage.hpp"
#include "forge/terraintex.hpp"
#include "forge/textbig.hpp"
#include "forge/bwd.hpp"

#include <stdexcept>

namespace forge::defexternal {
Review reviewLightingTga(const std::vector<uint8_t>& payload) {
    // CEnvironmentLookupTexture 02A4A830 reads an 18-byte image header and
    // loads a colour surface; LookupFloatColour 02A4AB20 consumes pixel channels.
    // Restrict storage to the fully accounted-for original lighting-table form.
    if (payload.size() < 44) return {false,"lighting TGA: truncated header/footer"};
    if (payload[2] != 2 || payload[16] != 24 || payload[17] != 0)
        return {false,"lighting TGA: unsupported image type, depth or descriptor"};
    for (size_t i = 0; i < 12; ++i)
        if (i != 2 && payload[i] != 0)
            return {false,"lighting TGA: unsupported image ID, colour map or origin"};
    const uint32_t width = uint32_t(payload[12]) | (uint32_t(payload[13]) << 8);
    const uint32_t height = uint32_t(payload[14]) | (uint32_t(payload[15]) << 8);
    if (!width || !height) return {false,"lighting TGA: empty dimensions"};
    const uint64_t pixels = uint64_t(width) * height * 3;
    if (pixels + 44 != payload.size()) return {false,"lighting TGA: inconsistent pixel extent"};
    const size_t footer = payload.size() - 26;
    for (size_t i = 0; i < 8; ++i)
        if (payload[footer + i] != 0)
            return {false,"lighting TGA: extension/developer data is unsupported"};
    constexpr char signature[] = "TRUEVISION-XFILE.";
    for (size_t i = 0; i < sizeof(signature); ++i)
        if (payload[footer + 8 + i] != uint8_t(signature[i]))
            return {false,"lighting TGA: invalid footer signature"};
    return {true,"complete supported lighting TGA pixel storage; environment bindings require resolution"};
}

Review reviewBwd(const std::vector<uint8_t>& payload) {
    try {
        // Retail CMapInfo::LoadBinary 004FB4F0 and FableWin
        // CRegion::LoadBinary 01F70940 / SaveBinary 01F70650. RegionDef is a
        // presized string; Contains/Sees arrays address this world's map slots.
        const auto world=bwd::File::parse(payload);
        if(world.serialize()!=payload)return {false,"compiled world did not round-trip exactly"};
        auto text=[](const std::string& s){return s.find('\0')==std::string::npos;};
        for(const auto& map:world.maps()) {
            if(!text(map.levelName) || !text(map.scriptName))return {false,"embedded NUL in world map name"};
            if(map.used>1 || map.loadedOnProximity>1 || map.isSea>1 || map.flag2>1)
                return {false,"unsupported compiled-world map flags"};
        }
        for(const auto& region:world.regions()) {
            for(const auto* slots:{&region.contains,&region.sees})for(int32_t slot:*slots)
                if(slot<=0 || size_t(slot)>world.maps().size())return {false,"compiled-world map slot out of bounds"};
            for(const auto* name:{&region.name,&region.displayName,&region.regionDef,&region.minimapGraphic})
                if(!text(*name))return {false,"embedded NUL in world region name"};
            for(const auto& exit:region.exits)if(!text(exit.name))return {false,"embedded NUL in world exit name"};
            if(region.onWorldMap>1 || region.creatureGen>1 || region.soundThemes>1)
                return {false,"unsupported compiled-world region flags"};
        }
        return {true,"complete compiled-world strings, coordinates, UIDs and local map-slot storage"};
    }catch(const std::runtime_error& error){return {false,error.what()};}
}

Review reviewBigEntry(std::string_view bank, uint32_t type,
                      const std::vector<uint8_t>& subHeader,
                      const std::vector<uint8_t>& payload) {
    try {
        if (bank=="GBANK_MAIN_PC" || bank=="GBANK_FRONT_END_PC" || bank=="GBANK_GUI_PC") {
            if (subHeader.size()!=34) return {false,"unsupported texture info size"};
            if (type!=0 && type!=1 && type!=2 && type!=4 && type!=5)
                return {false,"unsupported texture entry type"};
            const auto checked=terraintex::validateTextureEntry(subHeader,payload);
            if (!checked.ok || checked.consumed!=payload.size())
                return {false,checked.errors.empty()?"texture storage was not fully validated":checked.errors.front()};
            return {true,"texture dimensions, format, byte sizes and complete pixel/mip storage"};
        }
        if ((bank=="MBANK_ALLMESHES" || bank=="MBANK_ENGINE") &&
            (type==1 || type==2 || type==4 || type==5)) {
            meshstorage::validateStorage(subHeader,payload);
            return {true,"complete supported mesh geometry, local hierarchy and texture asset storage"};
        }
        if (bank=="MBANK_ALLMESHES" && type==3) {
            if (!subHeader.empty()) return {false,"unsupported physics info size"};
            physics::validateStorage(payload);
            return {true,"complete supported 3DMF geometry, material and local hierarchy storage"};
        }
        if (bank=="MBANK_ALLMESHES" && (type==6 || type==7 || type==9)) {
            // C3DAnimationInfo serialization: duration, non-looping duration,
            // movement XYZ and rotation (six floats; AnimParser.h/EgoCore).
            if (subHeader.size()!=24) return {false,"unsupported animation info size"};
            animation::validateStorage(payload);
            return {true,"animation info floats and complete known 3DAF storage"};
        }
        if (bank=="TEXT_ENGLISH_MAIN") {
            if (type==0 || type==1) {
                if (subHeader.size()!=(type==0?4u:0u))
                    return {false,"unsupported text info size"};
                const auto decoded=textbig::decode(payload,int(type));
                if (textbig::encode(decoded)!=payload)
                    return {false,"text payload did not round-trip exactly"};
                return {true,type==0?"text content, character tags and bank-local narrator index":
                                    "text group count and bank-local member IDs"};
            }
            if (type==2) {
                if (!payload.empty() || subHeader.size()<4)
                    return {false,"unsupported narrator-list framing"};
                const uint32_t count=uint32_t(subHeader[0]) | uint32_t(subHeader[1])<<8 |
                    uint32_t(subHeader[2])<<16 | uint32_t(subHeader[3])<<24;
                if (count>subHeader.size()-4) return {false,"truncated narrator list"};
                size_t at=4;
                for (uint32_t i=0;i<count;++i) {
                    while(at<subHeader.size() && subHeader[at]) ++at;
                    if(at==subHeader.size()) return {false,"unterminated narrator name"};
                    ++at;
                }
                if(at!=subHeader.size()) return {false,"trailing narrator-list bytes"};
                return {true,"narrator count and complete string list"};
            }
        }
        return {false,"unsupported external bank or entry type"};
    } catch (const std::runtime_error& error) {
        return {false,error.what()};
    }
}
} // namespace forge::defexternal
