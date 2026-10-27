// forge -- the FableForge command line. The commands live in src/cli/*.cpp, one file per
// family; this is only the dispatch. `forge --help` prints the usage (docs/CLI.md is
// generated from it).
#include <cstdio>
#include <string>
#include <vector>

#include "cli/common.hpp"
#include "windowsprocess.hpp"

int main(int argc, char** argv) try {
    using namespace albion::cli;
    const Args args = albion::process::arguments(argc, argv);
    if (args.empty() || args[0] == "-h" || args[0] == "--help") return usage();
    const std::string cmd = args[0];
    if (const auto r = runLevels(cmd, args)) return *r;
    if (const auto r = runTextures(cmd, args)) return *r;
    if (const auto r = runInstall(cmd, args)) return *r;
    if (const auto r = runCreatures(cmd, args)) return *r;
    if (const auto r = runWorld(cmd, args)) return *r;
    if (const auto r = runChunks(cmd, args)) return *r;
    if (const auto r = runNav(cmd, args)) return *r;
    return runExport(cmd, args);
} catch (const std::exception& error) {
    std::fprintf(stderr, "error: %s\n", error.what());
    return 1;
}
