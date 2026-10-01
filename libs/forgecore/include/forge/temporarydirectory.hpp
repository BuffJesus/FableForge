#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace forge {

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
            std::filesystem::remove_all(path_, error);
        }
    }
    const std::filesystem::path& path() const { return path_; }
    // Keep this owned directory when a failed rollback needs manual recovery.
    void retain() noexcept { retained_ = true; }
};

} // namespace forge
