#include "forge/stbheightbake.hpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <set>
#include <stdexcept>

#include "forge/lzo.hpp"
#include "forge/stbbake.hpp"
#include "forge/wld.hpp"

namespace forge::stbbake {

HeightfieldBakeResult bakeHeightfield(const std::vector<uint8_t>& chunkBytes,
                                      const lev::File& lev, int worldX, int worldY,
                                      const HeightfieldBakeOptions& options) {
    HeightfieldBakeResult result;
    auto notef = [&](const char* fmt, ...) {
        char buf[512];
        va_list ap; va_start(ap, fmt);
        std::vsnprintf(buf, sizeof buf, fmt, ap);
        va_end(ap);
        result.notes.emplace_back(buf);
    };
    if (options.themes.size() != 256 && options.rebuildTopology)
        throw std::invalid_argument("bakeHeightfield: rebuildTopology needs 256 theme slots");
    const std::vector<terrain::TerrainThemeMaterial> emptyThemes(256);
    struct NormalNeighbor {
        const lev::File* levPtr = nullptr;
        lev::File owned;
        int worldX = 0, worldY = 0;
        const lev::File& lev() const { return levPtr ? *levPtr : owned; }
    };
    std::vector<NormalNeighbor> normalNeighbors;
    for (const auto& n : options.neighbors) {
        NormalNeighbor nn; nn.levPtr = n.lev; nn.worldX = n.worldX; nn.worldY = n.worldY;
        normalNeighbors.push_back(std::move(nn));
    }
    if (options.rebuildTopology && std::none_of(
            options.themes.begin(), options.themes.end(),
            [](const auto& theme) { return theme.available; }))
        throw std::invalid_argument("bakeHeightfield: rebuildTopology requires theme materials");
    const bool supportedTopology =
        (lev.width() == 32 || lev.width() == 64) &&
        (lev.height() == 32 || lev.height() == 64);
    if (options.requireCanonicalSize && !supportedTopology)
        throw std::runtime_error("bakeHeightfield requires 32/64 cells per axis (requireCanonicalSize)");
    if (lev.width() % 16 != 0 || lev.height() % 16 != 0)
        throw std::runtime_error("bakeHeightfield: map extents must be multiples of 16 cells");
    if (worldX < 0 || worldY < 0 || worldX + lev.width() > 65535 ||
        worldY + lev.height() > 65535)
        throw std::runtime_error("bakeHeightfield world coordinates exceed u16 patch format");

    const bool anyWorldOption = !options.worldPath.empty() ||
                                !options.levelsRoot.empty() ||
                                !options.levelName.empty();
    if (anyWorldOption && (options.worldPath.empty() ||
                           options.levelsRoot.empty() ||
                           options.levelName.empty()))
        throw std::invalid_argument(
            "bakeHeightfield: worldPath, levelsRoot and levelName are required together");
    if (anyWorldOption) {
        namespace fs = std::filesystem;
        const auto world = forge::wld::File::parse(options.worldPath);
        const auto* targetMap = world.findMap(options.levelName);
        if (targetMap == nullptr)
            throw std::invalid_argument(
                "bakeHeightfield: levelName is not present in the WLD");
        if (targetMap->mapX != worldX || targetMap->mapY != worldY)
            throw std::invalid_argument(
                "bakeHeightfield: worldX/worldY disagree with the WLD placement");

        auto same = [](const std::string& a, const std::string& b) {
            return a.size() == b.size() && std::equal(
                a.begin(), a.end(), b.begin(),
                [](unsigned char x, unsigned char y) {
                    return std::tolower(x) == std::tolower(y);
                });
        };
        std::set<std::string> candidates;
        size_t owningRegions = 0;
        for (const auto& region : world.regions()) {
            const bool ownsTarget = std::any_of(
                region.containsMaps.begin(), region.containsMaps.end(),
                [&](const auto& name) { return same(name, targetMap->levelName); });
            if (!ownsTarget) continue;
            ++owningRegions;
            candidates.insert(region.containsMaps.begin(), region.containsMaps.end());
            candidates.insert(region.seesMaps.begin(), region.seesMaps.end());
        }
        if (owningRegions == 0)
            throw std::invalid_argument(
                "bakeHeightfield: the level does not belong to a WLD region");
        size_t missingNeighbors = 0;
        for (const auto& name : candidates) {
            if (same(name, targetMap->levelName)) continue;
            const auto* map = world.findMap(name);
            if (map == nullptr) continue;
            const fs::path path = fs::path(options.levelsRoot) / fs::path(name);
            if (!fs::is_regular_file(path)) {
                ++missingNeighbors;
                continue;
            }
            auto candidate = forge::lev::File::open(path);
            const int right = map->mapX + candidate.width() - 1;
            const int bottom = map->mapY + candidate.height() - 1;
            if (right < worldX - 1 || map->mapX > worldX + lev.width() ||
                bottom < worldY - 1 || map->mapY > worldY + lev.height())
                continue;
            const bool duplicate = std::any_of(
                normalNeighbors.begin(), normalNeighbors.end(),
                [&](const auto& n) {
                    return n.worldX == map->mapX && n.worldY == map->mapY;
                });
            if (!duplicate) {
                notef("  WLD neighbor (%d,%d) %s [%dx%d]",
                            map->mapX, map->mapY, name.c_str(),
                            candidate.width(), candidate.height());
                NormalNeighbor nn; nn.owned = std::move(candidate); nn.worldX = map->mapX; nn.worldY = map->mapY;
                normalNeighbors.push_back(std::move(nn));
            }
        }
        notef("auto-discovered %zu landscape neighbor(s) from WLD "
                    "(%zu unavailable candidate LEVs)",
                    normalNeighbors.size(), missingNeighbors);
    }

    const auto chunk = forge::stbbake::parseChunk(chunkBytes);
    forge::stbbake::EmitOptions opt;
    opt.highCompressionEdits = true;
    opt.preservePhysicalLayout = true;

    struct ForegroundCandidate {
        size_t frameIndex = 0;
        std::vector<uint8_t> body;
        forge::stbbake::ForegroundFrame frame;
        std::vector<uint8_t> compressed;
    };
    std::vector<ForegroundCandidate> foreground;
    uint16_t foregroundMinX = 0xffff, foregroundMinY = 0xffff;
    uint16_t foregroundMaxX = 0, foregroundMaxY = 0;
    for (size_t fi = 0; fi < chunk.frameIndices.size(); ++fi) {
        std::vector<uint8_t> body;
        try { body = forge::stbbake::decodeFrame(chunk, fi); }
        catch (const std::exception&) { continue; }
        forge::stbbake::ForegroundFrame parsed;
        try { parsed = forge::stbbake::parseForegroundFrame(body); }
        catch (const std::exception&) { continue; }
        if (forge::stbbake::serializeForegroundFrame(parsed) != body) continue;
        for (const auto& layer : parsed.layers) {
            for (const auto& vertex : layer.vertices) {
                foregroundMinX = std::min(foregroundMinX, vertex.x);
                foregroundMinY = std::min(foregroundMinY, vertex.y);
                foregroundMaxX = std::max(foregroundMaxX, vertex.x);
                foregroundMaxY = std::max(foregroundMaxY, vertex.y);
            }
        }
        foreground.push_back(ForegroundCandidate{
            fi, std::move(body), std::move(parsed), {}});
    }
    // one foreground frame per 16x16 cell that HAS one: the directory marks
    // cells without a foreground mesh with a zero frame pointer (fillers)
    size_t expectedForegroundFrames = 0;
    {
        const auto dir = forge::stbbake::parseQuadDir(chunk);
        for (const auto& e : dir) if (e.frameOffset != 0) ++expectedForegroundFrames;
        if (dir.empty())
            expectedForegroundFrames = static_cast<size_t>(lev.width() / 16) *
                                       static_cast<size_t>(lev.height() / 16);
    }
    if (foreground.size() != expectedForegroundFrames)
        throw std::runtime_error("expected " + std::to_string(expectedForegroundFrames) +
                                 " CLandscapeLayerMesh foreground frames, found " +
                                 std::to_string(foreground.size()));
    // cells without a foreground mesh (fillers) shrink the extent; it may never exceed the LEV
    if (foreground.empty() ||
        foregroundMaxX - foregroundMinX > lev.width() ||
        foregroundMaxY - foregroundMinY > lev.height())
        throw std::runtime_error("foreground layer mesh extent does not match LEV heightfield");

    auto put16At = [](std::vector<uint8_t>& bytes, size_t offset, uint16_t value) {
        bytes[offset] = uint8_t(value);
        bytes[offset + 1] = uint8_t(value >> 8);
    };
    auto put32At = [](std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
        bytes[offset] = uint8_t(value);
        bytes[offset + 1] = uint8_t(value >> 8);
        bytes[offset + 2] = uint8_t(value >> 16);
        bytes[offset + 3] = uint8_t(value >> 24);
    };
    // PeekLandscapeHeight addresses cells, not the serialized extra
    // vertex row/column: x==width or y==height belongs to an adjacent
    // map. If no loaded neighbor owns it, the engine clamps to the
    // target's last real cell (width-1/height-1).
    std::set<std::pair<int, int>> resolvedNeighborSamples;
    std::set<std::pair<int, int>> clampedWorldSamples;
    auto sampleHeight = [&](int sampleX, int sampleY) {
        auto engineHeight = [](float rawHeight) {
            return forge::stbbake::quantizeEngineHeight(rawHeight);
        };
        if (sampleX >= 0 && sampleX < lev.width() &&
            sampleY >= 0 && sampleY < lev.height())
            return engineHeight(lev.heightAt(sampleX, sampleY));
        const int wx = worldX + sampleX;
        const int wy = worldY + sampleY;
        for (const auto& neighbor : normalNeighbors) {
            const int nx = wx - neighbor.worldX;
            const int ny = wy - neighbor.worldY;
            if (nx >= 0 && nx < neighbor.lev().width() &&
                ny >= 0 && ny < neighbor.lev().height()) {
                resolvedNeighborSamples.emplace(wx, wy);
                return engineHeight(neighbor.lev().heightAt(nx, ny));
            }
        }
        clampedWorldSamples.emplace(wx, wy);
        return engineHeight(lev.heightAt(
            std::min(std::max(sampleX, 0), lev.width() - 1),
            std::min(std::max(sampleY, 0), lev.height() - 1)));
    };
    const forge::stbbake::HeightSampler dirSampler = sampleHeight;
    auto authoredNormal = [&](int lx, int ly) -> uint32_t {
        // CMap::PeekMapNormal (forge::stbbake::packMapNormal).
        return forge::stbbake::packMapNormal(dirSampler, lx, ly);
    };
    using DirectionNormal = forge::stbbake::DirectionNormal;
    auto authoredDirectionNormal = [&](int lx, int ly) {
        // CEngineLandscapeMeshBuilder::BuildMapDirMask.
        return forge::stbbake::buildMapDirMask(dirSampler, worldX, worldY, lx, ly);
    };
    auto authoredCliffLookup = [&](int lx, int ly) {
        const auto sum = authoredDirectionNormal(lx, ly);
        return std::pair<uint8_t, uint8_t>{
            forge::stbbake::packDirMaskByte(sum.x),
            forge::stbbake::packDirMaskByte(sum.y)};
    };
    // Normal quantization: clear the low `clearBits` of each signed
    // component. More cleared bits => more repeated bytes => a smaller LZO
    // stream. Default 0x3f keeps the authored gradient; the retry loop
    // below coarsens this only if the retargeted frames overflow.
    auto compactAuthoredNormal = [&](int lx, int ly, uint32_t clearBits) -> uint32_t {
        const uint32_t packed = authoredNormal(lx, ly);
        const uint32_t cx = clearBits & 0x3ffu;          // keep >=1 high bit
        const uint32_t cz = clearBits & 0xffu;           // z is only 10-bit
        const uint32_t qx = (packed & 0x7ffu) & ~cx;
        const uint32_t qy = ((packed >> 11) & 0x7ffu) & ~cx;
        const uint32_t qz = ((packed >> 22) & 0x3ffu) & ~cz;
        return qx | (qy << 11) | (qz << 22);
    };

    // The foreground frames are repacked contiguously into the donor's
    // foreground allocation (first foreground frame -> next non-pad
    // segment). Compute that budget up front so we can fit the retargeted
    // frames into it rather than failing hard.
    const size_t fgRegionStart =
        chunk.segments[chunk.frameIndices[foreground.front().frameIndex]].start;
    const size_t fgLastSeg = chunk.frameIndices[foreground.back().frameIndex];
    size_t fgRegionEnd = chunk.raw.size();
    for (size_t si = fgLastSeg + 1; si < chunk.segments.size(); ++si) {
        if (chunk.segments[si].kind != forge::stbbake::SegKind::Pad) {
            fgRegionEnd = chunk.segments[si].start;
            break;
        }
    }
    const size_t fgBudget = fgRegionEnd - fgRegionStart;

    if (options.rebuildTopology) {
        static constexpr int triangleOffsets[2][2][3][2] = {
            {{{0, 0}, {1, 0}, {1, 1}}, {{0, 0}, {0, 1}, {1, 1}}},
            {{{0, 0}, {1, 0}, {0, 1}}, {{1, 0}, {0, 1}, {1, 1}}},
        };
        auto sameMaterial = [](const auto& a, const auto& b) {
            return a.textures == b.textures &&
                   a.textureMaxSize == b.textureMaxSize &&
                   a.bumpMaxSize == b.bumpMaxSize &&
                   a.selfIllumination == b.selfIllumination;
        };
        auto directionActive = [&](uint8_t direction, int lx, int ly) {
            const auto normal = authoredDirectionNormal(lx, ly);
            const float topness = std::clamp(
                (std::asin(std::clamp(normal.z, -1.0f, 1.0f)) /
                     (3.14159265358979323846f * 0.5f) -
                 0.5f) * 4.0f,
                0.0f, 1.0f);
            if (direction == 0) return topness > 0.0f;
            if (topness == 1.0f) return false;
            const float length = std::sqrt(normal.x * normal.x + normal.y * normal.y);
            if (length == 0.0f) return false;
            static constexpr float dirs[5][2] = {
                {0, 0}, {0, -1}, {0, 1}, {-1, 0}, {1, 0}};
            const float dot = std::clamp(
                normal.x / length * dirs[direction][0] +
                normal.y / length * dirs[direction][1], -1.0f, 1.0f);
            const float sideness = std::clamp(
                1.0f - 2.0f * (std::acos(dot) /
                    (3.14159265358979323846f * 0.5f) - 0.25f),
                0.0f, 1.0f);
            return (1.0f - topness) * sideness > 0.0f;
        };

        struct GeneratedPass {
            forge::terrain::TerrainMaterialTuple material;
            uint8_t direction = 0;
            std::array<uint8_t, 17 * 17> blend{};
            std::array<bool, 512> triangles{};
        };
        for (auto& candidate : foreground) {
            uint16_t patchX = 0xffff, patchY = 0xffff;
            for (const auto& layer : candidate.frame.layers) {
                for (const auto& vertex : layer.vertices) {
                    patchX = std::min(patchX, vertex.x);
                    patchY = std::min(patchY, vertex.y);
                }
            }
            patchX = uint16_t(patchX / 16 * 16);
            patchY = uint16_t(patchY / 16 * 16);
            const int mapPatchX = int(patchX) - int(foregroundMinX);
            const int mapPatchY = int(patchY) - int(foregroundMinY);
            std::vector<GeneratedPass> passes;
            for (int y = 0; y <= 16; ++y) {
                for (int x = 0; x <= 16; ++x) {
                    forge::terrain::ThemeBlend blend;
                    for (int slot = 0; slot < 3; ++slot) {
                        blend.indices[slot] = lev.themeIndexAt(
                            mapPatchX + x, mapPatchY + y, slot);
                        blend.strengths[slot] = lev.themeStrengthAt(
                            mapPatchX + x, mapPatchY + y, slot);
                    }
                    for (const auto& contribution :
                         forge::terrain::buildThemeContributions(
                             blend, options.themes)) {
                        auto pass = std::find_if(
                            passes.begin(), passes.end(), [&](const auto& value) {
                                return value.direction == contribution.mappingDirection &&
                                       sameMaterial(value.material,
                                                    contribution.material);
                            });
                        if (pass == passes.end()) {
                            passes.push_back(GeneratedPass{});
                            pass = std::prev(passes.end());
                            pass->material = contribution.material;
                            pass->direction = contribution.mappingDirection;
                        }
                        pass->blend[y * 17 + x] = contribution.blend;
                    }
                }
            }
            for (auto& pass : passes) {
                for (int x = 0; x < 16; ++x) {
                    for (int y = 0; y < 16; ++y) {
                        for (int triangle = 0; triangle < 2; ++triangle) {
                            bool contributes = false, facesDirection = false;
                            for (int corner = 0; corner < 3; ++corner) {
                                const int vx = x + triangleOffsets[(x ^ y) & 1]
                                    [triangle][corner][0];
                                const int vy = y + triangleOffsets[(x ^ y) & 1]
                                    [triangle][corner][1];
                                contributes |= pass.blend[vy * 17 + vx] != 0;
                                facesDirection |= directionActive(
                                    pass.direction, mapPatchX + vx, mapPatchY + vy);
                            }
                            pass.triangles[(x * 16 + y) * 2 + triangle] =
                                contributes && facesDirection;
                        }
                    }
                }
            }

            std::vector<forge::stbbake::ForegroundLayer> generatedLayers;
            for (const auto& pass : passes) {
                const auto topology = forge::terrain::buildLayerTopology(pass.triangles);
                if (topology.vertices.empty()) continue;
                forge::stbbake::ForegroundLayer layer;
                layer.mappingDirection = pass.direction;
                for (int i = 0; i < 3; ++i)
                    layer.textures[i] = pass.material.textures[i];
                layer.textureMaxSize = pass.material.textureMaxSize;
                layer.bumpMaxSize = pass.material.bumpMaxSize;
                layer.selfIllumination = pass.material.selfIllumination;
                layer.sharedIndexBuffer = topology.fullPatch;
                layer.polygonCount = uint16_t(topology.indices.size() - 2);
                if (!layer.sharedIndexBuffer) layer.indices = topology.indices;
                for (const auto& coord : topology.vertices) {
                    forge::stbbake::ForegroundVertex vertex;
                    vertex.x = uint16_t(patchX + coord.x);
                    vertex.y = uint16_t(patchY + coord.y);
                    vertex.blend = pass.blend[size_t(coord.y) * 17 + coord.x];
                    layer.vertices.push_back(vertex);
                }
                generatedLayers.push_back(std::move(layer));
            }
            candidate.frame.layers = std::move(generatedLayers);
            candidate.body = forge::stbbake::serializeForegroundFrame(candidate.frame);
        }
    }

    // Snapshot structured donor frames so each compression retry starts
    // from the exact decoded field values.
    std::vector<forge::stbbake::ForegroundFrame> fgDonorFrames;
    fgDonorFrames.reserve(foreground.size());
    for (const auto& candidate : foreground) fgDonorFrames.push_back(candidate.frame);

    // Retarget the actual 15-byte CLandscapeLayerMesh vertices. Origin-
    // shifted XY compresses slightly differently than the donor, so the
    // frames can total a few bytes over the zero-slack donor budget.
    // Start with retail-precision normals and progressively coarsen
    // only when the fixed donor allocation requires it.
    auto editForeground = [&](uint32_t clearBits) -> size_t {
        size_t total = 0;
        for (size_t ci = 0; ci < foreground.size(); ++ci) {
            auto& candidate = foreground[ci];
            candidate.frame = fgDonorFrames[ci];
            for (auto& layer : candidate.frame.layers) {
                for (auto& vertex : layer.vertices) {
                    const int lx = int(vertex.x) - int(foregroundMinX);
                    const int ly = int(vertex.y) - int(foregroundMinY);
                    if (lx < 0 || ly < 0 || lx > lev.width() || ly > lev.height())
                        throw std::runtime_error(
                            "foreground vertex maps outside LEV heightfield");
                    vertex.x = uint16_t(worldX + lx);
                    vertex.y = uint16_t(worldY + ly);
                    vertex.height = sampleHeight(lx, ly);
                    vertex.packedNormal = compactAuthoredNormal(lx, ly, clearBits);
                    // Direction-mask rebuilding is opt-in until the generated
                    // material passes are wired into this writer.
                    if (options.rebuildDirectionMask) {
                        const auto cliff = authoredCliffLookup(lx, ly);
                        vertex.cliffU = cliff.first;
                        vertex.cliffV = cliff.second;
                    }
                }
            }
            candidate.body = forge::stbbake::serializeForegroundFrame(candidate.frame);
            candidate.compressed = forge::lzo::compressFramed999(candidate.body);
            total += candidate.compressed.size();
        }
        return total;
    };

    const uint32_t clearLevels[] = {
        0u, 0x0fu, 0x1fu, 0x3fu, 0x7fu, 0xffu, 0x1ffu, 0x3ffu
    };
    size_t fgTotal = 0;
    uint32_t fgClear = clearLevels[0];
    bool fgFits = false;
    auto alignedForegroundSpan = [&]() {
        constexpr size_t alignment = 2048;
        size_t cursor = fgRegionStart;
        for (const auto& candidate : foreground) {
            cursor = (cursor + alignment - 1) / alignment * alignment;
            cursor += candidate.compressed.size();
        }
        return cursor - fgRegionStart;
    };
    for (const uint32_t clearBits : clearLevels) {
        editForeground(clearBits);
        fgTotal = alignedForegroundSpan();
        fgClear = clearBits;
        if (fgTotal <= fgBudget) { fgFits = true; break; }
    }
    for (const auto& candidate : foreground) {
        const auto& seg = chunk.segments[chunk.frameIndices[candidate.frameIndex]];
        notef("foreground frame %zu compressed span %zu -> %zu "
                    "(slot to next frame starts at 0x%zx)",
                    candidate.frameIndex, seg.end - seg.start,
                    candidate.compressed.size(), seg.start);
    }
    if (!fgFits)
        throw std::runtime_error(
            "authored foreground frames exceed donor allocation: " +
            std::to_string(fgTotal) + " > " + std::to_string(fgBudget) +
            " bytes even at coarsest normal quantization (deficit " +
            std::to_string(fgTotal - fgBudget) + " B)");
    if (fgClear != clearLevels[0])
        notef("note: coarsened foreground normal (clear 0x%x) to fit "
                    "the donor budget (%zu <= %zu bytes)",
                    fgClear, fgTotal, fgBudget);

    size_t patchCount = 0;
    for (size_t fi = 0; fi < chunk.frameIndices.size(); ++fi) {
        std::vector<uint8_t> body;
        try { body = forge::stbbake::decodeFrame(chunk, fi); }
        catch (const std::exception&) { continue; }
        const auto h = forge::stbbake::parsePatchHeader(body);
        if (!h.valid || h.isWaterOnly) continue;
        auto pb = forge::stbbake::parsePatchBody(body);
        if (!pb.valid || pb.waterOnly) continue;
        if (options.backgroundTextures && pb.texture.size() >= 19) {
            // the distant-LOD texture: same size and format as the donor's so
            // the body keeps its span; a differently sized answer is ignored
            const auto old = forge::stbbake::parseInlineTexture(pb.texture);
            auto fresh = options.backgroundTextures(int(h.coord0), int(h.coord1), int(h.pw), int(h.ph), int(old.width), int(old.height));
            if (fresh.width == old.width && fresh.height == old.height && fresh.mipData.size() == old.mipData.size()) {
                fresh.levels = old.levels; fresh.pixelFormat0 = old.pixelFormat0; fresh.pixelFormat1 = old.pixelFormat1;
                fresh.usage = old.usage; fresh.surfacePool = old.surfacePool;
                const auto bytes = forge::stbbake::serializeInlineTexture(fresh);
                if (bytes.size() == pb.texture.size()) pb.texture = bytes;
            }
        }
        auto verts = forge::stbbake::decodePatchVertices(pb);
        uint16_t oldMinX = 0xffff, oldMinY = 0xffff;
        for (const auto& v : verts) {
            oldMinX = std::min(oldMinX, v.gridX);
            oldMinY = std::min(oldMinY, v.gridY);
        }
        // The editor derives the packed normal from the authored landscape
        // vertex; reconstruct it from the LEV height gradient. Full precision
        // first; when the fixed-span CRange block cannot hold it, coarsen the
        // low bits of each component progressively (the same ladder the
        // foreground frames use) until the donor allocation fits.
        static const uint32_t patchClearLevels[] = {0u, 0x0fu, 0x1fu, 0x3fu, 0x7fu};
        std::vector<uint8_t> newBody;
        bool fits = false;
        for (const uint32_t clear : patchClearLevels) {
            auto attempt = verts;
            for (auto& v : attempt) {
                const int dx = int(v.gridX) - int(oldMinX);
                const int dy = int(v.gridY) - int(oldMinY);
                const int lx = int(h.coord0) + dx;
                const int ly = int(h.coord1) + dy;
                if (lx < 0 || ly < 0 || lx > lev.width() || ly > lev.height())
                    throw std::runtime_error("patch vertex maps outside LEV heightfield");
                v.gridX = uint16_t(worldX + lx);
                v.gridY = uint16_t(worldY + ly);
                v.height = sampleHeight(lx, ly);
                const uint32_t packed = authoredNormal(lx, ly);
                const uint32_t cx = clear & 0x3ffu, cz = clear & 0xffu;
                const uint32_t qx = (packed & 0x7ffu) & ~cx;
                const uint32_t qy = ((packed >> 11) & 0x7ffu) & ~cx;
                const uint32_t qz = ((packed >> 22) & 0x3ffu) & ~cz;
                v.packedNormal = qx | (qy << 11) | (qz << 22);
            }
            std::vector<uint8_t> candidateBody;
            try {
                candidateBody = forge::stbbake::assemblePatchBodyVerticesFixedSpan(pb, attempt);
            } catch (const std::runtime_error&) {
                continue;   // CRange span: try the next coarser level
            }
            // The frame's on-disk slot is fixed too: its donor span plus the
            // zero PAD that follows it, up to the next non-pad segment.
            const size_t segIdx = chunk.frameIndices[fi];
            size_t slotEnd = chunk.segments[segIdx].end;
            for (size_t si = segIdx + 1; si < chunk.segments.size(); ++si) {
                if (chunk.segments[si].kind != forge::stbbake::SegKind::Pad) break;
                slotEnd = chunk.segments[si].end;
            }
            const size_t slot = slotEnd - chunk.segments[segIdx].start;
            const auto onDisk = forge::lzo::compressFramed999(candidateBody);
            if (onDisk.size() > slot) continue;   // LZO frame: try the next coarser level
            newBody = std::move(candidateBody);
            fits = true;
            if (clear != 0) notef("note: patch frame %zu normals coarsened (clear 0x%x) to fit its %zu-byte slot", fi, clear, slot);
            break;
        }
        if (!fits)
            throw std::runtime_error("patch frame " + std::to_string(fi) + ": vertices do not fit the donor CRange span at any normal precision");
        forge::stbbake::FrameEdit edit;
        edit.frameIndex = fi;
        edit.newBody = std::move(newBody);
        opt.edits.push_back(std::move(edit));
        ++patchCount;
    }
    // Retail donor chunks may carry an additional non-water mesh frame
    // outside the foreground set (the old 64x64 oracle had 17 total for
    // 16 foreground patches). Donor-free native chunks intentionally do
    // not. Require every discovered foreground patch, without imposing
    // the donor's unrelated extra-frame count.
    if (patchCount < foreground.size())
        throw std::runtime_error("expected at least " +
                                 std::to_string(foreground.size()) +
                                 " mesh patches, found " +
                                 std::to_string(patchCount));
    const auto emitted = forge::stbbake::emitChunk(chunk, opt);
    if (!emitted.ok) {
        std::string why;
        for (const auto& note : emitted.notes) why += "\n  " + note;
        throw std::runtime_error("heightfield chunk emission failed:" + why);
    }
    std::vector<uint8_t> authoredChunk = emitted.chunk;
    // The four foreground frames occupy the leading allocation from the
    // first frame through the PAD immediately before the next untracked
    // frame.  Their individually enlarged streams do not fit their old
    // slots, but their total still fits this allocation. Repack them as
    // a contiguous valid LZO run, rewrite all four directory offsets and
    // spans, and leave every later segment at its donor address.
    const size_t foregroundRegionStart =
        chunk.segments[chunk.frameIndices[foreground.front().frameIndex]].start;
    const size_t lastForegroundSegment =
        chunk.frameIndices[foreground.back().frameIndex];
    size_t foregroundRegionEnd = chunk.raw.size();
    for (size_t si = lastForegroundSegment + 1; si < chunk.segments.size(); ++si) {
        if (chunk.segments[si].kind != forge::stbbake::SegKind::Pad) {
            foregroundRegionEnd = chunk.segments[si].start;
            break;
        }
    }
    if (foregroundRegionEnd > authoredChunk.size() ||
        foregroundRegionStart >= foregroundRegionEnd)
        throw std::runtime_error("invalid foreground frame allocation");
    struct ForegroundMove { size_t oldStart, newStart, newSpan; };
    std::vector<ForegroundMove> foregroundMoves;
    // Retail keeps every quad-directory frameOffset on a 2048-byte
    // boundary (10257/10257 live entries across 332 maps, zero
    // exceptions) and pads each frame up to the next boundary. Packing
    // them tight is a container-format violation, so zero the whole
    // region first and align every frame start.
    constexpr size_t kFrameAlignment = 2048;
    std::fill(authoredChunk.begin() + foregroundRegionStart,
              authoredChunk.begin() + foregroundRegionEnd, uint8_t(0));
    size_t foregroundWrite = foregroundRegionStart;
    for (const auto& candidate : foreground) {
        const auto& donorSegment =
            chunk.segments[chunk.frameIndices[candidate.frameIndex]];
        if (foregroundWrite % kFrameAlignment != 0) {
            foregroundWrite =
                (foregroundWrite / kFrameAlignment + 1) * kFrameAlignment;
        }
        if (foregroundWrite + candidate.compressed.size() > foregroundRegionEnd)
            throw std::runtime_error("authored foreground frames exceed donor allocation");
        std::copy(candidate.compressed.begin(), candidate.compressed.end(),
                  authoredChunk.begin() + foregroundWrite);
        foregroundMoves.push_back(ForegroundMove{
            donorSegment.start, foregroundWrite, candidate.compressed.size()});
        foregroundWrite += candidate.compressed.size();
    }
    std::fill(authoredChunk.begin() + foregroundWrite,
              authoredChunk.begin() + foregroundRegionEnd, uint8_t(0));
    size_t foregroundDirectoryRewired = 0;
    for (const auto& entry : forge::stbbake::parseQuadDir(chunk)) {
        for (const auto& move : foregroundMoves) {
            if (entry.frameOffset != move.oldStart) continue;
            // record layout (retail, parseQuadDir): {frameOffset @0, frameSpan @4, aabb @8, flags @32}
            put32At(authoredChunk, entry.dirOffset + 0,
                    static_cast<uint32_t>(move.newStart));
            put32At(authoredChunk, entry.dirOffset + 4,
                    static_cast<uint32_t>(move.newSpan));
            ++foregroundDirectoryRewired;
            break;
        }
    }
    if (foregroundDirectoryRewired != foreground.size())
        throw std::runtime_error("not every foreground frame had a directory record");
    const auto quadDir = forge::stbbake::parseQuadDir(chunk);
    // Quad-directory XY bounds are still in the donor chunk's world
    // coordinates at this point.  Vertex and patch retargeting above
    // deliberately relocates the authored mesh to worldX/worldY, but
    // the old code subtracted the destination origin here as well.
    // That only worked when baking in-place over the donor origin and
    // rejected valid relocated maps as "outside the authored LEV".
    // Normalize against the donor's own minimum XY instead.
    float donorMinX = std::numeric_limits<float>::max();
    float donorMinY = std::numeric_limits<float>::max();
    for (const auto& entry : quadDir) {
        donorMinX = std::min(donorMinX, entry.aabb[0]);
        donorMinY = std::min(donorMinY, entry.aabb[1]);
    }
    std::vector<std::pair<float, float>> authoredZBounds;
    authoredZBounds.reserve(quadDir.size());
    for (const auto& entry : quadDir) {
        const int lx0 = int(std::lround(entry.aabb[0] - donorMinX));
        const int ly0 = int(std::lround(entry.aabb[1] - donorMinY));
        const int lx1 = int(std::lround(entry.aabb[3] - donorMinX));
        const int ly1 = int(std::lround(entry.aabb[4] - donorMinY));
        if (lx0 < 0 || ly0 < 0 || lx1 > lev.width() ||
            ly1 > lev.height() || lx0 > lx1 || ly0 > ly1) {
            throw std::runtime_error(
                "quadtree AABB falls outside the authored LEV heightfield");
        }
        float minZ = std::numeric_limits<float>::max();
        float maxZ = -std::numeric_limits<float>::max();
        for (int y = ly0; y <= ly1; ++y) {
            for (int x = lx0; x <= lx1; ++x) {
                const float z = sampleHeight(x, y);
                minZ = std::min(minZ, z);
                maxZ = std::max(maxZ, z);
            }
        }
        authoredZBounds.emplace_back(minZ, maxZ);
    }
    const size_t boundsUpdated =
        forge::stbbake::updateQuadDirZBounds(authoredChunk, authoredZBounds);
    // The quad-directory AABBs are still at the donor's world origin.
    // Relocate them onto this map's origin, or every entry falls
    // outside the InfoBlock box the engine streams against.
    const size_t quadXYTranslated = forge::stbbake::translateQuadDirXY(
        authoredChunk, float(worldX) - donorMinX, float(worldY) - donorMinY);
        notef("seam samples: %zu neighbor-resolved, %zu clamped locally",
                resolvedNeighborSamples.size(), clampedWorldSamples.size());
    notef("baked %zu LEV-driven composed patches + %zu foreground frames "
                "at (%d,%d) with %zu landscape neighbor(s): %zu -> %zu bytes, "
                "%zu frames re-encoded, %zu quad entries rewired, "
                "%zu AABBs height-fitted",
                patchCount, foreground.size(), worldX, worldY,
                normalNeighbors.size(),
                chunkBytes.size(), authoredChunk.size(),
                emitted.framesReencoded + foreground.size(),
                emitted.quadEntriesRewired + foregroundDirectoryRewired,
                boundsUpdated);

    result.chunk = std::move(authoredChunk);
    result.patches = patchCount;
    result.foregroundFrames = foreground.size();
    return result;
}

} // namespace forge::stbbake
