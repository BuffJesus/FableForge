#pragma once

// Authored water mesh and shoreline kernels, recovered from FableWin.
// Map/theme sampling remains external; shore collection, bodies and lookup are owned here.
// Payload serialization lives in stbwaterio.hpp; wet seams and complete wet
// terrain bakes still require separate producer integration and validation.
#include <array>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace forge::stbwater {

// Constructor02E064A0 initializes this table to -1. The no-body branch of
// ConstructVertexDistanceToShoreArray copies it verbatim, without delta coding.
inline constexpr std::array<float,12> noBodyShoreLookup() {
    std::array<float,12> result{};result.fill(-1.f);return result;
}

enum class Precision { Float24,Float53 };
enum class HeightRounding { NearestEven,Down,Up,TowardZero };
// QuantiseLandscapeHeight02CCCF70: scale by DOUBLE128, store float, FISTP int32,
// divide by128, store float. FISTP obeys the caller's rounding control. Keep that
// explicit instead of allowing the host process's floating environment to leak.
inline float quantizeMapHeight(float height,HeightRounding mode=HeightRounding::NearestEven) {
    const float scaled=height*128.f;
    if(!std::isfinite(scaled))throw std::invalid_argument("nonfinite or overflowing map height");
    double rounded=0;
    switch(mode) {
        case HeightRounding::NearestEven: {
            const double lower=std::floor(double(scaled)),fraction=double(scaled)-lower;
            rounded=lower+((fraction>.5 || (fraction==.5 && std::fmod(lower,2.)!=0))?1.:0.);break;
        }
        case HeightRounding::Down:rounded=std::floor(double(scaled));break;
        case HeightRounding::Up:rounded=std::ceil(double(scaled));break;
        case HeightRounding::TowardZero:rounded=std::trunc(double(scaled));break;
        default:throw std::invalid_argument("invalid map height rounding mode");
    }
    if(rounded<INT32_MIN || rounded>INT32_MAX)
        throw std::invalid_argument("map height exceeds native int32 quantization range");
    return float(int32_t(rounded))/128.f; // native FILD also canonicalizes signed zero
}

// Theme IDs and class entries after native palette/definition resolution.
// Global ID<=0 stops the fast/depth slot chain; class index0 has independent
// sentinel behavior in the type query. Preserve both rather than conflating them.
struct ThemeWaterSlot {
    int32_t globalId=0,classIndex=0,waterType=0;
    float depth=0;
};
struct ThemeWaterCell {
    std::array<ThemeWaterSlot,3> slots{};
    std::array<uint8_t,2> blends{255,0}; // third byte = BYTE(255-b0-b1)
};
inline bool themeHasWaterFast(const ThemeWaterCell& cell) {
    for(const auto& slot:cell.slots) {
        if(slot.globalId<=0)break;
        if(slot.waterType!=0)return true;
    }
    return false;
}
namespace detail {
template<class Real> inline float themeDepth(const ThemeWaterCell& cell) {
    const std::array<uint8_t,3> weights={cell.blends[0],cell.blends[1],
        uint8_t(255-int(cell.blends[0])-int(cell.blends[1]))};
    float sum=0;
    for(unsigned i=0;i<3;++i) {
        const auto& slot=cell.slots[i];if(slot.globalId<=0)break;
        if(!std::isfinite(slot.depth))throw std::invalid_argument("nonfinite water theme depth");
        const Real term=(Real(weights[i])*Real(slot.depth))/Real(255);
        sum=i?float(term+Real(sum)):float(term); // store after every slot
        if(!std::isfinite(sum))throw std::invalid_argument("water theme depth overflow");
    }
    return sum;
}
}
inline float themeWaterDepth(const ThemeWaterCell& cell,Precision precision=Precision::Float24) {
    if(precision==Precision::Float53)return detail::themeDepth<double>(cell);
    if(precision!=Precision::Float24)throw std::invalid_argument("invalid water theme precision");
    return detail::themeDepth<float>(cell);
}
inline bool themeHasWater(const ThemeWaterCell& cell,Precision precision=Precision::Float24) {
    return themeHasWaterFast(cell) && themeWaterDepth(cell,precision)>.001f;
}
// Native02D5D780 prioritizes slots, not blend magnitude. Type7 can be replaced;
// the third-slot test is nested under a nonzero second-slot class AND type.
inline int32_t themeWaterType(const ThemeWaterCell& cell) {
    int32_t type=cell.slots[0].waterType;
    if(cell.slots[1].classIndex!=0 && cell.slots[1].waterType!=0) {
        if(type==0 || type==7)type=cell.slots[1].waterType;
        if(cell.slots[2].classIndex!=0 && cell.slots[2].waterType!=0 && (type==0 || type==7))
            type=cell.slots[2].waterType;
    }
    return type;
}

// Native map-query routing uses ushort dimensions/origins. Sparse edge storage
// can lack interior data, so a rectangle alone is not a sufficient presence test.
struct WaterMapDomain {
    uint16_t width=0,height=0,originX=0,originY=0;
    std::function<bool(int,int)> hasData;
};
struct WaterMapSample {
    size_t map=0;
    int x=0,y=0;
    bool operator==(const WaterMapSample&) const=default;
};
using WaterMapAt=std::function<std::optional<size_t>(int,int)>;
// Native CEngineWorldMap::GetMap02CC98A0. Grid entries are SIGNED shorts;
// native slot0 and negative entries have no owner. Each grid cell covers32 units.
// The placement table is supplied in engine order, not regenerated from bounds.
inline std::optional<size_t> waterWorldMapAt(int x,int y,int originX,int originY,
    int width,int height,std::span<const int16_t> grid) {
    if(width<0 || height<0 || uint64_t(width)*uint64_t(height)!=grid.size())
        throw std::invalid_argument("invalid world water placement grid");
    const int64_t dx=int64_t(x)-originX,dy=int64_t(y)-originY;
    if(dx<0 || dy<0 || dx>=int64_t(width)*32 || dy>=int64_t(height)*32)return std::nullopt;
    const int16_t slot=grid[size_t(dx/32)+size_t(dy/32)*size_t(width)];
    if(slot<1)return std::nullopt;
    return size_t(slot);
}
struct WaterWorldGrid {
    int originX=0,originY=0,width=0,height=0;
    std::vector<int16_t> slots;
    std::optional<size_t> mapAt(int x,int y) const {
        return waterWorldMapAt(x,y,originX,originY,width,height,slots);
    }
};
// Fresh-grid branch of AutoBuildWorldMapGrid02D60AE0 plus LinkMapToWorld02D5F960.
// Input indices ARE native slots; null entries remain absent. An existing engine
// grid must be retained by the caller. Overlapping32-unit cells trigger native's
// assertion; refuse atomically instead of choosing an arbitrary winning map.
inline std::optional<WaterWorldGrid> buildWaterWorldGrid(std::span<const std::optional<WaterMapDomain>> maps) {
    if(maps.size()>32768)throw std::invalid_argument("water map slot exceeds signed placement format");
    int minX=INT32_MAX,minY=INT32_MAX,maxX=0,maxY=0;
    bool found=false;
    for(const auto& map:maps)if(map) {
        minX=std::min(minX,int(map->originX));minY=std::min(minY,int(map->originY));
        maxX=std::max(maxX,int(map->originX)+map->width);
        maxY=std::max(maxY,int(map->originY)+map->height);found=true;
    }
    if(!found)return std::nullopt;
    WaterWorldGrid grid;
    const int blockX=minX/32-1,blockY=minY/32-1;
    grid.originX=blockX*32;grid.originY=blockY*32;
    grid.width=(maxX+31)/32+1-blockX;grid.height=(maxY+31)/32+1-blockY;
    grid.slots.assign(size_t(grid.width)*size_t(grid.height),int16_t(-1));
    for(size_t slot=0;slot<maps.size();++slot)if(maps[slot]) {
        const auto& map=*maps[slot];
        const int x0=(int(map.originX)-grid.originX)/32,y0=(int(map.originY)-grid.originY)/32;
        const int x1=(int(map.originX)+map.width-grid.originX)/32;
        const int y1=(int(map.originY)+map.height-grid.originY)/32;
        for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x) {
            auto& cell=grid.slots[size_t(y)*size_t(grid.width)+size_t(x)];
            if(cell!=-1)throw std::invalid_argument("water maps overlap a native placement cell");
            cell=int16_t(slot);
        }
    }
    return grid;
}
// Shared routing in native PeekThemeId/Blend/LandscapeHeight. The world owner is
// supplied explicitly; never replace its placement table with nearest-map search.
inline WaterMapSample resolveWaterMapSample(std::span<const WaterMapDomain> maps,
    size_t current,int x,int y,const WaterMapAt& worldMapAt) {
    auto domain=[&](size_t index)->const WaterMapDomain& {
        if(index>=maps.size())throw std::invalid_argument("water sample map outside domain list");
        const auto& map=maps[index];
        if(!map.width || !map.height || !map.hasData)
            throw std::invalid_argument("invalid water sample map domain");
        return map;
    };
    auto has=[](const WaterMapDomain& map,int i,int j) {
        return i>=0 && j>=0 && i<map.width && j<map.height && map.hasData(i,j);
    };
    const auto& source=domain(current);
    if(has(source,x,y))return {current,x,y};
    if(!worldMapAt)throw std::invalid_argument("missing world water sample owner query");
    const int64_t wx=int64_t(source.originX)+x,wy=int64_t(source.originY)+y;
    if(wx<std::numeric_limits<int>::min() || wx>std::numeric_limits<int>::max() ||
       wy<std::numeric_limits<int>::min() || wy>std::numeric_limits<int>::max())
        throw std::invalid_argument("world water sample coordinate overflow");
    if(const auto selected=worldMapAt(int(wx),int(wy))) {
        const auto& target=domain(*selected);
        const int64_t tx=wx-target.originX,ty=wy-target.originY;
        if(tx>=0 && ty>=0 && tx<target.width && ty<target.height && has(target,int(tx),int(ty)))
            return {*selected,int(tx),int(ty)};
    }
    // Even an interior hole falls back to the original map; missing neighbors
    // clamp independently on each axis rather than returning a dry/zero sample.
    return {current,std::clamp(x,0,int(source.width)-1),std::clamp(y,0,int(source.height)-1)};
}

// Raw map queries resolve out-of-map coordinates through their owner. Native
// interpolation never clamps its neighborhood to the current map's bounds.
struct RawMapWaterQueries {
    std::function<bool(int,int)> fast,slow;
    std::function<int32_t(int,int)> type;
    std::function<float(int,int)> depth,height;
};
namespace detail {
inline std::array<int,4> waterQueryBounds(int x,int y,int radius) {
    if(radius<0 || int64_t(x)-radius<std::numeric_limits<int>::min() ||
       int64_t(y)-radius<std::numeric_limits<int>::min() ||
       int64_t(x)+radius>std::numeric_limits<int>::max() ||
       int64_t(y)+radius>std::numeric_limits<int>::max())
        throw std::invalid_argument("invalid water query radius/coordinate range");
    return {x-radius,y-radius,x+radius,y+radius};
}
template<class Real> inline float interpolatedWaterLevel(int x,int y,int radius,
    const RawMapWaterQueries& query,bool includeHeight) {
    auto bounds=waterQueryBounds(x,y,radius);
    if(!query.fast || !(includeHeight?bool(query.slow):bool(query.fast)) ||
       !query.depth || (includeHeight && !query.height))
        throw std::invalid_argument("missing water level queries");
    // Height tests SLOW at the center; depth tests FAST. Both recenter on the
    // first FAST neighbor, including neighbors whose positive depth is absent.
    if(!(includeHeight?query.slow(x,y):query.fast(x,y))) {
        bool found=false;
        for(int64_t i=bounds[0];i<=bounds[2] && !found;++i)
            for(int64_t j=bounds[1];j<=bounds[3] && !found;++j)
                if(query.fast(int(i),int(j))) {
                    bounds=waterQueryBounds(int(i),int(j),radius);found=true;
                }
        if(!found)return 0;
    }
    float sum=0,count=0;
    for(int64_t i=bounds[0];i<=bounds[2];++i)for(int64_t j=bounds[1];j<=bounds[3];++j) {
        const float depth=query.depth(int(i),int(j));
        if(!std::isfinite(depth))throw std::invalid_argument("nonfinite sampled water depth");
        if(depth>.001f) {
            if(includeHeight) {
                const float height=query.height(int(i),int(j));
                if(!std::isfinite(height))throw std::invalid_argument("nonfinite sampled terrain height");
                sum=float((Real(height)+Real(depth))+Real(sum));
            } else sum=float(Real(sum)+Real(depth));
            count=float(Real(count)+Real(1));
            if(!std::isfinite(sum))throw std::invalid_argument("water level accumulation overflow");
        }
    }
    return count>0?float(Real(sum)/Real(count)):0;
}
}
inline bool interpolatedHasWater(int x,int y,int radius,const RawMapWaterQueries& query,bool fast=false) {
    const auto bounds=detail::waterQueryBounds(x,y,radius);
    const auto& has=fast?query.fast:query.slow;
    if(!has)throw std::invalid_argument("missing water presence query");
    for(int64_t i=bounds[0];i<=bounds[2];++i)for(int64_t j=bounds[1];j<=bounds[3];++j)
        if(has(int(i),int(j)))return true;
    return false;
}
inline int32_t interpolatedWaterType(int x,int y,int radius,const RawMapWaterQueries& query) {
    const auto bounds=detail::waterQueryBounds(x,y,radius);
    if(!query.type)throw std::invalid_argument("missing water type query");
    int32_t result=0;
    for(int64_t i=bounds[0];i<=bounds[2];++i)for(int64_t j=bounds[1];j<=bounds[3];++j) {
        const int32_t type=query.type(int(i),int(j));
        if(result==0 || result==7)result=type; // still query every cell after selection
    }
    return result;
}
inline float interpolatedWaterLevel(int x,int y,int radius,const RawMapWaterQueries& query,
    bool includeHeight,Precision precision=Precision::Float24) {
    if(precision==Precision::Float53)return detail::interpolatedWaterLevel<double>(x,y,radius,query,includeHeight);
    if(precision!=Precision::Float24)throw std::invalid_argument("invalid water query precision");
    return detail::interpolatedWaterLevel<float>(x,y,radius,query,includeHeight);
}

struct ShoreMapQueries {
    std::function<bool(int,int,int)> hasWater; // native slow presence query
    std::function<float(int,int,int)> depth;
    std::function<bool(int,int)> hasType7;
};
// Native FindShorePointsInMap02E07490. Dimensions are native PeekWidth/Height
// results. Queries are map-local and radius1. Returns this map's
// new points, while boundary matches remove the FIRST existing world point.
// Local additions append to the world array only after the complete scan.
inline std::vector<std::array<int32_t,2>> collectMapShorePoints(int width,int height,
    int32_t worldX,int32_t worldY,const ShoreMapQueries& query,
    std::vector<std::array<int32_t,2>>& accumulated) {
    if(width<0 || height<0 || !query.hasWater || !query.depth || !query.hasType7)
        throw std::invalid_argument("invalid shore map dimensions/queries");
    if(int64_t(worldX)+width>std::numeric_limits<int32_t>::max() ||
       int64_t(worldY)+height>std::numeric_limits<int32_t>::max())
        throw std::invalid_argument("shore map boundary coordinate overflow");
    auto next=accumulated;
    std::vector<std::array<int32_t,2>> added;
    const int maxX=std::max(0,width-1),maxY=std::max(0,height-1);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
        std::array<int32_t,2> point={worldX+x,worldY+y};
        if(!query.hasWater(x,y,1)) {
            bool nearby=false;
            for(int j=std::max(0,y-1);j<=std::min(maxY,y+1);++j)
                for(int i=std::max(0,x-1);i<=std::min(maxX,x+1);++i) {
                    const float depth=query.depth(i,j,1);
                    if(!std::isfinite(depth))throw std::invalid_argument("nonfinite shore depth query");
                    nearby=nearby | (depth>=0.001f); // native never short-circuits
                }
            if(nearby) {
                if(std::find(added.begin(),added.end(),point)!=added.end())
                    throw std::invalid_argument("duplicate dry shore point");
                added.push_back(point);
            }
        } else if(query.hasType7(x,y)) {
            if(std::find(added.begin(),added.end(),point)==added.end())added.push_back(point);
        } else if(x==0 || x==maxX || y==0 || y==maxY) {
            if(x==maxX)++point[0];
            else if(y==maxY)++point[1];
            const auto found=std::find(next.begin(),next.end(),point);
            if(found==next.end())added.push_back(point);
            else next.erase(found);
        }
    }
    next.insert(next.end(),added.begin(),added.end());
    accumulated=std::move(next);
    return added;
}

struct ShorePointChoice {
    std::optional<size_t> index;
    std::vector<size_t> duplicateCurrentPoints;
};
namespace detail {
template<class Real> inline float shorePointDistance(std::array<int32_t,2> a,std::array<int32_t,2> b) {
    std::array<float,2> delta{};
    for(unsigned axis=0;axis<2;++axis) {
        const auto d=int64_t(a[axis])-b[axis];
        if(d<std::numeric_limits<int32_t>::min() || d>std::numeric_limits<int32_t>::max())
            throw std::invalid_argument("shore point distance coordinate overflow");
        delta[axis]=float(d);
    }
    const float square=float(Real(delta[0])*Real(delta[0])+Real(delta[1])*Real(delta[1]));
    return std::sqrt(square);
}
template<class Real> inline ShorePointChoice chooseShorePoint(
    std::span<const std::array<int32_t,2>> points,
    std::span<const std::array<int32_t,2>> currentBody,
    std::array<int32_t,2> current,float radius) {
    ShorePointChoice result;float closest=std::numeric_limits<float>::max();
    for(size_t i=0;i<points.size();++i) {
        const auto& point=points[i];
        if(point==current) {result.duplicateCurrentPoints.push_back(i);continue;}
        const float distance=shorePointDistance<Real>(current,point);
        if(distance>radius || distance>=closest)continue;
        bool alreadyUsed=false;
        for(const auto& used:currentBody)if(point==used){alreadyUsed=true;break;}
        if(alreadyUsed)continue;
        result.index=i;closest=distance;
    }
    return result;
}
}
// Native02E08010: inclusive radius, first equal-distance candidate, no repeated
// current-body coordinates. Duplicates of current are queued for later deletion
// in source order; this helper leaves the source container untouched.
inline ShorePointChoice chooseShorePoint(std::span<const std::array<int32_t,2>> points,
    std::span<const std::array<int32_t,2>> currentBody,std::array<int32_t,2> current,
    float radius,Precision precision=Precision::Float24) {
    if(!std::isfinite(radius) || radius<0 ||
       (precision!=Precision::Float24 && precision!=Precision::Float53))
        throw std::invalid_argument("invalid shoreline connection radius/precision");
    if(precision==Precision::Float53)return detail::chooseShorePoint<double>(points,currentBody,current,radius);
    return detail::chooseShorePoint<float>(points,currentBody,current,radius);
}

// Native GFArcCos01DF7340 stores radians to float before normalizing;
// CosThetaToBufferIndex02E09BD0 stores the mirrored/scaled angle, then floors.
inline unsigned shoreBufferIndex(float cosine,bool negativeX) {
    if(!std::isfinite(cosine) || cosine < -1 || cosine > 1)
        throw std::invalid_argument("invalid shoreline cosine");
    const float radians=float(std::acos(double(cosine)));
    float angle=radians/6.2831854820251465f;
    if(negativeX)angle=1.f-angle;
    const float position=511.f*angle;
    return unsigned(std::floor(position));
}

struct ShoreBounds {
    std::array<int32_t,2> minimum{},maximum{};
    bool operator==(const ShoreBounds&) const=default;
};
inline void validateShoreBounds(std::span<const ShoreBounds> bodies) {
    for(const auto& body:bodies)for(unsigned axis=0;axis<2;++axis)
        if(body.minimum[axis]>body.maximum[axis])
            throw std::invalid_argument("reversed water body bounds");
}
// Native AreaHasWater02E08CB0 tests inclusive metadata boxes, including boundary
// contact. Either orientation of query endpoints is accepted. No polygon or
// nearest-body substitution belongs in this coarse background-patch gate.
inline bool areaHasWater(std::span<const ShoreBounds> bodies,int32_t x0,int32_t y0,int32_t x1,int32_t y1) {
    validateShoreBounds(bodies);
    const int32_t left=std::min(x0,x1),right=std::max(x0,x1);
    const int32_t bottom=std::min(y0,y1),top=std::max(y0,y1);
    for(const auto& b:bodies)
        if(b.minimum[0]<=right && b.maximum[0]>=left && b.minimum[1]<=top && b.maximum[1]>=bottom)return true;
    return false;
}
// Native02E08B60 selects the first inclusive bounding box. Outside every box
// it returns no body; it never substitutes the nearest box or a polygon test.
inline std::optional<size_t> firstContainingWaterBody(std::span<const ShoreBounds> bodies,
    std::array<float,2> query) {
    validateShoreBounds(bodies);
    for(auto value:query)if(!std::isfinite(value))
        throw std::invalid_argument("nonfinite water body query");
    for(size_t i=0;i<bodies.size();++i) {
        const auto& b=bodies[i];
        // Native FILD compares exact int32 bounds with float coordinates.
        if(double(query[0])>=double(b.minimum[0]) && double(query[0])<=double(b.maximum[0]) &&
           double(query[1])>=double(b.minimum[1]) && double(query[1])<=double(b.maximum[1]))return i;
    }
    return std::nullopt;
}
// Native02E0A4C0 accepts nested inclusive boxes, excluding ALL boxes with
// identical bounds, even if they represent different body objects.
inline std::vector<size_t> possibleIslandBodies(std::span<const ShoreBounds> bodies,size_t owner) {
    validateShoreBounds(bodies);
    if(owner>=bodies.size())throw std::invalid_argument("water body owner outside bounds list");
    const auto& outer=bodies[owner];std::vector<size_t> result;
    for(size_t i=0;i<bodies.size();++i) {
        const auto& b=bodies[i];
        if(b==outer)continue;
        if(outer.minimum[0]<=b.minimum[0] && outer.minimum[1]<=b.minimum[1] &&
           b.maximum[0]<=outer.maximum[0] && b.maximum[1]<=outer.maximum[1])result.push_back(i);
    }
    return result;
}

// Native02E093A0 linearly interpolates shore coordinates across an already
// unwrapped angular span, keeping the closest contribution in each slot.
// first/last correspond to the lower/upper unwrapped slot, respectively.
namespace detail {
template<class Real> inline void shoreSegment(std::array<float,512>& buffer,
    std::array<float,2> query,unsigned begin,unsigned end,
    std::array<int32_t,2> first,std::array<int32_t,2> last) {
    std::array<float,257> distances{};
    for(unsigned slot=begin;slot<=end;++slot) {
        const float t=end==begin?0.f:float(slot-begin)/float(end-begin);
        auto coordinate=[&](unsigned axis) {
            if constexpr(std::is_same_v<Real,float>) {
                // FILD keeps an integer exact until FMUL/FADD apply PC24.
                // Avoid rounding a large integer before the multiplication.
                const float product=float(static_cast<long double>(last[axis]-first[axis])*t);
                return float(static_cast<long double>(product)+first[axis]);
            } else return float(Real(last[axis]-first[axis])*Real(t)+Real(first[axis]));
        };
        const float px=coordinate(0),py=coordinate(1);
        const float x=px-query[0],y=py-query[1];
        const float square=float(Real(x)*Real(x)+Real(y)*Real(y));
        const float distance=std::sqrt(square);
        if(!std::isfinite(distance))throw std::invalid_argument("shore segment distance overflow");
        distances[slot-begin]=distance;
    }
    for(unsigned slot=begin;slot<=end;++slot) {
        auto& value=buffer[slot%512];const float distance=distances[slot-begin];
        if(value<0 || value>distance)value=distance;
    }
}
}
inline void accumulateShoreSegment(std::array<float,512>& buffer,std::array<float,2> query,
    unsigned begin,unsigned end,std::array<int32_t,2> first,std::array<int32_t,2> last,
    Precision precision=Precision::Float24) {
    if(begin>end || end>1023 || end-begin>256 ||
       (precision!=Precision::Float24 && precision!=Precision::Float53))
        throw std::invalid_argument("invalid unwrapped shore segment span/precision");
    for(auto value:buffer)if(!std::isfinite(value))
        throw std::invalid_argument("nonfinite shore buffer entry");
    for(auto value:query)if(!std::isfinite(value))
        throw std::invalid_argument("nonfinite shore query coordinate");
    for(unsigned axis=0;axis<2;++axis) {
        const auto delta=int64_t(last[axis])-first[axis];
        if(delta<std::numeric_limits<int32_t>::min() || delta>std::numeric_limits<int32_t>::max())
            throw std::invalid_argument("shore segment coordinate delta overflow");
    }
    if(precision==Precision::Float53)detail::shoreSegment<double>(buffer,query,begin,end,first,last);
    else detail::shoreSegment<float>(buffer,query,begin,end,first,last);
}

struct ShoreContourState {
    std::array<float,512> distances=[] {std::array<float,512> a;a.fill(-1.f);return a;}();
    int32_t nearestPoint=-1;
    float nearestDistance=std::numeric_limits<float>::max();
};
namespace detail {
template<class Real> inline void shoreContour(ShoreContourState& state,
    std::span<const std::array<int32_t,2>> points,std::array<float,2> query) {
    auto measure=[&](const std::array<int32_t,2>& point) {
        auto relative=[&](unsigned axis) {
            if constexpr(std::is_same_v<Real,float>)
                return float(static_cast<long double>(point[axis])-query[axis]);
            else return float(Real(point[axis])-Real(query[axis]));
        };
        const float x=relative(0),y=relative(1);
        const float square=float(Real(x)*Real(x)+Real(y)*Real(y));
        const float distance=std::sqrt(square);
        if(!std::isfinite(distance))throw std::invalid_argument("shore contour distance overflow");
        const float cosine=distance<=0.10000000149011612?0.f:y/distance;
        return std::pair{distance,shoreBufferIndex(cosine,x<0)};
    };
    auto previous=points.back();auto [previousDistance,previousSlot]=measure(previous);
    for(size_t i=0;i<points.size();++i) {
        const auto [distance,currentSlot]=measure(points[i]);
        if(distance<state.nearestDistance) {state.nearestDistance=distance;state.nearestPoint=int32_t(i);}
        if(currentSlot==previousSlot) {
            // Native keeps the previous distance AND previous point in this
            // branch. Advancing them here changes same-angle contour runs.
            auto& entry=state.distances[currentSlot];
            if(entry<0 || entry>previousDistance)entry=previousDistance;
        } else {
            for(unsigned axis=0;axis<2;++axis) {
                const auto delta=int64_t(points[i][axis])-previous[axis];
                if(delta<std::numeric_limits<int32_t>::min() || delta>std::numeric_limits<int32_t>::max())
                    throw std::invalid_argument("shore contour segment delta overflow");
            }
            unsigned a=previousSlot,b=currentSlot;
            if(int(b)+512-int(a)<int(a)-int(b))b+=512;
            else if(int(a)+512-int(b)<int(b)-int(a))a+=512;
            if(a<b)shoreSegment<Real>(state.distances,query,a,b,previous,points[i]);
            else shoreSegment<Real>(state.distances,query,b,a,points[i],previous);
            previousSlot=b%512;previous=points[i];previousDistance=distance;
        }
    }
}
}
// Native FillBufferWithShoreDistances02E096F0. Pass contour coordinates only:
// the native array's final two bbox metadata coordinates are not shore points.
// Accumulates one body/island into existing state, atomically on success.
inline void accumulateShoreContour(ShoreContourState& state,
    std::span<const std::array<int32_t,2>> points,std::array<float,2> query,
    Precision precision=Precision::Float24) {
    if(points.empty() || points.size()>size_t(std::numeric_limits<int32_t>::max()) ||
       (precision!=Precision::Float24 && precision!=Precision::Float53))
        throw std::invalid_argument("invalid shoreline contour/precision");
    for(auto value:query)if(!std::isfinite(value))throw std::invalid_argument("nonfinite shoreline query");
    for(auto value:state.distances)if(!std::isfinite(value))throw std::invalid_argument("nonfinite shoreline state");
    if(!std::isfinite(state.nearestDistance) || state.nearestDistance<0)
        throw std::invalid_argument("invalid nearest shoreline distance");
    auto next=state;
    if(precision==Precision::Float53)detail::shoreContour<double>(next,points,query);
    else detail::shoreContour<float>(next,points,query);
    state=next;
}

namespace detail {
template<class Real> inline void resolveShore(std::array<float,512>& buffer,float endGuard) {
    auto invalid=[&](int index) {return index==512?endGuard<0:buffer[size_t(index)]<0;};
    for(int at=0;at<512;++at)if(invalid(at)) {
        int left=at-1,right=at+1,count=1;
        if(left<0) {left=511;while(invalid(left)){--left;++count;}}
        while(invalid(right)){++right;++count;}
        if(right<left)right+=512;
        for(int slot=left;slot<right;++slot) {
            const float fraction=float(slot-left)/float(count);
            const Real delta=Real(buffer[size_t(right%512)])-Real(buffer[size_t(left)]);
            buffer[size_t(slot%512)]=float(delta*Real(fraction)+Real(buffer[size_t(left)]));
        }
    }
}
}
// Native02E08E10 counts INVALID entries as the interpolation denominator,
// rather than the endpoint span. A trailing invalid run reads a guard at512.
// Require that guard explicitly, finite/nonnegative, to bound native semantics;
// this does not claim the original caller's uninitialized stack guard value.
inline void resolveShoreBuffer(std::array<float,512>& buffer,float endGuard,
    Precision precision=Precision::Float24) {
    if(!std::isfinite(endGuard) || endGuard<0 ||
       (precision!=Precision::Float24 && precision!=Precision::Float53))
        throw std::invalid_argument("shore interpolation requires a valid explicit end guard/precision");
    bool hasValid=false;
    for(auto value:buffer) {
        if(!std::isfinite(value))throw std::invalid_argument("nonfinite shore interpolation buffer");
        hasValid=hasValid || value>=0;
    }
    if(!hasValid)throw std::invalid_argument("shore interpolation requires a valid angle");
    auto next=buffer;
    if(precision==Precision::Float53)detail::resolveShore<double>(next,endGuard);
    else detail::resolveShore<float>(next,endGuard);
    for(auto value:next)if(!std::isfinite(value) || value<0)
        throw std::invalid_argument("shore interpolation distance overflow");
    buffer=next;
}

// Native02E09030 consumes entries0..510: eleven groups of42, then49.
// The caller must first resolve bodies/islands and interpolate invalid angles.
inline std::array<float,12> sampleShoreBuffer(const std::array<float,512>& buffer) {
    for(auto value:buffer)if(!std::isfinite(value) || value<0)
        throw std::invalid_argument("shore buffer requires finite resolved distances");
    std::array<float,12> out{};
    unsigned offset=0;
    for(unsigned sample=0;sample<out.size();++sample) {
        const unsigned count=sample==11?511-offset:42;
        float total=0;
        for(unsigned i=0;i<count;++i)total+=buffer[offset+i];
        out[sample]=total/float(count);offset+=count;
        if(!std::isfinite(out[sample]))throw std::invalid_argument("shore distance sum overflow");
    }
    return out;
}

// Native02E0A812..02E0A88F subtracts already encoded preceding outputs,
// descending within each forward sample. Adjacent subtraction differs in
// floating-point rounding even though it is algebraically equivalent.
inline std::array<float,12> encodeShoreDeltas(std::array<float,12> samples) {
    for(auto value:samples)if(!std::isfinite(value) || value<0)
        throw std::invalid_argument("shore samples require finite distances");
    for(unsigned sample=0;sample<samples.size();++sample)
        for(unsigned previous=sample;previous>0;--previous) {
            samples[sample]-=samples[previous-1];
            if(!std::isfinite(samples[sample]))throw std::invalid_argument("shore delta overflow");
        }
    return samples;
}

struct ShoreBody {
    ShoreBounds bounds;
    std::vector<std::array<int32_t,2>> contour; // excludes native bbox metadata
};
namespace detail {
template<class Real> inline std::array<int32_t,2> shoreMidpoint(
    std::array<int32_t,2> a,std::array<int32_t,2> b) {
    std::array<int32_t,2> result{};
    for(unsigned axis=0;axis<2;++axis) {
        const auto delta=int64_t(b[axis])-a[axis];
        if(delta<std::numeric_limits<int32_t>::min() || delta>std::numeric_limits<int32_t>::max())
            throw std::invalid_argument("shore midpoint coordinate delta overflow");
        double value;
        if constexpr(std::is_same_v<Real,float>) {
            const float half=float(static_cast<long double>(delta)*.5L);
            value=float(static_cast<long double>(a[axis])+half);
        } else value=double(a[axis])+double(delta)*.5;
        if(value<std::numeric_limits<int32_t>::min() || value>std::numeric_limits<int32_t>::max())
            throw std::invalid_argument("shore midpoint coordinate overflow");
        result[axis]=int32_t(value); // native __ftol2_sse truncates
    }
    return result;
}
template<class Real> inline std::vector<ShoreBody> sortShoreBodies(
    std::vector<std::array<int32_t,2>> points,float connection,float gap) {
    std::vector<ShoreBody> result;
    while(!points.empty()) {
        ShoreBody body;body.contour.push_back(points.front());points.erase(points.begin());
        bool done=false;
        std::vector<std::array<int32_t,2>> closurePositions{body.contour.back()};
        while(!done && !points.empty()) {
            const auto last=body.contour.back();
            const auto near=chooseShorePoint<Real>(points,body.contour,last,connection);
            auto removed=near.duplicateCurrentPoints;
            if(near.index) {
                body.contour.push_back(points[*near.index]);removed.push_back(*near.index);
            } else {
                const float distance=shorePointDistance<Real>(body.contour.front(),last);
                if(distance>gap) {
                    const auto across=chooseShorePoint<Real>(points,body.contour,last,gap);
                    removed.insert(removed.end(),across.duplicateCurrentPoints.begin(),across.duplicateCurrentPoints.end());
                    if(!across.index)done=true;
                    else {
                        const auto next=points[*across.index];
                        body.contour.push_back(shoreMidpoint<Real>(last,next));
                        body.contour.push_back(next);removed.push_back(*across.index);
                    }
                } else if(distance<=connection || gap<=distance)done=true;
                else body.contour.push_back(shoreMidpoint<Real>(body.contour.front(),last));
            }
            // Native queues physical iterator slots, then erases in that order.
            // Preserve compaction effects; refuse a slot expired by an earlier
            // removal instead of invoking the native invalid-parameter path.
            for(auto index:removed) {
                if(index>=points.size())throw std::invalid_argument("expired queued shore point removal");
                points.erase(points.begin()+index);
            }
            if(!done && removed.empty()) {
                for(const auto& previous:closurePositions)if(previous==body.contour.back())
                    throw std::invalid_argument("shore contour closure cannot make progress");
                closurePositions.push_back(body.contour.back());
            } else {closurePositions.clear();closurePositions.push_back(body.contour.back());}
        }
        body.bounds.minimum=body.bounds.maximum=body.contour.front();
        for(const auto& point:body.contour)for(unsigned axis=0;axis<2;++axis) {
            if(point[axis]<body.bounds.minimum[axis])body.bounds.minimum[axis]=point[axis];
            if(point[axis]>body.bounds.maximum[axis])body.bounds.maximum[axis]=point[axis];
        }
        result.push_back(std::move(body));
    }
    return result;
}
}
// Native combine02E07C50/sort02E083B0/extents02E07D70 over authored shore points.
// Previous contours append after new points, excluding their typed metadata.
// Input order and tiny bodies survive; no lexical sort or global deduplication.
// Defaults match static startup initialization04008E20 (2,4). Callers can
// supply runtime overrides explicitly; no running engine state is assumed.
inline std::vector<ShoreBody> buildShoreBodies(std::vector<std::array<int32_t,2>> points,
    float connection=2.f,float gap=4.f,Precision precision=Precision::Float24,
    std::span<const ShoreBody> previous={}) {
    if(!std::isfinite(connection) || connection<0 || !std::isfinite(gap) || gap<0 ||
       (precision!=Precision::Float24 && precision!=Precision::Float53))
        throw std::invalid_argument("invalid shore body connection/gap/precision");
    for(const auto& body:previous) {
        validateShoreBounds(std::span<const ShoreBounds>(&body.bounds,1));
        if(body.contour.empty())throw std::invalid_argument("previous shore body has no contour");
        for(const auto& p:body.contour)for(unsigned axis=0;axis<2;++axis)
            if(p[axis]<body.bounds.minimum[axis] || p[axis]>body.bounds.maximum[axis])
                throw std::invalid_argument("previous contour outside metadata bounds");
    }
    if(points.empty())return {previous.begin(),previous.end()}; // native returns before combine
    for(const auto& body:previous)points.insert(points.end(),body.contour.begin(),body.contour.end());
    if(precision==Precision::Float53)return detail::sortShoreBodies<double>(std::move(points),connection,gap);
    return detail::sortShoreBodies<float>(std::move(points),connection,gap);
}
struct ShoreLookup {
    std::optional<size_t> bodyIndex;
    std::array<float,12> values=noBodyShoreLookup();
};
// Prepared contours are an explicit input, not recovered from map water cells
// by this class. Cache body/island relationships once for repeated mesh queries.
class PreparedShoreBodies {
public:
    PreparedShoreBodies(std::vector<ShoreBody> bodies,float endGuard,Precision precision=Precision::Float24)
        : bodies_(std::move(bodies)),endGuard_(endGuard),precision_(precision) {
        if(!std::isfinite(endGuard_) || endGuard_<0 ||
           (precision_!=Precision::Float24 && precision_!=Precision::Float53))
            throw std::invalid_argument("prepared shores require a valid end guard/precision");
        for(const auto& b:bodies_) {
            if(b.contour.empty() || b.contour.size()>size_t(std::numeric_limits<int32_t>::max()-2))
                throw std::invalid_argument("prepared water body lacks a bounded contour");
            bounds_.push_back(b.bounds);
            for(const auto& point:b.contour)for(unsigned axis=0;axis<2;++axis)
                if(point[axis]<b.bounds.minimum[axis] || point[axis]>b.bounds.maximum[axis])
                    throw std::invalid_argument("shore contour point outside body metadata bounds");
        }
        validateShoreBounds(bounds_);
        for(size_t i=0;i<bodies_.size();++i)islands_.push_back(possibleIslandBodies(bounds_,i));
    }
    bool areaHasWater(int32_t x0,int32_t y0,int32_t x1,int32_t y1) const {
        return stbwater::areaHasWater(bounds_,x0,y0,x1,y1);
    }
    ShoreLookup lookup(std::array<float,2> query) const {
        ShoreLookup result;result.bodyIndex=firstContainingWaterBody(bounds_,query);
        if(!result.bodyIndex)return result;
        const auto owner=*result.bodyIndex;
        ShoreContourState state;
        accumulateShoreContour(state,bodies_[owner].contour,query,precision_);
        for(auto island:islands_[owner])accumulateShoreContour(state,bodies_[island].contour,query,precision_);
        resolveShoreBuffer(state.distances,endGuard_,precision_);
        result.values=encodeShoreDeltas(sampleShoreBuffer(state.distances));
        return result;
    }
private:
    std::vector<ShoreBody> bodies_;
    std::vector<ShoreBounds> bounds_;
    std::vector<std::vector<size_t>> islands_;
    float endGuard_;
    Precision precision_;
};

struct GroundVertex { uint16_t x=0,y=0; };
struct WaterVertex {
    int16_t x=0,y=0;
    float height=0;
    std::array<float,12> shore{};
};
struct Queries {
    std::function<bool(int,int,int)> hasWater; // native interpolated FAST query, radius1
    std::function<unsigned(int,int,int)> waterType; // native interpolated type, radius1
    std::function<float(int,int,int)> waterHeight; // native interpolated height, radius2*band
    std::function<std::array<float,12>(int16_t,int16_t,float)> shore;
};
struct BackgroundMesh {
    std::vector<WaterVertex> vertices;
    std::vector<uint16_t> indices;
    std::vector<int32_t> groundToWaterVertex;
    std::vector<int32_t> groundToWaterIndex;
    uint8_t waterType=0;
    // Native types3/4/5 use float XYZ; types1/2/6/8 use the raw56-byte vertex.
    unsigned vertexStride=0;
    std::vector<uint8_t> drawVertices; // little-endian native vertex buffer only
};

inline BackgroundMesh buildBackgroundMesh(std::span<const GroundVertex> ground,
    std::span<const uint16_t> triangles,int originX,int originY,unsigned band,
    const Queries& query) {
    if(band<1 || band>7 || !query.hasWater || !query.waterType ||
       !query.waterHeight || !query.shore)
        throw std::invalid_argument("background water requires band1..7 and all queries");
    if(ground.size()>65535 || triangles.size()>size_t(std::numeric_limits<int32_t>::max()) ||
       triangles.size()%3 || (!ground.empty() && (ground.size()<3 || triangles.size()<3)))
        throw std::invalid_argument("invalid background water triangle mesh");
    for(auto index:triangles)if(index>=ground.size())
        throw std::invalid_argument("background water index outside ground vertices");
    // Native subtraction is signed32 after zero-extending ground coordinates.
    // Refuse origins that would overflow rather than wrap an authored query.
    for(auto v:ground) {
        const auto x=int64_t(v.x)-originX,y=int64_t(v.y)-originY;
        if(x<std::numeric_limits<int>::min() || x>std::numeric_limits<int>::max() ||
           y<std::numeric_limits<int>::min() || y>std::numeric_limits<int>::max())
            throw std::invalid_argument("background water query coordinate overflow");
    }
    BackgroundMesh out;
    out.groundToWaterVertex.assign(ground.size(),-1);
    out.groundToWaterIndex.assign(triangles.size(),-1);
    std::array<size_t,9> types{};
    for(size_t at=0;at<triangles.size();at+=3) {
        std::array<int,3> x{},y{};
        for(unsigned j=0;j<3;++j) {
            const auto v=ground[triangles[at+j]];
            x[j]=int(v.x)-originX;y[j]=int(v.y)-originY;
        }
        if(!query.hasWater(x[0],y[0],1) && !query.hasWater(x[1],y[1],1) &&
           !query.hasWater(x[2],y[2],1))continue;
        // Count triangle corners, including repeated vertices; zero is ignored
        // only during winner selection. Ties keep the lower numbered type.
        for(unsigned j=0;j<3;++j) {
            const auto type=query.waterType(x[j],y[j],1);
            if(type>8)throw std::invalid_argument("background water type outside0..8");
            ++types[type];
        }
        for(unsigned j=0;j<3;++j) {
            const auto index=triangles[at+j];
            auto& mapped=out.groundToWaterVertex[index];
            if(mapped<0) {
                WaterVertex v;
                v.x=std::bit_cast<int16_t>(ground[index].x);
                v.y=std::bit_cast<int16_t>(ground[index].y);
                v.height=query.waterHeight(x[j],y[j],int(2*band));
                // Native retries the other corners cyclically on <=0. It does
                // not select the maximum height or revisit a shared vertex.
                for(unsigned retry=0;v.height<=0 && retry<2;++retry) {
                    const auto k=(j+1+retry)%3;
                    v.height=query.waterHeight(x[k],y[k],int(2*band));
                }
                if(!std::isfinite(v.height))
                    throw std::invalid_argument("nonfinite background water height");
                v.shore=query.shore(v.x,v.y,v.height);
                for(auto value:v.shore)if(!std::isfinite(value))
                    throw std::invalid_argument("nonfinite background water shoreline");
                mapped=int32_t(out.vertices.size());out.vertices.push_back(v);
            }
            out.groundToWaterIndex[at+j]=int32_t(out.indices.size());
            out.indices.push_back(uint16_t(mapped));
        }
    }
    if(!out.vertices.empty()) {
        size_t count=0;
        for(unsigned type=1;type<=8;++type)if(types[type]>count) {
            count=types[type];out.waterType=uint8_t(type);
        }
        switch(out.waterType) {
        case 1:case 2:case 6:case 8:out.vertexStride=56;break;
        case 3:case 4:case 5:out.vertexStride=12;break;
        default:throw std::invalid_argument("unsupported native background water type");
        }
        out.drawVertices.reserve(out.vertices.size()*out.vertexStride);
        auto half=[&](uint16_t value) {
            out.drawVertices.push_back(uint8_t(value));out.drawVertices.push_back(uint8_t(value>>8));
        };
        auto number=[&](float value) {
            const auto bits=std::bit_cast<uint32_t>(value);
            half(uint16_t(bits));half(uint16_t(bits>>16));
        };
        for(const auto& v:out.vertices) {
            if(out.vertexStride==12) {number(float(v.x));number(float(v.y));number(v.height);}
            else {
                half(std::bit_cast<uint16_t>(v.x));half(std::bit_cast<uint16_t>(v.y));number(v.height);
                for(auto value:v.shore)number(value);
            }
        }
    }
    return out;
}

} // namespace forge::stbwater
