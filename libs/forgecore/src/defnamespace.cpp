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
} // namespace forge::defnamespace
