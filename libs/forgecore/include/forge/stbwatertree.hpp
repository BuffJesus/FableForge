#pragma once

#include "forge/stbwaterpatch.hpp"
#include "forge/stbvisibility.hpp"

namespace forge::stbwater {

struct BackgroundWaterPolicy {
    Queries queries;
    // Original coarse water-body bounds test, in world coordinates.
    std::function<bool(int32_t,int32_t,int32_t,int32_t)> areaHasWater;
};

// Fresh native background tree with coherent ground/water samples. A supplied
// visibility policy contributes observer boxes/precision; its surface sampler
// is replaced by these same source queries. Without visibility, the original
// uncullled band decisions are retained and every node still gets water bounds.
inline stbbake::BackgroundTreeLayout buildNativeBackgroundWaterTree(
    const terrain::Heightfield& heights,int worldX,int worldY,
    const stbbake::NativeBackgroundTextureProvider& textures,
    const stbbake::HeightSampler& ground,const BackgroundWaterPolicy& water,
    size_t startOffset,size_t alignment=2048,
    stbbake::NativeBackgroundLodSettings settings={},
    const stbvisibility::BackgroundCull* visibility=nullptr) {
    if(!ground || !water.queries.hasWater || !water.queries.waterType ||
       !water.queries.waterHeight || !water.queries.shore || !water.areaHasWater)
        throw std::invalid_argument("native water tree requires complete source queries and area policy");
    const stbvisibility::SurfaceSampler surface=[&](int x,int y) {
        const float height=ground(x,y),level=water.queries.waterHeight(x,y,1);
        if(!std::isfinite(height) || !std::isfinite(level))
            throw std::invalid_argument("nonfinite native water tree surface");
        return std::array<float,2>{height,level};
    };
    stbvisibility::BackgroundCull cull;
    if(visibility){cull=*visibility;cull.surfaceAt=surface;}
    auto root=stbbake::buildNativeAdaptiveBackgroundTreeShape(heights,worldX,worldY,
        settings,ground,visibility?&cull:nullptr);
    if(!visibility) {
        auto bounds=[&](auto&& self,stbbake::BackgroundTreeNode& node)->void {
            const auto box=stbvisibility::backgroundBounds(node.header.mapX,node.header.mapY,
                node.header.width,node.header.height,{float(worldX),float(worldY),0},surface);
            std::copy(box.begin(),box.end(),node.header.aabb);
            for(auto& child:node.children)self(self,child);
        };bounds(bounds,root);
    }
    const stbbake::NativeBackgroundTrailerProvider trailer=[&](const stbbake::PatchBody& body) {
        const int32_t x=worldX+body.header.coord0,y=worldY+body.header.coord1;
        const bool hasWater=water.areaHasWater(x,y,x+body.header.pw,y+body.header.ph);
        const auto mesh=buildBackgroundWaterForPatch(body,worldX,worldY,water.queries,hasWater,settings.staticMapQuality);
        return mesh.vertices.empty()?body.trailer:addBackgroundWaterToDryTrailer(body.trailer,mesh,true);
    };
    return stbbake::layoutNativeBackgroundTree(std::move(root),heights,worldX,worldY,textures,
        trailer,startOffset,alignment,settings,ground);
}

} // namespace forge::stbwater
