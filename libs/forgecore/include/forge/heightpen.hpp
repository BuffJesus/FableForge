#pragma once

#include "forge/terrain.hpp"

#include <cstdint>
#include <vector>

// The vanilla editor's Height Toolbox pens (FableWin CPaintMapDialog, Land tab), ported from
// their code. One map block = one world unit = one Heightfield vertex. Every pen is a hard
// disc around the pointer rounded to a block (fistp, round half to even); none has a falloff
// or a time term -- each call is one application, which vanilla makes on the LMB press and,
// with Spray can on, on every held event and the release. Change Height, Paint Height and
// Smear keep heights in [0, 2048 - 1e-4] (GFLimit with DNZ_FOR_HEIGHTS); Noise clamps at 0 only. The water re-fit vanilla's setter does
// (EditSetGroundSizeZAtBlockUndoable 0x0297aa90) is not reproduced here.
namespace forge::heightpen {

constexpr float kMaxHeight = float(2048.0 - double(1.0e-4f));

// The Size slider (-2 .. 5): radius = 2^value blocks (GetBrushSize 0x02907170).
float sizeToRadius(float slider);
// The Speed slider (0 .. 1): opacity = 1 - cos(pi/2 * value) (GetOpacity).
float speedToOpacity(float speed);

// Change Height (EditChangeHeightPenUndoable 0x02975710): every block with distance < r + 0.5
// from the rounded centre gets `delta`, at most once per stroke -- `altered` (cellsX*cellsY,
// cleared at the stroke's press) records the blocks already changed.
size_t changeHeight(terrain::Heightfield& field, float x, float y, float radius, float delta, std::vector<uint8_t>& altered);

// Paint Height (EditPlacePenUndoable 0x029753d0): the disc moves `opacity` of the way to
// `target`, never past it.
size_t paintHeight(terrain::Heightfield& field, float x, float y, float radius, float target, float opacity);

// Smear (EditSmoothPenUndoable 0x0297b3c0 + GetFilteredHeight 0x02974c50): a block moves
// `smoothness` (0..1) toward its 8-neighbour average when |h - avg| / (max - min) > spikyness^2
// -- planar slopes stay, bumps and spikes go. Offsets [-r, r-1] with length < r, r = trunc(max(radius, 1)).
size_t smear(terrain::Heightfield& field, float x, float y, float radius, float smoothness, float spikyness);

// Noise (EditGenerateNoisePenUndoable 0x02975f00): nothing when the disc's mean height is
// <= 0.01; else an even number of random blocks in the square around the centre each get
// +amp / -amp alternately (amp = random(0.05 * magnifier) + 0.01), clamped at 0. `seed` is
// the engine LCG state (vanilla: the world seed), advanced as vanilla advances it.
size_t noise(terrain::Heightfield& field, float x, float y, float radius, float magnifier, uint32_t& seed);

// The engine's RNG (GFRandom 0x018cf370 / GFFloatRandom 0x018bac90).
uint32_t lcgStep(uint32_t& seed);
uint32_t random(uint32_t max, uint32_t& seed);
float floatRandom(float max, uint32_t& seed);

} // namespace forge::heightpen
