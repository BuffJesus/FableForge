#include "worlddemand.hpp"
#include "sceneryadmission.hpp"
#include <array>
#include <cstdio>
#include <limits>
#include <vector>

int main() {
    using namespace albion::worldview;
    int failures = 0, checks = 0;
    auto check = [&](bool ok) { ++checks; if (!ok) { ++failures; std::fprintf(stderr, "world demand check %d failed\n", checks); } };
    const float eye[] = {0, 0, 0}, forward[] = {0, 0, 1}, backward[] = {0, 0, -1};
    const float frontLo[] = {-5, -5, 100}, frontHi[] = {5, 5, 110};
    check(demandViewWeight(eye, forward, .9f, 1.6f, frontLo, frontHi) == 1);
    check(demandViewWeight(eye, backward, .9f, 1.6f, frontLo, frontHi) == 0);
    const float enclosingLo[] = {-5, -5, -5}, enclosingHi[] = {5, 5, 5};
    check(demandViewWeight(eye, backward, .9f, 1.6f, enclosingLo, enclosingHi) == 1);
    const float overlapLo[] = {45, -10, 30}, overlapHi[] = {130, 10, 100};
    check(demandViewWeight(eye, forward, .9f, 1.6f, overlapLo, overlapHi) == 1);
    const float invalid[] = {std::numeric_limits<float>::quiet_NaN(), 0, 0};
    check(demandViewWeight(invalid, forward, .9f, 1.6f, frontLo, frontHi) == 1);
    check(demandViewWeight(eye, forward, .9f, 1.6f, frontHi, frontLo) == 1);
    check(demandViewWeight(eye, forward, .9f, 0, frontLo, frontHi) == 1);
    const float translatedEye[] = {1000, 20, -800};
    const float translatedLo[] = {995, 15, -700}, translatedHi[] = {1005, 25, -690};
    check(demandViewWeight(translatedEye, forward, .9f, 1.6f, translatedLo, translatedHi) == 1);
    // Cone-edge priority is continuous and decreases monotonically during a turn.
    float previous = 1; bool fractional = false;
    for (int i = 0; i <= 160; ++i) {
        const float a = float(i) * .01f, dir[] = {std::sin(a), 0, std::cos(a)};
        const float w = demandViewWeight(eye, dir, .9f, 1.6f, frontLo, frontHi);
        check(w <= previous && previous - w < .09f);
        fractional |= w > 0 && w < 1; previous = w;
    }
    check(fractional && previous == 0);
    check(demandPriority(20, 250, false, false, 0) == demandPriority(20, 250, false, false, 1));
    check(demandPriority(90, 250, false, false, 1) < demandPriority(50, 250, false, false, 0));
    check(demandPriority(90, 250, true, false, 0) < demandPriority(50, 250, false, false, 0));
    check(demandPriority(0, 250, false, true, 0) < demandPriority(0, 250, true, false, 1));
    check(demandPriority(100, 250, false, false, 1) < demandPriority(120, 250, false, false, 1));
    // Truncated cross-map demand preserves underfoot and prefers useful view
    // coverage, while retaining an unseen candidate instead of hard-culling it.
    std::vector<std::pair<float, int>> maps = {
        {demandPriority(0, 250, false, true, 0), 0},
        {demandPriority(50, 250, false, false, 0), 1},
        {demandPriority(90, 250, false, false, 1), 2},
        {demandPriority(100, 250, false, false, 1), 3}};
    std::sort(maps.begin(), maps.end());
    check(maps[0].second == 0 && maps[1].second == 2 && maps[2].second == 3 && maps[3].second == 1);
    check(requestedDrawDistance(250) == 250 && requestedDrawDistance(1000) == 1000);
    check(requestedDrawDistance(-100) == 100 && requestedDrawDistance(1500) == 1000);
    check(requestedDrawDistance(std::numeric_limits<float>::quiet_NaN()) == kDefaultDrawDistance);
    check(requestedDrawDistance(std::numeric_limits<float>::infinity()) == 1000);
    check(sceneryLoadRadius(1000) == 1182 && sceneryLoadRadius(250) == 319.5f);
    check(sceneryLoadRadius(100) > requestedDrawDistance(100));
    check(boxDistance(eye, enclosingLo, enclosingHi) == 0);
    check(boxDistance(eye, frontLo, frontHi) == 100);
    check(boxDistance(invalid, frontLo, frontHi) == 0);
    check(boxDistance(eye, frontHi, frontLo) == 0);
    const float cornerLo[] = {3, 4, 0}, cornerHi[] = {30, 40, 20};
    check(boxDistance(eye, cornerLo, cornerHi) == 5);
    check(boxDistance(translatedEye, translatedLo, translatedHi) == boxDistance(eye, frontLo, frontHi));
    // Altitude is part of actual distance; no unrelated near-map budget or
    // blanket height cutoff controls admission of these distant visible maps.
    const float downForward[] = {0, -0.6f, 0.8f};
    for (float altitude : {250.0f, 500.0f}) {
        const float highEye[] = {0, altitude, 0};
        int lowRange = 0, highRange = 0;
        for (float z : {500.0f, 700.0f, 900.0f}) {
            const float lo[] = {-96, -32, z - 32}, hi[] = {96, 64, z + 160};
            const float distance = boxDistance(highEye, lo, hi);
            check(demandViewWeight(highEye, downForward, .9f, 1.6f, lo, hi) > 0);
            check(distance >= altitude - hi[1] && distance >= lo[2]);
            lowRange += sceneryInRange(distance, requestedDrawDistance(250));
            highRange += sceneryInRange(distance, requestedDrawDistance(1000));
        }
        check(lowRange == 0 && highRange == 3);
    }
    // A box's near corner can be in range while its centre is well outside.
    const float distantLo[] = {100, -32, 100}, distantHi[] = {900, 64, 900};
    check(sceneryInRange(boxDistance(eye, distantLo, distantHi), 100));
    const float boundary = sceneryLoadRadius(1000);
    check(sceneryInRange(boundary, 1000) && !sceneryInRange(boundary + 1, 1000));
    check(sceneryInRange(boundary + 1, 1000, true));
    check(!sceneryInRange(boundary + kSceneryResidentMargin + 1, 1000, true));
    for (float radius : {250.0f, 500.0f, 1000.0f, 2000.0f}) {
        const float noResident = demandPriority(900, radius, false, false, 0);
        const float resident = demandPriority(900, radius, true, false, 0);
        check(std::abs(noResident - resident - kResidentPriorityMaximum) < 0.001f);
    }
    {
        const std::vector<SceneryAdmission> requests={{10,60},{90,20}};
        std::vector<SceneryOccupant> residents={{30,40,false},{100,40,false},{160,40,false}};
        auto choice=sceneryReplacement(requests,residents,20,120);
        check(choice.admission==0 && choice.victim==2);
        // Once a victim is fading, stable admission does not spread partial
        // disappearance across another map just because its rank shifts a bit.
        residents[1].priority=170;
        choice=sceneryReplacement(requests,residents,20,120,2);
        check(choice.admission==0 && choice.victim==2);
        residents[2].protectedHandoff=true;
        choice=sceneryReplacement(requests,residents,20,120,2);
        check(choice.admission==0 && choice.victim==1);
        residents[1].protectedHandoff=true;
        check(sceneryReplacement(requests,residents,20,120).victim==-1);
        residents[1].protectedHandoff=false; residents[1].bytes=0;
        check(sceneryReplacement(requests,residents,20,120).victim==-1);
        check(sceneryReplacement(requests,residents,60,120).victim==-1);
        check(sceneryReplacement(requests,residents,0,0).victim==-1);
        residents[2].protectedHandoff=false;
        check(sceneryReplacement({{10,121}},residents,0,120).victim==-1);
        // Newly freed space serves the high-priority map; the evicted map's
        // recorded allocation cannot evict its more important replacement.
        check(sceneryReplacement({{10,60}},residents,60,120).victim==-1);
        check(sceneryReplacement({{160,40}},{{10,60,false},{30,40,false}},20,120).victim==-1);
        check(sceneryReplacement({{100,60}},{{132,40,false}},20,120).victim==-1);
        check(sceneryReplacement({{100,60}},{{133,40,false}},20,120).victim==0);
        // A turn can reverse relevance. Cancel a stale fade rather than remove
        // scenery that is now more important than its would-be replacement.
        check(sceneryReplacement({{160,60}},{{10,40,false}},20,120,0).victim==-1);
        choice=sceneryReplacement({{10,121},{20,60}},{{100,40,false}},20,120);
        check(choice.admission==1 && choice.victim==0);
        // A protected/higher-priority working set can leave too little room
        // even after every eligible victim is removed. Do not start a futile
        // retire/reload cycle just because individual victims exist.
        const std::vector<SceneryOccupant> constrained={{0,70,false},{150,20,false},{200,10,false}};
        check(sceneryReplacement({{10,40}},constrained,5,128).victim==-1);
        check(sceneryReplacement({{10,35}},constrained,5,128).victim==2);
        check(sceneryReplacement({{10,35}},constrained,5,128,1).victim==1);
        // Per-map totals can double-count shared textures: the renderer's exact
        // releasable-set cap takes precedence over those individual estimates.
        check(sceneryReplacement({{10,35,20}},constrained,5,128).victim==-1);
        check(sceneryReplacement({{10,25,20}},constrained,5,128).victim==2);
        check(sceneryReplacement({{10,6,0}},constrained,5,128).victim==-1);
        // A ready higher admission waiting for CPU cleanup must not be starved
        // by reclaiming space on behalf of a blocked lower-priority request.
        check(sceneryReplacement({{10,5},{20,30}},constrained,5,128).victim==-1);
        check(sceneryReplacement({{10,1},{20,30}},constrained,5,128,2).victim==-1);
    }
    std::printf("World demand: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
