// The Mods tab: the install's mod load order (forge_mods.json via forge::modorder) with
// add / remove / reorder / enable, and Build & deploy / Undeploy / Check conflicts, which
// run the shipped forge-tools.exe (mods deploy / undeploy / conflicts) as a process and
// stream its report into the Activity log. Writes go to the save root (the install, or a
// scratch tree under test), through the same running-game guard as every other writer.
#include "app.hpp"
#include "dialogueaudio.hpp"

#include <array>
#include <cctype>
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
#include "pendingbanks.hpp"
#include "forge/modorder.hpp"
#include "modpack.hpp"
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

// Quote one Windows argv value, including a trailing backslash. No shell expansion.
std::string processArgument(const std::string& value) {
    std::string out = "\"";
    size_t slashes = 0;
    for (const char c : value) {
        if (c == '\\') { ++slashes; continue; }
        out.append(c == '"' ? slashes * 2 + 1 : slashes, '\\');
        slashes = 0;
        out += c;
    }
    out.append(slashes * 2, '\\');
    out += '"';
    return out;
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

void App::setSaveRoot(const std::string& root) {
    const fs::path previous = fs::path(saveRoot()).lexically_normal();
    const fs::path next = fs::path(root.empty() ? installPath_ : root).lexically_normal();
    if (previous != next && fileWriteBlocked("save folder")) return;
    saveRoot_ = root;
    if (previous != fs::path(saveRoot()).lexically_normal()) resetModDestination();
}

void App::resetModDestination() {
    meshImportError_.clear(); customThemeError_.clear();
    meshImportSuccess_.clear();
    packDest_.clear(); packDestChosen_ = false;
    modConflicts_.clear(); modReportSummary_.clear();
    modsVerb_.clear();
    thingOrigin_.clear(); originMods_.clear(); originFilter_.clear();
    refreshModOrder();
    modPicks_.clear();
    loadModPicks();
}

void App::refreshModOrder() {
    modReportLoaded_ = false;   // a report describes one order; Check conflicts again after a change
    modNewMissingMeshes_.clear(); modMissingMeshTotal_ = modMissingMeshBaseline_ = modAssetUnparsed_ = 0;
    modAssetStatus_.clear(); modAssetError_.clear();
    try { modOrder_ = mo::load(saveRoot()); modOrderError_.clear(); }
    catch (const std::exception& e) { modOrderError_ = e.what(); modOrder_ = mo::Order(); }
    // the FableForge packs' masters, checked against this order
    modRows_.assign(modOrder_.mods.size(), {});
    std::vector<modpack::OrderEntry> order;
    std::vector<modpack::Pack> packs(modOrder_.mods.size());
    for (size_t i = 0; i < modOrder_.mods.size(); ++i) {
        const auto& m = modOrder_.mods[i];
        modpack::OrderEntry oe; oe.name = m.name; oe.enabled = m.enabled;
        if (m.kind == mo::Kind::Forge) {
            fs::path src(m.source);
            if (src.is_relative()) src = fs::path(saveRoot()) / src;
            modRows_[i].packFolder = src.string();
            try { packs[i] = modpack::load(src); oe.packName = packs[i].name; modRows_[i].packName = packs[i].name; modRows_[i].masters = packs[i].masters; }
            catch (const std::exception& e) { modRows_[i].problems.push_back(e.what()); }
        }
        order.push_back(oe);
    }
    for (size_t i = 0; i < modOrder_.mods.size(); ++i)
        if (modOrder_.mods[i].enabled && !modRows_[i].packFolder.empty())
            for (auto& prob : modpack::masterProblems(packs[i], i, order)) modRows_[i].problems.push_back(std::move(prob));
}

bool App::modSetRequires(const std::string& mod, const std::string& master, bool on) {
    if (fileWriteBlocked("mod requirements")) return false;
    size_t at = modOrder_.mods.size();
    for (size_t i = 0; i < modOrder_.mods.size(); ++i)
        if (modOrder_.mods[i].name == mod || std::to_string(i) == mod) { at = i; break; }
    if (at == modOrder_.mods.size() || modRows_[at].packFolder.empty()) { pushLog("mods: " + mod + " is not a FableForge pack (only packs declare masters)", 2); return false; }
    try {
        auto pk = modpack::load(modRows_[at].packFolder);
        std::erase(pk.masters, master);
        if (on) pk.masters.push_back(master);
        modpack::save(modRows_[at].packFolder, pk);
    } catch (const std::exception& e) { pushLog(std::string("mods: ") + e.what(), 2); return false; }
    pushLog("mods: " + modOrder_.mods[at].name + (on ? " now requires " : " no longer requires ") + master, 0);
    refreshModOrder();
    return true;
}

std::string App::modProblems() const {
    std::string s;
    for (size_t i = 0; i < modRows_.size(); ++i)
        for (const auto& p : modRows_[i].problems) s += (s.empty() ? "" : "; ") + modOrder_.mods[i].name + " " + p;
    return s;
}

bool App::modAdd(const std::string& source, const std::string& name) {
    if (fileWriteBlocked("add mod")) return false;
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
    if (fileWriteBlocked("remove mod")) return false;
    try { auto order = mo::load(saveRoot()); mo::remove(order, nameOrIndex); mo::save(saveRoot(), order); refreshModOrder(); pushLog("mods: removed " + nameOrIndex + " (deploy again to rebuild the install without it)", 0); return true; }
    catch (const std::exception& e) { pushLog(std::string("mods: ") + e.what(), 2); return false; }
}

bool App::modMove(const std::string& nameOrIndex, int to) {
    if (fileWriteBlocked("mod order")) return false;
    try { auto order = mo::load(saveRoot()); mo::move(order, nameOrIndex, to); mo::save(saveRoot(), order); refreshModOrder(); return true; }
    catch (const std::exception& e) { pushLog(std::string("mods: ") + e.what(), 2); return false; }
}

bool App::modEnable(const std::string& nameOrIndex, bool on) {
    if (fileWriteBlocked("mod enabled")) return false;
    try { auto order = mo::load(saveRoot()); mo::setEnabled(order, nameOrIndex, on); mo::save(saveRoot(), order); refreshModOrder(); return true; }
    catch (const std::exception& e) { pushLog(std::string("mods: ") + e.what(), 2); return false; }
}

// mods deploy / undeploy / conflicts through forge-tools.exe, output captured line by line
bool App::runModsTool(const std::string& verb) {
    if (modsFuture_.valid()) { pushLog("mods: still busy", 1); return false; }
    if (fileWriteBlocked("mods")) return false;
    if (batchActive()) { pushLog("mods: wait for the batch export to finish", 1); return false; }
    const fs::path tool = findForgeTools();
    if (tool.empty()) { pushLog("mods: forge-tools.exe not found next to FableForge.exe", 2); return false; }
    if (verb != "conflicts" && backups::gameRunningIn(saveRoot())) {
        pushLog("mods: Fable is running from this install; quit to the desktop before deploying", 2);
        return false;
    }
    // conflicts: one JSON report over the whole order; deploy/undeploy: the text report, streamed
    const std::string cmd = processArgument(tool.string()) + " mods " + verb + " " +
        processArgument(saveRoot()) + (verb == "conflicts" ? " --json" : "");
    modsVerb_ = verb;
    pushLog("mods: " + verb + " ...", 0);
    if (verb != "conflicts") {
        modsQueuedVerb_ = verb; modsQueuedCommand_ = cmd;
        if (worldTileCancel_) worldTileCancel_->store(true);
        clearWorldDetail();
    } else launchModsCommand(cmd);
    return true;
}

void App::launchModsCommand(const std::string& cmd) {
    modsFuture_ = std::async(std::launch::async, [cmd]() {
        ModsToolResult r;
#ifdef _WIN32
        struct Handle {
            HANDLE value = nullptr;
            ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
            void close() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); value = nullptr; }
        };
        Handle read, write, input, process, thread;
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        auto failed = [&](const char* operation) {
            const DWORD error = GetLastError();
            r.lines.push_back(std::string(operation) + " (Windows error " + std::to_string(error) + ")");
            r.rc = -1;
        };
        if (!CreatePipe(&read.value, &write.value, &security, 0) ||
            !SetHandleInformation(read.value, HANDLE_FLAG_INHERIT, 0)) {
            failed("cannot create mod command output pipe"); return r;
        }
        input.value = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (input.value == INVALID_HANDLE_VALUE) { failed("cannot open mod command input"); return r; }
        STARTUPINFOA startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = input.value;
        startup.hStdOutput = startup.hStdError = write.value;
        PROCESS_INFORMATION child{};
        std::string command = cmd;
        if (!CreateProcessA(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                nullptr, nullptr, &startup, &child)) {
            failed("cannot start forge-tools.exe"); return r;
        }
        process.value = child.hProcess; thread.value = child.hThread;
        write.close(); input.close();
        std::string output;
        char buffer[4096]; DWORD count = 0;
        DWORD readError = ERROR_SUCCESS;
        for (;;) {
            if (!ReadFile(read.value, buffer, sizeof(buffer), &count, nullptr)) { readError = GetLastError(); break; }
            if (!count) break;
            output.append(buffer, count);
        }
        read.close();
        WaitForSingleObject(process.value, INFINITE);
        DWORD code = 1;
        if (!GetExitCodeProcess(process.value, &code)) { failed("cannot read mod command status"); return r; }
        r.rc = static_cast<int>(code);
        std::istringstream lines(output);
        std::string line;
        while (std::getline(lines, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) r.lines.push_back(std::move(line));
        }
        if (readError != ERROR_BROKEN_PIPE && readError != ERROR_SUCCESS) {
            r.lines.push_back("cannot read mod command output (Windows error " + std::to_string(readError) + ")");
            r.rc = -1;
        }
#else
        r.lines.push_back("mod commands require Windows process support");
        r.rc = -1;
#endif
        return r;
    });
}

// Read a fresh candidate before editing so external choices are not overwritten.
namespace {
std::map<std::string, std::string> readModPicks(const fs::path& root) {
    const auto path = root / "forge_mods_picks.txt";
    std::map<std::string, std::string> picks;
    if (!fs::exists(path)) return picks;
    if (!fs::is_regular_file(path)) throw std::runtime_error("not a regular file: " + path.string());
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open " + path.string());
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t sep = line.find('\t');
        if (sep == std::string::npos) sep = line.find('=');
        if (sep == std::string::npos) continue;
        auto key = line.substr(0, sep), winner = line.substr(sep + 1);
        while (!key.empty() && std::isspace(static_cast<unsigned char>(key.back()))) key.pop_back();
        while (!winner.empty() && std::isspace(static_cast<unsigned char>(winner.front()))) winner.erase(winner.begin());
        while (!winner.empty() && std::isspace(static_cast<unsigned char>(winner.back()))) winner.pop_back();
        if (!key.empty()) picks[key] = winner;
    }
    if (in.bad() || !in.eof()) throw std::runtime_error("incomplete read of " + path.string());
    return picks;
}
} // namespace

void App::loadModPicks() {
    try { modPicks_ = readModPicks(saveRoot()); }
    catch (const std::exception& e) { pushLog(std::string("mods: cannot read conflict choices: ") + e.what(), 2); }
}

bool App::saveModPicks(const std::map<std::string, std::string>& picks) {
    try {
        detail::PendingBanks pending(saveRoot(), ".forge-mod-picks-");
        if (picks.empty()) pending.remove("forge_mods_picks.txt");
        else {
            std::ofstream out;
            out.exceptions(std::ios::failbit | std::ios::badbit);
            out.open(pending.prepare("forge_mods_picks.txt"), std::ios::binary);
            out << "# FableForge: the winners picked on the Mods tab; forge-tools mods deploy reads this file\n";
            for (const auto& [k, v] : picks) out << k << '\t' << v << '\n';
            out.close();
        }
        std::string error;
        if (!pending.install(false, error)) throw std::runtime_error(error);
        return true;
    } catch (const std::exception& e) {
        pushLog(std::string("mods: cannot save conflict choices: ") + e.what(), 2);
        return false;
    }
}

bool App::setModPick(const std::string& key, const std::string& winner) {
    if (fileWriteBlocked("mod conflict choice")) return false;
    std::map<std::string, std::string> picks;
    try { picks = readModPicks(saveRoot()); }
    catch (const std::exception& e) {
        pushLog(std::string("mods: cannot read conflict choices: ") + e.what(), 2);
        return false;
    }
    if (winner == "-") picks.erase(key); else picks[key] = winner;
    if (!saveModPicks(picks)) return false;
    modPicks_ = std::move(picks);
    return true;
}

bool App::modPick(const std::string& key, const std::string& winner) {
    if (fileWriteBlocked("mod conflict choice")) return false;
    const ModConflict* row = nullptr;
    for (const auto& c : modConflicts_) if (key == "*" || c.key == key) { row = &c; break; }
    if (!row) { pushLog("mods: no conflict " + key, 2); return false; }
    if (!row->pickable) { pushLog("mods: reorder packs to choose this lip sync winner", 1); return false; }
    if (!setModPick(row->key, winner)) return false;
    pushLog("mods: " + row->label + " -> " + (winner == "-" ? "load order" : winner) + " (forge_mods_picks.txt; deploy applies it)", 0);
    return true;
}

std::string App::modConflictWinner(const ModConflict& conflict) const {
    if (!conflict.pickable) return conflict.winner;
    const auto pick = modPicks_.find(conflict.key);
    if (pick != modPicks_.end()) return pick->second;
    // The report's winner may include a choice that has since been cleared.
    // Contributors are reported in load order; field merges retain their label.
    if (!conflict.fieldMerged && !conflict.mods.empty()) return conflict.mods.back();
    return conflict.winner;
}

// the conflict report: every row a thing several enabled mods want differently, with the winner the
// load order (or a pick) gives it
static void collectConflicts(const nlohmann::json& rep, std::vector<App::ModConflict>& rows) {
    using nlohmann::json;
    auto mods = [](const json& arr) { std::vector<std::string> v; for (const auto& m : arr) v.push_back(m.get<std::string>()); return v; };
    if (rep.contains("defs"))
        for (const auto& c : rep["defs"].value("conflicts", json::array()))
            rows.push_back({"record", c.value("record", ""), c.value("record", ""), mods(c["mods"]), c.value("winner", ""), c.value("overridden", false), true, c.value("field_merged", false)});
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
    if (rep.contains("fse")) {
        for (const auto& c : rep["fse"].value("contested", json::array()))
            rows.push_back({"fse", "fse:" + c.value("quest", ""), "quests.lua  " + c.value("quest", ""), mods(c["mods"]), c.value("winner", ""), c.value("overridden", false)});
        for (const auto& c : rep["fse"].value("id_clashes", json::array()))   // informational: no pick fixes an id, the mod needs a new one
            rows.push_back({"fse id", "", "id " + std::to_string(c.value("id", 0LL)) + ": " + c.value("quest", "") + " (" + c.value("mod", "") + ") also " + c.value("also", ""), {}, "", false});
    }
    if (rep.contains("files"))
        for (const auto& c : rep["files"].value("contested", json::array()))
            rows.push_back({"file", "file:" + c.value("path", ""), c.value("path", ""), mods(c["mods"]), c.value("winner", ""), c.value("overridden", false)});
    if (rep.contains("lip_sync"))
        for (const auto& c : rep["lip_sync"].value("contested", json::array())) {
            const std::string language=c.value("language",""),bank=c.value("bank","");
            const uint32_t id=c.value("soundId",0U);
            const std::string key="lip:"+language+"|"+bank+"|"+std::to_string(id);
            const size_t bankSeparator=bank.find('_',8);
            const std::string shortBank=bankSeparator==std::string::npos ? bank :
                                        bank.substr(bankSeparator+1);
            const std::string label=language+"  "+shortBank+"  Sound "+std::to_string(id);
            rows.push_back({"lip sync",key,label,mods(c["mods"]),c.value("winner",""),
                            false,false});
        }
}

void App::pollModsTool() {
    refreshAfterMods();
    if (!modsQueuedVerb_.empty()) {
        if (modReadersBusy()) return;
        stopWorldTiles();
        foliageexport::closeMeshBank(); thumbBankOpen_ = false;
        const std::string cmd = std::move(modsQueuedCommand_);
        modsVerb_ = std::move(modsQueuedVerb_);
        modsQueuedVerb_.clear();
        launchModsCommand(cmd);
    }
    if (!modsFuture_.valid() || modsFuture_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    const ModsToolResult r = modsFuture_.get();
    if (modsVerb_ == "conflicts" && r.rc == 0) {
        std::string text;
        for (const auto& l : r.lines) { text += l; text += '\n'; }
        try {
            const auto rep = nlohmann::json::parse(text.substr(text.find('{')));
            modConflicts_.clear();
            collectConflicts(rep, modConflicts_);
            modNewMissingMeshes_.clear();
            modMissingMeshTotal_ = modMissingMeshBaseline_ = modAssetUnparsed_ = 0;
            modAssetStatus_.clear(); modAssetError_.clear();
            if (const auto health = rep.find("asset_health"); health != rep.end()) {
                modAssetStatus_ = health->value("status", "unavailable");
                if (modAssetStatus_ == "checked") {
                    modMissingMeshTotal_ = health->value("missing_total", size_t(0));
                    modMissingMeshBaseline_ = health->value("baseline_missing", size_t(0));
                    modAssetUnparsed_ = health->value("unparsed_defs", size_t(0));
                    for (const auto& row : health->value("introduced", nlohmann::json::array()))
                        modNewMissingMeshes_.push_back({row.value("definition", ""), row.value("type", ""), row.value("mesh_id", 0U)});
                } else modAssetError_ = health->value("reason", "asset audit was unavailable");
            }
            loadModPicks();
            modReportLoaded_ = true;
            const auto& sm = rep.value("summary", nlohmann::json::object());
            char buf[256];
            std::snprintf(buf, sizeof buf, "%zu conflict(s) across %zu mod(s): %zu record, %zu thing, %zu quest, %zu string, %zu file, %zu lip sync",
                          modConflicts_.size(), size_t(sm.value("sources", 0)), size_t(sm.value("defs_conflicts", 0)), size_t(sm.value("tng_conflicts", 0)),
                          size_t(sm.value("qst_conflicts", 0)), size_t(sm.value("text_contested", 0)), size_t(sm.value("files_contested", 0)),
                          size_t(sm.value("lip_sync_contested", 0)));
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
    if (modsVerb_ == "deploy" || modsVerb_ == "undeploy") {
        // A failed redeploy may already have reverted the previous stage.
        // Refresh what is actually on disk while preserving the failure report.
        if (r.rc != 0) pushLog("mods: refreshing after a failed write; inspect the error before retrying", 1);
        modsRefreshPending_ = true;
        if (worldTileCancel_) worldTileCancel_->store(true);
        clearWorldDetail();
        refreshAfterMods();
    }
}

bool App::modReadersBusy() const {
    if (ctxFuture_.valid() || previewFuture_.valid() || foliageFuture_.valid() ||
        neighbourFuture_.valid() || exportFuture_.valid() || fitFuture_.valid() ||
        worldDetailFuture_.valid() || !worldScenery_.settled) return true;
    for (const auto& worker : worldTileWorkers_)
        if (worker.valid() && worker.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return true;
    return false;
}

void App::refreshAfterMods() {
    if (!modsRefreshPending_ || modReadersBusy()) return;
    // Let readers retire before closing shared banks or replacing their context.
    stopWorldTiles();
    modsRefreshPending_ = false;
    foliageexport::closeMeshBank();
    thumbBankOpen_ = false; defThumbs_.clear(); defList_.clear(); themeGroupOf_.clear();
    envDefs_.clear(); soundDefs_.clear(); familyNames_.clear(); issuesRev_ = ~0ull;
    newLevelDonor_.clear();
    dialogueAudio_.reset(); dialogueLoaded_ = false;
    dialogueScratchLanguage_.clear();
    const bool worldDraft = worldPendingCount() != 0;
    const bool hadWorld = worldLoaded_ || worldDraft;
    worldLoaded_ = false; worldLoadedFrom_.clear();
    if (hadWorld) {
        loadWorld(true);
        if (worldDraft) pushLog("mods: kept pending world edits and undo while refreshing the layout", 0);
    }
    if (fs::path(saveRoot()).lexically_normal() != fs::path(installPath_).lexically_normal()) {
        startContextLoad(saveRoot());
        pushLog("mods: refreshed assets from the save folder; Maps still lists the selected install", 0);
        return;
    }
    const std::string root = installPath_, selected = selectedName_;
    const bool keepDraft = hasUnsavedEdits() || (documentLoaded() && doc_.strokeActive());
    std::vector<MapEntry> external;
    for (const auto& entry : maps_) if (!entry.worldFile.empty() || entry.key.rfind("file:", 0) == 0) external.push_back(entry);
    ctx_ = {};
    scanInstall(root);
    maps_.insert(maps_.end(), external.begin(), external.end());
    reloadWhenContextReady_ = true;
    if (keepDraft) {
        foliageLoadedFor_.clear(); neighboursFor_.clear();
        loadThingOrigins();
        pushLog("mods: refreshed the map list and kept the unsaved map draft", 1);
        return;
    }
    selectedName_.clear(); docLoadedFor_.clear();
    previewLoadedFor_.clear(); foliageLoadedFor_.clear();
    previewPendingName_.clear(); foliagePendingName_.clear(); neighboursFor_.clear(); lastFramedFor_.clear();
    renderer_.clear(); renderer_.clearThings();
    previewScene_ = {}; previewTextured_ = false;
    foliageInstances_ = thingInstances_ = 0;
    selectedThing_ = -1; selectedUid_ = 0; extraUids_.clear();
    if (findEntry(selected)) selectMap(selected);
    pushLog("mods: refreshed maps and assets from the deployed install", 0);
}

void App::drawModsPanel(float pad, float inner, float cardInner) {
    using theme::S;
    pollModsTool();
    ImGui::SetCursorPosX(pad);
    theme::beginCard("##modorder", inner);
    theme::label("Load order");
    ImGui::PushFont(fontSmall_);
    theme::hintMore("Top loads first; the last mod wins. Drag to reorder.", "First loads first, the last word wins. Every mod is a layer: records of game.bin, things of a level, strings of text.big, maps and regions of the world are merged; whole files (banks) are taken from the last mod that ships them. Drag a name to reorder; right-click a FableForge pack for the mods it requires. Check conflicts adds wins / loses to every row. Deploy rebuilds the install from this list onto the retail files.");
    ImGui::PopFont();
    if (!modOrderError_.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cardInner);
        ImGui::TextColored(theme::vec(theme::Warn), "%s", modOrderError_.c_str());
        ImGui::PopTextWrapPos();
    }
    if (modOrder_.mods.empty()) ImGui::TextColored(theme::vec(theme::Faint), "no mods in the order yet");
    int moveUp = -1, moveDown = -1, remove = -1, dragFrom = -1, dragTo = -1;
    int enableIndex = -1;
    bool enableValue = false, requireValue = false;
    std::string requireMod, requireMaster;
    // the last conflict report per mod: rows it wins, rows it loses
    std::map<std::string, std::pair<std::vector<std::string>, std::vector<std::string>>> wl;
    if (modReportLoaded_)
        for (const auto& c : modConflicts_) {
            if (c.key.empty()) continue;
            const std::string win = modConflictWinner(c);
            for (const auto& m : c.mods) (m == win ? wl[m].first : wl[m].second).push_back(c.label);
        }
    auto listTip = [](const char* head, const std::vector<std::string>& v) {
        std::string t = head;
        for (size_t k = 0; k < v.size() && k < 12; ++k) t += "\n  " + v[k];
        if (v.size() > 12) t += "\n  ... " + std::to_string(v.size() - 12) + " more";
        return t;
    };
    for (size_t i = 0; i < modOrder_.mods.size(); ++i) {
        auto& e = modOrder_.mods[i];
        ImGui::PushID(int(i));
        // the name is the drag handle: drop it on another row to load it there
        const ModRowInfo& info = i < modRows_.size() ? modRows_[i] : ModRowInfo{};
        const float nameX = ImGui::GetCursorPosX();
        ImGui::PushStyleColor(ImGuiCol_Text, theme::vec(e.enabled ? (info.problems.empty() ? theme::Text : theme::Warn) : theme::Faint));
        const ImVec2 nameAt = ImGui::GetCursorScreenPos();
        const float nameHeight = ImGui::CalcTextSize(e.name.c_str(), nullptr, false, cardInner).y;
        ImGui::Selectable("##name", false, ImGuiSelectableFlags_None, ImVec2(cardInner, nameHeight));
        ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(), nameAt,
            ImGui::GetColorU32(ImGuiCol_Text), e.name.c_str(), nullptr, cardInner);
        ImGui::PopStyleColor();
        auto_.registerWidget(("mod_row_" + std::to_string(i)).c_str());
        if (ImGui::BeginDragDropSource()) {
            const int from = int(i);
            ImGui::SetDragDropPayload("FF_MOD_ROW", &from, sizeof from);
            ImGui::Text("Load %s at...", e.name.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("FF_MOD_ROW")) { dragFrom = *static_cast<const int*>(pl->Data); dragTo = int(i); }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::IsItemHovered() && !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            std::string tip = e.name + "\nDrag onto another row to load it there" + (info.packFolder.empty() ? "" : "; right-click for the mods it requires");
            for (const auto& r : info.masters) tip += "\nrequires " + r;
            for (const auto& pr : info.problems) tip += "\n! " + pr;
            ImGui::SetTooltip("%s", tip.c_str());
        }
        if (!info.packFolder.empty()) {
            const ImVec2 available = ImGui::GetMainViewport()->Size;
            const float popupWidth = std::min(S(420), available.x - S(32));
            ImGui::SetNextWindowSize(ImVec2(popupWidth, 0));
            ImGui::SetNextWindowSizeConstraints(ImVec2(popupWidth, 0), ImVec2(popupWidth, available.y - S(32)));
            if (ImGui::BeginPopupContextItem("##masters")) {
                ImGui::PushStyleColor(ImGuiCol_Text, theme::vec(theme::Muted));
                ImGui::TextWrapped("%s requires (loads after):", e.name.c_str());
                ImGui::PopStyleColor();
                for (size_t j = 0; j < modOrder_.mods.size(); ++j) {
                    if (j == i) continue;
                    const std::string& other = modOrder_.mods[j].name;
                    bool req = std::find(info.masters.begin(), info.masters.end(), other) != info.masters.end() ||
                               (!modRows_[j].packName.empty() && std::find(info.masters.begin(), info.masters.end(), modRows_[j].packName) != info.masters.end());
                    ImGui::PushID(int(j));
                    ImGui::BeginGroup();
                    bool changed = ImGui::Checkbox("##requires", &req);
                    auto_.registerWidget(("mod_requires_" + std::to_string(i) + "_" + std::to_string(j)).c_str());
                    ImGui::SameLine();
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
                    ImGui::TextUnformatted(other.c_str());
                    auto_.registerWidget(("mod_requires_label_" + std::to_string(i) + "_" + std::to_string(j)).c_str());
                    if (ImGui::IsItemClicked()) { req = !req; changed = true; }
                    ImGui::PopTextWrapPos();
                    ImGui::EndGroup();
                    if (changed) { requireMod = e.name; requireMaster = other; requireValue = req; }
                    ImGui::PopID();
                }
                ImGui::EndPopup();
            }
        }
        bool en = e.enabled;
        if (ImGui::Checkbox("##en", &en)) { enableIndex = int(i); enableValue = en; }
        auto_.registerWidget(("mod_enabled_" + std::to_string(i)).c_str());
        ImGui::SameLine(0, S(6));
        ImGui::BeginDisabled(i == 0);
        if (ImGui::ArrowButton("##up", ImGuiDir_Up)) moveUp = int(i);
        auto_.registerWidget(("mod_up_" + std::to_string(i)).c_str());
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Load earlier (the mods below win over it)");
        ImGui::SameLine(0, S(3));
        ImGui::BeginDisabled(i + 1 >= modOrder_.mods.size());
        if (ImGui::ArrowButton("##down", ImGuiDir_Down)) moveDown = int(i);
        auto_.registerWidget(("mod_down_" + std::to_string(i)).c_str());
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Load later (wins over the mods above)");
        ImGui::SameLine(0, S(3));
        if (theme::dangerButton("\xC3\x97##rm", ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()))) remove = int(i);
        auto_.registerWidget(("mod_remove_" + std::to_string(i)).c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove from the order (the mod's own files stay where they are)");
        ImGui::PushFont(fontSmall_);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::vec(theme::Muted));
        ImGui::TextWrapped("%s%s%s", mo::kindName(e.kind), e.note.empty() ? "" : "  ", e.note.c_str());
        ImGui::PopStyleColor();
        if (const auto it = wl.find(e.name); it != wl.end()) {
            if (!it->second.first.empty()) {
                ImGui::TextColored(theme::vec(theme::Success), "wins %zu", it->second.first.size());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", listTip("Wins over the other mods on:", it->second.first).c_str());
            }
            if (!it->second.second.empty()) {
                if (!it->second.first.empty()) ImGui::SameLine(0, S(6));
                ImGui::TextColored(theme::vec(theme::Warn), "loses %zu", it->second.second.size());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", listTip("Overridden by a later mod (or a pick) on:", it->second.second).c_str());
            }
        }
        for (const auto& pr : info.problems) {
            ImGui::SetCursorPosX(nameX);
            ImGui::PushStyleColor(ImGuiCol_Text, theme::vec(theme::Warn));
            ImGui::TextWrapped("! %s", pr.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::PopFont();
        if (i + 1 < modOrder_.mods.size()) ImGui::Separator();
        ImGui::PopID();
    }
    // These actions reload modOrder_/modRows_; no row may retain references.
    if (enableIndex >= 0) modEnable(std::to_string(enableIndex), enableValue);
    if (!requireMod.empty()) modSetRequires(requireMod, requireMaster, requireValue);
    if (dragFrom >= 0 && dragTo >= 0 && dragFrom != dragTo) modMove(std::to_string(dragFrom), dragTo);
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
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cardInner);
        if (modAssetStatus_ == "checked") {
            if (modNewMissingMeshes_.empty())
                ImGui::TextColored(theme::vec(theme::Faint), "Models: no new missing mesh references (%zu in build, %zu in base)", modMissingMeshTotal_, modMissingMeshBaseline_);
            else {
                ImGui::TextColored(theme::vec(theme::Warn), "Models: %zu new missing mesh reference(s)", modNewMissingMeshes_.size());
                for (size_t i = 0; i < modNewMissingMeshes_.size() && i < 20; ++i) {
                    const auto& row = modNewMissingMeshes_[i];
                    ImGui::TextWrapped("%s (%s) -> mesh %u", row.definition.c_str(), row.type.c_str(), row.meshId);
                }
                if (modNewMissingMeshes_.size() > 20) theme::hint("More entries are in the forge-tools mods conflicts --json report.");
            }
            if (modAssetUnparsed_) ImGui::TextColored(theme::vec(theme::Warn), "%zu definitions could not be decoded for the model check", modAssetUnparsed_);
        } else if (!modAssetError_.empty())
            ImGui::TextColored(theme::vec(theme::Warn), "Model check unavailable: %s", modAssetError_.c_str());
        ImGui::PopTextWrapPos();
        auto_.registerWidget("mods_asset_status");
        if (!modConflicts_.empty()) theme::hintMore("Things several mods change differently: the load order decides unless you pick.", "Each row is one thing several mods want differently. Reorder packs to choose a lip sync winner. Other winner picks are kept in forge_mods_picks.txt and applied by Build and deploy.");
        ImGui::PopFont();
        if (modConflicts_.empty()) ImGui::TextColored(theme::vec(theme::Faint), "the enabled mods do not contest anything");
        // one row = the kind and the label (ellipsised to the card), then the winner combo
        auto fit = [](const std::string& text, float width) {
            if (ImGui::CalcTextSize(text.c_str()).x <= width) return text;
            std::string t = text;
            while (t.size() > 4 && ImGui::CalcTextSize((t + "...").c_str()).x > width) t.pop_back();
            return t + "...";
        };
        const float comboW = std::min(S(170), cardInner);
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
            if (c.key.empty()) { ImGui::PopID(); continue; }   // informational row (an id clash)
            if (!c.pickable) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + cardInner - comboW);
                ImGui::TextUnformatted(fit(c.winner, comboW).c_str());
                if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nReorder packs to change this lip sync winner", c.winner.c_str());
                ImGui::PopID();
                continue;
            }
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + cardInner - comboW);
            const bool picked = modPicks_.contains(c.key);
            const std::string current = modConflictWinner(c);
            const std::string shown = (picked ? "* " : "") + current;
            ImGui::SetNextItemWidth(comboW);
            if (ImGui::BeginCombo("##winner", shown.c_str())) {
                if (ImGui::Selectable("load order", !picked)) modPick(c.key, "-");
                auto_.registerWidget(("mod_load_order_" + std::to_string(i)).c_str());
                for (const auto& m : c.mods) if (ImGui::Selectable(m.c_str(), current == m && picked)) modPick(c.key, m);
                if (ImGui::Selectable("vanilla (retail)", current == "vanilla")) modPick(c.key, "vanilla");
                ImGui::EndCombo();
            }
            auto_.registerWidget(("mod_winner_" + std::to_string(i)).c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", shown.c_str());
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
    drawPathInput("modpath", "Mod file or folder", modAddPath_, sizeof modAddPath_,
                  cardInner, PathField::ModSource, "input_mod_path");
    ImGui::SetNextItemWidth(cardInner);
    ImGui::InputTextWithHint("##modname", "Name (optional; the file or folder name otherwise)", modAddName_, sizeof modAddName_);
    auto_.registerWidget("input_mod_name");
    ImGui::PopStyleVar();
    if (theme::ghostButton("Add to the order", ImVec2(cardInner, S(28))) && modAddPath_[0]) {
        if (modAdd(modAddPath_, modAddName_)) { modAddPath_[0] = 0; modAddName_[0] = 0; }
    }
    auto_.registerWidget("btn_mod_add");
    ImGui::PushFont(fontSmall_);
    theme::hintMore("Point at the mod's pack file or folder.", "Drop the mod's archive contents somewhere and point at the pack file or folder; the order keeps the path and a hash of the contents. bsdiff patches need the retail file they were made against.");
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
    if (theme::dangerButton(busy && modsVerb_ == "undeploy" ? "Undeploying..." : "Undeploy (back to the retail files)", ImVec2(cardInner, S(28))) && !busy) runModsTool("undeploy");
    auto_.registerWidget("btn_mods_undeploy");
    ImGui::PushFont(fontSmall_);
    theme::hintMore("Deploy builds the whole list into the game; Undeploy puts the originals back.", "Deploy reverts the previous deploy first, builds the whole order with your picks and stages it (originals kept as .forgebak; Undeploy puts them back). Refused while Fable runs, and on an install EgoCore has deployed to (restore vanilla there first).");
    ImGui::PopFont();
    theme::endCard();
}

} // namespace albion::gui
