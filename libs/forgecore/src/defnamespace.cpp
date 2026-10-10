#include "forge/defnamespace.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace forge::defnamespace {
namespace {
using Identity = std::pair<std::string, std::string>;
void validate(Input input) {
    std::map<std::string, size_t> ordinals;
    for (const auto& entry : input.entries) {
        if (entry.indexInDefinition < 0 ||
            size_t(entry.indexInDefinition) != ordinals[entry.definition]++)
            throw std::runtime_error("definition namespace: noncanonical ordinal");
    }
    if (input.entries.size() >= UINT32_MAX)
        throw std::runtime_error("definition namespace: record index overflow");
}
uint32_t nextName(uint64_t& offset, const Name& name) {
    if (name.second.find('\0') != std::string::npos ||
        offset + name.second.size() + 5 > UINT32_MAX)
        throw std::runtime_error("definition namespace: invalid name or offset overflow");
    const auto result = uint32_t(offset);
    offset += name.second.size() + 5;
    return result;
}
}

Plan allocate(Input baseline, std::span<const Input> sources) {
    validate(baseline);
    Plan plan;
    plan.entries.assign(baseline.entries.begin(), baseline.entries.end());
    plan.names.assign(baseline.names.begin(), baseline.names.end());
    std::map<Identity, uint32_t> addedNamed;
    std::set<std::string> baselineNamed;
    std::set<std::string> ambiguousNames;
    std::map<std::string, size_t> ordinals;
    for (const auto& entry : baseline.entries) {
        ++ordinals[entry.definition];
        if (!entry.name.empty()) baselineNamed.insert(entry.name);
    }
    for (const auto source : sources) {
        std::map<std::string, size_t> counts;
        for (const auto& entry : source.entries)
            if (!entry.name.empty() && ++counts[entry.name] > 1) ambiguousNames.insert(entry.name);
    }
    uint64_t nameEnd = 4;
    std::map<std::string, std::pair<uint32_t, uint32_t>> namedOffsets;
    auto rememberName = [&](const Name& name, uint32_t offset) {
        const auto [it, inserted] = namedOffsets.emplace(name.second, std::pair{name.first, offset});
        if (!inserted && it->second.first != name.first)
            throw std::runtime_error("definition namespace: conflicting name CRC");
    };
    for (const auto& name : baseline.names) rememberName(name, nextName(nameEnd, name));
    for (const auto source : sources) {
        validate(source);
        defrelocate::Maps maps;
        for (size_t i = 0; i < std::min(baseline.entries.size(), source.entries.size()); ++i) {
            const auto& a = baseline.entries[i];
            const auto& b = source.entries[i];
            if (a.definition != b.definition || a.name != b.name ||
                a.indexInDefinition != b.indexInDefinition)
                throw std::runtime_error("definition namespace: changed baseline record identity");
        }
        for (size_t i = 0; i < std::min(baseline.names.size(), source.names.size()); ++i)
            if (baseline.names[i] != source.names[i])
                throw std::runtime_error("definition namespace: changed baseline name identity");
        for (size_t i = baseline.entries.size(); i < source.entries.size(); ++i) {
            const auto& entry = source.entries[i];
            const Identity identity{entry.definition, entry.name};
            uint32_t target = uint32_t(plan.entries.size());
            if (!entry.name.empty() && !baselineNamed.contains(entry.name) &&
                !ambiguousNames.contains(entry.name)) {
                if (const auto found = addedNamed.find(identity); found != addedNamed.end())
                    target = found->second;
                else addedNamed.emplace(identity, target);
            }
            if (target == plan.entries.size()) {
                if (plan.entries.size() >= UINT32_MAX || ordinals[entry.definition] > INT32_MAX)
                    throw std::runtime_error("definition namespace: record index overflow");
                auto added = entry;
                added.indexInDefinition = int32_t(ordinals[entry.definition]++);
                plan.entries.push_back(std::move(added));
            }
            if (target != i) maps.gameEntries.emplace(uint32_t(i), target);
        }
        uint64_t sourceOffset = 4;
        for (size_t i = 0; i < source.names.size(); ++i) {
            const auto& name = source.names[i];
            const uint32_t before = nextName(sourceOffset, name);
            // Preserve duplicate baseline names' individual offsets, even though
            // string lookup uses their first occurrence.
            if (i < baseline.names.size()) continue;
            auto found = namedOffsets.find(name.second);
            if (found == namedOffsets.end()) {
                const auto target = nextName(nameEnd, name);
                plan.names.push_back(name);
                rememberName(name, target);
                found = namedOffsets.find(name.second);
            } else if (found->second.first != name.first) {
                throw std::runtime_error("definition namespace: conflicting name CRC");
            }
            if (before != found->second.second) maps.nameOffsets.emplace(before, found->second.second);
        }
        plan.sources.push_back(std::move(maps));
    }
    return plan;
}
std::span<const uint32_t> NameLookupAnalysis::candidatesFor(std::string_view name) const {
    if (name.empty()) return {};
    const auto found = plannedCandidates.find(bin::nameCrc(name));
    return found == plannedCandidates.end() ? std::span<const uint32_t>{} : found->second;
}

NameLookupAnalysis analyzeNameLookups(const Plan& plan, std::span<const Input> sources) {
    if (plan.sources.size() != sources.size())
        throw std::runtime_error("definition lookup: source/map count mismatch");
    auto validateNames = [](Input input) {
        validate(input);
        std::set<std::string_view> names;
        for (const auto& [crc, name] : input.names) {
            if (name.find('\0') != name.npos || bin::nameCrc(name) != crc)
                throw std::runtime_error("definition lookup: invalid stored name CRC or string");
            names.insert(name);
        }
        for (const auto& entry : input.entries) {
            if (!names.contains(entry.definition) ||
                (!entry.name.empty() && !names.contains(entry.name)))
                throw std::runtime_error("definition lookup: entry identity absent from name pool");
        }
    };
    validateNames({plan.entries, plan.names});
    NameLookupAnalysis result;
    std::set<uint32_t> plannedQueries;
    for (size_t slot = 0; slot < plan.entries.size(); ++slot) {
        const auto& name = plan.entries[slot].name;
        const auto crc = bin::nameCrc(name);
        result.plannedCandidates[crc].push_back(uint32_t(slot));
        if (!name.empty()) plannedQueries.insert(crc);
    }
    for (const auto crc : plannedQueries)
        if (result.plannedCandidates.at(crc).size() > 1) result.ambiguousNamedCrcs.push_back(crc);
    for (size_t si = 0; si < sources.size(); ++si) {
        const auto source = sources[si];
        validateNames(source);
        const auto& map = plan.sources[si].gameEntries;
        for (const auto& [before, after] : map)
            if (before >= source.entries.size() || after >= plan.entries.size())
                throw std::runtime_error("definition lookup: map slot out of range");
        std::map<uint32_t, std::vector<uint32_t>> original;
        std::set<uint32_t> sourceQueries, assigned;
        for (size_t slot = 0; slot < source.entries.size(); ++slot) {
            const auto moved = map.find(uint32_t(slot));
            const auto target = moved == map.end() ? uint32_t(slot) : moved->second;
            if (target >= plan.entries.size() || !assigned.insert(target).second)
                throw std::runtime_error("definition lookup: noninjective or out-of-range mapping");
            const auto& entry = source.entries[slot];
            const auto& planned = plan.entries[target];
            if (entry.definition != planned.definition || entry.name != planned.name)
                throw std::runtime_error("definition lookup: mapped identity mismatch");
            const auto crc = bin::nameCrc(entry.name);
            original[crc].push_back(target);
            if (!entry.name.empty()) sourceQueries.insert(crc);
        }
        auto& analysis = result.sources.emplace_back();
        analysis.existingQueryGroups = sourceQueries.size();
        for (const auto crc : plannedQueries)
            if (!sourceQueries.contains(crc)) ++analysis.addedQueryGroups;
        for (const auto crc : sourceQueries) {
            auto& before = original.at(crc);
            std::sort(before.begin(), before.end());
            const auto& after = result.plannedCandidates.at(crc);
            if (before != after) analysis.changed.push_back({crc, before, after});
        }
    }
    return result;
}

Prepared prepare(Input baseline, std::span<const Input> sources,
                 const defschema::Schema& schema) {
    Prepared result;
    result.allocation = allocate(baseline, sources);
    result.sources.reserve(sources.size());
    for (size_t si = 0; si < sources.size(); ++si) {
        const auto& map = result.allocation.sources[si];
        auto& contributors = result.sources.emplace_back();
        contributors.reserve(sources[si].entries.size());
        for (size_t slot = 0; slot < sources[si].entries.size(); ++slot) {
            const auto moved = map.gameEntries.find(uint32_t(slot));
            const auto target = moved == map.gameEntries.end() ? uint32_t(slot) : moved->second;
            auto entry = sources[si].entries[slot];
            try {
                entry.data = defrelocate::relocate(entry, schema, map);
            } catch (const std::exception& error) {
                throw std::runtime_error("definition namespace: source " + std::to_string(si) +
                    " game.bin slot " + std::to_string(slot) + ": " + error.what());
            }
            entry.indexInDefinition = result.allocation.entries.at(target).indexInDefinition;
            contributors.push_back({target, std::move(entry)});
        }
    }
    return result;
}
} // namespace forge::defnamespace
