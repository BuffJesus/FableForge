#pragma once
// The backup manager. Every writer in FableForge keeps a one-time copy of a file it
// is about to change as `<file>.forge-orig` (the untouched original) and marks
// a file it created from nothing with `<file>.forge-created`; installs from before
// the rename carry the same as `.atlas-orig` / `.atlas-created` and are read as-is
// (a file keeps whichever it has; nothing is renamed). The other two conventions
// on an install are known too: `<file>.forgebak` is what a staged deploy (mod
// packs, forge-tools stage) put aside -- transient, consumed by Undeploy -- and
// `<file>.ovrbak` is FableTLC's overlay installer's original. This module finds
// all of them under an install and puts things back: an original is copied
// over the live file (the backup stays, it is still the baseline), a created
// file is deleted with its marker, a staged set is reverted through its manifest.
// Nothing is restored while Fable.exe runs.
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace albion::backups {

constexpr const char* kOrigSuffix = ".forge-orig";
constexpr const char* kCreatedSuffix = ".forge-created";
constexpr const char* kLegacyOrigSuffix = ".atlas-orig";        // installs touched before the rename
constexpr const char* kLegacyCreatedSuffix = ".atlas-created";
constexpr const char* kStagedSuffix = ".forgebak";              // forge::stage (mod deploy)
constexpr const char* kOverlaySuffix = ".ovrbak";               // FableTLC's overlay installer

enum class Kind { Original, Created, Staged, Overlay };

// The one-time original of `file`: <file>.forge-orig, or the legacy <file>.atlas-orig when that
// is what the install already has (so a file never gets two).
std::filesystem::path originalOf(const std::filesystem::path& file);
bool hasOriginal(const std::filesystem::path& file);
// Copy `file` to its original once; a no-op when an original exists or the file does not.
bool backupOnce(const std::filesystem::path& file, std::string& error);
// Mark `file` as created by FableForge (restore deletes it).
void markCreated(const std::filesystem::path& file);

struct Entry {
    std::filesystem::path file;      // the live file
    std::filesystem::path backup;    // the original copy, the created marker, the .forgebak or the .ovrbak
    Kind kind = Kind::Original;
    bool created = false;            // true = FableForge created `file`; restore deletes it
    bool differs = false;            // the live file differs from the backup (or exists, for created)
    uintmax_t size = 0;              // live file size
    std::string when;                // backup / marker mtime, "YYYY-MM-DD HH:MM"
};

// Every backup and creation marker under the install (Levels, the loose
// FinalAlbion folder, CompiledDefs, graphics/pc, FSE, the root BWD, Saves).
std::vector<Entry> scan(const std::filesystem::path& gameRoot);

// Fable.exe running? (restores are refused then: the engine holds the files open)
bool gameRunning();
// Fable.exe running FROM this install (its image path under gameRoot)? A game
// started from another copy does not hold this root's files. When the path
// cannot be read (a protected process) the answer is the conservative one: true.
bool gameRunningIn(const std::filesystem::path& gameRoot);

// Put one entry back. `keepBackup` = leave the original in place (default;
// it stays the baseline for the next edit); false removes it after restoring.
// A staged (.forgebak) entry is always copied back with its backup kept: the
// stage manifest still refers to it (restoreAll reverts the whole stage instead).
bool restore(const Entry& e, bool keepBackup, std::string& error);
// Everything scan() found: a staged deploy is reverted through forge::stage first
// (its manifest), then every original goes back. Returns the number restored.
size_t restoreAll(const std::filesystem::path& gameRoot, bool keepBackup, std::vector<std::string>& notes, std::string& error);

} // namespace albion::backups
