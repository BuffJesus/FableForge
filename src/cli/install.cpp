#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "forge/big.hpp"
#include "forge/env.hpp"
#include "forge/meshpreview.hpp"
#include "forge/lev.hpp"
#include "forge/lzo.hpp"
#include "forge/stb.hpp"
#include "forge/stbbake.hpp"
#include "forge/stbheightbake.hpp"
#include "forge/wad.hpp"
#include "effects.hpp"
#include "foliageexport.hpp"
#include "stbterrain.hpp"
#include "thingsexport.hpp"
#include "terrainexport.hpp"
#include "worldedit.hpp"
#include "overworld.hpp"
#include "gtg.hpp"
#include "texturebrowse.hpp"
#include "leveledit.hpp"
#include "stbrelocate.hpp"
#include "stitch.hpp"
#include "backups.hpp"
#include "lodbake.hpp"
#include "dxt1.hpp"
#include "forge/stbinfo.hpp"

namespace fs = std::filesystem;
namespace te = albion::terrainexport;
#include "cli/common.hpp"

namespace albion::cli {

// forge CLI: backups and restore
std::optional<int> runInstall(const std::string& cmd, const Args& args) {
    if (cmd == "backups" || cmd == "restore") {   // backups [--install root]: list; restore [--forget] [--install root]: put every backed-up file back (refused while the game runs)
        std::string installArg; bool forget = false;
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--install" && i + 1 < args.size()) installArg = args[++i];
            else if (args[i] == "--forget") forget = true;
            else { std::fprintf(stderr, "usage: forge backups | restore [--forget] [--install <root>]\n"); return 2; }
        }
        const Install install = findInstall(installArg);
        if (!install.valid) { std::fprintf(stderr, "no Fable install (use --install)\n"); return 2; }
        const auto entries = albion::backups::scan(install.root);
        if (cmd == "backups") {
            size_t changed = 0;
            for (const auto& e : entries) {
                std::printf("  %s  %-70s %s\n", e.differs ? (e.created ? "NEW " : "EDIT") : "same", e.file.string().c_str(), e.when.c_str());
                changed += e.differs;
            }
            std::printf("%zu backed-up file(s), %zu differ from their backup%s\n", entries.size(), changed, albion::backups::gameRunning() ? " (Fable.exe is running)" : "");
            return 0;
        }
        std::vector<std::string> notes; std::string err;
        const size_t n = albion::backups::restoreAll(install.root, !forget, notes, err);
        for (const auto& x : notes) std::printf("  %s\n", x.c_str());
        if (!err.empty() && n == 0) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        std::printf("%zu file(s) restored%s\n", n, forget ? ", backups removed" : "");
        return err.empty() ? 0 : 1;
    }
    return std::nullopt;
}

}  // namespace albion::cli
