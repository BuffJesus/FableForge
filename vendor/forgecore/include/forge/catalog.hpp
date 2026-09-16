#pragma once
// Loader for the RE-derived modding catalog
// (FableTLC ghidra_out/installed_game/fqt_modding_catalog.json) — the read-only
// bootstrap index of the installed game: BIG banks, WAD/STB entries, WLD maps
// and regions, loose TNGs, chest/key/reward entities, and editor module plans.

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace forge::catalog {

struct BigBank {
    std::string bank;
    int64_t entries = 0;
    int64_t bytes = 0;
};

struct NamedCount {
    std::string name;
    int64_t count = 0;
};

struct EditorModule {
    std::string id;
    std::vector<std::string> sources;
    std::vector<std::string> firstTasks;
};

struct Catalog {
    std::string generatedOn;
    std::string gameRoot;
    std::map<std::string, std::string> inputs;
    std::map<std::string, int64_t> counts;
    std::vector<BigBank> bigBanks;
    std::vector<NamedCount> entityCategories;
    std::vector<NamedCount> entityDefinitionTypes;
    std::vector<EditorModule> editorModules;

    // 0 when the key is absent.
    int64_t count(const std::string& key) const;
};

// Throws std::runtime_error on unreadable file or malformed JSON.
Catalog load(const std::filesystem::path& path);
Catalog loadText(const std::string& json, const std::string& sourceName = {});

} // namespace forge::catalog
