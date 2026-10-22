#pragma once
#include <atomic>
#include "forge/big.hpp"
#include <filesystem>
#include <string>
#include <vector>
#include <map>

namespace albion::textureproject {
std::vector<std::string> inspectSetup(const std::filesystem::path& root);
// Whitelisted game-relative destination, including legacy exported projects.
std::filesystem::path projectArchiveRelative(const std::filesystem::path& folder);
uint64_t rebuiltArchiveSize(const forge::big::File& file);
struct Options {
    bool imageSize = false;
    unsigned maxDimension = 512;
    // Caller must provision the audited Archive4GB runtime before deployment.
    // The default remains the stock engine's signed archive boundary.
    bool unsignedArchiveOffsets = false;
};
struct Result {
    bool ok = false;
    size_t exported = 0, originals = 0, matched = 0, unchanged = 0, changed = 0, removed = 0, missing = 0, skipped = 0;
    uint64_t beforeBytes = 0, afterBytes = 0, storedMipBytes = 0, allocationBytes = 0;
    std::string error, token;
    std::vector<std::string> details;
    std::map<std::string,double> timings;
};
struct Progress { std::atomic<size_t> done{0}, total{0}; std::atomic<bool> cancel{false}; size_t completedBefore = 0, totalOverride = 0; };
// Match flat original symbols or exported filenames/relative paths. Preparation
// creates a new subset project; neither input folder nor game data is modified.
Result matchReplacements(const std::filesystem::path& project,const std::filesystem::path& images,
                         const std::string& bank = {}, Progress* progress = nullptr);
Result prepareReplacements(const std::filesystem::path& project,const std::filesystem::path& images,
                           const std::filesystem::path& output,const std::string& expectedToken,
                           const std::string& bank = {}, Progress* progress = nullptr);
// Each project owns a new folder. Existing folders are never overwritten.
Result exportProject(const std::filesystem::path& archive, const std::filesystem::path& folder,
                     const std::string& bank = {}, const std::string& filter = {}, Progress* progress = nullptr,
                     bool includeOriginals = false, int pngCompressionLevel = 3);
// Review encodes changed images and computes exact rebuilt archive size without touching the install.
Result review(const std::filesystem::path& archive, const std::filesystem::path& folder,
              Options options = {}, Progress* progress = nullptr);
Result reviewPack(const std::filesystem::path& archive, const std::filesystem::path& folder,
                  const std::filesystem::path& pack, Options options = {}, Progress* progress = nullptr);
// Apply rechecks the review token and uses the existing backup/transaction machinery.
Result publishPack(const std::filesystem::path& archive, const std::filesystem::path& folder,
                   const std::filesystem::path& pack, Options options, const std::string& expectedToken,
                   Progress* progress = nullptr);
Result apply(const std::filesystem::path& root, const std::filesystem::path& folder,
             Options options, const std::string& expectedToken, Progress* progress = nullptr);
}
