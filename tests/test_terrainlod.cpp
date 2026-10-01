#include "terrainlod.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <map>
#include <utility>
#include <iostream>
struct Vertex { float px,py,pz,walk; };
int main() {
    for (const auto dimensions : {std::pair{65,65}, {34,19}, {4,36}, {2,2}, {8,8}}) {
        const auto [w,h]=dimensions;
        std::vector<Vertex> v;
        for(int y=0;y<h;++y) for(int x=0;x<w;++x)
            v.push_back({1000.f+x, 2.f*x+3.f*y, 2000.f-y, 2.f*x+3.f*y-10});
        std::vector<uint32_t> indices;
        for(int y=0;y<h-1;++y) for(int x=0;x<w-1;++x) {
            const uint32_t a=y*w+x,b=a+1,c=a+w,d=c+1;
            indices.insert(indices.end(),{a,b,d,a,d,c});
        }
        const auto result=albion::terrainlod::build(v,indices);
        assert(!result.patches.empty());
        for(const auto& patch:result.patches) {
            size_t previous=std::numeric_limits<size_t>::max();
            for(const auto& level:patch.levels) {
                assert(level.count<previous); previous=level.count;
                assert(level.error<0.001f);
                double area=0;
                std::map<std::pair<uint32_t,uint32_t>,int> edges;
                for(size_t i=level.first;i<level.first+level.count;i+=3) {
                    const auto a=result.indices[i],b=result.indices[i+1],c=result.indices[i+2];
                    const double signedArea=(v[b].px-v[a].px)*(v[a].pz-v[c].pz)-(v[a].pz-v[b].pz)*(v[c].px-v[a].px);
                    assert(signedArea>0); area+=signedArea/2;
                    for(auto edge:{std::pair{a,b},std::pair{b,c},std::pair{c,a}}) {
                        if(edge.first>edge.second) std::swap(edge.first,edge.second);
                        ++edges[edge];
                    }
                }
                assert(area==double(patch.hi[0]-patch.lo[0])*(patch.hi[2]-patch.lo[2]));
                for(const auto& [edge,count]:edges) {
                    assert(count==1 || count==2);
                    if(count==2) continue;
                    const auto& a=v[edge.first]; const auto& b=v[edge.second];
                    assert((a.px==b.px && (a.px==patch.lo[0] || a.px==patch.hi[0])) ||
                           (a.pz==b.pz && (a.pz==patch.lo[2] || a.pz==patch.hi[2])));
                    assert(std::abs(a.px-b.px)+std::abs(a.pz-b.pz)==1);
                }
            }
        }
        if(w==65) {
            assert(result.patches[0].levels.back().count<result.patches[0].levels[0].count/4);
            v[16*w+16].py+=100;
            const auto bump=albion::terrainlod::build(v,indices);
            assert(bump.patches[0].hi[1]>=v[16*w+16].py);
            for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
                v[y*w+x].py=12.f*std::sin(x*1.3f)*std::cos(y*0.7f);
                v[y*w+x].walk=5.f*std::cos(x*0.9f+y*1.2f);
            }
            const auto rough=albion::terrainlod::build(v,indices);
            for(const auto& patch:rough.patches) for(const auto& level:patch.levels)
                for(size_t i=level.first;i<level.first+level.count;i+=3) {
                    const auto& a=v[rough.indices[i]]; const auto& b=v[rough.indices[i+1]]; const auto& c=v[rough.indices[i+2]];
                    for(int ib=0;ib<=7;++ib) for(int ic=0;ic<=7-ib;++ic) {
                        const float wb=ib/7.f,wc=ic/7.f;
                        const float x=a.px-1000+wb*(b.px-a.px)+wc*(c.px-a.px);
                        const float y=2000-a.pz+wb*(a.pz-b.pz)+wc*(a.pz-c.pz);
                        const int ix=std::clamp(int(std::floor(x)),0,w-2),iy=std::clamp(int(std::floor(y)),0,h-2);
                        const float fx=x-ix,fy=y-iy;
                        const auto& aa=v[iy*w+ix]; const auto& dd=v[(iy+1)*w+ix+1];
                        const auto& side=v[(iy+(fy>fx))*w+ix+(fx>=fy)];
                        const float fine=aa.py+std::min(fx,fy)*(dd.py-aa.py)+std::abs(fx-fy)*(side.py-aa.py);
                        const float coarse=a.py+wb*(b.py-a.py)+wc*(c.py-a.py);
                        assert(std::abs(fine-coarse)<=level.error+0.001f);
                    }
                }
        }
        indices.pop_back();
        assert(albion::terrainlod::build(v,indices).patches.empty());
    }
    std::cout<<"terrain LOD topology PASS\n";
}
