#include "forge/stage.hpp"

#include <fstream>
#include <algorithm>
#include <set>
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

fs::path checkedTarget(const fs::path& root, const std::string& relative) {
    const fs::path rel(relative);
    if (rel.empty() || rel.has_root_path()) throw std::runtime_error("unstage: invalid relative path: " + relative);
    for (const auto& part : rel) if (part == "..") throw std::runtime_error("unstage: path escapes install: " + relative);
    const auto base = fs::weakly_canonical(root);
    const auto target = fs::weakly_canonical(root / rel);
    const auto [a, b] = std::mismatch(base.begin(), base.end(), target.begin(), target.end());
    if (a != base.end() || b == target.end()) throw std::runtime_error("unstage: path escapes install: " + relative);
    return root / rel;
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

    struct Entry { std::string relative; fs::path target, backup; bool hadOriginal; };
    std::vector<Entry> entries;
    std::set<fs::path> targets;
    // Validate the entire recovery plan before changing any file. A missing
    // original is an error, never a reason to delete the installed target.
    for (const auto& item : doc.at("files")) {
        Entry entry;
        entry.relative = item.at("path").get<std::string>();
        entry.target = checkedTarget(gameRoot, entry.relative);
        entry.backup = checkedTarget(gameRoot, entry.relative + kBackupSuffix);
        entry.hadOriginal = item.at("had_original").get<bool>();
        if (fs::weakly_canonical(entry.target) == fs::weakly_canonical(manifest) || entry.relative.ends_with(kBackupSuffix) ||
            !targets.insert(fs::weakly_canonical(entry.target)).second)
            throw std::runtime_error("unstage: conflicting manifest path: " + entry.relative);
        if (entry.hadOriginal && !fs::is_regular_file(entry.backup))
            throw std::runtime_error("unstage: missing original backup: " + entry.backup.string() + "; files were left unchanged");
        if (fs::exists(entry.target) && !fs::is_regular_file(entry.target))
            throw std::runtime_error("unstage: target is not a file: " + entry.target.string());
        entries.push_back(std::move(entry));
    }
    Result result;
    for (const auto& entry : entries) {
        const auto& [relative, target, backup, hadOriginal] = entry;
        if (hadOriginal) {
            // Keep all originals until the whole restore succeeds. A later
            // locked file can then be retried without consuming earlier backups.
            fs::copy_file(backup, target, fs::copy_options::overwrite_existing);
            result.restored.push_back(relative);
        } else {
            fs::remove(target);
            result.removed.push_back(relative);
            // the folders the stage created for it (Mods/<Name>/ ...) go too, while empty
            std::error_code ec;
            const fs::path rootCanon = fs::weakly_canonical(gameRoot, ec);
            for (fs::path dir = target.parent_path();
                 fs::weakly_canonical(dir, ec) != rootCanon && dir.has_parent_path() && fs::is_directory(dir, ec) && fs::is_empty(dir, ec);
                 dir = dir.parent_path())
                if (!fs::remove(dir, ec)) break;
        }
    }
    fs::remove(manifest);
    for (const auto& entry : entries) if (entry.hadOriginal) {
        std::error_code ignored;
        fs::remove(entry.backup, ignored);
    }
    return result;
}

} // namespace forge::stage
