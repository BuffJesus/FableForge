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
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "forge/big.hpp"
#include "forge/env.hpp"
#include "forge/meshpreview.hpp"
#include "forge/lev.hpp"
#include "forge/wad.hpp"
#include "effects.hpp"
#include "foliageexport.hpp"
#include "stbterrain.hpp"
#include "thingsexport.hpp"
#include "terrainexport.hpp"

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
