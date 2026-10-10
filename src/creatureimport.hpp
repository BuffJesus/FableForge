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
struct MeshPlan {
    forge::bin::File definitions;
    forge::big::File graphics;
    forge::defedit::CreatureVisualCloneResult clones;
    forge::defedit::CreatureBodyModelResult binding;
};
// All-or-nothing in-memory plan. Derives the mesh donor from the selected body
// slot in the privately cloned CCreatureDef, not an unrelated caller-chosen rig.
// Three definitions and one mesh append; only the selected new body model word
// differs from canonical private cloning. All source records remain const.
// Materials may refer to prepared texture IDs; coordinated publication must
// validate those IDs and all fresh source/destination/recovery/offline state.
// This neither publishes files nor establishes animation/runtime compatibility.
inline MeshPlan prepareMesh(const forge::bin::File& sourceDefinitions,
        const forge::big::File& sourceGraphics,const forge::defschema::Schema& schema,
        const MeshRequest& request,const meshimport::Model& model,
        const std::vector<forge::meshcompose::Material>& materials) {
    auto definitions=sourceDefinitions;
    const auto clones=forge::defedit::cloneCreatureVisuals(definitions,schema,request.donorIndex,
        request.rootName,request.bodyName,request.appearanceName);
    const auto& body=definitions.entries().at(clones.bodyIndex);
    const auto decoded=forge::defdecode::decode(body,schema);
    if(!decoded.clean() || forge::defdecode::encode(decoded)!=body.data)
        throw std::runtime_error("creature mesh plan: body requires clean exact decode");
    const auto layout=forge::fmplinks::creaturePrefixLayout(decoded.prefix);
    const auto slot=std::find_if(layout.bodyModels.begin(),layout.bodyModels.end(),[&](const auto& value){
        return value.bodyPart==request.bodyPart && value.alternative==request.alternative;
    });
    if(slot==layout.bodyModels.end()) throw std::runtime_error("creature mesh plan: absent body part or alternative");
    uint32_t donorId=0;
    for(size_t byte=0;byte<4;++byte) donorId|=uint32_t(body.data.at(slot->offset+byte))<<(8*byte);
    auto mesh=meshimport::prepareSkinnedMesh(model,sourceGraphics,donorId,request.meshName,materials,request.compress);
    const auto meshId=mesh.id;
    auto graphics=sourceGraphics;
    graphics.findBank("MBANK_ALLMESHES")->entries.push_back(std::move(mesh));
    const auto binding=forge::defedit::setCreatureBodyModel(definitions,schema,clones.bodyIndex,
        request.bodyPart,request.alternative,meshId,graphics);
    return {std::move(definitions),std::move(graphics),clones,binding};
}
} // namespace albion::creatureimport
