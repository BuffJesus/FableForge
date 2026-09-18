// The Textures tab (0.17): browse textures.big by bank, preview an entry, export it as
// PNG, replace it from an image, add a new one; the selected object's textures on top.
#include "app.hpp"

#include <algorithm>
#include <cstdio>

#include "theme.hpp"
#include "texturebrowse.hpp"

namespace albion::gui {
using theme::S;

void App::setTexturesMode(bool on) {
    // edit mode stays on underneath (the selected object's textures are the point)
    if (on && worldMode_) setWorldMode(false);
    texturesMode_ = on;
    if (on && !texturesLoaded_) refreshTextures();
}

// browse the save root's textures.big when it has one (scratch trees), else the install's;
// writes always go to the save root
std::filesystem::path App::texturesBigPath() const {
    const std::filesystem::path own = std::filesystem::path(saveRoot()) / "data" / "graphics" / "pc" / "textures.big";
    std::error_code ec;
    if (std::filesystem::exists(own, ec)) return own;
    return std::filesystem::path(installPath_) / "data" / "graphics" / "pc" / "textures.big";
}

void App::refreshTextures() {
    std::string err;
    texRows_ = texbrowse::listTextures(texturesBigPath(), err);
    if (texRows_.empty() && !err.empty()) pushLog("textures: " + err, 1);
    texBanks_.clear();
    for (const auto& r : texRows_) if (std::find(texBanks_.begin(), texBanks_.end(), r.bank) == texBanks_.end()) texBanks_.push_back(r.bank);
    texturesLoaded_ = true;
    texPreviewFor_.clear();
    texPreview_ = nullptr;
}

bool App::selectTexture(const std::string& nameOrLabel) {
    if (!texturesLoaded_) refreshTextures();
    for (const auto& r : texRows_)
        if (r.name == nameOrLabel || r.label == nameOrLabel || (nameOrLabel.size() < 10 && std::to_string(r.id) == nameOrLabel)) { texSelected_ = r.name; return true; }
    pushLog("textures: no texture named " + nameOrLabel, 1);
    return false;
}

const texbrowse::TextureRow* App::selectedTexture() const {
    for (const auto& r : texRows_) if (r.name == texSelected_) return &r;
    return nullptr;
}

bool App::exportSelectedTexture(const std::string& outPath) {
    const auto* r = selectedTexture();
    if (!r) { pushLog("textures: nothing selected", 1); return false; }
    std::filesystem::path png = outPath.empty() ? std::filesystem::path(outDirBuf_) / (r->label + ".png") : std::filesystem::path(outPath);
    std::string err;
    if (!texbrowse::exportPng(texturesBigPath(), r->name, png, err)) { pushLog("textures: " + err, 2); return false; }
    pushLog("wrote " + png.string() + " (" + std::to_string(r->width) + "x" + std::to_string(r->height) + ")", 3);
    lastTexturePng_ = png.string();
    return true;
}

bool App::replaceSelectedTexture(const std::string& image) {
    const auto* r = selectedTexture();
    if (!r) { pushLog("textures: nothing selected", 1); return false; }
    if (link_.heartbeatAge >= 0 && link_.heartbeatAge < 5.0) { pushLog("textures: the game is running (live link heartbeat); quit to the desktop first", 1); return false; }
    std::vector<std::string> notes; std::string err;
    const std::string name = r->name;
    if (!texbrowse::replaceTexture(saveRoot(), name, image, notes, err)) { for (const auto& n : notes) pushLog("textures: " + n, 1); pushLog("textures: " + err, 2); return false; }
    for (const auto& n : notes) pushLog("textures: " + n, 3);
    // the preview and the context caches decode from the file: reload both
    refreshTextures();
    texSelected_ = name;
    startContextLoad(saveRoot());
    return true;
}

bool App::addTexture(const std::string& name, const std::string& image, const std::string& bank, const std::string& format) {
    if (link_.heartbeatAge >= 0 && link_.heartbeatAge < 5.0) { pushLog("textures: the game is running (live link heartbeat); quit to the desktop first", 1); return false; }
    std::vector<std::string> notes; std::string err; uint32_t id = 0;
    if (!texbrowse::addTexture(saveRoot(), bank, name, image, format, id, notes, err)) { for (const auto& n : notes) pushLog("textures: " + n, 1); pushLog("textures: " + err, 2); return false; }
    for (const auto& n : notes) pushLog("textures: " + n, 3);
    pushLog("reference the new texture by id " + std::to_string(id) + " (a def's Graphic / theme texture field)", 0);
    refreshTextures();
    texSelected_ = name;
    startContextLoad(saveRoot());
    return true;
}

// the selected thing's diffuse textures (from its mesh parts), for "retexture this barrel"
std::vector<uint32_t> App::selectedThingTextures() const {
    std::vector<uint32_t> ids;
    if (selectedThing_ < 0) return ids;
    for (size_t i = 0; i < renderer_.instanceCount(); ++i) {
        const auto& d = renderer_.instance(i);
        if (d.thing != selectedThing_ || d.mesh < 0 || size_t(d.mesh) >= meshTextures_.size()) continue;
        for (const uint32_t id : meshTextures_[size_t(d.mesh)]) if (id && std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
    }
    return ids;
}

void App::drawTexturesPanel(float pad, float inner, float cardInner) {
    if (!installValid_) { ImGui::SetCursorPosX(pad); theme::hint("Browsing textures needs a Fable install."); return; }
    if (!texturesLoaded_) refreshTextures();

    // ---- the selected object's textures
    const auto thingTex = selectedThingTextures();
    if (!thingTex.empty()) {
        ImGui::SetCursorPosX(pad);
        theme::beginCard("##thingtex", inner);
        theme::label("Textures of the selected object");
        ImGui::PushFont(fontSmall_);
        for (const uint32_t id : thingTex) {
            const texbrowse::TextureRow* row = nullptr;
            for (const auto& r : texRows_) if (r.id == id && r.bank == "GBANK_MAIN_PC") { row = &r; break; }
            char lbl[160]; std::snprintf(lbl, sizeof lbl, "%u  %s##tt%u", id, row ? row->label.c_str() : "(not in textures.big)", id);
            if (ImGui::Selectable(lbl, row && row->name == texSelected_) && row) texSelected_ = row->name;
        }
        ImGui::PopFont();
        theme::endCard();
        ImGui::Dummy(ImVec2(0, S(8)));
    }

    // ---- browser
    ImGui::SetCursorPosX(pad);
    theme::beginCard("##texbrowse", inner);
    theme::label("textures.big");
    ImGui::SetNextItemWidth(cardInner);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(10), S(6)));
    ImGui::InputTextWithHint("##texsearch", "Search textures (name, id)", texSearch_, sizeof texSearch_);
    ImGui::PopStyleVar();
    auto_.registerWidget("input_texsearch");
    ImGui::SetNextItemWidth(cardInner);
    if (ImGui::BeginCombo("##texbank", texBank_.empty() ? "All banks" : texBank_.c_str())) {
        if (ImGui::Selectable("All banks", texBank_.empty())) texBank_.clear();
        for (const auto& b : texBanks_) if (ImGui::Selectable(b.c_str(), b == texBank_)) texBank_ = b;
        ImGui::EndCombo();
    }
    auto_.registerWidget("combo_texbank");
    std::vector<int> rows;
    rows.reserve(texRows_.size());
    const std::string needle = texSearch_;
    auto contains = [](const std::string& hay, const std::string& n) {
        if (n.empty()) return true;
        auto it = std::search(hay.begin(), hay.end(), n.begin(), n.end(), [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
        return it != hay.end();
    };
    for (size_t i = 0; i < texRows_.size(); ++i) {
        const auto& r = texRows_[i];
        if (!texBank_.empty() && r.bank != texBank_) continue;
        if (!needle.empty() && !contains(r.label, needle) && !contains(r.name, needle) && std::to_string(r.id) != needle) continue;
        rows.push_back(int(i));
    }
    ImGui::PushFont(fontSmall_);
    ImGui::TextColored(theme::vec(theme::Faint), "%zu of %zu", rows.size(), texRows_.size());
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::vec(theme::Bg0));
    ImGui::BeginChild("##texlist", ImVec2(cardInner, S(200)), ImGuiChildFlags_None);
    ImGui::PopStyleColor();
    ImGuiListClipper clipper;
    clipper.Begin(int(rows.size()));
    while (clipper.Step())
        for (int k = clipper.DisplayStart; k < clipper.DisplayEnd; ++k) {
            const auto& r = texRows_[size_t(rows[size_t(k)])];
            char lbl[200]; std::snprintf(lbl, sizeof lbl, "%s   %dx%d %s##tx%zu", r.label.c_str(), r.width, r.height, r.format.c_str(), size_t(rows[size_t(k)]));
            if (ImGui::Selectable(lbl, r.name == texSelected_)) texSelected_ = r.name;
        }
    ImGui::EndChild();
    ImGui::PopFont();
    auto_.registerWidget("list_textures");
    theme::endCard();
    ImGui::Dummy(ImVec2(0, S(8)));

    // ---- the selected texture: preview + actions
    const auto* sel = selectedTexture();
    ImGui::SetCursorPosX(pad);
    theme::beginCard("##texsel", inner);
    if (!sel) {
        theme::label("Texture");
        ImGui::PushFont(fontSmall_);
        ImGui::TextColored(theme::vec(theme::Faint), "Pick a texture above.");
        ImGui::PopFont();
    } else {
        ImGui::PushFont(fontBold_);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cardInner);
        ImGui::TextUnformatted(sel->label.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        ImGui::PushFont(fontSmall_);
        ImGui::TextColored(theme::vec(theme::Muted), "id %u   %dx%d %s   %d mips   %u bytes   %s", sel->id, sel->width, sel->height, sel->format.c_str(), sel->mips, sel->bytes, sel->bank.c_str());
        if (sel->label != sel->name) { ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cardInner); ImGui::TextColored(theme::vec(theme::Faint), "%s", sel->name.c_str()); ImGui::PopTextWrapPos(); }
        ImGui::PopFont();
        // preview (decoded once per selection, full first mip)
        if (texPreviewFor_ != sel->name) {
            texPreviewFor_ = sel->name;
            terrainexport::Image img; std::string err;
            texPreview_ = texbrowse::decodeTexture(texturesBigPath(), sel->name, img, err) ? renderer_.previewTexture(img) : nullptr;
            if (!texPreview_ && !err.empty()) pushLog("textures: " + err, 1);
        }
        if (texPreview_) {
            const float w = cardInner, h = sel->width > 0 ? cardInner * float(sel->height) / float(sel->width) : cardInner;
            ImGui::Image((ImTextureID)(intptr_t)texPreview_, ImVec2(w, std::min(h, S(300))));
            auto_.registerWidget("img_texture");
        }
        ImGui::Dummy(ImVec2(0, S(4)));
        const float half = (cardInner - S(6)) * 0.5f;
        if (theme::ghostButton("Export PNG", ImVec2(half, S(28)))) exportSelectedTexture("");
        auto_.registerWidget("btn_tex_export");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Writes %s\\%s.png (the export output folder).", outDirBuf_, sel->label.c_str());
        ImGui::SameLine(0, S(6));
        if (theme::ghostButton("Replace from image...", ImVec2(half, S(28)))) texReplaceOpen_ = !texReplaceOpen_;
        auto_.registerWidget("btn_tex_replace_toggle");
        if (texReplaceOpen_) {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(10), S(6)));
            ImGui::SetNextItemWidth(cardInner);
            ImGui::InputTextWithHint("##texreplacepath", "Path to a PNG / JPG / TGA (resampled to the slot's size)", texImagePath_, sizeof texImagePath_);
            ImGui::PopStyleVar();
            auto_.registerWidget("input_tex_image");
            ImGui::PushFont(fontSmall_);
            theme::hint("Same slot, same pixel format, mips rebuilt; every object using this texture changes. One-time textures.big.atlas-orig backup; refused while the game runs.");
            ImGui::PopFont();
            if (theme::primaryButton("Replace this texture", ImVec2(cardInner, S(30)), texImagePath_[0] != 0)) replaceSelectedTexture(texImagePath_);
            auto_.registerWidget("btn_tex_replace");
        }
    }
    theme::endCard();
    ImGui::Dummy(ImVec2(0, S(8)));

    // ---- add
    ImGui::SetCursorPosX(pad);
    theme::beginCard("##texadd", inner);
    theme::label("Add a texture");
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(10), S(6)));
    ImGui::SetNextItemWidth(cardInner);
    ImGui::InputTextWithHint("##texaddname", "Name, e.g. MY_BARREL_01", texAddName_, sizeof texAddName_, ImGuiInputTextFlags_CharsUppercase);
    auto_.registerWidget("input_tex_add_name");
    ImGui::SetNextItemWidth(cardInner);
    ImGui::InputTextWithHint("##texaddpath", "Path to a PNG / JPG / TGA", texAddPath_, sizeof texAddPath_);
    auto_.registerWidget("input_tex_add_image");
    ImGui::PopStyleVar();
    theme::segmented("##texaddfmt", texAddFormat_, {"DXT1", "DXT3 (alpha)", "ARGB8888"}, cardInner);
    ImGui::PushFont(fontSmall_);
    theme::hint("Appended to GBANK_MAIN_PC with a new id (shown in the log) that a def or theme can reference. Nothing retail is replaced.");
    ImGui::PopFont();
    if (theme::ghostButton("Add to textures.big", ImVec2(cardInner, S(28))) && texAddName_[0] && texAddPath_[0]) {
        const char* fmts[3] = {"dxt1", "dxt3", "argb8888"};
        if (addTexture(texAddName_, texAddPath_, "GBANK_MAIN_PC", fmts[std::clamp(texAddFormat_, 0, 2)])) texAddName_[0] = 0;
    }
    auto_.registerWidget("btn_tex_add");
    theme::endCard();
}

}  // namespace albion::gui
