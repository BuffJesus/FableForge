#include "namespacereview.hpp"
#include "forge/defnamespace.hpp"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>

namespace {
namespace fs = std::filesystem;
using nlohmann::json;
constexpr uint64_t inputLimit = 128ull * 1024 * 1024;

forge::bin::File openBank(const std::string& root) {
    const auto folder = fs::path(std::u8string(root.begin(), root.end())) / "data" / "CompiledDefs";
    for (const char* name : {"game.bin", "names.bin"}) {
        const auto path = folder / name;
        if (!fs::is_regular_file(path)) throw std::runtime_error("missing regular definition bank: " + path.string());
        if (fs::file_size(path) > inputLimit)
            throw std::runtime_error("definition bank exceeds diagnostic input limit (128 MiB): " + path.string());
    }
    return forge::bin::File::open(folder / "names.bin", folder / "game.bin");
}
}

int reviewDefinitionNamespace(const std::string& baseline,
                              const std::vector<std::string>& sources, bool jsonOutput) {
    const std::string scope = "Read-only game.bin namespace allocation and CRC candidate and fixed startup class-ordinal review. "
        "Payload relocation, complete storage, runtime class registration, external references "
        "and source admission are not certified. No duplicate winner is selected.";
    json report = {{"schema_version", 1}, {"baseline", baseline}, {"scope", scope},
        {"analysis_complete", false}, {"source_admitted", false}, {"relocation_checked", false},
        {"input_limit_bytes", inputLimit}};
    int status = 1;
    try {
        if (sources.empty()) throw std::runtime_error("at least one source is required");
        const auto base = openBank(baseline);
        std::vector<forge::bin::File> banks;
        for (const auto& source : sources) banks.push_back(openBank(source));
        std::vector<forge::defnamespace::Input> inputs;
        for (const auto& bank : banks) inputs.push_back({bank.entries(), bank.originalNames()});
        const auto plan = forge::defnamespace::allocate({base.entries(), base.originalNames()}, inputs);
        const auto lookups = forge::defnamespace::analyzeNameLookups(plan, inputs);
        auto candidates = [&](const std::vector<uint32_t>& slots) {
            json result = json::array();
            for (const auto slot : slots) {
                const auto& entry = plan.entries.at(slot);
                result.push_back({{"slot", slot}, {"definition", entry.definition}, {"name", entry.name}});
            }
            return result;
        };
        report["planned_entries"] = plan.entries.size();
        report["planned_names"] = plan.names.size();
        report["ambiguous_named_groups"] = json::array();
        for (const auto crc : lookups.ambiguousNamedCrcs)
            report["ambiguous_named_groups"].push_back({{"crc", crc},
                {"candidates", candidates(lookups.plannedCandidates.at(crc))}});
        report["sources"] = json::array();
        size_t changedGroups = 0, changedStartup = 0, missingStartup = 0;
        // FableWin InitAndCompile 01913D9D requests class ordinal 1.
        // analyzeNameLookups above has validated canonical ordinals and map identities.
        auto selected = [](std::span<const forge::bin::Entry> entries, std::string_view definition)
                -> std::optional<uint32_t> {
            for (size_t slot = 0; slot < entries.size(); ++slot)
                if (entries[slot].definition == definition && entries[slot].indexInDefinition == 1)
                    return uint32_t(slot);
            return std::nullopt;
        };
        for (size_t i = 0; i < sources.size(); ++i) {
            const auto& lookup = lookups.sources[i];
            json row = {{"root", sources[i]}, {"game_id_changes", plan.sources[i].gameEntries},
                {"name_offset_changes", plan.sources[i].nameOffsets},
                {"existing_name_crc_groups", lookup.existingQueryGroups},
                {"added_name_crc_groups", lookup.addedQueryGroups}, {"changed_candidate_groups", json::array()}};
            for (const auto& change : lookup.changed)
                row["changed_candidate_groups"].push_back({{"crc", change.crc},
                    {"source_candidates_in_planned_slots", candidates(change.sourceSlotsInPlan)},
                    {"planned_candidates", candidates(change.plannedSlots)}});
            row["startup_class_selections"] = json::array();
            for (const char* definition : {"GLOBAL", "ENGINE", "ENVIRONMENT", "HERO_STATS"}) {
                const auto before = selected(inputs[i].entries, definition);
                const auto after = selected(plan.entries, definition);
                auto mapped = before;
                if (before) {
                    const auto found = plan.sources[i].gameEntries.find(*before);
                    if (found != plan.sources[i].gameEntries.end()) mapped = found->second;
                }
                const std::string selectionStatus = !before ? "missing_source" :
                    !after ? "missing_planned" : mapped == after ? "unchanged" : "changed";
                changedStartup += before && mapped != after;
                missingStartup += !before || !after;
                auto identity = [&](std::optional<uint32_t> slot) -> json {
                    if (!slot) return nullptr;
                    return {{"slot", *slot}, {"name", plan.entries.at(*slot).name}};
                };
                row["startup_class_selections"].push_back({{"definition", definition},
                    {"class_ordinal", 1}, {"source_in_planned_slots", identity(mapped)},
                    {"planned", identity(after)}, {"status", selectionStatus}});
            }
            changedGroups += lookup.changed.size();
            report["sources"].push_back(std::move(row));
        }
        report["changed_source_candidate_groups"] = changedGroups;
        report["changed_startup_selections"] = changedStartup;
        report["missing_startup_selections"] = missingStartup;
        report["startup_selection_scope"] = "GLOBAL, ENGINE, ENVIRONMENT and HERO_STATS class ordinal 1 only. "
            "Missing source selections are coverage gaps, not merge changes; they do not affect exit status.";
        report["analysis_complete"] = true;
        status = lookups.ambiguousNamedCrcs.empty() && changedGroups == 0 && changedStartup == 0 ? 0 : 1;
    } catch (const std::exception& error) {
        report["reason"] = error.what();
    }
    if (jsonOutput) std::cout << report.dump(2, ' ', false, json::error_handler_t::replace) << '\n';
    else {
        if (!report["analysis_complete"].get<bool>())
            std::cout << "Namespace review refused: " << report.at("reason").get<std::string>() << '\n';
        else {
            std::cout << "Namespace plan: " << report["planned_entries"] << " slots, "
                      << report["planned_names"] << " names; " << report["ambiguous_named_groups"].size()
                      << " ambiguous named CRC groups, " << report["changed_source_candidate_groups"]
                      << " changed source candidate groups, " << report["changed_startup_selections"]
                      << " changed startup selections, " << report["missing_startup_selections"]
                      << " missing startup selections\n";
            for (const auto& source : report["sources"])
                for (const auto& selection : source["startup_class_selections"])
                    if (selection["status"] == "changed")
                        std::cout << "CHANGED startup " << selection["definition"].get<std::string>()
                                  << " ordinal 1 in " << source["root"].get<std::string>()
                                  << ": source maps to slot " << selection["source_in_planned_slots"]["slot"]
                                  << ", plan selects slot " << selection["planned"]["slot"] << '\n';
            if (report["missing_startup_selections"].get<size_t>() != 0)
                std::cout << "Missing startup selections are coverage gaps and do not affect exit status.\n";
            for (const auto& group : report["ambiguous_named_groups"]) {
                std::cout << "AMBIGUOUS CRC " << group["crc"] << ':';
                for (const auto& entry : group["candidates"])
                    std::cout << " slot " << entry["slot"] << " (" << entry["name"].get<std::string>() << ')';
                std::cout << '\n';
            }
        }
        std::cout << scope << '\n';
    }
    return status;
}
