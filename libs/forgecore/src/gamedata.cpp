// forge::gamedata -- live data spine. See gamedata.hpp.

#include "forge/gamedata.hpp"

#include <algorithm>
#include <set>

namespace forge::gamedata {

namespace {

constexpr uint32_t kInventoryItemDefCrc = 0x83ad7c03u;

bool hasInventoryItemComponent(const bin::Entry& entry) {
    // OBJECT payloads begin [01 00 01][u16 componentCount], followed by
    // 12-byte CDef-listing rows whose first dword is the component-class CRC.
    if (entry.data.size() < 5) return false;
    const uint16_t count = static_cast<uint16_t>(entry.data[3]) |
                           (static_cast<uint16_t>(entry.data[4]) << 8);
    for (uint16_t i = 0; i < count; ++i) {
        const size_t offset = 5 + static_cast<size_t>(i) * 12;
        if (offset + 4 > entry.data.size()) return false;
        const uint32_t crc = static_cast<uint32_t>(entry.data[offset]) |
                             (static_cast<uint32_t>(entry.data[offset + 1]) << 8) |
                             (static_cast<uint32_t>(entry.data[offset + 2]) << 16) |
                             (static_cast<uint32_t>(entry.data[offset + 3]) << 24);
        if (crc == kInventoryItemDefCrc) return true;
    }
    return false;
}

// Collect distinct, sorted entry names for a def category (skip NULLDEF/empty).
std::vector<std::string> collect(const std::vector<bin::Entry>& entries,
                                 const char* definition) {
    std::set<std::string> uniq;
    for (const auto& e : entries) {
        if (e.definition != definition) continue;
        if (e.name.empty()) continue;
        if (e.name.rfind("NULLDEF", 0) == 0) continue;
        uniq.insert(e.name);
    }
    return std::vector<std::string>(uniq.begin(), uniq.end());
}

}  // namespace

Catalog fromEntries(const std::vector<bin::Entry>& entries) {
    Catalog c;
    c.creatures = collect(entries, "CREATURE");
    c.objects = collect(entries, "OBJECT");
    for (const auto& entry : entries) {
        if (entry.definition == "OBJECT" && !entry.name.empty() &&
            entry.name.rfind("NULLDEF", 0) != 0 && hasInventoryItemComponent(entry))
            c.inventoryItems.push_back(entry.name);
    }
    std::sort(c.inventoryItems.begin(), c.inventoryItems.end());
    c.inventoryItems.erase(std::unique(c.inventoryItems.begin(), c.inventoryItems.end()),
                           c.inventoryItems.end());
    return c;
}

void addRegions(Catalog& out, const std::vector<wld::Region>& regions) {
    std::set<std::string> uniq;
    for (const auto& r : regions)
        if (!r.regionName.empty()) uniq.insert(r.regionName);
    out.regions.assign(uniq.begin(), uniq.end());
}

Catalog readGameRoot(const std::filesystem::path& gameRoot,
                     const std::filesystem::path& wldPath) {
    namespace fs = std::filesystem;
    const fs::path defs = gameRoot / "data" / "CompiledDefs";
    auto file = bin::File::open(defs / "names.bin", defs / "game.bin");
    Catalog c = fromEntries(file.entries());
    if (!wldPath.empty())
        addRegions(c, wld::File::parse(wldPath).regions());
    return c;
}

}  // namespace forge::gamedata
