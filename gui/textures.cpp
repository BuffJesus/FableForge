// The Textures tab (0.17): browse textures.big by bank, preview an entry, export it as
// PNG, replace it from an image, add a new one; the selected object's textures on top.
#include "app.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cctype>
#include <random>

#include "theme.hpp"
#include "texturebrowse.hpp"
#include "meshimport.hpp"
#include "forge/big.hpp"
#include "forge/terraintex.hpp"
#include "resolvedpath.hpp"
#include "windowsprocess.hpp"
#include "forge/defschema.hpp"

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

void App::invalidateTextureViews() {
    texturesLoaded_ = false;
    texRows_.clear(); texBanks_.clear(); texSelected_.clear();
    texExportError_.clear(); texReplaceError_.clear(); texAddError_.clear();
    texPreviewFor_.clear(); texPreview_ = nullptr;
    texThumbFile_.reset();
    renderer_.clearListThumbs();
}

void App::refreshTextures() {
    if (modFilesBusy()) return;
    std::string err;
    texRows_ = texbrowse::listTextures(texturesBigPath(), err);
    if (texRows_.empty() && !err.empty()) pushLog("textures: " + err, 1);
    texBanks_.clear();
    for (const auto& r : texRows_) if (std::find(texBanks_.begin(), texBanks_.end(), r.bank) == texBanks_.end()) texBanks_.push_back(r.bank);
    texturesLoaded_ = true;
    texPreviewFor_.clear();
    texPreview_ = nullptr;
    texThumbFile_.reset();   // contents may have changed: thumbnails decode again
    renderer_.clearListThumbs();
}

// The list thumbnails read one open archive; any change of the file (a replace, an add, a mod
// deploy, a model import) reopens it and drops the thumbnails made from the old contents.
void App::syncTextureThumbs() {
    const auto path = texturesBigPath();
    std::error_code ec;
    const auto time = std::filesystem::last_write_time(path, ec);
    const uintmax_t size = ec ? 0 : std::filesystem::file_size(path, ec);
    if (texThumbFile_ && path == texThumbPath_ && !ec && time == texThumbTime_ && size == texThumbSize_) return;
    texThumbFile_.reset();
    renderer_.clearListThumbs();
    texThumbPath_ = path; texThumbTime_ = time; texThumbSize_ = size;
    if (ec) return;
    try { texThumbFile_ = std::make_shared<const forge::big::File>(forge::big::File::open(path)); }
    catch (const std::exception& e) { pushLog(std::string("textures: thumbnails unavailable: ") + e.what(), 1); }
}

// A row's thumbnail, decoded while this frame's budget lasts (at least one per frame);
// null while it waits or when the entry cannot be decoded.
ID3D11ShaderResourceView* App::textureListThumb(const texbrowse::TextureRow& row, double& budgetMs) {
    ID3D11ShaderResourceView* srv = nullptr;
    if (renderer_.listThumb(row.name, srv) || !texThumbFile_ || budgetMs <= 0) return srv;
    const auto t0 = std::chrono::steady_clock::now();
    terrainexport::Image img; std::string err;
    srv = renderer_.makeListThumb(row.name, texbrowse::decodeTexture(*texThumbFile_, row.name, img, err) ? img : terrainexport::Image{});
    ++texThumbsMade_;
    budgetMs -= std::max(0.5, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    return srv;
}

std::filesystem::path App::graphicsBigPath() const {
    std::error_code ec;
    for (const auto& root : {modelRoot_, installPath_}) {
        if (root.empty()) continue;
        for (const auto* relative : {"data/graphics/graphics.big", "data/graphics/pc/graphics.big"}) {
            const auto path = std::filesystem::path(root) / relative;
            if (std::filesystem::is_regular_file(path, ec)) return path;
        }
    }
    return std::filesystem::path(installPath_) / "data/graphics/graphics.big";
}

bool App::selectTexture(const std::string& nameOrLabel) {
    if (modFilesBusy()) { fileWriteBlocked("texture preview"); return false; }
    if (!texturesLoaded_) refreshTextures();
    std::string error;
    const auto* row=texbrowse::findTextureRow(texRows_,nameOrLabel,error);
    if (!row) { pushLog("textures: " + error, 1); return false; }
    if (texSelected_ != row->name) { texExportError_.clear(); texReplaceError_.clear(); }
    texSelected_ = row->name;
    return true;
}

const texbrowse::TextureRow* App::selectedTexture() const {
    for (const auto& r : texRows_) if (r.name == texSelected_) return &r;
    return nullptr;
}

bool App::exportSelectedTexture(const std::string& outPath) {
    texExportError_.clear();
    const auto* r = selectedTexture();
    if (!r) { texExportError_ = "Nothing is selected."; pushLog("textures: " + texExportError_, 1); return false; }
    std::filesystem::path png = outPath.empty() ? std::filesystem::path(outDirBuf_) / (r->label + ".png") : std::filesystem::path(outPath);
    std::string err;
    if (!texbrowse::exportPng(texturesBigPath(), r->name, png, err)) {
        std::string reason = err;
        const std::string filesystemPrefix = "filesystem error: ";
        if (reason.starts_with(filesystemPrefix)) {
            reason.erase(0, filesystemPrefix.size());
            if (const auto paths = reason.find(" ["); paths != std::string::npos) reason.resize(paths);
        }
        texExportError_ = png.string() + ": " + reason;
        pushLog("textures: " + err, 2); return false;
    }
    pushLog("wrote " + png.string() + " (" + std::to_string(r->width) + "x" + std::to_string(r->height) + ")", 3);
    lastTexturePng_ = png.string();
    return true;
}

bool App::replaceSelectedTexture(const std::string& image) {
    texReplaceError_.clear();
    const auto* r = selectedTexture();
    if (!r) { texReplaceError_ = "Choose a texture first."; pushLog("textures: " + texReplaceError_, 1); return false; }
    if (gameWriteBlocked("textures")) {
        texReplaceError_ = "Texture replacement is blocked. See Activity for details.";
        return false;
    }
    std::vector<std::string> notes; std::string err;
    const std::string name = r->name;
    if (!texbrowse::replaceTexture(saveRoot(), name, image, notes, err)) {
        texReplaceError_ = err;
        for (const auto& n : notes) pushLog("textures: " + n, 1);
        pushLog("textures: " + err, 2); return false;
    }
    for (const auto& n : notes) pushLog("textures: " + n, 3);
    // the preview and the context caches decode from the file: reload both
    refreshTextures();
    texSelected_ = name;
    startContextLoad(saveRoot());
    return true;
}

bool App::addTexture(const std::string& name, const std::string& image, const std::string& bank, const std::string& format) {
    texAddError_.clear();
    if (gameWriteBlocked("textures")) {
        texAddError_ = "Texture import is blocked. See Activity for details.";
        return false;
    }
    std::vector<std::string> notes; std::string err; uint32_t id = 0;
    if (!texbrowse::addTexture(saveRoot(), bank, name, image, format, id, notes, err)) {
        texAddError_ = err;
        for (const auto& n : notes) pushLog("textures: " + n, 1);
        pushLog("textures: " + err, 2); return false;
    }
    for (const auto& n : notes) pushLog("textures: " + n, 3);
    pushLog("reference the new texture by id " + std::to_string(id) + " (a def's Graphic / theme texture field)", 0);
    refreshTextures();
    texExportError_.clear(); texReplaceError_.clear();
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
    if (modFilesBusy()) { ImGui::SetCursorPosX(pad); theme::hint("Assets will refresh when mod processing finishes."); return; }
    if (!installValid_) { ImGui::SetCursorPosX(pad); theme::hint("The asset tools need a Fable install."); return; }
    // Assets: textures.big, your own models, your own ground themes -- everything that writes the
    // game's shared banks, apart from the map editor (a modder's review: keep imports out of it)
    ImGui::SetCursorPosX(pad);
    static const char* assetNames[] = {"Textures", "Models", "Ground themes", "Effects", "Dialogue", "Text", "Definitions"};
    const float segmentWidth = (inner - S(4)) / 7;
    if (segmentWidth < ImGui::CalcTextSize("Ground themes").x + S(8)) {
        ImGui::SetNextItemWidth(inner);
        if (ImGui::BeginCombo("##assettab_menu", assetNames[std::clamp(assetsTab_, 0, 6)])) {
            for (int i = 0; i < 7; ++i) {
                if (ImGui::Selectable(assetNames[i], assetsTab_ == i)) assetsTab_ = i;
                auto_.registerWidget(("asset_page_" + std::to_string(i)).c_str());
            }
            ImGui::EndCombo();
        }
        auto_.registerWidget("combo_assets_tab");
    } else {
        theme::segmented("##assettab", assetsTab_, {"Textures", "Models", "Ground themes", "Effects", "Dialogue", "Text", "Definitions"}, inner);
        auto_.registerWidget("seg_assets_tab");
    }
    ImGui::Dummy(ImVec2(0, theme::S(8)));
    if (assetsTab_ == 1) { drawModelBrowser(pad, inner, cardInner); return; }
    if (assetsTab_ == 2) { drawGroundThemeCard(pad, inner, cardInner); return; }
    if (assetsTab_ == 3) { drawEffectBrowser(pad, inner, cardInner); return; }
    if (assetsTab_ == 4) { drawDialogueBrowser(pad, inner, cardInner); return; }
    if (assetsTab_ == 5) { drawTextBrowser(pad, inner, cardInner); return; }
    if (assetsTab_ == 6) { drawDefinitionBrowser(pad, inner, cardInner); return; }
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
            if (ImGui::Selectable(lbl, row && row->name == texSelected_) && row) selectTexture(row->name);
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
    if (focusFilter_) { ImGui::SetKeyboardFocusHere(); focusFilter_ = false; }
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
    syncTextureThumbs();
    double thumbBudgetMs = 4.0;   // decode time per frame; scrolling fills the rest over the next frames
    const float rowH = S(26), box = S(22);
    ImGuiListClipper clipper;
    clipper.Begin(int(rows.size()), rowH + ImGui::GetStyle().ItemSpacing.y);
    while (clipper.Step())
        for (int k = clipper.DisplayStart; k < clipper.DisplayEnd; ++k) {
            const auto& r = texRows_[size_t(rows[size_t(k)])];
            const ImVec2 at = ImGui::GetCursorScreenPos();
            char id[32]; std::snprintf(id, sizeof id, "##tx%zu", size_t(rows[size_t(k)]));
            if (ImGui::Selectable(id, r.name == texSelected_, 0, ImVec2(0, rowH))) selectTexture(r.name);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 b0(at.x + S(2), at.y + (rowH - box) * 0.5f), b1(b0.x + box, b0.y + box);
            dl->AddRectFilled(b0, b1, theme::col(theme::Bg2));
            if (ID3D11ShaderResourceView* t = textureListThumb(r, thumbBudgetMs)) {
                // aspect kept inside the square
                const float aspect = r.height > 0 ? float(r.width) / float(r.height) : 1.0f;
                const ImVec2 sz = aspect >= 1.0f ? ImVec2(box, box / aspect) : ImVec2(box * aspect, box);
                const ImVec2 i0(b0.x + (box - sz.x) * 0.5f, b0.y + (box - sz.y) * 0.5f);
                dl->AddImage((ImTextureID)(intptr_t)t, i0, ImVec2(i0.x + sz.x, i0.y + sz.y));
            }
            char lbl[200]; std::snprintf(lbl, sizeof lbl, "%s   %dx%d %s", r.label.c_str(), r.width, r.height, r.format.c_str());
            dl->AddText(ImVec2(b1.x + S(8), at.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f), theme::col(theme::Text), lbl);
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
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cardInner);
        ImGui::TextColored(theme::vec(theme::Muted), "id %u   %dx%d %s   %d mips   %u bytes   %s", sel->id, sel->width, sel->height, sel->format.c_str(), sel->mips, sel->bytes, sel->bank.c_str());
        ImGui::PopTextWrapPos();
        if (sel->label != sel->name) { ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cardInner); ImGui::TextColored(theme::vec(theme::Faint), "%s", sel->name.c_str()); ImGui::PopTextWrapPos(); }
        ImGui::PopFont();
        // preview (decoded once per selection, full first mip)
        if (texPreviewFor_ != sel->name) {
            texPreviewFor_ = sel->name;
            terrainexport::Image img; std::string err;
            texPreview_ = texbrowse::decodeTexture(texturesBigPath(), sel->name, img, err) ? renderer_.previewTexture(img) : nullptr;
            if (texPreview_) texPreviewSize_ = ImVec2(float(img.width), float(img.height));
            if (!texPreview_ && !err.empty()) pushLog("textures: " + err, 1);
        }
        if (texPreview_) {
            const float scale = std::min(cardInner / texPreviewSize_.x, S(300) / texPreviewSize_.y);
            const ImVec2 size(texPreviewSize_.x * scale, texPreviewSize_.y * scale);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (cardInner - size.x) * 0.5f);
            ImGui::Image((ImTextureID)(intptr_t)texPreview_, size);
            auto_.registerWidget("img_texture");
        }
        ImGui::Dummy(ImVec2(0, S(4)));
        const float half = (cardInner - S(6)) * 0.5f;
        const bool stackActions = half < ImGui::CalcTextSize("Replace from image...").x + S(24);
        const float actionWidth = stackActions ? cardInner : half;
        if (theme::ghostButton("Export PNG", ImVec2(actionWidth, S(28)))) exportSelectedTexture("");
        auto_.registerWidget("btn_tex_export");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Writes %s\\%s.png (the export output folder).", outDirBuf_, sel->label.c_str());
        if (!stackActions) ImGui::SameLine(0, S(6));
        if (theme::ghostButton("Replace from image...", ImVec2(actionWidth, S(28)))) texReplaceOpen_ = !texReplaceOpen_;
        auto_.registerWidget("btn_tex_replace_toggle");
        if (!texExportError_.empty()) {
            ImGui::PushFont(fontSmall_);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cardInner);
            ImGui::TextColored(theme::vec(theme::Error), "PNG export failed: %s", texExportError_.c_str());
            auto_.registerWidget("texture_export_error");
            ImGui::PopTextWrapPos();
            ImGui::PopFont();
        }
        if (texReplaceOpen_) {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(10), S(6)));
            drawPathInput("texreplacepath", "Image file", texImagePath_, sizeof texImagePath_,
                          cardInner, PathField::Image, "input_tex_image");
            ImGui::PopStyleVar();
            ImGui::PushFont(fontSmall_);
            theme::hintMore("Replaces the texture everywhere it is used (backed up once).", "Same slot, same pixel format, mips rebuilt; every object using this texture changes. One-time textures.big.forge-orig backup; refused while the game runs.");
            ImGui::PopFont();
            if (theme::primaryButton("Replace this texture", ImVec2(cardInner, S(30)), texImagePath_[0] != 0)) replaceSelectedTexture(texImagePath_);
            auto_.registerWidget("btn_tex_replace");
            if (!texReplaceError_.empty()) {
                ImGui::PushFont(fontSmall_);
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cardInner);
                ImGui::TextColored(theme::vec(theme::Error), "Replacement failed: %s", texReplaceError_.c_str());
                auto_.registerWidget("texture_replace_error");
                ImGui::PopTextWrapPos();
                ImGui::PopFont();
            }
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
    if (ImGui::InputTextWithHint("##texaddname", "Name, e.g. MY_BARREL_01", texAddName_, sizeof texAddName_, ImGuiInputTextFlags_CharsUppercase)) texAddError_.clear();
    auto_.registerWidget("input_tex_add_name");
    if (drawPathInput("texaddpath", "Path to a PNG / JPG / TGA", texAddPath_, sizeof texAddPath_,
                  cardInner, PathField::Image, "input_tex_add_image")) texAddError_.clear();
    ImGui::PopStyleVar();
    if (theme::segmented("##texaddfmt", texAddFormat_, {"DXT1", "DXT3 (alpha)", "ARGB8888"}, cardInner)) texAddError_.clear();
    ImGui::PushFont(fontSmall_);
    theme::hintMore("Adds a texture a def or theme can use; nothing is replaced.", "Appended to GBANK_MAIN_PC with a new id (shown in the log) that a def or theme can reference. Nothing retail is replaced.");
    ImGui::PopFont();
    if (theme::ghostButton("Add to textures.big", ImVec2(cardInner, S(28))) && texAddName_[0] && texAddPath_[0]) {
        const char* fmts[3] = {"dxt1", "dxt3", "argb8888"};
        if (addTexture(texAddName_, texAddPath_, "GBANK_MAIN_PC", fmts[std::clamp(texAddFormat_, 0, 2)])) texAddName_[0] = 0;
    }
    auto_.registerWidget("btn_tex_add");
    if (!texAddError_.empty()) {
        ImGui::PushFont(fontSmall_);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cardInner);
        ImGui::TextColored(theme::vec(theme::Error), "Texture import failed: %s", texAddError_.c_str());
        auto_.registerWidget("texture_add_error");
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
    }
    theme::endCard();
}


void App::refreshModels() {
    if (modFilesBusy() || meshReplaceFuture_.valid()) return;
    modelsLoaded_ = true;
    modelRows_.clear(); modelUsers_.clear(); modelUsersLoaded_ = false;
    modelGeometry_ = {}; modelReady_ = false; modelId_ = 0;
    modelName_.clear(); modelError_.clear(); renderer_.clearModelPreview();
    try {
        const auto path = graphicsBigPath();
        modelBankPath_ = path.string();
        const auto file = forge::big::File::open(path);
        const auto* bank = file.findBank("MBANK_ALLMESHES");
        if (!bank) throw std::runtime_error("MBANK_ALLMESHES is missing from graphics.big");
        std::map<uint32_t,size_t> idCounts;
        for (const auto& entry : bank->entries) ++idCounts[entry.id];
        for (const auto& entry : bank->entries)
            if (entry.type == 1 || entry.type == 2 || entry.type == 4 || entry.type == 5)
                modelRows_.push_back({entry.id, entry.type, entry.length, entry.name, idCounts[entry.id]!=1});
        std::sort(modelRows_.begin(), modelRows_.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    } catch (const std::exception& e) { modelError_ = e.what(); }
}

bool App::selectModel(const std::string& nameOrId) {
    if (modFilesBusy()) { fileWriteBlocked("model preview"); return false; }
    if (!modelsLoaded_) refreshModels();
    auto it = modelRows_.end();
    const bool exact=std::any_of(modelRows_.begin(),modelRows_.end(),[&](const auto& row) { return row.name==nameOrId; });
    for(auto row=modelRows_.begin();row!=modelRows_.end();++row) {
        if(exact ? row->name!=nameOrId : std::to_string(row->id)!=nameOrId) continue;
        if(it!=modelRows_.end()) { pushLog("models: ambiguous mesh name or ID " + nameOrId,1); return false; }
        it=row;
    }
    if (it == modelRows_.end()) { pushLog("models: no mesh named " + nameOrId, 1); return false; }
    return selectModelById(it->id);
}

bool App::selectModelById(uint32_t id) {
    if(meshReplaceFuture_.valid()) {fileWriteBlocked("model preview");return false;}
    if (modFilesBusy()) { fileWriteBlocked("model preview"); return false; }
    if (!modelsLoaded_) refreshModels();
    const auto it = std::find_if(modelRows_.begin(), modelRows_.end(),
                                 [id](const auto& row) { return row.id == id; });
    if (it == modelRows_.end()) { pushLog("models: no mesh ID " + std::to_string(id), 1); return false; }
    if(it->ambiguousId) {
        pushLog("models: ambiguous mesh ID " + std::to_string(it->id),1); return false;
    }
    if (!ctx_.ready() || ctxFuture_.valid()) { pushLog("models: wait for assets to finish loading", 1); return false; }
    modelReady_ = false; modelGeometry_ = {}; modelSelectedTextureContext_.reset(); renderer_.clearModelPreview();
    modelId_ = it->id; modelName_ = it->name; modelError_.clear(); creatureLookName_.clear(); modelExportStatus_.clear(); modelExportError_.clear();
    modelYaw_ = 0.8f; modelPitch_ = 0.55f; modelZoom_ = 1;
    try {
        modelGeometry_ = forge::meshpreview::readLod0(modelBankPath_, modelId_);
        std::vector<terrainexport::Image> images;
        std::map<uint32_t, int> textureIds;
        std::vector<std::string> warnings;
        const terrainexport::Context* textureContext=&ctx_;
        if(!modelPreviewGraphics_.empty() && paths::resolvedPath(modelBankPath_)==modelPreviewGraphics_) {
            if(!modelPreviewContext_) throw std::runtime_error("Pack model texture preview unavailable: "+modelPreviewContextError_);
            modelSelectedTextureContext_=modelPreviewContext_;
            textureContext=modelSelectedTextureContext_.get();
        }
        const auto mesh = foliageexport::makeMesh(modelId_, modelName_, modelName_, modelGeometry_, true, *textureContext, images, textureIds, warnings);
        modelReady_ = renderer_.setModelPreview(mesh, images);
        if (!modelReady_) modelError_ = "This model has no drawable geometry.";
        for (const auto& warning : warnings) pushLog("models: " + warning, 1);
    } catch (const std::exception& e) { modelError_ = e.what(); }
    if (!modelError_.empty()) pushLog("models: " + modelError_, 1);
    return modelReady_;
}

bool App::selectCreatureLook(const std::string& name, uint32_t seed) {
    if(meshReplaceFuture_.valid() || modFilesBusy()) {fileWriteBlocked("creature preview");return false;}
    if (!modelsLoaded_) refreshModels();
    if (!ctx_.ready() || ctxFuture_.valid()) { pushLog("creature looks: wait for assets to finish loading", 1); return false; }
    modelReady_ = false; modelGeometry_ = {}; modelSelectedTextureContext_.reset(); renderer_.clearModelPreview();
    modelId_ = 0; modelName_ = name; modelError_.clear(); creatureLookSummary_.clear(); modelExportStatus_.clear(); modelExportError_.clear();
    creatureLookName_ = name; creatureLookSeed_ = seed;
    modelYaw_ = 0.8f; modelPitch_ = 0.25f; modelZoom_ = 1;
    try {
        if (!creatureLookSchema_) {
            // Same lookup as the Definitions browser: the bundled copy wins.
            auto path = std::filesystem::current_path() / "docs" / "re_reference" / "def_schema.json";
#ifdef _WIN32
            const auto bundled = process::executableDirectory() / "docs" / "re_reference" / "def_schema.json";
            if (std::filesystem::is_regular_file(bundled)) path = bundled;
#endif
            if (!std::filesystem::is_regular_file(path))
                throw std::runtime_error("creature looks need docs/re_reference/def_schema.json beside FableForge");
            creatureLookSchema_ = std::make_shared<forge::defschema::Schema>(forge::defschema::Schema::load(path));
        }
        const auto assembly = ctx_.assembleCreature(name, seed, modelBankPath_, creatureLookSchema_.get());
        modelGeometry_ = forge::creatureassembly::merge(assembly);
        std::vector<terrainexport::Image> images;
        std::map<uint32_t, int> textureIds;
        std::vector<std::string> warnings;
        const auto mesh = foliageexport::makeMesh(0, name, name, modelGeometry_, true, ctx_, images, textureIds, warnings);
        modelReady_ = renderer_.setModelPreview(mesh, images);
        if (!modelReady_) modelError_ = "This look has no drawable geometry.";
        for (const auto& warning : warnings) pushLog("creature looks: " + warning, 1);
        std::string meshes, shapes;
        for (const auto& part : assembly.parts) meshes += (meshes.empty() ? "" : ", ") + std::to_string(part.mesh);
        for (size_t i = 0; i < assembly.skeletalTargets.size(); ++i) {
            char amount[16]; std::snprintf(amount, sizeof amount, " %.0f%%", assembly.selection.skeletal[i].amount * 100);
            auto target = assembly.skeletalTargets[i];
            if (target.size() > 6 && target.ends_with(".bncfg")) target.resize(target.size() - 6);
            shapes += (shapes.empty() ? "" : ", ") + target + amount;
        }
        creatureLookSummary_ = std::to_string(assembly.parts.size()) + " parts (meshes " + meshes + ")\nBody shape: " +
                               (shapes.empty() ? "default" : shapes);
    } catch (const std::exception& e) { modelError_ = e.what(); }
    if (!modelError_.empty()) pushLog("creature looks: " + modelError_, 1);
    return modelReady_;
}

void App::drawCreatureLooks(float pad, float inner, float cardInner) {
    if (!creatureLookNamesLoaded_ && ctx_.ready() && !ctxFuture_.valid()) {
        for (const auto& [name, type] : ctx_.definitions({"CREATURE"})) {
            std::string error;
            if (ctx_.definitionHasRuntimeComponent(name, "CTCRandomAppearanceMorph", error).value_or(false))
                creatureLookNames_.push_back(name);
        }
        creatureLookNamesLoaded_ = true;
    }
    ImGui::SetCursorPosX(pad);
    theme::beginCard("##creaturelooks", inner);
    theme::label("Creature looks");
    theme::hint("Villagers and bandits pick their body parts, textures and body shape from a seed. Pick a creature, then roll new looks.");
    ImGui::SetNextItemWidth(cardInner);
    ImGui::InputTextWithHint("##creaturelooksearch", "Search creatures", creatureLookSearch_, sizeof creatureLookSearch_);
    auto_.registerWidget("input_creature_look_search");
    const std::string needle = creatureLookSearch_;
    std::vector<const std::string*> rows;
    for (const auto& name : creatureLookNames_)
        if (needle.empty() || std::search(name.begin(), name.end(), needle.begin(), needle.end(), [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
            }) != name.end()) rows.push_back(&name);
    ImGui::BeginChild("##creaturelooklist", ImVec2(cardInner, S(140)));
    ImGuiListClipper clipper; clipper.Begin(int(rows.size()));
    while (clipper.Step()) for (int k = clipper.DisplayStart; k < clipper.DisplayEnd; ++k) {
        const auto& name = *rows[size_t(k)];
        if (ImGui::Selectable(name.c_str(), name == creatureLookName_)) selectCreatureLook(name, creatureLookSeed_);
        auto_.registerWidget(("creature_look_row_" + name).c_str());
    }
    ImGui::EndChild();
    if (rows.empty()) theme::hint(creatureLookNamesLoaded_ ? "No matching creatures." : "Loading creatures...");
    const bool chosen = !creatureLookName_.empty();
    ImGui::BeginDisabled(!chosen);
    const float rollWidth = ImGui::CalcTextSize("New look").x + S(24);
    ImGui::SetNextItemWidth(cardInner - rollWidth - ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::InputScalar("##creaturelookseed", ImGuiDataType_U32, &creatureLookSeed_, nullptr, nullptr, "Seed %u",
                           ImGuiInputTextFlags_EnterReturnsTrue)) selectCreatureLook(creatureLookName_, creatureLookSeed_);
    auto_.registerWidget("input_creature_look_seed");
    ImGui::SameLine();
    if (theme::primaryButton("New look", ImVec2(rollWidth, S(28)), chosen)) {
        static std::mt19937 roll{std::random_device{}()};
        selectCreatureLook(creatureLookName_, uint32_t(roll()));
    }
    auto_.registerWidget("btn_creature_look_roll");
    ImGui::EndDisabled();
    if (chosen && !creatureLookSummary_.empty() && modelName_ == creatureLookName_) {
        ImGui::TextWrapped("%s", creatureLookSummary_.c_str());
        theme::hint("Bind pose. The seed is the creature's stored appearance Seed; animation is not shown.");
    }
    theme::endCard();
    ImGui::Dummy(ImVec2(0, S(8)));
}

std::string App::exportModelGlb() {
    modelExportStatus_.clear(); modelExportError_.clear();
    try {
        if (!modelReady_ || modelGeometry_.empty()) throw std::runtime_error("select a model first");
        const bool look = !creatureLookName_.empty() && modelName_ == creatureLookName_;
        std::string stem = modelName_ + (look ? "_seed" + std::to_string(creatureLookSeed_) : "");
        for (auto& c : stem) if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') c = '_';
        const std::string folder = outDirBuf_[0] ? outDirBuf_ : "exports";
        const auto out = std::filesystem::u8path(folder) / (stem + ".glb");
        const terrainexport::Context* textures = modelSelectedTextureContext_ ? modelSelectedTextureContext_.get() : &ctx_;
        foliageexport::Scene scene;
        scene.rootName = stem;
        std::map<uint32_t, int> textureIds;
        auto mesh = foliageexport::makeMesh(modelId_, modelName_, modelName_, modelGeometry_, true, *textures,
                                            scene.images, textureIds, scene.warnings);
        mesh.instanceCount = 1;
        scene.meshes.push_back(std::move(mesh));
        foliageexport::Instance at; at.mesh = 0; at.prim = 0;
        scene.instances.push_back(at);
        foliageexport::writeModelGlb(scene, out);
        for (const auto& warning : scene.warnings) pushLog("model export: " + warning, 1);
        const auto written = out.u8string();
        const std::string shown(written.begin(), written.end());
        modelExportStatus_ = "Saved " + shown;
        pushLog("model export: " + shown, 0);
        return shown;
    } catch (const std::exception& e) {
        modelExportError_ = e.what();
        pushLog("model export failed: " + modelExportError_, 2);
        return {};
    }
}

bool App::replaceMesh(const std::string& model) {
    meshReplaceError_.clear();meshReplaceSuccess_.clear();
    auto fail=[&](const std::string& message){meshReplaceError_=message;pushLog("replace model: "+message,1);return false;};
    if(fileWriteBlocked("replace model")) return fail("wait for the current file operation to finish");
    if(ctxFuture_.valid()) return fail("model assets are still loading");
    if(foliageFuture_.valid()) return fail("wait for the current level geometry to finish loading");
    if(model.empty() || modelReplaceTarget_.empty()) return fail("choose a model file and target mesh");
    meshimport::ReplaceRequest request;request.model=std::filesystem::u8path(model);request.target=std::to_string(modelReplaceTargetId_);request.expectedName=modelReplaceTarget_;
    const auto base=installPath_,destination=saveRoot();
    pushLog("replace model "+modelReplaceTarget_+": preserving material records and collision",0);
    meshReplaceFuture_=std::async(std::launch::async,[base,destination,request,model]() {
        MeshReplaceJob job;job.submittedPath=model;
        meshimport::ReplaceResult result;
        job.ok=meshimport::replaceModel(base,destination,request,result,job.error);
        job.id=result.meshId;job.name=result.meshName;job.notes=std::move(result.notes);
        return job;
    });
    return true;
}

void App::pollMeshReplace() {
    if(meshReplaceFuture_.valid() && meshReplaceFuture_.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
        const auto job=meshReplaceFuture_.get();
        if(!job.ok) {meshReplaceError_=job.error;pushLog("replace model failed: "+job.error,2);return;}
        meshReplaceError_.clear();meshReplaceSuccess_="Replaced "+job.name+". Original materials and collision retained.";
        for(const auto& note:job.notes) pushLog("replace model: "+note,0);
        if(job.submittedPath==meshReplacePath_) meshReplacePath_[0]=0;
        foliageexport::closeMeshBank();defThumbs_.clear();issuesRev_=~0ull;
        // Only graphics changed. Keep the existing definitions/texture context,
        // particularly when a staged output contains just the new graphics bank.
        modelRoot_=saveRoot();refreshModels();modelReplaceReselect_=job.id;
        effectRenderer_.clearMeshes();effectMeshesReady_=false;
        renderer_.clearHeadPreview();dialogueHeadReady_=false;
        foliageLoadedFor_.clear();foliagePendingName_.clear();thingsReloadPending_=false;startFoliageLoad();
    }
    if(modelReplaceReselect_ && !ctxFuture_.valid()) {
        const auto id=*modelReplaceReselect_;modelReplaceReselect_.reset();selectModelById(id);
    }
}

void App::drawModelReplaceCard(float pad,float inner,float cardInner) {
    ImGui::SetCursorPosX(pad);theme::beginCard("##replacemodel",inner);
    ImGui::TextWrapped("%s (id %u)",modelReplaceTarget_.c_str(),modelReplaceTargetId_);
    theme::hint("All objects using this mesh will show the replacement shape. Keep material slots in their original order.");
    theme::hint("The original collision shape stays in place and may no longer match the visible model.");
    ImGui::TextWrapped("Destination: %s",saveRoot().c_str());
    drawPathInput("meshreplace","A .glb, .gltf or .obj",meshReplacePath_,sizeof meshReplacePath_,cardInner,PathField::Model,"input_mesh_replace_model");
    const bool busy=meshReplaceBusy();
    if(theme::primaryButton(busy?"Replacing...":"Replace geometry",ImVec2(cardInner,S(30)),
                            meshReplacePath_[0] && !busy && !ctxFuture_.valid() && !foliageFuture_.valid() && activeFileJob()==nullptr)) replaceMesh(meshReplacePath_);
    auto_.registerWidget("btn_mesh_replace");
    if(!meshReplaceError_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text,theme::vec(theme::Error));ImGui::TextWrapped("%s",meshReplaceError_.c_str());
        auto_.registerWidget("mesh_replace_error");ImGui::PopStyleColor();
    }
    if(!meshReplaceSuccess_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text,theme::vec(theme::Success));ImGui::TextWrapped("%s",meshReplaceSuccess_.c_str());
        auto_.registerWidget("mesh_replace_success");ImGui::PopStyleColor();
    }
    theme::hint("Supports static models with one LOD and no helpers, bones, cloth or radius override. Unsupported models keep the bank untouched.");
    theme::endCard();
}

void App::drawModelBrowser(float pad, float inner, float cardInner) {
    if (!modelsLoaded_) refreshModels();
    if (!modelUsersLoaded_ && ctx_.ready() && !ctxFuture_.valid()) {
        for (const auto& [name, type] : ctx_.definitions({"OBJECT", "CREATURE", "BUILDING"})) {
            uint32_t id = 0;
            if (ctx_.graphicModelId(name, id) > 0 && id) modelUsers_[id].push_back(name);
        }
        modelUsersLoaded_ = true;
    }
    ImGui::SetCursorPosX(pad);
    theme::beginCard("##modelbrowser", inner);
    theme::label("Models");
    if (theme::ghostButton("Import model...", ImVec2((cardInner-S(6))*0.65f, S(28)))) modelImportOpen_ = !modelImportOpen_;
    auto_.registerWidget("btn_model_import");
    ImGui::SameLine(0, S(6));
    if (theme::ghostButton("Refresh", ImVec2((cardInner-S(6))*0.35f, S(28)))) refreshModels();
    auto_.registerWidget("btn_model_refresh");
    ImGui::SetNextItemWidth(cardInner);
    if (focusFilter_) { ImGui::SetKeyboardFocusHere(); focusFilter_ = false; }
    ImGui::InputTextWithHint("##modelsearch", "Search models (name, id)", modelSearch_, sizeof modelSearch_);
    auto_.registerWidget("input_modelsearch");
    const std::string needle = modelSearch_;
    std::vector<size_t> rows;
    for (size_t i = 0; i < modelRows_.size(); ++i) {
        const auto& row = modelRows_[i];
        const bool matches = std::search(row.name.begin(), row.name.end(), needle.begin(), needle.end(), [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
        }) != row.name.end();
        if (needle.empty() || matches || needle == std::to_string(row.id)) rows.push_back(i);
    }
    modelFiltered_ = rows.size();
    ImGui::TextColored(theme::vec(theme::Faint), "%zu of %zu models", rows.size(), modelRows_.size());
    ImGui::BeginChild("##modellist", ImVec2(cardInner, S(200)));
    ImGuiListClipper clipper; clipper.Begin(int(rows.size()));
    while (clipper.Step()) for (int k = clipper.DisplayStart; k < clipper.DisplayEnd; ++k) {
        const auto& row = modelRows_[rows[size_t(k)]];
        ImGui::PushID(int(row.id));
        if (ImGui::Selectable(row.name.c_str(), row.id == modelId_ && !modelName_.empty())) selectModelById(row.id);
        auto_.registerWidget(("model_row_" + std::to_string(row.id)).c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nid %u | type %u | %u bytes", row.name.c_str(), row.id, row.type, row.bytes);
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (rows.empty()) theme::hint("No matching models.");
    theme::endCard();
    ImGui::Dummy(ImVec2(0, S(8)));
    drawCreatureLooks(pad, inner, cardInner);
    if (!modelError_.empty()) { ImGui::SetCursorPosX(pad); ImGui::TextWrapped("%s", modelError_.c_str()); }
    if (modelName_.empty()) return;
    ImGui::SetCursorPosX(pad);
    theme::beginCard("##modelinfo", inner);
    ImGui::TextWrapped("%s", modelName_.c_str());
    if (!creatureLookName_.empty() && modelName_ == creatureLookName_) ImGui::Text("Creature look | seed %u", creatureLookSeed_);
    else ImGui::Text("id %u | LOD 0", modelId_);
    ImGui::Text("%zu vertices | %zu triangles", modelGeometry_.vertices.size(), modelGeometry_.triangles.size());
    ImGui::Text("%u primitives | %u bones", modelGeometry_.primitiveCount, modelGeometry_.boneCount);
    const auto selected=std::find_if(modelRows_.begin(),modelRows_.end(),[&](const auto& row){return row.id==modelId_;});
    const bool replaceable=selected!=modelRows_.end() && selected->type==1 && !selected->ambiguousId && !meshReplaceBusy();
    ImGui::BeginDisabled(!replaceable);
    if(theme::ghostButton("Replace geometry...",ImVec2(cardInner,S(28)))) {
        modelReplaceTargetId_=modelId_;modelReplaceTarget_=modelName_;modelReplaceOpen_=true;
        meshReplaceError_.clear();meshReplaceSuccess_.clear();
    }
    auto_.registerWidget("btn_model_replace");
    ImGui::EndDisabled();
    if (theme::ghostButton("Export .glb (Blender)", ImVec2(cardInner, S(28)))) exportModelGlb();
    auto_.registerWidget("btn_model_export");
    if (!modelExportError_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::vec(theme::Error)); ImGui::TextWrapped("%s", modelExportError_.c_str()); ImGui::PopStyleColor();
    } else if (!modelExportStatus_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::vec(theme::Success)); ImGui::TextWrapped("%s", modelExportStatus_.c_str()); ImGui::PopStyleColor();
        theme::hint("Saved to the output folder on the Export tab. Textures are embedded.");
    }
    if (modelGeometry_.boneCount) theme::hint("Static pose; animation playback is not available yet.");
    ImGui::Checkbox("Wireframe", &modelWire_); auto_.registerWidget("check_model_wire");
    const float resetWidth = ImGui::CalcTextSize("Reset view").x + S(24);
    const bool stackReset = ImGui::GetItemRectSize().x + ImGui::GetStyle().ItemSpacing.x + resetWidth > cardInner;
    if (!stackReset) ImGui::SameLine();
    if (theme::ghostButton("Reset view", ImVec2(stackReset ? cardInner : resetWidth, S(28)))) { modelYaw_ = 0.8f; modelPitch_ = 0.55f; modelZoom_ = 1; }
    auto_.registerWidget("btn_model_reset");
    theme::hint("Drag the preview to orbit. Scroll to zoom.");
    const bool materialsOpen=ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_DefaultOpen);
    auto_.registerWidget("header_model_materials");
    if (materialsOpen) {
        for (size_t i = 0; i < modelGeometry_.materials.size(); ++i) {
            const auto& mat = modelGeometry_.materials[i];
            ImGui::PushID(int(i));
            ImGui::Text("Material %d", mat.id);
            auto link = [&](const char* label, uint32_t id) {
                if (id == 0) return;
                if(modelSelectedTextureContext_) {
                    std::string name;
                    forge::terraintex::TextureInfo info;
                    if(modelSelectedTextureContext_->textureInfo(id,name,info)) {
                        ImGui::TextWrapped("%s %u: %s (%u x %u)",label,id,name.c_str(),unsigned(info.frameWidth),unsigned(info.frameHeight));
                        auto_.registerWidget(("model_preview_texture_"+std::to_string(i)+"_"+label).c_str());
                    } else ImGui::TextDisabled("%s %u: absent from the model preview source",label,id);
                    return;
                }
                std::string error;
                const auto* row = texbrowse::findTextureRowById(texRows_, "GBANK_MAIN_PC", uint32_t(id), error);
                const std::string caption = std::string(label) + " " + std::to_string(id);
                ImGui::BeginDisabled(row == nullptr);
                if (ImGui::SmallButton(caption.c_str()) && row != nullptr) { selectTexture(row->name); assetsTab_ = 0; }
                auto_.registerWidget(("model_texture_" + std::to_string(i) + "_" + label).c_str());
                ImGui::EndDisabled();
                if (row != nullptr && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", row->label.c_str());
                if (!row) ImGui::TextDisabled("%s", error.c_str());
            };
            link("Diffuse", mat.diffuseTexture); link("Bump", mat.bumpTexture);
            link("Reflection", mat.reflectionTexture); link("Alpha", mat.alphaMapTexture);
            ImGui::PopID();
        }
    }
    if (ImGui::CollapsingHeader("Primitives")) {
        for (const auto& p : modelGeometry_.primitives)
            ImGui::Text("%u vertices | stride %u | format 0x%X", p.vertexCount, p.vertexStride, p.vertexFormat);
    }
    if (ImGui::CollapsingHeader("Helper points")) {
        if (modelGeometry_.helpers.empty()) theme::hint("No helper points.");
        for (const auto& h : modelGeometry_.helpers) {
            ImGui::TextWrapped("%s (bone %u)", h.name.c_str(), h.bone);
            ImGui::Text("  %.2f, %.2f, %.2f", h.matrix[9], h.matrix[10], h.matrix[11]);
        }
    }
    if (ImGui::CollapsingHeader("Used by", ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto it = modelUsers_.find(modelId_);
        if (it == modelUsers_.end()) theme::hint("No object, creature or building definitions reference this model.");
        else for (const auto& name : it->second) ImGui::TextWrapped("%s", name.c_str());
    }
    theme::endCard();
}

void App::drawModelViewport(const ImVec2& origin, const ImVec2& size) {
    const float side = std::max(8.0f, std::min(size.x, size.y));
    const ImVec2 pos(origin.x + (size.x-side)*0.5f, origin.y + (size.y-side)*0.5f);
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton("##modelorbit", ImVec2(side, side), ImGuiButtonFlags_MouseButtonLeft);
    auto_.registerWidget("model_viewport");
    const auto& io = ImGui::GetIO();
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        modelYaw_ -= io.MouseDelta.x * 0.01f;
        modelPitch_ = std::clamp(modelPitch_ + io.MouseDelta.y * 0.01f, -1.5f, 1.5f);
    }
    if (ImGui::IsItemHovered() && io.MouseWheel != 0)
        modelZoom_ = std::clamp(modelZoom_ * std::exp(-io.MouseWheel * 0.12f), 0.5f, 8.0f);
    auto* srv = modelReady_ ? renderer_.modelPreview(uint32_t(side), modelYaw_, modelPitch_, modelZoom_, modelWire_) : nullptr;
    if (srv) ImGui::GetWindowDrawList()->AddImage((ImTextureID)(intptr_t)srv, pos, ImVec2(pos.x+side, pos.y+side));
    else ImGui::GetWindowDrawList()->AddText(ImVec2(origin.x+S(24), origin.y+S(24)), theme::col(theme::Muted), "Select a model to preview it here.");
}

}  // namespace albion::gui
