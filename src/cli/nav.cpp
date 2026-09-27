#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "forge/big.hpp"
#include "forge/bin.hpp"
#include "forge/defdecode.hpp"
#include "forge/tng.hpp"
#include "forge/lev.hpp"
#include "forge/levelstore.hpp"
#include "forge/lzo.hpp"
#include "forge/navpatch.hpp"
#include "navlines.hpp"
#include "terrainexport.hpp"
#include "thingsexport.hpp"

namespace fs = std::filesystem;
#include "cli/common.hpp"

namespace albion::cli {

// forge CLI: navigation diagnostics
std::optional<int> runNav(const std::string& cmd, const Args& args) {
    if (cmd == "nav-lines") {   // nav-lines <map> [--raw-verts] [--install <root>]: placed objects' blocking lines vs the shipped nav
        if (args.size() < 2) { std::fprintf(stderr, "usage: forge nav-lines <map> [--raw-verts] [--install <root>]\n"); return 2; }
        std::string installArg;
        bool rawVerts = false;
        for (size_t i = 2; i < args.size(); ++i) {
            if (args[i] == "--install" && i + 1 < args.size()) installArg = args[++i];
            else if (args[i] == "--raw-verts") rawVerts = true;
        }
        const Install install = findInstall(installArg);
        if (!install.valid) { std::fprintf(stderr, "no Fable install (use --install)\n"); return 1; }
        const std::string map = args[1];

        terrainexport::Context ctx;
        std::string err;
        if (!ctx.load(install.root, install.root / "data" / "graphics" / "pc" / "textures.big", err))
            std::fprintf(stderr, "warning: %s\n", err.c_str());
        thingsexport::Options to;
        to.gameRoot = install.root;
        to.textures = false;
        to.up = terrainexport::UpAxis::Z;
        thingsexport::Stats stats;
        const auto scene = thingsexport::load(map, to, ctx, &stats);

        const auto big = forge::big::File::open(install.root / "data" / "graphics" / "graphics.big");
        navlines::HullOptions ho;
        ho.applySubMeshTransform = !rawVerts;
        navlines::HullCache hulls(big, ho);

        // every instance's level-0 lines in map-local world units
        std::vector<navlines::Line> lines;
        size_t withLines = 0, noHull = 0, noHelper = 0;
        for (const auto& inst : scene.instances) {
            if (inst.mesh < 0) continue;
            const auto* hull = hulls.forRenderMesh(scene.meshes[size_t(inst.mesh)].meshId);
            if (!hull) { ++noHull; continue; }
            if (hull->levels.empty() || hull->levels[0].empty()) { ++noHelper; continue; }
            ++withLines;
            for (const auto& l : hull->levels[0]) lines.push_back(navlines::toWorld(l, inst));
        }

        // the shipped nav, layer 0, as a 0.5-unit raster of navigable cells
        const auto levels = forge::levelstore::detect(install.root);
        const auto levBytes = forge::levelstore::requireFile(levels, map + ".lev");
        const fs::path tmp = fs::temp_directory_path() / "FableForge" / "navlines";
        fs::create_directories(tmp);
        const fs::path levPath = tmp / (map + ".lev");
        { FILE* f = std::fopen(levPath.string().c_str(), "wb"); std::fwrite(levBytes.data(), 1, levBytes.size(), f); std::fclose(f); }
        const auto lev = forge::lev::File::open(levPath);
        const auto nav = forge::navmesh::parseNavigation(lev);
        if (nav.sections.empty()) { std::fprintf(stderr, "%s has no navigation\n", map.c_str()); return 1; }
        const int W = lev.width(), H = lev.height();
        std::vector<uint8_t> shippedNav(size_t(W) * 2 * size_t(H) * 2, 0);   // half-unit cells
        for (const auto& n : nav.sections[0].nodes) {
            if (n.marker || !n.leaf || n.layer != 0) continue;
            const float size = 32.0f / float(1 << n.level);
            const int x0 = int(std::lround((n.cx - size / 2) * 2)), y0 = int(std::lround((n.cy - size / 2) * 2));
            const int s = int(std::lround(size * 2));
            for (int y = std::max(0, y0); y < std::min(H * 2, y0 + s); ++y)
                for (int x = std::max(0, x0); x < std::min(W * 2, x0 + s); ++x)
                    shippedNav[size_t(y) * size_t(W) * 2 + size_t(x)] = n.switchable ? 2 : 1;
        }

        // unit cells: walkable terrain only; shipped = fully blocked (all 4 halves), predicted = a line crosses the 1x1 box
        const auto crossed = navlines::rasterise(lines, W, H);
        size_t tp = 0, fp = 0, fn = 0, walk = 0;
        size_t fnNearLine = 0, switchCells = 0, switchCrossed = 0;
        size_t splitCells = 0, htp = 0, hfp = 0, hfn = 0;
        std::vector<navlines::Line> doubled;
        for (const auto& l : lines) doubled.push_back({l.x0 * 2, l.y0 * 2, l.x1 * 2, l.y1 * 2});
        const auto crossedHalf = navlines::rasterise(doubled, W * 2, H * 2);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                if (!lev.walkableAt(x, y)) continue;
                ++walk;
                int halves = 0, switchHalves = 0;
                for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) {
                    const uint8_t v = shippedNav[size_t(y * 2 + dy) * size_t(W) * 2 + size_t(x * 2 + dx)];
                    halves += v == 1; switchHalves += v == 2;
                }
                if (switchHalves) { ++switchCells; if (crossed[size_t(y) * size_t(W) + size_t(x)]) ++switchCrossed; continue; }
                if (halves > 0 && halves < 4) {
                    // split into half cells (a detailed area): score each half against half-unit boxes
                    ++splitCells;
                    for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) {
                        const size_t hi = size_t(y * 2 + dy) * size_t(W) * 2 + size_t(x * 2 + dx);
                        const bool hb = shippedNav[hi] == 0, hp = crossedHalf[hi] != 0;
                        if (hp && hb) ++htp; else if (hp) ++hfp; else if (hb) ++hfn;
                    }
                    continue;
                }
                const bool shippedBlocked = halves == 0;
                const bool predicted = crossed[size_t(y) * size_t(W) + size_t(x)] != 0;
                if (predicted && shippedBlocked) ++tp;
                else if (predicted) ++fp;
                else if (shippedBlocked) {
                    ++fn;
                    bool near = false;
                    for (int dy = -2; dy <= 2 && !near; ++dy) for (int dx = -2; dx <= 2 && !near; ++dx) {
                        const int xx = x + dx, yy = y + dy;
                        if (xx >= 0 && yy >= 0 && xx < W && yy < H && crossed[size_t(yy) * size_t(W) + size_t(xx)]) near = true;
                    }
                    if (near) ++fnNearLine;
                }
            }
        std::printf("%s: %zu instances (%zu with nav lines, %zu without a hull, %zu hull without NAV_LAYER), %zu lines\n",
                    map.c_str(), scene.instances.size(), withLines, noHull, noHelper, lines.size());
        std::printf("walkable cells %zu: line-crossed & shipped-blocked %zu, line-crossed but shipped-navigable %zu, shipped-blocked not crossed %zu (%zu of them within 2 cells of a line)\n",
                    walk, tp, fp, fn, fnNearLine);
        std::printf("door/switchable cells %zu (%zu line-crossed), left out of the scores\n", switchCells, switchCrossed);
        std::printf("half-split cells %zu: halves crossed & blocked %zu, crossed but navigable %zu, blocked not crossed %zu\n", splitCells, htp, hfp, hfn);
        // which meshes cross shipped-navigable cells (the false positives), worst first
        std::map<std::string, std::pair<size_t, size_t>> byMesh;   // name -> (instances, cells crossed but navigable)
        for (const auto& inst : scene.instances) {
            if (inst.mesh < 0) continue;
            const auto* hull = hulls.forRenderMesh(scene.meshes[size_t(inst.mesh)].meshId);
            if (!hull || hull->levels.empty() || hull->levels[0].empty()) continue;
            std::vector<navlines::Line> own;
            for (const auto& l : hull->levels[0]) own.push_back(navlines::toWorld(l, inst));
            const auto mine = navlines::rasterise(own, W, H);
            auto& row = byMesh[scene.meshes[size_t(inst.mesh)].name];
            ++row.first;
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) {
                    if (!mine[size_t(y) * size_t(W) + size_t(x)] || !lev.walkableAt(x, y)) continue;
                    int halves = 0;
                    for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) halves += shippedNav[size_t(y * 2 + dy) * size_t(W) * 2 + size_t(x * 2 + dx)] == 1;
                    if (halves == 4) ++row.second;
                }
        }
        std::vector<std::pair<std::string, std::pair<size_t, size_t>>> rows(byMesh.begin(), byMesh.end());
        std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second.second > b.second.second; });
        for (size_t i = 0; i < rows.size() && i < 6 && rows[i].second.second; ++i)
            std::printf("  crosses navigable cells: %-48s x%zu  %zu cells\n", rows[i].first.c_str(), rows[i].second.first, rows[i].second.second);
        // detailed areas: things whose def sets UseHighDetailQuadTree -> physics bounding box
        {
            const auto tngBytes = forge::levelstore::requireFile(levels, map + ".tng");
            const auto tng = forge::tng::File::parseText(std::string(tngBytes.begin(), tngBytes.end()), map + ".tng");
            const auto defs = forge::bin::File::open(install.root / "data" / "CompiledDefs" / "names.bin", install.root / "data" / "CompiledDefs" / "game.bin");
            const uint32_t tag = forge::defdecode::fieldTag("UseHighDetailQuadTree");
            std::map<std::string, bool> flagOf;
            auto highDetail = [&](const std::string& def) {
                const auto it = flagOf.find(def);
                if (it != flagOf.end()) return it->second;
                bool on = false;
                if (const auto* e = defs.find(def))
                    for (size_t i = 0; i + 5 <= e->data.size(); ++i) {
                        uint32_t t; std::memcpy(&t, e->data.data() + i, 4);
                        if (t == tag) { on = e->data[i + 4] != 0; break; }
                    }
                return flagOf[def] = on;
            };
            std::vector<navlines::Box> boxes;
            std::set<int> seenThing;
            for (const auto& inst : scene.instances) {
                if (inst.mesh < 0 || inst.thing < 0 || size_t(inst.thing) >= tng.things().size()) continue;
                if (!seenThing.insert(inst.thing).second) continue;   // the thing's own mesh (children share its index)
                if (!highDetail(tng.things()[size_t(inst.thing)].definitionType())) continue;
                const auto* hull = hulls.forRenderMesh(scene.meshes[size_t(inst.mesh)].meshId);
                if (!hull) continue;
                boxes.push_back(navlines::detailBox(*hull, inst, W, H));
            }
            size_t l6 = 0, l6Outside = 0;
            for (const auto& n : nav.sections[0].nodes) {
                if (n.marker || !n.leaf || n.layer != 0 || n.level != 6) continue;
                ++l6;
                bool inside = false;
                for (const auto& b : boxes)
                    if (n.cx + 0.25f > b.x0 && n.cx - 0.25f < b.x1 && n.cy + 0.25f > b.y0 && n.cy - 0.25f < b.y1) { inside = true; break; }
                if (!inside) ++l6Outside;
            }
            std::printf("detailed areas: %zu things with UseHighDetailQuadTree; shipped half-unit leaves %zu, %zu outside every predicted box\n", boxes.size(), l6, l6Outside);
        }
        std::printf("precision %.3f  recall(near lines) %.3f\n", tp + fp ? double(tp) / double(tp + fp) : 0.0,
                    tp + fnNearLine ? double(tp) / double(tp + fnNearLine) : 0.0);
        return 0;
    }
    return std::nullopt;
}

}  // namespace albion::cli
