// probe: does forge::bwd::compileFromWld(FinalAlbion.wld) reproduce FinalAlbion.bwd?
#include <cstdio>
#include <filesystem>
#include <string>
#include "forge/bwd.hpp"
#include "forge/wld.hpp"

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    const std::filesystem::path levels = std::filesystem::path(argv[1]) / "data" / "Levels";
    try {
        const auto wld = forge::wld::File::parse(levels / "FinalAlbion.wld");
        const auto bwd = forge::bwd::File::parse(levels / "FinalAlbion.bwd");
        const forge::bwd::DimSource dims = [&](const std::string& levelName, int& w, int& h) {
            const std::string stem = std::filesystem::path(levelName).stem().string();
            for (const auto& m : bwd.maps())
                if (std::filesystem::path(m.levelName).stem().string() == stem) { w = m.right - m.left; h = m.bottom - m.top; return true; }
            return false;
        };
        const auto c = forge::bwd::compileFromWld(wld, dims);
        const auto a = c.serialize(), b = bwd.serialize();
        std::printf("compiled %zu bytes, on disk %zu bytes, maps %zu/%zu regions %zu/%zu\n", a.size(), b.size(), c.maps().size(), bwd.maps().size(), c.regions().size(), bwd.regions().size());
        size_t diff = 0, first = SIZE_MAX;
        for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) if (a[i] != b[i]) { ++diff; if (first == SIZE_MAX) first = i; }
        std::printf("%zu differing bytes, first at %zu\n", diff + (a.size() > b.size() ? a.size() - b.size() : b.size() - a.size()), first);
        for (size_t i = 0; i < std::min(c.maps().size(), bwd.maps().size()); ++i) {
            const auto& x = c.maps()[i]; const auto& y = bwd.maps()[i];
            if (x.levelName != y.levelName || x.scriptName != y.scriptName || x.used != y.used || x.loadedOnProximity != y.loadedOnProximity || x.isSea != y.isSea || x.left != y.left || x.right != y.right || x.top != y.top || x.bottom != y.bottom || x.flag2 != y.flag2 || x.mapUid != y.mapUid) {
                std::printf("map %zu differs: %s|%s used %d/%d prox %d/%d sea %d/%d box %d,%d,%d,%d / %d,%d,%d,%d flag2 %d/%d uid %llu/%llu\n", i + 1, x.levelName.c_str(), y.levelName.c_str(), x.used, y.used, x.loadedOnProximity, y.loadedOnProximity, x.isSea, y.isSea, x.left, x.top, x.right, x.bottom, y.left, y.top, y.right, y.bottom, x.flag2, y.flag2, (unsigned long long)x.mapUid, (unsigned long long)y.mapUid);
                if (diff-- < 5) {}
            }
        }
        for (size_t i = 0; i < std::min(c.regions().size(), bwd.regions().size()); ++i) {
            const auto& x = c.regions()[i]; const auto& y = bwd.regions()[i];
            if (x.contains != y.contains || x.sees != y.sees || x.name != y.name || x.displayName != y.displayName || x.regionDef != y.regionDef || x.minimapGraphic != y.minimapGraphic || x.onWorldMap != y.onWorldMap || x.creatureGen != y.creatureGen || x.soundThemes != y.soundThemes || x.mmOffX != y.mmOffX || x.mmOffY != y.mmOffY || x.wmOffX != y.wmOffX || x.wmOffY != y.wmOffY || x.exits.size() != y.exits.size())
                std::printf("region %zu %s differs (contains %zu/%zu sees %zu/%zu gen %d/%d snd %d/%d mm %d,%d/%d,%d wm %d,%d/%d,%d exits %zu/%zu def '%s'/'%s' mmg '%s'/'%s')\n", i + 1, y.name.c_str(), x.contains.size(), y.contains.size(), x.sees.size(), y.sees.size(), x.creatureGen, y.creatureGen, x.soundThemes, y.soundThemes, x.mmOffX, x.mmOffY, y.mmOffX, y.mmOffY, x.wmOffX, x.wmOffY, y.wmOffX, y.wmOffY, x.exits.size(), y.exits.size(), x.regionDef.c_str(), y.regionDef.c_str(), x.minimapGraphic.c_str(), y.minimapGraphic.c_str());
        }
    } catch (const std::exception& e) { std::printf("error: %s\n", e.what()); return 1; }
    return 0;
}
