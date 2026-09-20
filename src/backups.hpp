#pragma once
// The backup manager. Every writer in FableForge keeps a one-time copy of a file it
// is about to change as `<file>.atlas-orig` (the untouched original) and marks
// a file it created from nothing with `<file>.atlas-created`. This module finds
// all of them under an install and puts things back: an original is copied
// over the live file (the backup stays, it is still the baseline), a created
// file is deleted with its marker. Nothing is restored while Fable.exe runs.
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace albion::backups {

struct Entry {
    std::filesystem::path file;      // the live file
    std::filesystem::path backup;    // the .atlas-orig copy, or the .atlas-created marker
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

// Put one entry back. `keepBackup` = leave the .atlas-orig in place (default;
// it stays the baseline for the next edit); false removes it after restoring.
bool restore(const Entry& e, bool keepBackup, std::string& error);
// Everything scan() found. Returns the number restored; `notes` says what.
size_t restoreAll(const std::filesystem::path& gameRoot, bool keepBackup, std::vector<std::string>& notes, std::string& error);

} // namespace albion::backups
