#pragma once

#include "forge/temporarydirectory.hpp"

namespace albion::detail {

// Exclusively owned scratch child, removed on normal or exceptional exit.
class TemporaryDirectory : public forge::TemporaryDirectory {
public:
    explicit TemporaryDirectory(const char* prefix)
        : forge::TemporaryDirectory(std::filesystem::temp_directory_path() / "FableForge", prefix) {}
};

} // namespace albion::detail
