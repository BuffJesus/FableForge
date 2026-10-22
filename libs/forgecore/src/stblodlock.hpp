#pragma once
// Native background UV-lock tree (02DF1AB0 / 02DF18B0). Internal producer
// primitive; a lock tree alone does not establish per-band mesh generation.
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

namespace forge::stbbake::detail {
enum class LodLockPrecision { Float24, Float53 };
inline float lodErrorScale(float distance,float detail,LodLockPrecision precision=LodLockPrecision::Float24) {
    if(!std::isfinite(distance) || !std::isfinite(detail) || distance<=0 || detail<=0)
        throw std::invalid_argument("LOD distance/detail must be finite and positive");
    const float scale=precision==LodLockPrecision::Float53?
        float(800.0/((double(distance)*2.0)*double(detail))):800.f/((distance*2.f)*detail);
    if(!std::isfinite(scale) || scale<=0)
        throw std::invalid_argument("LOD error scale exceeds supported float domain");
    return scale;
}
struct LodLock {
    bool split=false;
    float position=0;
    int minimumRange=0;
    std::unique_ptr<LodLock> first,second;
};

inline int lodMinimumRange(int width,int height,int band,int extent,bool vertical) {
    if(width<1 || height<1 || width>65535 || height>65535 || band<1 || band>7 || extent<1)
        throw std::invalid_argument("invalid LOD minimum range input");
    // Native BuildTree special-cases only the horizontal lock at band one.
    if(!vertical && band==1)return 1;
    return std::clamp(std::max(width,height)*16/extent,1,vertical?height:width);
}

template<class Real> inline std::unique_ptr<LodLock> buildLodLockPart(float scale,int extent,
    std::span<const float> values,int start,int length,float offset,float available,int minimum) {
    const int half=length/2;
    auto peak=[&](int begin,int count) {
        float result=0;
        for(int i=begin;i<begin+count;++i)result=std::max(result,values[size_t(i)]);
        return result;
    };
    const float a=float(((Real(length)*Real(scale))/Real(2))*Real(peak(start,half)));
    const float b=float(((Real(length)*Real(scale))/Real(2))*Real(peak(start+half,length-half)));
    auto result=std::make_unique<LodLock>();
    if(Real(extent)*Real(available)>Real(2)*Real(std::max(a,b))) {
        result->minimumRange=length;return result;
    }
    if(length==minimum)return {};
    auto cost=[&](int begin) {
        float sum=0;
        for(int i=begin;i<begin+half;i+=minimum)sum=float(Real(minimum)*Real(peak(i,minimum))+Real(sum));
        return float(Real(sum)*Real(scale));
    };
    const float firstCost=cost(start),secondCost=cost(start+half);
    const Real total=Real(firstCost)+Real(secondCost);
    if(!std::isfinite(total) || double(total)>std::numeric_limits<int>::max())
        throw std::invalid_argument("LOD lock cost exceeds supported integer domain");
    if(Real(float(int(total)))>Real(extent)*Real(available))return {};
    result->split=true;
    const Real halfCapacity=Real(extent)*Real(available)/Real(2);
    if(halfCapacity<=a && b<halfCapacity) {
        const float other=float(Real(b)/Real(extent)),range=float(Real(available)-Real(other));
        if(Real(firstCost)<Real(extent)*Real(range)) {
            result->first=buildLodLockPart<Real>(scale,extent,values,start,half,offset,range,minimum);
            if(result->first) {
                result->position=float(Real(offset)+Real(range));
                result->minimumRange=result->first->minimumRange;
                if(!result->first->split)result->first.reset();
                return result;
            }
        }
    } else if(a<halfCapacity) {
        const float range=float(Real(a)/Real(extent)),other=float(Real(available)-Real(range));
        if(Real(secondCost)<Real(extent)*Real(other)) {
            result->second=buildLodLockPart<Real>(scale,extent,values,start+half,half,float(Real(offset)+Real(range)),other,minimum);
            if(result->second) {
                result->position=float(Real(offset)+Real(range));
                result->minimumRange=result->second->minimumRange;
                if(!result->second->split)result->second.reset();
                return result;
            }
        }
    }
    const float firstUnits=float(Real(firstCost)/Real(extent)),secondUnits=float(Real(secondCost)/Real(extent));
    const float firstRange=float(((Real(firstUnits)+Real(available))-Real(secondUnits))/Real(2));
    const float secondRange=float(Real(available)-Real(firstRange));
    result->position=float(Real(offset)+Real(firstRange));
    result->first=buildLodLockPart<Real>(scale,extent,values,start,half,offset,firstRange,minimum);
    if(!result->first)return {};
    result->second=buildLodLockPart<Real>(scale,extent,values,start+half,half,float(Real(offset)+Real(firstRange)),secondRange,minimum);
    if(!result->second)return {};
    result->minimumRange=std::min(result->first->minimumRange,result->second->minimumRange);
    if(!result->first->split)result->first.reset();
    if(!result->second->split)result->second.reset();
    return result;
}

inline std::unique_ptr<LodLock> buildLodLock(float scale,int extent,
    std::span<const float> values,int minimum,LodLockPrecision precision=LodLockPrecision::Float24) {
    const auto power=[](size_t n){return n && !(n&(n-1));};
    if(!std::isfinite(scale) || scale<=0 || extent<=0 || values.size()>65536 ||
       !power(values.size()) || minimum<=0 || !power(size_t(minimum)) || size_t(minimum)>values.size())
        throw std::invalid_argument("invalid LOD lock input");
    for(float value:values)if(!std::isfinite(value) || value<0)
        throw std::invalid_argument("LOD lock samples must be finite and nonnegative");
    if(precision==LodLockPrecision::Float53)
        return buildLodLockPart<double>(scale,extent,values,0,int(values.size()),0,1,minimum);
    return buildLodLockPart<float>(scale,extent,values,0,int(values.size()),0,1,minimum);
}

template<class Real> inline std::vector<float> lodLockUVTyped(const LodLock* root,int length) {
    if(length<1 || length>65536 || (length&(length-1)))
        throw std::invalid_argument("invalid LOD UV table length");
    std::vector<float> values(size_t(length)+1);
    const auto fill=[&](auto&& self,const LodLock* node,int begin,int count,float offset,float range)->void {
        if(!node || !node->split) {
            for(int i=0;i<count;++i)values[size_t(begin+i)]=float((Real(i)*Real(range))/Real(count)+Real(offset));
        } else {
            if(count<2)throw std::invalid_argument("LOD UV split exceeds table depth");
            self(self,node->first.get(),begin,count/2,offset,float(Real(node->position)-Real(offset)));
            self(self,node->second.get(),begin+count/2,count/2,node->position,float((Real(range)+Real(offset))-Real(node->position)));
        }
    };
    fill(fill,root,0,length,0,1);values.back()=1;return values;
}
inline std::vector<float> lodLockUV(const LodLock* root,int length,LodLockPrecision precision=LodLockPrecision::Float24) {
    return precision==LodLockPrecision::Float53?lodLockUVTyped<double>(root,length):lodLockUVTyped<float>(root,length);
}
} // namespace forge::stbbake::detail
