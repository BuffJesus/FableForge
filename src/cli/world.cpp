#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <charconv>
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
#include <string_view>
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

// forge CLI: the overworld (WLD/BWD) and regions
std::optional<int> runWorld(const std::string& cmd, const Args& args) {
    if (cmd == "region-props") {   // region-props <region> [--def REGION_X] [--minimap MINIMAP_X] [--display NAME] [--worldmap 0|1] [--install root]
        if (args.size() < 3) { std::fprintf(stderr, "usage: forge region-props <region> [--def <REGION_DEF>] [--minimap <MINIMAP_GRAPHIC>] [--display <name>] [--worldmap 0|1] [--install <root>]\n"); return 2; }
        std::string installArg; albion::editor::RegionProps props;
        for (size_t i = 2; i < args.size(); i += 2) {
            if (i + 1 == args.size()) { std::fprintf(stderr, "option %s needs a value\n", args[i].c_str()); return 2; }
            if (args[i] == "--install") installArg = args[i + 1];
            else if (args[i] == "--def") props.regionDef = args[i + 1];
            else if (args[i] == "--minimap") props.minimapGraphic = args[i + 1];
            else if (args[i] == "--display") props.displayName = args[i + 1];
            else if (args[i] == "--worldmap") {
                if (args[i + 1] != "0" && args[i + 1] != "1") { std::fprintf(stderr, "--worldmap expects 0 or 1\n"); return 2; }
                props.onWorldMap = args[i + 1] == "1" ? 1 : 0;
            }
            else { std::fprintf(stderr, "unknown option %s\n", args[i].c_str()); return 2; }
        }
        const Install install = findInstall(installArg);
        if (!install.valid) { std::fprintf(stderr, "no Fable install (use --install)\n"); return 2; }
        if (!allowInstallWrite(install)) return 1;
        std::vector<std::string> notes; std::string err;
        if (!albion::editor::setRegionProperties(install.root, args[1], props, notes, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        for (const auto& n : notes) std::printf("  %s\n", n.c_str());
        return 0;
    }
    if (cmd == "world-stitch") {   // world-stitch <map> [<map2>] [--feather n] [--dry-run] [--install root]
        std::string installArg; std::vector<std::string> maps; albion::editor::StitchOptions so;
        for (size_t i = 1; i < args.size(); ++i) {
            const auto& option = args[i];
            if (option == "--install" || option == "--feather") {
                if (i + 1 == args.size() || args[i + 1].empty() || args[i + 1].starts_with("--")) {
                    std::fprintf(stderr, "%s needs a value\n", option.c_str()); return 2;
                }
                const auto& value = args[++i];
                if (option == "--install") installArg = value;
                else if (value == "auto") so.feather = -1;
                else {
                    std::string_view number(value);
                    if (number.size() > 1 && number.front() == '+' && number[1] != '-') number.remove_prefix(1);
                    const auto parsed = std::from_chars(number.data(), number.data() + number.size(), so.feather);
                    if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size()) {
                        std::fprintf(stderr, "--feather expects auto or a complete decimal integer in the signed 32-bit range\n"); return 2;
                    }
                }
            }
            else if (option == "--dry-run") so.deploy = false;
            else if (option.empty() || option.starts_with("--")) {
                std::fprintf(stderr, "unknown world-stitch argument %s\n", option.c_str()); return 2;
            }
            else maps.push_back(option);
        }
        if (maps.empty() || maps.size() > 2) { std::fprintf(stderr, "usage: forge world-stitch <map> [<map2>] [--feather <cells>] [--dry-run] [--install <root>]\n"); return 2; }
        const Install install = findInstall(installArg);
        if (!install.valid) { std::fprintf(stderr, "no Fable install (use --install)\n"); return 2; }
        if (so.deploy && !allowInstallWrite(install)) return 1;
        albion::editor::WorldLayout layout; std::string err; std::vector<std::string> notes; std::vector<albion::editor::StitchReport> reports;
        if (!albion::editor::loadWorldLayout(install.root, layout, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        bool ok;
        if (maps.size() == 2) { albion::editor::StitchReport r; ok = albion::editor::stitchEdges(install.root, layout, maps[0], maps[1], so, r, notes, err); reports.push_back(r); }
        else ok = albion::editor::stitchNeighbours(install.root, layout, maps[0], so, reports, notes, err);
        for (const auto& n : notes) std::printf("  %s\n", n.c_str());
        if (!ok) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        size_t done = 0; for (const auto& r : reports) done += r.stitched;
        std::printf("%zu seam(s) checked, %zu stitched\n", reports.size(), done);
        return 0;
    }
    if (cmd == "world" || cmd == "world-move" || cmd == "world-owner" || cmd == "world-sees") {
        // world [--regions]: list the layout; world-move <map> <x> <y> [...]: relocate maps;
        // world-owner <map> <region>: change the owning region; world-sees <region> <map> <0|1>: visibility
        std::string installArg;
        std::vector<albion::editor::MapMove> moves;
        std::vector<albion::editor::OwnerEdit> owners;
        std::vector<albion::editor::SeesEdit> sees;
        bool regionsList = false, stitch = false;
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--install" && i + 1 < args.size()) installArg = args[++i];
            else if (cmd == "world-move" && args[i] == "--stitch") stitch = true;
            else if (cmd == "world" && args[i] == "--regions") regionsList = true;
            else if (cmd == "world-move" && i + 2 < args.size()) {
                const auto coordinate = [](std::string_view value, int& out) {
                    if (value.size() > 1 && value.front() == '+' && value[1] != '-') value.remove_prefix(1);
                    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), out);
                    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
                };
                albion::editor::MapMove move{args[i]};
                if (!coordinate(args[i + 1], move.x) || !coordinate(args[i + 2], move.y)) {
                    std::fprintf(stderr, "world-move coordinates must be complete decimal integers in the signed 32-bit range\n"); return 2;
                }
                moves.push_back(move); i += 2;
            }
            else if (cmd == "world-owner" && i + 1 < args.size()) { owners.push_back({args[i], args[i + 1]}); i += 1; }
            else if (cmd == "world-sees" && i + 2 < args.size()) {
                if (args[i + 2] != "0" && args[i + 2] != "1") { std::fprintf(stderr, "world-sees expects 0 or 1\n"); return 2; }
                sees.push_back({args[i], args[i + 1], args[i + 2] == "1"}); i += 2;
            }
            else { std::fprintf(stderr, "usage: forge world [--regions] | world-move <map> <x> <y> [...] | world-owner <map> <region> | world-sees <region> <map> <0|1>  [--install <root>]\n"); return 2; }
        }
        if (cmd == "world-owner" || cmd == "world-sees") {
            const Install install = findInstall(installArg);
            if (!install.valid) { std::fprintf(stderr, "no Fable install (use --install)\n"); return 2; }
            if (!allowInstallWrite(install)) return 1;
            std::vector<std::string> notes; std::string err;
            if (!albion::editor::applyWorldEdits(install.root, {}, owners, sees, notes, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
            for (const auto& n : notes) std::printf("  %s\n", n.c_str());
            return 0;
        }
        const Install install = findInstall(installArg);
        if (!install.valid) { std::fprintf(stderr, "no Fable install (use --install)\n"); return 2; }
        if (cmd == "world-move" && !allowInstallWrite(install)) return 1;
        albion::editor::WorldLayout layout; std::string err;
        if (!albion::editor::loadWorldLayout(install.root, layout, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        if (cmd == "world" && regionsList) {
            for (const auto& r : layout.regionInfo) {
                std::printf("%4d  %-32s owns %zu, sees %zu\n", r.slot, r.name.c_str(), r.contains.size(), r.sees.size());
                std::string c, v;
                for (const auto& m : r.contains) c += (c.empty() ? "" : ", ") + m;
                for (const auto& m : r.sees) v += (v.empty() ? "" : ", ") + m;
                if (!c.empty()) std::printf("        owns: %s\n", c.c_str());
                if (!v.empty()) std::printf("        sees: %s\n", v.c_str());
            }
            return 0;
        }
        if (cmd == "world") {
            std::printf("%zu maps, %zu regions, world box (%d,%d)-(%d,%d)\n", layout.maps.size(), layout.regions.size(), layout.minX, layout.minY, layout.maxX, layout.maxY);
            for (const auto& m : layout.maps) {
                std::string baked = m.inStb ? "stb" : "-";
                if (m.inStb && (m.stbX != m.x || m.stbY != m.y)) baked += " (baked at " + std::to_string(m.stbX) + "," + std::to_string(m.stbY) + ")";
                std::printf("%4d  %-40s %5d %5d  %4dx%-4d %-28s %s\n", m.slot, m.name.c_str(), m.x, m.y, m.w, m.h, m.region.c_str(), baked.c_str());
            }
            return 0;
        }
        if (moves.empty()) { std::fprintf(stderr, "usage: forge world-move <map> <x> <y> [...] [--install <root>]\n"); return 2; }
        for (const auto& mv : moves) {
            std::string why;
            if (!albion::editor::checkMove(layout, moves, mv, why)) { std::fprintf(stderr, "error: %s: %s\n", mv.name.c_str(), why.c_str()); return 1; }
            const auto* b = layout.find(mv.name);
            std::printf("%s: (%d,%d) -> (%d,%d)\n", b->name.c_str(), b->x, b->y, mv.x, mv.y);
        }
        std::vector<std::string> notes;
        if (!albion::editor::applyMoves(install.root, moves, notes, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        for (const auto& n : notes) std::printf("  %s\n", n.c_str());
        std::printf("moved %zu map(s)\n", moves.size());
        if (stitch) {
            // the seams of every moved map at its new placement
            albion::editor::WorldLayout after; std::vector<std::string> snotes; std::vector<albion::editor::StitchReport> reports;
            if (!albion::editor::loadWorldLayout(install.root, after, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
            bool ok = true;
            for (const auto& mv : moves) ok = albion::editor::stitchNeighbours(install.root, after, mv.name, {}, reports, snotes, err) && ok;
            for (const auto& n : snotes) std::printf("  %s\n", n.c_str());
            if (!ok) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
            size_t done = 0; for (const auto& r : reports) done += r.stitched;
            std::printf("%zu seam(s) checked, %zu stitched\n", reports.size(), done);
        }
        return 0;
    }
    if (cmd == "minimap-register") {   // minimap-register <MINIMAP_NAME> <texture id> [--install <root>]: PLAYER_GUI MiniMapGraphics entry
        if (args.size() < 3) { std::fprintf(stderr, "usage: forge minimap-register <name> <id> [--install <root>]\n"); return 2; }
        std::string_view value = args[2];
        if (!value.empty() && value.front() == '+') value.remove_prefix(1);
        int base = 10;
        if (value.size() > 1 && value.front() == '0') {
            if (value[1] == 'x' || value[1] == 'X') { base = 16; value.remove_prefix(2); }
            else base = 8;
        }
        uint32_t id = 0;
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), id, base);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
            std::fprintf(stderr, "invalid minimap texture id: %s (expected an unsigned 32-bit integer)\n", args[2].c_str());
            return 2;
        }
        std::string installArg;
        for (size_t i = 3; i < args.size(); ++i) {
            if (args[i] != "--install" || i + 1 == args.size()) {
                std::fprintf(stderr, "usage: forge minimap-register <name> <id> [--install <root>]\n"); return 2;
            }
            installArg = args[++i];
        }
        const Install install = findInstall(installArg);
        if (!install.valid) { std::fprintf(stderr, "no Fable install (use --install)\n"); return 2; }
        if (!allowInstallWrite(install)) return 1;
        std::vector<std::string> notes; std::string err;
        if (!albion::editor::registerMinimapGraphic(install.root, args[1], id, notes, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        for (const auto& n : notes) std::printf("  %s\n", n.c_str());
        return 0;
    }
    return std::nullopt;
}

}  // namespace albion::cli
