#include "forge/catalog.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace forge::catalog {
namespace {

// Catalog numbers appear both as JSON numbers and as decimal strings
// (PowerShell's ConvertTo-Json emits CSV-derived values as strings).
int64_t asInt64(const json& value, int64_t fallback = 0) {
    if (value.is_number_integer()) return value.get<int64_t>();
    if (value.is_number()) return static_cast<int64_t>(value.get<double>());
    if (value.is_string()) {
        try {
            return std::stoll(value.get<std::string>());
        } catch (...) {
            return fallback;
        }
    }
    return fallback;
}

std::vector<NamedCount> parseNamedCounts(const json& array) {
    std::vector<NamedCount> result;
    if (!array.is_array()) return result;
    for (const json& item : array) {
        NamedCount entry;
        entry.name = item.value("name", std::string());
        entry.count = asInt64(item.value("count", json()));
        result.push_back(std::move(entry));
    }
    return result;
}

} // namespace

int64_t Catalog::count(const std::string& key) const {
    auto it = counts.find(key);
    return it == counts.end() ? 0 : it->second;
}

Catalog load(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("catalog: cannot open " + path.string());
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return loadText(buffer.str(), path.string());
}

Catalog loadText(const std::string& text, const std::string& sourceName) {
    json root = json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        throw std::runtime_error("catalog: malformed JSON in " + sourceName);
    }

    Catalog catalog;
    catalog.generatedOn = root.value("generated_on", std::string());
    catalog.gameRoot = root.value("game_root", std::string());

    if (const auto it = root.find("inputs"); it != root.end() && it->is_object()) {
        for (const auto& [key, value] : it->items()) {
            if (value.is_string()) catalog.inputs[key] = value.get<std::string>();
        }
    }

    if (const auto it = root.find("counts"); it != root.end() && it->is_object()) {
        for (const auto& [key, value] : it->items()) {
            catalog.counts[key] = asInt64(value);
        }
    }

    if (const auto top = root.find("top"); top != root.end() && top->is_object()) {
        if (const auto banks = top->find("big_banks");
            banks != top->end() && banks->is_array()) {
            for (const json& item : *banks) {
                BigBank bank;
                bank.bank = item.value("bank", std::string());
                bank.entries = asInt64(item.value("entries", json()));
                bank.bytes = asInt64(item.value("bytes", json()));
                catalog.bigBanks.push_back(std::move(bank));
            }
        }
        catalog.entityCategories = parseNamedCounts(top->value("entity_categories", json()));
        catalog.entityDefinitionTypes =
            parseNamedCounts(top->value("entity_definition_types", json()));
    }

    if (const auto modules = root.find("editor_modules");
        modules != root.end() && modules->is_array()) {
        for (const json& item : *modules) {
            EditorModule mod;
            mod.id = item.value("id", std::string());
            for (const json& source : item.value("sources", json::array())) {
                if (source.is_string()) mod.sources.push_back(source.get<std::string>());
            }
            for (const json& task : item.value("first_tasks", json::array())) {
                if (task.is_string()) mod.firstTasks.push_back(task.get<std::string>());
            }
            catalog.editorModules.push_back(std::move(mod));
        }
    }

    return catalog;
}

} // namespace forge::catalog
