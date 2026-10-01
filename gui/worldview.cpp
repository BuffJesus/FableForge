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
#include <stdexcept>
#include <thread>
#include <tuple>

#include "forge/lev.hpp"
#include "forge/levelstore.hpp"
#include "theme.hpp"
#include "thingsexport.hpp"
#include "worlddemand.hpp"
#include "worldterrain.hpp"

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
    return sourceRevision(src) + dependencies + "|ground-grid-80";
}

} // namespace

void App::startWorldTiles() {
    if (modFilesBusy()) return;
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
        worldTileWorkers_.push_back(std::async(std::launch::async, [this, names, next, ctxHold, layout, root, cache, gain, dependencies, cancel]() {
            FORGE_THREAD("World overview worker");
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
                        albion::detail::TemporaryDirectory scratch("world-tile-");
                        const fs::path lev = scratch.path() / fs::path(name + ".lev").filename();
                        std::ofstream out;
                        out.exceptions(std::ios::failbit | std::ios::badbit);
                        out.open(lev, std::ios::binary);
                        out.write(reinterpret_cast<const char*>(bytes->data()), std::streamsize(bytes->size()));
                        out.close();
                        const auto file = forge::lev::File::open(lev);
                        tile = worldtiles::buildTile(file, name, ctxHold.get(), root, gain);
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
    worldOverviewUploadFrames_ = worldThumbnailUploadFrames_ = 0;
    worldOverviewFirstMap_.clear();
}

void App::pollWorldTiles() {
    FORGE_ZONE("World overview poll / upload");
    if (modFilesBusy()) return;
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
        if (++uploaded >= worldOverviewBatchLimit_ || std::chrono::steady_clock::now() - uploadStarted >= std::chrono::milliseconds(2)) break;
    }
    if (uploaded) ++worldThumbnailUploadFrames_;
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
    // Populate the camera's area first instead of waiting for unrelated maps
    // earlier in file order. Keep original indices as renderer tags.
    std::vector<std::tuple<float,int,size_t>> pending;
    if (worldLayerAt_.size() < world_.maps.size()) for (size_t i=0;i<world_.maps.size();++i) {
        const auto& b=world_.maps[i];
        if (worldLayerAt_.count(b.name) || !worldTiles_.count(b.name)) continue;
        const auto [x,y]=placement(b);
        const float dx=std::max({float(x)-worldCamera_.posX,0.0f,worldCamera_.posX-float(x+b.w)});
        const float dy=std::max({float(y)+worldCamera_.posZ,0.0f,-worldCamera_.posZ-float(y+b.h)});
        pending.emplace_back(dx*dx+dy*dy,b.w*b.h,i);
    }
    std::sort(pending.begin(),pending.end());
    for (const auto& [distance,area,i] : pending) {
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
        if (worldOverviewFirstMap_.empty()) worldOverviewFirstMap_=name;
        if (++appended >= worldOverviewBatchLimit_ || std::chrono::steady_clock::now() - layerStarted >= std::chrono::milliseconds(2)) break;
    }
    if (appended) ++worldOverviewUploadFrames_;
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

// Pick actual overview triangles, not the volume below a map's heightfield.
// Entering a map from the void below its edge must not create a phantom hit.
bool App::worldPickTile(const float o[3], const float d[3], std::string& name, float hit[3]) const {
    FORGE_ZONE("World ground ray");
    float nearest = 60000;
    int smallestArea = 0;
    name.clear();
    for (const auto& b : world_.maps) {
        if (!worldLayerAt_.count(b.name)) continue; // only published terrain is pickable
        const auto tile = worldTiles_.find(b.name);
        if (tile == worldTiles_.end()) continue;
        int x=b.x, y=b.y;
        for (const auto& move : worldPending_) if (move.name==b.name) { x=move.x; y=move.y; }
        const float local[] = {o[0]-float(x), -o[2]-float(y), o[1]};
        const float direction[] = {d[0],-d[2],d[1]};
        float candidate=nearest;
        if (worldtiles::rayHit(tile->second,local,direction,candidate) &&
            (name.empty() || candidate<nearest || b.w*b.h<smallestArea)) {
            nearest=candidate; name=b.name; smallestArea=b.w*b.h;
        }
    }
    if (name.empty()) return false;
    for (int a=0;a<3;++a) hit[a]=o[a]+d[a]*nearest;
    return true;
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
    worldScenery_.clear(renderer_);
    if (worldDetailWork_) worldDetailWork_->cancel = true;
    worldDetailRetries_.clear(); worldDetailDeferred_ = 0;
    worldDetailLastObjects_ = worldDetailLastCreatures_ = 0;
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
void App::failWorldDetail(const std::string& name, const std::string& reason) {
    const unsigned delay = worldDetailRetries_.fail(name, time_);
    ++worldDetailFailures_;
    pushLog("world: " + name + " detail unavailable: " + reason + "; keeping overview, retry in " + std::to_string(delay) + "s", 1);
}

void App::updateWorldDetail() {
    FORGE_ZONE("World detail demand / upload");
    if (worldDetailRetiring_ && !releaseWorldDetail()) return;
    auto retireDisabledWorker = [&] {
        if (worldDetailWork_) worldDetailWork_->cancel = true;
        if (worldDetailFuture_.valid() && worldDetailRelease_.idle() &&
            worldDetailFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            worldDetailUpload_ = worldDetailFuture_.get();
            worldDetailCancelled_ += worldDetailUpload_->cancelled;
            worldCutoutCacheStats_ = worldDetailUpload_->cutoutCacheStats;
            releaseWorldDetail();
            worldDetailLoading_.clear();
        }
    };
    if (modFilesBusy() || !worldMode_ || !world3D_ || !ctx_.ready()) {
        if (!worldDetailShown_.empty() || worldDetailCache_.size() || worldDetailUpload_) clearWorldDetail();
        retireDisabledWorker(); return;
    }
    if (time_ >= worldVideoMemoryNext_) {
        worldVideoMemoryNext_ = time_ + 1.0;
        worldVideoMemory_ = worldVideoMemoryOverride_ ? *worldVideoMemoryOverride_ : renderer_.queryVideoMemory();
        renderer_.sampleWorldAaMemory(worldVideoMemory_);
        std::vector<int> inactiveTags;
        for (const auto& name : worldDetailCache_.names()) inactiveTags.push_back(worldTag(name));
        const size_t inactiveBytes = renderer_.layerSetBytes(Renderer::kWorldDetailLayer, inactiveTags, true);
        const size_t limit = worldCacheMemoryBudget_.observe(worldVideoMemory_.valid,
            worldVideoMemory_.budget, worldVideoMemory_.usage, inactiveBytes);
        { FORGE_ZONE("World inactive cache trim");
            for (const auto& name : worldDetailCache_.setBudget(limit)) {
                renderer_.removeLayerTag(Renderer::kWorldDetailLayer, worldTag(name));
                ++worldMemoryEvictions_;
            }
        }
        std::vector<int> activeTags;
        for (const auto& [name, state] : worldDetailShown_) {
            const auto bytes = renderer_.layerTagBytes(Renderer::kWorldDetailLayer, worldTag(name));
            activeTags.push_back(worldTag(name));
            worldDetailLargestMap_ = std::max<uint64_t>(worldDetailLargestMap_, bytes);
        }
        const size_t activeBytes = renderer_.layerSetBytes(Renderer::kWorldDetailLayer, activeTags, true);
        worldDetailMemoryMaps_ = worldview::memoryMapCeiling(worldVideoMemory_.valid,
            worldVideoMemory_.budget, worldVideoMemory_.usage, activeBytes,
            worldDetailLargestMap_, worldDetailAutoMaps_);
        FORGE_PLOT("GPU process memory budget", worldVideoMemory_.budget);
        FORGE_PLOT("GPU process memory usage", worldVideoMemory_.usage);
        FORGE_PLOT("World inactive memory allowance", limit);
    }
    if (!worldDetailOn_) {
        if (!worldDetailShown_.empty() || worldDetailCache_.size() || worldDetailUpload_) clearWorldDetail();
        retireDisabledWorker(); return;
    }
    worldDetailBudget_.observe(ImGui::GetIO().DeltaTime, std::min(worldDetailAutoMaps_, worldDetailMemoryMaps_),
        worldAutoDetail_ && !ImGui::GetIO().AppFocusLost && !worldDetailFuture_.valid() && !worldDetailUpload_ && worldDetailWanting_ == 0 && worldScenery_.settled);
    for (auto& [name, state] : worldDetailShown_) {
        // Keep outgoing detail until its visible coarse replacement is ready.
        // Memory pressure and out-of-range demand explicitly release this hold.
        const bool holdFallback = !state.wanted && worldScenery_.awaitingFallback(name);
        worldSceneryHolds_ += holdFallback;
        if (!holdFallback) state.fade = std::clamp(state.fade + (state.wanted ? 1.0f : -1.0f) * std::min(ImGui::GetIO().DeltaTime, 0.05f) / (worldSmoothObjects_ ? worldview::DetailFade::seconds : 0.25f), 0.0f, 1.0f);
        const int tag = worldTag(name);
        const float progress = worldDetailFadeOverride_.value_or(state.fade);
        renderer_.setLayerTagFade(Renderer::kWorldDetailLayer, tag,
            worldSmoothObjects_ ? worldview::DetailFade::terrain(progress) : progress, false,
            worldSmoothObjects_ ? worldview::DetailFade::objects(progress) : progress);
        renderer_.setLayerTagVisible(Renderer::kWorldLayer, tag, false);
    }
    if (worldDetailUpload_) {
        FORGE_ZONE("World paced upload");
        auto& detail = *worldDetailUpload_;
        const int tag = worldTag(detail.name);
        const auto started = std::chrono::steady_clock::now();
        const auto slice = std::chrono::milliseconds(worldDetailUploadBudgetMs_ ? worldDetailUploadBudgetMs_ :
            worldview::detailUploadMilliseconds(ImGui::GetIO().DeltaTime));
        ++worldDetailUploadFrames_;
        bool ok = detail.generation == worldDetailGeneration_ && tag >= 0;
        // A single driver allocation cannot be interrupted. Bound the work between
        // allocations, and keep every batch hidden until the whole map is ready.
        // Fast hardware may fit many small batches inside the same time budget.
        // Keep a finite count guard, but let elapsed time normally set the limit.
        for (int count = 0; ok && worldDetailUploadAt_ < detail.batches.size() && count < 32; ++count) {
            const auto& batch = detail.batches[worldDetailUploadAt_++];
            const auto mip = detail.cutoutMips.find(batch.image);
            ok = renderer_.appendPreparedBatch(Renderer::kWorldDetailLayer,
                batch, detail.images, tag, false, mip == detail.cutoutMips.end() ? nullptr : mip->second.get());
            if (detail.name == worldDetailFailUpload_) ok = false;
            if (std::chrono::steady_clock::now() - started >= slice) break;
        }
        if (!ok || worldDetailUploadAt_ == detail.batches.size()) {
            if (ok && std::any_of(detail.batches.begin(), detail.batches.end(), [](const auto& batch) { return batch.terrainMorph; })) {
                renderer_.setLayerTagVisible(Renderer::kWorldDetailLayer, tag, true);
                renderer_.setLayerTagFade(Renderer::kWorldDetailLayer, tag, 0.0f);
                renderer_.setLayerTagVisible(Renderer::kWorldLayer, tag, false);
                worldDetailShown_[detail.name] = {};
                worldDetailLastObjects_ = detail.objects;
                worldDetailLastCreatures_ = detail.creatures;
                worldDetailRetries_.success(detail.name);
            } else {
                renderer_.removeLayerTag(Renderer::kWorldDetailLayer, tag);
                if (detail.generation == worldDetailGeneration_ && tag >= 0)
                    failWorldDetail(detail.name, "terrain or GPU upload failed");
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
    // Candidate distance follows the eye, never the centre-ray ground hit.
    // A bounded view preference schedules useful maps across level boundaries;
    // nearby residency and distance hysteresis remain independent of the cone.
    const float gx = worldCamera_.posX, gy = -worldCamera_.posZ;
    const int mapLimit = worldAutoDetail_ ? worldDetailBudget_.maps : worldDetailMaps_;
    const float loadRadius = worldview::sceneryLoadRadius(worldDetailRadius_);
    renderer_.worldObjectDistance = worldview::requestedDrawDistance(worldDetailRadius_);
    float eye[3], direction[3]; worldCamera_.eye(eye); worldCamera_.dir(direction);
    const float aspect = viewportSize_.y > 0 ? std::max(1.0f, viewportSize_.x / viewportSize_.y) : 2.0f;
    // too high up to see detail: none (the camera's height over the ground stands in for zoom)
    bool inside = false;
    std::string underfoot;
    const float ground = worldGroundAt(worldCamera_.posX, -worldCamera_.posZ, inside, &underfoot);
    const float height = worldCamera_.posY - (inside ? ground : 0.0f);
    std::vector<std::pair<float, std::string>> want;
    if (height < loadRadius * 2.75f)
        for (const auto& b : world_.maps) {
            int x = b.x, y = b.y;
            for (const auto& mv : worldPending_) if (mv.name == b.name) { x = mv.x; y = mv.y; }
            const float dx = std::max({float(x) - gx, 0.0f, gx - float(x + b.w)});
            const float dy = std::max({float(y) - gy, 0.0f, gy - float(y + b.h)});
            const float d = std::sqrt(dx * dx + dy * dy);
            const bool resident = worldDetailShown_.count(b.name) != 0;
            // Retain nearby residents across a small camera movement instead of
            // repeatedly rebuilding maps at the distance/count boundary.
            if (height < loadRadius * (resident ? 2.75f : 2.5f) && d <= loadRadius * (resident ? 1.15f : 1.0f)) {
                // Bounds are available before loading detailed meshes. Expand for
                // buildings/trees outside the ground footprint; this only affects
                // priority, so unknown or oversized geometry is never excluded.
                float viewWeight = 1;
                if (const auto tile = worldTiles_.find(b.name); tile != worldTiles_.end()) {
                    const float lo[] = {float(x) - 32, tile->second.minH - 32, -float(y + b.h) - 32};
                    const float hi[] = {float(x + b.w) + 32, tile->second.maxH + 32, -float(y) + 32};
                    viewWeight = worldview::demandViewWeight(eye, direction, worldCamera_.fovY, aspect, lo, hi);
                }
                want.push_back({worldview::demandPriority(d, loadRadius, resident, inside && b.name == underfoot, viewWeight), b.name});
            }
        }
    std::sort(want.begin(), want.end());
    FORGE_PLOT("World detail budget", mapLimit);
    FORGE_PLOT("World resident maps", worldDetailShown_.size());
    if (int(want.size()) > mapLimit) want.resize(size_t(mapLimit));
    if (worldDetailFuture_.valid() && worldDetailWork_ &&
        std::none_of(want.begin(), want.end(), [&](const auto& w) { return w.second == worldDetailLoading_; }))
        worldDetailWork_->cancel = true;
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
        worldDetailCancelled_ += detail.cancelled;
        worldCutoutCacheStats_ = detail.cutoutCacheStats;
        const bool wanted = std::any_of(want.begin(), want.end(), [&](const auto& w) { return w.second == detail.name; });
        const int tag = worldTag(detail.name);
        const bool current = !detail.cancelled && wanted && detail.generation == worldDetailGeneration_ && tag >= 0 && !worldDetailShown_.count(detail.name);
        if (current && detail.error.empty()) {
            worldDetailUpload_ = std::move(detail);
            worldDetailUploadAt_ = 0;
        } else {
            if (current && !detail.error.empty()) failWorldDetail(detail.name, detail.error);
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
    worldDetailDeferred_ = 0;
    FORGE_PLOT("World inactive detail bytes", worldDetailCache_.bytes());
    FORGE_PLOT("World detail cache hits", worldDetailCacheHits_);
    for (const auto& w : want) if (!worldDetailShown_.count(w.second)) {
        if (worldDetailRetries_.ready(w.second, time_)) ++worldDetailWanting_;
        else ++worldDetailDeferred_;
    }
    // start the nearest missing one
    if (worldDetailFuture_.valid() || worldDetailUpload_) return;
    // Bound retained CPU data: finish the previous payload before preparing
    // another map. Camera/input/rendering continue while the worker frees it.
    if (!worldDetailRelease_.idle()) { worldDetailNext_ = 0; return; }
    for (const auto& [d, name] : want) {
        if (worldDetailShown_.count(name)) continue;
        if (!worldDetailRetries_.ready(name, time_)) continue;
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
        LevWorkspace scratch;
        const std::string lev = resolveLevPath(*e, scratch, err);
        if (lev.empty()) { failWorldDetail(name, err.empty() ? "level path unavailable" : err); continue; }
        int x, y; worldPlacement(name, x, y);
        const auto ctxHold = std::make_shared<const te::Context>(ctx_);
        const fs::path root = installPath_;
        const float gain = settings_.gain;
        const bool creatures = worldDetailCreatures_, things = worldDetailThings_, plants = worldDetailFoliage_;
        const bool cutoutMips = renderer_.worldCutoutMips;
        const auto cutoutCache = worldCutoutCache_;
        const bool reuseCutoutMips = worldCutoutCacheOn_;
        const uint64_t generation = worldDetailGeneration_;
        const bool failPrepare = name == worldDetailFailPrepare_;
        const auto work = std::make_shared<WorldDetailWork>();
        work->hold = name == worldDetailHoldPrepare_;
        worldDetailWork_ = work;
        worldDetailLoading_ = name;
        ++worldDetailLoads_;
        worldDetailFuture_ = std::async(std::launch::async, [ctxHold, scratch, root, gain, lev, name, x, y, creatures, things, plants, generation, cutoutMips, cutoutCache, reuseCutoutMips, failPrepare, work, overview = std::move(overview)]() {
            FORGE_THREAD("World detail worker");
            FORGE_ZONE("World detail prepare");
            FORGE_ZONE_TEXT(name);
            WorldDetail r; r.name = name; r.generation = generation;
            auto cancelled = [&] {
                if (!work->cancel.load()) return false;
                r.cancelled = true; r.cutoutCacheStats = cutoutCache->stats();
                return true;
            };
            if (!reuseCutoutMips || !cutoutMips) cutoutCache->clear();
            try {
                if (cancelled()) return r;
                if (failPrepare) throw std::runtime_error("injected preparation failure");
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
                } catch (const std::exception& e) { r.error = e.what(); r.cutoutCacheStats = cutoutCache->stats(); return r; }
                if (work->hold.load()) {
                    work->held = true;
                    while (work->hold.load() && !work->cancel.load())
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    work->held = false;
                }
                if (cancelled()) return r;
                if (plants) try {
                    foliageexport::Options fo;
                    fo.gameRoot = root; fo.textures = true; fo.up = te::UpAxis::Y;
                    fo.mapLocal = false;   // STB instances are world placed already
                    foliage = foliageexport::load(name, fo, *ctx);
                } catch (const std::exception&) {}
                if (cancelled()) return r;
                if (things || creatures) {
                    try {
                        thingsexport::Options to;
                        to.gameRoot = root; to.textures = true; to.up = te::UpAxis::Y;
                        to.creatures = creatures;
                        to.objects = things;
                        to.originX = float(x); to.originY = float(y);
                        thingsexport::Stats stats;
                        placed = thingsexport::load(name, to, *ctx, &stats);
                        r.objects = stats.rootObjectsPlaced;
                        r.creatures = stats.rootCreaturesPlaced;
                    } catch (const std::exception&) {}
                }
                if (cancelled()) return r;
                auto prepare = [&](foliageexport::Scene& scene, bool morph = false) {
                    if (!morph) foliageexport::loadWorldLods(scene, *ctx);
                    auto batches = Renderer::prepareLayer(scene, te::UpAxis::Y, !morph);
                    const int offset = int(r.images.size());
                    for (auto& batch : batches) {
                        batch.terrainMorph = morph;
                        if (morph) for (auto& v : batch.vertices) {
                            v.walk = worldtiles::meshHeightAt(overview, v.px - float(x), -v.pz - float(y));
                            const auto n = worldtiles::meshNormalAt(overview, v.px - float(x), -v.pz - float(y));
                            v.coarseNormal = Renderer::packNormal(n[0], n[2], -n[1]);
                            batch.lo[1] = std::min(batch.lo[1], v.walk);
                            batch.hi[1] = std::max(batch.hi[1], v.walk);
                        }
                        if (morph) {
                            auto lods = prepareWorldTerrainLods(batch);
                            if (!lods.patches.empty()) {
                                batch.indices = std::move(lods.indices);
                                batch.terrainPatches = std::move(lods.patches);
                            }
                        }
                        if (batch.image >= 0) batch.image += offset;
                        r.batches.push_back(std::move(batch));
                    }
                    for (auto& image : scene.images) r.images.push_back(std::move(image));
                };
                prepare(ground, true);
                if (cancelled()) return r;
                prepare(foliage);
                if (cancelled()) return r;
                prepare(placed);
                if (cancelled()) return r;
                if (cutoutMips) {
                    FORGE_ZONE("Cutout mip preparation (worker)");
                    for (const auto& batch : r.batches) {
                        if (cancelled()) return r;
                        if (batch.alpha && batch.image >= 0 && size_t(batch.image) < r.images.size() && !r.cutoutMips.count(batch.image))
                            r.cutoutMips.emplace(batch.image, cutoutCache->acquire(r.images[size_t(batch.image)], reuseCutoutMips));
                    }
                }
                size_t geometryBytes = 0, expandedBytes = 0;
                for (const auto& batch : r.batches) {
                    geometryBytes += batch.vertices.size() * sizeof(Renderer::LayerVertex) + batch.indices.size() * sizeof(uint32_t);
                    expandedBytes += (batch.indices.empty() ? batch.vertices.size() : batch.indices.size()) * sizeof(Renderer::LayerVertex);
                }
                FORGE_PLOT("World prepared geometry bytes", geometryBytes);
                FORGE_PLOT("World expanded equivalent bytes", expandedBytes);
            } catch (const std::exception& e) { r.error = e.what(); }
            r.cutoutCacheStats = cutoutCache->stats();
            FORGE_PLOT("Cutout cache retained CPU bytes", r.cutoutCacheStats.bytes);
            FORGE_PLOT("Cutout cache hits", r.cutoutCacheStats.hits);
            FORGE_PLOT("Cutout cache builds", r.cutoutCacheStats.builds);
            return r;
        });
        break;
    }
}

void App::updateWorldScenery() {
    const bool enabled = !modFilesBusy() && worldSceneryOn_ && worldMode_ && world3D_ && worldDetailOn_ && ctx_.ready();
    renderer_.worldObjectDistance = worldview::requestedDrawDistance(worldDetailRadius_);
    std::vector<WorldScenery::Demand> demand;
    if (enabled) {
        float eye[3], direction[3]; worldCamera_.eye(eye); worldCamera_.dir(direction);
        const float aspect = viewportSize_.y > 0 ? std::max(0.1f,viewportSize_.x/viewportSize_.y) : 2.0f;
        for (const auto& map : world_.maps) {
            const auto tile = worldTiles_.find(map.name);
            if (tile==worldTiles_.end() || !worldLayerAt_.count(map.name)) continue;
            int x=map.x,y=map.y; worldPlacement(map.name,x,y);
            constexpr float pad=worldview::kSceneryBoundsPadding;
            const float lo[]={float(x)-pad,tile->second.minH-pad,-float(y+map.h)-pad};
            const float hi[]={float(x+map.w)+pad,tile->second.maxH+pad,-float(y)+pad};
            const bool resident=std::find(worldScenery_.names.begin(),worldScenery_.names.end(),map.name)!=worldScenery_.names.end();
            const float distance=worldview::boxDistance(eye,lo,hi);
            if (!worldview::sceneryInRange(distance,worldDetailRadius_,resident)) continue;
            const float view=worldview::demandViewWeight(eye,direction,worldCamera_.fovY,aspect,lo,hi);
            // Visible and soon-to-be-visible maps are independent of near-map slots.
            // A small surrounding ring and padded cone prevent turn-edge churn.
            if (view<=0 && distance>100) continue;
            float fullCoverage=0;
            bool fallbackNeeded=false;
            if (const auto detailed=worldDetailShown_.find(map.name);detailed!=worldDetailShown_.end()) {
                const float fade=worldDetailFadeOverride_.value_or(detailed->second.fade);
                fullCoverage=worldSmoothObjects_ ? worldview::DetailFade::objects(fade) : fade;
                fallbackNeeded=!detailed->second.wanted;
            }
            demand.push_back({map.name,worldTag(map.name),x,y,distance-(view*64)-(resident?32:0)-(fallbackNeeded?10000:0),fullCoverage,fallbackNeeded});
        }
        std::sort(demand.begin(),demand.end(),[](const auto& a,const auto& b) { return a.priority!=b.priority ? a.priority<b.priority : a.name<b.name; });
    }
    worldScenery_.update(renderer_,ctx_,installPath_,demand,ImGui::GetIO().DeltaTime,enabled,
        worldDetailFoliage_,worldDetailThings_,worldDetailCreatures_,worldVideoMemory_,true);
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
    if (!on) {
        worldLayerAt_.clear(); worldOverviewUploadFrames_ = 0;
        worldOverviewFirstMap_.clear();
        renderer_.clearLayer(Renderer::kWorldLayer); clearWorldDetail();
    }
}

void App::drawWorld3D(const ImVec2& origin, const ImVec2& size) {
    using theme::S;
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool overGround = false;
    const float below = worldGroundAt(worldCamera_.posX, -worldCamera_.posZ, overGround, nullptr);
    const float nearPlane = worldview::nearPlane(worldCamera_.posY - (overGround ? below : 0.0f));
    renderer_.observeWorldAa(ImGui::GetIO().DeltaTime, !ImGui::GetIO().AppFocusLost && !worldDetailFuture_.valid() && !worldDetailUpload_ && worldDetailWanting_ == 0 && worldScenery_.settled);
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
    ImGui::PopFont();
    drawWorldLabel(origin, size);
    drawCompass(origin, size, worldCamera_.yaw, 0);
    drawToasts(origin, size);
}

void App::drawWorldLabel(const ImVec2& origin, const ImVec2& size) {
    using theme::S;
    const auto& name = worldHover_.empty() ? worldSelected_ : worldHover_;
    const auto* box = world_.find(name);
    if (!box || size.x < S(48) || size.y < S(80)) return;
    ImGui::PushFont(fontSmall_);
    const auto text = theme::fitText(name + "  |  " + (box->region.empty() ? "no region" : box->region), size.x - S(48));
    const ImVec2 extent = ImGui::CalcTextSize(text.c_str());
    const ImVec2 at(origin.x + (size.x-extent.x)*0.5f, origin.y + S(42));
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, ImVec2(origin.x+size.x,origin.y+size.y),true);
    draw->AddRectFilled(ImVec2(at.x-S(10),at.y-S(6)),ImVec2(at.x+extent.x+S(10),at.y+extent.y+S(6)),theme::col(theme::Bg0),S(5));
    draw->AddText(at,theme::col(theme::Text),text.c_str());
    draw->PopClipRect();
    ImGui::PopFont();
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
