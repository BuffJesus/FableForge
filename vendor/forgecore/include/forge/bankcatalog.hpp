#pragma once
// Unified-build bank/header catalog. Joins banks.ini routes to BankCreator's
// RetailHeaders so source-level symbols can be resolved to a concrete BIG,
// sub-bank, header enum, and one-based slot ID.

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace forge::bankcatalog {

struct Symbol {
    std::string name;
    std::string enumName;
    std::string sourcePath;
    uint32_t id = 0;
    bool named = true;
};

struct Header {
    std::string relativePath;
    std::vector<Symbol> symbols;
    uint32_t maxId = 0;
    size_t namedCount = 0;
    size_t pathCount = 0;

    const Symbol* find(const std::string& name) const;
};

struct Route {
    std::string bankSymbol;
    std::string bigPath;
    std::string sourceBankPath;
    std::string headerPath;
};

struct Match {
    const Route* route = nullptr;
    const Header* header = nullptr;
    const Symbol* symbol = nullptr;
};

struct Catalog {
    std::filesystem::path root;
    std::string retailHeaderDirectory;
    std::vector<Route> routes;
    std::map<std::string, Header> headers;

    size_t namedSymbolCount() const;
    size_t pathSymbolCount() const;
    std::vector<Match> resolve(const std::string& symbol) const;
};

// Accepts either the UnifiedFable directory or its banks.ini path. A parent
// directory containing UnifiedFable/banks.ini is accepted for convenience.
// Throws std::runtime_error for malformed routing rows or missing headers.
Catalog load(const std::filesystem::path& rootOrBanksIni);

} // namespace forge::bankcatalog
