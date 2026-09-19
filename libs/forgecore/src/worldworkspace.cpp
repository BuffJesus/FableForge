#include "forge/worldworkspace.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>

#include "forge/stb.hpp"
#include "forge/stbinfo.hpp"
#include "forge/tng.hpp"
#include "forge/wad.hpp"

namespace fs = std::filesystem;

namespace forge::worldworkspace {
namespace {

std::string normalized(std::string value) {
    std::replace(value.begin(), value.end(), '/', '\\');
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

fs::path loosePath(const fs::path& root, std::string relative) {
    std::replace(relative.begin(), relative.end(), '\\', '/');
    return root / fs::path(relative);
}

std::string tngNameFor(std::string levelName) {
    const size_t dot = levelName.find_last_of('.');
    if (dot == std::string::npos) return levelName + ".tng";
    levelName.replace(dot, std::string::npos, ".tng");
    return levelName;
}

bool containsLevel(const std::vector<std::string>& values,
                   const std::string& normalizedLevel) {
    return std::any_of(values.begin(), values.end(), [&](const std::string& value) {
        return normalized(value) == normalizedLevel;
    });
}

std::vector<uint8_t> readBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("worldworkspace: cannot open " + path.string());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void writeBytes(const fs::path& path, const std::vector<uint8_t>& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("worldworkspace: cannot write " + path.string());
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("worldworkspace: failed writing " + path.string());
}

void writeText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("worldworkspace: cannot write " + path.string());
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) throw std::runtime_error("worldworkspace: failed writing " + path.string());
}

std::string safeLevelName(std::string value) {
    std::replace(value.begin(), value.end(), '/', '\\');
    if (value.empty() || value.front() == '\\' || value.find(':') != std::string::npos)
        throw std::runtime_error("worldworkspace: level name must be a relative WLD path");
    size_t start = 0;
    while (start <= value.size()) {
        const size_t end = value.find('\\', start);
        const std::string part =
            value.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (part.empty() || part == "." || part == "..")
            throw std::runtime_error("worldworkspace: unsafe level path " + value);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (!normalized(value).starts_with("finalalbion\\") ||
        !normalized(value).ends_with(".lev")) {
        throw std::runtime_error(
            "worldworkspace: level name must be FinalAlbion\\Name.lev");
    }
    return value;
}

std::vector<uint8_t> resolveLevelAsset(const fs::path& levelsDirectory,
                                       const std::string& relativeName) {
    const fs::path loose = loosePath(levelsDirectory, relativeName);
    if (fs::exists(loose)) return readBytes(loose);

    const fs::path wadPath = levelsDirectory / "FinalAlbion.wad";
    if (!fs::exists(wadPath)) {
        throw std::runtime_error(
            "worldworkspace: donor asset missing loose and FinalAlbion.wad: " +
            relativeName);
    }
    const auto archive = wad::Archive::open(wadPath);
    const std::string wanted = normalized("Data\\Levels\\" + relativeName);
    for (const auto& entry : archive.entries()) {
        if (normalized(entry.name) == wanted) return archive.read(entry);
    }
    throw std::runtime_error("worldworkspace: donor asset not found in WAD: " +
                             relativeName);
}

} // namespace

std::string AssetLocation::effectiveSource() const {
    if (loose) return inWad ? "loose-override" : "loose";
    return inWad ? "wad" : "missing";
}

const char* severityName(Severity severity) {
    switch (severity) {
        case Severity::Info: return "info";
        case Severity::Warning: return "warning";
        case Severity::Error: return "error";
    }
    return "unknown";
}

Workspace Workspace::open(const fs::path& gameRootOrLevels) {
    Workspace result;
    const fs::path directWorld = gameRootOrLevels / "FinalAlbion.wld";
    const fs::path installedWorld =
        gameRootOrLevels / "data" / "Levels" / "FinalAlbion.wld";
    if (fs::exists(directWorld)) {
        result.levelsDirectory_ = gameRootOrLevels;
    } else if (fs::exists(installedWorld)) {
        result.levelsDirectory_ = gameRootOrLevels / "data" / "Levels";
    } else {
        throw std::runtime_error(
            "worldworkspace: FinalAlbion.wld not found under " +
            gameRootOrLevels.string());
    }

    result.world_ = wld::File::parse(result.levelsDirectory_ / "FinalAlbion.wld");

    std::set<std::string> wadNames;
    const fs::path wadPath = result.levelsDirectory_ / "FinalAlbion.wad";
    if (fs::exists(wadPath)) {
        const auto wad = wad::Archive::open(wadPath);
        result.hasWad_ = true;
        result.wadEntryCount_ = wad.entries().size();
        for (const auto& entry : wad.entries()) wadNames.insert(normalized(entry.name));
    } else {
        result.issues_.push_back({
            Severity::Info, "wad_absent", wadPath.string(),
            "FinalAlbion.wad is absent; this can be valid for an unpacked install."
        });
    }

    std::set<std::string> staticMapNames;
    const fs::path stbPath = result.levelsDirectory_ / "FinalAlbion_RT.stb";
    if (fs::exists(stbPath)) {
        const auto stb = stb::Archive::open(stbPath);
        result.hasStaticBank_ = true;
        result.staticMapCount_ = stb.staticMaps().size();
        for (const auto& map : stb.staticMaps())
            staticMapNames.insert(normalized(map.levelName));
    } else {
        result.issues_.push_back({
            Severity::Info, "stb_absent", stbPath.string(),
            "FinalAlbion_RT.stb is absent; static-map membership cannot be validated."
        });
    }

    std::set<std::string> knownLevels;
    std::map<int, std::string> indexes;
    std::map<uint32_t, std::string> uids;
    std::map<std::pair<int, int>, std::string> origins;
    for (const auto& map : result.world_.maps()) {
        const std::string levelKey = normalized(map.levelName);
        knownLevels.insert(levelKey);

        auto addDuplicate = [&](Severity severity, const char* code,
                                const std::string& prior, const std::string& detail) {
            result.issues_.push_back({
                severity, code, map.levelName,
                detail + " is also used by " + prior
            });
        };
        if (const auto [it, inserted] = indexes.emplace(map.index, map.levelName);
            !inserted) {
            addDuplicate(Severity::Error, "duplicate_map_index", it->second,
                         "Map index " + std::to_string(map.index));
        }
        if (map.mapUid != 0) {
            if (const auto [it, inserted] = uids.emplace(map.mapUid, map.levelName);
                !inserted) {
                addDuplicate(Severity::Error, "duplicate_map_uid", it->second,
                             "MapUID " + std::to_string(map.mapUid));
            }
        }
        if (const auto [it, inserted] =
                origins.emplace(std::pair{map.mapX, map.mapY}, map.levelName);
            !inserted) {
            addDuplicate(Severity::Warning, "duplicate_map_origin", it->second,
                         "World origin (" + std::to_string(map.mapX) + "," +
                         std::to_string(map.mapY) + ")");
        }

        Level level;
        level.map = map;
        level.tngName = tngNameFor(map.levelName);
        const std::string levArchive = "Data\\Levels\\" + map.levelName;
        const std::string tngArchive = "Data\\Levels\\" + level.tngName;
        level.lev.inWad = wadNames.contains(normalized(levArchive));
        level.tng.inWad = wadNames.contains(normalized(tngArchive));
        level.lev.loose = fs::exists(loosePath(result.levelsDirectory_, map.levelName));
        level.tng.loose = fs::exists(loosePath(result.levelsDirectory_, level.tngName));
        level.inStaticBank =
            staticMapNames.contains(normalized(levArchive));

        for (const auto& region : result.world_.regions()) {
            if (containsLevel(region.containsMaps, levelKey))
                level.owningRegions.push_back(region.regionName);
            if (containsLevel(region.seesMaps, levelKey))
                level.visibleFromRegions.push_back(region.regionName);
        }
        if (!level.lev.available()) {
            result.issues_.push_back({
                Severity::Error, "missing_lev", map.levelName,
                "LEV is absent from both FinalAlbion.wad and loose data/Levels."
            });
        }
        if (!level.tng.available()) {
            result.issues_.push_back({
                Severity::Error, "missing_tng", level.tngName,
                "TNG is absent from both FinalAlbion.wad and loose data/Levels."
            });
        }
        if (result.hasStaticBank_ && !level.inStaticBank) {
            result.issues_.push_back({
                Severity::Error, "missing_static_map", map.levelName,
                "No matching static-map record exists in FinalAlbion_RT.stb."
            });
        }
        if (level.owningRegions.empty()) {
            result.issues_.push_back({
                Severity::Warning, "unowned_map", map.levelName,
                "No region ContainsMap entry owns this level."
            });
        }
        result.levels_.push_back(std::move(level));
    }

    for (const auto& region : result.world_.regions()) {
        for (const auto* references : {&region.containsMaps, &region.seesMaps}) {
            for (const auto& reference : *references) {
                if (!knownLevels.contains(normalized(reference))) {
                    result.issues_.push_back({
                        Severity::Error, "unresolved_region_map", region.regionName,
                        "Region references unknown map " + reference
                    });
                }
            }
        }
    }
    return result;
}

bool Workspace::clean() const {
    return std::none_of(issues_.begin(), issues_.end(), [](const Issue& issue) {
        return issue.severity == Severity::Error;
    });
}

CreateLevelResult createLevelFromDonor(const CreateLevelRequest& request) {
    if (request.outputProject.empty())
        throw std::runtime_error("worldworkspace: output project path is required");
    if (fs::exists(request.outputProject))
        throw std::runtime_error("worldworkspace: output project already exists: " +
                                 request.outputProject.string());

    const Workspace source = Workspace::open(request.sourceRoot);
    const std::string donorName = safeLevelName(request.donorLevelName);
    const std::string newName = safeLevelName(request.newLevelName);
    const wld::Map* donor = source.world().findMap(donorName);
    if (donor == nullptr)
        throw std::runtime_error("worldworkspace: donor map is not registered: " +
                                 donorName);
    if (!request.regionName.empty() && !request.createRegionIfMissing &&
        source.world().findRegion(request.regionName) == nullptr) {
        throw std::runtime_error("worldworkspace: target region is not registered: " +
                                 request.regionName);
    }
    if (!request.allowOriginCollision) {
        for (const auto& map : source.world().maps()) {
            if (map.mapX == request.mapX && map.mapY == request.mapY) {
                throw std::runtime_error(
                    "worldworkspace: requested map origin is already occupied by " +
                    map.levelName + "; pass allowOriginCollision only when intentional");
            }
        }
    }

    const std::string donorTng = tngNameFor(donorName);
    const std::string newTng = tngNameFor(newName);
    const std::vector<uint8_t> levBytes =
        resolveLevelAsset(source.levelsDirectory(), donorName);
    const std::vector<uint8_t> tngBytes =
        resolveLevelAsset(source.levelsDirectory(), donorTng);
    if (levBytes.empty() || tngBytes.empty())
        throw std::runtime_error("worldworkspace: donor LEV/TNG cannot be empty");
    // Parsing the textual donor catches a truncated or mislabeled TNG before the
    // transaction is published.
    const std::string tngText(tngBytes.begin(), tngBytes.end());
    (void)tng::File::parseText(tngText, donorTng);

    const fs::path sourceWad = source.levelsDirectory() / "FinalAlbion.wad";
    if (request.cloneIntoWad) {
        if (!fs::exists(sourceWad))
            throw std::runtime_error(
                "worldworkspace: --with-wad requires FinalAlbion.wad");
        const auto archive = wad::Archive::open(sourceWad);
        auto findPayload = [&](const std::string& relativeName)
            -> std::vector<uint8_t> {
            const std::string wanted =
                normalized("Data\\Levels\\" + relativeName);
            for (const auto& entry : archive.entries())
                if (normalized(entry.name) == wanted) return archive.read(entry);
            throw std::runtime_error(
                "worldworkspace: donor WAD entry not found: " + relativeName);
        };
        if (findPayload(donorName) != levBytes ||
            findPayload(donorTng) != tngBytes) {
            throw std::runtime_error(
                "worldworkspace: effective loose donor differs from its WAD "
                "entry; archive cloning would package stale bytes");
        }
    }

    struct StaticClone {
        std::vector<uint8_t> chunk;
        std::vector<uint8_t> record;
    };
    std::optional<StaticClone> staticClone;
    const fs::path sourceStb = source.levelsDirectory() / "FinalAlbion_RT.stb";
    if (request.cloneStaticMap) {
        if (!fs::exists(sourceStb)) {
            throw std::runtime_error(
                "worldworkspace: FinalAlbion_RT.stb is required for a loadable "
                "new level; disable static-map cloning only for an intentional "
                "incomplete authoring project");
        }
        const auto archive = stb::Archive::open(sourceStb);
        const std::string fullDonor =
            normalized("Data\\Levels\\" + donorName);
        const stb::StaticMap* donorStaticMap = nullptr;
        for (const auto& map : archive.staticMaps()) {
            const std::string candidate = normalized(map.levelName);
            if (candidate == fullDonor || candidate == normalized(donorName)) {
                donorStaticMap = &map;
                break;
            }
        }
        if (donorStaticMap == nullptr) {
            throw std::runtime_error(
                "worldworkspace: donor has no static-map record: " + donorName);
        }
        StaticClone clone;
        clone.record = archive.readStaticMapRecord(*donorStaticMap);
        if (clone.record.size() < stbinfo::kInfoBlockSize)
            throw std::runtime_error("worldworkspace: donor static-map record is truncated");
        auto info = stbinfo::readInfoBlock(clone.record.data());
        const stb::Entry* donorEntry = nullptr;
        for (const auto& entry : archive.entries()) {
            if (entry.id == static_cast<uint32_t>(info.bankFileIndex)) {
                donorEntry = &entry;
                break;
            }
        }
        if (donorEntry == nullptr) {
            for (const auto& entry : archive.entries()) {
                if (normalized(entry.name) == normalized(donorStaticMap->levelName)) {
                    donorEntry = &entry;
                    break;
                }
            }
        }
        if (donorEntry == nullptr)
            throw std::runtime_error(
                "worldworkspace: cannot resolve donor STB chunk entry for " +
                donorName);
        clone.chunk = archive.read(*donorEntry);
        if (clone.chunk.empty())
            throw std::runtime_error("worldworkspace: donor STB chunk is empty");

        const int64_t deltaX = int64_t(request.mapX) - int64_t(info.worldX);
        const int64_t deltaY = int64_t(request.mapY) - int64_t(info.worldY);
        info.worldX = request.mapX;
        info.worldY = request.mapY;
        info.cameraMapBounds[0] += static_cast<float>(deltaX);
        info.cameraMapBounds[3] += static_cast<float>(deltaX);
        info.cameraMapBounds[1] += static_cast<float>(deltaY);
        info.cameraMapBounds[4] += static_cast<float>(deltaY);
        const auto encodedInfo = stbinfo::writeInfoBlock(info);
        std::copy(encodedInfo.begin(), encodedInfo.end(), clone.record.begin());
        staticClone = std::move(clone);
    }

    wld::File edited = wld::File::parse(
        source.levelsDirectory() / "FinalAlbion.wld");
    wld::Map added;
    added.mapX = request.mapX;
    added.mapY = request.mapY;
    added.levelName = newName;
    if (!request.levelScriptName.empty()) {
        added.levelScriptName = request.levelScriptName;
    } else {
        std::string leaf = newName.substr(newName.find_last_of('\\') + 1);
        const size_t dot = leaf.find_last_of('.');
        added.levelScriptName = leaf.substr(0, dot);
    }
    added.isSea = donor->isSea;
    added.loadedOnPlayerProximity = donor->loadedOnPlayerProximity;
    const wld::Map& registered = edited.addMap(std::move(added));
    const int mapIndex = registered.index;
    const uint32_t mapUid = registered.mapUid;
    if (!request.regionName.empty()) {
        if (edited.findRegion(request.regionName) == nullptr) {
            if (!request.createRegionIfMissing)
                throw std::runtime_error("worldworkspace: unknown region " +
                                         request.regionName);
            wld::Region region;
            region.regionName = request.regionName;
            region.displayName = request.regionDisplayName.empty()
                                     ? request.regionName
                                     : request.regionDisplayName;
            edited.addRegion(std::move(region));
        }
        edited.addMapToRegion(request.regionName, newName,
                             request.visibleFromRegion);
    }

    fs::path parent = request.outputProject.parent_path();
    if (parent.empty()) parent = fs::current_path();
    fs::create_directories(parent);
    const std::string base = request.outputProject.filename().string();
    fs::path temporary;
    for (int attempt = 0; attempt < 100; ++attempt) {
        const auto tick = std::chrono::high_resolution_clock::now()
                              .time_since_epoch().count();
        temporary = parent / (base + ".forge-tmp-" + std::to_string(tick) +
                              "-" + std::to_string(attempt));
        if (!fs::exists(temporary)) break;
        temporary.clear();
    }
    if (temporary.empty())
        throw std::runtime_error("worldworkspace: cannot reserve temporary project");

    CreateLevelResult result;
    result.projectRoot = request.outputProject;
    const fs::path relativeLevels = fs::path("data") / "Levels";
    result.worldPath = request.outputProject / relativeLevels / "FinalAlbion.wld";
    result.levPath =
        request.outputProject / relativeLevels / loosePath({}, newName);
    result.tngPath =
        request.outputProject / relativeLevels / loosePath({}, newTng);
    if (staticClone)
        result.stbPath = request.outputProject / relativeLevels / "FinalAlbion_RT.stb";
    if (request.cloneIntoWad)
        result.wadPath = request.outputProject / relativeLevels / "FinalAlbion.wad";
    result.mapIndex = mapIndex;
    result.mapUid = mapUid;
    result.levBytes = levBytes.size();
    result.tngBytes = tngBytes.size();
    result.staticMapCloned = staticClone.has_value();
    result.wadEntriesCloned = request.cloneIntoWad ? 2 : 0;

    try {
        const fs::path tempLevels = temporary / relativeLevels;
        const fs::path tempWorld = tempLevels / "FinalAlbion.wld";
        const fs::path tempLev = tempLevels / loosePath({}, newName);
        const fs::path tempTng = tempLevels / loosePath({}, newTng);
        writeText(tempWorld, edited.serialize());
        writeBytes(tempLev, levBytes);
        writeBytes(tempTng, tngBytes);
        if (staticClone) {
            stb::appendStaticMap(
                sourceStb, tempLevels / "FinalAlbion_RT.stb",
                "Data\\Levels\\" + newName,
                "Data\\Levels\\" + newName,
                staticClone->chunk, staticClone->record);
        }
        if (request.cloneIntoWad) {
            wad::appendClonedEntries(
                sourceWad,
                {
                    {"Data\\Levels\\" + donorName,
                     "Data\\Levels\\" + newName},
                    {"Data\\Levels\\" + donorTng,
                     "Data\\Levels\\" + newTng},
                },
                tempLevels / "FinalAlbion.wad");
        }

        const auto reparsed = wld::File::parse(tempWorld);
        const auto* check = reparsed.findMap(newName);
        if (check == nullptr || check->index != mapIndex || check->mapUid != mapUid)
            throw std::runtime_error("worldworkspace: staged WLD verification failed");
        if (readBytes(tempLev) != levBytes || readBytes(tempTng) != tngBytes)
            throw std::runtime_error("worldworkspace: staged donor clone mismatch");
        if (staticClone) {
            const auto stagedStb =
                stb::Archive::open(tempLevels / "FinalAlbion_RT.stb");
            const std::string wanted = normalized("Data\\Levels\\" + newName);
            const stb::StaticMap* stagedMap = nullptr;
            for (const auto& map : stagedStb.staticMaps()) {
                if (normalized(map.levelName) == wanted) {
                    stagedMap = &map;
                    break;
                }
            }
            const auto* stagedEntry =
                stagedStb.findEntry("Data\\Levels\\" + newName);
            if (stagedMap == nullptr || stagedEntry == nullptr ||
                stagedStb.read(*stagedEntry) != staticClone->chunk) {
                throw std::runtime_error(
                    "worldworkspace: staged static-map clone verification failed");
            }
            const auto stagedRecord = stagedStb.readStaticMapRecord(*stagedMap);
            const auto stagedInfo = stbinfo::readInfoBlock(stagedRecord.data());
            if (stagedInfo.worldX != request.mapX ||
                stagedInfo.worldY != request.mapY ||
                stagedInfo.bankFileIndex != static_cast<int32_t>(stagedEntry->id)) {
                throw std::runtime_error(
                    "worldworkspace: staged static-map placement verification failed");
            }
        }
        if (request.cloneIntoWad) {
            const auto stagedWad =
                wad::Archive::open(tempLevels / "FinalAlbion.wad");
            const wad::Entry* stagedLev = nullptr;
            const wad::Entry* stagedTng = nullptr;
            for (const auto& entry : stagedWad.entries()) {
                if (normalized(entry.name) ==
                    normalized("Data\\Levels\\" + newName))
                    stagedLev = &entry;
                if (normalized(entry.name) ==
                    normalized("Data\\Levels\\" + newTng))
                    stagedTng = &entry;
            }
            if (stagedLev == nullptr || stagedTng == nullptr ||
                stagedWad.read(*stagedLev) != levBytes ||
                stagedWad.read(*stagedTng) != tngBytes) {
                throw std::runtime_error(
                    "worldworkspace: staged WAD clone verification failed");
            }
        }
        if (!request.regionName.empty()) {
            const auto* region = reparsed.findRegion(request.regionName);
            if (region == nullptr ||
                !containsLevel(region->containsMaps, normalized(newName)) ||
                (request.visibleFromRegion &&
                 !containsLevel(region->seesMaps, normalized(newName)))) {
                throw std::runtime_error(
                    "worldworkspace: staged region membership verification failed");
            }
        }

        writeText(temporary / "FableForge.world-project",
                  "FableForgeWorldProject 1\n"
                  "Donor " + donorName + "\n"
                  "Level " + newName + "\n"
                  "MapIndex " + std::to_string(mapIndex) + "\n"
                  "MapUID " + std::to_string(mapUid) + "\n"
                  "Region " + request.regionName + "\n"
                  "StaticMap " + (staticClone ? "cloned\n" : "omitted\n") +
                  "WadEntries " +
                  (request.cloneIntoWad ? "cloned\n" : "loose-only\n"));
        fs::rename(temporary, request.outputProject);
    } catch (...) {
        std::error_code cleanupError;
        fs::remove_all(temporary, cleanupError);
        throw;
    }
    return result;
}

} // namespace forge::worldworkspace
