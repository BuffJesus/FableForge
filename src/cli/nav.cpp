#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <tuple>
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
#include "forge/navmesh.hpp"
#include "navlines.hpp"
#include "overworld.hpp"
#include "terrainexport.hpp"
#include "thingsexport.hpp"

namespace fs = std::filesystem;
#include "cli/common.hpp"

namespace albion::cli {

// forge CLI: navigation diagnostics
std::optional<int> runNav(const std::string& cmd, const Args& args) {
    if (cmd == "nav-lines" || cmd == "nav-compare") {   // read-only navigation diagnostics
      try {
        if (args.size() < 2) { std::fprintf(stderr, "usage: forge %s <map> [--raw-verts] [--details] [--install <root>]\n", cmd.c_str()); return 2; }
        std::string installArg;
        const bool compare = cmd == "nav-compare";
        bool rawVerts = compare, details = false;
        for (size_t i = 2; i < args.size(); ++i) {
            if (args[i] == "--install" && i + 1 < args.size()) installArg = args[++i];
            else if (args[i] == "--raw-verts") rawVerts = true;
            else if (args[i] == "--details") details = true;
            else { std::fprintf(stderr, "unknown or incomplete option: %s\n", args[i].c_str()); return 2; }
        }
        const Install install = findInstall(installArg);
        if (!install.valid) { std::fprintf(stderr, "no Fable install (use --install)\n"); return 1; }
        const std::string map = args[1];
        if (map.empty() || map == "." || map == ".." || fs::path(map).filename().string() != map)
            throw std::invalid_argument("map must be a level name, not a path");

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
        LevelWorkspace scratch;
        const auto lev = forge::lev::File::open(resolveLevel(map, install, scratch));
        const auto nav = forge::navmesh::parseNavigation(lev);
        if (nav.sections.empty()) { std::fprintf(stderr, "%s has no navigation\n", map.c_str()); return 1; }
        if (compare && nav.sections[0].name != "NULL") {
            std::fprintf(stderr, "nav-compare currently requires the first section to be NULL\n"); return 1;
        }
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
            forge::navmesh::GroundGeometry geometry;
            size_t unresolvedDoors = 0;
            const uint32_t componentsTag = forge::defdecode::fieldTag("Components");
            std::map<std::pair<std::string, std::string>, bool> componentOf;
            auto hasComponent = [&](const std::string& name, const std::string& component) {
                const auto key = std::make_pair(name, component);
                if (componentOf.contains(key)) return componentOf[key];
                const auto componentName = defs.originalNameOffset(component);
                bool found = false;
                if (const auto* e = defs.find(name); e && componentName) {
                    // CThingComponentSet: u32 count, then {name offset, parameter, flag} (9 bytes).
                    for (size_t at = 0; at + 8 <= e->data.size(); ++at) {
                        uint32_t tagValue, count;
                        std::memcpy(&tagValue, e->data.data() + at, 4);
                        if (tagValue != componentsTag) continue;
                        std::memcpy(&count, e->data.data() + at + 4, 4);
                        if (count > (e->data.size() - at - 8) / 9) throw std::runtime_error("invalid Components list in " + name);
                        for (size_t i = 0; i < count; ++i) {
                            uint32_t ref; std::memcpy(&ref, e->data.data() + at + 8 + i * 9, 4);
                            found |= ref == *componentName;
                        }
                        break;
                    }
                }
                return componentOf[key] = found;
            };
            auto switchable = [&](const std::string& name) { return hasComponent(name, "CTCSwitchableNavigation"); };
            // GetMapNavigationAreaInit (0x01c905d0) gathers RegionSeeds separately
            // from mesh ACTION_POINT dummies. SaveToFile stores ActionPoints;
            // using those as seeds invents islands and loses region exits.
            // Match native category order; leaf ownership/search order and
            // GetNavigationLayerAt still need the world reconstruction.
            auto collectSeeds = [&](const forge::tng::File& things, const std::string& mapName, float ox, float oy) {
                for (int category = 0; category < 6; ++category) {
                    for (size_t i = 0; i < things.things().size(); ++i) {
                        if (things.sectionOf(i) != "NULL") continue;
                        const auto& thing = things.things()[i];
                        const auto def = thing.definitionType();
                        auto has = [&](const char* component) { return thing.findCtc(component) || hasComponent(def, component); };
                        const auto type = lower(thing.type);
                        const bool selected = category == 0 ? type == "village" : category == 1 ? type == "aicreature" :
                            category == 2 ? has("CTCCreatureGenerator") : category == 3 ? has("CTCDNavigationSeed") :
                            category == 4 ? has("CTCDRegionEntrance") : has("CTCDRegionExit");
                        if (!selected) continue;
                        const auto* physics = thing.findCtc("CTCPhysicsStandard");
                        if (!physics) physics = thing.findCtc("CTCPhysicsNavigator");
                        if (!physics) continue;
                        float x = 0, y = 0;
                        for (const auto& property : physics->properties) {
                            if (lower(property.key) == "positionx") x = std::stof(property.value);
                            if (lower(property.key) == "positiony") y = std::stof(property.value);
                        }
                        x += ox; y += oy;
                        if (!std::isfinite(x) || !std::isfinite(y)) throw std::runtime_error("non-finite seed position in " + mapName);
                        if (x < 0 || y < 0 || x >= W || y >= H) continue;
                        geometry.regionSeeds.push_back({x, y});
                        if (details) std::printf("  region seed (ground assumed): %s/%s %.6f,%.6f\n", mapName.c_str(), def.c_str(), x, y);
                    }
                }
            };
            std::map<std::string, uint32_t> closedMeshOf;
            auto closedDoorMesh = [&](const std::string& name) {
                if (closedMeshOf.contains(name)) return closedMeshOf[name];
                uint32_t mesh = 0;
                if (const auto* e = defs.find(name); e && e->data.size() >= 5) {
                    // Same CDef-listing prefix used by gamedata's component lookup.
                    const size_t count = size_t(e->data[3]) | (size_t(e->data[4]) << 8);
                    if (count > (e->data.size() - 5) / 12) throw std::runtime_error("invalid def-component prefix in " + name);
                    for (size_t i = 0; i < count; ++i) {
                        uint32_t ref; std::memcpy(&ref, e->data.data() + 9 + i * 12, 4);
                        if (ref >= defs.entries().size()) continue;
                        const auto& d = defs.entries()[ref];
                        if (d.definition != "CDoorDef") continue;
                        const uint32_t tag = forge::defdecode::fieldTag("ClosedCollisionMesh");
                        for (size_t at = 3; at + 8 <= d.data.size(); ++at) {
                            uint32_t field; std::memcpy(&field, d.data.data() + at, 4);
                            if (field == tag) { std::memcpy(&mesh, d.data.data() + at + 4, 4); break; }
                        }
                    }
                }
                return closedMeshOf[name] = mesh;
            };
            auto collect = [&](const foliageexport::Scene& sc, const forge::tng::File& things, const std::string& mapName) {
                for (const auto& inst : sc.instances) {
                    if (inst.mesh < 0 || inst.thing < 0 || size_t(inst.thing) >= things.things().size()) continue;
                    if (compare && things.sectionOf(size_t(inst.thing)) != "NULL") continue;
                    const auto& thing = things.things()[size_t(inst.thing)];
                    const bool child = inst.tag.rfind("child:", 0) == 0;
                    const auto def = child ? inst.tag.substr(6) : thing.definitionType();
                    const uint32_t closedMesh = compare ? closedDoorMesh(def) : 0;
                    const auto* hull = closedMesh ? hulls.forPhysicsMesh(closedMesh) : hulls.forRenderMesh(sc.meshes[size_t(inst.mesh)].meshId);
                    const bool isSwitchable = compare && (switchable(def) || (!child && thing.findCtc("CTCSwitchableNavigation")));
                    if (details && compare && isSwitchable)
                        std::printf("  switchable object: %s/%s closed-mesh=%u hull=%d layers=%zu\n", mapName.c_str(), def.c_str(), closedMesh, hull != nullptr, hull ? hull->levels.size() : 0);
                    if (!hull) { if (isSwitchable) ++unresolvedDoors; continue; }
                    if (highDetail(def)) {
                        const auto b = navlines::detailBox(*hull, inst, W, H);
                        if (b.x0 < b.x1 && b.y0 < b.y1) {
                            boxes.push_back(b);
                            if (details) std::printf("  detailed object: %s/%s (%.2f, %.2f)-(%.2f, %.2f)\n", mapName.c_str(), def.c_str(), b.x0, b.y0, b.x1, b.y1);
                        }
                    }
                    if (!compare || hull->levels.empty()) continue;
                    std::vector<forge::navmesh::Line> own;
                    for (const auto& l : hull->levels[0]) {
                        const auto v = navlines::toWorld(l, inst);
                        own.push_back({v.x0, v.y0, v.x1, v.y1});
                    }
                    if (own.empty()) continue;
                    if (isSwitchable) {
                        const auto uid = thing.find("UID");
                        // Automatic child UIDs require a separate native creation trace.
                        if (child || !uid) { ++unresolvedDoors; continue; }
                        const uint64_t mapUid = std::stoull(*uid) & 0xffffffffffULL;
                        geometry.switchableLines.push_back({mapUid, std::move(own)});
                    } else geometry.blockingLines.insert(geometry.blockingLines.end(), own.begin(), own.end());
                }
            };
            collect(scene, tng, map);
            if (compare) collectSeeds(tng, map, 0, 0);
            editor::WorldLayout layout;
            if (!editor::loadWorldLayout(install.root, layout, err)) {
                std::fprintf(stderr, "cannot inspect neighbouring detailed areas: %s\n", err.c_str());
                return 1;
            }
            if (const auto* target = layout.find(map)) {
                for (const auto* neighbour : layout.touching(*target, target->x, target->y)) {
                    if (neighbour->name == map) continue;
                    auto neighbourOptions = to;
                    neighbourOptions.originX = float(neighbour->x - target->x);
                    neighbourOptions.originY = float(neighbour->y - target->y);
                    const auto neighbourScene = thingsexport::load(neighbour->name, neighbourOptions, ctx);
                    const auto bytes = forge::levelstore::requireFile(levels, neighbour->name + ".tng");
                    const auto neighbourTng = forge::tng::File::parseText(std::string(bytes.begin(), bytes.end()), neighbour->name + ".tng");
                    collect(neighbourScene, neighbourTng, neighbour->name);
                    if (compare) collectSeeds(neighbourTng, neighbour->name, neighbourOptions.originX, neighbourOptions.originY);
                }
            }
            size_t l6 = 0, l6Outside = 0;
            for (const auto& n : nav.sections[0].nodes) {
                if (n.marker || !n.leaf || n.layer != 0 || n.level != 6) continue;
                ++l6;
                const float x = std::floor(n.cx), y = std::floor(n.cy);
                if (!navlines::requestsHigherDetail({x, y, x + 1, y + 1}, boxes)) {
                    ++l6Outside;
                    if (details) std::printf("  unexplained half leaf: %.2f, %.2f\n", n.cx, n.cy);
                }
            }
            std::printf("detailed areas: %zu things with UseHighDetailQuadTree (including neighbours/children); shipped half-unit leaves %zu, %zu without a qualifying parent\n", boxes.size(), l6, l6Outside);
            if (compare) {
                for (const auto& b : boxes) geometry.detailedAreas.push_back({b.x0, b.y0, b.x1, b.y1});
                auto groundSource = nav.sections[0];
                groundSource.layerCount = 1;
                groundSource.positions.clear();
                for (size_t at = 0; at < nav.sections[0].positions.size(); at += 12) {
                    int32_t layer; std::memcpy(&layer, nav.sections[0].positions.data() + at + 8, 4);
                    if (details) {
                        float x, y;
                        std::memcpy(&x, nav.sections[0].positions.data() + at, 4);
                        std::memcpy(&y, nav.sections[0].positions.data() + at + 4, 4);
                        std::printf("  stored action point (not a seed): %.6f,%.6f layer=%d\n", x, y, layer);
                    }
                    if (layer == 0) groundSource.positions.insert(groundSource.positions.end(),
                        nav.sections[0].positions.begin() + at, nav.sections[0].positions.begin() + at + 12);
                }
                const auto generated = forge::navmesh::generateGround(lev, groundSource, geometry);
                using Key = std::tuple<bool, int, int, int, bool, int, std::vector<uint64_t>, bool>;
                auto keys = [](const forge::navmesh::RetailSection& s) {
                    std::multiset<Key> out;
                    for (const auto& n : s.nodes) if (!n.marker && n.layer == 0)
                        out.emplace(n.leaf, n.level, int(std::lround(n.cx * 4)), int(std::lround(n.cy * 4)), n.switchable, n.leaf ? n.preference : 0, n.uids, n.blocked);
                    return out;
                };
                const auto expected = keys(nav.sections[0]), actual = keys(generated.section);
                std::vector<Key> matched;
                std::set_intersection(expected.begin(), expected.end(), actual.begin(), actual.end(), std::back_inserter(matched));
                if (details) {
                    auto showDifference = [](const auto& a, const auto& b, const char* label) {
                        std::vector<Key> missing;
                        std::set_difference(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(missing));
                        for (size_t i = 0; i < missing.size() && i < 24; ++i) {
                            const auto& [leaf, level, x, y, door, pref, uids, blocked] = missing[i];
                            std::printf("  %s-only node: leaf=%d level=%d centre=%.2f,%.2f door=%d pref=%d blocked=%d", label, leaf, level, x / 4.0f, y / 4.0f, door, pref, blocked);
                            for (const auto uid : uids) std::printf(" uid=%llu", (unsigned long long)uid);
                            std::printf("\n");
                        }
                    };
                    showDifference(expected, actual, "retail");
                    showDifference(actual, expected, "generated");
                }
                std::printf("ground rebuild: %zu static lines, %zu switchable things, %zu unresolved doors (hull/child/UID); %zu usable region seeds, %zu island leaves removed\n",
                    geometry.blockingLines.size(), geometry.switchableLines.size(), unresolvedDoors, generated.anchorsUsed, generated.leavesRemoved);
                std::printf("node multiset (shape/type/preference/UID): %zu matched, %zu generated-only, %zu retail-only; regions %u generated / %u retail\n",
                    matched.size(), actual.size() - matched.size(), expected.size() - matched.size(), generated.section.regionCount, nav.sections[0].regionCount);
                // Compare geometric neighbour sets, independent of serialized node indices.
                auto graph = [](const forge::navmesh::RetailSection& s) {
                    using Location = std::tuple<int, int, int>;
                    std::map<int32_t, Location> byIndex;
                    for (const auto& n : s.nodes) if (!n.marker && n.leaf && n.layer == 0)
                        byIndex[n.index] = {n.level, int(std::lround(n.cx * 4)), int(std::lround(n.cy * 4))};
                    std::map<Location, std::set<Location>> out;
                    for (const auto& n : s.nodes) if (!n.marker && n.leaf && n.layer == 0) {
                        auto& edges = out[byIndex.at(n.index)];
                        for (const auto index : n.neighbours) if (byIndex.contains(index)) edges.insert(byIndex.at(index));
                    }
                    return out;
                };
                const auto expectedGraph = graph(nav.sections[0]), actualGraph = graph(generated.section);
                size_t sameEdges = 0;
                for (const auto& [key, edges] : actualGraph) {
                    const auto it = expectedGraph.find(key);
                    sameEdges += it != expectedGraph.end() && it->second == edges;
                    if (details && it != expectedGraph.end() && it->second != edges) {
                        const auto& [level, x, y] = key;
                        std::printf("  neighbour mismatch: level=%d centre=%.2f,%.2f generated=%zu retail=%zu\n", level, x / 4.0f, y / 4.0f, edges.size(), it->second.size());
                    }
                }
                std::printf("ground neighbour sets: %zu equal / %zu generated / %zu retail leaves; source layers %u\n",
                    sameEdges, actualGraph.size(), expectedGraph.size(), nav.sections[0].layerCount);
                using Location = std::tuple<int, int, int>;
                std::map<Location, int32_t> retailRegion;
                for (const auto& n : nav.sections[0].nodes) if (!n.marker && n.leaf && n.layer == 0)
                    retailRegion[{n.level, int(std::lround(n.cx * 4)), int(std::lround(n.cy * 4))}] = n.region;
                std::map<int32_t, std::set<int32_t>> forward, reverse;
                size_t comparedRegions = 0, zeroMismatch = 0;
                for (const auto& n : generated.section.nodes) if (!n.marker && n.leaf) {
                    const auto it = retailRegion.find({n.level, int(std::lround(n.cx * 4)), int(std::lround(n.cy * 4))});
                    if (it == retailRegion.end()) continue;
                    ++comparedRegions; zeroMismatch += (n.region == 0) != (it->second == 0);
                    forward[n.region].insert(it->second); reverse[it->second].insert(n.region);
                }
                size_t partitionMismatch = zeroMismatch;
                for (const auto& [id, ids] : forward) partitionMismatch += ids.size() != 1;
                for (const auto& [id, ids] : reverse) partitionMismatch += ids.size() != 1;
                if (details) for (const auto& [id, ids] : reverse) if (ids.size() > 1) {
                    std::printf("  retail region %d split into generated regions:", id);
                    for (const auto i : ids) std::printf(" %d", i);
                    std::printf("\n");
                }
                std::printf("ground region partitions: %zu shared leaves, %zu inconsistencies (region IDs may be renumbered)\n", comparedRegions, partitionMismatch);
                std::printf("comparison only: no install writes; seed layers assumed ground; layer/quest selection and region/neighbour parity remain experimental\n");
            }
        }
        std::printf("precision %.3f  recall(near lines) %.3f\n", tp + fp ? double(tp) / double(tp + fp) : 0.0,
                    tp + fnNearLine ? double(tp) / double(tp + fnNearLine) : 0.0);
        return 0;
      } catch (const std::exception& e) {
        std::fprintf(stderr, "%s: %s\n", cmd.c_str(), e.what()); return 1;
      }
    }
    return std::nullopt;
}

}  // namespace albion::cli
