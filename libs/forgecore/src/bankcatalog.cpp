#include "forge/bankcatalog.hpp"

#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace forge::bankcatalog {
namespace {

std::string readText(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("bankcatalog: cannot open " + path.string());
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

std::filesystem::path nativePath(const std::string& value) {
    std::string normalized = value;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    return std::filesystem::path(normalized);
}

Header parseHeader(const std::filesystem::path& path,
                   const std::string& relativePath) {
    static const std::regex enumPattern(
        R"re(^\s*(?:/\*\s*)?enum\s+([A-Za-z_][A-Za-z0-9_]*))re");
    static const std::regex namedPattern(
        R"re(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([0-9]+)\s*,?)re");
    static const std::regex pathPattern(
        R"re(^\s*\[(.+)\]\s*=\s*([0-9]+)\s*,?)re");

    Header header;
    header.relativePath = relativePath;
    std::istringstream lines(readText(path));
    std::string line;
    std::string currentEnum;
    std::smatch match;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (std::regex_search(line, match, enumPattern)) {
            currentEnum = match[1].str();
            continue;
        }
        Symbol symbol;
        if (std::regex_search(line, match, pathPattern)) {
            symbol.name = match[1].str();
            symbol.sourcePath = symbol.name;
            symbol.id = static_cast<uint32_t>(std::stoul(match[2].str()));
            symbol.enumName = currentEnum;
            symbol.named = false;
            ++header.pathCount;
        } else if (std::regex_search(line, match, namedPattern)) {
            symbol.name = match[1].str();
            symbol.id = static_cast<uint32_t>(std::stoul(match[2].str()));
            symbol.enumName = currentEnum;
            symbol.named = true;
            ++header.namedCount;
        } else {
            continue;
        }
        header.maxId = std::max(header.maxId, symbol.id);
        header.symbols.push_back(std::move(symbol));
    }
    return header;
}

} // namespace

const Symbol* Header::find(const std::string& name) const {
    const auto it = std::find_if(symbols.begin(), symbols.end(),
        [&](const Symbol& symbol) { return symbol.named && symbol.name == name; });
    return it == symbols.end() ? nullptr : &*it;
}

size_t Catalog::namedSymbolCount() const {
    size_t count = 0;
    for (const auto& [_, header] : headers) count += header.namedCount;
    return count;
}

size_t Catalog::pathSymbolCount() const {
    size_t count = 0;
    for (const auto& [_, header] : headers) count += header.pathCount;
    return count;
}

std::vector<Match> Catalog::resolve(const std::string& name) const {
    std::vector<Match> matches;
    for (const Route& route : routes) {
        const auto headerIt = headers.find(route.headerPath);
        if (headerIt == headers.end()) continue;
        if (const Symbol* symbol = headerIt->second.find(name)) {
            matches.push_back({&route, &headerIt->second, symbol});
        }
    }
    return matches;
}

Catalog load(const std::filesystem::path& rootOrBanksIni) {
    namespace fs = std::filesystem;
    fs::path ini = rootOrBanksIni;
    fs::path root;
    if (fs::is_directory(ini)) {
        if (fs::exists(ini / "banks.ini")) {
            root = ini;
            ini /= "banks.ini";
        } else if (fs::exists(ini / "UnifiedFable" / "banks.ini")) {
            root = ini / "UnifiedFable";
            ini = root / "banks.ini";
        } else {
            throw std::runtime_error("bankcatalog: banks.ini not found under " +
                                     rootOrBanksIni.string());
        }
    } else {
        root = ini.parent_path();
    }

    static const std::regex headerDirPattern(
        R"re(^\s*RetailHeaderDirectory\s+"([^"]+)")re");
    static const std::regex beginPattern(
        R"re(^\s*BeginRetailFile\s+"([^"]+)")re");
    static const std::regex routePattern(
        R"re(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*"([^"]+)"\s*,\s*"([^"]+)")re");

    Catalog catalog;
    catalog.root = root;
    std::istringstream lines(readText(ini));
    std::string line;
    std::string currentBig;
    std::smatch match;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (std::regex_search(line, match, headerDirPattern)) {
            catalog.retailHeaderDirectory = match[1].str();
        } else if (std::regex_search(line, match, beginPattern)) {
            currentBig = match[1].str();
        } else if (line.find("EndRetailFile") != std::string::npos) {
            currentBig.clear();
        } else if (!currentBig.empty() &&
                   std::regex_search(line, match, routePattern)) {
            Route route;
            route.bankSymbol = match[1].str();
            route.sourceBankPath = match[2].str();
            route.headerPath = match[3].str();
            route.bigPath = currentBig;
            catalog.routes.push_back(std::move(route));
        }
    }
    if (catalog.retailHeaderDirectory.empty()) {
        throw std::runtime_error("bankcatalog: RetailHeaderDirectory missing in " +
                                 ini.string());
    }
    if (catalog.routes.empty()) {
        throw std::runtime_error("bankcatalog: no retail bank routes in " + ini.string());
    }

    const fs::path headerRoot = root / nativePath(catalog.retailHeaderDirectory);
    for (const Route& route : catalog.routes) {
        if (catalog.headers.contains(route.headerPath)) continue;
        const fs::path headerPath = headerRoot / nativePath(route.headerPath);
        catalog.headers.emplace(route.headerPath,
                                parseHeader(headerPath, route.headerPath));
    }
    return catalog;
}

} // namespace forge::bankcatalog
