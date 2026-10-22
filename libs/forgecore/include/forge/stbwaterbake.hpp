#pragma once

#include "forge/stbwaterlev.hpp"
#include "forge/stbwatertree.hpp"

namespace forge::stbwater {

// Caller supplies the native map visitation order and an explicit shore end
// guard. No assumed allocator/stack value is introduced by the composed path.
inline PreparedShoreBodies prepareLoadedShoreBodies(const LoadedWaterWorld& world,
    std::span<const size_t> maps,float endGuard,Precision precision=Precision::Float24) {
    std::vector<size_t> visited;
    std::vector<std::array<int32_t,2>> points;
    for(auto map:maps) {
        (void)world.domain(map);
        if(std::find(visited.begin(),visited.end(),map)!=visited.end())
            throw std::invalid_argument("loaded shore map visitation contains duplicates");
        world.collectShorePoints(map,points);visited.push_back(map);
    }
    return PreparedShoreBodies(buildShoreBodies(std::move(points),2.f,4.f,precision),endGuard,precision);
}

// Returned callbacks borrow both immutable inputs. Neither may be destroyed or
// moved until the synchronous bake finishes; no callback is stored in its output.
inline BackgroundWaterPolicy loadedBackgroundWaterPolicy(const LoadedWaterWorld& world,
    size_t map,const PreparedShoreBodies& shores) {
    BackgroundWaterPolicy policy;
    policy.queries=world.backgroundQueries(map,[&shores](int16_t x,int16_t y,float) {
        return shores.lookup({float(x),float(y)}).values;
    });
    policy.areaHasWater=[&shores](int32_t x0,int32_t y0,int32_t x1,int32_t y1) {
        return shores.areaHasWater(x0,y0,x1,y1);
    };
    return policy;
}

inline stbbake::BackgroundTreeLayout buildLoadedBackgroundWaterTree(
    const LoadedWaterWorld& world,size_t map,const terrain::Heightfield& heights,
    const PreparedShoreBodies& shores,const stbbake::NativeBackgroundTextureProvider& textures,
    size_t startOffset,size_t alignment=2048,stbbake::NativeBackgroundLodSettings settings={},
    const stbvisibility::BackgroundCull* visibility=nullptr) {
    const auto& domain=world.domain(map);
    if(heights.width()!=domain.width || heights.height()!=domain.height)
        throw std::invalid_argument("loaded water tree dimensions differ from source map");
    const auto raw=world.rawQueries(map);
    return buildNativeBackgroundWaterTree(heights,domain.originX,domain.originY,textures,raw.height,
        loadedBackgroundWaterPolicy(world,map,shores),startOffset,alignment,settings,visibility);
}

} // namespace forge::stbwater
