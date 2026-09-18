// Presets: a saved selection (a Document::Fragment) as a small TNG-shaped text file that
// places with one click. Two folders: the ones shipped next to the exe (`presets/`) and
// the user's own (%APPDATA%/AlbionAtlas/presets). Format:
//
//   // Albion Atlas preset: <name>
//   // <description>
//   Version 2;
//   XXXSectionStart NULL;
//   <the thing blocks, verbatim retail spelling>
//   XXXSectionEnd;
//
// so a preset is also a valid loose .tng (opens anywhere a .tng does). Positions are the
// source map's; the centroid is re-derived on load and the paste lands it where asked.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "leveledit.hpp"

namespace albion::editor {

struct PresetInfo {
    std::string name;          // from the header comment (file stem when absent)
    std::string description;
    std::filesystem::path file;
    size_t things = 0;
    bool user = false;         // lives in the user folder (deletable / overwritable)
};

// Every *.preset.tng in the given folders (missing folders are skipped), shipped first.
std::vector<PresetInfo> listPresets(const std::vector<std::filesystem::path>& folders);
bool loadPreset(const std::filesystem::path& file, Document::Fragment& out, std::string& error);
bool savePreset(const std::filesystem::path& file, const std::string& name, const std::string& description,
                const Document::Fragment& fragment, std::string& error);
// Name -> file stem: letters, digits, '_' and '-'.
std::string presetSlug(const std::string& name);

}  // namespace albion::editor
