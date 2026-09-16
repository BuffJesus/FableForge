#include "forge/qst.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace forge::qst {
namespace {

// Longest first, so "AddTestQuest" is not decoded as "AddQuest".
constexpr std::string_view kKeywords[] = {"AddTestQuest", "AddQuest"};

bool isIdentChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

std::string upper(std::string_view text) {
    std::string out(text);
    for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    return text;
}

size_t lineOf(const std::string& text, size_t idx) {
    size_t line = 1;
    for (size_t i = 0; i < idx && i < text.size(); ++i)
        if (text[i] == '\n') ++line;
    return line;
}

// Recognized keyword starting at pos with identifier boundaries on both sides.
std::string_view keywordAt(const std::string& text, size_t pos) {
    for (std::string_view kw : kKeywords) {
        if (text.compare(pos, kw.size(), kw) != 0) continue;
        const bool beforeOk = pos == 0 || !isIdentChar(text[pos - 1]);
        const size_t after = pos + kw.size();
        const bool afterOk = after >= text.size() || !isIdentChar(text[after]);
        if (beforeOk && afterOk) return kw;
    }
    return {};
}

// Parse '<kw> ( args ) ;' beginning at `start`. On success fills `stmt` (raw =
// exact source of the whole call incl. ';', args decoded with quotes stripped)
// and sets `end` one past it. Returns false if it is not a well-formed call.
bool parseCall(const std::string& text, size_t start, std::string_view kw,
               Statement& stmt, size_t& end) {
    const size_t n = text.size();
    size_t i = start + kw.size();
    while (i < n && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' ||
                     text[i] == '\n'))
        ++i;
    if (i >= n || text[i] != '(') return false;
    ++i;

    std::vector<std::string> args;
    std::string cur;
    int depth = 1;
    bool inStr = false;
    bool closed = false;
    while (i < n) {
        const char c = text[i];
        if (inStr) {
            if (c == '\\' && i + 1 < n) { cur += text[i + 1]; i += 2; continue; }
            if (c == '"') { inStr = false; ++i; continue; }
            cur += c; ++i; continue;
        }
        if (c == '"') { inStr = true; ++i; continue; }
        if (c == '(') { ++depth; cur += c; ++i; continue; }
        if (c == ')') {
            if (--depth == 0) {
                args.emplace_back(trim(cur));
                ++i;
                closed = true;
                break;
            }
            cur += c; ++i; continue;
        }
        if (c == ',' && depth == 1) {
            args.emplace_back(trim(cur));
            cur.clear(); ++i; continue;
        }
        cur += c; ++i;
    }
    if (!closed) return false;
    while (i < n && (text[i] == ' ' || text[i] == '\t')) ++i;
    if (i < n && text[i] == ';') ++i;

    stmt.keyword = std::string(kw);
    stmt.args = std::move(args);
    stmt.raw = text.substr(start, i - start);
    end = i;
    return true;
}

void validate(const Statement& stmt) {
    if (stmt.keyword == "AddQuest") {
        const std::string flag = stmt.args.size() > 1 ? upper(stmt.args[1]) : "";
        if (stmt.args.size() != 2 || (flag != "TRUE" && flag != "FALSE"))
            throw std::runtime_error("qst: line " + std::to_string(stmt.line) +
                                     ": bad AddQuest args in '" + stmt.raw + "'");
        return;
    }
    if (stmt.args.size() != 7)
        throw std::runtime_error("qst: line " + std::to_string(stmt.line) +
                                 ": AddTestQuest needs 7 args, got " +
                                 std::to_string(stmt.args.size()));
    const std::string& mode = stmt.args[2];
    char* parseEnd = nullptr;
    std::strtol(mode.c_str(), &parseEnd, 10);
    if (mode.empty() || parseEnd != mode.c_str() + mode.size())
        throw std::runtime_error("qst: line " + std::to_string(stmt.line) +
                                 ": AddTestQuest arg 3 not an integer: '" +
                                 mode + "'");
}

// Byte span [tokenStart, tokenEnd) of the second argument's token inside an
// AddQuest statement's raw text (the TRUE/FALSE bareword).
bool secondArgSpan(const std::string& raw, size_t& tokenStart, size_t& tokenEnd) {
    const size_t open = raw.find('(');
    if (open == std::string::npos) return false;
    size_t i = open + 1;
    int depth = 1;
    bool inStr = false;
    size_t argStart = std::string::npos;
    size_t argEnd = std::string::npos;
    while (i < raw.size()) {
        const char c = raw[i];
        if (inStr) {
            if (c == '\\' && i + 1 < raw.size()) { i += 2; continue; }
            if (c == '"') inStr = false;
            ++i; continue;
        }
        if (c == '"') { inStr = true; ++i; continue; }
        if (c == '(') { ++depth; ++i; continue; }
        if (c == ')') {
            if (--depth == 0) {
                if (argStart != std::string::npos) argEnd = i;
                break;
            }
            ++i; continue;
        }
        if (c == ',' && depth == 1 && argStart == std::string::npos) {
            argStart = i + 1;
        }
        ++i;
    }
    if (argStart == std::string::npos || argEnd == std::string::npos) return false;
    while (argStart < argEnd &&
           std::isspace(static_cast<unsigned char>(raw[argStart])))
        ++argStart;
    while (argEnd > argStart &&
           std::isspace(static_cast<unsigned char>(raw[argEnd - 1])))
        --argEnd;
    tokenStart = argStart;
    tokenEnd = argEnd;
    return argStart < argEnd;
}

// Stable identity for an AddTestQuest across files: keyword + decoded args.
std::string signature(const Statement& stmt) {
    std::string sig = stmt.keyword;
    for (const std::string& arg : stmt.args) {
        sig += '\x1f';
        sig += arg;
    }
    return sig;
}

} // namespace

bool Statement::active() const {
    return args.size() > 1 && upper(args[1]) == "TRUE";
}

File File::parse(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
        throw std::runtime_error("qst: cannot open " + path.string());
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return parseText(buffer.str(), path.filename().string());
}

File File::parseText(std::string text, std::string sourceName) {
    File result;
    result.source_ = std::move(sourceName);

    // Dominant line terminator, for lines mutations add.
    size_t crlf = 0, lf = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\n') continue;
        if (i > 0 && text[i - 1] == '\r') ++crlf; else ++lf;
    }
    result.lineTerminator_ = (lf > crlf) ? "\n" : "\r\n";

    const size_t n = text.size();
    size_t i = 0, gapStart = 0;
    while (i < n) {
        const char c = text[i];
        // Comments are part of the surrounding gap, so a commented-out call is
        // not decoded (grammar per QST_FORMAT.md / SilverChest cross-check).
        if (c == '/' && i + 1 < n && text[i + 1] == '/') {
            const size_t j = text.find('\n', i);
            i = (j == std::string::npos) ? n : j + 1;
            continue;
        }
        if (c == '/' && i + 1 < n && text[i + 1] == '*') {
            const size_t j = text.find("*/", i + 2);
            i = (j == std::string::npos) ? n : j + 2;
            continue;
        }
        const std::string_view kw = keywordAt(text, i);
        if (kw.empty()) { ++i; continue; }
        Statement stmt;
        size_t end = 0;
        if (!parseCall(text, i, kw, stmt, end)) {
            i += kw.size(); // keyword not followed by a valid call: gap text
            continue;
        }
        stmt.line = lineOf(text, i);
        validate(stmt);
        if (gapStart < i)
            result.elements_.push_back({-1, text.substr(gapStart, i - gapStart)});
        result.elements_.push_back(
            {static_cast<ptrdiff_t>(result.statements_.size()), {}});
        result.statements_.push_back(std::move(stmt));
        i = end;
        gapStart = end;
    }
    if (gapStart < n)
        result.elements_.push_back({-1, text.substr(gapStart)});
    return result;
}

size_t File::questCount() const {
    size_t count = 0;
    for (const Statement& s : statements_) count += s.isQuest() ? 1 : 0;
    return count;
}

size_t File::testQuestCount() const {
    return statements_.size() - questCount();
}

const Statement* File::findQuest(std::string_view name) const {
    for (const Statement& s : statements_)
        if (s.isQuest() && s.name() == name) return &s;
    return nullptr;
}

std::string File::serialize() const {
    std::string out;
    size_t total = 0;
    for (const Element& e : elements_)
        total += e.stmt >= 0 ? statements_[static_cast<size_t>(e.stmt)].raw.size()
                             : e.gap.size();
    out.reserve(total);
    for (const Element& e : elements_)
        out += e.stmt >= 0 ? statements_[static_cast<size_t>(e.stmt)].raw : e.gap;
    return out;
}

bool File::setQuestActive(std::string_view name, bool active) {
    for (Statement& s : statements_) {
        if (!s.isQuest() || s.name() != name) continue;
        if (s.active() == active) return true; // already the requested value
        size_t tokenStart = 0, tokenEnd = 0;
        if (!secondArgSpan(s.raw, tokenStart, tokenEnd))
            throw std::runtime_error("qst: cannot locate flag token in '" +
                                     s.raw + "'");
        s.raw.replace(tokenStart, tokenEnd - tokenStart,
                      active ? "TRUE" : "FALSE");
        s.args[1] = active ? "TRUE" : "FALSE";
        return true;
    }
    return false;
}

void File::addQuest(std::string_view name, bool active) {
    std::string raw = "AddQuest(\"";
    raw += name;
    raw += "\", ";
    raw += active ? "TRUE" : "FALSE";
    raw += ");";
    appendRaw(std::move(raw), "AddQuest",
              {std::string(name), active ? "TRUE" : "FALSE"});
}

void File::appendStatement(std::string rawStatement) {
    const File parsed = parseText(std::move(rawStatement));
    if (parsed.statements_.size() != 1)
        throw std::runtime_error(
            "qst: appendStatement needs exactly one statement, got " +
            std::to_string(parsed.statements_.size()));
    const Statement& s = parsed.statements_.front();
    appendRaw(s.raw, s.keyword, s.args);
}

void File::appendRaw(std::string rawStatement, std::string keyword,
                     std::vector<std::string> args) {
    // The new statement starts on its own line and re-adds the trailing
    // terminator both retail files end with.
    if (!elements_.empty()) {
        const Element& last = elements_.back();
        const bool endsWithNewline =
            last.stmt < 0 && !last.gap.empty() && last.gap.back() == '\n';
        if (!endsWithNewline) elements_.push_back({-1, lineTerminator_});
    }
    Statement stmt;
    stmt.keyword = std::move(keyword);
    stmt.args = std::move(args);
    stmt.raw = std::move(rawStatement);
    validate(stmt);
    elements_.push_back({static_cast<ptrdiff_t>(statements_.size()), {}});
    statements_.push_back(std::move(stmt));
    elements_.push_back({-1, lineTerminator_});
}

MergeResult merge(const File& base, const std::vector<MergeInput>& mods,
                  const std::map<std::string, std::string>& picks) {
    MergeResult result;
    result.merged = base;

    // Base state: quest name -> flag (first occurrence wins; retail names are
    // unique), plus the identity set of base AddTestQuest statements.
    std::map<std::string, bool> baseFlag;
    std::set<std::string> baseSigs;
    for (const Statement& s : base.statements()) {
        if (s.isQuest()) baseFlag.emplace(s.name(), s.active());
        else baseSigs.insert(signature(s));
    }

    // Gather every mod version that flips or adds each quest, in load order.
    struct Version { std::string mod; bool active; std::string raw; };
    std::map<std::string, std::vector<Version>> byName;
    std::vector<std::string> nameOrder; // first-seen order for stable output
    std::vector<std::string> testAdds;  // raw AddTestQuest texts to append
    std::set<std::string> testSeen;
    for (const MergeInput& mod : mods) {
        for (const Statement& s : mod.file->statements()) {
            if (!s.isQuest()) {
                const std::string sig = signature(s);
                if (baseSigs.count(sig) != 0 || !testSeen.insert(sig).second)
                    continue; // already in base / queued by an earlier mod
                testAdds.push_back(s.raw);
                continue;
            }
            const auto bit = baseFlag.find(s.name());
            const bool isAdd = bit == baseFlag.end();
            if (!isAdd && s.active() == bit->second) continue; // same as base
            if (byName.find(s.name()) == byName.end()) nameOrder.push_back(s.name());
            byName[s.name()].push_back({mod.label, s.active(), s.raw});
        }
    }

    auto apply = [&](const std::string& name, const Version& v) {
        if (baseFlag.count(name) != 0) {
            result.merged.setQuestActive(name, v.active);
        } else {
            result.merged.appendStatement(v.raw); // keep the mod's formatting
            ++result.addedQuests;
        }
        ++result.applied;
    };

    for (const std::string& name : nameOrder) {
        const auto& vers = byName[name];
        // Single mod touches it: apply as-is (no conflict).
        if (vers.size() == 1) {
            apply(name, vers.front());
            continue;
        }
        // >1 mod wants this quest: load-order winner unless a pick overrides.
        MergeConflict c;
        c.name = name;
        for (const Version& v : vers) c.wanted.emplace_back(v.mod, v.active);
        const Version* winner = &vers.back();
        bool keepVanilla = false;
        const auto pick = picks.find(name);
        if (pick != picks.end()) {
            c.overridden = true;
            if (pick->second == "vanilla") {
                keepVanilla = true;
            } else {
                for (const Version& v : vers)
                    if (v.mod == pick->second) winner = &v;
            }
        }
        c.winner = keepVanilla ? "vanilla" : winner->mod;
        if (!keepVanilla) apply(name, *winner);
        result.conflicts.push_back(std::move(c));
    }

    for (const std::string& raw : testAdds) {
        result.merged.appendStatement(raw);
        ++result.addedTests;
    }
    return result;
}

} // namespace forge::qst
