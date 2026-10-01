#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace forge::animation {

struct PositionKey {int16_t x=0,y=0,z=0;};
struct Track {
    uint32_t boneIndex=0;
    int32_t parentIndex=-1;
    std::string boneName;
    float fps=0,positionFactor=0,scalingFactor=0;
    uint32_t frameCount=0;
    std::vector<std::array<float,4>> rotations; // quaternion x,y,z,w
    std::vector<uint16_t> rotationPalette;
    std::vector<PositionKey> positions;
    std::vector<uint16_t> positionPalette;
};

struct Animation {
    bool cyclic=false;
    float duration=0;
    std::string rigName;
    std::vector<Track> tracks;
    std::vector<Track> helperTracks;
};

// Read a graphics.big type-6/7/9 3DAF payload, compressed or raw. The reader
// keeps key pools and frame palettes; it does not modify the archive.
Animation decode(std::span<const uint8_t> payload);

struct Transform {
    std::array<float,4> rotation={0,0,0,1};
    std::array<float,3> position={0,0,0};
    bool hasRotation=false,hasPosition=false;
};

// Evaluate a track in seconds using palette lookup and adjacent-frame
// interpolation. Missing channels remain absent for bind-pose fallback.
Transform evaluate(const Track& track,double seconds);

} // namespace forge::animation
