#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace forge {

// Bounded std::filesystem::remove_all. libstdc++'s remove_all can spin forever on
// paths beyond MAX_PATH (seen in a long-path-aware build: its iterator erase never
// advances). List the tree once, then remove deepest entries first; every step is
// bounded and errors only leave files behind. Returns false if anything remains.
inline bool removeTree(const std::filesystem::path& root, std::error_code& error) {
    namespace fs = std::filesystem;
    error.clear();
    if (!fs::exists(root, error)) { error.clear(); return true; }
    std::vector<fs::path> entries;
    constexpr size_t kLimit = 1000000;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, error), end;
         !error && it != end && entries.size() < kLimit; it.increment(error))
        entries.push_back(it->path());
    bool removed = !error;
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
        std::error_code step;
        fs::remove(*it, step);
        if (step) { removed = false; error = step; }
    }
    std::error_code step;
    fs::remove(root, step);
    if (step) { removed = false; error = step; }
    return removed;
}

// Exclusively created scratch child; only that child is removed on scope exit.
class TemporaryDirectory {
    std::filesystem::path parent_, path_;
    bool retained_ = false;
public:
    TemporaryDirectory(const std::filesystem::path& parent, const std::string& prefix) {
        namespace fs = std::filesystem;
        const fs::path prefixPath(prefix);
        if (prefix.empty() || prefixPath.has_parent_path() || prefixPath.has_root_path())
            throw std::invalid_argument("temporary directory prefix must be a file-name prefix");
        parent_ = fs::absolute(parent).lexically_normal();
        fs::create_directories(parent_);
        static std::atomic<uint64_t> serial{0};
        for (;;) {
            const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
            const auto candidate = parent_ / (prefix + std::to_string(tick) + "-" + std::to_string(serial++));
            if (fs::create_directory(candidate)) { path_ = candidate; break; }
        }
    }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
    ~TemporaryDirectory() {
        if (!retained_ && !path_.empty() && path_.parent_path() == parent_) {
            std::error_code error;
            removeTree(path_, error);
        }
    }
    const std::filesystem::path& path() const { return path_; }
    // Keep this owned directory when a failed rollback needs manual recovery.
    void retain() noexcept { retained_ = true; }
};

} // namespace forge
