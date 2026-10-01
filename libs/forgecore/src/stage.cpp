#include "forge/stage.hpp"

#include <fstream>
#include <algorithm>
#include <cctype>
#include <cwctype>
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

bool recoveryPath(const std::string& relative) {
    for (const auto& component : fs::path(relative)) {
        std::string part = component.string();
        std::transform(part.begin(), part.end(), part.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (part == kManifestName || part.ends_with(kBackupSuffix)) return true;
    }
    return false;
}

fs::path targetKey(const fs::path& path) {
    auto key = fs::weakly_canonical(path);
#ifdef _WIN32
    auto text = key.native();
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return wchar_t(std::towlower(c)); });
    key = text;
#endif
    return key;
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

    struct Entry { std::string relative; fs::path source, target, backup; bool hadOriginal; };
    std::vector<Entry> plan;
    std::set<fs::path> targets;
    json entries = json::array();
    // Resolve and validate the entire plan before creating recovery data.
    for (const auto& item : fs::recursive_directory_iterator(modDir)) {
        if (!item.is_regular_file()) continue;
        Entry entry;
        entry.relative = relativeString(modDir, item.path());
        entry.source = item.path();
        entry.target = checkedTarget(gameRoot, entry.relative);
        entry.backup = checkedTarget(gameRoot, entry.relative + kBackupSuffix);
        entry.hadOriginal = fs::exists(entry.target);
        if (targetKey(entry.target) == targetKey(manifest) ||
            recoveryPath(entry.relative) ||
            !targets.insert(targetKey(entry.target)).second)
            throw std::runtime_error("stage: conflicting manifest path: " + entry.relative);
        if (entry.hadOriginal && !fs::is_regular_file(entry.target))
            throw std::runtime_error("stage: target is not a file: " + entry.target.string());
        if (fs::exists(entry.backup))
            throw std::runtime_error("stage: unowned backup exists: " + entry.backup.string() + "; files were left unchanged");
        entries.push_back({{"path", entry.relative}, {"had_original", entry.hadOriginal}});
        plan.push_back(std::move(entry));
    }
    if (plan.empty()) throw std::runtime_error("stage: no files found under " + modDir.string());

    Result result;
    std::vector<fs::path> createdBackups;
    bool manifestStarted = false, recoveryReady = false;
    try {
        // All originals and a checked manifest must exist before any target is
        // overwritten. Even a failure on the first target is now recoverable.
        for (const auto& entry : plan) if (entry.hadOriginal) {
            createdBackups.push_back(entry.backup);
            fs::copy_file(entry.target, entry.backup);
            result.backedUp.push_back(entry.relative);
        }
        {
            std::ofstream out;
            out.exceptions(std::ios::failbit | std::ios::badbit);
            manifestStarted = true;
            out.open(manifest, std::ios::binary);
            out << json{{"files", entries}}.dump(2);
            out.close();
        }
        recoveryReady = true;
        for (const auto& entry : plan) {
            fs::create_directories(entry.target.parent_path());
            fs::copy_file(entry.source, entry.target, fs::copy_options::overwrite_existing);
            result.staged.push_back(entry.relative);
        }
    } catch (const std::exception& e) {
        if (recoveryReady)
            throw std::runtime_error(std::string("stage: ") + e.what() + "; recovery manifest retained; run unstage or mods undeploy before retrying");
        // No target has changed yet. Remove only recovery files this call made.
        std::error_code ignored;
        if (manifestStarted) fs::remove(manifest, ignored);
        for (const auto& backup : createdBackups) fs::remove(backup, ignored);
        throw;
    }
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
        if (targetKey(entry.target) == targetKey(manifest) || recoveryPath(entry.relative) ||
            !targets.insert(targetKey(entry.target)).second)
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
