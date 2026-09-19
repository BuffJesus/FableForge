#pragma once
// Joined, read-only view of the files that make up Fable's world. This is the
// shared validation model for the CLI and the future world-canvas GUI.

#include <filesystem>
#include <string>
#include <vector>

#include "forge/wld.hpp"

namespace forge::worldworkspace {

enum class Severity {
    Info,
    Warning,
    Error,
};

struct AssetLocation {
    bool inWad = false;
    bool loose = false;

    bool available() const { return inWad || loose; }
    // Loose level files override their WAD counterparts at runtime.
    std::string effectiveSource() const;
};

struct Level {
    wld::Map map;
    std::string tngName;
    AssetLocation lev;
    AssetLocation tng;
    bool inStaticBank = false;
    std::vector<std::string> owningRegions;
    std::vector<std::string> visibleFromRegions;
};

struct Issue {
    Severity severity = Severity::Info;
    std::string code;
    std::string subject;
    std::string message;
};

class Workspace {
public:
    // Accepts a Fable installation root or its data/Levels directory.
    static Workspace open(const std::filesystem::path& gameRootOrLevels);

    const std::filesystem::path& levelsDirectory() const { return levelsDirectory_; }
    const wld::File& world() const { return world_; }
    const std::vector<Level>& levels() const { return levels_; }
    const std::vector<Issue>& issues() const { return issues_; }
    bool hasWad() const { return hasWad_; }
    bool hasStaticBank() const { return hasStaticBank_; }
    size_t wadEntryCount() const { return wadEntryCount_; }
    size_t staticMapCount() const { return staticMapCount_; }
    bool clean() const;

private:
    std::filesystem::path levelsDirectory_;
    wld::File world_;
    std::vector<Level> levels_;
    std::vector<Issue> issues_;
    bool hasWad_ = false;
    bool hasStaticBank_ = false;
    size_t wadEntryCount_ = 0;
    size_t staticMapCount_ = 0;
};

struct CreateLevelRequest {
    std::filesystem::path sourceRoot;
    std::filesystem::path outputProject;
    std::string donorLevelName;
    std::string newLevelName;
    std::string levelScriptName;
    std::string regionName;
    std::string regionDisplayName;
    bool createRegionIfMissing = false;
    int mapX = 0;
    int mapY = 0;
    bool visibleFromRegion = true;
    bool allowOriginCollision = false;
    bool cloneStaticMap = true;
    bool cloneIntoWad = false;
};

struct CreateLevelResult {
    std::filesystem::path projectRoot;
    std::filesystem::path worldPath;
    std::filesystem::path levPath;
    std::filesystem::path tngPath;
    std::filesystem::path stbPath;
    std::filesystem::path wadPath;
    int mapIndex = 0;
    uint32_t mapUid = 0;
    size_t levBytes = 0;
    size_t tngBytes = 0;
    bool staticMapCloned = false;
    size_t wadEntriesCloned = 0;
};

// Builds a deployable loose-file project without modifying the source install.
// The project directory is assembled in a uniquely owned sibling temporary
// directory and published with one rename. outputProject must not already exist.
CreateLevelResult createLevelFromDonor(const CreateLevelRequest& request);

const char* severityName(Severity severity);

} // namespace forge::worldworkspace
