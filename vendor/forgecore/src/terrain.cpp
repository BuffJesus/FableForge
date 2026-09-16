#include "forge/terrain.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace forge::terrain {

ThemeBlend projectMaterialWeights(const std::vector<float>& weights,
                                  const std::vector<uint8_t>& themeIndices) {
    if (weights.empty() || weights.size() != themeIndices.size())
        throw std::invalid_argument("terrain: material weights/mapping size mismatch");
    std::array<double, 256> merged{};
    double total = 0.0;
    for (size_t i = 0; i < weights.size(); ++i) {
        if (!std::isfinite(weights[i]) || weights[i] < -1e-6f)
            throw std::invalid_argument("terrain: material weight must be finite and nonnegative");
        const double weight = std::max(0.0f, weights[i]);
        merged[themeIndices[i]] += weight;
        total += weight;
    }
    if (!(total > 0.0))
        throw std::invalid_argument("terrain: material weights sum to zero");

    std::vector<std::pair<uint8_t, double>> ranked;
    for (size_t i = 0; i < merged.size(); ++i)
        if (merged[i] > 0.0) ranked.push_back({static_cast<uint8_t>(i), merged[i]});
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });
    if (ranked.size() > 3) ranked.resize(3);

    ThemeBlend out;
    const double kept = std::accumulate(ranked.begin(), ranked.end(), 0.0,
        [](double sum, const auto& item) { return sum + item.second; });
    struct Fraction { size_t slot; double value; };
    std::vector<Fraction> fractions;
    int assigned = 0;
    for (size_t i = 0; i < ranked.size(); ++i) {
        out.indices[i] = ranked[i].first;
        const double exact = ranked[i].second / kept * 255.0;
        const int base = static_cast<int>(std::floor(exact));
        out.strengths[i] = static_cast<uint8_t>(base);
        assigned += base;
        fractions.push_back({i, exact - base});
    }
    std::stable_sort(fractions.begin(), fractions.end(), [](const auto& a, const auto& b) {
        if (a.value != b.value) return a.value > b.value;
        return a.slot < b.slot;
    });
    for (int i = assigned; i < 255; ++i)
        ++out.strengths[fractions[size_t(i - assigned) % fractions.size()].slot];
    return out;
}

float sampleMaterialWeight(const std::vector<float>& raster,
                           int rasterWidth, int rasterHeight,
                           float worldX, float worldY,
                           float extentX, float extentY) {
    if (rasterWidth < 2 || rasterHeight < 2 ||
        raster.size() != static_cast<size_t>(rasterWidth) * rasterHeight ||
        !(extentX > 0.0f) || !(extentY > 0.0f) ||
        !std::isfinite(worldX) || !std::isfinite(worldY))
        throw std::invalid_argument("terrain: invalid material raster sampling request");
    if (worldX < 0.0f || worldX > extentX || worldY < 0.0f || worldY > extentY)
        throw std::out_of_range("terrain: material sample outside world extent");
    const double sx = double(worldX) / extentX * (rasterWidth - 1);
    const double sy = double(worldY) / extentY * (rasterHeight - 1);
    const int x0 = static_cast<int>(std::floor(sx));
    const int y0 = static_cast<int>(std::floor(sy));
    const int x1 = std::min(x0 + 1, rasterWidth - 1);
    const int y1 = std::min(y0 + 1, rasterHeight - 1);
    const float fx = static_cast<float>(sx - x0), fy = static_cast<float>(sy - y0);
    const auto at = [&](int x, int y) { return raster[size_t(y) * rasterWidth + x]; };
    for (float v : {at(x0,y0), at(x1,y0), at(x0,y1), at(x1,y1)})
        if (!std::isfinite(v) || v < -1e-6f)
            throw std::invalid_argument("terrain: invalid material raster value");
    const auto clean = [&](int x, int y) { return std::max(0.0f, at(x, y)); };
    const float top = clean(x0,y0) + (clean(x1,y0) - clean(x0,y0)) * fx;
    const float bottom = clean(x0,y1) + (clean(x1,y1) - clean(x0,y1)) * fx;
    return std::max(0.0f, top + (bottom - top) * fy);
}

Heightfield::Heightfield(int width, int height, float initialHeight)
    : width_(width), height_(height) {
    if (width < 0 || height < 0 || !std::isfinite(initialHeight)) {
        throw std::invalid_argument("terrain: invalid heightfield dimensions or height");
    }
    heights_.assign(static_cast<size_t>(width + 1) *
                        static_cast<size_t>(height + 1),
                    initialHeight);
}

size_t Heightfield::index(int x, int y) const {
    if (x < 0 || y < 0 || x >= cellsX() || y >= cellsY()) {
        throw std::out_of_range("terrain: heightfield coordinate out of range");
    }
    return static_cast<size_t>(y) * static_cast<size_t>(cellsX()) +
           static_cast<size_t>(x);
}

float Heightfield::at(int x, int y) const { return heights_[index(x, y)]; }
float& Heightfield::at(int x, int y) { return heights_[index(x, y)]; }

Heightfield Heightfield::fromLev(const lev::File& file) {
    Heightfield result(file.width(), file.height());
    for (int y = 0; y < result.cellsY(); ++y) {
        for (int x = 0; x < result.cellsX(); ++x) {
            result.at(x, y) = file.heightAt(x, y);
        }
    }
    return result;
}

Heightfield fromNormalizedRaster(const std::vector<float>& samples,
                                 int sourceWidth, int sourceHeight,
                                 int mapWidth, int mapHeight,
                                 float baseHeight, float heightScale) {
    if (sourceWidth < 2 || sourceHeight < 2 || mapWidth <= 0 || mapHeight <= 0 ||
        samples.size() != static_cast<size_t>(sourceWidth) * sourceHeight ||
        !std::isfinite(baseHeight) || !std::isfinite(heightScale)) {
        throw std::invalid_argument("terrain: invalid normalized raster");
    }
    for (float sample : samples) {
        if (!std::isfinite(sample) || sample < 0.0f || sample > 1.0f)
            throw std::invalid_argument("terrain: normalized sample outside 0..1");
    }
    Heightfield out(mapWidth, mapHeight, baseHeight);
    for (int y = 0; y <= mapHeight; ++y) {
        const float sy = float(y) * float(sourceHeight - 1) / float(mapHeight);
        const int y0 = static_cast<int>(std::floor(sy));
        const int y1 = std::min(y0 + 1, sourceHeight - 1);
        const float fy = sy - float(y0);
        for (int x = 0; x <= mapWidth; ++x) {
            const float sx = float(x) * float(sourceWidth - 1) / float(mapWidth);
            const int x0 = static_cast<int>(std::floor(sx));
            const int x1 = std::min(x0 + 1, sourceWidth - 1);
            const float fx = sx - float(x0);
            const float a = samples[size_t(y0) * sourceWidth + x0];
            const float b = samples[size_t(y0) * sourceWidth + x1];
            const float c = samples[size_t(y1) * sourceWidth + x0];
            const float d = samples[size_t(y1) * sourceWidth + x1];
            const float top = a + (b - a) * fx;
            const float bottom = c + (d - c) * fx;
            out.at(x, y) = baseHeight + (top + (bottom - top) * fy) * heightScale;
        }
    }
    return out;
}

Heightfield fromWorldHeightRaster(const std::vector<float>& samples,
                                  int sourceWidth, int sourceHeight,
                                  float sourceSpacing,
                                  float sourceHeightBias) {
    if (sourceWidth < 2 || sourceHeight < 2 ||
        samples.size() != static_cast<size_t>(sourceWidth) * sourceHeight ||
        !(sourceSpacing > 0.0f) || !std::isfinite(sourceSpacing) ||
        !std::isfinite(sourceHeightBias)) {
        throw std::invalid_argument("terrain: invalid world-height raster");
    }
    for (float sample : samples) {
        if (!std::isfinite(sample))
            throw std::invalid_argument("terrain: non-finite world-height sample");
    }
    const double extentX = double(sourceWidth - 1) * sourceSpacing;
    const double extentY = double(sourceHeight - 1) * sourceSpacing;
    const int mapWidth = static_cast<int>(std::llround(extentX));
    const int mapHeight = static_cast<int>(std::llround(extentY));
    constexpr double kExtentTolerance = 1e-6;
    if (mapWidth <= 0 || mapHeight <= 0 ||
        std::abs(extentX - mapWidth) > kExtentTolerance ||
        std::abs(extentY - mapHeight) > kExtentTolerance) {
        throw std::invalid_argument(
            "terrain: source extent is not an integer TLC world-unit extent");
    }

    Heightfield out(mapWidth, mapHeight);
    for (int y = 0; y <= mapHeight; ++y) {
        const double sy = double(y) / sourceSpacing;
        const int y0 = std::min(static_cast<int>(std::floor(sy)), sourceHeight - 1);
        const int y1 = std::min(y0 + 1, sourceHeight - 1);
        const float fy = static_cast<float>(sy - y0);
        for (int x = 0; x <= mapWidth; ++x) {
            const double sx = double(x) / sourceSpacing;
            const int x0 = std::min(static_cast<int>(std::floor(sx)), sourceWidth - 1);
            const int x1 = std::min(x0 + 1, sourceWidth - 1);
            const float fx = static_cast<float>(sx - x0);
            const float a = samples[size_t(y0) * sourceWidth + x0];
            const float b = samples[size_t(y0) * sourceWidth + x1];
            const float c = samples[size_t(y1) * sourceWidth + x0];
            const float d = samples[size_t(y1) * sourceWidth + x1];
            const float top = a + (b - a) * fx;
            const float bottom = c + (d - c) * fx;
            out.at(x, y) = sourceHeightBias + top + (bottom - top) * fy;
        }
    }
    return out;
}

std::vector<HeightfieldTile> splitHeightfield(
    const Heightfield& source,
    const std::vector<int>& tileWidths,
    const std::vector<int>& tileHeights) {
    if (tileWidths.empty() || tileHeights.empty())
        throw std::invalid_argument("terrain: tile layout is empty");
    int totalWidth = 0, totalHeight = 0;
    for (int width : tileWidths) {
        if (width <= 0 || width > source.width() - totalWidth)
            throw std::invalid_argument("terrain: invalid tile width layout");
        totalWidth += width;
    }
    for (int height : tileHeights) {
        if (height <= 0 || height > source.height() - totalHeight)
            throw std::invalid_argument("terrain: invalid tile height layout");
        totalHeight += height;
    }
    if (totalWidth != source.width() || totalHeight != source.height())
        throw std::invalid_argument("terrain: tile layout does not exactly cover source");

    std::vector<HeightfieldTile> out;
    out.reserve(tileWidths.size() * tileHeights.size());
    int originY = 0;
    for (int height : tileHeights) {
        int originX = 0;
        for (int width : tileWidths) {
            Heightfield tile(width, height);
            for (int y = 0; y <= height; ++y)
                for (int x = 0; x <= width; ++x)
                    tile.at(x, y) = source.at(originX + x, originY + y);
            out.push_back({originX, originY, std::move(tile)});
            originX += width;
        }
        originY += height;
    }
    return out;
}

void Heightfield::writeTo(lev::File& file) const {
    if (file.width() != width_ || file.height() != height_) {
        throw std::invalid_argument("terrain: LEV and heightfield dimensions differ");
    }
    for (int y = 0; y < cellsY(); ++y) {
        for (int x = 0; x < cellsX(); ++x) file.setHeightAt(x, y, at(x, y));
    }
}

static float falloff(float distance, float radius) {
    const float linear = std::clamp(1.0f - distance / radius, 0.0f, 1.0f);
    return linear * linear * (3.0f - 2.0f * linear);
}

size_t applyBrush(Heightfield& field, const Brush& brush) {
    if (!(brush.radius > 0.0f) || !std::isfinite(brush.radius) ||
        !std::isfinite(brush.centerX) || !std::isfinite(brush.centerY) ||
        !std::isfinite(brush.amount) || !std::isfinite(brush.targetHeight)) {
        throw std::invalid_argument("terrain: brush values must be finite and radius positive");
    }

    Heightfield source = field;
    const int minX = std::max(0, static_cast<int>(std::floor(brush.centerX - brush.radius)));
    const int maxX = std::min(field.width(), static_cast<int>(std::ceil(brush.centerX + brush.radius)));
    const int minY = std::max(0, static_cast<int>(std::floor(brush.centerY - brush.radius)));
    const int maxY = std::min(field.height(), static_cast<int>(std::ceil(brush.centerY + brush.radius)));
    size_t changed = 0;

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const float dx = static_cast<float>(x) - brush.centerX;
            const float dy = static_cast<float>(y) - brush.centerY;
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (distance > brush.radius) continue;
            const float weight = falloff(distance, brush.radius);
            const float oldHeight = source.at(x, y);
            float newHeight = oldHeight;
            if (brush.mode == BrushMode::RaiseLower) {
                newHeight += brush.amount * weight;
            } else if (brush.mode == BrushMode::Flatten) {
                const float blend = std::clamp(brush.amount * weight, 0.0f, 1.0f);
                newHeight += (brush.targetHeight - oldHeight) * blend;
            } else {
                float total = 0.0f;
                int samples = 0;
                for (int sy = std::max(0, y - 1); sy <= std::min(field.height(), y + 1); ++sy) {
                    for (int sx = std::max(0, x - 1); sx <= std::min(field.width(), x + 1); ++sx) {
                        total += source.at(sx, sy);
                        ++samples;
                    }
                }
                const float average = total / static_cast<float>(samples);
                const float blend = std::clamp(brush.amount * weight, 0.0f, 1.0f);
                newHeight += (average - oldHeight) * blend;
            }
            if (newHeight != oldHeight) {
                field.at(x, y) = newHeight;
                ++changed;
            }
        }
    }
    return changed;
}

ThemeBlend paintThemeBlend(ThemeBlend current, uint8_t themeIndex, float opacity) {
    if (!std::isfinite(opacity)) throw std::invalid_argument("terrain: paint opacity must be finite");
    opacity = std::clamp(opacity, 0.0f, 1.0f);
    if (opacity == 0.0f) return current;

    unsigned total = 0;
    for (uint8_t weight : current.strengths) total += weight;
    if (total != 255) throw std::invalid_argument("terrain: theme strengths must sum to 255");

    int target = -1;
    for (int i = 0; i < 3; ++i) {
        if (current.indices[i] == themeIndex) {
            if (target < 0) target = i;
            else {
                const unsigned merged = unsigned(current.strengths[target]) +
                                        unsigned(current.strengths[i]);
                current.strengths[target] = static_cast<uint8_t>(std::min(255u, merged));
                current.strengths[i] = 0;
            }
        }
    }
    if (target < 0) {
        target = int(std::min_element(current.strengths.begin(),
                                      current.strengths.end()) - current.strengths.begin());
        const uint8_t displaced = current.strengths[target];
        int receiver = target == 0 ? 1 : 0;
        for (int i = 0; i < 3; ++i) {
            if (i != target && current.strengths[i] > current.strengths[receiver]) receiver = i;
        }
        current.strengths[receiver] = static_cast<uint8_t>(
            unsigned(current.strengths[receiver]) + unsigned(displaced));
        current.strengths[target] = 0;
        current.indices[target] = themeIndex;
    }

    const unsigned oldTarget = current.strengths[target];
    const unsigned newTarget = std::min(255u, static_cast<unsigned>(std::lround(
        float(oldTarget) + float(255u - oldTarget) * opacity)));
    const unsigned remainder = 255u - newTarget;
    int otherA = (target + 1) % 3;
    int otherB = (target + 2) % 3;
    const unsigned otherTotal = unsigned(current.strengths[otherA]) +
                                unsigned(current.strengths[otherB]);
    unsigned newA = 0;
    if (otherTotal != 0) {
        newA = static_cast<unsigned>(std::lround(
            double(remainder) * double(current.strengths[otherA]) / double(otherTotal)));
        newA = std::min(newA, remainder);
    }
    current.strengths[target] = static_cast<uint8_t>(newTarget);
    current.strengths[otherA] = static_cast<uint8_t>(newA);
    current.strengths[otherB] = static_cast<uint8_t>(remainder - newA);
    return current;
}

size_t applyThemeBrush(lev::File& file, const ThemeBrush& brush) {
    if (!(brush.radius > 0.0f) || !std::isfinite(brush.radius) ||
        !std::isfinite(brush.centerX) || !std::isfinite(brush.centerY) ||
        !std::isfinite(brush.opacity)) {
        throw std::invalid_argument("terrain: invalid theme brush");
    }
    const int minX = std::max(0, int(std::floor(brush.centerX - brush.radius)));
    const int maxX = std::min(file.width(), int(std::ceil(brush.centerX + brush.radius)));
    const int minY = std::max(0, int(std::floor(brush.centerY - brush.radius)));
    const int maxY = std::min(file.height(), int(std::ceil(brush.centerY + brush.radius)));
    size_t changed = 0;
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const float dx = float(x) - brush.centerX;
            const float dy = float(y) - brush.centerY;
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (distance > brush.radius) continue;
            const float opacity = std::clamp(brush.opacity, 0.0f, 1.0f) *
                                  falloff(distance, brush.radius);
            if (opacity <= 0.0f) continue;
            ThemeBlend before;
            for (int i = 0; i < 3; ++i) {
                before.indices[i] = file.themeIndexAt(x, y, i);
                before.strengths[i] = file.themeStrengthAt(x, y, i);
            }
            const ThemeBlend after = paintThemeBlend(before, brush.themeIndex, opacity);
            if (after.indices != before.indices || after.strengths != before.strengths) {
                file.setThemeBlendAt(x, y, after.indices, after.strengths);
                ++changed;
            }
        }
    }
    return changed;
}

template <typename Getter, typename Setter>
size_t applyBooleanBrush(lev::File& file, const BooleanBrush& brush,
                         Getter getter, Setter setter) {
    if (!(brush.radius > 0.0f) || !std::isfinite(brush.radius) ||
        !std::isfinite(brush.centerX) || !std::isfinite(brush.centerY) ||
        !std::isfinite(brush.threshold) || brush.threshold < 0.0f || brush.threshold > 1.0f) {
        throw std::invalid_argument("terrain: invalid boolean brush");
    }
    size_t changed = 0;
    const int minX = std::max(0, int(std::floor(brush.centerX - brush.radius)));
    const int maxX = std::min(file.width(), int(std::ceil(brush.centerX + brush.radius)));
    const int minY = std::max(0, int(std::floor(brush.centerY - brush.radius)));
    const int maxY = std::min(file.height(), int(std::ceil(brush.centerY + brush.radius)));
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const float dx = float(x) - brush.centerX;
            const float dy = float(y) - brush.centerY;
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (distance > brush.radius || falloff(distance, brush.radius) < brush.threshold) continue;
            if (getter(file, x, y) != brush.value) {
                setter(file, x, y, brush.value);
                ++changed;
            }
        }
    }
    return changed;
}

size_t applyWalkabilityBrush(lev::File& file, const BooleanBrush& brush) {
    return applyBooleanBrush(file, brush,
        [](const lev::File& f, int x, int y) { return f.walkableAt(x, y); },
        [](lev::File& f, int x, int y, bool v) { f.setWalkableAt(x, y, v); });
}

size_t applyPreferredPathBrush(lev::File& file, const BooleanBrush& brush) {
    return applyBooleanBrush(file, brush,
        [](const lev::File& f, int x, int y) { return f.preferredPathAt(x, y); },
        [](lev::File& f, int x, int y, bool v) { f.setPreferredPathAt(x, y, v); });
}

LayerTopology buildLayerTopology(const std::array<bool, 512>& triangleMask) {
    static constexpr int kOffsets[2][2][3][2] = {
        {{{0, 0}, {1, 0}, {1, 1}}, {{0, 0}, {0, 1}, {1, 1}}},
        {{{0, 0}, {1, 0}, {0, 1}}, {{1, 0}, {0, 1}, {1, 1}}},
    };
    LayerTopology result;
    std::array<int, 17 * 17> vertexIds;
    vertexIds.fill(-1);
    size_t enabled = 0;

    for (int x = 0; x < 16; ++x) {
        for (int y = 0; y < 16; ++y) {
            for (int triangle = 0; triangle < 2; ++triangle) {
                if (!triangleMask[(x * 16 + y) * 2 + triangle]) continue;
                ++enabled;
                std::array<uint16_t, 3> ids{};
                for (int corner = 0; corner < 3; ++corner) {
                    const int vx = x + kOffsets[(x ^ y) & 1][triangle][corner][0];
                    const int vy = y + kOffsets[(x ^ y) & 1][triangle][corner][1];
                    int& id = vertexIds[vy * 17 + vx];
                    if (id < 0) {
                        id = static_cast<int>(result.vertices.size());
                        result.vertices.push_back(
                            {static_cast<uint8_t>(vx), static_cast<uint8_t>(vy)});
                    }
                    ids[corner] = static_cast<uint16_t>(id);
                }

                auto& strip = result.indices;
                if (strip.empty()) {
                    if (triangle == 0) strip.push_back(ids[0]);
                    strip.insert(strip.end(), ids.begin(), ids.end());
                } else if (triangle != (strip.size() & 1) && strip.size() >= 2 &&
                           strip[strip.size() - 2] == ids[0] && strip.back() == ids[1]) {
                    strip.push_back(ids[2]);
                } else if (triangle == (strip.size() & 1)) {
                    // Keep the disconnected triangle on the required strip parity.
                    // The shorter four-index bridge previously used when the strip
                    // held one triangle could turn the two bridge endpoints into a
                    // real triangle (seen in BWSMarket_07).  The repeated previous
                    // and next vertices below make every bridge triangle degenerate.
                    strip.insert(strip.end(),
                                 {strip.back(), ids[0], ids[0], ids[0], ids[1], ids[2]});
                } else {
                    strip.insert(strip.end(),
                                 {strip.back(), ids[0], ids[0], ids[1], ids[2]});
                }
            }
        }
    }
    result.fullPatch = enabled == triangleMask.size();
    return result;
}

static bool sameMaterial(const TerrainMaterialTuple& a,
                         const TerrainMaterialTuple& b) {
    return a.textures == b.textures &&
           a.textureMaxSize == b.textureMaxSize &&
           a.bumpMaxSize == b.bumpMaxSize &&
           a.selfIllumination == b.selfIllumination;
}

std::vector<TerrainLayerContribution> buildThemeContributions(
    const ThemeBlend& blend, const std::vector<TerrainThemeMaterial>& themes) {
    std::array<unsigned, 3> weights{};
    std::array<const TerrainThemeMaterial*, 3> selected{};
    unsigned total = 0;
    for (size_t slot = 0; slot < 3; ++slot) {
        const size_t themeIndex = blend.indices[slot];
        if (themeIndex < themes.size() && themes[themeIndex].available) {
            selected[slot] = &themes[themeIndex];
            weights[slot] = blend.strengths[slot];
            total += weights[slot];
        }
    }
    if (total == 0) return {};
    for (unsigned& weight : weights) weight = weight * 255u / total;

    std::array<unsigned, 3> baseWeights = weights;
    std::array<unsigned, 3> cliffWeights = weights;
    for (size_t first = 0; first < 3; ++first) {
        if (selected[first] == nullptr) continue;
        for (size_t later = first + 1; later < 3; ++later) {
            if (selected[later] == nullptr) continue;
            if (baseWeights[first] && baseWeights[later] &&
                selected[first]->base.textures[0] != 0 &&
                sameMaterial(selected[first]->base, selected[later]->base)) {
                baseWeights[first] += baseWeights[later];
                baseWeights[later] = 0;
            }
            if (cliffWeights[first] && cliffWeights[later] &&
                selected[first]->cliff.textures[0] != 0 &&
                sameMaterial(selected[first]->cliff, selected[later]->cliff)) {
                cliffWeights[first] += cliffWeights[later];
                cliffWeights[later] = 0;
            }
        }
    }

    std::vector<TerrainLayerContribution> result;
    for (size_t slot = 0; slot < 3; ++slot) {
        if (selected[slot] == nullptr) continue;
        if (selected[slot]->base.textures[0] != 0 && baseWeights[slot] > 0x10) {
            result.push_back({selected[slot]->base, 0,
                              static_cast<uint8_t>(baseWeights[slot])});
        }
        if (selected[slot]->cliff.textures[0] != 0 && cliffWeights[slot] > 0x10) {
            for (uint8_t direction = 1; direction <= 4; ++direction) {
                result.push_back({selected[slot]->cliff, direction,
                                  static_cast<uint8_t>(cliffWeights[slot])});
            }
        }
    }
    return result;
}

static void applyCorrection(Heightfield& target, int x, int y,
                            float correction, float weight, size_t& changed) {
    const float oldHeight = target.at(x, y);
    const float newHeight = oldHeight + correction * weight;
    if (newHeight != oldHeight) {
        target.at(x, y) = newHeight;
        ++changed;
    }
}

StitchResult stitchSharedBoundary(Heightfield& target, TilePlacement tp,
                                  const Heightfield& neighbor, TilePlacement np,
                                  int blendWidth) {
    if (blendWidth < 0) throw std::invalid_argument("terrain: blend width cannot be negative");
    StitchResult result;

    const bool neighborWest = np.worldX + neighbor.width() == tp.worldX;
    const bool neighborEast = tp.worldX + target.width() == np.worldX;
    const bool neighborNorth = np.worldY + neighbor.height() == tp.worldY;
    const bool neighborSouth = tp.worldY + target.height() == np.worldY;

    if (neighborWest || neighborEast) {
        const int start = std::max(tp.worldY, np.worldY);
        const int end = std::min(tp.worldY + target.height(),
                                 np.worldY + neighbor.height());
        if (start > end) return result;
        result.edge = neighborWest ? SharedEdge::West : SharedEdge::East;
        result.overlapStart = start;
        result.overlapEnd = end;
        const int tx = neighborWest ? 0 : target.width();
        const int nx = neighborWest ? neighbor.width() : 0;
        const int direction = neighborWest ? 1 : -1;
        const int depth = std::min(blendWidth, target.width());
        for (int wy = start; wy <= end; ++wy) {
            const int ty = wy - tp.worldY;
            const int ny = wy - np.worldY;
            const float correction = neighbor.at(nx, ny) - target.at(tx, ty);
            applyCorrection(target, tx, ty, correction, 1.0f, result.verticesChanged);
            for (int d = 1; d <= depth; ++d) {
                const float weight = static_cast<float>(depth + 1 - d) /
                                     static_cast<float>(depth + 1);
                applyCorrection(target, tx + direction * d, ty, correction,
                                weight, result.verticesChanged);
            }
            ++result.boundaryVertices;
        }
        return result;
    }

    if (neighborNorth || neighborSouth) {
        const int start = std::max(tp.worldX, np.worldX);
        const int end = std::min(tp.worldX + target.width(),
                                 np.worldX + neighbor.width());
        if (start > end) return result;
        result.edge = neighborNorth ? SharedEdge::North : SharedEdge::South;
        result.overlapStart = start;
        result.overlapEnd = end;
        const int ty = neighborNorth ? 0 : target.height();
        const int ny = neighborNorth ? neighbor.height() : 0;
        const int direction = neighborNorth ? 1 : -1;
        const int depth = std::min(blendWidth, target.height());
        for (int wx = start; wx <= end; ++wx) {
            const int tx = wx - tp.worldX;
            const int nx = wx - np.worldX;
            const float correction = neighbor.at(nx, ny) - target.at(tx, ty);
            applyCorrection(target, tx, ty, correction, 1.0f, result.verticesChanged);
            for (int d = 1; d <= depth; ++d) {
                const float weight = static_cast<float>(depth + 1 - d) /
                                     static_cast<float>(depth + 1);
                applyCorrection(target, tx, ty + direction * d, correction,
                                weight, result.verticesChanged);
            }
            ++result.boundaryVertices;
        }
    }
    return result;
}

const char* edgeName(SharedEdge edge) {
    switch (edge) {
    case SharedEdge::West: return "west";
    case SharedEdge::East: return "east";
    case SharedEdge::North: return "north";
    case SharedEdge::South: return "south";
    default: return "none";
    }
}

} // namespace forge::terrain
