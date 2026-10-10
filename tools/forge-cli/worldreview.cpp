#include "worldreview.hpp"
#include "forge/defexternal.hpp"
#include "forge/big.hpp"
#include "forge/bigstorage.hpp"
#include "forge/oggstorage.hpp"
#include "forge/levstorage.hpp"
#include "forge/stbstorage.hpp"
#include "forge/wadstorage.hpp"
#include "forge/bwd.hpp"
#include "forge/wld.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using nlohmann::json;
namespace fs = std::filesystem;
// A diagnostic memory limit, not a native format restriction.
constexpr uint64_t payloadLimit = 128ull * 1024 * 1024;

fs::path utf8Path(const std::string& value) {
    return fs::path(std::u8string(value.begin(), value.end()));
}

std::string extension(const std::string& name) {
    auto result = utf8Path(name).extension().string();
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

void checkSize(uint64_t size) {
    if (size > payloadLimit)
        throw std::runtime_error("payload exceeds diagnostic memory limit (128 MiB)");
}

std::vector<uint8_t> readFile(const fs::path& path) {
    const auto size = fs::file_size(path);
    checkSize(size);
    std::ifstream input(path, std::ios::binary);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!input || (size && !input.read(reinterpret_cast<char*>(bytes.data()),
                                     static_cast<std::streamsize>(size))))
        throw std::runtime_error("cannot read complete input file");
    return bytes;
}

void reviewPayload(json& unit, const std::vector<uint8_t>& bytes) {
    const auto ext = unit.at("format").get<std::string>();
    if (ext == ".ogg") {
        const auto r = forge::oggstorage::validateEnvelope(bytes);
        unit["supported"] = true;
        unit["scope"] = "Single-stream Ogg envelope only; codec packets, granule semantics, playback "
            "and music bindings require separate review. Empty pages and chained/multiplexed streams unsupported.";
        unit["codec_checked"] = false;
        unit["metrics"] = {{"pages", r.pages}, {"packets", r.packets},
            {"payload_bytes", r.payloadBytes}, {"stream_serial", r.serial}};
        return;
    }
    if (ext == ".tga") {
        const auto r = forge::defexternal::reviewLightingTga(bytes);
        unit["supported"] = r.definitionIndependent;
        unit["scope"] = r.reason;
        if (!r.definitionIndependent) unit["reason"] = r.reason;
        else unit["metrics"] = {{"width", uint32_t(bytes[12]) | (uint32_t(bytes[13]) << 8)},
            {"height", uint32_t(bytes[14]) | (uint32_t(bytes[15]) << 8)}, {"bits_per_pixel", 24}};
        return;
    }
    if (ext == ".lev") {
        const auto r = forge::levstorage::validate(bytes);
        unit["metrics"] = {{"width", r.width}, {"height", r.height},
            {"navigation_sections", r.navigationSections}, {"navigation_nodes", r.navigationNodes}};
        unit["unresolved_symbols"] = {{"ground", r.groundThemes},
            {"atmosphere", r.atmosThemes}, {"sound", r.soundThemes}};
        unit["supported"] = true;
        return;
    }
    forge::defexternal::Review r;
    forge::defexternal::TngDefinitionInventory tngFields;
    const std::string text(bytes.begin(), bytes.end());
    if (ext == ".bwd") r = forge::defexternal::reviewBwd(bytes);
    else if (ext == ".wld") r = forge::defexternal::reviewWld(text);
    else if (ext == ".tng") {
        tngFields = forge::defexternal::inventoryTngDefinitionFields(text);
        r = tngFields.storage;
    }
    else if (ext == ".gtg") {
        tngFields = forge::defexternal::inventoryGtgDefinitionFields(text);
        r = tngFields.storage;
    }
    else throw std::runtime_error("unsupported payload format");
    unit["supported"] = r.definitionIndependent;
    if (!r.definitionIndependent) {
        unit["reason"] = r.reason;
        if (r.line) unit["line"] = r.line;
        if (!r.directive.empty()) unit["directive"] = r.directive;
    }
    if (r.definitionIndependent && (ext == ".wld" || ext == ".bwd")) {
        json references = json::array();
        auto append = [&](size_t slot, const std::string& name, const auto& region) {
            references.push_back({{"region_slot", slot}, {"region_name", name},
                {"region_definition", region.regionDef}, {"display_name", region.displayName},
                {"minimap_graphic", region.minimapGraphic}});
        };
        if (ext == ".wld") {
            const auto world = forge::wld::File::parseText(text);
            for (const auto& region : world.regions()) append(region.index, region.regionName, region);
            unit["metrics"] = {{"maps", world.maps().size()}, {"regions", world.regions().size()}};
            // InitQuests 01B47560 -> ActivateMultipleQuests 01C49AF0 checks
            // quest registration, then GetPScriptInfo 03585320's string map.
            unit["unresolved_initial_quests"] = world.initialQuests();
            unit["initial_quest_scope"] = "Quest registration and native script-info lookup remain unchecked; "
                "these names are not game.bin definition queries.";
        } else {
            const auto world = forge::bwd::File::parse(bytes);
            for (size_t i = 0; i < world.regions().size(); ++i) append(i + 1, world.regions()[i].name, world.regions()[i]);
            unit["metrics"] = {{"maps", world.maps().size()}, {"regions", world.regions().size()}};
        }
        unit["unresolved_region_symbols"] = std::move(references);
    }
    if (r.definitionIndependent && (ext == ".tng" || ext == ".gtg")) {
        json references = json::array();
        json components = json::array();
        for (const auto& field : tngFields.fields) {
            json value = {{"thing_type", field.thingType}, {"definition", field.name}, {"line", field.line}};
            if (field.conditional) value["conditional"] = true;
            if (field.component.empty() && field.field != "DefinitionType") value["field"] = field.field;
            if (field.worldMapSlot) value["world_map_slot"] = field.worldMapSlot;
            if (field.component.empty()) references.push_back(std::move(value));
            else {
                value["component"] = field.component;
                value["field"] = field.field;
                components.push_back(std::move(value));
            }
        }
        unit["unresolved_thing_definitions"] = std::move(references);
        unit["unresolved_component_definitions"] = std::move(components);
        unit["thing_reference_scope"] = forge::defexternal::tngDefinitionInventoryScope;
    }
}

json makeUnit(const std::string& name) {
    return {{"name", name}, {"format", extension(name)}, {"supported", false}};
}
}

int reviewWorldStorage(const std::string& path, bool jsonOutput) {
    json units = json::array();
    auto file = makeUnit(path);
    try {
        const auto inputPath = utf8Path(path);
        if (!fs::is_regular_file(inputPath))
            throw std::runtime_error("input must be an existing regular file");
        const auto ext = extension(path);
        if (ext == ".wad") {
            const auto storage = forge::wadstorage::validateEnvelope(inputPath);
            file["supported"] = true;
            file["scope"] = "archive envelope";
            file["metrics"] = {{"entries", storage.metadata.entries.size()},
                {"maximum_id", storage.metadata.maximumId},
                {"zero_padding_bytes", storage.zeroPaddingBytes}};
            // Open before publishing success, so a failed second read is a refusal.
            const auto archive = forge::wad::Archive::open(inputPath);
            units.push_back(file);
            for (const auto& entry : storage.metadata.entries) {
                auto unit = makeUnit(entry.name);
                unit["entry_id"] = entry.id;
                try {
                    checkSize(entry.size);
                    reviewPayload(unit, archive.read(entry));
                } catch (const std::exception& e) {
                    unit["supported"] = false;
                    unit["reason"] = e.what();
                }
                units.push_back(std::move(unit));
            }
        } else if (ext == ".big") {
            const auto storage = forge::bigstorage::validateEnvelope(inputPath);
            const auto archive = forge::big::File::open(inputPath);
            file["supported"] = true;
            file["scope"] = "archive envelope";
            file["metrics"] = {{"banks", storage.banks}, {"entries", storage.entries},
                {"payload_bytes", storage.payloadBytes}, {"zero_padding_bytes", storage.zeroPaddingBytes}};
            units.push_back(file);
            for (const auto& bank : archive.banks()) for (const auto& entry : bank.entries) {
                json unit = {{"name", entry.name}, {"format", "big_entry"}, {"bank", bank.name},
                    {"entry_id", entry.id}, {"entry_type", entry.type}, {"supported", false}};
                try {
                    checkSize(entry.length);
                    const auto r = forge::defexternal::reviewBigEntry(bank.name, entry.type,
                                                                   entry.subHeader, archive.entryData(entry));
                    unit["supported"] = r.definitionIndependent;
                    if (!r.definitionIndependent) unit["reason"] = r.reason;
                } catch (const std::exception& e) { unit["reason"] = e.what(); }
                units.push_back(std::move(unit));
            }
        } else if (ext == ".stb") {
            const auto r = forge::stbstorage::validateBankStorage(inputPath);
            file["supported"] = true;
            file["metrics"] = {{"entries", r.entries}, {"maps", r.maps}, {"seas", r.seas},
                {"foreground_frames", r.chunks.foregroundFrames},
                {"background_frames", r.chunks.backgroundFrames},
                {"detail_frames", r.chunks.detailFrames}};
        } else {
            reviewPayload(file, readFile(inputPath));
        }
    } catch (const std::exception& e) {
        file["supported"] = false;
        file["reason"] = e.what();
    }
    if (units.empty()) units.push_back(std::move(file));
    size_t supported = 0;
    for (const auto& unit : units) if (unit.at("supported").get<bool>()) ++supported;
    const bool allSupported = supported == units.size();
    const std::string scope = "Supported storage and value domains only; external references, "
        "world topology, navigation behavior and source admission remain unchecked.";
    const json report = {{"schema_version", 1}, {"path", path}, {"scope", scope},
        {"source_admitted", false}, {"storage_supported", allSupported},
        {"supported_units", supported}, {"refused_units", units.size() - supported},
        {"payload_limit_bytes", payloadLimit}, {"units", units}};
    if (jsonOutput) {
        std::cout << report.dump(2, ' ', false, json::error_handler_t::replace) << '\n';
    } else {
        std::cout << "Storage review: " << supported << " supported, "
                  << units.size() - supported << " refused\n" << scope << '\n';
        for (const auto& unit : units) {
            if (unit.contains("codec_checked") && !unit.at("codec_checked").get<bool>())
                std::cout << unit.at("scope").get<std::string>() << '\n';
            if (!unit.at("supported").get<bool>()) {
                std::cout << "REFUSED " << unit.at("name").get<std::string>();
                if (unit.contains("line")) std::cout << ':' << unit.at("line");
                if (unit.contains("directive")) std::cout << " [" << unit.at("directive").get<std::string>() << ']';
                std::cout << ": " << unit.value("reason", "unsupported storage") << '\n';
            }
        }
    }
    return allSupported ? 0 : 1;
}
