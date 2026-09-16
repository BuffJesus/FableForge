#pragma once
// Loose-file mod staging for a Fable TLC install. The engine prefers loose
// files under Data\Levels over FinalAlbion.wad entries (verified against the
// retail install, which ships 397 loose TNGs overriding the WAD), so staging
// copies a mod's files into the game tree instead of repacking archives.
// Every overwritten original is backed up as "<file>.forgebak" and recorded in
// a manifest (forge_stage_manifest.json in the game root) so unstage() can
// restore the install exactly.

#include <filesystem>
#include <string>
#include <vector>

namespace forge::stage {

struct Result {
    std::vector<std::string> staged;   // game-root-relative paths written
    std::vector<std::string> backedUp; // subset that had originals backed up
    std::vector<std::string> restored; // unstage: files restored from backup
    std::vector<std::string> removed;  // unstage: staged files with no original
};

// Copy every file under modDir (mirroring the game tree, e.g.
// modDir/Data/Levels/FinalAlbion/Foo.tng) into gameRoot. Refuses to run if a
// manifest already exists (unstage first). Returns what was done.
Result apply(const std::filesystem::path& gameRoot,
             const std::filesystem::path& modDir);

// Undo a previous apply() using the manifest: restore backups, delete staged
// files that had no original, remove the manifest. Returns what was done.
Result revert(const std::filesystem::path& gameRoot);

// Path of the manifest a stage would write.
std::filesystem::path manifestPath(const std::filesystem::path& gameRoot);

} // namespace forge::stage
