#pragma once
// The vanilla editor's fractal terrain (FableWin CEditFractal / NFractal), ported
// from the debug build's own code, not re-invented: a Musgrave hybrid
// multifractal over 2D gradient Perlin noise with seed 0.
//   noise tables   CNoise ctor / InitPerlinNoise 0x032c5c60 (RNG 0x018bac90,
//                  GFFloatRandom 0x018cf370, GFRandom 0x018bad60, gradients
//                  normalised with GFFastOneOverSqrt 0x01ace0a0 + GFInitMaths
//                  0x03193c30's table)
//   basis          PerlinNoise 0x032c5e10
//   octaves        NewFractalData 0x032c7040, CHybridMultifractal::GetFractalHeight 0x032c7300
//   world sample   GetFractalHeightAt 0x029a7290 / WorldCoordToFractalCoord 0x029a7070
//   application    ApplyFractalToMaps 0x0203cd70 -> CEditMap::EditGenerateFractal
//                  0x029a8e20: every vertex SET to height * Scale (not added)
// Float32 steps mirror the x87 code; last-bit differences under a different
// precision control are possible, the algorithm is exact.

#include <array>
#include <cstdint>

namespace forge::fractal {

// The Fractals dialog (CFractalDialog ctor 0x028de7b0 start values).
struct Params {
    double lacunarity = 2.0;         // 0.001 .. 100
    double dimension = 0.07;         // "Fractal dimension" H: octave i weighs lacunarity^(-i*H)
    double octaves = 12.0;           // 1 .. 100, fractional octaves blend in
    double mapX = 4000.0;            // "Map pos X / Y": offset into the noise field
    double mapY = 60000.0;
    double worldScaler = 0.2;        // one noise unit = 4096 * worldScaler world units
    bool useFalloff = false;         // fade to 0 away from the world centre (2048, 2048)
    double startFalloff = 2000.0;    // full height inside, cos fade between, 0 beyond end
    double endFalloff = 2500.0;
    double scale = 1000.0;           // "Scale": the applied height = fractal (0..1) * scale
};

class Generator {
public:
    explicit Generator(const Params& params = {});
    // The fractal (0..1) at a world position (map origin + map-local), with the falloff.
    float heightAt(double worldX, double worldY) const;
    // Perlin basis at a noise-space point (tests).
    float perlin(float x, float y) const;
    const std::array<int, 512>& permutation() const { return p_; }
    const std::array<std::array<float, 2>, 512>& gradients() const { return g_; }
    static const std::array<uint8_t, 128>& rsqrtTable();

private:
    double hybrid(float x, float y) const;
    Params params_;
    std::array<int, 512> p_{};
    std::array<std::array<float, 2>, 512> g_{};
    std::array<double, 128> exp_{};
};

} // namespace forge::fractal
