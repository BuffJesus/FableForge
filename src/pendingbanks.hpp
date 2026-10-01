#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "backups.hpp"

namespace albion::detail {
namespace fs = std::filesystem;

// Build all changed banks before replacing any of them. The temporary directory is
// on the destination filesystem so installation and rollback use renames.
class PendingBanks {
    struct Bank {
        fs::path target, prepared, previous;
        bool saved = false, installed = false;
    };
    fs::path root_, staging_;
    std::vector<Bank> banks_;
    bool keepRecovery_ = false;
public:
    PendingBanks(const PendingBanks&) = delete;
    PendingBanks& operator=(const PendingBanks&) = delete;
    explicit PendingBanks(const fs::path& root, const std::string& prefix) : root_(fs::absolute(root).lexically_normal()) {
        fs::create_directories(root_);
        static std::atomic<uint64_t> serial{0};
        for (;;) {
            const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
            const fs::path candidate = root_ / (prefix + std::to_string(tick) + "-" + std::to_string(serial++));
            if (fs::create_directory(candidate)) { staging_ = candidate; break; }
        }
    }
    ~PendingBanks() {
        // A failed rollback retains its previous banks and reports this path.
        if (!keepRecovery_ && !staging_.empty() && staging_.parent_path() == root_) {
            std::error_code ec;
            fs::remove_all(staging_, ec);
        }
    }
    fs::path prepare(const fs::path& relative) {
        Bank bank{root_ / relative, staging_ / "new" / relative, staging_ / "previous" / relative};
        fs::create_directories(bank.prepared.parent_path());
        banks_.push_back(bank);
        return bank.prepared;
    }
    bool install(bool makeBackups, std::string& error) {
        try {
            // Complete the preflight and original backups before the first rename.
            for (const auto& bank : banks_) {
                if (!fs::is_regular_file(bank.prepared)) throw std::runtime_error("prepared bank is missing: " + bank.prepared.string());
                if (fs::exists(bank.target) && !fs::is_regular_file(bank.target))
                    throw std::runtime_error("bank destination is not a file: " + bank.target.string());
                fs::create_directories(bank.target.parent_path());
                fs::create_directories(bank.previous.parent_path());
                if (makeBackups && !backups::backupOnce(bank.target, error)) return false;
            }
            for (auto& bank : banks_) {
                if (fs::exists(bank.target)) { fs::rename(bank.target, bank.previous); bank.saved = true; }
                fs::rename(bank.prepared, bank.target);
                bank.installed = true;
            }
            return true;
        } catch (const std::exception& e) {
            error = e.what();
            for (auto it = banks_.rbegin(); it != banks_.rend(); ++it) {
                try {
                    if (it->installed) fs::rename(it->target, it->prepared);
                    if (it->saved) fs::rename(it->previous, it->target);
                } catch (const std::exception& restore) {
                    keepRecovery_ = true;
                    error += "; rollback failed: " + std::string(restore.what());
                }
            }
            if (keepRecovery_) error += "; recovery files retained in " + staging_.string();
            return false;
        }
    }
};

} // namespace albion::detail
