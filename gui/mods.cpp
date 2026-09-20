// The Mods tab: the install's mod load order (forge_mods.json via forge::modorder) with
// add / remove / reorder / enable, and Build & deploy / Undeploy / Check conflicts, which
// run the shipped forge-tools.exe (mods deploy / undeploy / conflicts) as a process and
// stream its report into the Activity log. Writes go to the save root (the install, or a
// scratch tree under test), through the same running-game guard as every other writer.
#include "app.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "backups.hpp"
#include "forge/modorder.hpp"
#include "nlohmann/json.hpp"
#include "theme.hpp"

namespace fs = std::filesystem;
namespace mo = forge::modorder;

namespace albion::gui {

namespace {

fs::path exeDir() {
#ifdef _WIN32
    char buf[MAX_PATH];
    const DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) return fs::path(std::string(buf, n)).parent_path();
#endif
    return fs::current_path();
}

// forge-tools.exe next to the GUI (the zip), else the build tree's
fs::path findForgeTools() {
    std::error_code ec;
    for (const fs::path cand : {exeDir() / "forge-tools.exe", fs::current_path() / "build" / "forge-tools.exe", fs::path("build/forge-tools.exe")})
        if (fs::exists(cand, ec)) return cand;
    return {};
}

} // namespace

void App::setModsMode(bool on) {
    if (on) { if (worldMode_) setWorldMode(false); if (texturesMode_) setTexturesMode(false); }
    modsMode_ = on;
    if (on) refreshModOrder();
}

void App::refreshModOrder() {
    modReportLoaded_ = false;   // a report describes one order; Check conflicts again after a change
    try { modOrder_ = mo::load(saveRoot()); modOrderError_.clear(); }
    catch (const std::exception& e) { modOrderError_ = e.what(); modOrder_ = mo::Order(); }
}

bool App::modAdd(const std::string& source, const std::string& name) {
    try {
        auto order = mo::load(saveRoot());
        auto& e = mo::add(order, saveRoot(), source, name);
        mo::save(saveRoot(), order);
        pushLog("mods: added \"" + e.name + "\" (" + mo::kindName(e.kind) + ")", 0);
        refreshModOrder();
        return true;
    } catch (const std::exception& e) { pushLog(std::string("mods: ") + e.what(), 2); return false; }
}

bool App::modRemove(const std::string& nameOrIndex) {
    try { auto order = mo::load(saveRoot()); mo::remove(order, nameOrIndex); mo::save(saveRoot(), order); refreshModOrder(); pushLog("mods: removed " + nameOrIndex + " (deploy again to rebuild the install without it)", 0); return true; }
    catch (const std::exception& e) { pushLog(std::string("mods: ") + e.what(), 2); return false; }
}

bool App::modMove(const std::string& nameOrIndex, int to) {
    try { auto order = mo::load(saveRoot()); mo::move(order, nameOrIndex, to); mo::save(saveRoot(), order); refreshModOrder(); return true; }
    catch (const std::exception& e) { pushLog(std::string("mods: ") + e.what(), 2); return false; }
}

bool App::modEnable(const std::string& nameOrIndex, bool on) {
    try { auto order = mo::load(saveRoot()); mo::setEnabled(order, nameOrIndex, on); mo::save(saveRoot(), order); refreshModOrder(); return true; }
    catch (const std::exception& e) { pushLog(std::string("mods: ") + e.what(), 2); return false; }
}

// mods deploy / undeploy / conflicts through forge-tools.exe, output captured line by line
bool App::runModsTool(const std::string& verb) {
    if (modsFuture_.valid()) { pushLog("mods: still busy", 1); return false; }
    const fs::path tool = findForgeTools();
    if (tool.empty()) { pushLog("mods: forge-tools.exe not found next to FableForge.exe", 2); return false; }
    if (verb != "conflicts" && backups::gameRunningIn(saveRoot())) {
        pushLog("mods: Fable is running from this install; quit to the desktop before deploying", 2);
        return false;
    }
    // conflicts: one JSON report over the whole order; deploy/undeploy: the text report, streamed
    const std::string cmd = "\"\"" + tool.string() + "\" mods " + verb + " \"" + saveRoot() + "\"" + (verb == "conflicts" ? " --json" : "") + " 2>&1\"";
    modsVerb_ = verb;
    pushLog("mods: " + verb + " ...", 0);
    modsFuture_ = std::async(std::launch::async, [cmd]() {
        ModsToolResult r;
        FILE* p = _popen(cmd.c_str(), "r");
        if (!p) { r.lines.push_back("cannot start forge-tools.exe"); r.rc = -1; return r; }
        char line[2048];
        while (std::fgets(line, sizeof line, p)) {
            std::string l = line;
            while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
            if (!l.empty()) r.lines.push_back(l);
        }
        r.rc = _pclose(p);
        return r;
    });
    return true;
}

// forge_mods_picks.txt: `key<TAB>winner` per line (forge-tools loadPicks), the keys namespaced by stage
void App::loadModPicks() {
    modPicks_.clear();
    std::ifstream in(fs::path(saveRoot()) / "forge_mods_picks.txt");
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        size_t sep = line.find('\t');
        if (sep == std::string::npos) sep = line.find('=');
        if (sep == std::string::npos) continue;
        modPicks_[line.substr(0, sep)] = line.substr(sep + 1);
    }
}

void App::saveModPicks() {
    const fs::path path = fs::path(saveRoot()) / "forge_mods_picks.txt";
    std::error_code ec;
    if (modPicks_.empty()) { fs::remove(path, ec); return; }
    std::ofstream out(path, std::ios::binary);
    out << "# FableForge: the winners picked on the Mods tab; forge-tools mods deploy reads this file\n";
    for (const auto& [k, v] : modPicks_) out << k << '\t' << v << '\n';
}

bool App::modPick(const std::string& key, const std::string& winner) {
    const ModConflict* row = nullptr;
    for (const auto& c : modConflicts_) if (key == "*" || c.key == key) { row = &c; break; }
    if (!row) { pushLog("mods: no conflict " + key, 2); return false; }
    if (winner == "-") modPicks_.erase(row->key);
    else modPicks_[row->key] = winner;
    saveModPicks();
    pushLog("mods: " + row->label + " -> " + (winner == "-" ? "load order" : winner) + " (forge_mods_picks.txt; deploy applies it)", 0);
    return true;
}

// the conflict report: every row a thing several enabled mods want differently, with the winner the
// load order (or a pick) gives it
static void collectConflicts(const nlohmann::json& rep, std::vector<App::ModConflict>& rows) {
    using nlohmann::json;
    auto mods = [](const json& arr) { std::vector<std::string> v; for (const auto& m : arr) v.push_back(m.get<std::string>()); return v; };
    if (rep.contains("defs"))
        for (const auto& c : rep["defs"].value("conflicts", json::array()))
            rows.push_back({"record", c.value("record", ""), c.value("record", ""), mods(c["mods"]), c.value("winner", ""), c.value("overridden", false)});
    if (rep.contains("tng"))
        for (const auto& l : rep["tng"].value("levels", json::array()))
            for (const auto& c : l.value("conflicts", json::array())) {
                const std::string level = l.value("level", ""), thing = c.value("thing", "");
                rows.push_back({"thing", "tng:" + level + "|" + thing, level + "  " + thing, mods(c["mods"]), c.value("winner", ""), c.value("overridden", false)});
            }
    if (rep.contains("qst"))
        for (const auto& f : rep["qst"].value("files", json::array()))
            for (const auto& c : f.value("conflicts", json::array())) {
                const std::string file = f.value("file", ""), quest = c.value("quest", "");
                std::vector<std::string> m;
                for (const auto& w : c.value("wanted", json::array())) m.push_back(w.value("mod", ""));
                rows.push_back({"quest", "qst:" + file + "|" + quest, file + "  " + quest, m, c.value("winner", ""), c.value("overridden", false)});
            }
    if (rep.contains("text"))
        for (const auto& c : rep["text"].value("contested", json::array())) {
            const std::string lang = c.value("language", ""), name = c.value("name", "");
            rows.push_back({"string", "text:" + lang + "|" + name, lang + "  " + name, mods(c["mods"]), c.value("winner", ""), c.value("overridden", false)});
        }
    if (rep.contains("files"))
        for (const auto& c : rep["files"].value("contested", json::array()))
            rows.push_back({"file", "file:" + c.value("path", ""), c.value("path", ""), mods(c["mods"]), c.value("winner", ""), c.value("overridden", false)});
}

void App::pollModsTool() {
    if (!modsFuture_.valid() || modsFuture_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    const ModsToolResult r = modsFuture_.get();
    if (modsVerb_ == "conflicts" && r.rc == 0) {
        std::string text;
        for (const auto& l : r.lines) { text += l; text += '\n'; }
        try {
            const auto rep = nlohmann::json::parse(text.substr(text.find('{')));
            modConflicts_.clear();
            collectConflicts(rep, modConflicts_);
            loadModPicks();
            modReportLoaded_ = true;
            const auto& sm = rep.value("summary", nlohmann::json::object());
            char buf[256];
            std::snprintf(buf, sizeof buf, "%zu conflict(s) across %zu mod(s): %zu record, %zu thing, %zu quest, %zu string, %zu file",
                          modConflicts_.size(), size_t(sm.value("sources", 0)), size_t(sm.value("defs_conflicts", 0)), size_t(sm.value("tng_conflicts", 0)),
                          size_t(sm.value("qst_conflicts", 0)), size_t(sm.value("text_contested", 0)), size_t(sm.value("files_contested", 0)));
            modReportSummary_ = buf;
            pushLog("mods conflicts: " + modReportSummary_, modConflicts_.empty() ? 0 : 1);
            return;
        } catch (const std::exception& e) {
            pushLog(std::string("mods conflicts: report not readable: ") + e.what(), 2);
        }
    }
    for (const auto& l : r.lines) {
        const bool warn = l.find("warning") != std::string::npos || l.find("conflict") != std::string::npos;
        pushLog("mods " + modsVerb_ + ": " + l, warn ? 1 : 0);
    }
    pushLog("mods " + modsVerb_ + (r.rc == 0 ? ": done" : ": FAILED (rc " + std::to_string(r.rc) + ")"), r.rc == 0 ? 0 : 2);
    if (modsVerb_ == "deploy" || modsVerb_ == "undeploy") backupsScannedAt_ = 0;   // the Setup card re-scans
    refreshModOrder();
}

void App::drawModsPanel(float pad, float inner, float cardInner) {
    using theme::S;
    pollModsTool();
    ImGui::SetCursorPosX(pad);
    theme::beginCard("##modorder", inner);
    theme::label("Load order");
    ImGui::PushFont(fontSmall_);
    theme::hint("First loads first, the last word wins. Every mod is a layer: records of game.bin, things of a level, strings of text.big are merged; whole files (levels, banks) are taken from the last mod that ships them. Deploy rebuilds the install from this list onto the retail files.");
    ImGui::PopFont();
    if (!modOrderError_.empty()) ImGui::TextColored(theme::vec(theme::Warn), "%s", modOrderError_.c_str());
    if (modOrder_.mods.empty()) ImGui::TextColored(theme::vec(theme::Faint), "no mods in the order yet");
    int moveUp = -1, moveDown = -1, remove = -1;
    for (size_t i = 0; i < modOrder_.mods.size(); ++i) {
        auto& e = modOrder_.mods[i];
        ImGui::PushID(int(i));
        bool en = e.enabled;
        if (ImGui::Checkbox("##en", &en)) modEnable(std::to_string(i), en);
        ImGui::SameLine(0, S(6));
        if (theme::ghostButton("^", ImVec2(S(26), S(22))) && i > 0) moveUp = int(i);
        ImGui::SameLine(0, S(3));
        if (theme::ghostButton("v", ImVec2(S(26), S(22))) && i + 1 < modOrder_.mods.size()) moveDown = int(i);
        ImGui::SameLine(0, S(3));
        if (theme::ghostButton("x", ImVec2(S(26), S(22)))) remove = int(i);
        ImGui::SameLine(0, S(8));
        ImGui::TextUnformatted(e.name.c_str());
        ImGui::SameLine();
        ImGui::PushFont(fontSmall_);
        ImGui::TextColored(theme::vec(theme::Muted), "%s%s%s", mo::kindName(e.kind), e.note.empty() ? "" : "  ", e.note.c_str());
        ImGui::PopFont();
        ImGui::PopID();
    }
    if (moveUp >= 0) modMove(std::to_string(moveUp), moveUp - 1);
    if (moveDown >= 0) modMove(std::to_string(moveDown), moveDown + 1);
    if (remove >= 0) modRemove(std::to_string(remove));
    theme::endCard();
    ImGui::Dummy(ImVec2(0, S(8)));

    if (modReportLoaded_) {
        ImGui::SetCursorPosX(pad);
        theme::beginCard("##modconflicts", inner);
        theme::label("Conflicts");
        ImGui::PushFont(fontSmall_);
        theme::hint(modReportSummary_.c_str());
        if (!modConflicts_.empty()) theme::hint("Each row is one thing several mods want differently; the load order decides unless you pick. Picks are kept in forge_mods_picks.txt and applied by Build and deploy.");
        ImGui::PopFont();
        if (modConflicts_.empty()) ImGui::TextColored(theme::vec(theme::Faint), "the enabled mods do not contest anything");
        // one row = the kind and the label (ellipsised to the card), then the winner combo
        auto fit = [](const std::string& text, float width) {
            if (ImGui::CalcTextSize(text.c_str()).x <= width) return text;
            std::string t = text;
            while (t.size() > 4 && ImGui::CalcTextSize((t + "...").c_str()).x > width) t.pop_back();
            return t + "...";
        };
        const float comboW = S(170);
        for (size_t i = 0; i < modConflicts_.size() && i < 400; ++i) {
            const auto& c = modConflicts_[i];
            ImGui::PushID(int(i));
            if (i) ImGui::Dummy(ImVec2(0, S(2)));
            ImGui::PushFont(fontSmall_);
            ImGui::TextColored(theme::vec(theme::Muted), "%s", c.kind.c_str());
            ImGui::SameLine(S(52));
            ImGui::TextUnformatted(fit(c.label, cardInner - S(52)).c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", c.label.c_str());
            ImGui::PopFont();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + cardInner - comboW);
            const auto pk = modPicks_.find(c.key);
            const std::string current = pk != modPicks_.end() ? pk->second : c.winner;
            const std::string shown = (pk != modPicks_.end() ? "* " : "") + current;
            ImGui::SetNextItemWidth(comboW);
            if (ImGui::BeginCombo("##winner", shown.c_str())) {
                if (ImGui::Selectable("load order", pk == modPicks_.end())) modPick(c.key, "-");
                for (const auto& m : c.mods) if (ImGui::Selectable(m.c_str(), current == m && pk != modPicks_.end())) modPick(c.key, m);
                if (ImGui::Selectable("vanilla (retail)", current == "vanilla")) modPick(c.key, "vanilla");
                ImGui::EndCombo();
            }
            ImGui::PopID();
        }
        if (modConflicts_.size() > 400) ImGui::TextColored(theme::vec(theme::Faint), "... %zu more (forge-tools mods conflicts --json lists them all)", modConflicts_.size() - 400);
        theme::endCard();
        ImGui::Dummy(ImVec2(0, S(8)));
    }

    ImGui::SetCursorPosX(pad);
    theme::beginCard("##modadd", inner);
    theme::label("Add a mod");
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(10), S(6)));
    ImGui::SetNextItemWidth(cardInner);
    ImGui::InputTextWithHint("##modpath", "A .fmp, a bsdiff .patch, a .qst, a folder with Data/, or an EgoCore Mods/<Name>/ folder", modAddPath_, sizeof modAddPath_);
    auto_.registerWidget("input_mod_path");
    ImGui::SetNextItemWidth(cardInner);
    ImGui::InputTextWithHint("##modname", "Name (optional; the file or folder name otherwise)", modAddName_, sizeof modAddName_);
    auto_.registerWidget("input_mod_name");
    ImGui::PopStyleVar();
    if (theme::ghostButton("Add to the order", ImVec2(cardInner, S(28))) && modAddPath_[0]) {
        if (modAdd(modAddPath_, modAddName_)) { modAddPath_[0] = 0; modAddName_[0] = 0; }
    }
    auto_.registerWidget("btn_mod_add");
    ImGui::PushFont(fontSmall_);
    theme::hint("Drop the mod's archive contents somewhere and point at the pack file or folder; the order keeps the path and a hash of the contents. bsdiff patches need the retail file they were made against.");
    ImGui::PopFont();
    theme::endCard();
    ImGui::Dummy(ImVec2(0, S(8)));

    ImGui::SetCursorPosX(pad);
    theme::beginCard("##modactions", inner);
    theme::label("Install");
    const bool busy = modsFuture_.valid();
    if (theme::ghostButton(busy && modsVerb_ == "conflicts" ? "Checking..." : "Check conflicts", ImVec2(cardInner, S(28))) && !busy) runModsTool("conflicts");
    auto_.registerWidget("btn_mods_conflicts");
    if (theme::primaryButton(busy && modsVerb_ == "deploy" ? "Deploying..." : "Build and deploy into the game", ImVec2(cardInner, S(32)), !busy && !modOrder_.mods.empty())) runModsTool("deploy");
    auto_.registerWidget("btn_mods_deploy");
    if (theme::ghostButton(busy && modsVerb_ == "undeploy" ? "Undeploying..." : "Undeploy (back to the retail files)", ImVec2(cardInner, S(28))) && !busy) runModsTool("undeploy");
    auto_.registerWidget("btn_mods_undeploy");
    ImGui::PushFont(fontSmall_);
    theme::hint("Deploy reverts the previous deploy first, builds the whole order with your picks and stages it (originals kept as .forgebak; Undeploy puts them back). Refused while Fable runs, and on an install EgoCore has deployed to (restore vanilla there first).");
    ImGui::PopFont();
    theme::endCard();
}

} // namespace albion::gui
