#include "forge/stage.hpp"

#include <fstream>
#include <stdexcept>

#include "nlohmann/json.hpp"

namespace fs = std::filesystem;
using nlohmann::json;

namespace forge::stage {
namespace {

constexpr const char* kManifestName = "forge_stage_manifest.json";
constexpr const char* kBackupSuffix = ".forgebak";

std::string relativeString(const fs::path& base, const fs::path& path) {
    return fs::relative(path, base).generic_string();
}

} // namespace

fs::path manifestPath(const fs::path& gameRoot) {
    return gameRoot / kManifestName;
}

Result apply(const fs::path& gameRoot, const fs::path& modDir) {
    if (!fs::exists(gameRoot)) {
        throw std::runtime_error("stage: game root not found: " + gameRoot.string());
    }
    if (!fs::exists(modDir)) {
        throw std::runtime_error("stage: mod dir not found: " + modDir.string());
    }
    const fs::path manifest = manifestPath(gameRoot);
    if (fs::exists(manifest)) {
        throw std::runtime_error(
            "stage: a mod is already staged (manifest exists); run unstage first");
    }

    Result result;
    json entries = json::array();

    for (const auto& item : fs::recursive_directory_iterator(modDir)) {
        if (!item.is_regular_file()) continue;
        const std::string relative = relativeString(modDir, item.path());
        const fs::path target = gameRoot / relative;

        const bool hadOriginal = fs::exists(target);
        if (hadOriginal) {
            const fs::path backup = target.string() + kBackupSuffix;
            if (!fs::exists(backup)) {
                fs::copy_file(target, backup);
            }
            result.backedUp.push_back(relative);
        } else {
            fs::create_directories(target.parent_path());
        }
        fs::copy_file(item.path(), target, fs::copy_options::overwrite_existing);
        result.staged.push_back(relative);

        entries.push_back({{"path", relative}, {"had_original", hadOriginal}});
    }

    if (result.staged.empty()) {
        throw std::runtime_error("stage: no files found under " + modDir.string());
    }

    std::ofstream out(manifest, std::ios::binary);
    out << json{{"files", entries}}.dump(2);
    return result;
}

Result revert(const fs::path& gameRoot) {
    const fs::path manifest = manifestPath(gameRoot);
    if (!fs::exists(manifest)) {
        throw std::runtime_error("unstage: no manifest at " + manifest.string());
    }

    std::ifstream in(manifest, std::ios::binary);
    const json doc = json::parse(in);
    in.close();

    Result result;
    for (const auto& entry : doc.at("files")) {
        const std::string relative = entry.at("path").get<std::string>();
        const fs::path target = gameRoot / relative;
        const fs::path backup = target.string() + kBackupSuffix;

        if (entry.at("had_original").get<bool>() && fs::exists(backup)) {
            fs::rename(backup, target);
            result.restored.push_back(relative);
        } else {
            fs::remove(target);
            result.removed.push_back(relative);
        }
    }
    fs::remove(manifest);
    return result;
}

} // namespace forge::stage
