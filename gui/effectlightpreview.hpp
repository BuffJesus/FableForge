#pragma once
#include "particlepreview.hpp"
#include "renderer.hpp"
#include "imgui.h"
#include <algorithm>
#include <cmath>

namespace albion::gui {
// Diagnostic wire volumes, not an approximation of native scene illumination.
// All segments are clipped to the effect image; near-plane crossings are omitted.
inline size_t drawEffectLightVolumes(ImDrawList& draw,const Camera& camera,
                                    ImVec2 origin,ImVec2 size,
                                    const std::vector<particlepreview::DrawLight>& lights) {
    if (size.x<=0 || size.y<=0) return 0;
    float right[3],up[3],forward[3]; camera.right(right); camera.up(up); camera.dir(forward);
    const float sy=1/std::tan(camera.fovY*.5f),sx=sy*size.y/size.x;
    auto project=[&](const float p[3],ImVec2& screen) {
        const float delta[3]={p[0]-camera.posX,p[2]-camera.posY,-p[1]-camera.posZ};
        float x=0,y=0,z=0;
        for (int i=0;i<3;++i) { x+=delta[i]*right[i]; y+=delta[i]*up[i]; z+=delta[i]*forward[i]; }
        if (!std::isfinite(z) || z<=.01f) return false;
        screen={origin.x+size.x*(.5f+.5f*x*sx/z),origin.y+size.y*(.5f-.5f*y*sy/z)};
        return std::isfinite(screen.x) && std::isfinite(screen.y) && std::abs(screen.x)<1e6f && std::abs(screen.y)<1e6f;
    };
    draw.PushClipRect(origin,{origin.x+size.x,origin.y+size.y},true);
    auto intersects=[&](ImVec2 a,ImVec2 b) {
        float low=0,high=1;
        auto edge=[&](float p,float q) {
            if (p==0) return q>=0;
            const float t=q/p;
            if (p<0) low=std::max(low,t); else high=std::min(high,t);
            return low<=high;
        };
        const float dx=b.x-a.x,dy=b.y-a.y;
        return edge(-dx,a.x-origin.x) && edge(dx,origin.x+size.x-a.x) &&
            edge(-dy,a.y-origin.y) && edge(dy,origin.y+size.y-a.y);
    };
    size_t visible=0;
    for (const auto& light:lights) {
        if (!std::isfinite(light.radius) || light.radius<=0) continue;
        bool valid=true;
        for (float p:light.position) valid=valid && std::isfinite(p);
        for (float c:light.colour) valid=valid && std::isfinite(c);
        if (!valid) continue;
        const ImU32 colour=ImGui::ColorConvertFloat4ToU32({std::clamp(light.colour[0],0.f,1.f),
            std::clamp(light.colour[1],0.f,1.f),std::clamp(light.colour[2],0.f,1.f),.85f});
        bool drawn=false;
        for (int plane=0;plane<3;++plane) {
            ImVec2 previous; bool previousValid=false;
            for (int step=0;step<=64;++step) {
                const float angle=float(step)*(6.28318530718f/64.f);
                float point[3]={light.position[0],light.position[1],light.position[2]};
                point[(plane+1)%3]+=light.radius*std::cos(angle);
                point[(plane+2)%3]+=light.radius*std::sin(angle);
                ImVec2 current; const bool currentValid=project(point,current);
                if (currentValid && previousValid && intersects(previous,current)) { draw.AddLine(previous,current,colour,1.2f); drawn=true; }
                previous=current; previousValid=currentValid;
            }
        }
        ImVec2 centre;
        if (project(light.position,centre) && centre.x>=origin.x && centre.x<=origin.x+size.x && centre.y>=origin.y && centre.y<=origin.y+size.y) {
            draw.AddCircleFilled(centre,3.f,colour); drawn=true;
        }
        if (drawn) ++visible;
    }
    draw.PopClipRect();
    return visible;
}
}
