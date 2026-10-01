#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

namespace albion::detail {

// Exclusively owned scratch child, removed on normal or exceptional exit.
class TemporaryDirectory {
    std::filesystem::path parent_, path_;
public:
    explicit TemporaryDirectory(const char* prefix) {
        namespace fs = std::filesystem;
        parent_ = fs::absolute(fs::temp_directory_path() / "FableForge").lexically_normal();
        fs::create_directories(parent_);
        static std::atomic<uint64_t> serial{0};
        for (;;) {
            const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
            const auto candidate = parent_ / (std::string(prefix) + std::to_string(tick) + "-" + std::to_string(serial++));
            if (fs::create_directory(candidate)) { path_ = candidate; break; }
        }
    }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
    ~TemporaryDirectory() {
        if (!path_.empty() && path_.parent_path() == parent_) {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }
    }
    const std::filesystem::path& path() const { return path_; }
};

} // namespace albion::detail
