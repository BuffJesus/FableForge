#pragma once
#include "textureproject.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace textureimport {
struct Project {
    std::filesystem::path folder;   // export folder holding texture-project.json
    std::filesystem::path archive;  // game archive it rebuilds
    albion::textureproject::Result review;
    std::string destinationHash;    // installed archive at review time
};
struct Plan {
    std::vector<Project> projects;
    size_t changed = 0, unchanged = 0, missing = 0;
    size_t restoredArchives = 0;
    bool needsArchive4GB = false;
    std::string error;
    bool hasChanges() const { return changed!=0 || restoredArchives!=0; }
};
struct Outcome {
    bool ok = false;
    size_t imported = 0, resized = 0;
    unsigned largest = 0;            // widest/tallest allocation among resized textures
    size_t restoredArchives = 0;
    bool archive4GBInstalled = false;
    std::vector<std::string> notes;
    std::string error;
};
// Finds the main and front-end projects of a "Fable textures" export and reviews
// every edited PNG against the installed archives. Nothing is written.
Plan review(const std::filesystem::path& root, const std::filesystem::path& exportFolder,
            albion::textureproject::Progress* progress = nullptr);
// Applies a reviewed plan (originals are kept once as <archive>.forge-orig), then
// writes Mods/TextureSizeFix/TextureSizes.dat and installs TextureSizeFix.dll from
// `runtime` so interface textures keep their original on-screen size.
Outcome apply(const std::filesystem::path& root, const Plan& plan, const std::filesystem::path& runtime,
              albion::textureproject::Progress* progress = nullptr);
// TextureSizes.dat bytes for every bank whose frame sizes differ from its original.
std::vector<unsigned char> sizeData(const std::filesystem::path& root, size_t& resized, unsigned& largest);
}
