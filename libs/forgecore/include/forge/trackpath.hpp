#pragma once

#include <array>
#include <vector>

// The vanilla editor's Preview Track (FableWin CEditControlCentre::PreviewCameraTrack 0x0202f090 /
// UpdatePreviewTrack 0x0202f380): no spline -- a track is the polyline through its nodes, walked
// at constant speed by 3D arc length.
namespace forge::trackpath {

using Point = std::array<float, 3>;

// CThingTrackNode::GetTrackLength 0x027fdd80: the sum of the straight segments.
float length(const std::vector<Point>& nodes);

// GetIndexAlongTrackAtDistance 0x027fdb80 + the lerp in UpdatePreviewTrack: the first segment
// with d < acc + seg (strictly, so zero-length segments are skipped), A + (B - A) * (d - acc) / seg.
// Distances at or past the end give the last node (vanilla would read past the chain there).
Point pointAtDistance(const std::vector<Point>& nodes, float d);

} // namespace forge::trackpath
