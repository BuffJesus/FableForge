#include "forge/themepalette.hpp"

#include <memory>
#include <sstream>
#include <unordered_map>

namespace fs = std::filesystem;

namespace forge::themepalette {

lev::DefIndexTable makeDefIndexTable(const bin::File& defs) {
    // Build once; the lambdas below capture these by value so the table stays
    // usable after this call returns (they do not touch `defs` again).
    auto byName = std::make_shared<std::unordered_map<std::string, uint32_t>>();
    auto typeByIndex = std::make_shared<std::vector<std::string>>();
    const auto& entries = defs.entries();
    typeByIndex->reserve(entries.size());
    for (size_t i = 0; i < entries.size(); ++i) {
        typeByIndex->push_back(entries[i].definition);
        if (entries[i].name.empty()) continue;
        // Retail names are unique; keep the first if a mod ever duplicates one.
        byName->emplace(entries[i].name, static_cast<uint32_t>(i));
    }

    lev::DefIndexTable table;
    table.indexOf = [byName](const std::string& name, uint32_t& index) {
        const auto it = byName->find(name);
        if (it == byName->end()) return false;
        index = it->second;
        return true;
    };
    table.typeOf = [typeByIndex](uint32_t index) -> std::string {
        if (index >= typeByIndex->size()) return "<out of range>";
        return (*typeByIndex)[index];
    };
    return table;
}

bin::File openDefBank(const fs::path& gameRoot, const std::string& binName) {
    const fs::path defsDir = gameRoot / "data" / "CompiledDefs";
    return bin::File::open(defsDir / "names.bin", defsDir / binName);
}

namespace {

LevelReport buildReport(const fs::path& path, const lev::File& level,
                        const lev::DefIndexTable& defs) {
    LevelReport report;
    report.path = path;
    for (const lev::GroundTheme& theme : level.groundThemes()) {
        if (!theme.name.empty()) ++report.namedSlots;
    }
    report.issues = level.auditThemePalette(defs);
    for (const lev::ThemePaletteIssue& issue : report.issues) {
        if (issue.resolvable) ++report.wrong;
        else ++report.unresolvable;
    }
    return report;
}

} // namespace

LevelReport auditLevel(const fs::path& levelPath, const lev::DefIndexTable& defs) {
    const lev::File level = lev::File::open(levelPath);
    return buildReport(levelPath, level, defs);
}

LevelReport rebaseLevel(const fs::path& levelPath, const lev::DefIndexTable& defs) {
    lev::File level = lev::File::open(levelPath);
    LevelReport before = buildReport(levelPath, level, defs);
    if (before.wrong > 0) {
        level.rebaseThemePalette(defs);
        level.save(levelPath);
    }
    return before;
}

std::string formatIssue(const lev::ThemePaletteIssue& issue) {
    std::ostringstream out;
    out << "slot " << issue.slot << "  " << issue.name << "  stored " << issue.stored;
    if (issue.resolvable) {
        out << " -> " << issue.expected;
        if (!issue.storedDefType.empty()) out << "  (was " << issue.storedDefType << ")";
    } else {
        out << "  NO SUCH THEME in the target install";
    }
    return out.str();
}

} // namespace forge::themepalette
