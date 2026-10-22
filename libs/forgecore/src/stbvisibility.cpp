#include "forge/stbvisibility.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace forge::stbvisibility {
namespace {
void validate(const Bounds& box) {
    for (float value : box)
        if (!std::isfinite(value)) throw std::invalid_argument("non-finite visibility bounds");
    for (int axis = 0; axis < 3; ++axis)
        if (box[axis] > box[axis+3]) throw std::invalid_argument("reversed visibility bounds");
}
float magnitude(const std::array<float, 3>& delta, Precision precision) {
    // Keep multiplication/addition order and the native float store before sqrt.
    float squared;
    if (precision == Precision::Float53) {
        const double x=delta[0], y=delta[1], z=delta[2];
        squared = static_cast<float>((x*x + y*y) + z*z);
    } else {
        const float x=delta[0]*delta[0], y=delta[1]*delta[1], z=delta[2]*delta[2];
        const float xy=x+y;
        squared = xy+z;
    }
    return std::sqrt(squared);
}
float farEndpoint(float point, float low, float high) {
    return std::abs(point-low) <= std::abs(point-high) ? high : low;
}
}

Distances observingDistances(const Bounds& query, std::span<const Bounds> observers,
                             Precision precision) {
    validate(query);
    Distances result;
    for (const auto& box : observers) {
        validate(box);
        std::array<float, 3> nearDelta{}, farDelta{};
        for (int axis=0; axis<3; ++axis) {
            const float center=(query[axis]+query[axis+3])*0.5f;
            const float onObserver=std::clamp(center,box[axis],box[axis+3]);
            const float onQuery=std::clamp(onObserver,query[axis],query[axis+3]);
            nearDelta[axis]=onObserver-onQuery;
            const float farObserver=farEndpoint(center,box[axis],box[axis+3]);
            farDelta[axis]=farObserver-farEndpoint(farObserver,query[axis],query[axis+3]);
        }
        result.nearest=std::min(result.nearest,magnitude(nearDelta,precision));
        result.furthest=std::max(result.furthest,magnitude(farDelta,precision));
    }
    return result;
}

bool foregroundNeeded(const Bounds& patch, const ForegroundCull* policy) {
    validate(patch);
    if (!policy) return true;
    if (!std::isfinite(policy->maximumDistance) || policy->maximumDistance < 0)
        throw std::invalid_argument("invalid foreground culling distance");
    return observingDistances(patch,policy->observers,policy->precision).nearest <=
        policy->maximumDistance;
}

Bounds cameraBounds(const terrain::Heightfield& heights,
                    const std::array<float, 3>& origin, float cameraHeight) {
    if (heights.width() <= 0 || heights.height() <= 0 ||
        !std::isfinite(cameraHeight) || cameraHeight < 0)
        throw std::invalid_argument("invalid camera bounds dimensions or height");
    float low=1.0e30f, high=-1.0e30f;
    for (int y=0; y<heights.height(); ++y) for (int x=0; x<heights.width(); ++x) {
        const float height=heights.at(x,y);
        if (!std::isfinite(height)) throw std::invalid_argument("non-finite landscape height");
        low=std::min(low,height); high=std::max(high,height);
    }
    Bounds result{origin[0],origin[1],low+origin[2],
        float(heights.width())+origin[0],float(heights.height())+origin[1],
        (high+cameraHeight)+origin[2]};
    validate(result);
    return result;
}

Bounds backgroundBounds(int mapX, int mapY, int width, int height,
                        const std::array<float, 3>& origin,
                        const SurfaceSampler& surfaceAt) {
    if (mapX<0 || mapY<0 || width<1 || height<1 || mapX>65535 || mapY>65535 ||
        width>65535-mapX || height>65535-mapY)
        throw std::invalid_argument("invalid background bounds rectangle");
    float low=0.0f, high=0.0f;
    if (surfaceAt) {
        low=1.0e30f;high=-1.0e30f;
        for (int y=mapY;y<=mapY+height;++y) for (int x=mapX;x<=mapX+width;++x) {
            const auto sample=surfaceAt(x,y);
            if (!std::isfinite(sample[0]) || !std::isfinite(sample[1]))
                throw std::invalid_argument("non-finite background surface height");
            low=std::min(low,sample[0]);
            high=std::max(high,std::max(sample[0],sample[1]));
        }
    }
    Bounds result{float(mapX)+origin[0],float(mapY)+origin[1],low+origin[2],
        float(mapX+width)+origin[0],float(mapY+height)+origin[1],high+origin[2]};
    validate(result);
    return result;
}

BackgroundBands backgroundBands(Distances distance, float firstLodZ, int lastBand) {
    if (!std::isfinite(firstLodZ) || firstLodZ<=0 || lastBand<1 || lastBand>7 ||
        !std::isfinite(distance.nearest) || !std::isfinite(distance.furthest) ||
        distance.nearest<0 || distance.furthest<0)
        throw std::invalid_argument("invalid background visibility bands");
    const auto z=[&](int band){return band<2 ? 0.0f : std::ldexp(firstLodZ,band-2);};
    if (!std::isfinite(z(lastBand))) throw std::invalid_argument("background LOD distance overflow");
    BackgroundBands result;result.last=uint8_t(lastBand);
    while (result.first<lastBand-1 && z(result.first+1)<distance.nearest) ++result.first;
    for (int band=result.first;band<=lastBand;++band)
        result.needed[band]=band==result.first || band==lastBand || z(band)<distance.furthest;
    return result;
}

Graph buildGraph(const bwd::File& world, bool enabled) {
    Graph result;
    result.enabled=enabled;
    if (!enabled) return result;
    const size_t count=world.maps().size()+1;
    result.visibleFrom.resize(count); result.observedBy.resize(count);
    const auto check=[&](int32_t slot) {
        if (slot<=0 || static_cast<size_t>(slot)>=count)
            throw std::invalid_argument("visibility region references an invalid map slot");
    };
    for (const auto& region : world.regions()) {
        std::set<int32_t> targets;
        for (auto slot : region.contains) {check(slot); targets.insert(slot);}
        for (auto slot : region.sees) {check(slot); targets.insert(slot);}
        for (auto slot : region.contains) {
            auto& visible=result.visibleFrom[slot];
            visible.clear();
            if (world.maps()[slot-1].loadedOnProximity)
                visible.assign(targets.begin(),targets.end());
        }
    }
    for (size_t observer=1; observer<count; ++observer)
        for (auto target : result.visibleFrom[observer])
            result.observedBy[target].push_back(static_cast<int32_t>(observer));
    return result;
}
} // namespace forge::stbvisibility
