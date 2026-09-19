#include "forge/defschema.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace forge::defschema {

const DefType* Schema::find(std::string_view name) const {
    for (const auto& d : defs_) {
        if (d.name == name) return &d;
    }
    return nullptr;
}

Schema Schema::load(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("defschema: cannot open " + path.string());
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return loadText(buffer.str(), path.string());
}

Schema Schema::loadText(const std::string& text, const std::string& sourceName) {
    json root = json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        throw std::runtime_error("defschema: malformed JSON in " + sourceName);
    }

    Schema schema;
    for (const auto& [name, info] : root.items()) {
        DefType def;
        def.name = name;
        def.transferAddr = info.value("addr", std::string());
        def.retailOffsets = info.value("retail_offsets", false);
        def.prefixLen = info.value("prefix_len", -1);
        if (const auto it = info.find("fields");
            it != info.end() && it->is_array()) {
            for (const json& fj : *it) {
                Field f;
                if (fj.contains("name") && fj["name"].is_string()) {
                    f.name = fj["name"].get<std::string>();
                }
                f.type = fj.value("type", std::string());
                f.donorOffset = fj.value("donor_off", -1);
                if (const auto ro = fj.find("retail_off");
                    ro != fj.end() && ro->is_number_integer()) {
                    f.retailOffset = ro->get<int>();
                }
                def.fields.push_back(std::move(f));
            }
        }
        schema.defs_.push_back(std::move(def));
    }
    return schema;
}

} // namespace forge::defschema
