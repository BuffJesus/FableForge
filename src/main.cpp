// AlbionAtlas -- export Fable: The Lost Chapters terrain to .glb / .obj.
//
//   AlbionAtlas list   [--install <root>]
//   AlbionAtlas info   <map|file.lev> [--install <root>]
//   AlbionAtlas export <map|file.lev> [--out <file.glb|file.obj>] [options]
//
// <map> is a level name (e.g. Greatwood_1) read from the install's
// data/Levels/FinalAlbion.wad (a loose data/Levels/FinalAlbion/<map>.lev wins if
// present); <file.lev> is any .lev on disk. Without --install the Steam install
// is auto-detected.

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
#include "stbrelocate.hpp"
#include "stitch.hpp"
#include "lodbake.hpp"
#include "dxt1.hpp"
#include "forge/stbinfo.hpp"

namespace fs = std::filesystem;
namespace te = albion::terrainexport;

namespace {

int usage() {
    std::puts(
        "Albion Atlas " ALBION_VERSION " -- Fable: The Lost Chapters maps -> .glb / .obj\n"
        "\n"
        "usage:\n"
        "  AlbionAtlas list   [--install <fable-root>]\n"
        "  AlbionAtlas info   <map|file.lev> [--install <fable-root>]\n"
        "  AlbionAtlas export <map|file.lev> [--out <file.glb|file.obj>] [options]\n"
        "  AlbionAtlas new-level <donor> <name> [--at x,y] [--region <host>] [--dedicated] [--no-rebake] [--install <root>]\n"
        "  AlbionAtlas blank-level <name> [--size WxH] [--at x,y] [--region <host>] [--template <map>] [--theme <slot|name>] [--height <h>] [--install <root>]\n"
        "      new-level / blank-level: [--own-region [<filler region>]] [--merge-into <filler>] [--display <name>] [--no-minimap]\n"
        "      (own region = take over a retail filler slot under the 141-region cap, with a baked MINIMAP_<NAME> texture)\n"
        "  AlbionAtlas world  [--install <root>]                      every map's box, region and baked origin\n"
        "  AlbionAtlas world-move <map> <x> <y> [<map> <x> <y> ...] [--install <root>]\n"
        "      (relocate maps: WLD/BWD placement + STB chunks translated to the new origins, touching neighbours re-baked)\n"
        "  AlbionAtlas world-owner <map> <region>   |   AlbionAtlas world-sees <region> <map> <0|1>   (region edits; world --regions lists them)\n"
        "  AlbionAtlas theme-add <png> <NAME> [--donor <ENGINE_THEME>] [--cliff <png>] [--install <root>]\n"
        "      (a ground theme from your own texture: appended to textures.big + a new ENGINE_THEME in game.bin; paint it from the editor)\n"
        "  AlbionAtlas region-props <region> [--def <REGION_DEF>] [--minimap <MINIMAP_X>] [--display <name>] [--worldmap 0|1]   (a region's def/minimap/name, WLD + BWD)\n"
        "  AlbionAtlas world-stitch <map> [<map2>] [--feather <cells>|auto] [--dry-run] [--install <root>]\n"
        "      (average the shared edge heights with every edge-sharing neighbour, or one pair; world-move --stitch does it after a move)\n"
        "\n"
        "export options:\n"
        "  --out <path>        output file; .glb (default, self-contained) or .obj (+ .mtl + PNG)\n"
        "  --install <root>    Fable TLC install dir (default: auto-detect Steam)\n"
        "  --no-textures       heightmap only (no install needed)\n"
        "  --foliage           add the baked grass/plants/trees as mesh instances (needs install)\n"
        "  --things            add the placed objects (fences, walls, rocks, buildings) from the .tng\n"
        "  --creatures         with --things: include creature meshes in bind pose\n"
        "  --particles         with --things: static stand-ins for particle emitters (flames, sun beams, lights)\n"
        "  --no-water          leave out the water surface (lakes, rivers, sea)\n"
        "  --max-texture <px>  shrink object/plant textures to at most <px> on a side (256 = quarter-size files)\n"
        "  --layers            also write splat attributes + one PNG per ground theme\n"
        "  --texels <n>        baked albedo texels per cell edge (default 8)\n"
        "  --tile <units>      world units per texture repeat (default 8, the engine's)\n"
        "  --gain <f>          brighten the baked ground texture (1 = raw texels; ~2 looks like in-game)\n"
        "  --up <y|z>          up axis: y = glTF/Blender/Unreal-friendly (default), z = Fable native\n"
        "  --world             place the map at its world position (WLD MapX/MapY) so maps line up\n"
        "  --origin <x,y>      add an explicit offset (Fable units) to every vertex\n"
        "  --walkable-colors   COLOR_0 vertex colours: white = walkable, red = blocked\n"
        "  --quiet             only print errors\n");
    return 2;
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}

struct Install {
    fs::path root;
    bool valid = false;
    std::string how;
};

Install findInstall(const std::string& override) {
    Install i;
    if (!override.empty()) {
        i.root = override;
        i.valid = fs::exists(i.root / "data" / "CompiledDefs" / "game.bin");
        i.how = "--install";
        return i;
    }
    try {
        const auto env = forge::env::Environment::detect();
        i.root = env.installDir;
        i.valid = env.installValid && fs::exists(i.root / "data" / "CompiledDefs" / "game.bin");
        i.how = env.detectSource;
    } catch (...) {
        i.valid = false;
    }
    return i;
}

// Resolve the user's level argument to a .lev path on disk. WAD-resident maps
// are extracted to a temp file (the LEV reader is path-based).
fs::path resolveLevel(const std::string& arg, const Install& install, fs::path& tempOut) {
    if (fs::exists(arg) && fs::is_regular_file(arg)) return arg;
    if (!install.valid) throw std::runtime_error("'" + arg + "' is not a file and no Fable install was found (use --install)");
    std::string name = arg;
    if (lower(name).size() > 4 && lower(name).substr(name.size() - 4) == ".lev") name.resize(name.size() - 4);
    const fs::path loose = install.root / "data" / "Levels" / "FinalAlbion" / (name + ".lev");
    if (fs::exists(loose)) return loose;

    const fs::path wadPath = install.root / "data" / "Levels" / "FinalAlbion.wad";
    const auto wad = forge::wad::Archive::open(wadPath);
    const std::string want = lower(name) + ".lev";
    for (const auto& e : wad.entries()) {
        const std::string leaf = lower(fs::path(e.name).filename().string());
        if (leaf != want) continue;
        const auto bytes = wad.read(e);
        const fs::path dir = fs::temp_directory_path() / "Albion Atlas";
        fs::create_directories(dir);
        tempOut = dir / (name + ".lev");
        std::ofstream(tempOut, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        return tempOut;
    }
    throw std::runtime_error("no map named '" + name + "' in " + wadPath.string() + " (try: AlbionAtlas list)");
}

int cmdList(const Install& install) {
    if (!install.valid) {
        std::fprintf(stderr, "no Fable install found; pass --install <root>\n");
        return 1;
    }
    const fs::path wadPath = install.root / "data" / "Levels" / "FinalAlbion.wad";
    const auto wad = forge::wad::Archive::open(wadPath);
    std::vector<std::pair<std::string, uint32_t>> maps;
    for (const auto& e : wad.entries()) {
        const fs::path p(e.name);
        if (lower(p.extension().string()) == ".lev") maps.emplace_back(p.stem().string(), e.size);
    }
    std::sort(maps.begin(), maps.end());
    std::printf("%zu maps in %s\n", maps.size(), wadPath.string().c_str());
    for (const auto& [n, sz] : maps) std::printf("  %-40s %8u bytes\n", n.c_str(), sz);
    return 0;
}

// Debug aid: materials / texture ids / UV ranges of one MBANK_ALLMESHES entry.
int cmdMesh(const Install& install, const std::string& idOrName) {
    fs::path graphics = install.root / "data" / "graphics" / "graphics.big";
    if (!fs::exists(graphics)) graphics = install.root / "data" / "graphics" / "pc" / "graphics.big";
    const auto big = forge::big::File::open(graphics);
    const auto* bank = big.findBank("MBANK_ALLMESHES");
    if (!bank) { std::fprintf(stderr, "no MBANK_ALLMESHES\n"); return 1; }
    const auto tex = forge::big::File::open(install.root / "data" / "graphics" / "pc" / "textures.big");
    std::map<uint32_t, std::string> texNames;
    if (const auto* tb = tex.findBank("GBANK_MAIN_PC")) for (const auto& e : tb->entries) texNames[e.id] = e.name;
    const uint32_t id = uint32_t(std::strtoul(idOrName.c_str(), nullptr, 10));
    for (const auto& e : bank->entries) {
        if (!(e.id == id || lower(e.name) == lower(idOrName))) continue;
        const auto g = forge::meshpreview::decodeLod0(big.entryData(e), e.type);
        std::printf("%s (id %u, type %u): %zu vertices, %zu triangles, %zu materials, %u primitives\n", e.name.c_str(), e.id, e.type,
                    g.vertices.size(), g.triangles.size(), g.materials.size(), g.primitiveCount);
        std::map<int32_t, size_t> perMat;
        for (const auto& t : g.triangles) ++perMat[t.material];
        for (size_t i = 0; i < g.materials.size(); ++i) {
            const auto& m = g.materials[i];
            auto nm = [&](int32_t t) { return t > 0 && texNames.count(uint32_t(t)) ? texNames[uint32_t(t)] : std::string("-"); };
            std::printf("  material[%zu] id=%d diffuse=%d %s bump=%d %s alphaMap=%d alpha=%d flags=0x%x  triangles=%zu\n", i, m.id,
                        m.diffuseTexture, nm(m.diffuseTexture).c_str(), m.bumpTexture, nm(m.bumpTexture).c_str(), m.alphaMapTexture,
                        int(m.alphaEnabled), m.textureFlags, perMat.count(int32_t(i)) ? perMat[int32_t(i)] : 0);
        }
        for (const auto& [mat, cnt] : perMat) if (mat < 0 || size_t(mat) >= g.materials.size()) std::printf("  triangles with material %d (no such material): %zu\n", mat, cnt);
        float umin = 1e9f, umax = -1e9f, vmin = 1e9f, vmax = -1e9f, zmin = 1e9f, zmax = -1e9f;
        for (const auto& v : g.vertices) { umin = std::min(umin, v.u); umax = std::max(umax, v.u); vmin = std::min(vmin, v.v); vmax = std::max(vmax, v.v); zmin = std::min(zmin, v.z); zmax = std::max(zmax, v.z); }
        std::printf("  uv u %.2f..%.2f v %.2f..%.2f   z %.1f..%.1f (mesh units)\n", umin, umax, vmin, vmax, zmin, zmax);
        for (const auto& p : g.primitives) std::printf("  primitive: %u verts %u indices stride %u fmt 0x%x\n", p.vertexCount, p.indexCount, p.vertexStride, p.vertexFormat);
        return 0;
    }
    std::fprintf(stderr, "mesh not found: %s\n", idOrName.c_str());
    return 1;
}

int cmdInfo(const fs::path& lev) {
    const auto file = forge::lev::File::open(lev);
    float lo = 1e30f, hi = -1e30f;
    size_t walkable = 0;
    std::vector<size_t> use(file.groundThemes().size(), 0);
    for (int y = 0; y < file.cellsY(); ++y)
        for (int x = 0; x < file.cellsX(); ++x) {
            const float h = file.heightAt(x, y);
            lo = std::min(lo, h); hi = std::max(hi, h);
            if (file.walkableAt(x, y)) ++walkable;
            for (int s = 0; s < 3; ++s)
                if (file.themeStrengthAt(x, y, s)) ++use[file.themeIndexAt(x, y, s)];
        }
    std::printf("%s\n  map %dx%d cells (%dx%d vertices), uid %llu\n  height %.2f .. %.2f (span %.2f)\n"
                "  walkable %zu / %d vertices\n  ground themes:\n",
                file.source().c_str(), file.width(), file.height(), file.cellsX(), file.cellsY(),
                (unsigned long long)file.uid(), lo, hi, hi - lo, walkable, file.cellsX() * file.cellsY());
    for (size_t i = 0; i < use.size(); ++i)
        if (use[i]) std::printf("    [%3zu] %-44s def %-6u %zu refs\n", i, file.groundThemes()[i].name.c_str(),
                                file.groundThemes()[i].value, use[i]);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "-h" || args[0] == "--help") return usage();
    const std::string cmd = args[0];
    if (cmd == "blank-level") {   // blank-level <name> [--at x,y] [--region <host>] [--template <64x64 map>] [--theme <slot|name>] [--height h] [--install <root>]
        if (args.size() < 2) { std::fprintf(stderr, "usage: AlbionAtlas blank-level <name> [--size WxH] [--at x,y] [--region <hostRegion>] [--template <map>] [--theme <slot|name>] [--height <h>] [--install <root>]\n"); return 2; }
        albion::editor::BlankLevelRequest req;
        req.name = args[1];
        std::string installArg, at, theme;
        for (size_t i = 2; i < args.size(); ++i) {
            if (args[i] == "--install" && i + 1 < args.size()) installArg = args[++i];
            else if (args[i] == "--at" && i + 1 < args.size()) at = args[++i];
            else if (args[i] == "--region" && i + 1 < args.size()) req.hostRegion = args[++i];
            else if (args[i] == "--template" && i + 1 < args.size()) req.templateLevel = args[++i];
            else if (args[i] == "--theme" && i + 1 < args.size()) theme = args[++i];
            else if (args[i] == "--height" && i + 1 < args.size()) req.groundHeight = float(std::atof(args[++i].c_str()));
            else if (args[i] == "--size" && i + 1 < args.size()) { if (std::sscanf(args[++i].c_str(), "%dx%d", &req.width, &req.height) != 2) { std::fprintf(stderr, "bad --size %s (WxH)\n", args[i].c_str()); return 2; } }
            else if (args[i] == "--own-region") { req.ownRegion.wanted = true; if (i + 1 < args.size() && args[i + 1].rfind("--", 0) != 0) req.ownRegion.takeOver = args[++i]; }
            else if (args[i] == "--merge-into" && i + 1 < args.size()) req.ownRegion.mergeInto = args[++i];
            else if (args[i] == "--display" && i + 1 < args.size()) req.ownRegion.displayName = args[++i];
            else if (args[i] == "--no-minimap") req.ownRegion.minimap = false;
            else { std::fprintf(stderr, "unknown option %s\n", args[i].c_str()); return 2; }
        }
        const Install install = findInstall(installArg);
        if (!install.valid) { std::fprintf(stderr, "no Fable install (use --install)\n"); return 2; }
        albion::editor::DonorInfo info; std::string err;
        if (req.templateLevel.empty()) {
            std::string serr;
            for (const auto& s : albion::editor::retailMapSizes(install.root, serr))
                if (s.width == req.width && s.height == req.height) { req.templateLevel = s.templateLevel; break; }
            if (req.templateLevel.empty()) { std::fprintf(stderr, "no retail map is %dx%d; sizes available:", req.width, req.height); for (const auto& s : albion::editor::retailMapSizes(install.root, serr)) std::fprintf(stderr, " %dx%d", s.width, s.height); std::fprintf(stderr, "\n"); return 2; }
        }
        if (!albion::editor::donorInfo(install.root, req.templateLevel, info, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        req.worldX = info.suggestedX; req.worldY = info.suggestedY;
        if (!at.empty() && std::sscanf(at.c_str(), "%d,%d", &req.worldX, &req.worldY) != 2) { std::fprintf(stderr, "bad --at %s\n", at.c_str()); return 2; }
        if (req.hostRegion.empty()) req.hostRegion = info.owningRegion;
        if (req.ownRegion.wanted) {
            std::string rerr;
            const auto rr = albion::editor::reusableRegions(install.root, rerr);
            std::printf("reusable filler regions:"); for (const auto& r : rr) std::printf(" %s(slot %d, %d maps)", r.name.c_str(), r.slot, r.maps); std::printf("\n");
        }
        if (!theme.empty()) {
            if (std::isdigit(static_cast<unsigned char>(theme[0]))) req.themeSlot = std::atoi(theme.c_str());
            else {
                fs::path temp;
                const auto tl = forge::lev::File::open(resolveLevel(req.templateLevel, install, temp));
                for (size_t i = 0; i < tl.groundThemes().size(); ++i) if (tl.groundThemes()[i].name == theme) req.themeSlot = int(i);
                if (req.themeSlot < 0) { std::fprintf(stderr, "theme %s is not in %s's palette\n", theme.c_str(), req.templateLevel.c_str()); return 2; }
            }
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
        if (args.size() < 3) { std::fprintf(stderr, "usage: AlbionAtlas new-level <donor> <name> [--at x,y] [--region <hostRegion>] [--dedicated] [--no-rebake] [--install <root>]\n"); return 2; }
        albion::editor::NewLevelRequest req;
        req.donor = args[1]; req.name = args[2];
        std::string installArg, at; bool dedicated = false;
        for (size_t i = 3; i < args.size(); ++i) {
            if (args[i] == "--install" && i + 1 < args.size()) installArg = args[++i];
            else if (args[i] == "--at" && i + 1 < args.size()) at = args[++i];
            else if (args[i] == "--region" && i + 1 < args.size()) req.hostRegion = args[++i];
            else if (args[i] == "--dedicated") dedicated = true;
            else if (args[i] == "--no-rebake") req.rebakeChunk = false;
            else if (args[i] == "--own-region") { req.ownRegion.wanted = true; if (i + 1 < args.size() && args[i + 1].rfind("--", 0) != 0) req.ownRegion.takeOver = args[++i]; }
            else if (args[i] == "--merge-into" && i + 1 < args.size()) req.ownRegion.mergeInto = args[++i];
            else if (args[i] == "--display" && i + 1 < args.size()) req.ownRegion.displayName = args[++i];
            else if (args[i] == "--no-minimap") req.ownRegion.minimap = false;
            else { std::fprintf(stderr, "unknown option %s\n", args[i].c_str()); return 2; }
        }
        const Install install = findInstall(installArg);
        if (!install.valid) { std::fprintf(stderr, "no Fable install (use --install)\n"); return 2; }
        albion::editor::DonorInfo info; std::string err;
        if (!albion::editor::donorInfo(install.root, req.donor, info, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        req.worldX = info.suggestedX; req.worldY = info.suggestedY;
        if (!at.empty() && std::sscanf(at.c_str(), "%d,%d", &req.worldX, &req.worldY) != 2) { std::fprintf(stderr, "bad --at %s\n", at.c_str()); return 2; }
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
    if (cmd == "region-props") {   // region-props <region> [--def REGION_X] [--minimap MINIMAP_X] [--display NAME] [--worldmap 0|1] [--install root]
        if (args.size() < 3) { std::fprintf(stderr, "usage: AlbionAtlas region-props <region> [--def <REGION_DEF>] [--minimap <MINIMAP_GRAPHIC>] [--display <name>] [--worldmap 0|1] [--install <root>]\n"); return 2; }
        std::string installArg; albion::editor::RegionProps props;
        for (size_t i = 2; i + 1 < args.size(); i += 2) {
            if (args[i] == "--install") installArg = args[i + 1];
            else if (args[i] == "--def") props.regionDef = args[i + 1];
            else if (args[i] == "--minimap") props.minimapGraphic = args[i + 1];
            else if (args[i] == "--display") props.displayName = args[i + 1];
            else if (args[i] == "--worldmap") props.onWorldMap = std::atoi(args[i + 1].c_str());
            else { std::fprintf(stderr, "unknown option %s\n", args[i].c_str()); return 2; }
        }
        const Install install = findInstall(installArg);
        if (!install.valid) { std::fprintf(stderr, "no Fable install (use --install)\n"); return 2; }
        std::vector<std::string> notes; std::string err;
        if (!albion::editor::setRegionProperties(install.root, args[1], props, notes, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        for (const auto& n : notes) std::printf("  %s\n", n.c_str());
        return 0;
    }
    if (cmd == "theme-add") {   // theme-add <png> <NAME> [--donor <ENGINE_THEME>] [--cliff <png>] [--install <root>]: a ground theme from your own texture
        if (args.size() < 3) { std::fprintf(stderr, "usage: AlbionAtlas theme-add <png> <NAME> [--donor <ENGINE_THEME>] [--cliff <png>] [--install <root>]\n"); return 2; }
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
    if (cmd == "world-stitch") {   // world-stitch <map> [<map2>] [--feather n] [--dry-run] [--install root]
        std::string installArg; std::vector<std::string> maps; albion::editor::StitchOptions so;
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--install" && i + 1 < args.size()) installArg = args[++i];
            else if (args[i] == "--feather" && i + 1 < args.size()) { ++i; so.feather = args[i] == "auto" ? -1 : std::atoi(args[i].c_str()); }
            else if (args[i] == "--dry-run") so.deploy = false;
            else maps.push_back(args[i]);
        }
        if (maps.empty() || maps.size() > 2) { std::fprintf(stderr, "usage: AlbionAtlas world-stitch <map> [<map2>] [--feather <cells>] [--dry-run] [--install <root>]\n"); return 2; }
        const Install install = findInstall(installArg);
        if (!install.valid) { std::fprintf(stderr, "no Fable install (use --install)\n"); return 2; }
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
            else if (cmd == "world-move" && i + 2 < args.size()) { moves.push_back({args[i], std::atoi(args[i + 1].c_str()), std::atoi(args[i + 2].c_str())}); i += 2; }
            else if (cmd == "world-owner" && i + 1 < args.size()) { owners.push_back({args[i], args[i + 1]}); i += 1; }
            else if (cmd == "world-sees" && i + 2 < args.size()) { sees.push_back({args[i], args[i + 1], args[i + 2] != "0"}); i += 2; }
            else { std::fprintf(stderr, "usage: AlbionAtlas world [--regions] | world-move <map> <x> <y> [...] | world-owner <map> <region> | world-sees <region> <map> <0|1>  [--install <root>]\n"); return 2; }
        }
        if (cmd == "world-owner" || cmd == "world-sees") {
            const Install install = findInstall(installArg);
            if (!install.valid) { std::fprintf(stderr, "no Fable install (use --install)\n"); return 2; }
            std::vector<std::string> notes; std::string err;
            if (!albion::editor::applyWorldEdits(install.root, {}, owners, sees, notes, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
            for (const auto& n : notes) std::printf("  %s\n", n.c_str());
            return 0;
        }
        const Install install = findInstall(installArg);
        if (!install.valid) { std::fprintf(stderr, "no Fable install (use --install)\n"); return 2; }
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
        if (moves.empty()) { std::fprintf(stderr, "usage: AlbionAtlas world-move <map> <x> <y> [...] [--install <root>]\n"); return 2; }
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
    if (cmd == "heights") {   // heights <map.lev> <x,y> [<x,y> ...]: bilinear LEV heights at map-local points (in-game harness oracle)
        if (args.size() < 3) { std::fprintf(stderr, "usage: AlbionAtlas heights <map.lev> <x,y> ...\n"); return 2; }
        try {
            fs::path temp;
            const fs::path levPath = fs::exists(args[1]) ? fs::path(args[1]) : resolveLevel(args[1], findInstall(""), temp);
            const auto lev = forge::lev::File::open(levPath);
            for (size_t i = 2; i < args.size(); ++i) {
                float x = 0, y = 0;
                if (std::sscanf(args[i].c_str(), "%f,%f", &x, &y) != 2) { std::fprintf(stderr, "bad point %s\n", args[i].c_str()); return 2; }
                const int cx = lev.cellsX(), cy = lev.cellsY();
                if (x < 0 || y < 0 || x > float(cx - 1) || y > float(cy - 1)) { std::printf("%g,%g outside\n", x, y); continue; }
                const int x0 = std::min(int(x), cx - 1), y0 = std::min(int(y), cy - 1), x1 = std::min(x0 + 1, cx - 1), y1 = std::min(y0 + 1, cy - 1);
                const float fx = x - float(x0), fy = y - float(y0);
                const float h = (lev.heightAt(x0, y0) * (1 - fx) + lev.heightAt(x1, y0) * fx) * (1 - fy) + (lev.heightAt(x0, y1) * (1 - fx) + lev.heightAt(x1, y1) * fx) * fy;
                std::printf("%g,%g %.4f\n", x, y, h);
            }
            return 0;
        } catch (const std::exception& e) { std::fprintf(stderr, "error: %s\n", e.what()); return 1; }
    }
    if (cmd == "recompress-chunk") {   // diagnostic: re-encode every frame's LZO with our encoder, bodies untouched
        if (args.size() < 3) { std::fprintf(stderr, "usage: AlbionAtlas recompress-chunk <in.bin> <out.bin>\n"); return 2; }
        try {
            std::ifstream cf(args[1], std::ios::binary);
            std::vector<uint8_t> raw((std::istreambuf_iterator<char>(cf)), std::istreambuf_iterator<char>());
            const auto chunk = forge::stbbake::parseChunk(raw);
            // in place: every frame whose re-encoded bytes fit its slot (frame + trailing pad) is
            // rewritten with our LZO, bodies untouched; the rest keep the donor bytes
            std::vector<uint8_t> out = raw;
            size_t redone = 0, kept = 0;
            for (size_t fi = 0; fi < chunk.frameIndices.size(); ++fi) {
                const size_t si = chunk.frameIndices[fi];
                const auto& seg = chunk.segments[si];
                size_t slotEnd = seg.end;
                for (size_t k = si + 1; k < chunk.segments.size() && chunk.segments[k].kind == forge::stbbake::SegKind::Pad; ++k) slotEnd = chunk.segments[k].end;
                std::vector<uint8_t> body;
                try { body = forge::stbbake::decodeFrame(chunk, fi); } catch (...) { ++kept; continue; }
                const auto enc = forge::lzo::compressFramed999(body);
                if (seg.start + enc.size() > slotEnd) { ++kept; continue; }
                std::fill(out.begin() + seg.start, out.begin() + slotEnd, uint8_t(0));
                std::copy(enc.begin(), enc.end(), out.begin() + seg.start);
                ++redone;
            }
            std::ofstream(args[2], std::ios::binary).write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
            std::printf("wrote %s (%zu bytes, %zu frames re-encoded, %zu kept)\n", args[2].c_str(), out.size(), redone, kept);
            return 0;
        } catch (const std::exception& e) { std::fprintf(stderr, "error: %s\n", e.what()); return 1; }
    }
    if (cmd == "minimap-register") {   // minimap-register <MINIMAP_NAME> <texture id> [--install <root>]: PLAYER_GUI MiniMapGraphics entry
        if (args.size() < 3) { std::fprintf(stderr, "usage: AlbionAtlas minimap-register <name> <id> [--install <root>]\n"); return 2; }
        std::string installArg;
        for (size_t i = 3; i + 1 < args.size(); ++i) if (args[i] == "--install") installArg = args[i + 1];
        const Install install = findInstall(installArg);
        if (!install.valid) { std::fprintf(stderr, "no Fable install (use --install)\n"); return 2; }
        std::vector<std::string> notes; std::string err;
        if (!albion::editor::registerMinimapGraphic(install.root, args[1], uint32_t(std::strtoul(args[2].c_str(), nullptr, 0)), notes, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        for (const auto& n : notes) std::printf("  %s\n", n.c_str());
        return 0;
    }
    if (cmd == "lod-check") {   // diagnostic: lod-check <map> [--install <root>]: compare our baked distant-LOD tiles with the retail inline textures
        if (args.size() < 2) { std::fprintf(stderr, "usage: AlbionAtlas lod-check <map> [--install <root>]\n"); return 2; }
        std::string installArg;
        for (size_t i = 2; i + 1 < args.size(); ++i) if (args[i] == "--install") installArg = args[i + 1];
        const Install install = findInstall(installArg);
        if (!install.valid) { std::fprintf(stderr, "no Fable install (use --install)\n"); return 2; }
        try {
            fs::path temp;
            const fs::path levPath = resolveLevel(args[1], install, temp);
            const auto lev = forge::lev::File::open(levPath);
            const auto albedo = albion::editor::bakeLodAlbedo(install.root, lev);
            std::printf("albedo %ux%u (%s)\n", albedo.image.width, albedo.image.height, albedo.textured ? "textured" : "flat");
            const auto archive = forge::stb::Archive::open(install.root / "data" / "Levels" / "FinalAlbion_RT.stb");
            const std::string want = lower(args[1]) + ".lev";
            for (const auto& m : archive.staticMaps()) {
                if (lower(fs::path(m.levelName).filename().string()) != want) continue;
                const auto record = archive.readStaticMapRecord(m);
                uint32_t bankIndex = 0; std::memcpy(&bankIndex, record.data() + 4, 4);
                uint32_t rootPos = 0; std::memcpy(&rootPos, record.data() + 0x68, 4);
                const forge::stb::Entry* entry = nullptr;
                for (const auto& e : archive.entries()) if (e.id == bankIndex) { entry = &e; break; }
                const auto chunk = archive.read(*entry);
                const auto parsed = forge::stbbake::parseChunk(chunk);
                std::map<size_t, size_t> frameIndexByStart;
                for (size_t i = 0; i < parsed.frameIndices.size(); ++i) frameIndexByStart[parsed.segments[parsed.frameIndices[i]].start] = i;
                const auto root = forge::stbbake::parseBackgroundTree(chunk, rootPos);
                double corrSame = 0, corrFlip = 0; int nodes = 0; bool dumped = false;
                std::function<void(const forge::stbbake::BackgroundTreeNode&)> walk = [&](const forge::stbbake::BackgroundTreeNode& n) {
                    for (const auto& l : n.header.lod) {
                        if (l.fileBlockPos == 0) continue;
                        auto it = frameIndexByStart.find(size_t(l.fileBlockPos) + size_t(l.offsetIntoFileBlock));
                        if (it == frameIndexByStart.end()) continue;
                        const auto body = forge::stbbake::decodeFrame(parsed, it->second);
                        const auto pb = forge::stbbake::parsePatchBody(body);
                        if (!pb.valid || pb.texture.size() < 19) break;
                        const auto tex = forge::stbbake::parseInlineTexture(pb.texture);
                        if (tex.width != 64 || tex.height != 64 || tex.mipData.size() < 2048) {
                            std::printf("node %d,%d %dx%d: texture %ux%u levels %d fmt %u/%u usage %u pool %u bytes %zu (skipped)\n", n.header.mapX, n.header.mapY, n.header.width, n.header.height, tex.width, tex.height, tex.levels, tex.pixelFormat0, tex.pixelFormat1, tex.usage, tex.surfacePool, tex.mipData.size());
                            break;
                        }
                        if (nodes == 0) std::printf("retail inline texture: %ux%u levels %d fmt %u/%u usage %u pool %u mip bytes %zu\n", tex.width, tex.height, tex.levels, tex.pixelFormat0, tex.pixelFormat1, tex.usage, tex.surfacePool, tex.mipData.size());
                        const auto retail = albion::dxt1::decode(tex.mipData.data(), 64, 64);
                        const auto same = albion::editor::lodTile(albedo, n.header.mapX, n.header.mapY, n.header.width, n.header.height, false);
                        const auto flip = albion::editor::lodTile(albedo, n.header.mapX, n.header.mapY, n.header.width, n.header.height, true);
                        auto corr = [&](const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
                            double ma = 0, mb = 0; const size_t n4 = a.size() / 4;
                            for (size_t i = 0; i < n4; ++i) { ma += a[i * 4] + a[i * 4 + 1] + a[i * 4 + 2]; mb += b[i * 4] + b[i * 4 + 1] + b[i * 4 + 2]; }
                            ma /= double(n4 * 3); mb /= double(n4 * 3);
                            double num = 0, da = 0, db = 0;
                            for (size_t i = 0; i < n4 * 4; ++i) { if (i % 4 == 3) continue; const double x = a[i] - ma, y = b[i] - mb; num += x * y; da += x * x; db += y * y; }
                            return da > 0 && db > 0 ? num / std::sqrt(da * db) : 0.0;
                        };
                        corrSame += corr(retail, same.rgba); corrFlip += corr(retail, flip.rgba); ++nodes;
                        if (!dumped) {
                            dumped = true;
                            fs::create_directories("build/lodcheck");
                            albion::terrainexport::Image ri; ri.width = ri.height = 64; ri.rgba = retail;
                            auto w = [&](const albion::terrainexport::Image& im, const char* nm) { const auto png = albion::terrainexport::encodePng(im); std::ofstream(fs::path("build/lodcheck") / nm, std::ios::binary).write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size())); };
                            w(ri, "retail.png"); w(same, "ours_same.png"); w(flip, "ours_flip.png");
                        }
                        break;
                    }
                    for (const auto& c : n.children) walk(c);
                };
                walk(root);
                std::printf("%d node textures compared: mean correlation same-rows %.3f, flipped-rows %.3f -> %s (build/lodcheck/*.png)\n", nodes, nodes ? corrSame / nodes : 0, nodes ? corrFlip / nodes : 0, corrFlip > corrSame ? "FLIP" : "SAME");
                return 0;
            }
            std::fprintf(stderr, "%s has no static map\n", args[1].c_str()); return 1;
        } catch (const std::exception& e) { std::fprintf(stderr, "error: %s\n", e.what()); return 1; }
    }
    if (cmd == "chunk-extract") {   // diagnostic: chunk-extract <map> <out.bin> [--install <root>]: the map's terrain chunk from FinalAlbion_RT.stb (+ <out.bin>.record)
        if (args.size() < 3) { std::fprintf(stderr, "usage: AlbionAtlas chunk-extract <map> <out.bin> [--install <root>]\n"); return 2; }
        std::string installArg;
        for (size_t i = 3; i + 1 < args.size(); ++i) if (args[i] == "--install") installArg = args[i + 1];
        const Install install = findInstall(installArg);
        if (!install.valid) { std::fprintf(stderr, "no Fable install (use --install)\n"); return 2; }
        try {
            const auto archive = forge::stb::Archive::open(install.root / "data" / "Levels" / "FinalAlbion_RT.stb");
            const std::string want = lower(args[1]) + ".lev";
            for (const auto& m : archive.staticMaps()) {
                if (lower(fs::path(m.levelName).filename().string()) != want) continue;
                const auto record = archive.readStaticMapRecord(m);
                uint32_t bankIndex = 0; std::memcpy(&bankIndex, record.data() + 4, 4);
                for (const auto& e : archive.entries()) if (e.id == bankIndex) {
                    const auto chunk = archive.read(e);
                    std::ofstream(args[2], std::ios::binary).write(reinterpret_cast<const char*>(chunk.data()), std::streamsize(chunk.size()));
                    std::ofstream(args[2] + ".record", std::ios::binary).write(reinterpret_cast<const char*>(record.data()), std::streamsize(record.size()));
                    std::printf("wrote %s (%zu bytes, entry %s) + .record (%zu bytes)\n", args[2].c_str(), chunk.size(), e.name.c_str(), record.size());
                    return 0;
                }
            }
            std::fprintf(stderr, "%s has no static map\n", args[1].c_str()); return 1;
        } catch (const std::exception& e) { std::fprintf(stderr, "error: %s\n", e.what()); return 1; }
    }
    if (cmd == "chunk-audit") {   // diagnostic: chunk-audit <map>|--all [--install <root>]: every world coordinate in the chunk must lie in the map's box
        std::string installArg, target;
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--install" && i + 1 < args.size()) installArg = args[++i];
            else target = args[i];
        }
        const Install install = findInstall(installArg);
        if (!install.valid || target.empty()) { std::fprintf(stderr, "usage: AlbionAtlas chunk-audit <map>|--all [--install <root>]\n"); return 2; }
        try {
            const auto archive = forge::stb::Archive::open(install.root / "data" / "Levels" / "FinalAlbion_RT.stb");
            int maps = 0, bad = 0;
            for (const auto& m : archive.staticMaps()) {
                const std::string stem = fs::path(m.levelName).stem().string();
                if (target != "--all" && lower(stem) != lower(target)) continue;
                const auto record = archive.readStaticMapRecord(m);
                const auto info = forge::stbinfo::readInfoBlock(record.data());
                const forge::stb::Entry* entry = nullptr;
                for (const auto& e : archive.entries()) if (int32_t(e.id) == info.bankFileIndex) { entry = &e; break; }
                if (!entry) { std::printf("%-36s no bank entry\n", stem.c_str()); continue; }
                const auto chunk = archive.read(*entry);
                albion::editor::RelocateReport rep; std::string err;
                const bool ok = albion::editor::auditChunk(chunk, record, info.worldX, info.worldY, info.mapWidth, info.mapHeight, rep, err);
                ++maps;
                if (!ok || !rep.issues.empty()) ++bad;
                std::printf("%-36s %s fg %d patches %d groups %d tree %d detail %d/%d blocks %d unclassified %d%s%s\n", stem.c_str(), ok ? "ok " : "ERR",
                            rep.foregroundFrames, rep.patchFrames, rep.groupFrames, rep.treeNodes, rep.detailNodes, rep.detailGroups, rep.rangeBlocks, rep.unclassifiedFrames,
                            ok ? "" : (" : " + err).c_str(), rep.issues.empty() ? "" : (" issues " + std::to_string(rep.issues.size())).c_str());
                for (size_t i = 0; i < rep.issues.size() && i < (target == "--all" ? 3u : 40u); ++i) std::printf("    %s\n", rep.issues[i].c_str());
                for (const auto& n : rep.notes) std::printf("    note: %s\n", n.c_str());
            }
            std::printf("%d map(s), %d with findings\n", maps, bad);
            return bad ? 1 : 0;
        } catch (const std::exception& e) { std::fprintf(stderr, "error: %s\n", e.what()); return 1; }
    }
    if (cmd == "chunk-textures") {   // diagnostic: chunk-textures <map> [--install <root>]: distinct foreground texture triples (GBANK_MAIN_PC ids) and their layer counts
        if (args.size() < 2) { std::fprintf(stderr, "usage: AlbionAtlas chunk-textures <map> [--install <root>]\n"); return 2; }
        std::string installArg;
        for (size_t i = 2; i + 1 < args.size(); ++i) if (args[i] == "--install") installArg = args[i + 1];
        const Install install = findInstall(installArg);
        if (!install.valid) { std::fprintf(stderr, "no Fable install (use --install)\n"); return 2; }
        try {
            const auto archive = forge::stb::Archive::open(install.root / "data" / "Levels" / "FinalAlbion_RT.stb");
            for (const auto& m : archive.staticMaps()) {
                if (lower(fs::path(m.levelName).stem().string()) != lower(args[1])) continue;
                const auto record = archive.readStaticMapRecord(m);
                const auto info = forge::stbinfo::readInfoBlock(record.data());
                const forge::stb::Entry* entry = nullptr;
                for (const auto& e : archive.entries()) if (int32_t(e.id) == info.bankFileIndex) { entry = &e; break; }
                if (!entry) { std::fprintf(stderr, "no bank entry\n"); return 1; }
                const auto chunk = archive.read(*entry);
                const auto parsed = forge::stbbake::parseChunk(chunk);
                // the foreground directory by cell count (a zero frame pointer is an empty cell, not the end)
                const size_t cells = size_t(info.mapWidth / 16) * size_t(info.mapHeight / 16);
                uint32_t fgPos = 0; std::memcpy(&fgPos, record.data() + 0x64, 4);
                std::map<std::array<uint32_t, 3>, size_t> triples;
                std::set<uint32_t> frames;
                size_t layers = 0;
                for (size_t i = 0; i < cells; ++i) {
                    const size_t o = size_t(fgPos ? fgPos : 0x800) + i * 0x24;
                    if (o + 4 > chunk.size()) break;
                    uint32_t frameOffset = 0; std::memcpy(&frameOffset, chunk.data() + o, 4);
                    if (!frameOffset || !frames.insert(frameOffset).second) continue;
                    size_t fi = SIZE_MAX;
                    for (size_t k = 0; k < parsed.frameIndices.size(); ++k) if (parsed.segments[parsed.frameIndices[k]].start == frameOffset) { fi = k; break; }
                    if (fi == SIZE_MAX) continue;
                    const auto body = forge::stbbake::decodeFrame(parsed, fi);
                    const auto frame = forge::stbbake::parseForegroundFrame(body);
                    for (const auto& l : frame.layers) { ++triples[{l.textures[0], l.textures[1], l.textures[2]}]; ++layers; }
                }
                std::printf("%s: %zu foreground frames, %zu layers, %zu distinct texture triples (base, background, bump)\n", args[1].c_str(), frames.size(), layers, triples.size());
                for (const auto& [t, n] : triples) std::printf("  (%u, %u, %u)  %zu layer(s)\n", t[0], t[1], t[2], n);
                return 0;
            }
            std::fprintf(stderr, "no static map named %s\n", args[1].c_str());
            return 1;
        } catch (const std::exception& e) { std::fprintf(stderr, "error: %s\n", e.what()); return 1; }
    }
    if (cmd == "chunk-zcheck") {   // diagnostic: chunk-zcheck <map> <dz> [--install <root>]: foliage Z ride by a constant, audit, ride back, compare digests
        if (args.size() < 3) { std::fprintf(stderr, "usage: AlbionAtlas chunk-zcheck <map> <dz> [--install <root>]\n"); return 2; }
        std::string installArg;
        for (size_t i = 3; i + 1 < args.size(); ++i) if (args[i] == "--install") installArg = args[i + 1];
        const Install install = findInstall(installArg);
        if (!install.valid) { std::fprintf(stderr, "no Fable install (use --install)\n"); return 2; }
        const float dz = float(std::atof(args[2].c_str()));
        try {
            const auto archive = forge::stb::Archive::open(install.root / "data" / "Levels" / "FinalAlbion_RT.stb");
            for (const auto& m : archive.staticMaps()) {
                if (lower(fs::path(m.levelName).stem().string()) != lower(args[1])) continue;
                const auto record = archive.readStaticMapRecord(m);
                const auto info = forge::stbinfo::readInfoBlock(record.data());
                const forge::stb::Entry* entry = nullptr;
                for (const auto& e : archive.entries()) if (int32_t(e.id) == info.bankFileIndex) { entry = &e; break; }
                if (!entry) { std::fprintf(stderr, "no bank entry\n"); return 1; }
                auto chunk = archive.read(*entry);
                albion::editor::RelocateReport rep; std::string err;
                for (size_t i = 3; i + 1 < args.size(); ++i) if (args[i] == "--bake") {   // --bake <lev|map>: bakeHeightfield first (identity), audit
                    fs::path temp;
                    const auto lev = forge::lev::File::open(resolveLevel(args[i + 1], install, temp));
                    forge::stbbake::HeightfieldBakeOptions opt; opt.requireCanonicalSize = false;
                    const auto baked = forge::stbbake::bakeHeightfield(chunk, lev, info.worldX, info.worldY, opt);
                    chunk = baked.chunk;
                    albion::editor::RelocateReport ba; auto rc = record;
                    const bool ok = albion::editor::auditChunk(chunk, rc, info.worldX, info.worldY, info.mapWidth, info.mapHeight, ba, err);
                    std::printf("after bake: %zu bytes, audit %s (%zu issues) groups %d\n", chunk.size(), ok ? "ok" : err.c_str(), ba.issues.size(), ba.groupFrames);
                }
                auto moved = chunk; auto movedRecord = record;
                float slack = 0; bool seam = false;   // --slack <v>: grow bounds; --seam: dz only within 20 units of the map's left edge (a seam-like ride)
                for (size_t i = 3; i < args.size(); ++i) { if (args[i] == "--slack" && i + 1 < args.size()) slack = float(std::atof(args[i + 1].c_str())); if (args[i] == "--seam") seam = true; }
                const float edgeX = float(info.worldX);
                auto rideFn = [dz, seam, edgeX](float x, float) { return seam ? (x < edgeX + 20.0f ? dz * (1.0f - (x - edgeX) / 20.0f) : 0.0f) : dz; };
                if (!albion::editor::reseatFoliageZ(moved, movedRecord, rideFn, slack, rep, err)) { std::fprintf(stderr, "z ride failed: %s\n", err.c_str()); return 1; }
                std::printf("rode z by %g: %zu -> %zu bytes, groups %d detail %d/%d\n", dz, chunk.size(), moved.size(), rep.groupFrames, rep.detailNodes, rep.detailGroups);
                for (const auto& n : rep.notes) std::printf("  %s\n", n.c_str());
                albion::editor::RelocateReport audit;
                if (!albion::editor::auditChunk(moved, movedRecord, info.worldX, info.worldY, info.mapWidth, info.mapHeight, audit, err)) { std::fprintf(stderr, "audit failed: %s\n", err.c_str()); return 1; }
                std::printf("audit: %zu issue(s), groups %d\n", audit.issues.size(), audit.groupFrames);
                for (size_t i = 0; i < audit.issues.size() && i < 10; ++i) std::printf("    %s\n", audit.issues[i].c_str());
                auto back = moved; auto backRecord = movedRecord;
                albion::editor::RelocateReport rep2;
                if (!albion::editor::reseatFoliageZ(back, backRecord, [rideFn](float x, float y) { return -rideFn(x, y); }, -slack, rep2, err)) { std::fprintf(stderr, "z ride back failed: %s\n", err.c_str()); return 1; }
                albion::editor::RelocateReport da, db;
                if (!albion::editor::auditChunk(chunk, record, info.worldX, info.worldY, info.mapWidth, info.mapHeight, da, err)) { std::fprintf(stderr, "audit failed: %s\n", err.c_str()); return 1; }
                if (!albion::editor::auditChunk(back, backRecord, info.worldX, info.worldY, info.mapWidth, info.mapHeight, db, err)) { std::fprintf(stderr, "audit of the round-tripped chunk failed: %s\n", err.c_str()); return 1; }
                std::printf("round trip: %s (digest %016llx vs %016llx)\n", da.digest == db.digest ? "byte-equal data" : "DIFFERS (expected for float z within rounding)", (unsigned long long)da.digest, (unsigned long long)db.digest);
                for (size_t i = 3; i < args.size(); ++i) if (args[i] == "--write") {   // write the ridden chunk back (same-size in place / relayout) and audit it from the file
                    const fs::path stbPath = install.root / "data" / "Levels" / "FinalAlbion_RT.stb";
                    std::vector<forge::stb::StaticMapAppend> batch;
                    batch.push_back({m.levelName, entry->name, moved, movedRecord});
                    const fs::path tmp = stbPath.string() + ".atlas-tmp";
                    if (moved.size() == chunk.size()) forge::stb::replaceStaticMaps(stbPath, tmp, batch);
                    else forge::stb::replaceStaticMapsRelayout(stbPath, tmp, batch);
                    fs::rename(tmp, stbPath);
                    const auto a2 = forge::stb::Archive::open(stbPath);
                    for (const auto& m2 : a2.staticMaps()) {
                        if (lower(fs::path(m2.levelName).stem().string()) != lower(args[1])) continue;
                        const auto r2 = a2.readStaticMapRecord(m2);
                        const auto i2 = forge::stbinfo::readInfoBlock(r2.data());
                        const forge::stb::Entry* e2 = nullptr;
                        for (const auto& e : a2.entries()) if (int32_t(e.id) == i2.bankFileIndex) { e2 = &e; break; }
                        const auto c2 = a2.read(*e2);
                        albion::editor::RelocateReport wa;
                        const bool ok = albion::editor::auditChunk(c2, r2, i2.worldX, i2.worldY, i2.mapWidth, i2.mapHeight, wa, err);
                        std::printf("written (%s): %zu bytes, record equal %d, chunk equal %d, audit %s (%zu issues)\n", moved.size() == chunk.size() ? "in place" : "relayout", c2.size(), int(r2 == movedRecord), int(c2 == moved), ok ? "ok" : err.c_str(), wa.issues.size());
                        if (r2 != movedRecord) { for (size_t k = 0; k < r2.size() && k < movedRecord.size(); ++k) if (r2[k] != movedRecord[k]) { std::printf("  first record diff at 0x%zx\n", k); break; } }
                    }
                }
                return 0;
            }
            std::fprintf(stderr, "no static map named %s\n", args[1].c_str());
            return 1;
        } catch (const std::exception& e) { std::fprintf(stderr, "error: %s\n", e.what()); return 1; }
    }
    if (cmd == "chunk-relocate") {   // diagnostic: chunk-relocate <map> <dx> <dy> [--install <root>]: translate + audit at the new box, then translate back and compare
        if (args.size() < 4) { std::fprintf(stderr, "usage: AlbionAtlas chunk-relocate <map> <dx> <dy> [--install <root>]\n"); return 2; }
        std::string installArg;
        for (size_t i = 4; i + 1 < args.size(); ++i) if (args[i] == "--install") installArg = args[i + 1];
        const Install install = findInstall(installArg);
        if (!install.valid) { std::fprintf(stderr, "no Fable install (use --install)\n"); return 2; }
        const int dx = std::atoi(args[2].c_str()), dy = std::atoi(args[3].c_str());
        try {
            const auto archive = forge::stb::Archive::open(install.root / "data" / "Levels" / "FinalAlbion_RT.stb");
            for (const auto& m : archive.staticMaps()) {
                if (lower(fs::path(m.levelName).stem().string()) != lower(args[1])) continue;
                const auto record = archive.readStaticMapRecord(m);
                const auto info = forge::stbinfo::readInfoBlock(record.data());
                const forge::stb::Entry* entry = nullptr;
                for (const auto& e : archive.entries()) if (int32_t(e.id) == info.bankFileIndex) { entry = &e; break; }
                if (!entry) { std::fprintf(stderr, "no bank entry\n"); return 1; }
                const auto chunk = archive.read(*entry);
                auto moved = chunk; auto movedRecord = record;
                albion::editor::RelocateReport rep; std::string err;
                const auto t0 = std::chrono::steady_clock::now();
                if (!albion::editor::relocateChunk(moved, movedRecord, dx, dy, rep, err)) { std::fprintf(stderr, "relocate failed: %s\n", err.c_str()); return 1; }
                const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                std::printf("relocated by (%d,%d) in %.1fs: fg %d patches %d groups %d tree %d detail %d/%d range blocks %d (%d resized)\n", dx, dy, secs,
                            rep.foregroundFrames, rep.patchFrames, rep.groupFrames, rep.treeNodes, rep.detailNodes, rep.detailGroups, rep.rangeBlocks, rep.rangeBlocksResized);
                albion::editor::RelocateReport audit;
                if (!albion::editor::auditChunk(moved, movedRecord, info.worldX + dx, info.worldY + dy, info.mapWidth, info.mapHeight, audit, err)) { std::fprintf(stderr, "audit failed: %s\n", err.c_str()); return 1; }
                std::printf("audit at the new box: %zu issue(s)\n", audit.issues.size());
                for (size_t i = 0; i < audit.issues.size() && i < 10; ++i) std::printf("    %s\n", audit.issues[i].c_str());
                auto back = moved; auto backRecord = movedRecord;
                albion::editor::RelocateReport rep2;
                if (!albion::editor::relocateChunk(back, backRecord, -dx, -dy, rep2, err)) { std::fprintf(stderr, "relocate back failed: %s\n", err.c_str()); return 1; }
                // compare the DATA, not the bytes: range blocks re-encode to
                // slightly different sizes and the foliage section is re-laid,
                // so the oracle is the audit walk's digest of every decoded
                // record/body (foreground, patches, tree, foliage) in walk order
                albion::editor::RelocateReport da, db;
                if (!albion::editor::auditChunk(chunk, record, info.worldX, info.worldY, info.mapWidth, info.mapHeight, da, err)) { std::fprintf(stderr, "audit failed: %s\n", err.c_str()); return 1; }
                if (!albion::editor::auditChunk(back, backRecord, info.worldX, info.worldY, info.mapWidth, info.mapHeight, db, err)) { std::fprintf(stderr, "audit of the round-tripped chunk failed: %s\n", err.c_str()); return 1; }
                const bool counts = da.foregroundFrames == db.foregroundFrames && da.patchFrames == db.patchFrames && da.groupFrames == db.groupFrames && da.treeNodes == db.treeNodes && da.detailNodes == db.detailNodes && da.detailGroups == db.detailGroups;
                // the translation itself: every coordinate site of the moved chunk
                // must equal the original's + the shift (u16 exactly; floats to
                // within their precision), in the same walk order
                size_t bad = 0, checked = 0;
                if (audit.sites.size() != da.sites.size()) { std::printf("    site count differs: %zu vs %zu\n", da.sites.size(), audit.sites.size()); ++bad; }
                else for (size_t i = 0; i < da.sites.size(); ++i) {
                    const float d = da.siteIsX[i] ? float(dx) : float(dy);
                    const float want = da.sites[i] + d, got = audit.sites[i];
                    const float tol = std::max(0.002f, std::fabs(want) * 1e-6f * 4);
                    ++checked;
                    if (std::fabs(want - got) > tol) { if (++bad <= 5) std::printf("    site %zu: %g + %g = %g, chunk has %g\n", i, da.sites[i], d, want, got); }
                }
                std::printf("translation: %zu coordinate(s) checked, %zu wrong; round-trip digest %s; counts %s; chunk %zu -> %zu bytes; back-audit issues %zu\n",
                            checked, bad, da.digest == db.digest ? "identical" : "differs (float precision at a power-of-two boundary is expected)", counts ? "match" : "DIFFER", chunk.size(), moved.size(), db.issues.size());
                return bad || !counts || !audit.issues.empty() || !db.issues.empty() ? 1 : 0;
            }
            std::fprintf(stderr, "%s has no static map\n", args[1].c_str()); return 1;
        } catch (const std::exception& e) { std::fprintf(stderr, "error: %s\n", e.what()); return 1; }
    }
    if (cmd == "chunk-dump") {   // diagnostic: chunk-dump <chunk.bin> <outdir>: every segment as a file (frames decoded), plus segments.txt
        if (args.size() < 3) { std::fprintf(stderr, "usage: AlbionAtlas chunk-dump <chunk.bin> <outdir>\n"); return 2; }
        try {
            std::ifstream cf(args[1], std::ios::binary);
            std::vector<uint8_t> raw((std::istreambuf_iterator<char>(cf)), std::istreambuf_iterator<char>());
            const auto chunk = forge::stbbake::parseChunk(raw);
            fs::create_directories(args[2]);
            std::ofstream index(fs::path(args[2]) / "segments.txt");
            size_t frame = 0;
            for (size_t si = 0; si < chunk.segments.size(); ++si) {
                const auto& s = chunk.segments[si];
                char name[64];
                if (s.kind == forge::stbbake::SegKind::Frame) {
                    const auto body = forge::stbbake::decodeFrame(chunk, frame);
                    std::snprintf(name, sizeof name, "%03zu_frame%03zu.bin", si, frame);
                    std::ofstream(fs::path(args[2]) / name, std::ios::binary).write(reinterpret_cast<const char*>(body.data()), std::streamsize(body.size()));
                    index << name << " raw " << s.start << ".." << s.end << " decoded " << body.size();
                    try {
                        const auto h = forge::stbbake::parsePatchHeader(body);
                        if (h.valid || h.isWaterOnly) {
                            const auto pb = forge::stbbake::parsePatchBody(body);
                            index << " patch " << h.pw << "x" << h.ph << " at " << h.coord0 << "," << h.coord1 << (h.isWaterOnly ? " water-only" : "")
                                  << " tex " << pb.texture.size() << " vb " << pb.vbBlock.size() << " ib " << pb.ibBlock.size() << " trailer " << pb.trailer.size();
                            std::ofstream(fs::path(args[2]) / (std::string(name) + ".trailer"), std::ios::binary).write(reinterpret_cast<const char*>(pb.trailer.data()), std::streamsize(pb.trailer.size()));
                        }
                    } catch (...) {}
                    index << "\n";
                    ++frame;
                } else if (s.kind == forge::stbbake::SegKind::Hdr) {
                    std::snprintf(name, sizeof name, "%03zu_hdr.bin", si);
                    std::ofstream(fs::path(args[2]) / name, std::ios::binary).write(reinterpret_cast<const char*>(raw.data() + s.start), std::streamsize(s.end - s.start));
                    index << name << " raw " << s.start << ".." << s.end << "\n";
                } else index << "pad raw " << s.start << ".." << s.end << "\n";
            }
            std::printf("%zu segments, %zu frames -> %s\n", chunk.segments.size(), frame, args[2].c_str());
            // the background-LOD tree with its file-block references (needs the record next to the chunk)
            if (fs::exists(args[1] + ".record")) {
                std::ifstream rf(args[1] + ".record", std::ios::binary);
                std::vector<uint8_t> record((std::istreambuf_iterator<char>(rf)), std::istreambuf_iterator<char>());
                uint32_t rootPos = 0; std::memcpy(&rootPos, record.data() + 0x68, 4);
                std::ofstream tree(fs::path(args[2]) / "tree.txt");
                std::function<void(const forge::stbbake::BackgroundTreeNode&, int)> dump = [&](const forge::stbbake::BackgroundTreeNode& n, int depth) {
                    const auto& h = n.header;
                    tree << std::string(size_t(depth) * 2, ' ') << "node @" << n.headerOffset << " map " << h.mapX << "," << h.mapY << " " << h.width << "x" << h.height
                         << " bands " << int(h.firstBand) << "/" << int(h.firstNonSplitBand) << "/" << int(h.lastBand)
                         << " fb " << h.fileBlockPos << "+" << h.fileBlockSize << " @" << h.offsetIntoFileBlock
                         << " aabb " << h.aabb[0] << "," << h.aabb[1] << ".." << h.aabb[3] << "," << h.aabb[4] << "\n";
                    for (const auto& l : h.lod)
                        tree << std::string(size_t(depth) * 2 + 4, ' ') << "lod remap " << int(l.optimizedBandRemap) << " fb " << l.fileBlockPos << "+" << l.fileBlockSize << " @" << l.offsetIntoFileBlock << " -> frame at " << (l.fileBlockPos + l.offsetIntoFileBlock) << "\n";
                    for (const auto& c : n.children) dump(c, depth + 1);
                };
                dump(forge::stbbake::parseBackgroundTree(raw, rootPos), 0);
            }
            return 0;
        } catch (const std::exception& e) { std::fprintf(stderr, "error: %s\n", e.what()); return 1; }
    }
    if (cmd == "bake-terrain") {   // diagnostic: stbbake::bakeHeightfield <chunk.bin> <map.lev> <worldX> <worldY> <out.bin>
        if (args.size() < 6) { std::fprintf(stderr, "usage: AlbionAtlas bake-terrain <chunk.bin> <map.lev> <worldX> <worldY> <out.bin>\n"); return 2; }
        try {
            std::ifstream cf(args[1], std::ios::binary);
            std::vector<uint8_t> chunk((std::istreambuf_iterator<char>(cf)), std::istreambuf_iterator<char>());
            const auto lev = forge::lev::File::open(args[2]);
            forge::stbbake::HeightfieldBakeOptions opt;
            opt.requireCanonicalSize = false;
            const auto r = forge::stbbake::bakeHeightfield(chunk, lev, std::atoi(args[3].c_str()), std::atoi(args[4].c_str()), opt);
            for (const auto& n : r.notes) std::printf("%s\n", n.c_str());
            std::ofstream(args[5], std::ios::binary).write(reinterpret_cast<const char*>(r.chunk.data()), std::streamsize(r.chunk.size()));
            std::printf("wrote %s (%zu bytes, %zu patches, %zu foreground frames)\n", args[5].c_str(), r.chunk.size(), r.patches, r.foregroundFrames);
            // verify: every foreground vertex and composed-patch vertex height must equal the LEV height
            {
                const auto out = forge::stbbake::parseChunk(r.chunk);
                double fgMax = 0, bgMax = 0; size_t fgN = 0, bgN = 0;
                const int wx = std::atoi(args[3].c_str()), wy = std::atoi(args[4].c_str());
                auto levH = [&](int lx, int ly) {
                    const int cx = std::min(lx, lev.width() - 1), cy = std::min(ly, lev.height() - 1);
                    return double(forge::stbbake::quantizeEngineHeight(lev.heightAt(cx, cy)));
                };
                for (size_t fi = 0; fi < out.frameIndices.size(); ++fi) {
                    std::vector<uint8_t> body;
                    try { body = forge::stbbake::decodeFrame(out, fi); } catch (...) { continue; }
                    bool isForeground = false;
                    try {
                        const auto fg = forge::stbbake::parseForegroundFrame(body);
                        if (forge::stbbake::serializeForegroundFrame(fg) == body) {
                            isForeground = true;
                            for (const auto& layer : fg.layers) for (const auto& v : layer.vertices) {
                                const int lx = int(v.x) - wx, ly = int(v.y) - wy;
                                if (lx < 0 || ly < 0 || lx >= lev.cellsX() || ly >= lev.cellsY()) continue;
                                fgMax = std::max(fgMax, std::fabs(double(v.height) - levH(lx, ly))); ++fgN;
                            }
                        }
                    } catch (...) {}
                    if (isForeground) continue;
                    try {
                        const auto h = forge::stbbake::parsePatchHeader(body);
                        if (!h.valid || h.isWaterOnly) continue;
                        const auto pb = forge::stbbake::parsePatchBody(body);
                        if (!pb.valid || pb.waterOnly) continue;
                        for (const auto& v : forge::stbbake::decodePatchVertices(pb)) {
                            const int lx = int(v.gridX) - wx, ly = int(v.gridY) - wy;
                            if (lx < 0 || ly < 0 || lx >= lev.cellsX() || ly >= lev.cellsY()) continue;
                            bgMax = std::max(bgMax, std::fabs(double(v.height) - levH(lx, ly))); ++bgN;
                        }
                    } catch (...) {}
                }
                std::printf("verify: foreground %zu vertices max |dh| = %.4f; composed patches %zu vertices max |dh| = %.4f\n", fgN, fgMax, bgN, bgMax);
            }
            return 0;
        } catch (const std::exception& e) { std::fprintf(stderr, "error: %s\n", e.what()); return 1; }
    }

    std::string installArg, out, target, upArg = "y", originArg;
    bool textures = true, layers = false, walkable = false, quiet = false, foliage = false, things = false, creatures = false, particles = false, world = false, water = true;
    int texels = 8;
    float tile = 8.0f, gain = 1.0f;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= args.size()) { std::fprintf(stderr, "%s needs a value\n", a.c_str()); std::exit(2); }
            return args[++i];
        };
        if (a == "--install") installArg = next();
        else if (a == "--out") out = next();
        else if (a == "--no-textures") textures = false;
        else if (a == "--foliage") foliage = true;
        else if (a == "--things") things = true;
        else if (a == "--no-water") water = false;
        else if (a == "--max-texture") albion::foliageexport::setTextureLimit(std::atoi(next().c_str()));
        else if (a == "--world") world = true;
        else if (a == "--creatures") creatures = true;
        else if (a == "--particles") particles = true;
        else if (a == "--layers") layers = true;
        else if (a == "--texels") texels = std::atoi(next().c_str());
        else if (a == "--tile") tile = float(std::atof(next().c_str()));
        else if (a == "--gain") gain = float(std::atof(next().c_str()));
        else if (a == "--up") upArg = lower(next());
        else if (a == "--origin") originArg = next();
        else if (a == "--walkable-colors") walkable = true;
        else if (a == "--quiet") quiet = true;
        else if (!a.empty() && a[0] == '-') { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return usage(); }
        else if (target.empty()) target = a;
        else { std::fprintf(stderr, "unexpected argument %s\n", a.c_str()); return usage(); }
    }

    try {
        const Install install = findInstall(installArg);
        if (cmd == "list") return cmdList(install);
        if (cmd == "mesh") { if (!install.valid || target.empty()) return usage(); return cmdMesh(install, target); }
        if (cmd == "effects") {   // diagnostic: walk every effects.big entry with the ported grammar
            if (!install.valid) return usage();
            std::string err;
            if (!albion::effects::openBank(install.root, err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
            const auto bank = forge::big::File::open(install.root / "data" / "Misc" / "pc" / "effects.big");
            int full = 0, partial = 0, sprites = 0, lights = 0, meshes = 0;
            for (const auto& b : bank.banks())
                for (const auto& e : b.entries) {
                    const auto* fx = albion::effects::byName(e.name);
                    if (!fx) continue;
                    if (fx->parsedFully) ++full; else { ++partial; if (!target.empty() && target == "verbose") std::printf("partial: %s\n", e.name.c_str()); }
                    sprites += int(fx->sprites.size()); lights += int(fx->lights.size()); meshes += int(fx->meshes.size());
                    if (!target.empty() && target != "verbose" && lower(target) == lower(e.name)) {
                        std::printf("%s (%s) id %u: %d systems, %zu sprite, %zu mesh, %zu light\n", fx->name.c_str(), fx->displayName.c_str(), fx->id, fx->systems, fx->sprites.size(), fx->meshes.size(), fx->lights.size());
                        for (const auto& sp : fx->sprites) std::printf("  sprite %-16s tex %d rgba %d,%d,%d,%d size %.2f->%.2f blend %d %.1f/s life %.2fs offset %.2f,%.2f,%.2f%s\n", sp.system.c_str(), sp.sprite, sp.colour[0], sp.colour[1], sp.colour[2], sp.colour[3], sp.startSize, sp.endSize, sp.blendMode, sp.perSecond, sp.lifeSecs, sp.offset[0], sp.offset[1], sp.offset[2], sp.single ? " (single)" : "");
                        for (const auto& l : fx->lights) std::printf("  light  %-16s rgba %d,%d,%d,%d radius %.2f\n", l.system.c_str(), l.colour[0], l.colour[1], l.colour[2], l.colour[3], l.radius);
                        for (const auto& m : fx->meshes) std::printf("  mesh   %-16s mesh %d size %.2f,%.2f,%.2f\n", m.system.c_str(), m.mesh, m.size[0], m.size[1], m.size[2]);
                    }
                }
            std::printf("effects.big: %d parsed fully, %d partially; %d sprite systems, %d lights, %d mesh systems\n", full, partial, sprites, lights, meshes);
            return partial ? 1 : 0;
        }
        if (target.empty()) return usage();

        fs::path temp;
        const fs::path lev = resolveLevel(target, install, temp);
        struct TempGuard { fs::path p; ~TempGuard() { if (!p.empty()) { std::error_code ec; fs::remove(p, ec); } } } guard{temp};

        if (cmd == "info") return cmdInfo(lev);
        if (cmd == "ground") {   // diagnostic: engine background albedo vs our bake -> writes PNGs, prints mean colours
            if (!install.valid) return usage();
            const auto file = forge::lev::File::open(lev);
            const auto bg = albion::stbterrain::backgroundAlbedo(install.root, lev.stem().string(), file.width(), file.height());
            std::printf("%s: %s\n", lev.stem().string().c_str(), bg.note.c_str());
            if (!bg.found) return 1;
            te::Context ctx; std::string err;
            if (!ctx.load(install.root, install.root / "data" / "graphics" / "pc" / "textures.big", err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
            te::Options o; o.texelsPerCell = bg.texelsPerCell; o.gain = gain; o.gameRoot = install.root; o.mapName = lev.stem().string(); o.tileSize = tile;
            const auto scene = te::buildScene(file, o, &ctx);
            te::Options o2 = o; o2.engineLayers = false;
            const auto sceneLev = te::buildScene(file, o2, &ctx);
            auto mean = [](const te::Image& im, double m[3]) {
                m[0] = m[1] = m[2] = 0; size_t n = 0;
                for (size_t i = 0; i + 3 < im.rgba.size(); i += 4) { if (im.rgba[i + 3] == 0) continue; m[0] += im.rgba[i]; m[1] += im.rgba[i + 1]; m[2] += im.rgba[i + 2]; ++n; }
                if (n) { m[0] /= n; m[1] /= n; m[2] /= n; }
            };
            // Mean absolute error against the engine's background bake over the texels it covers.
            auto mae = [&](const te::Image& im) {
                double e = 0; size_t n = 0;
                if (im.width != bg.image.width || im.height != bg.image.height) return -1.0;
                for (size_t i = 0; i + 3 < im.rgba.size(); i += 4) { if (bg.image.rgba[i + 3] == 0) continue; for (int k = 0; k < 3; ++k) e += std::fabs(double(im.rgba[i + k]) - bg.image.rgba[i + k]); n += 3; }
                return n ? e / n : -1.0;
            };
            double a[3], b[3], c[3]; mean(bg.image, a); mean(scene.albedo, b); mean(sceneLev.albedo, c);
            std::printf("engine background mean RGB %.1f %.1f %.1f\n", a[0], a[1], a[2]);
            std::printf("  engine-pass bake (gain %.2f): mean %.1f %.1f %.1f  MAE vs background %.1f  (%d passes)\n", gain, b[0], b[1], b[2], mae(scene.albedo), scene.enginePasses);
            std::printf("  LEV-theme bake:               mean %.1f %.1f %.1f  MAE vs background %.1f\n", c[0], c[1], c[2], mae(sceneLev.albedo));
            const std::string stem = out.empty() ? lev.stem().string() : fs::path(out).stem().string();
            auto save = [&](const te::Image& im, const std::string& name) { const auto png = te::encodePng(im); std::ofstream(name, std::ios::binary).write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size())); std::printf("wrote %s\n", name.c_str()); };
            save(bg.image, stem + "_engine_bg.png"); save(scene.albedo, stem + "_our_bake.png"); save(sceneLev.albedo, stem + "_lev_bake.png");
            return 0;
        }
        if (cmd == "layers") {   // diagnostic: the engine's per-patch texture passes (direction, texture, vertex bytes)
            const auto file = forge::lev::File::open(lev);
            const auto fl = albion::stbterrain::loadLayers(install.root, lev.stem().string(), file.width(), file.height());
            if (!fl.found) { std::printf("%s: %s\n", lev.stem().string().c_str(), fl.note.c_str()); return 1; }
            std::map<int, int> dirs; std::map<uint32_t, int> texs;
            int b1nz = 0, b2nz = 0, verts = 0; std::map<int, int> blendHist;
            for (const auto& L : fl.layers) {
                ++dirs[L.direction]; ++texs[L.texture];
                for (const auto& v : L.vertices) { ++verts; if (v.b1) ++b1nz; if (v.b2) ++b2nz; ++blendHist[v.blend / 32]; }
            }
            std::printf("%s: %d patches, %zu layers; directions:", lev.stem().string().c_str(), fl.frames, fl.layers.size());
            for (auto& [d, n] : dirs) std::printf(" %d:%d", d, n);
            std::printf("\n  %zu textures used; %d vertices, b1 nonzero %d, b2 nonzero %d; blend/32 histogram:", texs.size(), verts, b1nz, b2nz);
            for (auto& [k, n] : blendHist) std::printf(" [%d]=%d", k, n);
            std::printf("\n");
            // Height check: STB foreground vertex heights vs the LEV heightmap.
            {
                double maxd = 0; int over = 0, n = 0; int mx = 0, my = 0;
                for (const auto& L : fl.layers)
                    for (const auto& v : L.vertices) {
                        if (v.x < 0 || v.y < 0 || v.x > file.width() || v.y > file.height()) continue;
                        const double dlt = std::fabs(double(v.height) - file.heightAt(v.x, v.y));
                        ++n; if (dlt > 0.5) ++over; if (dlt > maxd) { maxd = dlt; mx = v.x; my = v.y; }
                    }
                std::printf("  STB vertex heights vs LEV: %d vertices, %d differ by > 0.5, max diff %.2f at (%d,%d)\n", n, over, maxd, mx, my);
            }
            int shown = 0;
            for (const auto& L : fl.layers) {
                if (L.patchIndex > 1 && shown > 12) break;
                std::printf("  patch %d layer %d dir %u tex %u bg %u bump %u shared %d verts %zu strip %zu  bytes:", L.patchIndex, L.layerIndex, L.direction, L.texture, L.backgroundTexture, L.bumpTexture, L.sharedIndexBuffer ? 1 : 0, L.vertices.size(), L.strip.size());
                for (size_t i = 0; i < std::min<size_t>(6, L.vertices.size()); ++i) std::printf(" (%d,%d %u,%u,%u)", L.vertices[i].x, L.vertices[i].y, L.vertices[i].blend, L.vertices[i].b1, L.vertices[i].b2);
                std::printf("\n"); ++shown;
            }
            return 0;
        }
        if (cmd == "coverage") {   // diagnostic: how many cells the STB foreground frames draw (expected: all)
            const auto file = forge::lev::File::open(lev);
            const auto mask = albion::stbterrain::load(install.root, lev.stem().string(), file.width(), file.height());
            std::printf("%s: %d of %d cells drawn by %d foreground frames%s\n", lev.stem().string().c_str(), mask.presentCells,
                        file.width() * file.height(), mask.frames, mask.found ? "" : (" (" + mask.note + ")").c_str());
            return mask.found && mask.presentCells == file.width() * file.height() ? 0 : 1;
        }
        if (cmd != "export") return usage();

        if (out.empty()) out = lev.stem().string() + ".glb";
        const std::string ext = lower(fs::path(out).extension().string());
        if (ext != ".glb" && ext != ".obj") {
            std::fprintf(stderr, "--out must end in .glb or .obj\n");
            return 2;
        }

        te::Options o;
        o.textures = textures;
        o.layers = layers;
        o.walkableColor = walkable;
        o.water = water;
        o.texelsPerCell = std::clamp(texels, 1, 64);
        o.tileSize = tile;
        o.gain = std::clamp(gain, 0.25f, 4.0f);
        o.gameRoot = install.root;
        // The STB bake is keyed by map name; a loose .lev given as a path is the user's
        // own file and gets the LEV-theme bake even if it shares a retail name.
        o.mapName = (install.valid && !(fs::exists(target) && fs::is_regular_file(target))) ? lev.stem().string() : std::string();
        o.up = upArg == "z" ? te::UpAxis::Z : te::UpAxis::Y;
        if (!originArg.empty()) {
            const size_t c = originArg.find(',');
            if (c == std::string::npos) { std::fprintf(stderr, "--origin needs X,Y\n"); return 2; }
            o.originX = float(std::atof(originArg.substr(0, c).c_str()));
            o.originY = float(std::atof(originArg.substr(c + 1).c_str()));
        } else if (world) {
            if (!install.valid || !te::worldOrigin(install.root, lev.stem().string(), o.originX, o.originY)) {
                std::fprintf(stderr, "--world: no WLD placement found for %s (is it a retail map name?)\n", lev.stem().string().c_str());
                return 1;
            }
            if (!quiet) std::printf("world origin: %.0f, %.0f\n", o.originX, o.originY);
        }
        if (!quiet) o.log = [](const std::string& m) { std::printf("  %s\n", m.c_str()); };
        if ((foliage || things) && !install.valid) {
            std::fprintf(stderr, "--foliage / --things need a Fable install (pass --install <root>)\n");
            return 1;
        }
        if (textures) {
            if (!install.valid) {
                std::fprintf(stderr, "no Fable install found for textures (pass --install <root> or --no-textures)\n");
                return 1;
            }
            o.gameRoot = install.root;
            o.texturesBig = install.root / "data" / "graphics" / "pc" / "textures.big";
            if (!quiet) std::printf("install: %s (%s)\n", install.root.string().c_str(), install.how.c_str());
        }

        const auto t0 = std::chrono::steady_clock::now();
        const auto file = forge::lev::File::open(lev);
        if (!quiet) std::printf("map: %s  %dx%d cells\n", file.source().c_str(), file.width(), file.height());
        te::Context ctx;
        if (textures || foliage || things) {
            std::string err;
            if (!ctx.load(install.root, install.root / "data" / "graphics" / "pc" / "textures.big", err))
                std::fprintf(stderr, "warning: %s\n", err.c_str());
        }
        const te::Scene scene = te::buildScene(file, o, &ctx);
        albion::foliageexport::Scene fol;
        if (foliage) {
            albion::foliageexport::Options fo;
            fo.gameRoot = install.root;
            fo.textures = textures;
            fo.up = o.up;
            fo.mapLocal = originArg.empty() && !world;
            fo.log = o.log;
            fol = albion::foliageexport::load(lev.stem().string(), fo, ctx);
        }
        albion::foliageexport::Scene thg;
        albion::thingsexport::Stats thingStats;
        if (things) {
            albion::thingsexport::Options to;
            to.gameRoot = install.root;
            to.textures = textures;
            to.creatures = creatures;
            to.particles = particles;
            to.up = o.up;
            to.originX = o.originX; to.originY = o.originY;
            to.log = o.log;
            thg = albion::thingsexport::load(lev.stem().string(), to, ctx, &thingStats);
        }
        std::vector<const albion::foliageexport::Scene*> layersOut;
        if (foliage) layersOut.push_back(&fol);
        if (things) layersOut.push_back(&thg);
        const auto written = ext == ".glb" ? albion::foliageexport::writeGlbWith(scene, layersOut, out)
                                           : albion::foliageexport::writeObjWith(scene, layersOut, out);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

        if (!quiet) {
            std::printf("wrote:\n");
            for (const auto& p : written) std::printf("  %s (%llu bytes)\n", p.string().c_str(),
                                                      (unsigned long long)fs::file_size(p));
            if (foliage) std::printf("foliage: %zu instances, %zu meshes, %zu triangles%s\n", fol.instances.size(), fol.meshes.size(),
                                     fol.triangleCount(), fol.found ? "" : " (none found for this map)");
            if (things) std::printf("things: %d placed of %d, %zu meshes, %zu triangles\n", thingStats.placed, thingStats.things,
                                    thg.meshes.size(), thg.triangleCount());
            std::printf("%zu vertices, %zu triangles, %s, %.2fs\n", scene.vertices.size(), scene.indices.size() / 3,
                        scene.hasAlbedo ? (std::to_string(scene.albedo.width) + "x" + std::to_string(scene.albedo.height) + " albedo").c_str()
                                        : "untextured", secs);
            if (!scene.warnings.empty()) std::printf("%zu warning(s) above\n", scene.warnings.size());
        }
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
