#pragma once
#include "forge/stbwater.hpp"

namespace forge::stbwater {

// BuildLayersFromThemes02CB0660 stores X-major 17x17 descriptors. These are
// interpolated water levels, not ground heights or serialized draw vertices.
using ForegroundWaterLevels = std::array<float,17*17>;

// GetWaterType017A59BD: first usable type in X-major scan order, with each
// cell's native nested slot priority. Blend weights and majority do not vote.
inline int32_t foregroundWaterType(std::span<const ThemeWaterCell,17*17> cells) {
    int32_t type=0;
    for(const auto& cell:cells)if(type==0 || type==7) {
        const auto next=themeWaterType(cell);
        if(next!=0)type=next;
    }
    if(type==7)throw std::invalid_argument("foreground patch contains only dummy shore water");
    if(type<0 || type>8)throw std::invalid_argument("unsupported foreground water type");
    return type;
}

// FindCorrectWaterLevel02E67AF0 averages strictly positive ORIGINAL descriptor
// entries in the inclusive, clipped radius-two square. Repaired entries must
// never feed subsequent repairs. Native float stores occur after every add.
inline std::optional<float> foregroundWaterLevelNear(
    const ForegroundWaterLevels& source,int x,int y) {
    if(x<0 || x>16 || y<0 || y>16)
        throw std::invalid_argument("foreground water coordinate outside patch");
    float sum=0.f,count=0.f;
    for(int sx=std::max(0,x-2);sx<=std::min(16,x+2);++sx)
        for(int sy=std::max(0,y-2);sy<=std::min(16,y+2);++sy) {
            const float level=source[size_t(sx)*17+sy];
            if(!std::isfinite(level))throw std::invalid_argument("nonfinite foreground water level");
            if(level>0.f){sum=float(sum+level);count=float(count+1.f);}
        }
    if(!std::isfinite(sum))throw std::invalid_argument("overflowing foreground water level sum");
    if(count>0.f)return float(sum/count);
    return std::nullopt;
}

struct ForegroundWaterHeightData {
    bool hasWater=false;
    ForegroundWaterLevels levels{};
};

// Build02E689B0: only an originally positive descriptor makes the patch wet.
// A dry vertex borrows the source neighborhood average, otherwise ground-1.
// Inputs are patch-local and sampled before draw-buffer world quantization.
inline ForegroundWaterHeightData buildForegroundWaterHeightData(
    const ForegroundWaterLevels& source,const ForegroundWaterLevels& ground) {
    ForegroundWaterHeightData out;
    for(size_t i=0;i<source.size();++i)
        if(!std::isfinite(source[i]) || !std::isfinite(ground[i]))
            throw std::invalid_argument("nonfinite foreground water/ground input");
    for(int x=0;x<17;++x)for(int y=0;y<17;++y) {
        const size_t i=size_t(x)*17+y;
        if(source[i]>0.f){out.hasWater=true;out.levels[i]=source[i];}
        else if(const auto nearby=foregroundWaterLevelNear(source,x,y))out.levels[i]=*nearby;
        else out.levels[i]=float(ground[i]-1.f);
        if(!std::isfinite(out.levels[i]))throw std::invalid_argument("overflowing foreground water height");
    }
    return out;
}

} // namespace forge::stbwater
