// forge::questdeploy — see questdeploy.hpp for the three-part FSE invariant.
#include "forge/questdeploy.hpp"

#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

#include "forge/qst.hpp"

namespace fs = std::filesystem;

namespace forge::questdeploy {
namespace {

[[noreturn]] void fail(const std::string& what) {
    throw std::runtime_error(what);
}

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) fail("cannot open " + path.string());
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

bool isIdentStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}
bool isIdentChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

// --- minimal Lua-source scanning (strings, comments, balanced tables) --------

// Returns the index just past a quoted string starting at s[i] (a ' or ").
size_t skipString(const std::string& s, size_t i) {
    const char quote = s[i++];
    while (i < s.size()) {
        if (s[i] == '\\') { i += 2; continue; }
        if (s[i] == quote) return i + 1;
        ++i;
    }
    fail("unterminated string in quests.lua");
}

// Skips whitespace and Lua comments (both `-- line` and `--[[ block ]]`).
size_t skipWs(const std::string& s, size_t i) {
    while (i < s.size()) {
        const char c = s[i];
        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }
        if (c == '-' && i + 1 < s.size() && s[i + 1] == '-') {
            i += 2;
            if (i + 1 < s.size() && s[i] == '[' && s[i + 1] == '[') {
                const size_t close = s.find("]]", i + 2);
                if (close == std::string::npos) fail("unterminated --[[ comment");
                i = close + 2;
            } else {
                while (i < s.size() && s[i] != '\n') ++i;
            }
            continue;
        }
        break;
    }
    return i;
}

// s[open] == '{'; returns the index of the matching '}'.
size_t matchBrace(const std::string& s, size_t open) {
    int depth = 0;
    size_t i = open;
    while (i < s.size()) {
        const char c = s[i];
        if (c == '"' || c == '\'') { i = skipString(s, i); continue; }
        if (c == '-' && i + 1 < s.size() && s[i + 1] == '-') {
            i = skipWs(s, i);
            continue;
        }
        if (c == '{') ++depth;
        if (c == '}' && --depth == 0) return i;
        ++i;
    }
    fail("unbalanced braces in quests.lua");
}

std::string decodeString(const std::string& s, size_t i, size_t end) {
    // [i, end) covers the quotes; decode simple escapes.
    std::string out;
    for (size_t p = i + 1; p + 1 < end; ++p) {
        if (s[p] == '\\' && p + 2 < end) { out += s[++p]; continue; }
        out += s[p];
    }
    return out;
}

struct Field {
    std::string key;
    std::string stringValue;  // decoded, when the value is a string
    long long number = 0;
    bool isString = false, isNumber = false, isTrue = false;
    size_t keyBegin = 0;   // start of the key identifier
    size_t valueEnd = 0;   // just past the value
    bool isTable = false;
    size_t tableOpen = 0, tableClose = 0;  // when isTable
};

// Parses the fields of a table constructor body [begin, end) at depth 0.
// Positional (key-less) sub-tables get key "" — used by entity_scripts lists.
std::vector<Field> parseFields(const std::string& s, size_t begin, size_t end) {
    std::vector<Field> fields;
    size_t i = skipWs(s, begin);
    while (i < end) {
        if (s[i] == ',' || s[i] == ';') { i = skipWs(s, i + 1); continue; }
        Field f;
        f.keyBegin = i;
        if (isIdentStart(s[i])) {
            size_t k = i;
            while (k < end && isIdentChar(s[k])) ++k;
            const size_t afterKey = skipWs(s, k);
            if (afterKey < end && s[afterKey] == '=') {
                f.key = s.substr(i, k - i);
                i = skipWs(s, afterKey + 1);
            } else {
                // bare identifier value (e.g. a constant) — skip it
                i = skipWs(s, k);
                continue;
            }
        }
        if (i >= end) break;
        const char c = s[i];
        if (c == '"' || c == '\'') {
            const size_t past = skipString(s, i);
            f.isString = true;
            f.stringValue = decodeString(s, i, past);
            f.valueEnd = past;
        } else if (c == '{') {
            f.isTable = true;
            f.tableOpen = i;
            f.tableClose = matchBrace(s, i);
            f.valueEnd = f.tableClose + 1;
        } else if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
            size_t p = i + (c == '-' ? 1 : 0);
            while (p < end && (std::isdigit(static_cast<unsigned char>(s[p])) ||
                               s[p] == '.' || s[p] == 'x' || s[p] == 'X' ||
                               (s[p] >= 'a' && s[p] <= 'f') ||
                               (s[p] >= 'A' && s[p] <= 'F')))
                ++p;
            f.isNumber = true;
            f.number = std::stoll(s.substr(i, p - i), nullptr, 0);
            f.valueEnd = p;
        } else if (isIdentStart(c)) {
            size_t p = i;
            while (p < end && isIdentChar(s[p])) ++p;
            f.isTrue = s.substr(i, p - i) == "true";
            f.valueEnd = p;
        } else {
            fail("unsupported token in quests.lua at offset " +
                 std::to_string(i));
        }
        fields.push_back(f);
        i = skipWs(s, f.valueEnd);
    }
    return fields;
}

std::string dominantEol(const std::string& s) {
    size_t crlf = 0, lf = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\n') continue;
        (i > 0 && s[i - 1] == '\r') ? ++crlf : ++lf;
    }
    return crlf > lf ? "\r\n" : "\n";
}

bool validQuestIdent(const std::string& name) {
    if (name.empty() || !isIdentStart(name[0])) return false;
    for (char c : name)
        if (!isIdentChar(c)) return false;
    return true;
}

} // namespace

// --- QuestsLua ----------------------------------------------------------------

QuestsLua QuestsLua::parse(const fs::path& path) {
    return parseText(readFile(path), path.string());
}

QuestsLua QuestsLua::parseText(std::string text, std::string sourceName) {
    QuestsLua out;
    out.source_ = std::move(text);
    out.eol_ = dominantEol(out.source_);
    const std::string& s = out.source_;
    const std::string where = sourceName.empty() ? "quests.lua" : sourceName;

    // Locate the top-level `Quests = {` assignment.
    size_t tableOpen = std::string::npos;
    for (size_t i = 0; i < s.size();) {
        i = skipWs(s, i);
        if (i >= s.size()) break;
        if (s[i] == '"' || s[i] == '\'') { i = skipString(s, i); continue; }
        if (isIdentStart(s[i])) {
            size_t k = i;
            while (k < s.size() && isIdentChar(s[k])) ++k;
            if (s.substr(i, k - i) == "Quests") {
                size_t p = skipWs(s, k);
                if (p < s.size() && s[p] == '=') {
                    p = skipWs(s, p + 1);
                    if (p < s.size() && s[p] == '{') {
                        tableOpen = p;
                        break;
                    }
                }
            }
            i = k;
            continue;
        }
        ++i;
    }
    if (tableOpen == std::string::npos)
        fail(where + ": no top-level `Quests = { ... }` table found");
    out.tableOpen_ = tableOpen;
    out.tableClose_ = matchBrace(s, tableOpen);

    // Each entry: `Key = { ... }`.
    size_t i = skipWs(s, tableOpen + 1);
    while (i < out.tableClose_) {
        if (s[i] == ',' || s[i] == ';') { i = skipWs(s, i + 1); continue; }
        if (!isIdentStart(s[i]))
            fail(where + ": unsupported Quests entry at offset " +
                 std::to_string(i));
        QuestRef ref;
        ref.begin = i;
        size_t k = i;
        while (k < out.tableClose_ && isIdentChar(s[k])) ++k;
        ref.key = s.substr(i, k - i);
        size_t p = skipWs(s, k);
        if (p >= out.tableClose_ || s[p] != '=')
            fail(where + ": expected `=` after Quests key " + ref.key);
        p = skipWs(s, p + 1);
        if (p >= out.tableClose_ || s[p] != '{')
            fail(where + ": Quests entry " + ref.key +
                 " is not a table constructor");
        const size_t bodyClose = matchBrace(s, p);
        ref.end = bodyClose + 1;

        for (const Field& f : parseFields(s, p + 1, bodyClose)) {
            if (f.key == "name" && f.isString) ref.name = f.stringValue;
            else if (f.key == "file" && f.isString) ref.file = f.stringValue;
            else if (f.key == "id" && f.isNumber) {
                ref.id = f.number;
                ref.hasId = true;
            } else if (f.key == "master") ref.master = f.isTrue;
            else if (f.key == "entity_scripts" && f.isTable) {
                ref.esBegin = f.keyBegin;
                ref.esEnd = f.valueEnd;
                for (const Field& e :
                     parseFields(s, f.tableOpen + 1, f.tableClose)) {
                    if (!e.isTable) continue;
                    EntityScriptRef es;
                    for (const Field& g :
                         parseFields(s, e.tableOpen + 1, e.tableClose)) {
                        if (g.key == "name" && g.isString) es.name = g.stringValue;
                        else if (g.key == "file" && g.isString)
                            es.file = g.stringValue;
                        else if (g.key == "id" && g.isNumber) es.id = g.number;
                    }
                    ref.entityScripts.push_back(std::move(es));
                }
            }
        }
        if (ref.name.empty()) ref.name = ref.key;
        out.quests_.push_back(std::move(ref));
        i = skipWs(s, bodyClose + 1);
    }
    return out;
}

const QuestRef* QuestsLua::find(std::string_view name) const {
    for (const auto& q : quests_)
        if (q.name == name || q.key == name) return &q;
    return nullptr;
}

long long QuestsLua::nextFreeId(long long min) const {
    std::set<long long> taken;
    for (const auto& q : quests_) {
        if (q.hasId) taken.insert(q.id);
        for (const auto& es : q.entityScripts) taken.insert(es.id);
    }
    long long id = min;
    while (taken.count(id)) ++id;
    return id;
}

std::string QuestsLua::withQuest(const std::string& name,
                                 const std::string& file, long long id) const {
    const QuestRef* existing = find(name);

    // Install-style entry body (4-space key indent, 8-space fields). An
    // existing entity_scripts field is spliced back in verbatim.
    std::string entry = name + " = {" + eol_;
    entry += "        name = \"" + name + "\"," + eol_;
    entry += "        file = \"" + file + "\"," + eol_;
    entry += "        id = " + std::to_string(id) + "," + eol_;
    if (existing && existing->esEnd > existing->esBegin) {
        entry += "        " +
                 source_.substr(existing->esBegin,
                                existing->esEnd - existing->esBegin) +
                 eol_;
    }
    entry += "    }";

    if (existing) {
        return source_.substr(0, existing->begin) + entry +
               source_.substr(existing->end);
    }

    // Append before the table's closing brace. Ensure the previous entry has
    // a separating comma (the gap after it holds only whitespace/comments).
    std::string out = source_.substr(0, tableClose_);
    size_t splitAt = tableClose_;
    bool needComma = false;
    if (!quests_.empty()) {
        const size_t lastEnd = quests_.back().end;
        const size_t next = skipWs(source_, lastEnd);
        needComma = next >= tableClose_ || source_[next] != ',';
        if (needComma) {
            out = source_.substr(0, lastEnd) + "," +
                  source_.substr(lastEnd, tableClose_ - lastEnd);
            splitAt = tableClose_;
        }
    }
    std::string insertion;
    if (out.empty() || out.back() != '\n') insertion += eol_;
    insertion += "    " + entry + "," + eol_;
    return out + insertion + source_.substr(splitAt);
}

// --- deploy --------------------------------------------------------------------

DeployPlan planDeploy(const questnodes::Graph& graph, const fs::path& gameRoot,
                      const DeployOptions& options) {
    if (!validQuestIdent(graph.questName))
        fail("quest name \"" + graph.questName +
             "\" is not a valid identifier (needed as a Lua table key)");

    DeployPlan plan;
    plan.questName = graph.questName;

    // (2) FSE quest registry — must exist: its absence means this is not an
    // FSE-enabled game root, and deploying would be pointless.
    plan.registry.path = gameRoot / "FSE" / "quests.lua";
    if (!fs::exists(plan.registry.path))
        fail(plan.registry.path.string() +
             " not found — is FSE/ForgeFSE installed in this game root?");
    plan.registry.existed = true;
    plan.registry.before = readFile(plan.registry.path);
    const auto registry = QuestsLua::parseText(plan.registry.before,
                                               plan.registry.path.string());
    const QuestRef* existing = registry.find(plan.questName);

    // Quest id: explicit > existing entry's > next free >= 50000.
    if (options.id >= 0) {
        for (const auto& q : registry.quests()) {
            if (q.hasId && q.id == options.id && q.name != plan.questName)
                fail("--id " + std::to_string(options.id) +
                     " is already taken by quest " + q.name);
            for (const auto& es : q.entityScripts)
                if (es.id == options.id && q.name != plan.questName)
                    fail("--id " + std::to_string(options.id) +
                         " is already taken by entity script " + es.name);
        }
        plan.id = options.id;
    } else if (existing && existing->hasId) {
        plan.id = existing->id;
    } else {
        plan.id = registry.nextFreeId();
        plan.idWasAuto = true;
    }

    // (1) Compile the graph (hard-fail on errors; warnings are reported).
    questnodes::CompileOptions compileOptions;
    questnodes::Registry nodeRegistry =
        options.manifest ? questnodes::Registry::withManifest(*options.manifest)
                         : questnodes::Registry::curated();
    compileOptions.validateAgainst = options.manifest;
    const auto compiled =
        questnodes::compileEntityScript(graph, nodeRegistry, compileOptions);
    plan.warnings = compiled.warnings;

    const std::string fseFile = plan.questName + "/" + plan.questName;
    plan.script.path =
        gameRoot / "FSE" / plan.questName / (plan.questName + ".lua");
    plan.script.existed = fs::exists(plan.script.path);
    if (plan.script.existed) plan.script.before = readFile(plan.script.path);
    plan.script.after = compiled.lua;

    plan.registry.after = registry.withQuest(plan.questName, fseFile, plan.id);

    // (3) Engine activation list.
    plan.qst.path = gameRoot / "data" / "Levels" / "FinalAlbion.qst";
    if (!fs::exists(plan.qst.path))
        fail(plan.qst.path.string() + " not found — not a Fable game root?");
    plan.qst.existed = true;
    plan.qst.before = readFile(plan.qst.path);
    auto qstFile =
        forge::qst::File::parseText(plan.qst.before, plan.qst.path.string());
    if (const auto* stmt = qstFile.findQuest(plan.questName)) {
        plan.active = options.activeFlag < 0 ? stmt->active()
                                             : options.activeFlag != 0;
        if (plan.active != stmt->active())
            qstFile.setQuestActive(plan.questName, plan.active);
    } else {
        // New quests default to dormant (FALSE), matching FSE's own examples;
        // FSE activates them when the script asks.
        plan.active = options.activeFlag == 1;
        qstFile.addQuest(plan.questName, plan.active);
    }
    plan.qst.after = qstFile.serialize();
    return plan;
}

std::vector<fs::path> applyDeploy(const DeployPlan& plan) {
    std::vector<fs::path> written;
    for (const FileChange* change :
         {&plan.script, &plan.registry, &plan.qst}) {
        if (!change->changed()) continue;
        fs::create_directories(change->path.parent_path());
        if (change->existed) {
            fs::copy_file(change->path,
                          fs::path(change->path.string() + ".bak"),
                          fs::copy_options::overwrite_existing);
        }
        std::ofstream out(change->path, std::ios::binary);
        if (!out) fail("cannot write " + change->path.string());
        out << change->after;
        written.push_back(change->path);
    }
    return written;
}

// --- doctor --------------------------------------------------------------------

DoctorReport doctor(const fs::path& gameRoot) {
    DoctorReport report;
    report.questsLuaPath = gameRoot / "FSE" / "quests.lua";
    report.qstPath = gameRoot / "data" / "Levels" / "FinalAlbion.qst";

    if (!fs::exists(report.questsLuaPath)) {
        report.issues.push_back(
            {"", report.questsLuaPath.string() + " missing",
             "install FSE/ForgeFSE (FSE folder next to Fable.exe)"});
        return report;
    }
    const auto registry = QuestsLua::parse(report.questsLuaPath);
    report.questsChecked = registry.quests().size();

    bool haveQst = fs::exists(report.qstPath);
    forge::qst::File qstFile;
    if (haveQst) {
        qstFile = forge::qst::File::parse(report.qstPath);
    } else {
        report.issues.push_back(
            {"", report.qstPath.string() + " missing",
             "not a Fable game root, or the data folder is damaged"});
    }

    std::map<long long, std::string> idOwner;
    auto claimId = [&](long long id, const std::string& owner,
                       const std::string& quest) {
        auto [it, fresh] = idOwner.emplace(id, owner);
        if (!fresh) {
            report.issues.push_back(
                {quest,
                 "id " + std::to_string(id) + " used by both " + it->second +
                     " and " + owner,
                 "give one of them a unique id (forge quest deploy --id N, "
                 "or edit quests.lua)"});
        }
    };

    for (const auto& q : registry.quests()) {
        // 1. compiled Lua present (quest + entity scripts)
        const fs::path script = gameRoot / "FSE" / (q.file + ".lua");
        if (!fs::exists(script)) {
            report.issues.push_back(
                {q.name, "script missing: " + script.string(),
                 "recompile and redeploy (forge quest deploy <graph.json> "
                 "<game-root>)"});
        }
        for (const auto& es : q.entityScripts) {
            const fs::path esPath = gameRoot / "FSE" / (es.file + ".lua");
            if (!fs::exists(esPath)) {
                report.issues.push_back(
                    {q.name,
                     "entity script missing: " + esPath.string(),
                     "restore " + es.file + ".lua under FSE\\"});
            }
        }
        // 2. ids unique
        if (q.hasId) claimId(q.id, "quest " + q.name, q.name);
        for (const auto& es : q.entityScripts)
            claimId(es.id, "entity script " + es.name, q.name);
        // 3. AddQuest line in the engine activation list
        if (haveQst && !qstFile.findQuest(q.name)) {
            report.issues.push_back(
                {q.name,
                 "no AddQuest(\"" + q.name + "\", ...) line in " +
                     report.qstPath.string() +
                     " — quest registers but never runs (a Steam integrity "
                     "verify silently wipes custom lines)",
                 "append AddQuest(\"" + q.name +
                     "\", FALSE); (forge quest deploy re-adds it)"});
        }
    }
    return report;
}

} // namespace forge::questdeploy
