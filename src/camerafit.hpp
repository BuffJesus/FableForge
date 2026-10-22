#pragma once
#include <algorithm>
#include <cmath>

namespace albion {

// Fit all eight bounds corners in a perspective orbit view. Horizontal FOV
// depends on the actual viewport aspect; the map's longest side alone cannot
// frame a narrow viewport or a tall terrain edit.
inline float cameraFitDistance(const float lo[3],const float hi[3],float yaw,float pitch,
    float fovY,float aspect) {
    const double sy=std::sin(double(yaw)),cy=std::cos(double(yaw));
    const double sp=std::sin(double(pitch)),cp=std::cos(double(pitch));
    const double tanY=std::tan(double(std::clamp(fovY,.01f,3.f))*.5);
    const double tanX=tanY*(aspect>0?double(aspect):1.);
    constexpr double usable=.85; // leave room around the map and viewport controls
    double distance=.5;
    for(int corner=0;corner<8;++corner) {
        double p[3];
        for(int axis=0;axis<3;++axis)p[axis]=double(hi[axis]-lo[axis])*.5*((corner&(1<<axis))?1:-1);
        const double x=p[0]*cy-p[2]*sy;
        const double y=-p[0]*sp*sy+p[1]*cp-p[2]*sp*cy;
        const double depth=-p[0]*cp*sy-p[1]*sp-p[2]*cp*cy;
        distance=std::max({distance,std::abs(x)/(tanX*usable)-depth,
            std::abs(y)/(tanY*usable)-depth,-depth/.99});
    }
    return float(distance);
}

} // namespace albion
