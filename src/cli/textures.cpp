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
#include "meshimport.hpp"
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

// forge CLI: textures.big and ground themes
std::optional<int> runTextures(const std::string& cmd, const Args& args) {
    if (cmd == "textures" || cmd == "texture-export" || cmd == "texture-replace" || cmd == "texture-add") {
        // textures [filter] [--bank <bank>] [--install root]        list textures.big entries
        // texture-export <name> <out.png> [--install root]           first mip as PNG
        // texture-replace <name> <image> [--install root]            same slot, same format (backup once; refused while the game runs)
        // texture-add <name> <image> [--bank GBANK_MAIN_PC] [--format dxt1|dxt3|argb8888] [--install root]
        std::string installArg, bankArg, formatArg;
        std::vector<std::string> pos;
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--install" && i + 1 < args.size()) installArg = args[++i];
            else if (args[i] == "--bank" && i + 1 < args.size()) bankArg = args[++i];
            else if (args[i] == "--format" && i + 1 < args.size()) formatArg = args[++i];
            else pos.push_back(args[i]);
        }
        const Install install = findInstall(installArg);
        if (!install.valid) { std::fprintf(stderr, "no Fable install (use --install)\n"); return 2; }
        const fs::path big = install.root / "data" / "graphics" / "pc" / "textures.big";
        std::string err;
        if (cmd == "textures") {
            const auto rows = albion::texbrowse::listTextures(big, err);
            if (rows.empty()) { std::fprintf(stderr, "error: %s\n", err.empty() ? "no textures" : err.c_str()); return 1; }
            const std::string filter = pos.empty() ? "" : pos[0];
            size_t shown = 0;
            for (const auto& r : rows) {
                if (!bankArg.empty() && r.bank != bankArg) continue;
                if (!filter.empty() && r.name.find(filter) == std::string::npos && r.label.find(filter) == std::string::npos) continue;
                std::printf("%6u  %-44s %5dx%-5d %-9s %2d mips  %8u bytes  %s%s%s\n", r.id, r.label.c_str(), r.width, r.height, r.format.c_str(), r.mips, r.bytes, r.bank.c_str(), r.label == r.name ? "" : "   ", r.label == r.name ? "" : r.name.c_str());
                ++shown;
            }
            std::printf("%zu of %zu textures\n", shown, rows.size());
            return 0;
        }
        if (pos.size() < 2) { std::fprintf(stderr, "usage: FableForge %s <name> <file> [--install <root>]\n", cmd.c_str()); return 2; }
        std::vector<std::string> notes;
        bool ok = false;
        if (cmd == "texture-export") ok = albion::texbrowse::exportPng(big, pos[0], pos[1], err);
        else if (cmd == "texture-replace") ok = albion::texbrowse::replaceTexture(install.root, pos[0], pos[1], notes, err);
        else { uint32_t id = 0; ok = albion::texbrowse::addTexture(install.root, bankArg, pos[0], pos[1], formatArg, id, notes, err); if (ok) std::printf("new texture id %u\n", id); }
        for (const auto& n : notes) std::printf("  %s\n", n.c_str());
        if (!ok) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        if (cmd == "texture-export") std::printf("wrote %s\n", pos[1].c_str());
        return 0;
    }
    if (cmd == "theme-add") {   // theme-add <png> <NAME> [--donor <ENGINE_THEME>] [--cliff <png>] [--install <root>]: a ground theme from your own texture
        if (args.size() < 3) { std::fprintf(stderr, "usage: forge theme-add <png> <NAME> [--donor <ENGINE_THEME>] [--cliff <png>] [--install <root>]\n"); return 2; }
        std::string installArg; albion::editor::CustomThemeRequest req;
        req.png = args[1]; req.name = args[2];
        for (size_t i = 3; i < args.size(); ++i) {
            if (args[i] == "--install" && i + 1 < args.size()) installArg = args[++i];
            else if (args[i] == "--donor" && i + 1 < args.size()) req.donor = args[++i];
            else if (args[i] == "--cliff" && i + 1 < args.size()) req.cliffPng = args[++i];
            else { std::fprintf(stderr, "unknown option %s\n", args[i].c_str()); return 2; }
        }
        const Install install = findInstall(installArg);
        if (!install.valid) { std::fprintf(stderr, "no Fable install (use --install)\n"); return 2; }
        albion::editor::CustomThemeResult out; std::string err;
        if (!albion::editor::createCustomTheme(install.root, req, out, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        for (const auto& n : out.notes) std::printf("  %s\n", n.c_str());
        std::printf("theme %s ready: def index %u, texture %u -- paint it from the editor (Paint ground -> search the name)\n", req.name.c_str(), out.defIndex, out.baseTexture);
        return 0;
    }
    if (cmd == "mesh-import") {   // mesh-import <model.glb|.gltf|.obj> <NAME> [--texture <png> | --texture-id <n>] [--donor <OBJECT_...>] [--stored] [--no-collision] [--install <root>]: a custom static object
        if (args.size() < 3) { std::fprintf(stderr, "usage: forge mesh-import <model.glb|.gltf|.obj> <NAME> [--texture <png> | --texture-id <n>] [--donor <OBJECT_...>] [--stored] [--no-collision] [--install <root>]\n"); return 2; }
        std::string installArg; albion::meshimport::ImportRequest req;
        req.model = args[1]; req.name = args[2];
        for (size_t i = 3; i < args.size(); ++i) {
            if (args[i] == "--install" && i + 1 < args.size()) installArg = args[++i];
            else if (args[i] == "--texture" && i + 1 < args.size()) req.texturePng = args[++i];
            else if (args[i] == "--texture-id" && i + 1 < args.size()) req.textureId = uint32_t(std::stoul(args[++i]));
            else if (args[i] == "--donor" && i + 1 < args.size()) req.donor = args[++i];
            else if (args[i] == "--stored") req.compress = false;
            else if (args[i] == "--no-collision") req.collision = false;
            else { std::fprintf(stderr, "unknown option %s\n", args[i].c_str()); return 2; }
        }
        const Install install = findInstall(installArg);
        if (!install.valid) { std::fprintf(stderr, "no Fable install (use --install)\n"); return 2; }
        albion::meshimport::ImportResult out; std::string err;
        if (!albion::meshimport::importModel(install.root, req, out, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        for (const auto& n : out.notes) std::printf("  %s\n", n.c_str());
        std::printf("%s ready: mesh %s id %u%s, def index %zu -- place it from the editor (Add an object -> search %s). Not yet seen in-game.\n",
                    out.objectName.c_str(), out.meshName.c_str(), out.meshId, out.physicsId ? (", collision hull id " + std::to_string(out.physicsId)).c_str() : ", no collision hull", out.defIndex, out.objectName.c_str());
        return 0;
    }
    return std::nullopt;
}

}  // namespace albion::cli
