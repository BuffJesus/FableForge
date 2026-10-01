#include "forge/lipsync_preset.hpp"

namespace forge::lipsync {

const std::vector<HeadPreset>& headPresets() {
    static const std::vector<HeadPreset> presets={
        {"Bandit Lieutenant","MESH_BANDITLIEUTENANT_HEAD_01",{
            {"AH","ANIM_BANDIT_PHONEME_AH"},{"EE","ANIM_BANDIT_PHONEME_EE"},
            {"MM","ANIM_BANDIT_PHONEME_MM"},{"OH","ANIM_BANDIT_PHONEME_OH"},
            {"SZ","ANIM_BANDIT_PHONEME_SZ"},{"WW","ANIM_BANDIT_PHONEME_WW"}},"MESH_EYE_BLUE_DARK",1,1.21f},
        {"Female Villager","MESH_BS_FEMALE_MIDDLE_HEAD_01",{
            {"AH","ANIM_VILLAGER_FEMALE_PHONEME_AH"},{"EE","ANIM_VILLAGER_FEMALE_PHONEME_EE"},
            {"MM","ANIM_VILLAGER_FEMALE_PHONEME_MM"},{"OH","ANIM_VILLAGER_FEMALE_PHONEME_OH"},
            {"SZ","ANIM_VILLAGER_FEMALE_PHONEME_SZ"},{"WW","ANIM_VILLAGER_FEMALE_PHONEME_WW"}},"MESH_EYE_BLUE_DARK",3,1.34f},
        {"Male Villager","MESH_BS_MALE_MIDDLE_HEAD_01",{
            {"AH","ANIM_VILLAGER_MALE_PHONEME_AH"},{"EE","ANIM_VILLAGER_MALE_PHONEME_EE"},
            {"MM","ANIM_VILLAGER_MALE_PHONEME_MM"},{"OH","ANIM_VILLAGER_MALE_PHONEME_OH"},
            {"SZ","ANIM_VILLAGER_MALE_PHONEME_SZ"},{"WW","ANIM_VILLAGER_MALE_PHONEME_WW"}},"MESH_EYE_BLUE_DARK",3,1.34f},
        {"Demon Door","MESH_DEMON_DOOR_FACE_01",{
            {"AH","ANIM_DEMON_DOOR_PHONEME_AI"},{"AI","ANIM_DEMON_DOOR_PHONEME_AI"},
            {"EE","ANIM_DEMON_DOOR_PHONEME_EE"},{"MM","ANIM_DEMON_DOOR_PHONEME_MM"},
            {"OH","ANIM_DEMON_DOOR_PHONEME_OH"},{"SZ","ANIM_DEMON_DOOR_PHONEME_ST"},
            {"WW","ANIM_DEMON_DOOR_PHONEME_WW"}}},
        {"Male Child","MESH_BS_MALE_CHILD_HEAD_01",{
            {"AH","ANIM_BIPED_BOY_01_PHONEME_AH"},{"EE","ANIM_BIPED_BOY_01_PHONEME_EE"},
            {"MM","ANIM_BIPED_BOY_01_PHONEME_MM"},{"OH","ANIM_BIPED_BOY_01_PHONEME_OH"},
            {"SZ","ANIM_BIPED_BOY_01_PHONEME_SZ"},{"WW","ANIM_BIPED_BOY_01_PHONEME_WW"}},"MESH_EYE_BLUE_DARK",3,1.30f}
    };
    return presets;
}

PresetAssets inspectHeadPreset(const big::File& graphics,const HeadPreset& preset) {
    PresetAssets result;
    result.animationIds.resize(preset.tracks.size());
    const big::Bank* bank=graphics.findBank("MBANK_ALLMESHES");
    if(!bank) {
        result.missing.push_back("MBANK_ALLMESHES");
        return result;
    }
    const auto find=[&](const std::string& name,uint32_t expectedType) {
        const big::Entry* match=nullptr;
        for(const auto& entry:bank->entries) if(entry.name==name && entry.length) {
            if(match) return uint32_t(0); // ambiguous names cannot choose a pose
            match=&entry;
        }
        return match && match->type==expectedType ? match->id : uint32_t(0);
    };
    // Retail head meshes are type 5. Keep type checks here so an unrelated
    // record with the same name cannot silently become a preview asset.
    result.meshId=find(preset.mesh,5);
    if(!result.meshId) result.missing.push_back(preset.mesh);
    if(!preset.eyeMesh.empty()) {
        result.eyeMeshId=find(preset.eyeMesh,1);
        if(!result.eyeMeshId) result.missing.push_back(preset.eyeMesh);
    }
    for(size_t i=0;i<preset.tracks.size();++i) {
        result.animationIds[i]=find(preset.tracks[i].animation,9);
        if(!result.animationIds[i]) result.missing.push_back(preset.tracks[i].animation);
    }
    return result;
}

} // namespace forge::lipsync
