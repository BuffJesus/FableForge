// The whole-world overview (vanilla FableWin's 2D world map and 3D engine view over every
// map, Aeon's review item 12): every map of the world as a low-res textured tile at its WLD
// origin, built on worker threads and cached on disk. The World tab draws the tiles inside
// its 2D boxes and, in 3D, flies over all of them; double-click a map to edit it.
#include "app.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <thread>

#include "forge/lev.hpp"
#include "forge/levelstore.hpp"
#include "theme.hpp"

namespace albion::gui {

namespace fs = std::filesystem;
namespace te = albion::terrainexport;

namespace {

fs::path tileCacheDir() {
    const char* base = std::getenv("LOCALAPPDATA");
    if (!base) base = std::getenv("TEMP");
    return fs::path(base ? base : ".") / "FableForge" / "worldtiles";
}

// where a map's .lev comes from, so the cache knows when it changed
std::string sourceKey(const forge::levelstore::Layout& layout, const std::string& name, bool textured) {
    std::error_code ec;
    fs::path src = forge::levelstore::loosePath(layout, name + ".lev");
    if (!fs::is_regular_file(src, ec)) src = layout.wad;
    const auto size = fs::file_size(src, ec);
    const auto time = fs::last_write_time(src, ec).time_since_epoch().count();
    char k[64]; std::snprintf(k, sizeof k, "|%llu|%lld|%s", (unsigned long long)size, (long long)time, textured ? "t" : "h");
    return src.string() + k;
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
    const auto cancel = worldTileCancel_;
    const unsigned threads = std::clamp(std::thread::hardware_concurrency() / 2u, 1u, 6u);
    worldTileStarted_ = std::chrono::steady_clock::now();
    for (unsigned w = 0; w < threads; ++w) {
        worldTileWorkers_.push_back(std::async(std::launch::async, [this, names, next, ctxHold, layout, root, cache, gain, cancel, w]() {
            const fs::path tmpDir = fs::temp_directory_path() / "FableForge" / "worldtiles";
            std::error_code ec;
            fs::create_directories(tmpDir, ec);
            for (;;) {
                if (cancel->load()) return;
                const size_t i = next->fetch_add(1);
                if (i >= names->size()) return;
                const std::string& name = (*names)[i];
                worldtiles::Tile tile;
                const std::string key = sourceKey(*layout, name, true);
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
    if (!worldMode_) return;
    if (!worldLoaded_ && installValid_ && !worldFuture_.valid() && worldLoadedFrom_ != saveRoot() + "|" + packDest_) loadWorld();   // the 3D view has no 2D canvas to trigger it
    if (worldTilesFor_.empty() || worldTilesFor_ != installPath_) startWorldTiles();
    std::vector<worldtiles::Tile> done;
    { std::lock_guard<std::mutex> lock(worldTileMutex_); done.swap(worldTileDone_); }
    for (auto& t : done) {
        worldTileTex_[t.name] = renderer_.uiTexture("worldtile:" + t.name, t.ground);
        const std::string name = t.name;
        worldTiles_[name] = std::move(t);
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
    for (const auto& [name, at] : worldLayerAt_) {
        int x, y;
        if (worldPlacement(name, x, y) && (x != at.first || y != at.second)) { moved = true; break; }
    }
    if (moved) { renderer_.clearLayer(Renderer::kWorldLayer); worldLayerAt_.clear(); }
    foliageexport::Scene batch;
    for (const auto& [name, tile] : worldTiles_) {
        if (worldLayerAt_.count(name)) continue;
        int x, y;
        if (!worldPlacement(name, x, y)) continue;
        worldtiles::appendMesh(tile, batch, float(x), float(y));
        worldLayerAt_[name] = {x, y};
    }
    if (!batch.instances.empty()) renderer_.appendLayer(Renderer::kWorldLayer, batch, te::UpAxis::Y);   // Y = "convert the Fable Z-up tiles to render Y-up", as the neighbour layer does
}

// The ground under a world point (Fable x, y) from the tiles; `inside` = some map covers it.
float App::worldGroundAt(float wx, float wy, bool& inside, std::string* name) const {
    inside = false;
    const editor::WorldMapBox* best = nullptr;
    for (const auto& b : world_.maps) {
        int x, y;
        if (!worldPlacement(b.name, x, y)) continue;
        if (wx < float(x) || wy < float(y) || wx > float(x + b.w) || wy > float(y + b.h)) continue;
        if (!best || b.w * b.h < best->w * best->h) best = &b;   // the smallest box on top, as the 2D map picks
    }
    if (!best) return 0.0f;
    const auto t = worldTiles_.find(best->name);
    if (t == worldTiles_.end()) return 0.0f;
    int x, y; worldPlacement(best->name, x, y);
    inside = true;
    if (name) *name = best->name;
    return worldtiles::heightAt(t->second, wx - float(x), wy - float(y));
}

// March a render-space ray over the tiles: the first sample under the ground.
bool App::worldPickTile(const float o[3], const float d[3], std::string& name, float hit[3]) const {
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
    if (!on) worldLayerAt_.clear(), renderer_.clearLayer(Renderer::kWorldLayer);
}

void App::drawWorld3D(const ImVec2& origin, const ImVec2& size) {
    using theme::S;
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ID3D11ShaderResourceView* srv = renderer_.render(uint32_t(std::max(size.x, 8.0f)), uint32_t(std::max(size.y, 8.0f)), worldCamera_, mode_, time_);
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
