#pragma once
#include "forge/stbwaterforeground.hpp"
#include "forge/stbwaterforegroundio.hpp"

namespace forge::stbwater {

struct ForegroundShoreSample {
    std::array<float,12> distances=noBodyShoreLookup();
    float bodySpan=0,shoreU=0;
};
using ForegroundShoreQuery=std::function<ForegroundShoreSample(std::array<float,3>)>;
struct ForegroundWaterDraw {
    ForegroundWaterPayload payload;
    std::array<float,289> runtimeDepth{};
};
namespace detail {
inline int32_t foregroundRound(float value,HeightRounding mode) {
    if(!std::isfinite(value))throw std::invalid_argument("nonfinite foreground integer input");
    double rounded=0;
    switch(mode) {
        case HeightRounding::NearestEven: {
            const double low=std::floor(double(value)),part=double(value)-low;
            rounded=low+((part>.5 || (part==.5 && std::fmod(low,2.)!=0))?1.:0.);break;
        }
        case HeightRounding::Down:rounded=std::floor(double(value));break;
        case HeightRounding::Up:rounded=std::ceil(double(value));break;
        case HeightRounding::TowardZero:rounded=std::trunc(double(value));break;
        default:throw std::invalid_argument("invalid foreground rounding mode");
    }
    if(rounded<double(INT32_MIN) || rounded>double(INT32_MAX))
        throw std::invalid_argument("foreground integer conversion overflow");
    return int32_t(rounded);
}
template<class Real> inline ForegroundWaterDraw foregroundWaterDraw(
    const ForegroundWaterLevels& levels,const ForegroundWaterLevels& ground,
    int32_t offsetX,int32_t offsetY,std::array<float,3> origin,int32_t type,
    const ForegroundShoreQuery& shore,HeightRounding rounding) {
    // x87 precision control rounds arithmetic results, not loaded double
    // operands. Preserve double constants and CRT sin/cos results until each op.
    auto op=[](double value){return double(Real(value));};
    constexpr double tau=6.283185482025146484375;
    ForegroundWaterDraw out;auto& p=out.payload;
    p.offsetX=offsetX;p.offsetY=offsetY;p.waterType=type;
    for(int x=0;x<17;++x)for(int y=0;y<17;++y) {
        const auto i=size_t(x)*17+y;auto& v=p.vertices[i];
        const int64_t lx=int64_t(offsetX)+x,ly=int64_t(offsetY)+y;
        if(lx<INT32_MIN || lx>INT32_MAX || ly<INT32_MIN || ly>INT32_MAX)
            throw std::invalid_argument("foreground local coordinate overflow");
        const float wx=float(op(double(lx)+origin[0])),wy=float(op(double(ly)+origin[1]));
        const double bias=type==8?double(.001f):double(.1f);
        const double adjusted=std::max(0.,op(op(double(origin[2])+levels[i])-bias));
        const double scaled=op(adjusted*256.);
        if(!std::isfinite(scaled) || scaled>double(UINT32_MAX))
            throw std::invalid_argument("foreground quantized height overflow");
        // Native explicitly switches to truncation for this FISTP64, then
        // zero-extends its low32 bits; it does not use nearest-even here.
        v.height=float(op(op(double(uint32_t(std::trunc(scaled))))/256.));
        const auto coord=[](float f) {
            if(!std::isfinite(f) || double(f)<double(INT32_MIN) || double(f)>double(INT32_MAX))
                throw std::invalid_argument("foreground world coordinate overflow");
            return uint16_t(int32_t(std::trunc(double(f))));
        };
        v.x=coord(wx);v.y=coord(wy);
        auto wave=[&](bool cosine) {
            const float nx=float(op(double(wx)/tau)),ny=float(op(double(wy)/tau));
            const double ax=op(double(nx)*tau),ay=op(double(ny)*tau);
            const double sx=cosine?std::cos(ax):std::sin(ax),sy=cosine?std::cos(ay):std::sin(ay);
            const float scaledWave=float(op(op(op(sy+sx)*32767.)/2.));
            return std::bit_cast<int16_t>(uint16_t(foregroundRound(scaledWave,rounding)));
        };
        v.sinWave=wave(false);v.cosWave=wave(true);
        const float depth=float(std::clamp(op(op(double(v.height)+double(.1f))-ground[i]),0.,2.));
        out.runtimeDepth[i]=depth;
        v.depth=int16_t(foregroundRound(float(op(op(double(depth)*32767.)/2.)),rounding));
        const auto sample=shore({wx,wy,v.height});
        v.shore=sample.distances;v.shoreU=sample.shoreU;
        // Original Construct overwrites the patch span on EVERY vertex, even
        // the no-body case. Preserve the final query's value, not a maximum.
        p.waterBodySpan=sample.bodySpan;
    }
    validateForegroundWater(p);return out;
}
}
inline ForegroundWaterDraw buildForegroundWaterDraw(
    const ForegroundWaterLevels& repairedLevels,const ForegroundWaterLevels& ground,
    int32_t offsetX,int32_t offsetY,std::array<float,3> worldOrigin,int32_t waterType,
    const ForegroundShoreQuery& shore,Precision precision=Precision::Float24,
    HeightRounding rounding=HeightRounding::NearestEven) {
    if(!shore || waterType<1 || waterType>8 || waterType==7)
        throw std::invalid_argument("foreground water requires valid type and shore query");
    for(float f:worldOrigin)if(!std::isfinite(f))throw std::invalid_argument("nonfinite foreground origin");
    for(size_t i=0;i<289;++i)if(!std::isfinite(repairedLevels[i]) || !std::isfinite(ground[i]))
        throw std::invalid_argument("nonfinite foreground water/ground level");
    if(precision==Precision::Float24)return detail::foregroundWaterDraw<float>(repairedLevels,ground,offsetX,offsetY,worldOrigin,waterType,shore,rounding);
    if(precision==Precision::Float53)return detail::foregroundWaterDraw<double>(repairedLevels,ground,offsetX,offsetY,worldOrigin,waterType,shore,rounding);
    throw std::invalid_argument("invalid foreground water precision");
}

} // namespace forge::stbwater
