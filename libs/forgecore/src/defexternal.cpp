#include "forge/defexternal.hpp"
#include "forge/animation.hpp"
#include "forge/physics.hpp"
#include "forge/meshstorage.hpp"
#include "forge/terraintex.hpp"
#include "forge/textbig.hpp"

#include <stdexcept>

namespace forge::defexternal {
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
