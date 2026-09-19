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

// forge CLI: STB terrain chunk diagnostics and bakes
std::optional<int> runChunks(const std::string& cmd, const Args& args) {
    if (cmd == "heights") {   // heights <map.lev> <x,y> [<x,y> ...]: bilinear LEV heights at map-local points (in-game harness oracle)
        if (args.size() < 3) { std::fprintf(stderr, "usage: forge heights <map.lev> <x,y> ...\n"); return 2; }
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
        if (args.size() < 3) { std::fprintf(stderr, "usage: forge recompress-chunk <in.bin> <out.bin>\n"); return 2; }
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
    if (cmd == "lod-check") {   // diagnostic: lod-check <map> [--install <root>]: compare our baked distant-LOD tiles with the retail inline textures
        if (args.size() < 2) { std::fprintf(stderr, "usage: forge lod-check <map> [--install <root>]\n"); return 2; }
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
        if (args.size() < 3) { std::fprintf(stderr, "usage: forge chunk-extract <map> <out.bin> [--install <root>]\n"); return 2; }
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
        if (!install.valid || target.empty()) { std::fprintf(stderr, "usage: forge chunk-audit <map>|--all [--install <root>]\n"); return 2; }
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
        if (args.size() < 2) { std::fprintf(stderr, "usage: forge chunk-textures <map> [--install <root>]\n"); return 2; }
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
        if (args.size() < 3) { std::fprintf(stderr, "usage: forge chunk-zcheck <map> <dz> [--install <root>]\n"); return 2; }
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
        if (args.size() < 4) { std::fprintf(stderr, "usage: forge chunk-relocate <map> <dx> <dy> [--install <root>]\n"); return 2; }
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
        if (args.size() < 3) { std::fprintf(stderr, "usage: forge chunk-dump <chunk.bin> <outdir>\n"); return 2; }
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
        if (args.size() < 6) { std::fprintf(stderr, "usage: forge bake-terrain <chunk.bin> <map.lev> <worldX> <worldY> <out.bin>\n"); return 2; }
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
    return std::nullopt;
}

}  // namespace albion::cli
