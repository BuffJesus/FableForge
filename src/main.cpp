// forge -- the FableForge command line. The commands live in src/cli/*.cpp, one file per
// family; this is only the dispatch. `forge --help` prints the usage (docs/CLI.md is
// generated from it).
#include <string>
#include <vector>

#include "cli/common.hpp"

int main(int argc, char** argv) {
    using namespace albion::cli;
    const Args args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "-h" || args[0] == "--help") return usage();
    const std::string cmd = args[0];
    if (const auto r = runLevels(cmd, args)) return *r;
    if (const auto r = runTextures(cmd, args)) return *r;
    if (const auto r = runInstall(cmd, args)) return *r;
    if (const auto r = runWorld(cmd, args)) return *r;
    if (const auto r = runChunks(cmd, args)) return *r;
    return runExport(cmd, args);
}
