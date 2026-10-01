#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
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
namespace {
template<class T> bool completeNumber(std::string_view value, T& out) {
    if (!value.empty() && value.front() == '+') {
        value.remove_prefix(1);
        if (!value.empty() && value.front() == '-') return false;
    }
    if (value.empty()) return false;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), out);
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() && std::isfinite(out);
}
bool integerPair(std::string_view value, char separator, int& first, int& second) {
    const auto split = value.find(separator);
    return split != std::string_view::npos && completeNumber(value.substr(0, split), first) &&
           completeNumber(value.substr(split + 1), second);
}
} // namespace

// forge CLI: new levels and region entrances
std::optional<int> runLevels(const std::string& cmd, const Args& args) {
    if (cmd == "blank-level") {   // blank-level <name> [--at x,y] [--region <host>] [--template <64x64 map>] [--theme <slot|name>] [--height h] [--install <root>]
        if (args.size() < 2) { std::fprintf(stderr, "usage: forge blank-level <name> [--size WxH] [--at x,y] [--region <hostRegion>] [--template <map>] [--theme <slot|name>] [--height <h>] [--install <root>]\n"); return 2; }
        albion::editor::BlankLevelRequest req;
        req.name = args[1];
        std::string installArg, theme; bool hasOrigin = false;
        for (size_t i = 2; i < args.size(); ++i) {
            if (args[i] == "--install" && i + 1 < args.size()) installArg = args[++i];
            else if (args[i] == "--at" && i + 1 < args.size()) {
                if (!integerPair(args[++i], ',', req.worldX, req.worldY)) { std::fprintf(stderr, "bad --at %s (expected complete decimal x,y)\n", args[i].c_str()); return 2; }
                hasOrigin = true;
            }
            else if (args[i] == "--region" && i + 1 < args.size()) req.hostRegion = args[++i];
            else if (args[i] == "--template" && i + 1 < args.size()) req.templateLevel = args[++i];
            else if (args[i] == "--theme" && i + 1 < args.size()) {
                theme = args[++i];
                if (theme.empty()) { std::fprintf(stderr, "--theme needs a palette slot or theme name\n"); return 2; }
                if (std::isdigit(static_cast<unsigned char>(theme.front())) || theme.front() == '+' || theme.front() == '-') {
                    if (!completeNumber(theme, req.themeSlot) || req.themeSlot < 0 || req.themeSlot > 255) {
                        std::fprintf(stderr, "bad --theme %s (expected palette slot 0..255 or theme name)\n", theme.c_str()); return 2;
                    }
                    theme.clear();
                } else req.themeSlot = -1;
            }
            else if (args[i] == "--height" && i + 1 < args.size()) {
                if (!completeNumber(args[++i], req.groundHeight)) { std::fprintf(stderr, "bad --height %s (expected a complete finite number)\n", args[i].c_str()); return 2; }
            }
            else if (args[i] == "--size" && i + 1 < args.size()) { if (!integerPair(args[++i], 'x', req.width, req.height)) { std::fprintf(stderr, "bad --size %s (WxH)\n", args[i].c_str()); return 2; } }
            else if (args[i] == "--own-region") { req.ownRegion.wanted = true; if (i + 1 < args.size() && args[i + 1].rfind("--", 0) != 0) { req.ownRegion.takeOver = args[++i]; if (req.ownRegion.takeOver == "new") { req.ownRegion.dedicated = true; req.ownRegion.takeOver.clear(); } } }
            else if (args[i] == "--merge-into" && i + 1 < args.size()) req.ownRegion.mergeInto = args[++i];
            else if (args[i] == "--display" && i + 1 < args.size()) req.ownRegion.displayName = args[++i];
            else if (args[i] == "--no-minimap") req.ownRegion.minimap = false;
            else { std::fprintf(stderr, "unknown option %s\n", args[i].c_str()); return 2; }
        }
        const Install install = findInstall(installArg);
        if (!install.valid) { std::fprintf(stderr, "no Fable install (use --install)\n"); return 2; }
        if (!allowInstallWrite(install)) return 1;
        albion::editor::DonorInfo info; std::string err;
        if (req.templateLevel.empty()) {
            std::string serr;
            for (const auto& s : albion::editor::retailMapSizes(install.root, serr))
                if (s.width == req.width && s.height == req.height) { req.templateLevel = s.templateLevel; break; }
            if (req.templateLevel.empty()) { std::fprintf(stderr, "no retail map is %dx%d; sizes available:", req.width, req.height); for (const auto& s : albion::editor::retailMapSizes(install.root, serr)) std::fprintf(stderr, " %dx%d", s.width, s.height); std::fprintf(stderr, "\n"); return 2; }
        }
        if (!albion::editor::donorInfo(install.root, req.templateLevel, info, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        if (!hasOrigin) { req.worldX = info.suggestedX; req.worldY = info.suggestedY; }
        if (req.hostRegion.empty()) req.hostRegion = info.owningRegion;
        if (req.ownRegion.wanted && !req.ownRegion.dedicated) {
            std::string rerr;
            const auto rr = albion::editor::reusableRegions(install.root, rerr);
            std::printf("reusable filler regions:"); for (const auto& r : rr) std::printf(" %s(slot %d, %d maps)", r.name.c_str(), r.slot, r.maps); std::printf("\n");
        }
        if (!theme.empty()) {
            LevelWorkspace temp;
            const auto tl = forge::lev::File::open(resolveLevel(req.templateLevel, install, temp));
            for (size_t i = 0; i < tl.groundThemes().size(); ++i) if (tl.groundThemes()[i].name == theme) req.themeSlot = int(i);
            if (req.themeSlot < 0) { std::fprintf(stderr, "--theme %s is not in %s's palette\n", theme.c_str(), req.templateLevel.c_str()); return 2; }
        }
        albion::terrainexport::Context ctx;
        if (!ctx.loadDefs(install.root, err) || !ctx.themeLibrary()) { std::fprintf(stderr, "cannot load the ENGINE_THEME library: %s\n", err.c_str()); return 1; }
        std::printf("blank level %s at (%d,%d) owned by %s, template %s, height %g\n", req.name.c_str(), req.worldX, req.worldY, req.hostRegion.c_str(), req.templateLevel.c_str(), req.groundHeight);
        albion::editor::NewLevelResult out;
        if (!albion::editor::createBlankLevel(install.root, req, *ctx.themeLibrary(), out, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        for (const auto& n : out.notes) std::printf("  %s\n", n.c_str());
        std::printf("installed: map slot %d, box (%d,%d)-(%d,%d)\n", out.mapSlot, out.worldX, out.worldY, out.worldX + out.width, out.worldY + out.height);
        return 0;
    }
    if (cmd == "new-level") {   // new-level <donor> <name> [--at x,y] [--region <host>] [--dedicated] [--no-rebake] [--install <root>]
        if (args.size() < 3) { std::fprintf(stderr, "usage: forge new-level <donor> <name> [--at x,y] [--region <hostRegion>] [--dedicated] [--no-rebake] [--install <root>]\n"); return 2; }
        albion::editor::NewLevelRequest req;
        req.donor = args[1]; req.name = args[2];
        std::string installArg; bool dedicated = false, hasOrigin = false;
        for (size_t i = 3; i < args.size(); ++i) {
            if (args[i] == "--install" && i + 1 < args.size()) installArg = args[++i];
            else if (args[i] == "--at" && i + 1 < args.size()) {
                if (!integerPair(args[++i], ',', req.worldX, req.worldY)) { std::fprintf(stderr, "bad --at %s (expected complete decimal x,y)\n", args[i].c_str()); return 2; }
                hasOrigin = true;
            }
            else if (args[i] == "--region" && i + 1 < args.size()) req.hostRegion = args[++i];
            else if (args[i] == "--dedicated") dedicated = true;
            else if (args[i] == "--no-rebake") req.rebakeChunk = false;
            else if (args[i] == "--own-region") { req.ownRegion.wanted = true; if (i + 1 < args.size() && args[i + 1].rfind("--", 0) != 0) { req.ownRegion.takeOver = args[++i]; if (req.ownRegion.takeOver == "new") { req.ownRegion.dedicated = true; req.ownRegion.takeOver.clear(); } } }
            else if (args[i] == "--merge-into" && i + 1 < args.size()) req.ownRegion.mergeInto = args[++i];
            else if (args[i] == "--display" && i + 1 < args.size()) req.ownRegion.displayName = args[++i];
            else if (args[i] == "--no-minimap") req.ownRegion.minimap = false;
            else { std::fprintf(stderr, "unknown option %s\n", args[i].c_str()); return 2; }
        }
        const Install install = findInstall(installArg);
        if (!install.valid) { std::fprintf(stderr, "no Fable install (use --install)\n"); return 2; }
        if (!allowInstallWrite(install)) return 1;
        albion::editor::DonorInfo info; std::string err;
        if (!albion::editor::donorInfo(install.root, req.donor, info, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        if (!hasOrigin) { req.worldX = info.suggestedX; req.worldY = info.suggestedY; }
        if (req.hostRegion.empty() && !dedicated) req.hostRegion = info.owningRegion;
        std::printf("donor %s: %dx%d at (%d,%d), region %s\n", req.donor.c_str(), info.width, info.height, info.worldX, info.worldY, info.owningRegion.c_str());
        std::printf("new level %s at (%d,%d)%s%s\n", req.name.c_str(), req.worldX, req.worldY,
                    req.hostRegion.empty() ? " in a dedicated region" : (" owned by " + req.hostRegion).c_str(), req.rebakeChunk ? ", chunk re-baked" : ", donor chunk");
        albion::editor::NewLevelResult out;
        if (!albion::editor::createLevelFromDonor(install.root, req, out, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        for (const auto& n : out.notes) std::printf("  %s\n", n.c_str());
        std::printf("installed: map slot %d, box (%d,%d)-(%d,%d)\n", out.mapSlot, out.worldX, out.worldY, out.worldX + out.width, out.worldY + out.height);
        return 0;
    }
    if (cmd == "entrance") {   // entrance <map> [x y [z]] [--install <root>]: show, or set, the map's region entrance in FinalAlbion.gtg
        if (args.size() < 2) { std::fprintf(stderr, "usage: forge entrance <map> [x y [z]] [--install <root>]\n"); return 2; }
        std::string installArg; std::vector<float> xyz;
        for (size_t i = 2; i < args.size(); ++i) {
            if (args[i] == "--install") {
                if (i + 1 == args.size()) { std::fprintf(stderr, "--install needs a path\n"); return 2; }
                installArg = args[++i];
            } else {
                std::string_view value = args[i];
                if (value.size() > 1 && value.front() == '+' && value[1] != '-') value.remove_prefix(1);
                float coordinate = 0;
                const auto parsed = std::from_chars(value.data(), value.data() + value.size(), coordinate);
                if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || !std::isfinite(coordinate)) {
                    std::fprintf(stderr, "bad entrance coordinate %s (expected a finite number)\n", args[i].c_str()); return 2;
                }
                xyz.push_back(coordinate);
            }
        }
        if (!xyz.empty() && xyz.size() != 2 && xyz.size() != 3) {
            std::fprintf(stderr, "entrance expects x y and optional z, or no coordinates to show the entrance\n"); return 2;
        }
        const Install install = findInstall(installArg);
        if (!install.valid) { std::fprintf(stderr, "no Fable install (use --install)\n"); return 2; }
        if (xyz.size() >= 2 && !allowInstallWrite(install)) return 1;
        albion::editor::WorldLayout layout; std::string err;
        if (!albion::editor::loadWorldLayout(install.root, layout, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        const auto* box = layout.find(args[1]);
        if (!box) { std::fprintf(stderr, "no map %s in FinalAlbion.wld\n", args[1].c_str()); return 1; }
        if (xyz.size() >= 2) {
            float pos[3] = {xyz[0], xyz[1], xyz.size() > 2 ? xyz[2] : 0.0f};
            if (xyz.size() < 3) {
                // on the ground when a loose .lev is at hand (the WAD copy needs the editor's extraction)
                albion::editor::Document doc;
                std::string derr;
                const fs::path loose = install.root / "data" / "Levels" / "FinalAlbion" / (box->name + ".lev");
                if (fs::exists(loose) && doc.loadLevel(loose, derr)) if (const auto h = doc.groundHeight(pos[0], pos[1])) pos[2] = *h;
            }
            const float fwd[2] = {0.0f, 1.0f};
            std::vector<std::string> notes;
            if (!albion::editor::setRegionEntrance(install.root, box->slot, box->name, pos, fwd, notes, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
            for (const auto& n : notes) std::printf("  %s\n", n.c_str());
            return 0;
        }
        const auto e = albion::editor::entranceOf(install.root, box->slot, err);
        if (!e) { std::printf("%s (slot %d): no region entrance in FinalAlbion.gtg\n", box->name.c_str(), box->slot); return 0; }
        std::printf("%s (slot %d): entrance at %.3f %.3f %.3f facing %.3f %.3f%s%s\n", box->name.c_str(), box->slot, e->pos[0], e->pos[1], e->pos[2], e->forward[0], e->forward[1], e->startScript.empty() ? "" : ", start ", e->startScript.c_str());
        return 0;
    }
    return std::nullopt;
}

}  // namespace albion::cli
