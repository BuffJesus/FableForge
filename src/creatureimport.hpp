#pragma once
#include "meshimport.hpp"
#include "forge/big.hpp"
#include "forge/defedit.hpp"
#include "forge/defdecode.hpp"
#include "forge/fmplinks.hpp"
#include <algorithm>
#include <stdexcept>

namespace albion::creatureimport {
struct MeshRequest {
    size_t donorIndex=0,bodyPart=0,alternative=0;
    std::string rootName,bodyName,appearanceName,meshName;
    bool compress=true;
};
// One body slot of a multi-part import. `meshName` must be unique; materials
// refer to existing texture IDs or (in publication) to this part's appended one.
struct PartRequest {
    size_t bodyPart=0,alternative=0;
    std::string meshName;
    meshimport::Model model;
    std::vector<forge::meshcompose::Material> materials;
};
struct MeshPlan {
    forge::bin::File definitions;
    forge::big::File graphics;
    forge::defedit::CreatureVisualCloneResult clones;
    forge::defedit::CreatureBodyModelResult binding;               // last part's binding
    std::vector<forge::defedit::CreatureBodyModelResult> bindings; // one per part, in order
    std::optional<forge::big::File> textures; // publication with texture append only
    uint32_t textureId=0;                     // first appended texture
    std::vector<uint32_t> textureIds;         // one per texture append, in order
};
// All-or-nothing in-memory plan. Derives the mesh donor from the selected body
// slot in the privately cloned CCreatureDef, not an unrelated caller-chosen rig.
// Three definitions and one mesh append; only the selected new body model word
// differs from canonical private cloning. All source records remain const.
// Materials may refer to prepared texture IDs; coordinated publication must
// validate those IDs and all fresh source/destination/recovery/offline state.
// This neither publishes files nor establishes animation/runtime compatibility.
// Several body slots of one private clone (e.g. creature-fit's head/torso/legs).
// Each part's donor rig is that slot's mesh in the clone as cloned, read before
// any rebinding; meshes append in order, so IDs are distinct and ascending.
inline MeshPlan prepareMeshes(const forge::bin::File& sourceDefinitions,
        const forge::big::File& sourceGraphics,const forge::defschema::Schema& schema,
        const MeshRequest& request,const std::vector<PartRequest>& parts) {
    if(parts.empty()) throw std::runtime_error("creature mesh plan: no body parts");
    for(size_t i=0;i<parts.size();++i) for(size_t j=0;j<i;++j) {
        if(parts[i].bodyPart==parts[j].bodyPart && parts[i].alternative==parts[j].alternative)
            throw std::runtime_error("creature mesh plan: body slot imported twice");
        if(parts[i].meshName==parts[j].meshName) throw std::runtime_error("creature mesh plan: repeated mesh name");
    }
    auto definitions=sourceDefinitions;
    const auto clones=forge::defedit::cloneCreatureVisuals(definitions,schema,request.donorIndex,
        request.rootName,request.bodyName,request.appearanceName);
    const auto body=definitions.entries().at(clones.bodyIndex); // as cloned, before rebinding
    const auto decoded=forge::defdecode::decode(body,schema);
    if(!decoded.clean() || forge::defdecode::encode(decoded)!=body.data)
        throw std::runtime_error("creature mesh plan: body requires clean exact decode");
    const auto layout=forge::fmplinks::creaturePrefixLayout(decoded.prefix);
    auto graphics=sourceGraphics;
    std::vector<forge::defedit::CreatureBodyModelResult> bindings;
    for(const auto& part:parts) {
        const auto slot=std::find_if(layout.bodyModels.begin(),layout.bodyModels.end(),[&](const auto& value){
            return value.bodyPart==part.bodyPart && value.alternative==part.alternative;
        });
        if(slot==layout.bodyModels.end()) throw std::runtime_error("creature mesh plan: absent body part or alternative");
        uint32_t donorId=0;
        for(size_t byte=0;byte<4;++byte) donorId|=uint32_t(body.data.at(slot->offset+byte))<<(8*byte);
        auto mesh=meshimport::prepareSkinnedMesh(part.model,graphics,donorId,part.meshName,part.materials,request.compress);
        const auto meshId=mesh.id;
        graphics.findBank("MBANK_ALLMESHES")->entries.push_back(std::move(mesh));
        bindings.push_back(forge::defedit::setCreatureBodyModel(definitions,schema,clones.bodyIndex,
            part.bodyPart,part.alternative,meshId,graphics));
    }
    MeshPlan plan{std::move(definitions),std::move(graphics),clones,bindings.back()};
    plan.bindings=std::move(bindings);
    return plan;
}
inline MeshPlan prepareMesh(const forge::bin::File& sourceDefinitions,
        const forge::big::File& sourceGraphics,const forge::defschema::Schema& schema,
        const MeshRequest& request,const meshimport::Model& model,
        const std::vector<forge::meshcompose::Material>& materials) {
    return prepareMeshes(sourceDefinitions,sourceGraphics,schema,request,
                         {PartRequest{request.bodyPart,request.alternative,request.meshName,model,materials}});
}
} // namespace albion::creatureimport
