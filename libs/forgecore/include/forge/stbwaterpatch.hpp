#pragma once

#include "forge/stbwatertrailer.hpp"
#include "forge/stbbake.hpp"

namespace forge::stbwater {

struct WaterBackgroundPatch {
    stbbake::PatchBody body;
    BackgroundMesh mesh; // raw water coordinates/heights are also needed for bounds
};

inline BackgroundMesh buildBackgroundWaterForPatch(const stbbake::PatchBody& body,
    int worldX,int worldY,const Queries& queries,bool areaHasWater,int staticQuality) {
    if(staticQuality<0 || !body.valid || !body.header.valid || body.waterOnly ||
       body.header.isWaterOnly || body.header.detailMode<1 || body.header.detailMode>7)
        throw std::invalid_argument("background water requires a valid normal ground patch/quality");
    if(!areaHasWater || staticQuality<=4)return {};
    std::vector<GroundVertex> ground;
    for(const auto& vertex:stbbake::decodePatchVertices(body))ground.push_back({vertex.gridX,vertex.gridY});
    std::vector<uint16_t> triangles;
    if(body.header.detailMode==1) {
        triangles=terrain::buildSharedLayerTopology(body.header.pw,body.header.ph).triangles;
    } else {
        const auto& block=body.ibBlock;
        if(block.size()<5)throw std::invalid_argument("background water requires a ground triangle block");
        const auto bytes=rangecodec::decodeExact(block.data()+4,block.size()-4,
            size_t(body.header.indexCount)*3,2);
        for(size_t i=0;i<bytes.size();i+=2)
            triangles.push_back(uint16_t(bytes[i]|(unsigned(bytes[i+1])<<8)));
    }
    return buildBackgroundMesh(ground,triangles,worldX,worldY,body.header.detailMode,queries);
}

// Fresh authored patch, following BuildVertexAndIndexBuffers' water gate.
// Existing world/install producers must validate their complete water policy
// before opting into this function; this does not relax NativeDryBands' guard.
inline WaterBackgroundPatch buildNativeBackgroundPatchWithWater(
    const terrain::Heightfield& heightfield,
    int patchX,int patchY,int width,int height,int worldX,int worldY,uint8_t band,
    const stbbake::BackgroundBandLockInfo& locks,
    const std::vector<uint8_t>& thresholds,const stbbake::InlineTexture& texture,
    const Queries& queries,bool areaHasWater,int staticQuality,
    const stbbake::HeightSampler& sampleHeight={}) {
    WaterBackgroundPatch result;
    result.body=stbbake::buildNativeDryBackgroundPatchRect(heightfield,patchX,patchY,
        width,height,worldX,worldY,band,locks,thresholds,texture,sampleHeight);
    result.mesh=buildBackgroundWaterForPatch(result.body,worldX,worldY,queries,areaHasWater,staticQuality);
    // Native deletes an empty subpatch after edge construction. All mappings
    // then contain -1 and its water edge arrays are empty, so retain dry bytes.
    if(!result.mesh.vertices.empty())
        result.body.trailer=addBackgroundWaterToDryTrailer(result.body.trailer,result.mesh,true);
    return result;
}

} // namespace forge::stbwater
