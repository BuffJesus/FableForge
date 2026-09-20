// Compacting the install's static-map bank (FinalAlbion_RT.stb). Every deploy
// that changes a chunk's size appends the new payload and a cloned table and
// leaves the old ones behind; retail's own bake did the same (~3.4 MB of dead
// bytes ship in the retail bank). This rewrites the bank as live payloads +
// one table, takes the one-time .forge-orig backup like a deploy, verifies
// every payload byte-identical through a fresh parse, and swaps the file in.
// A running game holds the bank open, so the swap fails there and nothing is lost.
#pragma once

#include <filesystem>
#include <string>

#include "forge/stb.hpp"

namespace albion::stbcompact {

struct Result {
    bool ok = false;
    bool alreadyCompact = false;
    std::string error;
    forge::stb::CompactReport report;
};

std::filesystem::path bankPath(const std::filesystem::path& installRoot);

// Header + table only; safe to poll.
forge::stb::CompactReport measure(const std::filesystem::path& installRoot);

Result compact(const std::filesystem::path& installRoot);

}  // namespace albion::stbcompact
