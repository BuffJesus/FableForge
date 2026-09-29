#include "profile.hpp"
// The whole-world overview (vanilla FableWin's 2D world map and 3D engine view over every
// map, Aeon's review item 12): every map of the world as a low-res textured tile at its WLD
// origin, built on worker threads and cached on disk. The World tab draws the tiles inside
// its 2D boxes and, in 3D, flies over all of them; double-click a map to edit it.
#include "app.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <thread>

#include "forge/lev.hpp"
#include "forge/levelstore.hpp"
#include "theme.hpp"
#include "thingsexport.hpp"

namespace albion::gui {

namespace fs = std::filesystem;
namespace te = albion::terrainexport;

namespace {

fs::path tileCacheDir() {
    // Isolate first-use performance runs without deleting the user's warm cache.
    if (const char* overridePath = std::getenv("FABLEFORGE_TILE_CACHE"); overridePath && *overridePath) return fs::path(overridePath);
    const char* base = std::getenv("LOCALAPPDATA");
    if (!base) base = std::getenv("TEMP");
    return fs::path(base ? base : ".") / "FableForge" / "worldtiles";
}

std::string sourceRevision(const fs::path& src) {
    // std::filesystem::file_time_type has different epochs/units in libstdc++
    // and MSVC. Raw Windows metadata keeps Release/profile caches compatible.
    WIN32_FILE_ATTRIBUTE_DATA metadata{};
    const bool found = GetFileAttributesExW(src.c_str(), GetFileExInfoStandard, &metadata) != 0;
    const uint64_t size = (uint64_t(metadata.nFileSizeHigh) << 32) | metadata.nFileSizeLow;
    const uint64_t time = (uint64_t(metadata.ftLastWriteTime.dwHighDateTime) << 32) | metadata.ftLastWriteTime.dwLowDateTime;
    return src.string() + "|" + std::to_string(size) + "|" + std::to_string(time) + (found ? "|present" : "|missing");
}

// Level source plus every external input used by the overview ground/water bake.
std::string sourceKey(const forge::levelstore::Layout& layout, const std::string& name, const std::string& dependencies) {
    std::error_code ec;
    fs::path src = forge::levelstore::loosePath(layout, name + ".lev");
    if (!fs::is_regular_file(src, ec)) src = layout.wad;
    return sourceRevision(src) + dependencies;
}

} // namespace

void App::startWorldTiles() {
    if (!installValid_ || !worldLoaded_ || !ctx_.ready()) return;
    if (worldTilesFor_ == installPath_ && (!worldTileWorkers_.empty() || !worldTiles_.empty())) return;
    stopWorldTiles();
    worldTilesFor_ = installPath_;
    auto names = std::make_shared<std::vector<std::string>>();
    for (const auto& b : world_.maps) names->push_back(b.name);
    worldTileTotal_ = names->size();
    worldTileCancel_ = std::make_shared<std::atomic<bool>>(false);
    auto next = std::make_shared<std::atomic<size_t>>(0);
    const auto ctxHold = std::make_shared<const te::Context>(ctx_);
    const auto layout = std::make_shared<forge::levelstore::Layout>(forge::levelstore::detect(installPath_));
    const fs::path root = installPath_, cache = tileCacheDir();
    const float gain = settings_.gain;
    const std::string dependencies = "|overview-water-v4|gain:" + std::to_string(std::bit_cast<uint32_t>(gain)) +
        "|" + sourceRevision(root / "data/CompiledDefs/game.bin") + "|" + sourceRevision(root / "data/CompiledDefs/names.bin") +
        "|" + sourceRevision(root / "data/graphics/pc/textures.big") + "|" + sourceRevision(root / "data/Levels/FinalAlbion_RT.stb");
    const auto cancel = worldTileCancel_;
    const unsigned threads = std::clamp(std::thread::hardware_concurrency() / 2u, 1u, 6u);
    worldTileStarted_ = std::chrono::steady_clock::now();
    for (unsigned w = 0; w < threads; ++w) {
        worldTileWorkers_.push_back(std::async(std::launch::async, [this, names, next, ctxHold, layout, root, cache, gain, dependencies, cancel, w]() {
            FORGE_THREAD("World overview worker");
            const fs::path tmpDir = fs::temp_directory_path() / "FableForge" / "worldtiles";
            std::error_code ec;
            fs::create_directories(tmpDir, ec);
            for (;;) {
                if (cancel->load()) return;
                const size_t i = next->fetch_add(1);
                if (i >= names->size()) return;
                const std::string& name = (*names)[i];
                FORGE_ZONE("World overview tile");
                FORGE_ZONE_TEXT(name);
                worldtiles::Tile tile;
                const std::string key = sourceKey(*layout, name, dependencies);
                const fs::path cached = cache / (name + ".tile");
                bool ok = worldtiles::loadTile(cached, key, tile);
                if (!ok) {
                    try {
                        const auto bytes = forge::levelstore::readFile(*layout, name + ".lev");
                        if (!bytes) continue;   // a WLD map the levels do not carry
                        const fs::path lev = tmpDir / (name + "_" + std::to_string(w) + ".lev");
                        std::ofstream(lev, std::ios::binary).write(reinterpret_cast<const char*>(bytes->data()), std::streamsize(bytes->size()));
                        const auto file = forge::lev::File::open(lev);
                        tile = worldtiles::buildTile(file, name, ctxHold.get(), root, gain);
                        fs::remove(lev, ec);
                        worldtiles::saveTile(cached, key, tile);
                        ok = true;
                    } catch (const std::exception&) { ok = false; }
                }
                if (!ok) continue;
                std::lock_guard<std::mutex> lock(worldTileMutex_);
                worldTileDone_.push_back(std::move(tile));
            }
        }));
    }
}

void App::stopWorldTiles() {
    clearWorldDetail(); // includes inactive residents and generation of an old install's worker
    if (worldTileCancel_) worldTileCancel_->store(true);
    for (auto& f : worldTileWorkers_) if (f.valid()) f.wait();
    worldTileWorkers_.clear();
    { std::lock_guard<std::mutex> lock(worldTileMutex_); worldTileDone_.clear(); }
    worldTiles_.clear();
    worldTileTex_.clear();   // the renderer owns the textures (uiTexture keys are reused)
    worldLayerAt_.clear();
    renderer_.clearLayer(Renderer::kWorldLayer);
    worldTilesFor_.clear();
    worldTileTotal_ = 0;
}

void App::pollWorldTiles() {
    FORGE_ZONE("World overview poll / upload");
    if (!worldMode_) return;
    if (!worldLoaded_ && installValid_ && !worldFuture_.valid() && worldLoadedFrom_ != saveRoot() + "|" + packDest_) loadWorld();   // the 3D view has no 2D canvas to trigger it
    if (worldTilesFor_.empty() || worldTilesFor_ != installPath_) startWorldTiles();
    std::vector<worldtiles::Tile> done;
    { std::lock_guard<std::mutex> lock(worldTileMutex_); done.swap(worldTileDone_); }
    for (auto& t : done) {
        const std::string name = t.name;
        worldTiles_[name] = std::move(t);
    }
    const auto uploadStarted = std::chrono::steady_clock::now();
    int uploaded = 0;
    for (const auto& [name, tile] : worldTiles_) {
        if (worldTileTex_.count(name)) continue;
        worldTileTex_[name] = renderer_.uiTexture("worldtile:" + name, tile.ground);
        if (++uploaded >= 4 || std::chrono::steady_clock::now() - uploadStarted >= std::chrono::milliseconds(2)) break;
    }
    if (!worldTileWorkers_.empty()) {
        bool all = true;
        for (auto& f : worldTileWorkers_) all = all && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        if (all) {
            for (auto& f : worldTileWorkers_) f.get();
            worldTileWorkers_.clear();
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - worldTileStarted_).count();
            char msg[128]; std::snprintf(msg, sizeof msg, "world: %zu of %zu map tiles ready (%.1f s)", worldTiles_.size(), worldTileTotal_, secs);
            pushLog(msg, 0);
        }
    }
    if (!world3D_) return;
    // the 3D layer: append new tiles, rebuild when a pending move changed where one sits
    bool moved = false;
    auto placement = [&](const editor::WorldMapBox& b) {
        int x = b.x, y = b.y;
        for (const auto& mv : worldPending_) if (mv.name == b.name) { x = mv.x; y = mv.y; }
        return std::pair{x, y};
    };
    // Iterate known boxes once. Looking each name up in worldPlacement made
    // this every-frame maintenance quadratic even when nothing had moved.
    for (const auto& b : world_.maps) {
        const auto at = worldLayerAt_.find(b.name);
        if (at != worldLayerAt_.end() && placement(b) != at->second) { moved = true; break; }
    }
    if (moved) { renderer_.clearLayer(Renderer::kWorldLayer); worldLayerAt_.clear(); clearWorldDetail(); }
    const auto layerStarted = std::chrono::steady_clock::now();
    int appended = 0;
    for (size_t i = 0; i < world_.maps.size(); ++i) {
        const auto& b = world_.maps[i];
        const auto& name = b.name;
        if (worldLayerAt_.count(name)) continue;
        const auto tile = worldTiles_.find(name);
        if (tile == worldTiles_.end()) continue;
        const auto [x, y] = placement(b);
        foliageexport::Scene one;
        worldtiles::appendMesh(tile->second, one, float(x), float(y));
        // Y = "convert the Fable Z-up tiles to render Y-up", as the neighbour layer does; tagged per map
        renderer_.appendLayer(Renderer::kWorldLayer, one, te::UpAxis::Y, int(i));
        renderer_.appendWorldWater(tile->second.water, int(i), float(x), float(y));
        const auto detail = worldDetailShown_.find(name);
        renderer_.setLayerTagVisible(Renderer::kWorldLayer, int(i), detail == worldDetailShown_.end());
        worldLayerAt_[name] = {x, y};
        if (++appended >= 4 || std::chrono::steady_clock::now() - layerStarted >= std::chrono::milliseconds(2)) break;
    }
    updateWorldDetail();
}

// The ground under a world point (Fable x, y) from the tiles; `inside` = some map covers it.
float App::worldGroundAt(float wx, float wy, bool& inside, std::string* name) const {
    inside = false;
    const editor::WorldMapBox* best = nullptr;
    int bestX = 0, bestY = 0;
    for (const auto& b : world_.maps) {
        // We already have the box. worldPlacement searches all map names again,
        // which made each ray sample quadratic in the number of world maps.
        int x = b.x, y = b.y;
        for (const auto& mv : worldPending_) if (mv.name == b.name) { x = mv.x; y = mv.y; }
        if (wx < float(x) || wy < float(y) || wx > float(x + b.w) || wy > float(y + b.h)) continue;
        if (!best || b.w * b.h < best->w * best->h) { best = &b; bestX = x; bestY = y; }   // smallest box on top, as in 2D
    }
    if (!best) return 0.0f;
    const auto t = worldTiles_.find(best->name);
    if (t == worldTiles_.end()) return 0.0f;
    inside = true;
    if (name) *name = best->name;
    return worldtiles::heightAt(t->second, wx - float(bestX), wy - float(bestY));
}

// March a render-space ray over the tiles: the first sample under the ground.
bool App::worldPickTile(const float o[3], const float d[3], std::string& name, float hit[3]) const {
    FORGE_ZONE("World ground ray");
    float t = 0.0f;
    float prevT = 0.0f;
    for (int k = 0; k < 4000 && t < 60000.0f; ++k) {
        const float px = o[0] + d[0] * t, py = o[1] + d[1] * t, pz = o[2] + d[2] * t;
        bool inside = false;
        std::string n;
        const float g = worldGroundAt(px, -pz, inside, &n);
        if (inside && py <= g) {
            // refine between the last two samples
            float a = prevT, b = t;
            for (int r = 0; r < 12; ++r) {
                const float m = (a + b) * 0.5f;
                bool in2 = false;
                const float g2 = worldGroundAt(o[0] + d[0] * m, -(o[2] + d[2] * m), in2, nullptr);
                if (in2 && o[1] + d[1] * m <= g2) b = m; else a = m;
            }
            hit[0] = o[0] + d[0] * b; hit[1] = o[1] + d[1] * b; hit[2] = o[2] + d[2] * b;
            name = n;
            return true;
        }
        prevT = t;
        t += std::max(1.0f, t * 0.004f);
    }
    return false;
}

int App::worldTag(const std::string& name) const {
    for (size_t i = 0; i < world_.maps.size(); ++i) if (world_.maps[i].name == name) return int(i);
    return -1;
}

bool App::releaseWorldDetail() {
    FORGE_ZONE("World payload handoff");
    worldDetailRetiring_ = !worldDetailRelease_.tryRelease(worldDetailUpload_);
    return !worldDetailRetiring_;
}

void App::clearWorldDetail() {
    ++worldDetailGeneration_;   // invalidate an in-flight load after a move or option change
    renderer_.clearLayer(Renderer::kWorldDetailLayer);
    worldDetailCache_.clear();
    releaseWorldDetail();
    worldDetailUploadAt_ = 0;
    worldDetailLoading_.clear();
    worldDetailWanting_ = 0;
    worldDetailNext_ = 0;
    for (const auto& [name, on] : worldDetailShown_) {
        renderer_.setLayerTagVisible(Renderer::kWorldLayer, worldTag(name), true);
        renderer_.setLayerTagFade(Renderer::kWorldLayer, worldTag(name), 1.0f);
    }
    worldDetailShown_.clear();
}

// Which maps get full detail: the nearest ones (by their box) to the camera's ground point, within
// the radius; one load at a time on a worker; far ones dropped (their tile shown again).
void App::updateWorldDetail() {
    FORGE_ZONE("World detail demand / upload");
    if (worldDetailRetiring_ && !releaseWorldDetail()) return;
    if (!world3D_ || !worldDetailOn_ || !ctx_.ready()) { if (!worldDetailShown_.empty() || worldDetailCache_.size() || worldDetailUpload_) clearWorldDetail(); return; }
    if (time_ >= worldVideoMemoryNext_) {
        worldVideoMemoryNext_ = time_ + 1.0;
        worldVideoMemory_ = worldVideoMemoryOverride_ ? *worldVideoMemoryOverride_ : renderer_.queryVideoMemory();
        const size_t limit = worldCacheMemoryBudget_.observe(worldVideoMemory_.valid,
            worldVideoMemory_.budget, worldVideoMemory_.usage, worldDetailCache_.bytes());
        { FORGE_ZONE("World inactive cache trim");
            for (const auto& name : worldDetailCache_.setBudget(limit)) {
                renderer_.removeLayerTag(Renderer::kWorldDetailLayer, worldTag(name));
                ++worldMemoryEvictions_;
            }
        }
        FORGE_PLOT("GPU process memory budget", worldVideoMemory_.budget);
        FORGE_PLOT("GPU process memory usage", worldVideoMemory_.usage);
        FORGE_PLOT("World inactive memory allowance", limit);
    }
    worldDetailBudget_.observe(ImGui::GetIO().DeltaTime, worldDetailMaps_,
        worldAutoDetail_ && !ImGui::GetIO().AppFocusLost && !worldDetailFuture_.valid() && !worldDetailUpload_ && worldDetailWanting_ == 0);
    for (auto& [name, state] : worldDetailShown_) {
        state.fade = std::clamp(state.fade + (state.wanted ? 1.0f : -1.0f) * std::min(ImGui::GetIO().DeltaTime, 0.05f) * 4.0f, 0.0f, 1.0f);
        const int tag = worldTag(name);
        renderer_.setLayerTagFade(Renderer::kWorldDetailLayer, tag, worldDetailFadeOverride_.value_or(state.fade));
        renderer_.setLayerTagVisible(Renderer::kWorldLayer, tag, false);
    }
    if (worldDetailUpload_) {
        FORGE_ZONE("World paced upload");
        auto& detail = *worldDetailUpload_;
        const int tag = worldTag(detail.name);
        const auto started = std::chrono::steady_clock::now();
        bool ok = detail.generation == worldDetailGeneration_ && tag >= 0;
        // A single driver allocation cannot be interrupted. Bound the work between
        // allocations, and keep every batch hidden until the whole map is ready.
        // Fast hardware may fit many small batches inside the same time budget.
        // Keep a finite count guard, but let elapsed time normally set the limit.
        for (int count = 0; ok && worldDetailUploadAt_ < detail.batches.size() && count < 32; ++count) {
            ok = renderer_.appendPreparedBatch(Renderer::kWorldDetailLayer,
                detail.batches[worldDetailUploadAt_++], detail.images, tag, false);
            if (std::chrono::steady_clock::now() - started >= std::chrono::milliseconds(2)) break;
        }
        if (!ok || worldDetailUploadAt_ == detail.batches.size()) {
            if (ok && std::any_of(detail.batches.begin(), detail.batches.end(), [](const auto& batch) { return batch.terrainMorph; })) {
                renderer_.setLayerTagVisible(Renderer::kWorldDetailLayer, tag, true);
                renderer_.setLayerTagFade(Renderer::kWorldDetailLayer, tag, 0.0f);
                renderer_.setLayerTagVisible(Renderer::kWorldLayer, tag, false);
                worldDetailShown_[detail.name] = {};
            } else {
                renderer_.removeLayerTag(Renderer::kWorldDetailLayer, tag);
                pushLog("world: could not upload detail for " + detail.name + "; retaining overview", 1);
            }
            if (!releaseWorldDetail()) return;
            worldDetailLoading_.clear();
            worldDetailNext_ = 0;
        }
    }
    // Demand selection is throttled, but completed CPU work must not sit behind
    // that timer. Re-rank against the current eye before accepting a ready map.
    const bool detailReady = worldDetailFuture_.valid() &&
        worldDetailFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready && worldDetailRelease_.idle();
    if (time_ < worldDetailNext_ && !detailReady) return;
    worldDetailNext_ = time_ + 0.4;
    // Residency follows translation, not the centre-ray ground hit. Near the
    // horizon that hit can jump across the world during an in-place look,
    // evicting nearby maps even though the eye has not moved.
    const float gx = worldCamera_.posX, gy = -worldCamera_.posZ;
    const float loadRadius = std::clamp(worldCamera_.distance, worldDetailRadius_, worldDetailRadius_ * 2.0f);
    auto distTo = [&](const editor::WorldMapBox& b) {
        int x = b.x, y = b.y;
        for (const auto& mv : worldPending_) if (mv.name == b.name) { x = mv.x; y = mv.y; }
        const float dx = std::max({float(x) - gx, 0.0f, gx - float(x + b.w)});
        const float dy = std::max({float(y) - gy, 0.0f, gy - float(y + b.h)});
        return std::sqrt(dx * dx + dy * dy);
    };
    // too high up to see detail: none (the camera's height over the ground stands in for zoom)
    bool inside = false;
    const float ground = worldGroundAt(worldCamera_.posX, -worldCamera_.posZ, inside, nullptr);
    const float height = worldCamera_.posY - (inside ? ground : 0.0f);
    std::vector<std::pair<float, std::string>> want;
    if (height < worldDetailRadius_ * 2.75f)
        for (const auto& b : world_.maps) {
            const float d = distTo(b);
            const bool resident = worldDetailShown_.count(b.name) != 0;
            // Retain nearby residents across a small camera movement instead of
            // repeatedly rebuilding maps at the distance/count boundary.
            if (height < worldDetailRadius_ * (resident ? 2.75f : 2.5f) && d <= loadRadius * (resident ? 1.15f : 1.0f))
                want.push_back({d - (resident ? loadRadius * 0.1f : 0.0f), b.name});
        }
    std::sort(want.begin(), want.end());
    const int mapLimit = worldAutoDetail_ ? worldDetailBudget_.maps : worldDetailMaps_;
    FORGE_PLOT("World detail budget", mapLimit);
    FORGE_PLOT("World resident maps", worldDetailShown_.size());
    if (int(want.size()) > mapLimit) want.resize(size_t(mapLimit));
    // Rescue wanted residents before admitting old active maps to the LRU;
    // otherwise an outgoing map could evict the very map we are returning to.
    for (const auto& [distance, name] : want) {
        if (worldDetailShown_.count(name) || !worldDetailCache_.take(name)) continue;
        const int tag = worldTag(name);
        renderer_.setLayerTagVisible(Renderer::kWorldDetailLayer, tag, true);
        renderer_.setLayerTagFade(Renderer::kWorldDetailLayer, tag, 0.0f);
        renderer_.setLayerTagVisible(Renderer::kWorldLayer, tag, false);
        worldDetailShown_[name] = {};
        ++worldDetailCacheHits_;
    }
    // Check the latest camera and settings before spending time on a GPU upload.
    if (detailReady) {
        WorldDetail detail = worldDetailFuture_.get();
        const bool wanted = std::any_of(want.begin(), want.end(), [&](const auto& w) { return w.second == detail.name; });
        const int tag = worldTag(detail.name);
        if (wanted && detail.generation == worldDetailGeneration_ && tag >= 0 && !worldDetailShown_.count(detail.name)) {
            worldDetailUpload_ = std::move(detail);
            worldDetailUploadAt_ = 0;
        } else {
            worldDetailUpload_ = std::move(detail);
            releaseWorldDetail();
            worldDetailLoading_.clear();
        }
    }
    if (worldDetailUpload_ && std::none_of(want.begin(), want.end(), [&](const auto& w) { return w.second == worldDetailUpload_->name; })) {
        renderer_.removeLayerTag(Renderer::kWorldDetailLayer, worldTag(worldDetailUpload_->name));
        releaseWorldDetail();
        worldDetailLoading_.clear();
    }
    // Retain recently used maps invisibly, within the inactive byte/map limits.
    for (auto it = worldDetailShown_.begin(); it != worldDetailShown_.end();) {
        const bool keep = std::any_of(want.begin(), want.end(), [&](const auto& w) { return w.second == it->first; });
        it->second.wanted = keep;
        if (keep || it->second.fade > 0) { ++it; continue; }
        const int tag = worldTag(it->first);
        renderer_.setLayerTagVisible(Renderer::kWorldDetailLayer, tag, false);
        for (const auto& evicted : worldDetailCache_.retain(it->first, renderer_.layerTagBytes(Renderer::kWorldDetailLayer, tag)))
            renderer_.removeLayerTag(Renderer::kWorldDetailLayer, worldTag(evicted));
        renderer_.setLayerTagVisible(Renderer::kWorldLayer, tag, true);
        renderer_.setLayerTagFade(Renderer::kWorldLayer, tag, 1.0f);
        it = worldDetailShown_.erase(it);
    }
    worldDetailWanting_ = 0;
    FORGE_PLOT("World inactive detail bytes", worldDetailCache_.bytes());
    FORGE_PLOT("World detail cache hits", worldDetailCacheHits_);
    for (const auto& w : want) if (!worldDetailShown_.count(w.second)) ++worldDetailWanting_;
    // start the nearest missing one
    if (worldDetailFuture_.valid() || worldDetailUpload_) return;
    // Bound retained CPU data: finish the previous payload before preparing
    // another map. Camera/input/rendering continue while the worker frees it.
    if (!worldDetailRelease_.idle()) { worldDetailNext_ = 0; return; }
    for (const auto& [d, name] : want) {
        if (worldDetailShown_.count(name)) continue;
        const auto tile = worldTiles_.find(name);
        if (tile == worldTiles_.end()) continue;
        // Only the small height grid is needed; do not copy colour/water payloads.
        worldtiles::Tile overview;
        overview.cellsX = tile->second.cellsX; overview.cellsY = tile->second.cellsY;
        overview.stride = tile->second.stride; overview.gw = tile->second.gw; overview.gh = tile->second.gh;
        overview.heights = tile->second.heights;
        const MapEntry* e = findEntry(name);
        if (!e) continue;
        std::string err;
        const std::string lev = resolveLevPath(*e, err);
        if (lev.empty()) continue;
        int x, y; worldPlacement(name, x, y);
        const auto ctxHold = std::make_shared<const te::Context>(ctx_);
        const fs::path root = installPath_;
        const float gain = settings_.gain;
        const bool creatures = worldDetailCreatures_, things = worldDetailThings_, plants = worldDetailFoliage_;
        const uint64_t generation = worldDetailGeneration_;
        worldDetailLoading_ = name;
        ++worldDetailLoads_;
        worldDetailFuture_ = std::async(std::launch::async, [ctxHold, root, gain, lev, name, x, y, creatures, things, plants, generation, overview = std::move(overview)]() {
            FORGE_THREAD("World detail worker");
            FORGE_ZONE("World detail prepare");
            FORGE_ZONE_TEXT(name);
            WorldDetail r; r.name = name; r.generation = generation;
            foliageexport::Scene ground, foliage, placed;
            const te::Context* ctx = ctxHold.get();
            try {
                const auto file = forge::lev::File::open(lev);
                te::Options o;
                o.textures = true; o.texelsPerCell = 4; o.gain = gain; o.up = te::UpAxis::Z; o.water = false;
                o.gameRoot = root; o.mapName = name;
                auto sc = te::buildScene(file, o, ctx);
                foliageexport::Mesh m;
                m.name = name;
                m.geometry.vertices.reserve(sc.vertices.size());
                for (const auto& v : sc.vertices) m.geometry.vertices.push_back({v.px, v.py, v.pz, v.nx, v.ny, v.nz, v.u, v.v});
                foliageexport::SubMesh part;
                part.indices = sc.indices;
                if (sc.hasAlbedo) { part.image = 0; ground.images.push_back(sc.albedo); }
                m.parts.push_back(std::move(part));
                foliageexport::Instance inst;
                inst.mesh = 0; inst.x = float(x); inst.y = float(y); inst.z = 0; inst.scale = 1;
                ground.meshes.push_back(std::move(m));
                ground.instances.push_back(inst);
            } catch (const std::exception&) {}
            if (plants) try {
                foliageexport::Options fo;
                fo.gameRoot = root; fo.textures = true; fo.up = te::UpAxis::Y;
                fo.mapLocal = false;   // STB instances are world placed already
                foliage = foliageexport::load(name, fo, *ctx);
            } catch (const std::exception&) {}
            if (things || creatures) {
                try {
                    thingsexport::Options to;
                    to.gameRoot = root; to.textures = true; to.up = te::UpAxis::Y;
                    to.creatures = creatures;
                    to.originX = float(x); to.originY = float(y);
                    placed = thingsexport::load(name, to, *ctx, nullptr);
                } catch (const std::exception&) {}
            }
            auto prepare = [&](foliageexport::Scene& scene, bool morph = false) {
                auto batches = Renderer::prepareLayer(scene, te::UpAxis::Y);
                const int offset = int(r.images.size());
                for (auto& batch : batches) {
                    batch.terrainMorph = morph;
                    if (morph) for (auto& v : batch.vertices) {
                        v.walk = worldtiles::meshHeightAt(overview, v.px - float(x), -v.pz - float(y));
                        batch.lo[1] = std::min(batch.lo[1], v.walk);
                        batch.hi[1] = std::max(batch.hi[1], v.walk);
                    }
                    if (batch.image >= 0) batch.image += offset;
                    r.batches.push_back(std::move(batch));
                }
                for (auto& image : scene.images) r.images.push_back(std::move(image));
            };
            prepare(ground, true); prepare(foliage); prepare(placed);
            size_t geometryBytes = 0, expandedBytes = 0;
            for (const auto& batch : r.batches) {
                geometryBytes += batch.vertices.size() * sizeof(Renderer::LayerVertex) + batch.indices.size() * sizeof(uint32_t);
                expandedBytes += (batch.indices.empty() ? batch.vertices.size() : batch.indices.size()) * sizeof(Renderer::LayerVertex);
            }
            FORGE_PLOT("World prepared geometry bytes", geometryBytes);
            FORGE_PLOT("World expanded equivalent bytes", expandedBytes);
            return r;
        });
        break;
    }
}

void App::setWorld3D(bool on) {
    world3D_ = on;
    renderer_.worldOnly = on && worldMode_;
    if (on && !worldCameraSet_ && worldLoaded_) {
        // start over the whole world, looking north-ish and down
        const float cx = float(world_.minX + world_.maxX) * 0.5f, cy = float(world_.minY + world_.maxY) * 0.5f;
        const float span = float(std::max(world_.maxX - world_.minX, world_.maxY - world_.minY));
        worldCamera_.lookAt(cx, 0.0f, -cy, 0.6f, 0.9f, span * 0.9f);
        worldCamera_.flySpeed = 400.0f;
        worldCameraSet_ = true;
    }
    if (!on) worldLayerAt_.clear(), renderer_.clearLayer(Renderer::kWorldLayer), clearWorldDetail();
}

void App::drawWorld3D(const ImVec2& origin, const ImVec2& size) {
    using theme::S;
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool overGround = false;
    const float below = worldGroundAt(worldCamera_.posX, -worldCamera_.posZ, overGround, nullptr);
    const float nearPlane = worldview::nearPlane(worldCamera_.posY - (overGround ? below : 0.0f));
    ID3D11ShaderResourceView* srv = renderer_.render(uint32_t(std::max(size.x, 8.0f)), uint32_t(std::max(size.y, 8.0f)), worldCamera_, mode_, time_, nearPlane);
    ImGui::SetCursorScreenPos(origin);
    if (srv) ImGui::Image((ImTextureID)(intptr_t)srv, size);
    else dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), theme::col(theme::Bg0));
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("##world3d", ImVec2(std::max(size.x, 8.0f), std::max(size.y, 8.0f)), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    viewportOrigin_ = origin; viewportSize_ = size;
    viewportHovered_ = ImGui::IsItemHovered();
    auto_.registerWidget("viewport");

    // camera: the map viewport's grammar (RMB look + WASD fly, LMB drag turn/dolly, Alt orbit, MMB pan, wheel)
    const bool active = viewportHovered_ || worldCaptured_;
    const bool rmb = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    worldCaptured_ = active && (rmb || ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Middle));
    Camera& c = worldCamera_;
    if (active) {
        const float dx = io.MouseDelta.x, dy = io.MouseDelta.y;
        const float panK = c.distance / std::max(size.y, 1.0f) * 1.6f;
        if (rmb) {
            c.look(-dx * 0.005f, dy * 0.005f);
            if (io.MouseWheel != 0) c.flySpeed = std::clamp(c.flySpeed * std::pow(1.25f, io.MouseWheel), 5.0f, 20000.0f);
            float fwd = 0, strafe = 0, rise = 0;
            if (ImGui::IsKeyDown(ImGuiKey_W)) fwd += 1; if (ImGui::IsKeyDown(ImGuiKey_S)) fwd -= 1;
            if (ImGui::IsKeyDown(ImGuiKey_D)) strafe += 1; if (ImGui::IsKeyDown(ImGuiKey_A)) strafe -= 1;
            if (ImGui::IsKeyDown(ImGuiKey_E)) rise += 1; if (ImGui::IsKeyDown(ImGuiKey_Q)) rise -= 1;
            const float boost = io.KeyShift ? 3.0f : 1.0f;
            if (fwd || strafe || rise) c.fly(fwd * boost, strafe * boost, rise * boost, std::min(io.DeltaTime, 0.1f));
        } else {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && viewportHovered_) { worldClickArmed_ = true; worldClickPos_ = io.MousePos; }
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f)) {
                worldClickArmed_ = false;
                if (io.KeyAlt) c.orbit(-dx * 0.008f, dy * 0.008f);
                else { c.turn(-dx * 0.005f); c.dolly(-dy * 0.02f); }
            }
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) c.pan(-dx * panK, dy * panK);
            if (io.MouseWheel != 0 && viewportHovered_) c.dolly(io.MouseWheel);
        }
    }
    if (viewportHovered_ && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F)) worldCameraSet_ = false, setWorld3D(true);

    // hover: the map under the cursor
    worldHover_.clear();
    float hit[3] = {0, 0, 0};
    if (viewportHovered_ && !rmb && size.x > 0 && size.y > 0) {
        float o[3], d[3];
        renderer_.screenRay((io.MousePos.x - origin.x) / size.x, (io.MousePos.y - origin.y) / size.y, o, d);
        std::string name;
        if (worldPickTile(o, d, name, hit)) worldHover_ = name;
    }
    if (worldClickArmed_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        worldClickArmed_ = false;
        if (!worldHover_.empty()) worldSelect(worldHover_); else worldSelected_.clear();
    }
    // double-click: edit that map, the camera kept where it was (map-local)
    if (viewportHovered_ && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !worldHover_.empty() && worldPending_.empty()) {
        openFromWorld3D(worldHover_);
        return;
    }

    // outline the hovered and selected maps: their boxes projected at the ground
    auto outline = [&](const std::string& name, ImU32 col, float thick) {
        const auto* b = world_.find(name);
        if (!b) return;
        int x, y; if (!worldPlacement(name, x, y)) return;
        const auto t = worldTiles_.find(name);
        const int n = 24;
        std::vector<ImVec2> pts;
        auto push = [&](float fx, float fy) {
            const float h = t != worldTiles_.end() ? worldtiles::heightAt(t->second, fx - float(x), fy - float(y)) : 0.0f;
            const float p[3] = {fx, h + 0.5f, -fy};
            float u, v;
            if (renderer_.project(p, u, v)) pts.push_back(ImVec2(origin.x + u * size.x, origin.y + v * size.y));
            else if (pts.size() > 1) { dl->AddPolyline(pts.data(), int(pts.size()), col, 0, thick); pts.clear(); }
            else pts.clear();
        };
        for (int i = 0; i <= n; ++i) push(float(x) + float(b->w) * float(i) / n, float(y));
        for (int i = 1; i <= n; ++i) push(float(x + b->w), float(y) + float(b->h) * float(i) / n);
        for (int i = 1; i <= n; ++i) push(float(x + b->w) - float(b->w) * float(i) / n, float(y + b->h));
        for (int i = 1; i <= n; ++i) push(float(x), float(y + b->h) - float(b->h) * float(i) / n);
        if (pts.size() > 1) dl->AddPolyline(pts.data(), int(pts.size()), col, 0, thick);
    };
    if (!worldSelected_.empty()) outline(worldSelected_, theme::col(theme::Accent), S(2.5f));
    if (!worldHover_.empty() && worldHover_ != worldSelected_) outline(worldHover_, theme::col(theme::Text), S(1.5f));

    // labels and status
    ImGui::PushFont(fontSmall_);
    char t[220];
    std::snprintf(t, sizeof t, "%zu / %zu maps  |  RMB + WASD fly (wheel = speed), LMB drag turn, wheel zoom, F overview  |  click selects, double-click edits",
                  worldTiles_.size(), worldTileTotal_);
    dl->AddText(ImVec2(origin.x + S(12), origin.y + S(10)), theme::col(theme::Muted), t);
    if (!worldHover_.empty()) {
        const auto* b = world_.find(worldHover_);
        std::snprintf(t, sizeof t, "%s  (%s)", worldHover_.c_str(), b && !b->region.empty() ? b->region.c_str() : "no region");
        dl->AddText(ImVec2(io.MousePos.x + S(14), io.MousePos.y + S(4)), theme::col(theme::Text), t);
    }
    ImGui::PopFont();
    drawToasts(origin, size);
}

void App::openFromWorld3D(const std::string& name) {
    int x = 0, y = 0;
    worldPlacement(name, x, y);
    // the world camera in the map's own frame (render x = Fable x, render z = -Fable y)
    Camera c = worldCamera_;
    c.posX -= float(x);
    c.posZ += float(y);
    // keep the view only when it is looking at this map; otherwise the map is framed as usual
    float focus[3]; worldCamera_.focus(focus);
    const auto* box = world_.find(name);
    const bool looking = box && focus[0] >= float(x) && focus[0] <= float(x + box->w) && -focus[2] >= float(y) && -focus[2] <= float(y + box->h);
    worldCaptured_ = false;
    setWorldMode(false);
    selectMap(name);
    if (looking) {
        camera_ = c;
        lastFramedFor_ = name;   // the preview upload keeps this camera
    }
    setEditMode(true);
    pushLog("world: opened " + name + " for editing", 0);
}

} // namespace albion::gui
