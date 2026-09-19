#include "forge/fse.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace forge::fse {

std::string Function::signature() const {
    std::string out = returnType + " " + name + "(";
    for (size_t i = 0; i < parameters.size(); ++i) {
        if (i != 0) out += ", ";
        out += parameters[i].type + " " + parameters[i].name;
        if (parameters[i].optional) out += " = <opt>";
    }
    out += ")";
    return out;
}

const Function* Manifest::find(std::string_view name) const {
    for (const auto& fn : functions) {
        if (fn.name == name) return &fn;
    }
    return nullptr;
}

Manifest load(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("fse: cannot open " + path.string());
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return loadText(buffer.str(), path.string());
}

Manifest loadText(const std::string& text, const std::string& sourceName) {
    // The manifest ships with a UTF-8 BOM; nlohmann tolerates it only when told.
    json root = json::parse(text, nullptr, false, /*ignore_comments=*/false);
    if (root.is_discarded()) {
        // Retry skipping a leading BOM.
        const std::string_view sv(text);
        const std::string_view stripped =
            sv.size() >= 3 && sv.substr(0, 3) == "\xEF\xBB\xBF" ? sv.substr(3) : sv;
        root = json::parse(stripped, nullptr, false);
    }
    if (root.is_discarded() || !root.is_object()) {
        throw std::runtime_error("fse: malformed JSON in " + sourceName);
    }

    Manifest manifest;
    manifest.fseVersion = root.value("fseVersion", std::string());
    manifest.apiVersion = root.value("apiVersion", std::string());
    manifest.generatedAtUtc = root.value("generatedAtUtc", std::string());

    if (const auto it = root.find("functions"); it != root.end() && it->is_array()) {
        for (const json& item : *it) {
            Function fn;
            fn.name = item.value("name", std::string());
            fn.scope = item.value("scope", std::string());
            fn.returnType = item.value("returnType", std::string());
            fn.blocking = item.value("blocking", false);
            fn.category = item.value("category", std::string());
            fn.description = item.value("description", std::string());
            if (const auto p = item.find("parameters");
                p != item.end() && p->is_array()) {
                for (const json& pj : *p) {
                    Parameter param;
                    param.name = pj.value("name", std::string());
                    param.type = pj.value("type", std::string());
                    param.optional = pj.value("optional", false);
                    fn.parameters.push_back(std::move(param));
                }
            }
            manifest.functions.push_back(std::move(fn));
        }
    }

    if (const auto it = root.find("limits"); it != root.end() && it->is_object()) {
        for (const auto& [key, value] : it->items()) {
            if (value.is_number_integer()) {
                manifest.limits.emplace_back(key, value.get<int64_t>());
            }
        }
    }

    return manifest;
}

} // namespace forge::fse
