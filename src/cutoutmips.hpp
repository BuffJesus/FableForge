#pragma once
#include "texturepool.hpp"
#include <cmath>

namespace albion::cutoutmips {
using Chain = std::vector<terrainexport::Image>; // excludes the unchanged authored base

// Four bilinear samples per texel, with the world sampler's wrapping semantics.
// Scale/clamp/quantize before filtering, as the eventual RGBA8 texture does.
inline float coverage(const terrainexport::Image& image, float scale = 1) {
    if (!TexturePool<int>::valid(image)) return 0;
    size_t covered = 0;
    uint16_t scaled[256];
    for (int i=0;i<256;++i) scaled[i]=uint16_t(std::clamp(int(std::lround(i*scale)),0,255));
    auto alpha = [&](uint32_t x, uint32_t y) { return int(scaled[image.rgba[(size_t(y)*image.width+x)*4+3]]); };
    for (uint32_t y = 0; y < image.height; ++y) for (uint32_t x = 0; x < image.width; ++x) {
        const uint32_t nx = x+1 == image.width ? 0 : x+1, ny = y+1 == image.height ? 0 : y+1;
        const int a = alpha(x,y), b = alpha(nx,y), c = alpha(x,ny), d = alpha(nx,ny);
        covered += 9*a+3*b+3*c+d >= 2040;
        covered += 3*a+9*b+c+3*d >= 2040;
        covered += 3*a+b+9*c+3*d >= 2040;
        covered += a+3*b+3*c+9*d >= 2040;
    }
    return float(covered) / (float(image.width)*image.height*4);
}

inline terrainexport::Image reduce(const terrainexport::Image& source) {
    terrainexport::Image out;
    if (!TexturePool<int>::valid(source)) return out;
    out.width = std::max(1u, source.width/2); out.height = std::max(1u, source.height/2);
    out.rgba.resize(size_t(out.width)*out.height*4);
    const double sx = double(source.width)/out.width, sy = double(source.height)/out.height;
    for (uint32_t y=0; y<out.height; ++y) for (uint32_t x=0; x<out.width; ++x) {
        const double x0=x*sx, x1=(x+1)*sx, y0=y*sy, y1=(y+1)*sy;
        double rgb[3]={}, alpha=0, area=0;
        for (uint32_t yy=uint32_t(y0); yy<std::min(source.height,uint32_t(std::ceil(y1))); ++yy)
            for (uint32_t xx=uint32_t(x0); xx<std::min(source.width,uint32_t(std::ceil(x1))); ++xx) {
                const double weight=(std::min(x1,double(xx+1))-std::max(x0,double(xx))) *
                    (std::min(y1,double(yy+1))-std::max(y0,double(yy)));
                const auto* pixel=&source.rgba[(size_t(yy)*source.width+xx)*4];
                const double aw=weight*pixel[3];
                for(int c=0;c<3;++c) rgb[c]+=aw*pixel[c];
                alpha+=aw; area+=weight;
            }
        auto* pixel=&out.rgba[(size_t(y)*out.width+x)*4];
        for(int c=0;c<3;++c) pixel[c]=alpha>0 ? uint8_t(std::clamp(std::lround(rgb[c]/alpha),0l,255l)) : 0;
        pixel[3]=uint8_t(std::clamp(std::lround(alpha/area),0l,255l));
    }
    return out;
}

inline Chain build(const terrainexport::Image& base) {
    Chain chain;
    if (!TexturePool<int>::valid(base)) return chain;
    const float target=coverage(base);
    const terrainexport::Image* previous=&base;
    terrainexport::Image raw;
    while (std::max(previous->width,previous->height)>4) {
        auto next=reduce(*previous);
        float low=0, high=8, best=1, error=std::abs(coverage(next)-target);
        // Once coverage is exact, no later candidate can improve the strictly
        // non-negative error or replace `best`. Preserve identical output while
        // avoiding redundant full-image scans (especially opaque/small levels).
        for(int iteration=0;iteration<10 && error>0;++iteration) {
            const float scale=(low+high)*.5f, value=coverage(next,scale);
            if (std::abs(value-target)<error) { error=std::abs(value-target); best=scale; }
            if(value<target) low=scale; else high=scale;
        }
        // A tiny or uniform level cannot represent every silhouette. Retain the
        // last trustworthy level rather than losing leaves or filling their card.
        if(error>std::min({.03f,target*.1f,(1-target)*.1f})+1e-6f) break;
        auto corrected=next;
        for(size_t i=3;i<corrected.rgba.size();i+=4)
            corrected.rgba[i]=uint8_t(std::clamp(std::lround(corrected.rgba[i]*best),0l,255l));
        chain.push_back(std::move(corrected));
        raw=std::move(next); previous=&raw; // never compound coverage corrections
    }
    return chain;
}
} // namespace albion::cutoutmips
