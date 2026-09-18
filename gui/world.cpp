// Overworld tab: every map of the world as a box on a 2D grid, coloured by
// region, dragged to a new 32-aligned origin (overlaps refused, touching
// neighbours highlighted). Pending moves are applied to the install in one
// go: WLD MapX/MapY, BWD boxes (all three copies) and the STB chunks
// translated to their new origins (src/overworld + src/stbrelocate).
#include "app.hpp"
#include "stitch.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <future>

#include "theme.hpp"

namespace albion::gui {

namespace {

// a stable colour per region: golden-angle hue walk over the region index
ImU32 regionColour(int slot, float alpha) {
    const float h = std::fmod(0.61803398875f * float(std::max(slot, 0)), 1.0f);
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(h, 0.55f, 0.85f, r, g, b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, alpha));
}

int snap32(float v) { return int(std::floor(v / 32.0f + 0.5f)) * 32; }

} // namespace

void App::setWorldMode(bool on) {
    if (on && editMode_) setEditMode(false);
    worldMode_ = on;
    if (on) loadWorld();
}

void App::loadWorld() {
    const std::string root = saveRoot();
    if (worldLoaded_ && worldLoadedFrom_ == root) return;
    std::string err;
    worldLoaded_ = editor::loadWorldLayout(root, world_, err);
    worldLoadedFrom_ = root;
    worldPending_.clear();
    worldOwnerEdits_.clear();
    worldSeesEdits_.clear();
    worldSelected_.clear();
    worldZoom_ = 0;
    if (!worldLoaded_) pushLog("world: " + err, 2);
    else pushLog("world: " + std::to_string(world_.maps.size()) + " maps in " + std::to_string(world_.regions.size()) + " regions", 0);
}

bool App::worldPlacement(const std::string& map, int& x, int& y) const {
    const editor::WorldMapBox* box = world_.find(map);
    if (!box) return false;
    x = box->x; y = box->y;
    for (const auto& mv : worldPending_) if (mv.name == box->name) { x = mv.x; y = mv.y; }
    return true;
}

void App::worldSelect(const std::string& map) {
    const editor::WorldMapBox* box = world_.find(map);
    worldSelected_ = box ? box->name : "";
    if (box) { worldPlacement(box->name, worldEditX_, worldEditY_); worldEditFor_ = box->name; }
}

bool App::worldMove(const std::string& map, int x, int y) {
    const editor::WorldMapBox* box = world_.find(map);
    if (!box) { pushLog("world: no map " + map, 2); return false; }
    // validate against the layout WITH the other pending moves applied
    editor::WorldLayout current = world_;
    for (auto& b : current.maps)
        for (const auto& mv : worldPending_) if (mv.name == b.name) { b.x = mv.x; b.y = mv.y; }
    std::vector<editor::MapMove> others;
    for (const auto& mv : worldPending_) if (mv.name != box->name) others.push_back(mv);
    editor::MapMove mv{box->name, x, y};
    std::string why;
    if (!editor::checkMove(current, others, mv, why)) { pushLog("world: " + box->name + " -> (" + std::to_string(x) + "," + std::to_string(y) + ") refused: " + why, 1); return false; }
    auto it = std::find_if(worldPending_.begin(), worldPending_.end(), [&](const editor::MapMove& m) { return m.name == box->name; });
    if (it == worldPending_.end() ? (x == box->x && y == box->y) : (it->x == x && it->y == y)) return true;   // nothing changes
    worldPushUndo();
    if (x == box->x && y == box->y) { if (it != worldPending_.end()) worldPending_.erase(it); }
    else if (it != worldPending_.end()) { it->x = x; it->y = y; }
    else worldPending_.push_back(mv);
    if (worldEditFor_ == box->name) { worldEditX_ = x; worldEditY_ = y; }
    return true;
}

std::string App::worldOwnerOf(const std::string& map) const {
    for (const auto& e : worldOwnerEdits_) if (e.map == map) return e.region;
    const auto* b = world_.find(map);
    return b ? b->region : std::string();
}

bool App::worldSees(const std::string& region, const std::string& map) const {
    for (const auto& e : worldSeesEdits_) if (e.region == region && e.map == map) return e.sees;
    const auto* r = world_.region(region);
    return r && r->sees_(map);
}

bool App::worldSetOwner(const std::string& map, const std::string& region) {
    const auto* b = world_.find(map);
    if (!b) { pushLog("world: no map " + map, 2); return false; }
    if (!world_.region(region)) { pushLog("world: no region " + region, 2); return false; }
    if (worldOwnerOf(b->name) == region) return true;
    worldPushUndo();
    worldOwnerEdits_.erase(std::remove_if(worldOwnerEdits_.begin(), worldOwnerEdits_.end(), [&](const editor::OwnerEdit& e) { return e.map == b->name; }), worldOwnerEdits_.end());
    if (region != b->region) worldOwnerEdits_.push_back({b->name, region});
    return true;
}

bool App::worldSetSees(const std::string& region, const std::string& map, bool sees) {
    const auto* b = world_.find(map);
    const auto* r = world_.region(region);
    if (!b || !r) { pushLog("world: no such map/region " + region + " / " + map, 2); return false; }
    if (worldSees(r->name, b->name) == sees) return true;
    worldPushUndo();
    worldSeesEdits_.erase(std::remove_if(worldSeesEdits_.begin(), worldSeesEdits_.end(), [&](const editor::SeesEdit& e) { return e.region == r->name && e.map == b->name; }), worldSeesEdits_.end());
    if (sees != r->sees_(b->name)) worldSeesEdits_.push_back({r->name, b->name, sees});
    return true;
}

void App::worldRevert() {
    if (worldPendingCount() == 0) return;
    worldPushUndo();
    worldPending_.clear();
    worldOwnerEdits_.clear();
    worldSeesEdits_.clear();
    if (!worldSelected_.empty()) worldSelect(worldSelected_);
}

void App::worldPushUndo() {
    worldUndo_.push_back(worldSnapshot());
    if (worldUndo_.size() > 128) worldUndo_.erase(worldUndo_.begin());
    worldRedo_.clear();
}

void App::worldRestore(const WorldSnap& s) {
    worldPending_ = s.moves;
    worldOwnerEdits_ = s.owners;
    worldSeesEdits_ = s.sees;
    if (!worldSelected_.empty()) worldSelect(worldSelected_);
}

bool App::worldUndo() {
    if (worldUndo_.empty() || worldFuture_.valid()) return false;
    worldRedo_.push_back(worldSnapshot());
    worldRestore(worldUndo_.back());
    worldUndo_.pop_back();
    return true;
}

bool App::worldRedo() {
    if (worldRedo_.empty() || worldFuture_.valid()) return false;
    worldUndo_.push_back(worldSnapshot());
    worldRestore(worldRedo_.back());
    worldRedo_.pop_back();
    return true;
}

void App::worldApply() {
    if (worldPendingCount() == 0 || worldFuture_.valid()) return;
    const std::string root = saveRoot();
    const std::vector<editor::MapMove> moves = worldPending_;
    const std::vector<editor::OwnerEdit> owners = worldOwnerEdits_;
    const std::vector<editor::SeesEdit> sees = worldSeesEdits_;
    const bool stitch = worldStitch_ && !moves.empty();
    const int feather = worldStitchFeather_;
    worldUndo_.clear(); worldRedo_.clear();
    pushLog("world: " + std::to_string(moves.size()) + " move(s), " + std::to_string(owners.size()) + " owner change(s), " + std::to_string(sees.size()) + " visibility change(s): writing the WLD/BWD" + (moves.empty() ? "" : " and translating terrain chunks in FinalAlbion_RT.stb") + (stitch ? ", then stitching seams" : "") + "...", 0);
    worldFuture_ = std::async(std::launch::async, [root, moves, owners, sees, stitch, feather]() {
        WorldJob r;
        r.ok = editor::applyWorldEdits(root, moves, owners, sees, r.notes, r.error);
        if (r.ok && stitch) {
            // every moved map's seams at its new placement: shared-edge heights
            // averaged into both LEVs, both chunks re-baked
            editor::WorldLayout after;
            if (!editor::loadWorldLayout(root, after, r.error)) { r.ok = false; return r; }
            editor::StitchOptions so; so.feather = feather;
            std::vector<editor::StitchReport> reports;
            for (const auto& mv : moves) r.ok = editor::stitchNeighbours(root, after, mv.name, so, reports, r.notes, r.error) && r.ok;
            size_t done = 0; for (const auto& rep : reports) done += rep.stitched;
            r.notes.push_back(std::to_string(reports.size()) + " seam(s) checked, " + std::to_string(done) + " stitched");
        }
        return r;
    });
}

// ---------------------------------------------------------------- canvas

void App::drawWorldCanvas(const ImVec2& origin, const ImVec2& size) {
    using theme::S;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), theme::col(theme::Bg0));
    if (!worldLoaded_ && installValid_ && !worldFuture_.valid()) loadWorld();   // invalidated by a new level / an applied job
    if (!worldLoaded_) {
        ImGui::SetCursorScreenPos(ImVec2(origin.x + S(24), origin.y + S(24)));
        ImGui::TextColored(theme::vec(theme::Muted), "%s", installValid_ ? "The world could not be read (see the log)." : "Point Albion Atlas at a Fable install to see its world.");
        return;
    }
    // fit on first use: the whole world box inside the canvas with a margin
    const float worldW = float(std::max(1, world_.maxX - world_.minX)), worldH = float(std::max(1, world_.maxY - world_.minY));
    if (worldZoom_ <= 0) {
        worldZoom_ = std::min((size.x - S(40)) / worldW, (size.y - S(40)) / worldH);
        worldPanX_ = origin.x + (size.x - worldW * worldZoom_) * 0.5f - float(world_.minX) * worldZoom_;
        worldPanY_ = origin.y + (size.y - worldH * worldZoom_) * 0.5f - float(world_.minY) * worldZoom_;
    }
    // world (x right, y down) -> screen
    auto toScreen = [&](float wx, float wy) { return ImVec2(worldPanX_ + wx * worldZoom_, worldPanY_ + wy * worldZoom_); };
    auto toWorld = [&](const ImVec2& p) { return ImVec2((p.x - worldPanX_) / worldZoom_, (p.y - worldPanY_) / worldZoom_); };

    ImGuiIO& io = ImGui::GetIO();
    const bool hovered = viewportHovered_;
    if (hovered && !io.WantTextInput && (ImGui::IsKeyPressed(ImGuiKey_F) || ImGui::IsKeyPressed(ImGuiKey_Home))) { worldZoom_ = 0; return; }
    if (!io.WantTextInput && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) worldUndo();
    if (!io.WantTextInput && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) worldRedo();
    // arrow keys nudge the selected map by one grid step
    if (hovered && !io.WantTextInput && !worldSelected_.empty()) {
        int nx, ny;
        if (worldPlacement(worldSelected_, nx, ny)) {
            int dx = 0, dy = 0;
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) dx = -32; if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) dx = 32;
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) dy = -32; if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) dy = 32;
            if (dx || dy) worldMove(worldSelected_, nx + dx, ny + dy);
        }
    }
    // zoom about the cursor
    if (hovered && io.MouseWheel != 0) {
        const ImVec2 before = toWorld(io.MousePos);
        worldZoom_ *= io.MouseWheel > 0 ? 1.15f : 1.0f / 1.15f;
        worldZoom_ = std::clamp(worldZoom_, 0.02f, 40.0f);
        const ImVec2 after = toScreen(before.x, before.y);
        worldPanX_ += io.MousePos.x - after.x; worldPanY_ += io.MousePos.y - after.y;
    }
    // pan: right/middle drag, or left drag on empty ground
    if (hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle))) worldPanning_ = true;
    if (worldPanning_) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle)) { worldPanX_ += io.MouseDelta.x; worldPanY_ += io.MouseDelta.y; }
        else worldPanning_ = false;
    }

    // grid: 32-unit cells when zoomed in, 256 otherwise
    {
        const float step = worldZoom_ * 32 >= S(14) ? 32.0f : worldZoom_ * 256 >= S(14) ? 256.0f : 1024.0f;
        const ImVec2 w0 = toWorld(origin), w1 = toWorld(ImVec2(origin.x + size.x, origin.y + size.y));
        const ImU32 faint = theme::col(theme::Bg2), strong = theme::col(theme::Bg3);
        for (float x = std::floor(w0.x / step) * step; x <= w1.x; x += step) {
            const float sx = toScreen(x, 0).x;
            dl->AddLine(ImVec2(sx, origin.y), ImVec2(sx, origin.y + size.y), std::fmod(x, step * 4) == 0 ? strong : faint);
        }
        for (float y = std::floor(w0.y / step) * step; y <= w1.y; y += step) {
            const float sy = toScreen(0, y).y;
            dl->AddLine(ImVec2(origin.x, sy), ImVec2(origin.x + size.x, sy), std::fmod(y, step * 4) == 0 ? strong : faint);
        }
    }

    // the engine's placement grid (0,0)-(8192,8192): nothing can live outside it
    {
        const ImVec2 e0 = toScreen(0, 0), e1 = toScreen(float(editor::kWorldExtent), float(editor::kWorldExtent));
        dl->AddRect(e0, e1, theme::col(theme::Border), 0, 0, S(1.5f));
        ImGui::PushFont(fontSmall_);
        dl->AddText(ImVec2(e1.x - S(120), e1.y + S(4)), theme::col(theme::Muted), "engine world grid 8192");
        ImGui::PopFont();
    }
    // hit test (topmost = last drawn = smallest on top: sort by area descending)
    std::vector<const editor::WorldMapBox*> order;
    for (const auto& b : world_.maps) order.push_back(&b);
    std::sort(order.begin(), order.end(), [](const editor::WorldMapBox* a, const editor::WorldMapBox* b) { return a->w * a->h > b->w * b->h; });
    const ImVec2 mouseW = toWorld(io.MousePos);
    worldHover_.clear();
    if (hovered && !worldPanning_)
        for (const auto* b : order) {
            int x, y; worldPlacement(b->name, x, y);
            if (mouseW.x >= float(x) && mouseW.x < float(x + b->w) && mouseW.y >= float(y) && mouseW.y < float(y + b->h)) worldHover_ = b->name;
        }

    // drag a box
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !worldPanning_) {
        if (!worldHover_.empty()) {
            worldSelect(worldHover_);
            worldDragging_ = true;
            worldDragStart_ = io.MousePos;
            worldPlacement(worldSelected_, worldDragX_, worldDragY_);
            worldDragValid_ = true;
        } else worldSelected_.clear();
    }
    if (worldDragging_) {
        const editor::WorldMapBox* box = world_.find(worldSelected_);
        if (!box || !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            // release: commit when the box moved and the spot is legal
            if (box && worldDragValid_) {
                int x0, y0; worldPlacement(box->name, x0, y0);
                if (x0 != worldDragX_ || y0 != worldDragY_) worldMove(box->name, worldDragX_, worldDragY_);
            }
            worldDragging_ = false;
        } else {
            int x0, y0; worldPlacement(box->name, x0, y0);
            const ImVec2 d = ImVec2((io.MousePos.x - worldDragStart_.x) / worldZoom_, (io.MousePos.y - worldDragStart_.y) / worldZoom_);
            worldDragX_ = snap32(float(x0) + d.x); worldDragY_ = snap32(float(y0) + d.y);
            editor::WorldLayout current = world_;
            for (auto& b : current.maps) for (const auto& mv : worldPending_) if (mv.name == b.name) { b.x = mv.x; b.y = mv.y; }
            std::vector<editor::MapMove> others;
            for (const auto& mv : worldPending_) if (mv.name != box->name) others.push_back(mv);
            worldDragValid_ = editor::checkMove(current, others, {box->name, worldDragX_, worldDragY_}, worldDragWhy_);
        }
    }

    // draw every box (largest first so small maps stay clickable)
    const bool showNames = worldZoom_ * 32 >= S(9);
    std::vector<std::string> neighbours;
    if (const auto* sel = world_.find(worldSelected_)) {
        int sx, sy; worldPlacement(sel->name, sx, sy);
        if (worldDragging_) { sx = worldDragX_; sy = worldDragY_; }
        editor::WorldLayout current = world_;
        for (auto& b : current.maps) for (const auto& mv : worldPending_) if (mv.name == b.name) { b.x = mv.x; b.y = mv.y; }
        for (const auto* n : current.touching(*sel, sx, sy)) neighbours.push_back(n->name);
    }
    for (const auto* b : order) {
        int x, y; worldPlacement(b->name, x, y);
        const bool isSel = b->name == worldSelected_;
        if (isSel && worldDragging_) { x = worldDragX_; y = worldDragY_; }
        const ImVec2 p0 = toScreen(float(x), float(y)), p1 = toScreen(float(x + b->w), float(y + b->h));
        if (p1.x < origin.x || p0.x > origin.x + size.x || p1.y < origin.y || p0.y > origin.y + size.y) continue;
        bool pending = false;
        for (const auto& mv : worldPending_) pending = pending || mv.name == b->name;
        const bool isNeighbour = std::find(neighbours.begin(), neighbours.end(), b->name) != neighbours.end();
        const bool isHover = b->name == worldHover_;
        ImU32 fill = regionColour(b->regionSlot, isSel ? 0.85f : isHover ? 0.75f : 0.55f);
        if (b->region.empty()) fill = ImGui::ColorConvertFloat4ToU32(ImVec4(0.4f, 0.4f, 0.45f, 0.5f));
        dl->AddRectFilled(p0, p1, fill);
        ImU32 line = theme::col(theme::Border);
        float thick = 1.0f;
        if (isNeighbour) { line = theme::col(theme::AccentSoft); thick = S(2); }
        if (pending) { line = theme::col(theme::Warn); thick = S(2); }
        if (isSel) { line = worldDragging_ && !worldDragValid_ ? theme::col(theme::Error) : theme::col(theme::Accent); thick = S(2.5f); }
        dl->AddRect(p0, p1, line, 0, 0, thick);
        if (showNames && p1.x - p0.x > S(30)) {
            ImGui::PushFont(fontSmall_);
            const ImVec2 ts = ImGui::CalcTextSize(b->name.c_str());
            if (ts.x < p1.x - p0.x - S(4)) dl->AddText(ImVec2(p0.x + S(3), p0.y + S(2)), theme::col(theme::Text), b->name.c_str());
            ImGui::PopFont();
        }
        if (isSel && worldDragging_) {
            // the ghost of the original place
            int ox, oy; worldPlacement(b->name, ox, oy);
            const ImVec2 g0 = toScreen(float(ox), float(oy)), g1 = toScreen(float(ox + b->w), float(oy + b->h));
            dl->AddRect(g0, g1, theme::col(theme::Muted), 0, 0, 1.0f);
        }
    }
    // hover tooltip
    if (!worldHover_.empty() && !worldDragging_ && !worldPanning_) {
        const auto* b = world_.find(worldHover_);
        int x, y; worldPlacement(b->name, x, y);
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(b->name.c_str());
        ImGui::PushFont(fontSmall_);
        ImGui::TextColored(theme::vec(theme::Muted), "%s  |  %dx%d at %d,%d%s", b->region.empty() ? "(no region)" : b->region.c_str(), b->w, b->h, x, y, b->inStb ? "" : "  |  no terrain chunk");
        ImGui::PopFont();
        ImGui::EndTooltip();
    }
    if (worldDragging_) {
        ImGui::PushFont(fontSmall_);
        char t[160];
        std::snprintf(t, sizeof t, worldDragValid_ ? "%s -> %d, %d" : "%s -> %d, %d  (%s)", worldSelected_.c_str(), worldDragX_, worldDragY_, worldDragWhy_.c_str());
        dl->AddText(ImVec2(origin.x + S(12), origin.y + size.y - S(24)), worldDragValid_ ? theme::col(theme::Text) : theme::col(theme::Error), t);
        ImGui::PopFont();
    }
    // a legend line
    {
        ImGui::PushFont(fontSmall_);
        char t[200];
        std::snprintf(t, sizeof t, "%zu maps  |  drag a map to move it (snaps to 32)  |  wheel zooms, right-drag pans, F fits  |  %zu pending change%s",
                      world_.maps.size(), worldPendingCount(), worldPendingCount() == 1 ? "" : "s");
        dl->AddText(ImVec2(origin.x + S(12), origin.y + S(10)), theme::col(theme::Muted), t);
        ImGui::PopFont();
    }
}

// ----------------------------------------------------------------- panel

void App::drawWorldPanel(float pad, float inner, float cardInner) {
    using theme::S;
    ImGui::SetCursorPosX(pad);
    theme::beginCard("##worldsel", inner);
    theme::label("Selected map");
    const editor::WorldMapBox* box = world_.find(worldSelected_);
    if (!box) {
        ImGui::PushFont(fontSmall_);
        theme::hint("Click a map on the grid. Drag it to a new place; neighbours it touches light up. Moves are queued here and written to the game together.");
        ImGui::PopFont();
    } else {
        if (worldEditFor_ != box->name) { worldPlacement(box->name, worldEditX_, worldEditY_); worldEditFor_ = box->name; }
        ImGui::PushFont(fontBold_);
        ImGui::TextUnformatted(box->name.c_str());
        ImGui::PopFont();
        ImGui::PushFont(fontSmall_);
        ImGui::TextColored(theme::vec(theme::Muted), "%s  |  %d x %d cells  |  slot %d%s", box->region.empty() ? "no region" : box->region.c_str(), box->w, box->h, box->slot, box->inStb ? "" : "  |  no terrain chunk");
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, S(4)));
        const float half = (cardInner - S(6)) * 0.5f;
        ImGui::SetNextItemWidth(half);
        ImGui::InputInt("##wx", &worldEditX_, 32, 128);
        auto_.registerWidget("world_x");
        ImGui::SameLine(0, S(6));
        ImGui::SetNextItemWidth(half);
        ImGui::InputInt("##wy", &worldEditY_, 32, 128);
        auto_.registerWidget("world_y");
        int cx, cy; worldPlacement(box->name, cx, cy);
        const bool changed = cx != worldEditX_ || cy != worldEditY_;
        if (theme::primaryButton(changed ? "Move here" : "Origin", ImVec2(cardInner, S(30)), changed)) worldMove(box->name, snap32(float(worldEditX_)), snap32(float(worldEditY_)));
        auto_.registerWidget("btn_world_move");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("World origin of the map's top-left cell (WLD MapX/MapY). Snaps to 32.\nArrow keys nudge the selected map by 32 on the grid.");
        if (cx != box->x || cy != box->y) {
            ImGui::PushFont(fontSmall_);
            ImGui::TextColored(theme::vec(theme::Warn), "pending: %d,%d -> %d,%d", box->x, box->y, cx, cy);
            ImGui::PopFont();
            if (theme::ghostButton("Put back", ImVec2(cardInner, S(26)))) worldMove(box->name, box->x, box->y);
            auto_.registerWidget("btn_world_putback");
        }
        // owning region: the region a map is loaded with (exactly one)
        ImGui::Dummy(ImVec2(0, S(6)));
        theme::labelValue("Owned by region", "", cardInner);
        const std::string owner = worldOwnerOf(box->name);
        ImGui::SetNextItemWidth(cardInner);
        if (ImGui::BeginCombo("##owner", owner.empty() ? "(no region)" : owner.c_str())) {
            for (const auto& r : world_.regions)
                if (ImGui::Selectable(r.c_str(), r == owner)) worldSetOwner(box->name, r);
            ImGui::EndCombo();
        }
        auto_.registerWidget("combo_world_owner");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The region that loads this map (WLD ContainsMap; retail keeps exactly one owner per map).");
        if (owner != box->region) {
            ImGui::PushFont(fontSmall_);
            ImGui::TextColored(theme::vec(theme::Warn), "pending: %s -> %s", box->region.empty() ? "(none)" : box->region.c_str(), owner.c_str());
            ImGui::PopFont();
        }
    }
    theme::endCard();

    // neighbours: which of them this map's region draws, and which of their
    // regions draw this map (WLD SeesMap = loaded/visible while in that region)
    if (box) {
        int sx, sy; worldPlacement(box->name, sx, sy);
        editor::WorldLayout current = world_;
        for (auto& b : current.maps) for (const auto& mv : worldPending_) if (mv.name == b.name) { b.x = mv.x; b.y = mv.y; }
        const auto touching = current.touching(*box, sx, sy);
        ImGui::Dummy(ImVec2(0, S(8)));
        ImGui::SetCursorPosX(pad);
        theme::beginCard("##worldnb", inner);
        theme::label("Neighbours");
        const std::string mine = worldOwnerOf(box->name);
        ImGui::PushFont(fontSmall_);
        if (touching.empty()) theme::hint("No map touches this one at its current place.");
        else theme::hint("Which regions draw which maps across this edge: 'seen' = loaded and drawn while the player is in that region (WLD SeesMap).");
        ImGui::PopFont();
        int row = 0;
        for (const auto* n : touching) {
            ImGui::PushID(row++);
            const std::string theirs = worldOwnerOf(n->name);
            ImGui::TextUnformatted(n->name.c_str());
            ImGui::PushFont(fontSmall_);
            ImGui::SameLine();
            ImGui::TextColored(theme::vec(theme::Muted), "(%s)", theirs.empty() ? "no region" : theirs.c_str());
            bool a = !mine.empty() && worldSees(mine, n->name);
            bool b = !theirs.empty() && worldSees(theirs, box->name);
            if (!mine.empty()) {
                if (ImGui::Checkbox("seen from mine", &a)) worldSetSees(mine, n->name, a);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s sees %s", mine.c_str(), n->name.c_str());
                if (!theirs.empty()) ImGui::SameLine();
            }
            if (!theirs.empty()) {
                if (ImGui::Checkbox("sees me", &b)) worldSetSees(theirs, box->name, b);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s sees %s", theirs.c_str(), box->name.c_str());
            }
            ImGui::PopFont();
            ImGui::PopID();
        }
        theme::endCard();
    }

    if (worldPendingCount() > 0) {
        ImGui::Dummy(ImVec2(0, S(8)));
        ImGui::SetCursorPosX(pad);
        theme::beginCard("##worldpending", inner);
        theme::label("Pending changes");
        ImGui::PushFont(fontSmall_);
        for (const auto& mv : worldPending_) {
            const auto* b = world_.find(mv.name);
            ImGui::TextColored(theme::vec(theme::Text), "%s", mv.name.c_str());
            ImGui::SameLine();
            ImGui::TextColored(theme::vec(theme::Muted), "%d,%d -> %d,%d", b ? b->x : 0, b ? b->y : 0, mv.x, mv.y);
        }
        for (const auto& e : worldOwnerEdits_) {
            ImGui::TextColored(theme::vec(theme::Text), "%s", e.map.c_str());
            ImGui::SameLine();
            ImGui::TextColored(theme::vec(theme::Muted), "owned by %s", e.region.c_str());
        }
        for (const auto& e : worldSeesEdits_) {
            ImGui::TextColored(theme::vec(theme::Text), "%s", e.region.c_str());
            ImGui::SameLine();
            ImGui::TextColored(theme::vec(theme::Muted), "%s %s", e.sees ? "sees" : "no longer sees", e.map.c_str());
        }
        ImGui::PopFont();
        if (!worldPending_.empty()) {
            ImGui::Dummy(ImVec2(0, S(4)));
            ImGui::Checkbox("Stitch edges with neighbours", &worldStitch_);
            auto_.registerWidget("chk_world_stitch");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("After the move, average the shared edge heights of each moved map and every map it now touches\n(feathered a few cells into both), then re-bake both terrain chunks. Off: the seams stay as the LEVs have them.");
            if (worldStitch_) {
                ImGui::PushFont(fontSmall_);
                char val[48];
                if (worldStitchFeather_ < 0) std::snprintf(val, sizeof val, "auto (one cell per unit of step)");
                else std::snprintf(val, sizeof val, "%d cells", worldStitchFeather_);
                theme::labelValue("Feather", val, inner - S(24));
                ImGui::PopFont();
                ImGui::SetNextItemWidth(inner - S(24));
                ImGui::SliderInt("##stitchfeather", &worldStitchFeather_, -1, 32, "");
            }
        }
        theme::endCard();
    }

    ImGui::Dummy(ImVec2(0, S(8)));
    ImGui::SetCursorPosX(pad);
    theme::beginCard("##worldhelp", inner);
    theme::label("What a move writes");
    ImGui::PushFont(fontSmall_);
    theme::hint("FinalAlbion.wld MapX/MapY and the .bwd box (all three copies the game reads), and the map's terrain chunk in FinalAlbion_RT.stb translated to the new origin: ground and background LOD meshes, water, tree/grass placements. Placed objects (.tng) are map-local and stay as they are. Maps that touched the moved one get their shared edges re-baked. One-time .atlas-orig backups; saves cache the region table, so start a new game to walk the new layout.");
    ImGui::PopFont();
    theme::endCard();
}

void App::drawWorldFooter(float pad, float inner) {
    using theme::S;
    ImGui::SetCursorPosX(pad);
    if (worldFuture_.valid()) {
        theme::primaryButton("Moving maps...", ImVec2(inner, S(42)), false);
        return;
    }
    const bool any = worldPendingCount() > 0;
    if (!confirmWorldApply_) {
        char label[96];
        if (worldPending_.empty()) std::snprintf(label, sizeof label, any ? "Write %zu region change%s to the game" : "No pending changes", worldPendingCount(), worldPendingCount() == 1 ? "" : "s");
        else std::snprintf(label, sizeof label, "Move %zu map%s in the game%s", worldPending_.size(), worldPending_.size() == 1 ? "" : "s", worldPendingCount() > worldPending_.size() ? " (+ region changes)" : "");
        if (theme::primaryButton(label, ImVec2(inner, S(42)), any)) confirmWorldApply_ = true;
        auto_.registerWidget("btn_world_apply");
        if (any || worldCanUndo() || worldCanRedo()) {
            const float third = (inner - 2 * S(6)) / 3.0f;
            ImGui::SetCursorPosX(pad);
            if (theme::ghostButton(worldCanUndo() ? "Undo  (Ctrl+Z)" : "Undo", ImVec2(third, S(30))) && worldCanUndo()) worldUndo();
            auto_.registerWidget("btn_world_undo");
            ImGui::SameLine(0, S(6));
            if (theme::ghostButton(worldCanRedo() ? "Redo  (Ctrl+Y)" : "Redo", ImVec2(third, S(30))) && worldCanRedo()) worldRedo();
            auto_.registerWidget("btn_world_redo");
            ImGui::SameLine(0, S(6));
            if (theme::ghostButton("Revert all", ImVec2(third, S(30))) && any) worldRevert();
            auto_.registerWidget("btn_world_revert");
        }
    } else {
        ImGui::PushFont(fontSmall_);
        ImGui::TextColored(theme::vec(theme::Warn), "%s", worldPending_.empty() ? "Rewrite FinalAlbion.wld/.bwd?" : "Rewrite the world files and the terrain chunks?");
        ImGui::PopFont();
        const float half = (inner - S(6)) * 0.5f;
        if (theme::primaryButton(worldPending_.empty() ? "Yes, write them" : "Yes, move them", ImVec2(half, S(32)))) { confirmWorldApply_ = false; worldApply(); }
        auto_.registerWidget("btn_world_apply_confirm");
        ImGui::SameLine(0, S(6));
        if (theme::ghostButton("Cancel", ImVec2(half, S(32)))) confirmWorldApply_ = false;
    }
}

} // namespace albion::gui
