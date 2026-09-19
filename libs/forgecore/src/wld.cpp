#include "forge/wld.hpp"

#include <fstream>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <limits>
#include <sstream>
#include <stdexcept>

#include "textformat.hpp"

namespace fs = std::filesystem;
namespace tf = forge::textformat;

namespace forge::wld {

namespace {

bool iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) ==
                      std::tolower(static_cast<unsigned char>(y));
           });
}

std::string quoteWld(std::string_view value) {
    if (value.find('"') != std::string_view::npos ||
        value.find('\n') != std::string_view::npos ||
        value.find('\r') != std::string_view::npos) {
        throw std::runtime_error("wld: value contains an unsupported quote/newline");
    }
    return "\"" + std::string(value) + "\"";
}

std::string regionFloat(float value) {
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(1);
    out << value;
    return out.str();
}

} // namespace

File File::parse(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("wld: cannot open " + path.string());
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return parseText(buffer.str(), path.filename().string());
}

File File::parseText(std::string text, std::string sourceName) {
    File result;
    result.source_ = std::move(sourceName);
    result.rawLines_ = tf::splitRawLines(text);

    enum class Section { None, InitialQuests, Map, Region };
    Section section = Section::None;
    Map* map = nullptr;
    Region* region = nullptr;

    for (const std::string& raw : result.rawLines_) {
        std::string key, value;
        if (!tf::parseLogicalLine(raw, key, value)) continue;

        if (tf::equalsIgnoreCase(key, "START_INITIAL_QUESTS")) {
            section = Section::InitialQuests;
            continue;
        }
        if (tf::equalsIgnoreCase(key, "END_INITIAL_QUESTS")) {
            section = Section::None;
            continue;
        }
        if (section == Section::InitialQuests) {
            result.initialQuests_.push_back(key);
            continue;
        }

        if (tf::equalsIgnoreCase(key, "MapUIDCount")) {
            result.mapUidCount_ = std::atoi(value.c_str());
            continue;
        }
        if (tf::equalsIgnoreCase(key, "NewMap")) {
            result.maps_.emplace_back();
            map = &result.maps_.back();
            map->index = std::atoi(value.c_str());
            section = Section::Map;
            continue;
        }
        if (tf::equalsIgnoreCase(key, "EndMap")) {
            map = nullptr;
            section = Section::None;
            continue;
        }
        if (tf::equalsIgnoreCase(key, "NewRegion")) {
            result.regions_.emplace_back();
            region = &result.regions_.back();
            region->index = std::atoi(value.c_str());
            section = Section::Region;
            continue;
        }
        if (tf::equalsIgnoreCase(key, "EndRegion")) {
            region = nullptr;
            section = Section::None;
            continue;
        }

        if (section == Section::Map && map != nullptr) {
            if (tf::equalsIgnoreCase(key, "MapX")) map->mapX = std::atoi(value.c_str());
            else if (tf::equalsIgnoreCase(key, "MapY")) map->mapY = std::atoi(value.c_str());
            else if (tf::equalsIgnoreCase(key, "LevelName")) map->levelName = tf::stripQuotes(value);
            else if (tf::equalsIgnoreCase(key, "LevelScriptName")) map->levelScriptName = tf::stripQuotes(value);
            else if (tf::equalsIgnoreCase(key, "MapUID")) map->mapUid = static_cast<uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
            else if (tf::equalsIgnoreCase(key, "IsSea")) map->isSea = tf::equalsIgnoreCase(value, "TRUE");
            else if (tf::equalsIgnoreCase(key, "LoadedOnPlayerProximity")) map->loadedOnPlayerProximity = tf::equalsIgnoreCase(value, "TRUE");
            continue;
        }

        if (section == Section::Region && region != nullptr) {
            if (tf::equalsIgnoreCase(key, "RegionName")) region->regionName = tf::stripQuotes(value);
            else if (tf::equalsIgnoreCase(key, "NewDisplayName")) region->displayName = tf::stripQuotes(value);
            else if (tf::equalsIgnoreCase(key, "RegionDef")) region->regionDef = tf::stripQuotes(value);
            else if (tf::equalsIgnoreCase(key, "AppearOnWorldMap")) region->appearOnWorldMap = true;
            else if (tf::equalsIgnoreCase(key, "ContainsMap")) region->containsMaps.push_back(tf::stripQuotes(value));
            else if (tf::equalsIgnoreCase(key, "SeesMap")) region->seesMaps.push_back(tf::stripQuotes(value));
            else if (tf::equalsIgnoreCase(key, "MiniMapGraphic")) region->minimapGraphic = tf::stripQuotes(value);
            else if (tf::equalsIgnoreCase(key, "MiniMapScale")) region->minimapScale = static_cast<float>(std::atof(value.c_str()));
            else if (tf::equalsIgnoreCase(key, "MiniMapOffsetX")) region->miniMapOffsetX = static_cast<float>(std::atof(value.c_str()));
            else if (tf::equalsIgnoreCase(key, "MiniMapOffsetY")) region->miniMapOffsetY = static_cast<float>(std::atof(value.c_str()));
            else if (tf::equalsIgnoreCase(key, "WorldMapOffsetX")) region->worldMapOffsetX = static_cast<float>(std::atof(value.c_str()));
            else if (tf::equalsIgnoreCase(key, "WorldMapOffsetY")) region->worldMapOffsetY = static_cast<float>(std::atof(value.c_str()));
            else if (tf::equalsIgnoreCase(key, "NameGraphicOffsetX")) region->nameGraphicOffsetX = static_cast<float>(std::atof(value.c_str()));
            else if (tf::equalsIgnoreCase(key, "NameGraphicOffsetY")) region->nameGraphicOffsetY = static_cast<float>(std::atof(value.c_str()));
            else {
                // MiniMapRegionExitTextOffsetX[Name] / ...Y[Name] -> per-exit floats.
                const bool isX = key.rfind("MiniMapRegionExitTextOffsetX[", 0) == 0;
                const bool isY = key.rfind("MiniMapRegionExitTextOffsetY[", 0) == 0;
                if (isX || isY) {
                    const size_t lb = key.find('[');
                    const size_t rb = key.find(']', lb + 1);
                    if (lb != std::string::npos && rb != std::string::npos) {
                        const std::string name = key.substr(lb + 1, rb - lb - 1);
                        auto it = std::find_if(region->exitTextOffsets.begin(),
                                               region->exitTextOffsets.end(),
                                               [&](const Region::ExitTextOffset& e) {
                                                   return e.name == name;
                                               });
                        if (it == region->exitTextOffsets.end()) {
                            region->exitTextOffsets.push_back({name, 0.0f, 0.0f});
                            it = std::prev(region->exitTextOffsets.end());
                        }
                        const float v = static_cast<float>(std::atof(value.c_str()));
                        if (isX) it->x = v; else it->y = v;
                    }
                }
            }
            continue;
        }
    }

    return result;
}

std::string File::serialize() const {
    std::string out;
    size_t total = 0;
    for (const std::string& line : rawLines_) total += line.size();
    out.reserve(total);
    for (const std::string& line : rawLines_) out += line;
    return out;
}

const Map* File::findMap(std::string_view levelName) const {
    for (const auto& map : maps_) {
        if (iequals(map.levelName, levelName)) return &map;
    }
    return nullptr;
}

const Region* File::findRegion(std::string_view regionName) const {
    for (const auto& region : regions_) {
        if (iequals(region.regionName, regionName)) return &region;
    }
    return nullptr;
}

const Map& File::addMap(Map map) {
    if (map.levelName.empty() || map.levelScriptName.empty())
        throw std::runtime_error("wld: new map needs LevelName and LevelScriptName");
    if (findMap(map.levelName) != nullptr)
        throw std::runtime_error("wld: duplicate level " + map.levelName);

    int maxIndex = 0;
    uint32_t maxUid = 0;
    for (const auto& existing : maps_) {
        maxIndex = std::max(maxIndex, existing.index);
        maxUid = std::max(maxUid, existing.mapUid);
        if (map.index != 0 && existing.index == map.index)
            throw std::runtime_error("wld: duplicate map index " +
                                     std::to_string(map.index));
        if (map.mapUid != 0 && existing.mapUid == map.mapUid)
            throw std::runtime_error("wld: duplicate MapUID " +
                                     std::to_string(map.mapUid));
    }
    if (map.index == 0) map.index = maxIndex + 1;
    if (map.mapUid == 0) {
        if (maxUid == std::numeric_limits<uint32_t>::max())
            throw std::runtime_error("wld: MapUID space exhausted");
        map.mapUid = maxUid + 1;
    }

    std::string eol = "\r\n";
    for (const auto& line : rawLines_) {
        if (!line.empty() && line.back() == '\n') {
            eol = line.size() >= 2 && line[line.size() - 2] == '\r' ? "\r\n" : "\n";
            break;
        }
    }
    std::vector<std::string> block = {
        "NewMap " + std::to_string(map.index) + ";" + eol,
        "MapX " + std::to_string(map.mapX) + ";" + eol,
        "MapY " + std::to_string(map.mapY) + ";" + eol,
        "LevelName " + quoteWld(map.levelName) + ";" + eol,
        "LevelScriptName " + quoteWld(map.levelScriptName) + ";" + eol,
        "MapUID " + std::to_string(map.mapUid) + ";" + eol,
        std::string("IsSea ") + (map.isSea ? "TRUE;" : "FALSE;") + eol,
        std::string("LoadedOnPlayerProximity ") +
            (map.loadedOnPlayerProximity ? "TRUE;" : "FALSE;") + eol,
        "EndMap;" + eol,
        eol,
    };
    size_t insertAt = rawLines_.size();
    for (size_t i = 0; i < rawLines_.size(); ++i) {
        std::string key, value;
        if (tf::parseLogicalLine(rawLines_[i], key, value) &&
            tf::equalsIgnoreCase(key, "NewRegion")) {
            insertAt = i;
            break;
        }
    }
    rawLines_.insert(rawLines_.begin() + static_cast<ptrdiff_t>(insertAt),
                     block.begin(), block.end());
    maps_.push_back(std::move(map));
    return maps_.back();
}

const Region& File::addRegion(Region region) {
    // RegionDef may be empty: retail regions 140/141/142 ship with RegionDef "".
    // Only RegionName is mandatory.
    if (region.regionName.empty())
        throw std::runtime_error("wld: new region needs a RegionName");
    if (findRegion(region.regionName) != nullptr)
        throw std::runtime_error("wld: duplicate region " + region.regionName);
    int maxIndex = 0;
    for (const auto& existing : regions_) {
        maxIndex = std::max(maxIndex, existing.index);
        if (region.index != 0 && existing.index == region.index)
            throw std::runtime_error("wld: duplicate region index " +
                                     std::to_string(region.index));
    }
    if (region.index == 0) region.index = maxIndex + 1;
    for (const auto& name : region.containsMaps)
        if (findMap(name) == nullptr)
            throw std::runtime_error("wld: region contains unknown map " + name);
    for (const auto& name : region.seesMaps)
        if (findMap(name) == nullptr)
            throw std::runtime_error("wld: region sees unknown map " + name);

    std::string eol = "\r\n";
    for (const auto& line : rawLines_) {
        if (!line.empty() && line.back() == '\n') {
            eol = line.size() >= 2 && line[line.size() - 2] == '\r' ? "\r\n" : "\n";
            break;
        }
    }
    if (!rawLines_.empty() && !rawLines_.back().empty() &&
        rawLines_.back().back() != '\n') rawLines_.back() += eol;
    rawLines_.push_back("NewRegion " + std::to_string(region.index) + ";" + eol);
    rawLines_.push_back("RegionName " + quoteWld(region.regionName) + ";" + eol);
    if (!region.displayName.empty())
        rawLines_.push_back("NewDisplayName " + quoteWld(region.displayName) + ";" + eol);
    rawLines_.push_back("RegionDef " + quoteWld(region.regionDef) + ";" + eol);
    if (region.appearOnWorldMap) rawLines_.push_back("AppearOnWorldMap;" + eol);
    // These seven layout keys occur in every retail region (141/141), including
    // the map-less filler region.  Keep appended regions loader-shaped even
    // when they are intentionally absent from the world map/minimap.
    rawLines_.push_back("MiniMapScale " + regionFloat(region.minimapScale) + ";" + eol);
    rawLines_.push_back("MiniMapOffsetX " + regionFloat(region.miniMapOffsetX) + ";" + eol);
    rawLines_.push_back("MiniMapOffsetY " + regionFloat(region.miniMapOffsetY) + ";" + eol);
    rawLines_.push_back("WorldMapOffsetX " + regionFloat(region.worldMapOffsetX) + ";" + eol);
    rawLines_.push_back("WorldMapOffsetY " + regionFloat(region.worldMapOffsetY) + ";" + eol);
    rawLines_.push_back("NameGraphicOffsetX " + regionFloat(region.nameGraphicOffsetX) + ";" + eol);
    rawLines_.push_back("NameGraphicOffsetY " + regionFloat(region.nameGraphicOffsetY) + ";" + eol);
    for (const auto& name : region.containsMaps)
        rawLines_.push_back("ContainsMap " + quoteWld(name) + ";" + eol);
    for (const auto& name : region.seesMaps)
        rawLines_.push_back("SeesMap " + quoteWld(name) + ";" + eol);
    rawLines_.push_back("EndRegion;" + eol);
    rawLines_.push_back(eol);
    regions_.push_back(std::move(region));
    return regions_.back();
}

void File::addMapToRegion(std::string_view regionName,
                          std::string_view levelName, bool visible) {
    if (findMap(levelName) == nullptr)
        throw std::runtime_error("wld: unknown map " + std::string(levelName));
    size_t regionVectorIndex = regions_.size();
    for (size_t i = 0; i < regions_.size(); ++i) {
        if (iequals(regions_[i].regionName, regionName)) {
            regionVectorIndex = i;
            break;
        }
    }
    if (regionVectorIndex == regions_.size())
        throw std::runtime_error("wld: unknown region " + std::string(regionName));
    auto& region = regions_[regionVectorIndex];
    const auto has = [&](const std::vector<std::string>& values) {
        return std::any_of(values.begin(), values.end(), [&](const auto& value) {
            return iequals(value, levelName);
        });
    };
    const bool addContains = !has(region.containsMaps);
    const bool addSees = visible && !has(region.seesMaps);
    if (!addContains && !addSees) return;

    std::string eol = "\r\n";
    int activeRegion = -1;
    for (size_t i = 0; i < rawLines_.size(); ++i) {
        if (!rawLines_[i].empty() && rawLines_[i].back() == '\n')
            eol = rawLines_[i].size() >= 2 && rawLines_[i][rawLines_[i].size() - 2] == '\r'
                      ? "\r\n" : "\n";
        std::string key, value;
        if (!tf::parseLogicalLine(rawLines_[i], key, value)) continue;
        if (tf::equalsIgnoreCase(key, "NewRegion")) activeRegion = std::atoi(value.c_str());
        else if (tf::equalsIgnoreCase(key, "EndRegion")) {
            if (activeRegion == region.index) {
                std::vector<std::string> lines;
                if (addContains)
                    lines.push_back("ContainsMap " + quoteWld(levelName) + ";" + eol);
                if (addSees)
                    lines.push_back("SeesMap " + quoteWld(levelName) + ";" + eol);
                rawLines_.insert(rawLines_.begin() + static_cast<ptrdiff_t>(i),
                                 lines.begin(), lines.end());
                if (addContains) region.containsMaps.emplace_back(levelName);
                if (addSees) region.seesMaps.emplace_back(levelName);
                return;
            }
            activeRegion = -1;
        }
    }
    throw std::runtime_error("wld: region block missing EndRegion");
}

void File::setMapOwner(std::string_view regionName, std::string_view levelName) {
    if (findMap(levelName) == nullptr)
        throw std::runtime_error("wld: unknown map " + std::string(levelName));
    size_t target = regions_.size();
    for (size_t i = 0; i < regions_.size(); ++i) {
        if (iequals(regions_[i].regionName, regionName)) {
            target = i;
            break;
        }
    }
    if (target == regions_.size())
        throw std::runtime_error("wld: unknown region " + std::string(regionName));

    for (auto& region : regions_) {
        region.containsMaps.erase(
            std::remove_if(region.containsMaps.begin(), region.containsMaps.end(),
                           [&](const std::string& value) {
                               return iequals(value, levelName);
                           }),
            region.containsMaps.end());
    }
    regions_[target].containsMaps.emplace_back(levelName);

    std::string eol = "\r\n";
    int activeRegion = -1;
    for (size_t i = 0; i < rawLines_.size();) {
        if (!rawLines_[i].empty() && rawLines_[i].back() == '\n')
            eol = rawLines_[i].size() >= 2 && rawLines_[i][rawLines_[i].size() - 2] == '\r'
                      ? "\r\n" : "\n";
        std::string key, value;
        if (!tf::parseLogicalLine(rawLines_[i], key, value)) {
            ++i;
            continue;
        }
        if (tf::equalsIgnoreCase(key, "NewRegion")) {
            activeRegion = std::atoi(value.c_str());
            ++i;
            continue;
        }
        if (tf::equalsIgnoreCase(key, "ContainsMap") && iequals(tf::stripQuotes(value), levelName)) {
            rawLines_.erase(rawLines_.begin() + static_cast<ptrdiff_t>(i));
            continue;
        }
        if (tf::equalsIgnoreCase(key, "EndRegion")) {
            if (activeRegion == regions_[target].index) {
                rawLines_.insert(rawLines_.begin() + static_cast<ptrdiff_t>(i),
                                 "ContainsMap " + quoteWld(levelName) + ";" + eol);
                ++i;
            }
            activeRegion = -1;
        }
        ++i;
    }
}

void File::setRegionText(std::string_view regionName, std::string_view key, std::string_view value) {
    size_t target = regions_.size();
    for (size_t i = 0; i < regions_.size(); ++i)
        if (iequals(regions_[i].regionName, regionName)) { target = i; break; }
    if (target == regions_.size())
        throw std::runtime_error("wld: unknown region " + std::string(regionName));
    Region& region = regions_[target];
    const bool quoted = !(tf::equalsIgnoreCase(key, "MiniMapGraphic"));   // the graphic is a bare token in retail
    std::string eol = "\r\n";
    int activeRegion = -1;
    size_t nameLine = rawLines_.size();
    bool written = false;
    for (size_t i = 0; i < rawLines_.size(); ++i) {
        if (!rawLines_[i].empty() && rawLines_[i].back() == '\n')
            eol = rawLines_[i].size() >= 2 && rawLines_[i][rawLines_[i].size() - 2] == '\r' ? "\r\n" : "\n";
        std::string k, v;
        if (!tf::parseLogicalLine(rawLines_[i], k, v)) continue;
        if (tf::equalsIgnoreCase(k, "NewRegion")) { activeRegion = std::atoi(v.c_str()); continue; }
        if (activeRegion != region.index) continue;
        if (tf::equalsIgnoreCase(k, "RegionName")) nameLine = i;
        if (tf::equalsIgnoreCase(k, key)) {
            rawLines_[i] = std::string(key) + " " + (quoted ? quoteWld(value) : std::string(value)) + ";" + eol;
            written = true;
            break;
        }
        if (tf::equalsIgnoreCase(k, "EndRegion")) break;
    }
    if (!written) {
        if (nameLine == rawLines_.size())
            throw std::runtime_error("wld: region " + std::string(regionName) + " has no RegionName line");
        rawLines_.insert(rawLines_.begin() + static_cast<ptrdiff_t>(nameLine) + 1,
                         std::string(key) + " " + (quoted ? quoteWld(value) : std::string(value)) + ";" + eol);
    }
    if (tf::equalsIgnoreCase(key, "RegionName")) region.regionName = std::string(value);
    else if (tf::equalsIgnoreCase(key, "NewDisplayName")) region.displayName = std::string(value);
    else if (tf::equalsIgnoreCase(key, "RegionDef")) region.regionDef = std::string(value);
    else if (tf::equalsIgnoreCase(key, "MiniMapGraphic")) region.minimapGraphic = std::string(value);
}

void File::removeMapFromRegion(std::string_view regionName, std::string_view levelName, bool alsoSees) {
    size_t target = regions_.size();
    for (size_t i = 0; i < regions_.size(); ++i)
        if (iequals(regions_[i].regionName, regionName)) { target = i; break; }
    if (target == regions_.size())
        throw std::runtime_error("wld: unknown region " + std::string(regionName));
    Region& region = regions_[target];
    auto drop = [&](std::vector<std::string>& values) {
        values.erase(std::remove_if(values.begin(), values.end(), [&](const std::string& v) { return iequals(v, levelName); }), values.end());
    };
    drop(region.containsMaps);
    if (alsoSees) drop(region.seesMaps);
    int activeRegion = -1;
    for (size_t i = 0; i < rawLines_.size();) {
        std::string k, v;
        if (!tf::parseLogicalLine(rawLines_[i], k, v)) { ++i; continue; }
        if (tf::equalsIgnoreCase(k, "NewRegion")) { activeRegion = std::atoi(v.c_str()); ++i; continue; }
        if (activeRegion == region.index &&
            ((tf::equalsIgnoreCase(k, "ContainsMap") || (alsoSees && tf::equalsIgnoreCase(k, "SeesMap"))) &&
             iequals(tf::stripQuotes(v), levelName))) {
            rawLines_.erase(rawLines_.begin() + static_cast<ptrdiff_t>(i));
            continue;
        }
        if (tf::equalsIgnoreCase(k, "EndRegion")) activeRegion = -1;
        ++i;
    }
}

void File::relocateMap(std::string_view levelName, int mapX, int mapY) {
    Map* target = nullptr;
    for (auto& map : maps_)
        if (iequals(map.levelName, levelName)) { target = &map; break; }
    if (target == nullptr)
        throw std::runtime_error("wld: unknown map " + std::string(levelName));
    const int targetIndex = target->index;
    int activeMap = -1;
    bool wroteX = false, wroteY = false;
    for (auto& line : rawLines_) {
        std::string key, value;
        if (!tf::parseLogicalLine(line, key, value)) continue;
        if (tf::equalsIgnoreCase(key, "NewMap")) {
            activeMap = std::atoi(value.c_str());
            continue;
        }
        if (tf::equalsIgnoreCase(key, "EndMap")) {
            activeMap = -1;
            continue;
        }
        if (activeMap != targetIndex) continue;
        const std::string eol = !line.empty() && line.back() == '\n'
            ? (line.size() >= 2 && line[line.size()-2] == '\r' ? "\r\n" : "\n")
            : "";
        if (tf::equalsIgnoreCase(key, "MapX")) {
            line = "MapX " + std::to_string(mapX) + ";" + eol;
            wroteX = true;
        } else if (tf::equalsIgnoreCase(key, "MapY")) {
            line = "MapY " + std::to_string(mapY) + ";" + eol;
            wroteY = true;
        }
    }
    if (!wroteX || !wroteY)
        throw std::runtime_error("wld: map block lacks MapX/MapY");
    target->mapX = mapX;
    target->mapY = mapY;
}

} // namespace forge::wld
