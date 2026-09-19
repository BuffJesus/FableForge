// forge — command-line front end for the FableForge modding core.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <numeric>
#include <set>
#include <string>
#include <vector>

#include "forge/bin.hpp"
#include "forge/themepalette.hpp"
#include "forge/bankcatalog.hpp"
#include "forge/catalog.hpp"
#include "forge/cutscene_script.hpp"
#include "forge/cutscene_verb_info.hpp"
#include "forge/cutscene_verbs.hpp"
#include "forge/big.hpp"
#include "forge/bspatch.hpp"
#include "forge/save.hpp"
#include "forge/textbig.hpp"
#include "forge/bunzip.hpp"
#include "forge/controls.hpp"
#include "forge/controls_enums.hpp"
#include "forge/gamedata.hpp"
#include "forge/questproject.hpp"
#include "forge/defdecode.hpp"
#include "forge/defedit.hpp"
#include "forge/defschema.hpp"
#include "forge/fse.hpp"
#include "forge/questnodes.hpp"
#include "forge/questdeploy.hpp"
#include "forge/questcard.hpp"
#include "forge/uidef.hpp"
#include "forge/lev.hpp"
#include "forge/terraintex.hpp"
#include "forge/thingplacer.hpp"
#include "forge/navmesh.hpp"
#include "forge/terrain.hpp"
#include "forge/foliage.hpp"
#include "forge/lzo.hpp"
#include "forge/meshpreview.hpp"
#include "forge/qst.hpp"
#include "forge/rangecodec.hpp"
#include "forge/stage.hpp"
#include "forge/stb.hpp"
#include "forge/stbbake.hpp"
#include "forge/stbheightbake.hpp"
#include "forge/stbinfo.hpp"
#include "forge/stbvalidate.hpp"
#include "forge/tng.hpp"
#include "forge/wad.hpp"
#include "forge/wld.hpp"
#include "forge/bwd.hpp"
#include "forge/worldworkspace.hpp"
#include "nlohmann/json.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#endif

using nlohmann::json;

namespace {

int usage() {
    std::puts(
        "forge-tools (the legacy FableForge CLI) -- quests, mods, saves, defs, scripts, UI, STB tooling; folded into forge over time\n"
        "\n"
        "usage:\n"
        "  forge wad list <file.wad>\n"
        "  forge wad extract <file.wad> <outdir> [filter]\n"
        "  forge wad metadata <file.wad> <filter>\n"
        "  forge wad append-native-batch <src.wad> <out.wad> <manifest.json>\n"
        "  forge wad repack <src.wad> <replace-dir> <out.wad>\n"
        "  forge wad diff <a.wad> <b.wad> [--json] [--deep]\n"
        "  forge tng list <file.tng>\n"
        "  forge tng roundtrip <file.tng>\n"
        "  forge tng transition-audit <levels-dir> <world.wld>\n"
        "  forge tng conflicts <base.tng> <mod.tng>... [--json]\n"
        "  forge tng merge <base.tng> <out.tng> <mod.tng>... [--json]\n"
        "  forge tng place <file.tng> --def <DEFINITION> [--out <file.tng>]\n"
        "      (--local X,Y | --world X,Y --map-origin MX,MY) [--lev <file.lev>|--z Z]\n"
        "      [--yaw DEG] [--text TXT_TAG] [--script-name N] [--script-data D]\n"
        "      [--scripted-hook] [--not-targetable] [--not-game-persistent]\n"
        "      [--not-level-persistent] [--no-health] [--type Object]\n"
        "      [--section NULL] [--uid N] [--json]\n"
        "  forge qst list <file.qst> [--json]\n"
        "  forge qst roundtrip <file.qst>\n"
        "  forge qst merge <base.qst> <out.qst> <mod.qst>... [--picks f] [--json]\n"
        "  forge wld info <file.wld> [maps|regions]\n"
        "  forge wld translate-prefix <in.wld> <out.wld> <level-prefix> <dx> <dy>\n"
        "  forge bwd info <file.bwd> [maps|regions]\n"
        "  forge bwd roundtrip <file.bwd>\n"
        "  forge wld compile <file.wld> <file_RT.stb> <out.bwd>\n"
        "  forge bwd set-region-name <in.bwd> <out.bwd> <oldName> <newName> [--minimap <g>]\n"
        "  forge bwd add-region <in.bwd> <out.bwd> <name> [<name>...]\n"
        "  forge bwd set-owner <in.bwd> <out.bwd> <levelName> --region <regionName>\n"
        "  forge bwd add-level <in.bwd> <out.bwd> <name> <left> <top> <right> "
        "<bottom> [--region <n>] [--def <d>] [--proximity] [--sea] "
        "[--host-region <n>] [--uid <n>]\n"
        "  forge world add-level <root> <name> <left> <top> <right> <bottom> "
        "[--region <n>] [--display <n>] [--def <d>] [--minimap <g>] "
        "[--proximity] [--sea] [--host-region <n>] [--uid <n>] [--no-backup]\n"
        "  forge world add-tiled-region <root> <stb-batch-manifest.json> <region-name>\n"
        "  forge world install-level <root> <name> <x> <y> --from-donor <Donor> "
        "[--lev <f>] [--tng <f>] [--chunk <f>] [--region <n>] [--display <n>]\n"
        "  forge world attach-map <root> <levelName> --region <hostRegion> "
        "[--no-sees] [--no-backup]\n"
        "  forge world inspect <game-root|data/Levels> [--json]\n"
        "  forge level create-from-donor <game-root|data/Levels> <donor.lev> <new.lev>\n"
        "       <map-x> <map-y> <out-project> [--script NAME] [--region NAME]\n"
        "       [--not-visible] [--allow-origin-collision] [--no-stb] [--with-wad] [--json]\n"
        "  forge lev info <file.lev>\n"
        "  forge lev themecheck <game-root> <level.lev>...\n"
        "  forge lev themerebase <game-root> <level.lev>...\n"
        "  forge lev themegrid <file.lev> [out.csv]\n"
        "  forge lev sculpt <src.lev> <out.lev> <raise|flatten|smooth> <x> <y> <radius> <amount> [target-height]\n"
        "  forge lev import-heightmap <src.lev> <out.lev> <raw-f32> <src-w> <src-h> <base> <scale>\n"
        "  forge lev import-world-heightmap <src.lev> <out.lev> <raw-f32> <src-w> <src-h> <spacing> [height-bias]\n"
        "  forge lev author-surface <src.lev> <out.lev> <low-theme> <mid-theme> <high-theme> [max-slope]\n"
        "  forge lev import-f2-preview-materials <src.lev> <out.lev> <manifest.json>\n"
        "      <tile-index> <material-theme-slots-csv> --accept-preview-heuristic\n"
        "      [--theme <slot> <ENGINE_THEME_name> <def-index>]...\n"
        "  forge lev paint-theme <src.lev> <out.lev> <theme-index|name> <x> <y> <radius> <opacity>\n"
        "  forge lev paint-walkable <src.lev> <out.lev> <x> <y> <radius> <0|1> [threshold]\n"
        "  forge lev paint-preferred <src.lev> <out.lev> <x> <y> <radius> <0|1> [threshold]\n"
        "  forge lev rebuild-nav <src.lev> <out.lev> [--tng <level.tng>]\n"
        "  forge lev stitch <target.lev> <target-x> <target-y> <neighbor.lev> <neighbor-x> <neighbor-y> <blend-width> <out.lev>\n"
        "  forge lev stitch-region <world.wld> <levels-root> <target-level-name> <target.lev> <blend-width> <out.lev>\n"
        "  forge stb list <file.stb>\n"
        "  forge stb diff <a.stb> <b.stb>\n"
        "  forge stb extract <file.stb> <outdir> [filter]\n"
        "  forge stb record <file.stb> <level-name> <out.bin>\n"
        "  forge stb append <src.stb> <out.stb> <level-name> <entry-name> <chunk> <common-record>\n"
        "  forge stb append-batch <src.stb> <out.stb> <manifest.json>\n"
        "  forge stb replace-static-map-batch <src.stb> <out.stb> <manifest.json>\n"
        "  forge stb replace-static-map-batch-relayout <src.stb> <out.stb> <manifest.json>\n"
        "  forge stb terrainrecord <info.bin> <chunk.bin> <common-record.bin>\n"
        "  forge stb refit-common-height-bounds <in.common.bin> <heightfield.lev> <out.common.bin>\n"
        "  forge stb relocate-common <in.common.bin> <out.common.bin> <world-x> <world-y>\n"
        "  forge stb replace <src.stb> <out.stb> <entry-name> <payload>\n"
        "  forge stb patchinfo <chunk.bin>\n"
        "  forge stb frame-extract <chunk.bin> <frame-index> <out.bin>\n"
        "  forge stb replace-static-map <src.stb> <out.stb> <level> <entry> <chunk> <common>\n"
        "  forge stb backgroundtreeinfo <chunk.bin> <root-offset|auto> [heightfield.lev [--validate-only|static-quality [world.wld levels-root level-name]]]\n"
        "  forge stb create-background <heightfield.lev> <inline-texture.bin> <world-x> <world-y> <out-prefix>\n"
        "  forge stb create-solid-inline <out.bin> <rgb565>\n"
        "  forge stb create-terrain <heightfield.lev> <background-inline.bin> <world-x> <world-y> <bank-index>\n"
        "      <tex0> <tex1> <tex2> <out.chunk> <out.info> [--slope-tex <id>]\n"
        "      [--height-tex <id>] [--theme-material SLOT B0 B1 B2 C0 C1 C2]...\n"
        "      [--foliage] [--mesh-bank <graphics.big>]\n"
        "      [--no-foliage-subsections]\n"
        "       <foreground-base-symbol> <foreground-bump-symbol> <foreground-cliff-symbol> <out-chunk> <out-info>\n"
        "  forge stb foregroundinfo <chunk.bin> [--vertices <out.tsv>] [--triangles <out.tsv>] [--indices <out.tsv>] [--verify-topology] [--verify-roundtrip] [--verify-base-coverage]\n"
        "  forge texture import <src.big> <out.big> <entry> <image.png> [--add]\n"
        "       [--bank GBANK_MAIN_PC] [--format dxt1|dxt3|argb8888] [--dims WxH]\n"
        "       [--tools <FableTLC/tools>] [--python <exe>]\n"
        "  forge texture verify <big> <entry>\n"
        "  forge texture verify-bank <big> [--bank NAME] [--filter TEXT] [--limit N]\n"
        "  forge texture free-slots <game-root> <textures.big> [--schema <s>]\n"
        "       [--prefix UNASSIGNED_] [--json]\n"
        "  forge terrain themes <level.lev> --root <game-root> [--schema <s>]\n"
        "       [--textures <textures.big>] [--json]\n"
        "  forge terrain split-world-heightmap <raw-f32> <src-w> <src-h> <spacing> <height-bias>\n"
        "       <tile-widths-csv> <tile-heights-csv> <out-dir>\n"
        "  forge terrain verify-tiled-levels <tile-plan.json> <levels-dir> <name-prefix>\n"
        "  forge stb settex <chunk.bin> <out.bin> --map OLD:NEW [--map ...]\n"
        "  forge stb bake-heightfield <chunk.bin> <heightfield.lev> <world-x> <world-y> <out.bin>\n"
        "       [--neighbor <neighbor.lev> <world-x> <world-y>]...\n"
        "       [--world <world.wld> --levels-root <dir> --level <level-name>]\n"
        "       [--rebuild-direction-mask]\n"
        "       [--rebuild-topology --theme-material SLOT B0 B1 B2 C0 C1 C2]...\n"
        "       [--themes-from-defs <game-root> [--theme-schema <def_schema.json>]]\n"
        "           (resolves every theme material from the LEV ground-theme\n"
        "            palette via the ENGINE_THEME defs; --theme-material wins)\n"
        "  forge foliage palette [--json]\n"
        "  forge mesh audit <graphics.big> [--json]\n"
        "  forge scene audit-all <game-root> <def-schema.json> <tng-root>\n"
        "  forge foliage read <common-record.bin> [--offset <n>] [--json]\n"
        "  forge foliage meshinfo <graphics.big> [--json]\n"
        "  forge foliage instances <chunk.lev> [xlo xhi ylo yhi] [--json]\n"
        "  forge catalog info <catalog.json>\n"
        "  forge bank-catalog <UnifiedFable-dir|banks.ini> [symbol-filter] [--json]\n"
        "  forge fse list <manifest.json> [filter] [--json]\n"
        "  forge fse show <manifest.json> <function> [--json]\n"
        "  forge quest nodes [filter] [--json] [--manifest <fse-manifest.json>]\n"
        "  forge quest compile <graph.json> [--out <script.lua>] [--manifest <fse-manifest.json>]\n"
        "  forge quest deploy <graph.json> <game-root> [--dry-run] [--active|--dormant] [--id N] [--manifest <fse-manifest.json>]\n"
        "  forge quest doctor <game-root> [--json]\n"
        "  forge quest card <game-root> <schema.json> <NAME> (--donor <OBJECT_QUEST_CARD_*> | --from-scratch)\n"
        "       [--out <out-root>|--in-place] [--overwrite-donor] [--gold N] [--renown N] [--boasts N]\n"
        "       [--bank-catalog <UnifiedFable-dir>] [--quest-name N|TEXT_*]\n"
        "       [--quest-summary N|TEXT_*] [--quest-objective N|TEXT_*]\n"
        "       [--success-summary N|TEXT_*] [--epilogue N] [--core 0|1] [--vignette 0|1]\n"
        "       [--title-text TEXT] [--summary-text TEXT] [--objective-text TEXT]\n"
        "       [--success-text TEXT] [--text-donor TEXT_*]\n"
        "       [--exclusive 0|1] [--can-cancel 0|1] [--json]\n"
        "       [--emit-quest|--no-emit-quest] [--quest-script-name NAME]\n"
        "       [--region NAME] [--quest-id N]   (companion FSE quest; on by default for --from-scratch)\n"
        "  forge defs list <game-root> [bin] [filter]\n"
        "  forge defs show <game-root> <name-or-index> [bin]\n"
        "  forge defs decode <game-root> <schema.json> <name-or-index> [bin] [--json]\n"
        "  forge defs decode <game-root> <schema.json> --all [bin]\n"
        "  forge defs families <game-root>\n"
        "  forge defs schema <schema.json> [def-type] [--json]\n"
        "  forge ui set-graphic <game-root> <entryName> <textureId> [--state N]\n"
        "       [--schema <schema.json>] [--out <out-root>|--in-place]\n"
        "  forge ui add-sprite <game-root> <newName> --from <srcEntry> --graphic <id>\n"
        "       [--state N] [--schema <schema.json>] [--out <out-root>|--in-place]\n"
        "  forge ui set-cardmodel <game-root> <objectName> <meshId>\n"
        "       [--schema <schema.json>] [--out <out-root>|--in-place]\n"
        "  forge ui clone-object-model <game-root> <donorObject> <newObject> <meshId>\n"
        "       [--schema <schema.json>] --out <out-root>\n"
        "  forge defs set-field <bin> [--names <names.bin>] <entryName> <field> <value>\n"
        "       [--schema <schema.json>] [--out <out.bin>] [--json]\n"
        "  forge defs roundtrip <game-root> [bin]\n"
        "  forge defs diff <root-a> <root-b> [bin] [--json]\n"
        "  forge defs merge <base-root> <out-dir> <bin> <mod-root>... [--fields schema.json] [--picks f] [--json]\n"
        "  forge text show <text.big> <name-or-id> [--json]\n"
        "  forge text list <text.big> [name-filter] [--json]\n"
        "  forge text set <text.big> <TEXT_NAME> <content> --out <text.big>|--in-place\n"
        "       [--id N] [--donor TEXT_*] [--speaker NAME] [--speech-bank FILE] [--json]\n"
        "  forge text import <text.big> <manifest.json> --out <text.big>|--in-place\n"
        "       [--donor TEXT_*] [--json]\n"
        "  forge big list <file.big> [bank-filter] [--json]\n"
        "  forge big extract <file.big> <out-dir> [bank-filter]\n"
        "  forge save read <FableSave-file> [--json]\n"
        "  forge fmp list <file.fmp> [--json]\n"
        "  forge fmp apply <base-root> <file.fmp> <out-root>\n"
        "  forge fmp extract <file.fmp> <out-dir> [bank-filter]\n"
        "  forge fmp export <base-root> <modded-root> <out.fmp>\n"
        "  forge mods merge <base-root> <out-dir> --with <src>... [--fields schema.json] [--stage] [--json]\n"
        "  forge patch info <file.patch>\n"
        "  forge patch apply <old-file> <file.patch> <out-file>\n"
        "  forge mods analyze <base-root> <mod-root>... [--json]\n"
        "  forge script refs <game-root> [level-filter] [--json]\n"
        "  forge script cutscenes <game-root> [filter] [--json]\n"
        "  forge script cutscene <game-root> <name-or-index> [--json]\n"
        "  forge script command-stats <game-root> [--json]\n"
        "  forge script verbs [filter] [--json]\n"
        "  forge script validate <game-root> [filter] [--json]\n"
        "  forge script fixup <game-root> [--write]\n"
        "  forge chest list <game-root> [level-filter]\n"
        "  forge gamedata <game-root> [--wld <finalalbion.wld>] [--full]\n"
        "  forge quest master <finalalbion.qst> [--global name:type[:default]]...\n"
        "      [--id N] [--lua-out <FSE_Master.lua>] [--out <qst>]\n"
        "  forge controls list <bin> [--names <names.bin>]\n"
        "  forge controls set-binding <bin> <entry> <action> <pad|key|mouse> <value>\n"
        "      [--occurrence N] [--names <names.bin>] [--out <bin>]\n"
        "  forge stage <game-root> <mod-dir>\n"
        "  forge unstage <game-root>\n"
        "  forge validate <game-root>\n");
    return 2;
}

// CLandscapeLayerMesh::LoadForeground consumes a different vertex grammar from
// the 16-byte composed/background PatchVertex handled by stbbake::parsePatchBody.
// A foreground frame is [u16 layerCount], followed by layer headers and raw
// 15-byte vertices, then [u8 hasWater].  Keep this small recognizer strict so a
// background frame cannot be mistaken for editable foreground geometry.
struct ForegroundDiskFrame {
    std::vector<size_t> vertexOffsets;
    uint16_t minX = 0xffff, minY = 0xffff, maxX = 0, maxY = 0;
};

bool parseForegroundDiskFrame(const std::vector<uint8_t>& body,
                              ForegroundDiskFrame& out) {
    out = ForegroundDiskFrame{};
    size_t pos = 0;
    auto read16 = [&](uint16_t& value) -> bool {
        if (pos + 2 > body.size()) return false;
        value = uint16_t(body[pos]) | (uint16_t(body[pos + 1]) << 8);
        pos += 2;
        return true;
    };
    auto skip = [&](size_t count) -> bool {
        if (pos + count > body.size()) return false;
        pos += count;
        return true;
    };

    uint16_t layerCount = 0;
    if (!read16(layerCount) || layerCount == 0 || layerCount > 64) return false;
    for (uint16_t layer = 0; layer < layerCount; ++layer) {
        uint16_t vertexCount = 0, polyCount = 0;
        if (!read16(vertexCount) || !read16(polyCount) || pos >= body.size()) return false;
        const uint8_t mappingDirection = body[pos++];
        if (vertexCount == 0 || vertexCount > 4096 || mappingDirection > 4) return false;
        if (!skip(12) || pos >= body.size()) return false; // three texture indices
        const bool sharedIndexBuffer = body[pos++] != 0;
        if (!skip(12)) return false; // two mip levels + self illumination
        const size_t vertexBytes = size_t(vertexCount) * 15;
        if (pos + vertexBytes > body.size()) return false;
        for (uint16_t vertex = 0; vertex < vertexCount; ++vertex) {
            const size_t offset = pos + size_t(vertex) * 15;
            const uint16_t x = uint16_t(body[offset]) |
                               (uint16_t(body[offset + 1]) << 8);
            const uint16_t y = uint16_t(body[offset + 2]) |
                               (uint16_t(body[offset + 3]) << 8);
            out.vertexOffsets.push_back(offset);
            out.minX = std::min(out.minX, x); out.maxX = std::max(out.maxX, x);
            out.minY = std::min(out.minY, y); out.maxY = std::max(out.maxY, y);
        }
        pos += vertexBytes;
        if (!sharedIndexBuffer && !skip((size_t(polyCount) + 2) * 2)) return false;
    }
    // Water payload parsing is deliberately excluded: the four terrain frames
    // under authoring are non-water and must consume their body exactly.
    if (pos >= body.size() || body[pos++] != 0 || pos != body.size()) return false;
    return !out.vertexOffsets.empty();
}

int wadList(const std::string& path) {
    const auto archive = forge::wad::Archive::open(path);
    std::printf("%s: %zu entries\n", path.c_str(), archive.entries().size());
    for (const auto& entry : archive.entries()) {
        std::printf("%6u  type=%u  %10u bytes  %s\n",
                    entry.id, entry.type, entry.size, entry.name.c_str());
    }
    return 0;
}

int wadExtract(const std::string& path, const std::string& outDir,
               const std::string& filter) {
    const auto archive = forge::wad::Archive::open(path);
    const size_t written = archive.extract(outDir, filter);
    std::printf("extracted %zu of %zu entries to %s\n",
                written, archive.entries().size(), outDir.c_str());
    return 0;
}

std::string lowered(std::string value);  // defined below

// Record-level diff of two WADs (e.g. a modded FinalAlbion.wad vs vanilla) — the
// WAD analogue of `defs diff`, for analyzing overhaul mods that repackage level
// archives. Entries are keyed by archive name; a change is a differing size or
// stored crc (fast, no payload reads). With --deep, changed candidates are
// confirmed by comparing payload bytes.
int wadDiff(const std::string& pathA, const std::string& pathB, bool jsonOutput,
            bool deep) {
    const auto a = forge::wad::Archive::open(pathA);
    const auto b = forge::wad::Archive::open(pathB);

    std::map<std::string, const forge::wad::Entry*> byNameA, byNameB;
    for (const auto& e : a.entries()) byNameA[lowered(e.name)] = &e;
    for (const auto& e : b.entries()) byNameB[lowered(e.name)] = &e;

    std::vector<std::string> added, removed, changed;
    for (const auto& [key, eb] : byNameB) {
        auto it = byNameA.find(key);
        if (it == byNameA.end()) {
            added.push_back(eb->name);
            continue;
        }
        const auto* ea = it->second;
        bool differs = ea->size != eb->size || ea->crc != eb->crc;
        if (deep && !differs) differs = a.read(*ea) != b.read(*eb);
        if (differs) changed.push_back(eb->name);
    }
    for (const auto& [key, ea] : byNameA) {
        if (byNameB.find(key) == byNameB.end()) removed.push_back(ea->name);
    }
    std::sort(added.begin(), added.end());
    std::sort(removed.begin(), removed.end());
    std::sort(changed.begin(), changed.end());

    if (jsonOutput) {
        std::puts(json{{"added", added},
                       {"removed", removed},
                       {"changed", changed},
                       {"summary",
                        {{"entries_a", a.entries().size()},
                         {"entries_b", b.entries().size()},
                         {"added", added.size()},
                         {"removed", removed.size()},
                         {"changed", changed.size()}}}}
                      .dump(2)
                      .c_str());
        return 0;
    }

    const auto dump = [](const char* label, const std::vector<std::string>& v) {
        std::printf("%s: %zu\n", label, v.size());
        for (const auto& n : v) std::printf("  %s\n", n.c_str());
    };
    std::printf("wad diff: A=%zu entries, B=%zu entries\n", a.entries().size(),
                b.entries().size());
    dump("added (in B, not A)", added);
    dump("removed (in A, not B)", removed);
    dump("changed (size/crc differ)", changed);
    std::printf("\nchange set: %zu added, %zu removed, %zu changed\n",
                added.size(), removed.size(), changed.size());
    return 0;
}

// Repack a WAD, replacing entries from files under replaceDir laid out with
// archive-relative paths (the same layout `forge wad extract` produces).
int wadRepack(const std::string& srcPath, const std::string& replaceDir,
              const std::string& outPath) {
    namespace fs = std::filesystem;
    std::map<std::string, std::vector<uint8_t>> replacements;
    for (const auto& item : fs::recursive_directory_iterator(replaceDir)) {
        if (!item.is_regular_file()) continue;
        std::string relative =
            fs::relative(item.path(), replaceDir).generic_string();
        std::replace(relative.begin(), relative.end(), '/', '\\');

        std::ifstream in(item.path(), std::ios::binary);
        std::vector<uint8_t> payload((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
        replacements.emplace(std::move(relative), std::move(payload));
    }
    const size_t replaced = forge::wad::repack(srcPath, replacements, outPath);
    std::printf("repacked %s -> %s, %zu of %zu entries replaced\n",
                srcPath.c_str(), outPath.c_str(), replaced, replacements.size());
    return 0;
}

int stageApply(const std::string& gameRoot, const std::string& modDir) {
    const auto result = forge::stage::apply(gameRoot, modDir);
    for (const auto& path : result.staged) {
        std::printf("  staged: %s\n", path.c_str());
    }
    std::printf("staged %zu files (%zu originals backed up); manifest: %s\n",
                result.staged.size(), result.backedUp.size(),
                forge::stage::manifestPath(gameRoot).string().c_str());
    return 0;
}

int stageRevert(const std::string& gameRoot) {
    const auto result = forge::stage::revert(gameRoot);
    for (const auto& path : result.restored) {
        std::printf("  restored: %s\n", path.c_str());
    }
    for (const auto& path : result.removed) {
        std::printf("  removed: %s\n", path.c_str());
    }
    std::printf("unstaged: %zu restored, %zu removed\n", result.restored.size(),
                result.removed.size());
    return 0;
}

int stbList(const std::string& path) {
    const auto archive = forge::stb::Archive::open(path);
    std::printf("%s: %zu entries, %zu static maps, alignment %u, table @ %u\n",
                path.c_str(), archive.entries().size(), archive.staticMaps().size(),
                archive.alignment(), archive.tableOffset());
    for (const auto& entry : archive.entries()) {
        std::printf("%6u  type=%u  %10u bytes  @%-10u  %s\n",
                    entry.id, entry.type, entry.size, entry.offset, entry.name.c_str());
    }
    return 0;
}

int stbExtract(const std::string& path, const std::string& outDir,
               const std::string& filter) {
    const auto archive = forge::stb::Archive::open(path);
    const size_t written = archive.extract(outDir, filter);
    std::printf("extracted %zu of %zu entries to %s\n",
                written, archive.entries().size(), outDir.c_str());
    return 0;
}

std::vector<uint8_t> readAllBytes(const std::string& path); // defined below
void writeAllBytes(const std::string& path, const std::vector<uint8_t>& data);

int wadAppendNativeBatch(const std::string& src, const std::string& out,
                         const std::string& manifestPath) {
    std::ifstream input(manifestPath);
    if (!input) throw std::runtime_error("wad append-native-batch: cannot open manifest");
    nlohmann::json manifest;
    input >> manifest;
    if (!manifest.contains("entries") || !manifest["entries"].is_array() ||
        manifest["entries"].empty())
        throw std::runtime_error(
            "wad append-native-batch: manifest.entries must be a non-empty array");
    const std::filesystem::path base =
        std::filesystem::path(manifestPath).parent_path();
    std::vector<forge::wad::NativeEntry> entries;
    for (const auto& item : manifest["entries"]) {
        if (!item.contains("name") || !item["name"].is_string() ||
            !item.contains("payload") || !item["payload"].is_string())
            throw std::runtime_error(
                "wad append-native-batch: each entry requires string name/payload");
        std::filesystem::path payload = item["payload"].get<std::string>();
        if (payload.is_relative()) payload = base / payload;
        entries.push_back({item["name"].get<std::string>(),
                           readAllBytes(payload.string())});
    }
    forge::wad::appendNativeEntries(src, entries, out);
    const auto verify = forge::wad::Archive::open(out);
    for (const auto& expected : entries) {
        const auto found = std::find_if(verify.entries().begin(), verify.entries().end(),
            [&](const forge::wad::Entry& entry) { return entry.name == expected.name; });
        if (found == verify.entries().end() || verify.read(*found) != expected.payload ||
            found->info.size() != 88)
            throw std::runtime_error(
                "wad append-native-batch: post-write verification failed: " + expected.name);
    }
    std::printf("appended %zu donor-free WAD entries; verified payloads -> %s\n",
                entries.size(), out.c_str());
    return 0;
}

int stbDiff(const std::string& pathA, const std::string& pathB) {
    const auto a = forge::stb::Archive::open(pathA);
    const auto b = forge::stb::Archive::open(pathB);
    std::map<std::string, const forge::stb::Entry*> entriesA;
    std::map<std::string, const forge::stb::Entry*> entriesB;
    for (const auto& entry : a.entries()) entriesA[entry.name] = &entry;
    for (const auto& entry : b.entries()) entriesB[entry.name] = &entry;

    size_t unchanged = 0, changed = 0, added = 0, removed = 0;
    for (const auto& [name, entryA] : entriesA) {
        const auto found = entriesB.find(name);
        if (found == entriesB.end()) {
            ++removed;
            std::printf("REMOVED\t%s\n", name.c_str());
            continue;
        }
        const auto* entryB = found->second;
        if (entryA->size == entryB->size && a.read(*entryA) == b.read(*entryB)) {
            ++unchanged;
        } else {
            ++changed;
            std::printf("CHANGED\t%s\t%u\t%u\n", name.c_str(),
                        entryA->size, entryB->size);
        }
    }
    for (const auto& [name, entryB] : entriesB) {
        (void)entryB;
        if (entriesA.find(name) == entriesA.end()) {
            ++added;
            std::printf("ADDED\t%s\n", name.c_str());
        }
    }
    std::printf("stb diff: unchanged=%zu changed=%zu added=%zu removed=%zu\n",
                unchanged, changed, added, removed);
    return 0;
}

int stbAppend(const std::string& src, const std::string& out,
              const std::string& levelName, const std::string& entryName,
              const std::string& chunkPath, const std::string& infoPath) {
    const auto chunk = readAllBytes(chunkPath);
    const auto info = readAllBytes(infoPath);
    forge::stb::appendStaticMap(src, out, levelName, entryName, chunk, info);
    const auto result = forge::stb::Archive::open(out);
    const auto* entry = result.findEntry(entryName);
    std::printf("appended %s: id=%u, %zu bytes, offset=%u; static maps=%zu -> %s\n",
                levelName.c_str(), entry ? entry->id : 0, chunk.size(),
                entry ? entry->offset : 0, result.staticMaps().size(), out.c_str());
    return 0;
}

int stbRecord(const std::string& path, const std::string& levelName,
              const std::string& outPath) {
    const auto archive = forge::stb::Archive::open(path);
    const forge::stb::StaticMap* found = nullptr;
    for (const auto& map : archive.staticMaps()) {
        if (map.levelName == levelName) { found = &map; break; }
    }
    if (!found) throw std::runtime_error("stb: static map not found: " + levelName);
    const auto record = archive.readStaticMapRecord(*found);
    std::ofstream out(outPath, std::ios::binary);
    if (!out) throw std::runtime_error("stb: cannot write " + outPath);
    out.write(reinterpret_cast<const char*>(record.data()),
              static_cast<std::streamsize>(record.size()));
    std::printf("wrote %zu-byte common record for %s -> %s\n",
                record.size(), levelName.c_str(), outPath.c_str());
    return 0;
}

int stbTerrainRecord(const std::string& infoPath, const std::string& chunkPath,
                     const std::string& outPath, bool includeLocalDetail,
                     bool includeLandscape) {
    const auto bytes = readAllBytes(infoPath);
    if (bytes.size() != forge::stbinfo::kInfoBlockSize)
        throw std::runtime_error("stb terrainrecord: input must be exactly 0x5c bytes");
    const auto info = forge::stbinfo::readInfoBlock(bytes.data());
    const auto record = forge::stbbake::buildTerrainCommonRecord(
        info, readAllBytes(chunkPath), includeLocalDetail, includeLandscape);
    writeAllBytes(outPath, record);
    std::printf("created canonical 0x%zx-byte terrain common record -> %s\n",
                record.size(), outPath.c_str());
    return 0;
}

std::vector<uint8_t> readAllBytes(const std::string& path); // defined below

const char* sevStr(forge::stbvalidate::Severity s) {
    using S = forge::stbvalidate::Severity;
    return s == S::Pass ? "PASS" : s == S::Warn ? "WARN" : "FAIL";
}

// forge stbvalidate <chunk.bin> [--reference <donor.bin>]
//                    [--ptrs land,local,ehptr,ehsize,cbptr,cbsize] [--texcheck]
int stbValidate(const std::vector<std::string>& args) {
    std::string chunkPath, refPath;
    forge::stbvalidate::InfoBlockPtrs ib;
    bool texcheck = false;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--reference" && i + 1 < args.size()) refPath = args[++i];
        else if (args[i] == "--texcheck") texcheck = true;
        else if (args[i] == "--ptrs" && i + 1 < args.size()) {
            std::string v = args[++i];
            uint32_t* fields[6] = {&ib.landscapeMapPtr, &ib.localDetailMapPtr,
                                   &ib.edgeHeightFilePtr, &ib.edgeHeightFileSize,
                                   &ib.checksumBlockFilePtr, &ib.checksumBlockFileSize};
            size_t k = 0, start = 0;
            while (k < 6 && start <= v.size()) {
                size_t comma = v.find(',', start);
                std::string tok = v.substr(start, comma - start);
                if (!tok.empty()) *fields[k] = uint32_t(std::stoul(tok, nullptr, 0));
                ++k;
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
        } else if (chunkPath.empty()) chunkPath = args[i];
    }
    if (chunkPath.empty()) {
        std::fprintf(stderr, "usage: forge stbvalidate <chunk.bin> "
                             "[--reference <donor.bin>] [--ptrs a,b,...] [--texcheck]\n");
        return 2;
    }
    auto chunkBytes = readAllBytes(chunkPath);
    auto chunk = forge::stbbake::parseChunk(chunkBytes);
    std::printf("chunk %s : %zu bytes, %zu segments, %zu frames "
                "(frame=%zu hdr=%zu pad=%zu)\n",
                chunkPath.c_str(), chunkBytes.size(), chunk.segments.size(),
                chunk.frameIndices.size(), chunk.frameBytes(), chunk.hdrBytes(),
                chunk.padBytes());

    bool allOk = true;
    auto printReport = [&](const std::vector<forge::stbvalidate::Check>& checks) {
        for (const auto& c : checks) {
            std::printf("  %-4s  %-26s  %s\n", sevStr(c.sev), c.name.c_str(),
                        c.detail.c_str());
            if (c.sev == forge::stbvalidate::Severity::Fail) allOk = false;
        }
    };

    std::printf("--- L1 structural acceptance ---\n");
    printReport(forge::stbvalidate::validateChunk(chunk, ib).checks);

    std::optional<forge::stbbake::Chunk> ref;
    if (!refPath.empty()) {
        ref = forge::stbbake::parseChunk(readAllBytes(refPath));
        std::printf("--- L2 oracle diff vs %s ---\n", refPath.c_str());
        printReport(forge::stbvalidate::diffAgainstReference(chunk, *ref).checks);
    }
    if (texcheck) {
        std::printf("--- L3 texture-resolve / isolation ---\n");
        printReport(forge::stbvalidate::validateTextureResolve(
                        chunk, ref ? &*ref : nullptr).checks);
    }
    std::printf("RESULT: %s\n", allOk ? "OK" : "FAIL");
    return allOk ? 0 : 1;
}

// forge stbretarget <donor-relocated.bin> [--out <out.bin>]
int stbRetarget(const std::vector<std::string>& args) {
    std::string in, out;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--out" && i + 1 < args.size()) out = args[++i];
        else if (in.empty()) in = args[i];
    }
    if (in.empty()) {
        std::fprintf(stderr, "usage: forge stbretarget <donor-relocated.bin> [--out <out.bin>]\n");
        return 2;
    }
    auto r = forge::stbbake::retargetChunk(readAllBytes(in));
    for (const auto& note : r.notes) std::printf("  %s\n", note.c_str());
    std::printf("consistent: %s\n", r.consistent ? "yes" : "NO");
    if (!out.empty()) {
        std::ofstream o(out, std::ios::binary);
        o.write(reinterpret_cast<const char*>(r.chunk.data()), r.chunk.size());
        std::printf("wrote %zu bytes to %s\n", r.chunk.size(), out.c_str());
    }
    return r.consistent ? 0 : 1;
}

int tngList(const std::string& path) {
    const auto file = forge::tng::File::parse(path);
    std::printf("%s: %zu things\n", file.source().c_str(), file.things().size());
    for (const auto& thing : file.things()) {
        std::printf("%-12s %-40s %s\n", thing.type.c_str(),
                    thing.scriptName().empty() ? "-" : thing.scriptName().c_str(),
                    thing.definitionType().c_str());
    }
    return 0;
}

int tngRoundtrip(const std::string& path) {
    std::FILE* raw = std::fopen(path.c_str(), "rb");
    if (raw == nullptr) {
        std::fprintf(stderr, "cannot open %s\n", path.c_str());
        return 1;
    }
    std::string original;
    char buffer[65536];
    size_t got;
    while ((got = std::fread(buffer, 1, sizeof(buffer), raw)) > 0) {
        original.append(buffer, got);
    }
    std::fclose(raw);

    const auto file = forge::tng::File::parseText(original, path);
    const std::string serialized = file.serialize();
    if (serialized == original) {
        std::printf("roundtrip OK: %zu bytes, %zu things\n",
                    original.size(), file.things().size());
        return 0;
    }

    size_t firstDiff = 0;
    while (firstDiff < original.size() && firstDiff < serialized.size() &&
           original[firstDiff] == serialized[firstDiff]) {
        ++firstDiff;
    }
    std::fprintf(stderr,
                 "roundtrip FAILED: input %zu bytes, output %zu bytes, "
                 "first difference at byte %zu\n",
                 original.size(), serialized.size(), firstDiff);
    return 1;
}

// `forge tng place` — append one authored Thing to a level TNG.
//
// TNGs carry no header counters, so a placement is purely the new
// NewThing..EndThing block; every other byte of the input round-trips
// unchanged. Positions are MAP-LOCAL (world minus the .wld MapX/MapY); pass
// --lev to snap PositionZ to the bilinear terrain height.
int tngPlace(const std::vector<std::string>& args) {
    namespace tp = forge::thingplacer;

    auto need = [&](size_t i) -> const std::string& {
        if (i >= args.size()) throw std::runtime_error("tng place: missing value");
        return args[i];
    };
    auto parsePair = [](const std::string& text, float& a, float& b) {
        const size_t comma = text.find(',');
        if (comma == std::string::npos) {
            throw std::runtime_error("tng place: expected X,Y not " + text);
        }
        a = std::stof(text.substr(0, comma));
        b = std::stof(text.substr(comma + 1));
    };

    std::string inPath = args.empty() ? std::string() : args[0];
    std::string outPath;
    std::string levPath;
    tp::Placement placement;
    bool haveLocal = false;
    bool haveWorld = false;
    bool haveZ = false;
    bool asJson = false;
    float worldX = 0.0f, worldY = 0.0f;
    float originX = 0.0f, originY = 0.0f;
    bool haveOrigin = false;
    float yaw = 0.0f;

    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--json") { asJson = true; continue; }
        if (a == "--scripted-hook") { placement.scriptedHook = true; continue; }
        if (a == "--not-targetable") { placement.targetable = false; continue; }
        if (a == "--not-game-persistent") {
            placement.thingGamePersistent = false;
            continue;
        }
        if (a == "--not-level-persistent") {
            placement.thingLevelPersistent = false;
            continue;
        }
        if (a == "--no-health") { placement.health.reset(); continue; }
        if (a == "--def") { placement.definitionType = need(++i); continue; }
        if (a == "--out") { outPath = need(++i); continue; }
        if (a == "--lev") { levPath = need(++i); continue; }
        if (a == "--type") { placement.thingType = need(++i); continue; }
        if (a == "--section") { placement.section = need(++i); continue; }
        if (a == "--text") { placement.gameTextDefName = need(++i); continue; }
        if (a == "--script-name") { placement.scriptName = need(++i); continue; }
        if (a == "--script-data") { placement.scriptData = need(++i); continue; }
        if (a == "--player") { placement.player = std::stoi(need(++i)); continue; }
        if (a == "--health") { placement.health = std::stof(need(++i)); continue; }
        if (a == "--uid") { placement.uid = std::stoull(need(++i)); continue; }
        if (a == "--yaw") { yaw = std::stof(need(++i)); continue; }
        if (a == "--z") {
            placement.position.z = std::stof(need(++i));
            haveZ = true;
            continue;
        }
        if (a == "--local") {
            parsePair(need(++i), placement.position.x, placement.position.y);
            haveLocal = true;
            continue;
        }
        if (a == "--world") {
            parsePair(need(++i), worldX, worldY);
            haveWorld = true;
            continue;
        }
        if (a == "--map-origin") {
            parsePair(need(++i), originX, originY);
            haveOrigin = true;
            continue;
        }
        std::fprintf(stderr, "tng place: unknown option %s\n", a.c_str());
        return 2;
    }

    if (inPath.empty() || placement.definitionType.empty()) {
        std::fprintf(stderr,
                     "tng place: <file.tng> and --def <DEFINITION> are required\n");
        return 2;
    }
    if (haveWorld) {
        if (!haveOrigin) {
            std::fprintf(stderr,
                         "tng place: --world needs --map-origin MX,MY (this "
                         "level's MapX/MapY from the .wld)\n");
            return 2;
        }
        const tp::Vec3 local = tp::worldToLocal(
            tp::Vec3{worldX, worldY, 0.0f}, static_cast<int>(originX),
            static_cast<int>(originY));
        placement.position.x = local.x;
        placement.position.y = local.y;
        haveLocal = true;
    }
    if (!haveLocal) {
        std::fprintf(stderr,
                     "tng place: give --local X,Y or --world X,Y --map-origin MX,MY\n");
        return 2;
    }
    placement.forward = tp::forwardFromYawDegrees(yaw);
    placement.up = tp::Vec3{0.0f, 0.0f, tp::kGroundUpZ};

    float terrainZ = 0.0f;
    bool snapped = false;
    if (!levPath.empty()) {
        const auto level = forge::lev::File::open(levPath);
        terrainZ = tp::terrainHeightAt(level, placement.position.x,
                                       placement.position.y);
        if (!haveZ) {
            placement.position.z = terrainZ;
            snapped = true;
        }
    } else if (!haveZ) {
        std::fprintf(stderr, "tng place: give --lev <file.lev> or --z <height>\n");
        return 2;
    }

    auto file = forge::tng::File::parse(inPath);
    const size_t before = file.things().size();
    const tp::PlaceResult result = tp::place(file, placement);

    const std::string outText = file.serialize();
    const std::string target = outPath.empty() ? inPath : outPath;
    {
        std::ofstream out(target, std::ios::binary);
        if (!out) {
            std::fprintf(stderr, "tng place: cannot write %s\n", target.c_str());
            return 1;
        }
        out.write(outText.data(), static_cast<std::streamsize>(outText.size()));
    }

    if (asJson) {
        std::printf("{\n");
        std::printf("  \"output\": \"%s\",\n", target.c_str());
        std::printf("  \"uid\": %llu,\n",
                    static_cast<unsigned long long>(result.uid));
        std::printf("  \"thingIndex\": %zu,\n", result.thingIndex);
        std::printf("  \"thingsBefore\": %zu,\n", before);
        std::printf("  \"thingsAfter\": %zu,\n", file.things().size());
        std::printf("  \"definitionType\": \"%s\",\n",
                    placement.definitionType.c_str());
        std::printf("  \"section\": \"%s\",\n", placement.section.c_str());
        std::printf("  \"local\": [%s, %s, %s],\n",
                    tp::formatFloat(placement.position.x).c_str(),
                    tp::formatFloat(placement.position.y).c_str(),
                    tp::formatFloat(placement.position.z).c_str());
        std::printf("  \"terrainSnapped\": %s,\n", snapped ? "true" : "false");
        std::printf("  \"bytes\": %zu\n", outText.size());
        std::printf("}\n");
    } else {
        std::printf("placed %s in section %s of %s\n",
                    placement.definitionType.c_str(), placement.section.c_str(),
                    target.c_str());
        std::printf("  UID %llu (thing %zu of %zu)\n",
                    static_cast<unsigned long long>(result.uid),
                    result.thingIndex + 1, file.things().size());
        std::printf("  local position %s, %s, %s%s\n",
                    tp::formatFloat(placement.position.x).c_str(),
                    tp::formatFloat(placement.position.y).c_str(),
                    tp::formatFloat(placement.position.z).c_str(),
                    snapped ? " (snapped to terrain)" : "");
        if (!levPath.empty() && !snapped) {
            std::printf("  terrain height here is %s\n",
                        tp::formatFloat(terrainZ).c_str());
        }
        std::printf("  %zu bytes written (%zu things)\n", outText.size(),
                    file.things().size());
    }
    return 0;
}

// Content signature of a thing (its type + all properties + CTC blocks, minus
// the UID key), used to detect whether two mods changed the same thing.
std::string thingSignature(const forge::tng::Thing& t) {
    std::string s = t.type;
    s += '|';
    for (const auto& p : t.properties) {
        if (p.key == "UID") continue;
        s += p.key;
        s += '=';
        s += p.value;
        s += ';';
    }
    for (const auto& c : t.ctcBlocks) {
        s += '[';
        s += c.name;
        s += ':';
        for (const auto& p : c.properties) {
            s += p.key;
            s += '=';
            s += p.value;
            s += ',';
        }
        s += ']';
    }
    return s;
}

// A thing's merge key: its UID if present, else a content-derived key.
std::string thingKey(const forge::tng::Thing& t) {
    if (auto uid = t.find("UID")) return "uid:" + *uid;
    return "sig:" + thingSignature(t);
}

// Thing-level conflict report for one level's TNG across mods (the level analog
// of `mods analyze`): keys things by UID, and reports which things each mod
// adds/changes/removes vs base, and which things multiple mods change (true
// thing-level conflicts) — proving whole-file-conflicting TNGs are mostly
// mergeable at the thing level. Read-only. mods in load order.
int tngConflicts(const std::string& basePath,
                 const std::vector<std::string>& modPaths, bool jsonOutput) {
    const auto base = forge::tng::File::parse(basePath);
    std::map<std::string, std::string> baseSig;  // key -> signature
    for (const auto& t : base.things()) baseSig[thingKey(t)] = thingSignature(t);

    struct ModStat {
        std::string name;
        size_t added = 0, changed = 0, removed = 0;
    };
    std::vector<ModStat> stats;
    std::map<std::string, std::vector<std::string>> changedBy;  // key -> mods

    for (const auto& modPath : modPaths) {
        namespace fs = std::filesystem;
        ModStat st;
        // Level files share a filename across mods; the parent dir distinguishes.
        const fs::path mp(modPath);
        st.name = mp.parent_path().filename().string();
        if (st.name.empty()) st.name = mp.filename().string();
        const auto mod = forge::tng::File::parse(modPath);
        std::map<std::string, std::string> modSig;
        for (const auto& t : mod.things()) {
            const std::string k = thingKey(t);
            modSig[k] = thingSignature(t);
            auto bit = baseSig.find(k);
            if (bit == baseSig.end()) {
                ++st.added;
            } else if (bit->second != modSig[k]) {
                ++st.changed;
                changedBy[k].push_back(st.name);
            }
        }
        for (const auto& [k, sig] : baseSig) {
            if (modSig.find(k) == modSig.end()) ++st.removed;
        }
        stats.push_back(st);
    }

    size_t conflicts = 0;
    std::vector<std::pair<std::string, std::vector<std::string>>> conflictList;
    for (const auto& [k, mods] : changedBy) {
        if (mods.size() > 1) { ++conflicts; conflictList.emplace_back(k, mods); }
    }
    std::sort(conflictList.begin(), conflictList.end());

    if (jsonOutput) {
        json modRows = json::array();
        for (const auto& s : stats) {
            modRows.push_back({{"mod", s.name}, {"added", s.added},
                               {"changed", s.changed}, {"removed", s.removed}});
        }
        json crows = json::array();
        for (const auto& [k, mods] : conflictList) {
            crows.push_back({{"thing", k}, {"mods", mods}});
        }
        std::puts(json{{"base", basePath},
                       {"base_things", base.things().size()},
                       {"mods", modRows},
                       {"thing_conflicts", crows},
                       {"conflict_count", conflicts}}
                      .dump(2)
                      .c_str());
        return 0;
    }

    std::printf("%s: %zu things\n", basePath.c_str(), base.things().size());
    for (const auto& s : stats) {
        std::printf("  %-28s +%zu added  ~%zu changed  -%zu removed\n",
                    s.name.c_str(), s.added, s.changed, s.removed);
    }
    std::printf("thing-level conflicts (same thing changed by >1 mod): %zu\n",
                conflicts);
    size_t shown = 0;
    for (const auto& [k, mods] : conflictList) {
        if (shown++ >= 15) {
            std::printf("  ... and %zu more (use --json for the full list)\n",
                        conflictList.size() - 15);
            break;
        }
        std::string chain;
        for (size_t i = 0; i < mods.size(); ++i) chain += (i ? ", " : "") + mods[i];
        std::printf("  %-30s %s\n", k.c_str(), chain.c_str());
    }
    return 0;
}

// Thing-level merge of one level's TNG across mods: base + N mods -> out.tng.
// Things only one mod changed both apply; same-UID edits resolve by load order
// (later mod wins) with a reported conflict count. Additions from all mods are
// kept. Removals are NOT applied (conservative — keeps content). The level analog
// of `defs merge`; turns whole-file-conflicting TNGs into a merged level.
struct TngMergeResult { size_t things = 0, applied = 0, added = 0, conflicts = 0; };

// Thing-level merge: base TNG + mod TNGs (load order) -> outPath. A thing is keyed
// by UID; a thing only one mod changes auto-merges, same-thing changes take the
// load-order winner. Returns stats. Shared by `tng merge` and `mods merge`.
TngMergeResult mergeTngFiles(const std::string& basePath,
                             const std::vector<std::string>& modPaths,
                             const std::string& outPath) {
    namespace fs = std::filesystem;
    auto out = forge::tng::File::parse(basePath);

    std::map<std::string, std::string> baseSig;  // key -> signature
    for (const auto& t : out.things()) baseSig[thingKey(t)] = thingSignature(t);

    std::map<std::string, std::string> touchedBy;  // key -> last mod
    TngMergeResult r;

    for (const auto& modPath : modPaths) {
        const fs::path mp(modPath);
        std::string modName = mp.parent_path().filename().string();
        if (modName.empty()) modName = mp.filename().string();
        const auto mod = forge::tng::File::parse(modPath);

        for (const auto& mt : mod.things()) {
            const std::string key = thingKey(mt);
            const std::string sig = thingSignature(mt);
            auto bit = baseSig.find(key);
            const bool isAdd = bit == baseSig.end();
            const bool isChange = !isAdd && sig != bit->second;
            if (!isAdd && !isChange) continue;  // identical to base

            if (touchedBy.count(key)) ++r.conflicts;  // >1 mod touches this thing
            touchedBy[key] = modName;

            if (isAdd) {
                out.addThing(mt);
                baseSig[key] = sig;  // now present
                ++r.added;
            } else {
                // Locate the (possibly moved) thing in out by key and replace.
                size_t idx = out.things().size();
                for (size_t i = 0; i < out.things().size(); ++i) {
                    if (thingKey(out.things()[i]) == key) { idx = i; break; }
                }
                if (idx < out.things().size()) {
                    out.replaceThing(idx, mt);
                    baseSig[key] = sig;
                }
            }
            ++r.applied;
        }
    }

    fs::create_directories(fs::path(outPath).parent_path());
    std::ofstream ofs(outPath, std::ios::binary);
    const std::string serialized = out.serialize();
    ofs.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
    ofs.close();
    r.things = out.things().size();
    return r;
}

int tngMerge(const std::string& basePath,
             const std::vector<std::string>& modPaths, const std::string& outPath,
             bool jsonOutput) {
    const auto r = mergeTngFiles(basePath, modPaths, outPath);
    if (jsonOutput) {
        std::puts(json{{"out", outPath},
                       {"things", r.things},
                       {"applied", r.applied},
                       {"added", r.added},
                       {"conflicts", r.conflicts}}
                      .dump(2)
                      .c_str());
        return 0;
    }
    std::printf("merged %zu mod(s) into %s: %zu things, %zu changes applied "
                "(%zu new), %zu conflicts (load order: last wins)\n",
                modPaths.size(), outPath.c_str(), r.things, r.applied, r.added,
                r.conflicts);
    return 0;
}

// Parse a picks file: record<TAB>winner (or record=winner), one per line,
// '#' comments. Winner = a mod's label or "vanilla" to keep base. Shared by
// `defs merge --picks` and `qst merge --picks` (same GUI winner-picker output).
std::map<std::string, std::string> loadPicks(const std::string& picksPath) {
    std::map<std::string, std::string> picks;
    if (picksPath.empty()) return picks;
    std::ifstream pf(picksPath);
    std::string line;
    while (std::getline(pf, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t sep = line.find('\t');
        if (sep == std::string::npos) sep = line.find('=');
        if (sep == std::string::npos) continue;
        std::string rec = line.substr(0, sep);
        std::string win = line.substr(sep + 1);
        while (!rec.empty() && std::isspace((unsigned char)rec.back())) rec.pop_back();
        while (!win.empty() && std::isspace((unsigned char)win.front())) win.erase(win.begin());
        while (!win.empty() && std::isspace((unsigned char)win.back())) win.pop_back();
        if (!rec.empty()) picks[rec] = win;
    }
    return picks;
}

// Decoded dump of a .qst quest registry (AddQuest flags + AddTestQuest entries).
int qstList(const std::string& path, bool jsonOutput) {
    const auto file = forge::qst::File::parse(path);
    if (jsonOutput) {
        json stmts = json::array();
        for (const auto& s : file.statements()) {
            if (s.isQuest()) {
                stmts.push_back({{"type", "AddQuest"}, {"name", s.name()},
                                 {"active", s.active()}, {"line", s.line}});
            } else {
                stmts.push_back({{"type", "AddTestQuest"}, {"name", s.name()},
                                 {"start_hsp", s.args[1]}, {"mode", s.args[2]},
                                 {"display", s.args[3]}, {"ini", s.args[4]},
                                 {"end_script", s.args[5]}, {"card", s.args[6]},
                                 {"line", s.line}});
            }
        }
        std::puts(json{{"file", path}, {"quests", file.questCount()},
                       {"test_quests", file.testQuestCount()},
                       {"statements", stmts}}
                      .dump(2)
                      .c_str());
        return 0;
    }
    std::printf("%s: %zu AddQuest, %zu AddTestQuest\n", path.c_str(),
                file.questCount(), file.testQuestCount());
    for (const auto& s : file.statements()) {
        if (s.isQuest()) {
            std::printf("  L%4zu AddQuest      %-40s %s\n", s.line,
                        s.name().c_str(), s.active() ? "ACTIVE" : "dormant");
        } else {
            std::printf("  L%4zu AddTestQuest  %-40s mode=%s hsp=%s \"%s\"\n",
                        s.line, s.name().c_str(), s.args[2].c_str(),
                        s.args[1].empty() ? "-" : s.args[1].c_str(),
                        s.args[3].c_str());
        }
    }
    return 0;
}

int qstRoundtrip(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
    const auto file = forge::qst::File::parseText(bytes, path);
    const bool ok = file.serialize() == bytes;
    std::printf("%s: %s (%zu quests, %zu test quests, %zu bytes)\n", path.c_str(),
                ok ? "byte-identical" : "MISMATCH", file.questCount(),
                file.testQuestCount(), bytes.size());
    return ok ? 0 : 1;
}

// Statement-level merge of quest registries: base + N mods (load order) ->
// out. Flag flips edit the flag token in place (retail ordering/formatting
// preserved), new AddQuest/AddTestQuest statements append at end; same-name
// flag conflicts take the load-order winner unless a pick overrides. Shared
// by `qst merge` and `mods merge`.
forge::qst::MergeResult mergeQstFiles(
    const std::string& basePath, const std::vector<std::string>& modPaths,
    const std::string& outPath,
    const std::map<std::string, std::string>& picks) {
    namespace fs = std::filesystem;
    const auto base = forge::qst::File::parse(basePath);
    std::vector<forge::qst::File> files;
    files.reserve(modPaths.size());
    std::vector<forge::qst::MergeInput> inputs;
    for (const auto& modPath : modPaths) {
        const fs::path mp(modPath);
        std::string modName = mp.parent_path().filename().string();
        if (modName.empty()) modName = mp.filename().string();
        files.push_back(forge::qst::File::parse(modPath));
        inputs.push_back({modName, &files.back()});
    }
    auto result = forge::qst::merge(base, inputs, picks);

    if (fs::path(outPath).has_parent_path())
        fs::create_directories(fs::path(outPath).parent_path());
    std::ofstream ofs(outPath, std::ios::binary);
    const std::string serialized = result.merged.serialize();
    ofs.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
    return result;
}

int qstMerge(const std::string& basePath,
             const std::vector<std::string>& modPaths, const std::string& outPath,
             const std::string& picksPath, bool jsonOutput) {
    const auto picks = loadPicks(picksPath);
    const auto r = mergeQstFiles(basePath, modPaths, outPath, picks);
    if (jsonOutput) {
        json conflicts = json::array();
        for (const auto& c : r.conflicts) {
            json wanted = json::array();
            for (const auto& [mod, flag] : c.wanted)
                wanted.push_back({{"mod", mod}, {"active", flag}});
            conflicts.push_back({{"quest", c.name}, {"wanted", wanted},
                                 {"winner", c.winner},
                                 {"overridden", c.overridden}});
        }
        std::puts(json{{"out", outPath},
                       {"quests", r.merged.questCount()},
                       {"test_quests", r.merged.testQuestCount()},
                       {"applied", r.applied},
                       {"added_quests", r.addedQuests},
                       {"added_test_quests", r.addedTests},
                       {"conflicts", conflicts}}
                      .dump(2)
                      .c_str());
        return 0;
    }
    std::printf("merged %zu mod(s) into %s: %zu quests (%zu new), %zu test "
                "quests (%zu new), %zu changes applied, %zu conflicts\n",
                modPaths.size(), outPath.c_str(), r.merged.questCount(),
                r.addedQuests, r.merged.testQuestCount(), r.addedTests,
                r.applied, r.conflicts.size());
    for (const auto& c : r.conflicts) {
        std::string chain;
        for (const auto& [mod, flag] : c.wanted)
            chain += (chain.empty() ? "" : ", ") + mod +
                     (flag ? "=TRUE" : "=FALSE");
        std::printf("  %-36s %s -> %s%s\n", c.name.c_str(), chain.c_str(),
                    c.winner.c_str(), c.overridden ? "  [pick]" : "");
    }
    return 0;
}

int levInfo(const std::string& path) {
    const auto file = forge::lev::File::open(path);
    std::printf("%s: map %dx%d (%dx%d cells), uid=%llu\n", file.source().c_str(),
                file.width(), file.height(), file.cellsX(), file.cellsY(),
                static_cast<unsigned long long>(file.uid()));

    size_t walkable = 0;
    float minHeight = 1.0e30f, maxHeight = -1.0e30f;
    std::vector<size_t> themeUse(file.groundThemes().size(), 0);
    for (int y = 0; y < file.cellsY(); ++y) {
        for (int x = 0; x < file.cellsX(); ++x) {
            const float height = file.heightAt(x, y);
            minHeight = std::min(minHeight, height);
            maxHeight = std::max(maxHeight, height);
            if (file.walkableAt(x, y)) ++walkable;
            ++themeUse[file.themeIndexAt(x, y, file.dominantLayerAt(x, y))];
        }
    }
    std::printf("walkable: %zu of %d cells\n", walkable,
                file.cellsX() * file.cellsY());
    std::printf("height: %.3f..%.3f, center(%d,%d)=%.3f\n", minHeight, maxHeight,
                file.width() / 2, file.height() / 2,
                file.heightAt(file.width() / 2, file.height() / 2));
    std::printf("themes: %zu, nav sections: %zu\n", file.themes().size(),
                file.navSections().size());
    std::printf("ground themes in use:\n");
    for (size_t i = 0; i < themeUse.size(); ++i) {
        if (themeUse[i] > 0) {
            std::printf("  [%3zu] %-45s def=%u %zu cells\n", i,
                        file.groundThemes()[i].name.c_str(),
                        file.groundThemes()[i].value, themeUse[i]);
        }
    }
    return 0;
}

// Audit (and optionally repair) the ground-theme palette of one or more levels
// against a target install's game.bin. A palette slot stores a GLOBAL def entry
// index; names are stable, indices are not, so a level authored elsewhere must
// be rebased by name before it is shipped. See docs/CUSTOM_LEVEL_AUTHORING_UX.md
// section 6.1 for how this was found and proven on retail data.
int levThemePalette(const std::vector<std::string>& args, bool rebase) {
    if (args.size() < 2) {
        std::fprintf(stderr, "usage: forge lev theme%s <game-root> <level.lev>...\n",
                     rebase ? "rebase" : "check");
        return 2;
    }
    const auto defs = forge::themepalette::openDefBank(args[0]);
    const auto table = forge::themepalette::makeDefIndexTable(defs);
    std::printf("def bank: %zu entries\n", defs.entries().size());
    int wrong = 0, unresolvable = 0, changed = 0;
    for (size_t i = 1; i < args.size(); ++i) {
        const auto report = rebase
            ? forge::themepalette::rebaseLevel(args[i], table)
            : forge::themepalette::auditLevel(args[i], table);
        std::printf("%s\n  %d named slots, %d stale, %d unknown%s\n",
                    args[i].c_str(), report.namedSlots, report.wrong,
                    report.unresolvable,
                    rebase && report.wrong > 0 ? " -> REBASED" : "");
        for (const auto& issue : report.issues) {
            std::printf("    %s\n",
                        forge::themepalette::formatIssue(issue).c_str());
        }
        wrong += report.wrong;
        unresolvable += report.unresolvable;
        if (rebase) changed += report.wrong;
    }
    if (rebase) {
        std::printf("rebased %d palette entries; %d unknown themes left untouched\n",
                    changed, unresolvable);
        return unresolvable == 0 ? 0 : 1;
    }
    return (wrong == 0 && unresolvable == 0) ? 0 : 1;
}

int levThemeGrid(const std::string& path, const std::string& outPath) {
    const auto file = forge::lev::File::open(path);
    std::FILE* out = outPath.empty() ? stdout : std::fopen(outPath.c_str(), "wb");
    if (out == nullptr) {
        std::fprintf(stderr, "cannot write %s\n", outPath.c_str());
        return 1;
    }
    std::fprintf(out, "x,y,height,theme,strength\n");
    for (int y = 0; y < file.cellsY(); ++y) {
        for (int x = 0; x < file.cellsX(); ++x) {
            const int layer = file.dominantLayerAt(x, y);
            std::fprintf(out, "%d,%d,%g,%s,%u\n", x, y, file.heightAt(x, y),
                         file.themeNameAt(x, y).c_str(),
                         file.themeStrengthAt(x, y, layer));
        }
    }
    if (out != stdout) std::fclose(out);
    return 0;
}

int levSculpt(const std::vector<std::string>& args) {
    // args: src out mode x y radius amount [flatten-target]
    if (args.size() < 7) return usage();
    auto file = forge::lev::File::open(args[0]);
    auto field = forge::terrain::Heightfield::fromLev(file);
    forge::terrain::Brush brush;
    const std::string& mode = args[2];
    if (mode == "raise" || mode == "lower") {
        brush.mode = forge::terrain::BrushMode::RaiseLower;
    } else if (mode == "flatten") {
        brush.mode = forge::terrain::BrushMode::Flatten;
        if (args.size() < 8) {
            throw std::invalid_argument("lev sculpt flatten requires target-height");
        }
        brush.targetHeight = std::stof(args[7]);
    } else if (mode == "smooth") {
        brush.mode = forge::terrain::BrushMode::Smooth;
    } else {
        throw std::invalid_argument("lev sculpt: mode must be raise, lower, flatten, or smooth");
    }
    brush.centerX = std::stof(args[3]);
    brush.centerY = std::stof(args[4]);
    brush.radius = std::stof(args[5]);
    brush.amount = std::stof(args[6]);
    if (mode == "lower") brush.amount = -std::abs(brush.amount);

    const size_t changed = forge::terrain::applyBrush(field, brush);
    field.writeTo(file);
    file.save(args[1]);
    std::printf("sculpted %zu vertices (%s, center %.2f,%.2f radius %.2f) -> %s\n",
                changed, mode.c_str(), brush.centerX, brush.centerY, brush.radius,
                args[1].c_str());
    return 0;
}

int levImportHeightmap(const std::vector<std::string>& args) {
    // args: src.lev out.lev normalized-f32.raw source-width source-height base scale
    if (args.size() != 7) return usage();
    auto file = forge::lev::File::open(args[0]);
    const int sourceWidth = std::stoi(args[3]);
    const int sourceHeight = std::stoi(args[4]);
    const float base = std::stof(args[5]);
    const float scale = std::stof(args[6]);
    const auto raw = readAllBytes(args[2]);
    const size_t sampleCount = static_cast<size_t>(sourceWidth) * sourceHeight;
    if (sourceWidth < 2 || sourceHeight < 2 || raw.size() != sampleCount * 4)
        throw std::invalid_argument("lev import-heightmap: raw file must contain src-w*src-h float32 samples");
    std::vector<float> samples(sampleCount);
    std::memcpy(samples.data(), raw.data(), raw.size());
    auto field = forge::terrain::fromNormalizedRaster(
        samples, sourceWidth, sourceHeight, file.width(), file.height(), base, scale);
    field.writeTo(file);
    file.save(args[1]);
    std::printf("imported %dx%d normalized float raster -> %dx%d map (%dx%d vertices), "
                "height %.3f..%.3f -> %s\n",
                sourceWidth, sourceHeight, file.width(), file.height(),
                file.cellsX(), file.cellsY(), base, base + scale, args[1].c_str());
    return 0;
}

int levAuthorSurface(const std::vector<std::string>& args) {
    // Replace every inherited cell material/walkability value with a deterministic
    // authored result derived solely from the current heightfield.
    if (args.size() < 5 || args.size() > 6) return usage();
    auto file = forge::lev::File::open(args[0]);
    auto themeIndex = [&](const std::string& wanted) -> uint8_t {
        for (size_t i = 0; i < file.groundThemes().size(); ++i) {
            std::string a = file.groundThemes()[i].name, b = wanted;
            std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c){ return std::tolower(c); });
            std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c){ return std::tolower(c); });
            if (a == b) {
                if (i > 255) throw std::runtime_error("lev author-surface: theme index exceeds byte range");
                return static_cast<uint8_t>(i);
            }
        }
        throw std::runtime_error("lev author-surface: theme not found: " + wanted);
    };
    const std::array<uint8_t, 3> themes = {
        themeIndex(args[2]), themeIndex(args[3]), themeIndex(args[4])};
    const float maxSlope = args.size() == 6 ? std::stof(args[5]) : 1.25f;
    if (!(maxSlope >= 0.0f) || !std::isfinite(maxSlope))
        throw std::invalid_argument("lev author-surface: max-slope must be finite and nonnegative");
    float minHeight = std::numeric_limits<float>::max();
    float maxHeight = -std::numeric_limits<float>::max();
    for (int y = 0; y < file.cellsY(); ++y)
        for (int x = 0; x < file.cellsX(); ++x) {
            minHeight = std::min(minHeight, file.heightAt(x, y));
            maxHeight = std::max(maxHeight, file.heightAt(x, y));
        }
    size_t walkable = 0;
    for (int y = 0; y < file.cellsY(); ++y) {
        for (int x = 0; x < file.cellsX(); ++x) {
            const float t = maxHeight > minHeight
                ? std::clamp((file.heightAt(x, y) - minHeight) / (maxHeight - minHeight), 0.0f, 1.0f)
                : 0.0f;
            std::array<uint8_t, 3> strengths{};
            if (t <= 0.5f) {
                strengths[1] = static_cast<uint8_t>(std::lround(t * 2.0f * 255.0f));
                strengths[0] = static_cast<uint8_t>(255 - strengths[1]);
            } else {
                strengths[2] = static_cast<uint8_t>(std::lround((t - 0.5f) * 2.0f * 255.0f));
                strengths[1] = static_cast<uint8_t>(255 - strengths[2]);
            }
            file.setThemeBlendAt(x, y, themes, strengths);
            const int xl = std::max(0, x - 1), xr = std::min(file.width(), x + 1);
            const int yt = std::max(0, y - 1), yb = std::min(file.height(), y + 1);
            const float dx = (file.heightAt(xr, y) - file.heightAt(xl, y)) / float(xr - xl);
            const float dy = (file.heightAt(x, yb) - file.heightAt(x, yt)) / float(yb - yt);
            const bool canWalk = std::sqrt(dx * dx + dy * dy) <= maxSlope;
            file.setWalkableAt(x, y, canWalk);
            file.setPreferredPathAt(x, y, false);
            walkable += canWalk;
        }
    }
    file.save(args[1]);
    std::printf("authored surface themes=(%s,%s,%s), walkable=%zu/%d maxSlope=%.3f -> %s\n",
                args[2].c_str(), args[3].c_str(), args[4].c_str(), walkable,
                file.cellsX() * file.cellsY(), maxSlope, args[1].c_str());
    return 0;
}

int stbCreateBackground(const std::vector<std::string>& args) {
    // args: lev inline-texture world-x world-y out-prefix
    if (args.size() != 5) return usage();
    const auto lev = forge::lev::File::open(args[0]);
    const auto heightfield = forge::terrain::Heightfield::fromLev(lev);
    const auto texture = forge::stbbake::parseInlineTexture(readAllBytes(args[1]));
    const int worldX = std::stoi(args[2]);
    const int worldY = std::stoi(args[3]);
    const auto patches = forge::stbbake::buildBackgroundPatchGrid(
        heightfield, worldX, worldY, texture);
    constexpr size_t baseOffset = 0x1000;
    const auto layout = forge::stbbake::layoutBackgroundFrames(
        patches, baseOffset, 2048);
    std::vector<uint8_t> framed(baseOffset, 0);
    framed.insert(framed.end(), layout.bytes.begin(), layout.bytes.end());
    const auto directory = forge::stbbake::generateQuadDir(layout.entries);
    writeAllBytes(args[4] + ".frames.bin", framed);
    writeAllBytes(args[4] + ".quaddir.bin", directory);
    std::FILE* manifest = std::fopen((args[4] + ".frames.tsv").c_str(), "wb");
    if (!manifest) throw std::runtime_error("create-background: cannot write manifest");
    std::fprintf(manifest, "index\tflags\toffset\tspan\tmin_x\tmin_y\tmin_z\tmax_x\tmax_y\tmax_z\n");
    for (size_t i = 0; i < layout.entries.size(); ++i) {
        const auto& e = layout.entries[i];
        std::fprintf(manifest, "%zu\t0x%08x\t%u\t%u\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\n",
                     i, e.flags, e.frameOffset, e.frameSpan,
                     e.aabb[0], e.aabb[1], e.aabb[2],
                     e.aabb[3], e.aabb[4], e.aabb[5]);
    }
    std::fclose(manifest);
    std::printf("created 16 donor-free background frames, 2048-aligned, "
                "%zu frame bytes + %zu directory bytes -> %s.*\n",
                framed.size(), directory.size(), args[4].c_str());
    return 0;
}

int stbCreateSolidInline(const std::vector<std::string>& args) {
    if (args.size() != 2) return usage();
    size_t consumed = 0;
    const unsigned long parsed = std::stoul(args[1], &consumed, 0);
    if (consumed != args[1].size() || parsed > UINT16_MAX)
        throw std::invalid_argument(
            "create-solid-inline: rgb565 must be a 16-bit integer");
    // A single DXT1 block. Identical endpoints plus zero selectors make all
    // sixteen texels decode to the caller-supplied RGB565 colour.
    forge::stbbake::InlineTexture texture;
    texture.width = texture.height = 4;
    texture.levels = 1;
    texture.pixelFormat0 = 3;
    texture.mipData.resize(8, 0);
    const uint16_t colour = static_cast<uint16_t>(parsed);
    texture.mipData[0] = texture.mipData[2] = uint8_t(colour);
    texture.mipData[1] = texture.mipData[3] = uint8_t(colour >> 8);
    const auto bytes = forge::stbbake::serializeInlineTexture(texture);
    writeAllBytes(args[0], bytes);
    std::printf("created 4x4 single-level DXT1 inline texture rgb565=0x%04x "
                "-> %s (%zu bytes)\n",
                unsigned(colour), args[0].c_str(), bytes.size());
    return 0;
}

int levImportF2PreviewMaterials(const std::vector<std::string>& args) {
    namespace fs = std::filesystem;
    if (args.size() < 6) return usage();
    bool acceptedHeuristic = false;
    struct ThemeAssignment { size_t slot; std::string name; uint32_t defIndex; };
    std::vector<ThemeAssignment> assignments;
    for (size_t i = 5; i < args.size(); ++i) {
        if (args[i] == "--accept-preview-heuristic") {
            acceptedHeuristic = true;
        } else if (args[i] == "--theme" && i + 3 < args.size()) {
            const unsigned long slot = std::stoul(args[++i]);
            const std::string name = args[++i];
            const unsigned long defIndex = std::stoul(args[++i]);
            if (slot > 255 || defIndex > UINT32_MAX)
                throw std::invalid_argument("lev import-f2-preview-materials: theme assignment out of range");
            assignments.push_back({size_t(slot), name, uint32_t(defIndex)});
        } else {
            throw std::invalid_argument("lev import-f2-preview-materials: unknown option " + args[i]);
        }
    }
    if (!acceptedHeuristic)
        throw std::invalid_argument(
            "lev import-f2-preview-materials: explicit --accept-preview-heuristic is required");
    auto level = forge::lev::File::open(args[0]);
    const fs::path manifestPath = args[2];
    std::ifstream input(manifestPath);
    if (!input) throw std::runtime_error("lev import-f2-preview-materials: cannot open manifest");
    const json manifest = json::parse(input);
    if (manifest.value("schema", "") != "f2_ehf_material_handoff_v1" ||
        manifest.value("classification", "") !=
            "PREVIEW_HEURISTIC_PLUS_LOSSLESS_SOURCE")
        throw std::runtime_error(
            "lev import-f2-preview-materials: unsupported or unclassified manifest");
    const auto dims = manifest.at("previewHeuristics").at("weightDimensions");
    const int rasterWidth = dims.at(0).get<int>();
    const int rasterHeight = dims.at(1).get<int>();
    if (manifest.at("previewHeuristics").value("orientation", "").find("no flip") ==
        std::string::npos)
        throw std::runtime_error("lev import-f2-preview-materials: orientation is not proven no-flip");

    std::vector<uint8_t> mapping;
    size_t start = 0;
    while (start <= args[4].size()) {
        const size_t comma = args[4].find(',', start);
        const std::string token = args[4].substr(start, comma - start);
        const unsigned long value = std::stoul(token);
        if (value > 255) throw std::invalid_argument("lev import-f2-preview-materials: theme slot exceeds 255");
        mapping.push_back(static_cast<uint8_t>(value));
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    const auto& files = manifest.at("weightFiles");
    if (mapping.size() != files.size())
        throw std::invalid_argument(
            "lev import-f2-preview-materials: mapping count must equal weight file count");
    std::vector<std::vector<float>> rasters;
    const size_t rasterSamples = static_cast<size_t>(rasterWidth) * rasterHeight;
    for (size_t i = 0; i < files.size(); ++i) {
        if (files.at(i).at("materialIndex").get<size_t>() != i)
            throw std::runtime_error("lev import-f2-preview-materials: material indices are not dense");
        const fs::path path = manifestPath.parent_path() /
                              files.at(i).at("file").get<std::string>();
        const auto bytes = readAllBytes(path.string());
        if (bytes.size() != rasterSamples * sizeof(float) ||
            bytes.size() != files.at(i).at("bytes").get<size_t>())
            throw std::runtime_error("lev import-f2-preview-materials: weight raster size mismatch: " + path.string());
        std::vector<float> raster(rasterSamples);
        std::memcpy(raster.data(), bytes.data(), bytes.size());
        rasters.push_back(std::move(raster));
    }

    const int tileIndex = std::stoi(args[3]);
    const auto& tiles = manifest.at("tlcTiles");
    const auto tileIt = std::find_if(tiles.begin(), tiles.end(), [&](const json& tile) {
        return tile.at("index").get<int>() == tileIndex;
    });
    if (tileIt == tiles.end())
        throw std::invalid_argument("lev import-f2-preview-materials: tile index absent from manifest");
    const int originX = tileIt->at("origin_cells").at(0).get<int>();
    const int originY = tileIt->at("origin_cells").at(1).get<int>();
    if (tileIt->at("cells").at(0).get<int>() != level.width() ||
        tileIt->at("cells").at(1).get<int>() != level.height())
        throw std::runtime_error("lev import-f2-preview-materials: tile/LEV dimensions differ");
    int extentX = 0, extentY = 0;
    for (const auto& tile : tiles) {
        extentX = std::max(extentX, tile.at("origin_cells").at(0).get<int>() +
                                    tile.at("cells").at(0).get<int>());
        extentY = std::max(extentY, tile.at("origin_cells").at(1).get<int>() +
                                    tile.at("cells").at(1).get<int>());
    }

    double discardedTotal = 0.0, maxDiscarded = 0.0;
    size_t vertices = 0;
    for (int y = 0; y < level.cellsY(); ++y) {
        for (int x = 0; x < level.cellsX(); ++x) {
            std::vector<float> weights;
            weights.reserve(rasters.size());
            for (const auto& raster : rasters)
                weights.push_back(forge::terrain::sampleMaterialWeight(
                    raster, rasterWidth, rasterHeight, float(originX + x),
                    float(originY + y), float(extentX), float(extentY)));
            const double sum = std::accumulate(weights.begin(), weights.end(), 0.0);
            if (std::abs(sum - 1.0) > 1e-4)
                throw std::runtime_error("lev import-f2-preview-materials: sampled weights do not sum to one");
            std::array<double, 256> merged{};
            for (size_t i = 0; i < weights.size(); ++i) merged[mapping[i]] += weights[i];
            std::vector<double> ranked;
            for (double value : merged) if (value > 0.0) ranked.push_back(value);
            std::sort(ranked.begin(), ranked.end(), std::greater<double>());
            const double kept = std::accumulate(ranked.begin(),
                ranked.begin() + std::min<size_t>(3, ranked.size()), 0.0);
            const double discarded = std::max(0.0, sum - kept);
            discardedTotal += discarded;
            maxDiscarded = std::max(maxDiscarded, discarded);
            const auto blend = forge::terrain::projectMaterialWeights(weights, mapping);
            level.setThemeBlendAt(x, y, blend.indices, blend.strengths);
            ++vertices;
        }
    }
    for (const auto& assignment : assignments)
        level.setGroundTheme(assignment.slot, assignment.name, assignment.defIndex);
    for (uint8_t slot : mapping) {
        if (level.groundThemes().at(slot).name.empty())
            throw std::runtime_error(
                "lev import-f2-preview-materials: mapped TLC theme slot is empty: " +
                std::to_string(slot));
    }
    level.save(args[1]);
    std::printf("imported F2 PREVIEW_HEURISTIC material weights for tile %d: %zu vertices, mean discarded %.8f, max discarded %.8f -> %s\n",
                tileIndex, vertices, discardedTotal / vertices, maxDiscarded,
                args[1].c_str());
    return 0;
}

std::string lowered(std::string value);  // defined below

int wadMetadata(const std::string& path, const std::string& filter) {
    const auto archive = forge::wad::Archive::open(path);
    size_t shown = 0;
    for (const auto& entry : archive.entries()) {
        if (lowered(entry.name).find(lowered(filter)) == std::string::npos) continue;
        std::printf("id=%u type=%u size=%u crc=0x%08x timestamp=%u deps=%zu info=%zu %s\n",
                    entry.id, entry.type, entry.size, entry.crc, entry.timestamp,
                    entry.dependencies.size(), entry.info.size(), entry.name.c_str());
        for (const auto& dep : entry.dependencies)
            std::printf("  dep: %s\n", dep.c_str());
        if (!entry.info.empty()) {
            std::printf("  info:");
            for (uint8_t byte : entry.info) std::printf(" %02x", unsigned(byte));
            std::printf("\n");
        }
        ++shown;
    }
    std::printf("matched %zu entries\n", shown);
    return 0;
}

int stbAppendBatch(const std::string& src, const std::string& out,
                   const std::string& manifestPath, int replace = 0) {
    std::ifstream input(manifestPath);
    if (!input) throw std::runtime_error("stb append-batch: cannot open manifest");
    nlohmann::json manifest;
    input >> manifest;
    if (!manifest.contains("maps") || !manifest["maps"].is_array() ||
        manifest["maps"].empty())
        throw std::runtime_error("stb append-batch: manifest.maps must be a non-empty array");
    const std::filesystem::path base =
        std::filesystem::absolute(std::filesystem::path(manifestPath)).parent_path();
    std::vector<forge::stb::StaticMapAppend> maps;
    maps.reserve(manifest["maps"].size());
    for (const auto& item : manifest["maps"]) {
        for (const char* field : {"levelName", "entryName", "chunk", "commonRecord"})
            if (!item.contains(field) || !item[field].is_string())
                throw std::runtime_error(std::string("stb append-batch: map requires string ") + field);
        auto resolve = [&](const char* field) {
            std::filesystem::path path = item[field].get<std::string>();
            return path.is_absolute() ? path : base / path;
        };
        maps.push_back({item["levelName"].get<std::string>(),
                        item["entryName"].get<std::string>(),
                        readAllBytes(resolve("chunk").string()),
                        readAllBytes(resolve("commonRecord").string())});
    }
    if (replace == 2) forge::stb::replaceStaticMapsRelayout(src, out, maps);
    else if (replace) forge::stb::replaceStaticMaps(src, out, maps);
    else forge::stb::appendStaticMaps(src, out, maps);
    const auto result = forge::stb::Archive::open(out);
    for (const auto& map : maps) {
        const auto* entry = result.findEntry(map.entryName);
        if (!entry || result.read(*entry) != map.chunk)
            throw std::runtime_error("stb batch: post-write payload verification failed: " +
                                     map.entryName);
    }
    std::printf("atomically %s %zu static maps; verified payloads -> %s\n",
                replace ? "replaced" : "appended", maps.size(), out.c_str());
    return 0;
}

// forge stb patchverts <chunk.bin> <frame-index> [limit]
// Dump a background patch VB in STORED ORDER. Background triangles come from a
// shared index buffer, so vertex order is part of the contract: a correct AABB
// with the wrong order still yields holes and giant spanning triangles.
int stbPatchVerts(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::fprintf(stderr, "usage: forge stb patchverts <chunk.bin> <frame> [limit]\n");
        return 1;
    }
    const auto raw = readAllBytes(args[0]);
    const auto chunk = forge::stbbake::parseChunk(raw);
    const size_t frame = static_cast<size_t>(std::stoul(args[1]));
    const size_t limit = args.size() > 2 ? static_cast<size_t>(std::stoul(args[2])) : 24;
    const auto body = forge::stbbake::decodeFrame(chunk, frame);
    const auto pb = forge::stbbake::parsePatchBody(body);
    if (!pb.valid || pb.waterOnly) {
        std::fprintf(stderr, "patchverts: frame %zu is not a mesh patch\n", frame);
        return 1;
    }
    const auto verts = forge::stbbake::decodePatchVertices(pb);
    std::printf("frame %zu: pw=%u ph=%u verts=%zu\n", frame,
                unsigned(pb.header.pw), unsigned(pb.header.ph), verts.size());
    const int ox = verts.empty() ? 0 : int(verts[0].gridX);
    const int oy = verts.empty() ? 0 : int(verts[0].gridY);
    for (size_t i = 0; i < verts.size() && i < limit; ++i)
        std::printf("  %3zu: (%d,%d) z=%.3f\n", i,
                    int(verts[i].gridX) - ox, int(verts[i].gridY) - oy, verts[i].height);
    return 0;
}

int stbCreateTerrain(const std::vector<std::string>& args) {
    // lev background-inline world-x world-y bank-index fg-base fg-bump fg-cliff out-chunk out-info
    if (args.size() < 10) return usage();
    const auto lev = forge::lev::File::open(args[0]);
    const auto heightfield = forge::terrain::Heightfield::fromLev(lev);
    const auto backgroundTexture =
        forge::stbbake::parseInlineTexture(readAllBytes(args[1]));
    const int worldX = std::stoi(args[2]);
    const int worldY = std::stoi(args[3]);
    const int bankIndex = std::stoi(args[4]);
    forge::terrain::TerrainMaterialTuple material;
    std::vector<std::string> palette;
    bool numericIds = true;
    for (size_t i = 5; i <= 7; ++i) {
        size_t consumed = 0;
        unsigned long value = 0;
        try { value = std::stoul(args[i], &consumed, 0); }
        catch (const std::exception&) { numericIds = false; break; }
        if (consumed != args[i].size() || value > UINT16_MAX) {
            numericIds = false; break;
        }
        material.textures[i - 5] = static_cast<uint16_t>(value);
    }
    if (!numericIds) {
        material.textures = {1, 2, 3};
        palette = {"", args[5], args[6], args[7]};
    }
    // Optional extra foreground layers: --slope-tex <id> paints steep faces,
    // --height-tex <id> coats high ground. Both blend over the base layer.
    forge::terrain::TerrainMaterialTuple slopeMaterial{}, heightMaterial{};
    bool authorFoliage = false;
    bool foliageSubsections = true;
    std::filesystem::path meshBank;
    std::vector<forge::terrain::TerrainThemeMaterial> themeMaterials(256);
    bool themed = false;
    for (size_t i = 10; i < args.size(); ++i) {
        if (args[i] == "--foliage") { authorFoliage = true; continue; }
        if (args[i] == "--no-foliage-subsections") {
            foliageSubsections = false; continue;
        }
        if (args[i] == "--mesh-bank") {
            if (i + 1 >= args.size())
                throw std::invalid_argument("--mesh-bank requires a graphics.big path");
            meshBank = args[++i];
            continue;
        }
        if (args[i] == "--theme-material") {
            if (i + 7 >= args.size())
                throw std::invalid_argument("--theme-material requires SLOT and six texture ids");
            const unsigned long slot = std::stoul(args[++i]);
            if (slot > 255)
                throw std::invalid_argument("--theme-material slot exceeds 255");
            auto& theme = themeMaterials[slot];
            theme.available = true;
            for (size_t field = 0; field < 3; ++field)
                theme.base.textures[field] = uint32_t(std::stoul(args[++i]));
            for (size_t field = 0; field < 3; ++field)
                theme.cliff.textures[field] = uint32_t(std::stoul(args[++i]));
            themed = true;
            continue;
        }
        const bool slope = args[i] == "--slope-tex";
        const bool height = args[i] == "--height-tex";
        if (!slope && !height) continue;
        if (i + 1 >= args.size())
            throw std::invalid_argument(args[i] + " requires a texture id");
        const unsigned long id = std::stoul(args[i + 1], nullptr, 0);
        if (slope) slopeMaterial.textures = {uint32_t(id), uint32_t(id), 0};
        else heightMaterial.textures = {uint32_t(id), uint32_t(id), 0};
        ++i;
    }
    const auto built = forge::stbbake::buildTerrainChunk64(
        heightfield, worldX, worldY, bankIndex,
        material, palette, backgroundTexture, 2048,
        slopeMaterial, heightMaterial, authorFoliage, meshBank,
        foliageSubsections, themed ? themeMaterials :
            std::vector<forge::terrain::TerrainThemeMaterial>{},
        themed ? std::function<forge::terrain::ThemeBlend(int,int)>(
            [&](int x, int y) {
                forge::terrain::ThemeBlend blend;
                for (int slot = 0; slot < 3; ++slot) {
                    blend.indices[slot] = lev.themeIndexAt(x, y, slot);
                    blend.strengths[slot] = lev.themeStrengthAt(x, y, slot);
                }
                return blend;
            }) : std::function<forge::terrain::ThemeBlend(int,int)>{});
    writeAllBytes(args[8], built.chunk);
    writeAllBytes(args[9], forge::stbinfo::writeInfoBlock(built.info));
    std::printf("created donor-free %dx%d terrain: chunk=%zu info=0x%zx "
                "foregroundDir=0x%zx backgroundRoot=0x%zx localDetail=0x%zx -> %s, %s\n",
                lev.width(), lev.height(), built.chunk.size(), forge::stbinfo::kInfoBlockSize,
                built.foregroundDirectoryOffset, built.backgroundRootOffset,
                built.localDetailDescriptorOffset, args[8].c_str(), args[9].c_str());
    return 0;
}

int stbBackgroundTreeInfo(const std::vector<std::string>& args) {
    if (args.size() < 2 || (args.size() > 4 && args.size() != 7)) return usage();
    const auto raw = readAllBytes(args[0]);
    size_t rootOffset = 0;
    if (args[1] == "auto") {
        if (args.size() < 3)
            throw std::invalid_argument(
                "backgroundtreeinfo: auto requires a heightfield LEV");
        const auto dimensions = forge::terrain::Heightfield::fromLev(
            forge::lev::File::open(args[2]));
        std::vector<size_t> candidates;
        // A root must cover the complete map-local LEV rectangle. Parsing then
        // validates every file-block reference and exact binary partition, so
        // arbitrary header-like byte sequences cannot qualify.
        for (size_t offset = 0; offset + 47 <= raw.size(); ++offset) {
            const auto read16 = [&](size_t at) {
                return uint16_t(raw[at]) | uint16_t(raw[at + 1]) << 8;
            };
            if (read16(offset) != 0 || read16(offset + 2) != 0 ||
                read16(offset + 4) != dimensions.width() ||
                read16(offset + 6) != dimensions.height())
                continue;
            try {
                (void)forge::stbbake::parseBackgroundTree(raw, offset);
                candidates.push_back(offset);
            } catch (const std::exception&) {
            }
        }
        if (candidates.size() != 1) {
            std::ostringstream message;
            message << "backgroundtreeinfo: auto found " << candidates.size()
                    << " structurally valid roots";
            for (size_t offset : candidates)
                message << " 0x" << std::hex << offset;
            throw std::runtime_error(message.str());
        }
        rootOffset = candidates.front();
        std::printf("background root: auto=0x%zx\n", rootOffset);
    } else {
        size_t parsed = 0;
        const unsigned long long rootValue = std::stoull(args[1], &parsed, 0);
        if (parsed != args[1].size() ||
            rootValue > std::numeric_limits<size_t>::max())
            throw std::invalid_argument("backgroundtreeinfo: invalid root offset");
        rootOffset = static_cast<size_t>(rootValue);
    }
    const auto root = forge::stbbake::parseBackgroundTree(raw, rootOffset);
    std::optional<forge::terrain::Heightfield> oracleHeightfield;
    std::vector<uint8_t> oracleThresholds;
    std::vector<forge::stbbake::NativeThresholdProvenance> oracleProvenance;
    struct OracleMap {
        forge::terrain::Heightfield heightfield;
        int worldX = 0, worldY = 0;
        size_t worldSlot = 0;
        std::string name;
    };
    std::vector<OracleMap> oracleNeighbors;
    std::optional<std::pair<int, int>> oracleWorldOrigin;
    forge::stbbake::NativeBackgroundLodSettings oracleSettings;
    forge::stbbake::HeightSampler oracleSampler;
    const bool validateOnly = args.size() == 4 && args[3] == "--validate-only";
    if (args.size() >= 3 && !validateOnly) {
        oracleHeightfield.emplace(forge::terrain::Heightfield::fromLev(
            forge::lev::File::open(args[2])));
        forge::stbbake::NativeBackgroundLodSettings settings;
        if (args.size() >= 4) settings.staticMapQuality = std::stoi(args[3]);
        forge::stbbake::HeightSampler worldSampler;
        if (args.size() == 7) {
            namespace fs = std::filesystem;
            const auto world = forge::wld::File::parse(args[4]);
            const auto* target = world.findMap(args[6]);
            if (!target)
                throw std::invalid_argument("backgroundtreeinfo: level is absent from WLD");
            auto sameName = [](const std::string& a, const std::string& b) {
                return a.size() == b.size() && std::equal(
                    a.begin(), a.end(), b.begin(),
                    [](unsigned char x, unsigned char y) {
                        return std::tolower(x) == std::tolower(y);
                    });
            };
            std::set<std::string> candidateNames;
            for (const auto& region : world.regions()) {
                const bool ownsTarget = std::any_of(
                    region.containsMaps.begin(), region.containsMaps.end(),
                    [&](const auto& name) { return sameName(name, target->levelName); });
                if (!ownsTarget) continue;
                candidateNames.insert(region.containsMaps.begin(), region.containsMaps.end());
                candidateNames.insert(region.seesMaps.begin(), region.seesMaps.end());
            }
            if (candidateNames.empty())
                throw std::invalid_argument(
                    "backgroundtreeinfo: target does not belong to a WLD region");
            for (size_t worldSlot = 0; worldSlot < world.maps().size(); ++worldSlot) {
                const auto& map = world.maps()[worldSlot];
                if (sameName(map.levelName, target->levelName)) continue;
                fs::path relative(map.levelName);
                const fs::path path = fs::path(args[5]) / relative;
                if (!fs::is_regular_file(path)) continue;
                auto candidate = forge::terrain::Heightfield::fromLev(
                    forge::lev::File::open(path));
                if (map.mapX + candidate.width() < target->mapX - 1 ||
                    map.mapX > target->mapX + oracleHeightfield->width() + 1 ||
                    map.mapY + candidate.height() < target->mapY - 1 ||
                    map.mapY > target->mapY + oracleHeightfield->height() + 1)
                    continue;
                oracleNeighbors.push_back(
                    {std::move(candidate), map.mapX, map.mapY,
                     static_cast<size_t>(map.index),
                     map.levelName});
            }
            const int targetX = target->mapX, targetY = target->mapY;
            oracleWorldOrigin = std::pair<int, int>{targetX, targetY};
            const auto* targetHf = &*oracleHeightfield;
            const auto* neighbors = &oracleNeighbors;
            if (target->index < 1)
                throw std::runtime_error(
                    "backgroundtreeinfo: WLD map index 0 is reserved by the engine");
            const size_t targetSlot = static_cast<size_t>(target->index);
            int gridMinX = targetX, gridMinY = targetY;
            int gridMaxX = targetX + targetHf->width();
            int gridMaxY = targetY + targetHf->height();
            for (const auto& map : oracleNeighbors) {
                gridMinX = std::min(gridMinX, map.worldX);
                gridMinY = std::min(gridMinY, map.worldY);
                gridMaxX = std::max(gridMaxX, map.worldX + map.heightfield.width());
                gridMaxY = std::max(gridMaxY, map.worldY + map.heightfield.height());
            }
            const int gridOriginX = ((gridMinX >> 5) - 1) * 32;
            const int gridOriginY = ((gridMinY >> 5) - 1) * 32;
            const int gridWidth = (((gridMaxX + 31) >> 5) - (gridMinX >> 5) + 1) + 1;
            const int gridHeight = (((gridMaxY + 31) >> 5) - (gridMinY >> 5) + 1) + 1;
            auto ownership = std::make_shared<std::vector<int16_t>>(
                static_cast<size_t>(gridWidth) * gridHeight, int16_t(-1));
            auto linkMap = [&](int mapX, int mapY, int width, int height,
                               size_t slot) {
                const int x0 = (mapX - gridOriginX) >> 5;
                const int y0 = (mapY - gridOriginY) >> 5;
                const int x1 = (mapX + width - gridOriginX) >> 5;
                const int y1 = (mapY + height - gridOriginY) >> 5;
                for (int gy = y0; gy < y1; ++gy) {
                    for (int gx = x0; gx < x1; ++gx) {
                        auto& owner = (*ownership)[static_cast<size_t>(gy) * gridWidth + gx];
                        if (owner != -1)
                            throw std::runtime_error(
                                "backgroundtreeinfo: overlapping WLD map grid cells");
                        owner = static_cast<int16_t>(slot);
                    }
                }
            };
            linkMap(targetX, targetY, targetHf->width(), targetHf->height(),
                    targetSlot);
            for (const auto& map : oracleNeighbors)
                linkMap(map.worldX, map.worldY, map.heightfield.width(),
                        map.heightfield.height(), map.worldSlot);
            worldSampler = [targetHf, neighbors, ownership, targetX, targetY,
                            targetSlot, gridOriginX, gridOriginY, gridWidth,
                            gridHeight](int x, int y) {
                const int wx = targetX + x, wy = targetY + y;
                const int gx = (wx - gridOriginX) >> 5;
                const int gy = (wy - gridOriginY) >> 5;
                int owner = -1;
                if (gx >= 0 && gx < gridWidth && gy >= 0 && gy < gridHeight)
                    owner = (*ownership)[static_cast<size_t>(gy) * gridWidth + gx];
                if (owner == static_cast<int>(targetSlot) && x >= 0 &&
                    x < targetHf->width() && y >= 0 && y < targetHf->height())
                    return forge::stbbake::quantizeEngineHeight(targetHf->at(x, y));
                for (const auto& map : *neighbors) {
                    if (owner != static_cast<int>(map.worldSlot)) continue;
                    return forge::stbbake::quantizeEngineHeight(map.heightfield.at(
                        wx - map.worldX, wy - map.worldY));
                }
                return forge::stbbake::quantizeEngineHeight(targetHf->at(
                    std::clamp(x, 0, targetHf->width() - 1),
                    std::clamp(y, 0, targetHf->height() - 1)));
            };
            std::printf("background oracle: WLD world=(%d,%d), grid=%dx%d origin=(%d,%d), neighboring maps=%zu\n",
                        targetX, targetY, gridWidth, gridHeight, gridOriginX,
                        gridOriginY, oracleNeighbors.size());
            for (const auto& map : oracleNeighbors)
                std::printf("  neighbor world=(%d,%d) size=%dx%d %s\n",
                            map.worldX, map.worldY, map.heightfield.width(),
                            map.heightfield.height(), map.name.c_str());
        }
        oracleThresholds = forge::stbbake::buildNativeBackgroundLodThresholds(
            *oracleHeightfield, settings, worldSampler, &oracleProvenance);
        oracleSettings = settings;
        oracleSampler = worldSampler;

        const auto adaptiveShape =
            forge::stbbake::buildNativeAdaptiveBackgroundTreeShape(
                *oracleHeightfield, oracleWorldOrigin ? oracleWorldOrigin->first : 0,
                oracleWorldOrigin ? oracleWorldOrigin->second : 0,
                settings, worldSampler);
        using ShapeSignature = std::array<int, 8>;
        auto shapeSignatures = [](const forge::stbbake::BackgroundTreeNode& tree) {
            std::vector<ShapeSignature> signatures;
            auto visit = [&](auto&& self,
                             const forge::stbbake::BackgroundTreeNode& node) -> void {
                const auto& h = node.header;
                signatures.push_back({int(h.mapX), int(h.mapY), int(h.width),
                                      int(h.height), int(h.firstBand),
                                      int(h.firstNonSplitBand), int(h.lastBand),
                                      int(node.children.size())});
                for (const auto& child : node.children) self(self, child);
            };
            visit(visit, tree);
            std::sort(signatures.begin(), signatures.end());
            return signatures;
        };
        const auto retailShape = shapeSignatures(root);
        const auto nativeShape = shapeSignatures(adaptiveShape);
        std::vector<ShapeSignature> missingShape, extraShape;
        std::set_difference(retailShape.begin(), retailShape.end(),
                            nativeShape.begin(), nativeShape.end(),
                            std::back_inserter(missingShape));
        std::set_difference(nativeShape.begin(), nativeShape.end(),
                            retailShape.begin(), retailShape.end(),
                            std::back_inserter(extraShape));
        std::printf("adaptive tree oracle: retail=%zu native=%zu exact=%zu/%zu "
                    "missing=%zu extra=%zu\n",
                    retailShape.size(), nativeShape.size(),
                    retailShape.size() - missingShape.size(), retailShape.size(),
                    missingShape.size(), extraShape.size());
        for (size_t i = 0; i < std::min<size_t>(missingShape.size(), 8); ++i) {
            const auto& s = missingShape[i];
            std::printf("  missing rect=(%d,%d %dx%d) bands=(%d,%d,%d) children=%d\n",
                        s[0],s[1],s[2],s[3],s[4],s[5],s[6],s[7]);
        }
        for (size_t i = 0; i < std::min<size_t>(extraShape.size(), 8); ++i) {
            const auto& s = extraShape[i];
            std::printf("  extra rect=(%d,%d %dx%d) bands=(%d,%d,%d) children=%d\n",
                        s[0],s[1],s[2],s[3],s[4],s[5],s[6],s[7]);
        }

        // Foreground layer vertices carry the same quantised landscape height
        // used by the LOD-map producer. Audit source parity before interpreting
        // a topology-count mismatch as an algorithm mismatch.
        if (oracleWorldOrigin) {
            const auto chunk = forge::stbbake::parseChunk(raw);
            size_t compared = 0, mismatched = 0;
            size_t normalsCompared = 0, normalsMismatched = 0;
            std::set<std::pair<int, int>> normalMismatchPoints;
            float maxNormalComponentDifference = 0.0f;
            float maxDifference = 0.0f;
            std::map<std::pair<int, int>, std::pair<float, float>> mismatchPoints;
            for (size_t fi = 0; fi < chunk.frameIndices.size(); ++fi) {
                try {
                    const auto body = forge::stbbake::decodeFrame(chunk, fi);
                    const auto foreground = forge::stbbake::parseForegroundFrame(body);
                    if (forge::stbbake::serializeForegroundFrame(foreground) != body)
                        continue;
                    for (const auto& layer : foreground.layers) {
                        for (const auto& vertex : layer.vertices) {
                            const int x = int(vertex.x) - oracleWorldOrigin->first;
                            const int y = int(vertex.y) - oracleWorldOrigin->second;
                            if (x < 0 || y < 0 || x > oracleHeightfield->width() ||
                                y > oracleHeightfield->height())
                                continue;
                            const float expected = forge::stbbake::quantizeEngineHeight(
                                oracleHeightfield->at(x, y));
                            const float difference = std::fabs(vertex.height - expected);
                            ++compared;
                            if (difference != 0.0f) {
                                ++mismatched;
                                mismatchPoints.emplace(
                                    std::pair<int, int>{x, y},
                                    std::pair<float, float>{vertex.height, expected});
                            }
                            maxDifference = std::max(maxDifference, difference);
                            if (worldSampler) {
                                ++normalsCompared;
                                if (vertex.packedNormal !=
                                    forge::stbbake::packMapNormal(worldSampler, x, y)) {
                                    ++normalsMismatched;
                                    normalMismatchPoints.emplace(x, y);
                                    auto signedField = [](uint32_t value, uint32_t sign,
                                                          uint32_t range) {
                                        return int(value & (range - 1)) & int(sign)
                                            ? int(value & (range - 1)) - int(range)
                                            : int(value & (range - 1));
                                    };
                                    const auto expectedNormal =
                                        forge::stbbake::mapNormal(worldSampler, x, y);
                                    const float actualX = float(signedField(
                                        vertex.packedNormal, 0x400, 0x800)) / 1023.0f;
                                    const float actualY = float(signedField(
                                        vertex.packedNormal >> 11, 0x400, 0x800)) / 1023.0f;
                                    const float actualZ = float(signedField(
                                        vertex.packedNormal >> 22, 0x200, 0x400)) / 511.0f;
                                    maxNormalComponentDifference = std::max({
                                        maxNormalComponentDifference,
                                        std::fabs(actualX - expectedNormal.x),
                                        std::fabs(actualY - expectedNormal.y),
                                        std::fabs(actualZ - expectedNormal.z)});
                                }
                            }
                        }
                    }
                } catch (const std::exception&) {
                    // Background and unrelated frames are intentionally skipped.
                }
            }
            std::printf("background oracle source parity: foreground heights "
                        "mismatched=%zu/%zu max-difference=%.9g\n",
                        mismatched, compared, double(maxDifference));
            std::printf("background oracle source parity: foreground normals "
                        "mismatched=%zu/%zu unique-points=%zu\n",
                        normalsMismatched, normalsCompared,
                        normalMismatchPoints.size());
            std::printf("background oracle source parity: max normal-component "
                        "difference=%.9g\n", double(maxNormalComponentDifference));
            for (const auto& [coordinate, values] : mismatchPoints)
                std::printf("  source-height mismatch local=(%d,%d) "
                            "stb=%.9g lev=%.9g delta=%+.9g\n",
                            coordinate.first, coordinate.second,
                            double(values.first), double(values.second),
                            double(values.first - values.second));
        }
    }
    size_t nodeCount = 0, leafCount = 0, lodCount = 0;
    struct RetailAdaptiveRecord {
        int x, y, width, height, band;
        size_t vertices, triangles;
    };
    std::vector<RetailAdaptiveRecord> retailAdaptiveRecords;
    auto printNode = [&](auto&& self, const forge::stbbake::BackgroundTreeNode& node,
                         int depth) -> void {
        const auto& h = node.header;
        ++nodeCount;
        if (node.children.empty()) ++leafCount;
        lodCount += h.lod.size();
        std::printf("%*s@0x%zx rect=(%u,%u %ux%u) bands=%u,%u,%u "
                    "block=(0x%x,0x%x,0x%x) lod=%zu\n",
                    depth * 2, "", node.headerOffset,
                    unsigned(h.mapX), unsigned(h.mapY),
                    unsigned(h.width), unsigned(h.height),
                    unsigned(h.firstBand), unsigned(h.firstNonSplitBand),
                    unsigned(h.lastBand), unsigned(h.fileBlockPos),
                    unsigned(h.fileBlockSize), unsigned(h.offsetIntoFileBlock),
                    h.lod.size());
        for (size_t i = 0; i < h.lod.size(); ++i) {
            const auto& lod = h.lod[i];
            std::printf("%*slod band=%u remap=%u block=(0x%x,0x%x,0x%x)\n",
                        depth * 2 + 2, "",
                        unsigned(h.firstNonSplitBand + i),
                        unsigned(lod.optimizedBandRemap),
                        unsigned(lod.fileBlockPos), unsigned(lod.fileBlockSize),
                        unsigned(lod.offsetIntoFileBlock));
            if (lod.fileBlockPos > 0 && lod.fileBlockSize > 0 &&
                lod.offsetIntoFileBlock >= 0) {
                const uint64_t frameOffset = uint64_t(uint32_t(lod.fileBlockPos)) +
                                             uint64_t(uint32_t(lod.offsetIntoFileBlock));
                if (frameOffset < raw.size()) {
                    try {
                        size_t pos = static_cast<size_t>(frameOffset);
                        const auto body = forge::lzo::decompressFramed(raw, pos);
                        const auto header = forge::stbbake::parsePatchHeader(body);
                        const auto patch = forge::stbbake::parsePatchBody(body);
                        if (body.size() >= 17) {
                            if (oracleHeightfield && h.firstNonSplitBand + i != 1)
                                retailAdaptiveRecords.push_back({
                                    int(h.mapX), int(h.mapY), int(h.width), int(h.height),
                                    int(h.firstNonSplitBand + i), header.vertexCount,
                                    header.indexCount});
                            std::printf("%*spayload grid=%ux%u origin=(%u,%u) "
                                        "vertices=%u indices=%u bytes=%zu%s%s\n",
                                        depth * 2 + 4, "",
                                        unsigned(header.pw), unsigned(header.ph),
                                        unsigned(header.coord0), unsigned(header.coord1),
                                        unsigned(header.vertexCount),
                                        unsigned(header.indexCount), body.size(),
                                        header.valid ? "" : " sparse-grid",
                                        patch.valid ? "" : " body-segmentation-unknown");
                            if (oracleHeightfield) {
                                const auto predicted =
                                    forge::stbbake::buildNativeBackgroundLodTopology(
                                        oracleThresholds,
                                        oracleHeightfield->width(), oracleHeightfield->height(),
                                        h.mapX, h.mapY, h.width, h.height,
                                        uint8_t(h.firstNonSplitBand + i), 128, 128);
                                std::printf("%*spredicted vertices=%zu indices=%zu%s\n",
                                            depth * 2 + 4, "", predicted.vertices.size(),
                                            predicted.indices.size() / 3,
                                            h.firstNonSplitBand + i != 1 &&
                                                    predicted.vertices.size() == header.vertexCount &&
                                                    predicted.indices.size() / 3 == header.indexCount
                                                ? " MATCH" : " MISMATCH");
                                if (h.firstNonSplitBand + i != 1 &&
                                    (predicted.vertices.size() != header.vertexCount ||
                                     predicted.indices.size() / 3 != header.indexCount)) {
                                    const long long vertexDelta =
                                        static_cast<long long>(predicted.vertices.size()) -
                                        static_cast<long long>(header.vertexCount);
                                    const long long triangleDelta =
                                        static_cast<long long>(predicted.indices.size() / 3) -
                                        static_cast<long long>(header.indexCount);
                                    const bool mapBoundary = h.mapX == 0 || h.mapY == 0 ||
                                        unsigned(h.mapX) + unsigned(h.width) ==
                                            unsigned(oracleHeightfield->width()) ||
                                        unsigned(h.mapY) + unsigned(h.height) ==
                                            unsigned(oracleHeightfield->height());
                                    std::printf("%*sdelta rect=(%u,%u %ux%u) band=%u "
                                                "vertices=%+lld triangles=%+lld %s\n",
                                                depth * 2 + 4, "",
                                                unsigned(h.mapX), unsigned(h.mapY),
                                                unsigned(h.width), unsigned(h.height),
                                                unsigned(h.firstNonSplitBand + i),
                                                vertexDelta, triangleDelta,
                                                mapBoundary ? "MAP-BOUNDARY" : "INTERIOR");
                                    if (patch.valid) {
                                        const auto actualVertices =
                                            forge::stbbake::decodePatchVertices(patch);
                                        uint16_t actualMinX = 0xffff, actualMinY = 0xffff;
                                        for (const auto& vertex : actualVertices) {
                                            actualMinX = std::min(actualMinX, vertex.gridX);
                                            actualMinY = std::min(actualMinY, vertex.gridY);
                                        }
                                        std::set<std::pair<int, int>> actualPoints;
                                        std::set<std::pair<int, int>> predictedPoints;
                                        for (const auto& vertex : actualVertices)
                                            actualPoints.emplace(int(vertex.gridX - actualMinX),
                                                                 int(vertex.gridY - actualMinY));
                                        for (const auto& vertex : predicted.vertices)
                                            predictedPoints.emplace(int(vertex[0]) - int(h.mapX),
                                                                    int(vertex[1]) - int(h.mapY));
                                        std::vector<std::pair<int, int>> absent;
                                        std::set_difference(
                                            actualPoints.begin(), actualPoints.end(),
                                            predictedPoints.begin(), predictedPoints.end(),
                                            std::back_inserter(absent));
                                        std::printf("%*sretail-only adaptive points=%zu:",
                                                    depth * 2 + 4, "", absent.size());
                                        for (size_t p = 0; p < std::min<size_t>(absent.size(), 16); ++p) {
                                            const int mapX = int(h.mapX) + absent[p].first;
                                            const int mapY = int(h.mapY) + absent[p].second;
                                            const size_t thresholdIndex = size_t(mapY) *
                                                size_t(oracleHeightfield->width() + 1) + size_t(mapX);
                                            std::printf(" (%d,%d:t%u)", absent[p].first,
                                                        absent[p].second,
                                                        unsigned(oracleThresholds[thresholdIndex]));
                                        }
                                        std::printf("%s\n", absent.size() > 16 ? " ..." : "");
                                        float effectiveMeshDetail = oracleSettings.meshDetail;
                                        for (int quality = oracleSettings.staticMapQuality;
                                             quality < 8; ++quality)
                                            effectiveMeshDetail *= 2.0f;
                                        const auto traceSampler = oracleSampler
                                            ? oracleSampler
                                            : forge::stbbake::clampedHeightSampler(
                                                  *oracleHeightfield);
                                        for (size_t p = 0;
                                             p < std::min<size_t>(absent.size(), 16); ++p) {
                                            const int mapX = int(h.mapX) + absent[p].first;
                                            const int mapY = int(h.mapY) + absent[p].second;
                                            const auto trace =
                                                forge::stbbake::traceNativeBackgroundLodThreshold(
                                                    traceSampler,
                                                    oracleHeightfield->width(),
                                                    oracleHeightfield->height(), mapX, mapY,
                                                    effectiveMeshDetail);
                                            std::printf(
                                                "%*sthreshold-trace (%d,%d) endpoints=(%d,%d)-(%d,%d) "
                                                "h=%.9g mid=%.9g err=%.9g term=%.9g "
                                                "dots=(%.9g,%.9g) nerr=%.9g dist=%.9g "
                                                "nterm=%.9g base=%.9g\n",
                                                depth * 2 + 6, "", mapX, mapY,
                                                trace.ax, trace.ay, trace.bx, trace.by,
                                                trace.centerHeight, trace.midpointHeight,
                                                trace.midpointError, trace.midpointTerm,
                                                trace.dotA, trace.dotB, trace.normalError,
                                                trace.endpointDistance, trace.normalTerm,
                                                trace.baseZThreshold);
                                            const size_t provenanceIndex = size_t(mapY) *
                                                size_t(oracleHeightfield->width() + 1) +
                                                size_t(mapX);
                                            if (provenanceIndex < oracleProvenance.size()) {
                                                const auto& witness =
                                                    oracleProvenance[provenanceIndex];
                                                std::printf(
                                                    "%*spropagation witness source=(%d,%d) "
                                                    "base=%.9g chain=(%d,%d)",
                                                    depth * 2 + 8, "", witness.sourceX,
                                                    witness.sourceY, witness.sourceBaseZ,
                                                    mapX, mapY);
                                                int chainX = mapX, chainY = mapY;
                                                for (int hop = 0; hop < 32; ++hop) {
                                                    const size_t chainIndex = size_t(chainY) *
                                                        size_t(oracleHeightfield->width() + 1) +
                                                        size_t(chainX);
                                                    if (chainIndex >= oracleProvenance.size()) break;
                                                    const auto& link = oracleProvenance[chainIndex];
                                                    if (link.parentX < 0 ||
                                                        (link.parentX == chainX &&
                                                         link.parentY == chainY))
                                                        break;
                                                    chainX = link.parentX;
                                                    chainY = link.parentY;
                                                    std::printf("->(%d,%d)", chainX, chainY);
                                                }
                                                std::printf("\n");
                                                std::vector<std::pair<int, int>> pending{
                                                    {mapX, mapY}};
                                                std::set<std::pair<int, int>> closure;
                                                float strongestBase = -1.0f;
                                                int strongestX = -1, strongestY = -1;
                                                while (!pending.empty()) {
                                                    const auto point = pending.back();
                                                    pending.pop_back();
                                                    if (!closure.insert(point).second) continue;
                                                    const int px = point.first, py = point.second;
                                                    if (px < 0 || py < 0 ||
                                                        px > oracleHeightfield->width() ||
                                                        py > oracleHeightfield->height())
                                                        continue;
                                                    const size_t pi = size_t(py) *
                                                        size_t(oracleHeightfield->width() + 1) +
                                                        size_t(px);
                                                    const float pointBase =
                                                        oracleProvenance[pi].pointBaseZ;
                                                    if (pointBase > strongestBase) {
                                                        strongestBase = pointBase;
                                                        strongestX = px;
                                                        strongestY = py;
                                                    }
                                                    if (px == 0 && py == 0) continue;
                                                    uint32_t step = 1;
                                                    while (((uint32_t(px) & step) == 0) &&
                                                           ((uint32_t(py) & step) == 0))
                                                        step <<= 1;
                                                    auto queue = [&](int qx, int qy) {
                                                        if (qx >= 0 && qy >= 0 &&
                                                            qx <= oracleHeightfield->width() &&
                                                            qy <= oracleHeightfield->height())
                                                            pending.emplace_back(qx, qy);
                                                    };
                                                    if ((uint32_t(px) & uint32_t(py) & step) == 0) {
                                                        if (step > 1) {
                                                            const int half = int(step / 2);
                                                            queue(px-half, py-half);
                                                            queue(px+half, py-half);
                                                            queue(px-half, py+half);
                                                            queue(px+half, py+half);
                                                        }
                                                    } else {
                                                        const int s = int(step);
                                                        queue(px-s, py); queue(px+s, py);
                                                        queue(px, py-s); queue(px, py+s);
                                                    }
                                                }
                                                const int requestedBand =
                                                    int(h.firstNonSplitBand + i);
                                                const float cutoff = oracleSettings.firstLodZ *
                                                    std::ldexp(1.0f, requestedBand - 2);
                                                std::printf(
                                                    "%*sdependency closure=%zu strongest=(%d,%d) "
                                                    "base=%.9g cutoff=%.9g margin=%+.9g\n",
                                                    depth * 2 + 8, "", closure.size(),
                                                    strongestX, strongestY, strongestBase,
                                                    cutoff, strongestBase - cutoff);
                                            }
                                        }
                                    }
                                    std::vector<std::pair<int, int>> extentMatches;
                                    static constexpr int candidateExtents[] = {16, 32, 64, 128};
                                    for (int extentX : candidateExtents) {
                                        for (int extentY : candidateExtents) {
                                            const auto candidate =
                                                forge::stbbake::buildNativeBackgroundLodTopology(
                                                    oracleThresholds,
                                                    oracleHeightfield->width(),
                                                    oracleHeightfield->height(),
                                                    h.mapX, h.mapY, h.width, h.height,
                                                    uint8_t(h.firstNonSplitBand + i),
                                                    extentX, extentY);
                                            if (candidate.vertices.size() == header.vertexCount &&
                                                candidate.indices.size() / 3 == header.indexCount)
                                                extentMatches.emplace_back(extentX, extentY);
                                        }
                                    }
                                    std::printf("%*sextent-count matches:", depth * 2 + 4, "");
                                    for (const auto& extent : extentMatches)
                                        std::printf(" %dx%d", extent.first, extent.second);
                                    std::printf("%s\n", extentMatches.empty() ? " none" : "");
                                }
                            }
                        }
                    } catch (const std::exception& e) {
                        std::printf("%*spayload-decode-error: %s\n",
                                    depth * 2 + 4, "", e.what());
                    }
                }
            }
        }
        for (const auto& child : node.children) self(self, child, depth + 1);
    };
    printNode(printNode, root, 0);
    if (oracleHeightfield && oracleWorldOrigin && !oracleNeighbors.empty()) {
        struct SubsetScore { size_t mask = 0, matches = 0; };
        std::vector<SubsetScore> scores;
        const size_t subsetCount = size_t(1) << oracleNeighbors.size();
        scores.reserve(subsetCount);
        const int targetX = oracleWorldOrigin->first;
        const int targetY = oracleWorldOrigin->second;
        const auto* targetHf = &*oracleHeightfield;
        for (size_t mask = 0; mask < subsetCount; ++mask) {
            const auto subsetSampler = [targetHf, &oracleNeighbors, targetX, targetY, mask]
                (int x, int y) {
                    if (x >= 0 && x < targetHf->width() &&
                        y >= 0 && y < targetHf->height())
                        return forge::stbbake::quantizeEngineHeight(targetHf->at(x, y));
                    const int wx = targetX + x, wy = targetY + y;
                    for (size_t n = 0; n < oracleNeighbors.size(); ++n) {
                        if ((mask & (size_t(1) << n)) == 0) continue;
                        const auto& map = oracleNeighbors[n];
                        const int nx = wx - map.worldX, ny = wy - map.worldY;
                        if (nx >= 0 && nx < map.heightfield.width() &&
                            ny >= 0 && ny < map.heightfield.height())
                            return forge::stbbake::quantizeEngineHeight(
                                map.heightfield.at(nx, ny));
                    }
                    return forge::stbbake::quantizeEngineHeight(targetHf->at(
                        std::clamp(x, 0, targetHf->width() - 1),
                        std::clamp(y, 0, targetHf->height() - 1)));
                };
            const auto thresholds = forge::stbbake::buildNativeBackgroundLodThresholds(
                *oracleHeightfield, oracleSettings, subsetSampler);
            size_t matches = 0;
            for (const auto& record : retailAdaptiveRecords) {
                const auto topology = forge::stbbake::buildNativeBackgroundLodTopology(
                    thresholds, oracleHeightfield->width(), oracleHeightfield->height(),
                    record.x, record.y, record.width, record.height,
                    uint8_t(record.band), 128, 128);
                matches += topology.vertices.size() == record.vertices &&
                           topology.indices.size() / 3 == record.triangles;
            }
            scores.push_back({mask, matches});
        }
        std::sort(scores.begin(), scores.end(), [](const auto& a, const auto& b) {
            if (a.matches != b.matches) return a.matches > b.matches;
            return a.mask < b.mask;
        });
        std::printf("neighbor-subset oracle best=%zu/%zu mask=0x%zx maps:",
                    scores.front().matches, retailAdaptiveRecords.size(),
                    scores.front().mask);
        for (size_t n = 0; n < oracleNeighbors.size(); ++n)
            if (scores.front().mask & (size_t(1) << n))
                std::printf(" %s", oracleNeighbors[n].name.c_str());
        std::printf("\n");

        size_t bestLodZMatches = 0;
        int bestLodZ = 0;
        for (int firstLodZ = 1; firstLodZ <= 128; ++firstLodZ) {
            auto candidateSettings = oracleSettings;
            candidateSettings.firstLodZ = float(firstLodZ);
            const auto thresholds = forge::stbbake::buildNativeBackgroundLodThresholds(
                *oracleHeightfield, candidateSettings, oracleSampler);
            size_t matches = 0;
            for (const auto& record : retailAdaptiveRecords) {
                const auto topology = forge::stbbake::buildNativeBackgroundLodTopology(
                    thresholds, oracleHeightfield->width(), oracleHeightfield->height(),
                    record.x, record.y, record.width, record.height,
                    uint8_t(record.band), 128, 128);
                matches += topology.vertices.size() == record.vertices &&
                           topology.indices.size() / 3 == record.triangles;
            }
            if (matches > bestLodZMatches) {
                bestLodZMatches = matches;
                bestLodZ = firstLodZ;
            }
        }
        std::printf("firstLodZ integer oracle best=%zu/%zu value=%d\n",
                    bestLodZMatches, retailAdaptiveRecords.size(), bestLodZ);

        auto compilerMathSettings = oracleSettings;
        compilerMathSettings.compilerFastNormals = true;
        const auto compilerThresholds =
            forge::stbbake::buildNativeBackgroundLodThresholds(
                *oracleHeightfield, compilerMathSettings, oracleSampler);
        size_t compilerMathMatches = 0;
        size_t changedThresholds = 0;
        for (size_t i = 0; i < std::min(oracleThresholds.size(),
                                       compilerThresholds.size()); ++i)
            changedThresholds += oracleThresholds[i] != compilerThresholds[i];
        for (const auto& record : retailAdaptiveRecords) {
            const auto topology = forge::stbbake::buildNativeBackgroundLodTopology(
                compilerThresholds, oracleHeightfield->width(),
                oracleHeightfield->height(), record.x, record.y, record.width,
                record.height, uint8_t(record.band), 128, 128);
            compilerMathMatches += topology.vertices.size() == record.vertices &&
                                   topology.indices.size() / 3 == record.triangles;
        }
        std::printf("compiler-fast-normal oracle=%zu/%zu changed-thresholds=%zu/%zu\n",
                    compilerMathMatches, retailAdaptiveRecords.size(),
                    changedThresholds, compilerThresholds.size());

        auto egoSettings = oracleSettings;
        egoSettings.egoProducerNormals = true;
        const auto egoThresholds = forge::stbbake::buildNativeBackgroundLodThresholds(
            *oracleHeightfield, egoSettings, oracleSampler);
        size_t egoMatches = 0;
        size_t egoChangedThresholds = 0;
        for (size_t i = 0; i < std::min(oracleThresholds.size(), egoThresholds.size()); ++i)
            egoChangedThresholds += oracleThresholds[i] != egoThresholds[i];
        for (const auto& record : retailAdaptiveRecords) {
            const auto topology = forge::stbbake::buildNativeBackgroundLodTopology(
                egoThresholds, oracleHeightfield->width(), oracleHeightfield->height(),
                record.x, record.y, record.width, record.height,
                uint8_t(record.band), 128, 128);
            egoMatches += topology.vertices.size() == record.vertices &&
                          topology.indices.size() / 3 == record.triangles;
        }
        std::printf("ego-producer-normal oracle=%zu/%zu changed-thresholds=%zu/%zu\n",
                    egoMatches, retailAdaptiveRecords.size(), egoChangedThresholds,
                    egoThresholds.size());
    }
    std::printf("background tree: nodes=%zu leaves=%zu lod-records=%zu\n",
                nodeCount, leafCount, lodCount);
    return 0;
}

int levPaintTheme(const std::vector<std::string>& args) {
    // args: src out theme x y radius opacity
    if (args.size() != 7) return usage();
    auto file = forge::lev::File::open(args[0]);
    int themeIndex = -1;
    try {
        size_t consumed = 0;
        const int parsed = std::stoi(args[2], &consumed);
        if (consumed == args[2].size()) themeIndex = parsed;
    } catch (const std::exception&) {}
    auto equalInsensitive = [](const std::string& a, const std::string& b) {
        return a.size() == b.size() && std::equal(
            a.begin(), a.end(), b.begin(), [](unsigned char x, unsigned char y) {
                return std::tolower(x) == std::tolower(y);
            });
    };
    if (themeIndex < 0) {
        for (size_t i = 0; i < file.groundThemes().size(); ++i) {
            if (equalInsensitive(file.groundThemes()[i].name, args[2])) {
                themeIndex = static_cast<int>(i);
                break;
            }
        }
    }
    if (themeIndex < 0 || themeIndex >= static_cast<int>(file.groundThemes().size()) ||
        file.groundThemes()[themeIndex].name.empty()) {
        throw std::invalid_argument("lev paint-theme: unknown/empty ground-theme index or name");
    }
    forge::terrain::ThemeBrush brush;
    brush.themeIndex = static_cast<uint8_t>(themeIndex);
    brush.centerX = std::stof(args[3]);
    brush.centerY = std::stof(args[4]);
    brush.radius = std::stof(args[5]);
    brush.opacity = std::stof(args[6]);
    const size_t changed = forge::terrain::applyThemeBrush(file, brush);
    file.save(args[1]);
    std::printf("painted theme [%d] %s across %zu cells (center %.2f,%.2f radius %.2f opacity %.2f) -> %s\n",
                themeIndex, file.groundThemes()[themeIndex].name.c_str(), changed,
                brush.centerX, brush.centerY, brush.radius, brush.opacity,
                args[1].c_str());
    return 0;
}

int levPaintBoolean(const std::vector<std::string>& args, bool preferred) {
    // args: src out x y radius value [threshold]
    if (args.size() < 6 || args.size() > 7) return usage();
    auto file = forge::lev::File::open(args[0]);
    forge::terrain::BooleanBrush brush;
    brush.centerX = std::stof(args[2]);
    brush.centerY = std::stof(args[3]);
    brush.radius = std::stof(args[4]);
    brush.value = args[5] != "0";
    if (args.size() == 7) brush.threshold = std::stof(args[6]);
    const size_t changed = preferred
        ? forge::terrain::applyPreferredPathBrush(file, brush)
        : forge::terrain::applyWalkabilityBrush(file, brush);
    file.save(args[1]);
    std::printf("painted %s=%d across %zu cells (center %.2f,%.2f radius %.2f threshold %.2f) -> %s\n",
                preferred ? "preferred-path" : "walkable", brush.value ? 1 : 0,
                changed, brush.centerX, brush.centerY, brush.radius, brush.threshold,
                args[1].c_str());
    return 0;
}

int levRebuildNav(const std::vector<std::string>& args) {
    // args: src out [--tng file]
    if (args.size() != 2 && args.size() != 4) return usage();
    std::optional<forge::tng::File> tng;
    if (args.size() == 4) {
        if (args[2] != "--tng") return usage();
        tng = forge::tng::File::parse(args[3]);
    }
    const auto lev = forge::lev::File::open(args[0]);
    const auto result = forge::navmesh::generateTerrain(
        lev, tng ? &*tng : nullptr);
    const std::filesystem::path outPath(args[1]);
    if (!outPath.parent_path().empty())
        std::filesystem::create_directories(outPath.parent_path());
    std::ofstream output(outPath, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("lev rebuild-nav: cannot write output");
    output.write(reinterpret_cast<const char*>(result.levBytes.data()),
                 static_cast<std::streamsize>(result.levBytes.size()));
    if (!output) throw std::runtime_error("lev rebuild-nav: failed writing output");
    std::printf("rebuilt terrain-only nav: %zu section(s), %zu records/section "
                "(%zu internal, %zu navigable, %zu blocked roots), %zu regions, "
                "%zu walkable cells, %zu island cells removed, %zu TNG anchors -> %s\n",
                result.sections, result.recordsPerSection, result.internalNodes,
                result.navigableLeaves, result.blockedRootRecords, result.regions,
                result.walkableCells, result.islandCellsRemoved, result.anchorCount,
                args[1].c_str());
    std::puts("warning: terrain-only single-layer nav excludes placed-object collision, "
              "switchable blockers, detailed areas, and stacked floors");
    return 0;
}

int levStitch(const std::vector<std::string>& args) {
    // args: target target-x target-y neighbor neighbor-x neighbor-y blend out
    if (args.size() != 8) return usage();
    auto targetFile = forge::lev::File::open(args[0]);
    const auto neighborFile = forge::lev::File::open(args[3]);
    auto target = forge::terrain::Heightfield::fromLev(targetFile);
    const auto neighbor = forge::terrain::Heightfield::fromLev(neighborFile);
    const forge::terrain::TilePlacement tp{std::stoi(args[1]), std::stoi(args[2])};
    const forge::terrain::TilePlacement np{std::stoi(args[4]), std::stoi(args[5])};
    const int blend = std::stoi(args[6]);
    const auto result = forge::terrain::stitchSharedBoundary(
        target, tp, neighbor, np, blend);
    if (result.edge == forge::terrain::SharedEdge::None) {
        throw std::invalid_argument("lev stitch: maps do not share an overlapping edge at those world coordinates");
    }
    target.writeTo(targetFile);
    targetFile.save(args[7]);
    std::printf("stitched %s edge over world coordinates %d..%d: %zu boundary, "
                "%zu total vertices changed -> %s\n",
                forge::terrain::edgeName(result.edge), result.overlapStart,
                result.overlapEnd, result.boundaryVertices,
                result.verticesChanged, args[7].c_str());
    return 0;
}

int levStitchRegion(const std::vector<std::string>& args) {
    // args: world levels-root target-level target-file blend out
    if (args.size() != 6) return usage();
    namespace fs = std::filesystem;
    const auto world = forge::wld::File::parse(args[0]);
    const auto* targetMap = world.findMap(args[2]);
    if (targetMap == nullptr) {
        throw std::invalid_argument("lev stitch-region: target level is not in the WLD");
    }
    auto targetFile = forge::lev::File::open(args[3]);
    auto target = forge::terrain::Heightfield::fromLev(targetFile);
    const forge::terrain::TilePlacement targetPlacement{targetMap->mapX,
                                                        targetMap->mapY};
    const int blend = std::stoi(args[4]);

    auto same = [](const std::string& a, const std::string& b) {
        return a.size() == b.size() && std::equal(
            a.begin(), a.end(), b.begin(), [](unsigned char x, unsigned char y) {
                return std::tolower(x) == std::tolower(y);
            });
    };
    auto contains = [&](const std::vector<std::string>& names,
                        const std::string& wanted) {
        return std::any_of(names.begin(), names.end(),
                           [&](const auto& name) { return same(name, wanted); });
    };

    // Limit discovery to the owning region(s) and their visible maps. This is
    // both semantically correct for region authoring and avoids opening every
    // LEV in FinalAlbion merely to learn its dimensions.
    std::set<std::string> candidates;
    size_t owningRegions = 0;
    for (const auto& region : world.regions()) {
        if (!contains(region.containsMaps, targetMap->levelName)) continue;
        ++owningRegions;
        candidates.insert(region.containsMaps.begin(), region.containsMaps.end());
        candidates.insert(region.seesMaps.begin(), region.seesMaps.end());
    }
    if (owningRegions == 0) {
        throw std::invalid_argument("lev stitch-region: target does not belong to a WLD region");
    }

    size_t stitched = 0, missing = 0, available = 0;
    size_t boundaryVertices = 0, changedVertices = 0;
    for (const auto& levelName : candidates) {
        if (same(levelName, targetMap->levelName)) continue;
        const auto* map = world.findMap(levelName);
        if (map == nullptr) continue;
        const fs::path neighborPath = fs::path(args[1]) / fs::path(levelName);
        if (!fs::is_regular_file(neighborPath)) {
            ++missing;
            continue;
        }
        ++available;
        const auto neighborFile = forge::lev::File::open(neighborPath);
        const auto neighbor = forge::terrain::Heightfield::fromLev(neighborFile);
        const auto result = forge::terrain::stitchSharedBoundary(
            target, targetPlacement, neighbor, {map->mapX, map->mapY}, blend);
        if (result.edge == forge::terrain::SharedEdge::None) continue;
        ++stitched;
        boundaryVertices += result.boundaryVertices;
        changedVertices += result.verticesChanged;
        std::printf("  %-5s <- %s (%zu boundary vertices)\n",
                    forge::terrain::edgeName(result.edge), levelName.c_str(),
                    result.boundaryVertices);
    }
    if (stitched == 0) {
        throw std::invalid_argument(
            "lev stitch-region: no available region LEVs share a physical edge; "
            "extract neighboring LEVs under levels-root and verify WLD placement");
    }
    target.writeTo(targetFile);
    targetFile.save(args[5]);
    std::printf("stitched %zu neighbors from %zu owning region(s): %zu boundary, "
                "%zu total vertices changed (%zu candidate LEVs available, %zu missing) -> %s\n",
                stitched, owningRegions, boundaryVertices, changedVertices,
                available, missing, args[5].c_str());
    return 0;
}

int wldInfo(const std::string& path, const std::string& detail) {
    const auto file = forge::wld::File::parse(path);
    std::printf("%s: %zu maps, %zu regions, %zu initial quests, MapUIDCount %d\n",
                file.source().c_str(), file.maps().size(), file.regions().size(),
                file.initialQuests().size(), file.mapUidCount());

    // Round-trip check comes free; report it so wld info doubles as a fidelity probe.
    std::FILE* raw = std::fopen(path.c_str(), "rb");
    if (raw != nullptr) {
        std::string original;
        char buffer[65536];
        size_t got;
        while ((got = std::fread(buffer, 1, sizeof(buffer), raw)) > 0) {
            original.append(buffer, got);
        }
        std::fclose(raw);
        std::printf("roundtrip: %s\n",
                    file.serialize() == original ? "byte-identical" : "MISMATCH");
    }

    if (detail == "maps") {
        for (const auto& map : file.maps()) {
            std::printf("%4d  (%5d,%5d)  uid=%-8u  sea=%d  %s\n", map.index,
                        map.mapX, map.mapY, map.mapUid, map.isSea ? 1 : 0,
                        map.levelName.c_str());
        }
    } else if (detail == "regions") {
        for (const auto& region : file.regions()) {
            std::printf("%4d  %-32s contains=%zu sees=%zu  minimap='%s'  %s\n", region.index,
                        region.regionName.c_str(), region.containsMaps.size(),
                        region.seesMaps.size(), region.minimapGraphic.c_str(),
                        region.displayName.c_str());
        }
    }
    return 0;
}

// --- Compiled world map (.bwd) ------------------------------------------------
// The engine loads FinalAlbion.bwd (not the text .wld) when UseCompiledWorldFiles
// is TRUE (retail default). These commands read/write the compiled form directly
// so custom levels can be authored without the old manual wld_bwd.py assembly.

int bwdInfo(const std::string& path, const std::string& detail) {
    const auto file = forge::bwd::File::parse(path);
    std::printf("%s: %zu maps (slots 1..%zu), %zu regions (slots 1..%zu)\n",
                path.c_str(), file.maps().size(), file.maps().size(),
                file.regions().size(), file.regions().size());
    if (detail == "maps") {
        int slot = 0;
        for (const auto& m : file.maps()) {
            ++slot;
            std::printf("%4d  (%5d,%5d)-(%5d,%5d)  uid=%-8llu used=%d prox=%d sea=%d f2=%d  %s\n",
                        slot, m.left, m.top, m.right, m.bottom,
                        static_cast<unsigned long long>(m.mapUid), m.used,
                        m.loadedOnProximity, m.isSea, m.flag2, m.scriptName.c_str());
        }
    } else if (detail == "regions") {
        int slot = 0;
        for (const auto& g : file.regions()) {
            ++slot;
            std::printf("%4d  %-28s def=%-20s mm='%s' contains=%zu sees=%zu exits=%zu\n",
                        slot, g.name.c_str(),
                        (g.regionDef.empty() ? "\"\"" : g.regionDef).c_str(),
                        g.minimapGraphic.c_str(),
                        g.contains.size(), g.sees.size(), g.exits.size());
            {
                float sc; std::memcpy(&sc, g.minimapScale, 4);
                if (sc != 1.0f || g.mmOffX || g.mmOffY || g.wmOffX || g.wmOffY)
                    std::printf("        mmScale=%.3f mmOff=(%d,%d) wmOff=(%d,%d)\n",
                                sc, g.mmOffX, g.mmOffY, g.wmOffX, g.wmOffY);
            }
        }
    }
    return 0;
}

int bwdRoundtrip(const std::string& path) {
    std::FILE* raw = std::fopen(path.c_str(), "rb");
    if (raw == nullptr) {
        std::fprintf(stderr, "cannot open %s\n", path.c_str());
        return 1;
    }
    std::vector<uint8_t> original;
    uint8_t buffer[65536];
    size_t got;
    while ((got = std::fread(buffer, 1, sizeof(buffer), raw)) > 0)
        original.insert(original.end(), buffer, buffer + got);
    std::fclose(raw);

    const auto file = forge::bwd::File::parse(original);
    const auto out = file.serialize();
    if (out == original) {
        std::printf("ROUNDTRIP OK: %s (%zu bytes byte-identical)\n", path.c_str(),
                    original.size());
        return 0;
    }
    size_t diff = 0;
    const size_t n = std::min(out.size(), original.size());
    while (diff < n && out[diff] == original[diff]) ++diff;
    std::fprintf(stderr, "ROUNDTRIP FAIL at byte %zu (out %zu vs src %zu)\n", diff,
                 out.size(), original.size());
    return 1;
}

// forge bwd set-region-name <in.bwd> <out.bwd> <oldName> <newName> [--minimap <graphic>]
// Rename a region and optionally set its minimap graphic. The region graphic is
// resolved per region NAME, so a repurposed retail region keeps showing the old
// area map until it is renamed. All other records stay byte-identical.
int bwdSetRegionName(const std::vector<std::string>& args) {
    if (args.size() < 4) {
        std::fprintf(stderr,
                     "usage: forge bwd set-region-name <in.bwd> <out.bwd> <oldName> "
                     "<newName> [--minimap <graphic>] [--def <regionDef>]\n");
        return 1;
    }
    std::string minimap, regionDef;
    bool haveMinimap = false, haveDef = false;
    for (size_t i = 4; i + 1 < args.size(); ++i) {
        if (args[i] == "--minimap") { minimap = args[i + 1]; haveMinimap = true; }
        if (args[i] == "--def") { regionDef = args[i + 1]; haveDef = true; }
    }
    try {
        auto f = forge::bwd::File::parse(args[0]);
        int hit = 0;
        for (size_t i = 0; i < f.regions().size(); ++i) {
            auto& r = f.regions()[i];
            if (lowered(r.name) != lowered(args[2])) continue;
            ++hit;
            r.name = args[3];
            r.displayName = args[3];
            if (haveMinimap) r.minimapGraphic = minimap;
            // Every retail region that shows a minimap has BOTH a RegionDef and a
            // minimapGraphic; every filler region has neither (141/141, measured
            // 2026-08-22). Setting only the graphic was not enough in-game.
            if (haveDef) r.regionDef = regionDef;
            std::printf("  region %zu renamed to '%s'%s\n", i + 1, args[3].c_str(),
                        haveMinimap ? (" minimap=" + minimap).c_str() : "");
        }
        if (!hit) {
            std::fprintf(stderr, "bwd set-region-name: no region named '%s'\n",
                         args[2].c_str());
            return 1;
        }
        f.write(args[1]);
        std::printf("set-region-name: %d region(s) -> %s\n", hit, args[1].c_str());
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "bwd set-region-name failed: %s\n", e.what());
        return 1;
    }
}

// forge bwd add-region <in.bwd> <out.bwd> <name> [<name>...]
// Append one or more EMPTY regions (no contained/seen maps, no exits). Used to
// push a real region away from the end of the region vector: the runtime region
// vector measures one short of the BWD region count, and a map owned by the
// last populated region resolves to 0 in GetRegionNumberMapIsIn (measured
// 2026-08-22, stage22). All other records stay byte-identical.
int bwdAddRegion(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        std::fprintf(stderr,
                     "usage: forge bwd add-region <in.bwd> <out.bwd> <name> [<name>...]\n");
        return 1;
    }
    try {
        auto f = forge::bwd::File::parse(args[0]);
        const size_t before = f.regions().size();
        for (size_t i = 2; i < args.size(); ++i) {
            for (const auto& r : f.regions())
                if (lowered(r.name) == lowered(args[i])) {
                    std::fprintf(stderr, "bwd add-region: region '%s' already exists\n",
                                 args[i].c_str());
                    return 1;
                }
            forge::bwd::Region r;   // struct defaults match a synthesized region
            r.name = args[i];
            r.displayName = args[i];
            f.regions().push_back(r);
            std::printf("  appended empty region %zu '%s'\n", f.regions().size(),
                        args[i].c_str());
        }
        f.write(args[1]);
        std::printf("add-region: %zu -> %zu regions -> %s\n", before,
                    f.regions().size(), args[1].c_str());
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "bwd add-region failed: %s\n", e.what());
        return 1;
    }
}

// forge bwd set-owner <in.bwd> <out.bwd> <levelName> --region <regionName>
// Retail map->region containment is a STRICT PARTITION: the BWD owner histogram
// over 398 retail maps is {1: 398}, i.e. every map is contained by exactly one
// region. This command enforces that for one map: it removes the map slot from
// every region's contains[] list and adds it to exactly one region, leaving
// sees[] untouched so an existing visibility attach still works. All other
// records stay byte-identical.
int bwdSetOwner(const std::vector<std::string>& args) {
    if (args.size() < 5 || args[3] != "--region") {
        std::fprintf(stderr,
                     "usage: forge bwd set-owner <in.bwd> <out.bwd> <levelName> "
                     "--region <regionName>\n");
        return 1;
    }
    const std::string levelStem = args[2];
    const std::string regionName = args[4];
    try {
        auto f = forge::bwd::File::parse(args[0]);

        // Resolve the 1-based map slot by level-name stem (case-insensitive).
        const std::string wanted = lowered(levelStem + ".lev");
        int slot = 0;
        for (size_t i = 0; i < f.maps().size(); ++i) {
            const std::string ln = lowered(f.maps()[i].levelName);
            if (ln.size() >= wanted.size() &&
                ln.compare(ln.size() - wanted.size(), wanted.size(), wanted) == 0) {
                if (slot) {
                    std::fprintf(stderr,
                                 "bwd set-owner: '%s' matches more than one map\n",
                                 levelStem.c_str());
                    return 1;
                }
                slot = static_cast<int>(i) + 1;
            }
        }
        if (!slot) {
            std::fprintf(stderr, "bwd set-owner: no map named '%s'\n",
                         levelStem.c_str());
            return 1;
        }

        int target = 0;
        for (size_t i = 0; i < f.regions().size(); ++i)
            if (lowered(f.regions()[i].name) == lowered(regionName))
                target = static_cast<int>(i) + 1;
        if (!target) {
            std::fprintf(stderr, "bwd set-owner: no region named '%s'\n",
                         regionName.c_str());
            return 1;
        }

        int removed = 0;
        for (size_t i = 0; i < f.regions().size(); ++i) {
            auto& c = f.regions()[i].contains;
            const size_t before = c.size();
            c.erase(std::remove(c.begin(), c.end(), slot), c.end());
            if (c.size() != before) {
                removed += static_cast<int>(before - c.size());
                std::printf("  removed slot %d from region %zu '%s'\n", slot,
                            i + 1, f.regions()[i].name.c_str());
            }
        }
        auto& tc = f.regions()[target - 1].contains;
        tc.push_back(slot);
        std::sort(tc.begin(), tc.end());
        tc.erase(std::unique(tc.begin(), tc.end()), tc.end());

        f.write(args[1]);
        std::printf("set-owner: map slot %d ('%s') owned by region %d '%s' "
                    "(%d prior containment(s) removed) -> %s\n",
                    slot, levelStem.c_str(), target, regionName.c_str(), removed,
                    args[1].c_str());
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "bwd set-owner failed: %s\n", e.what());
        return 1;
    }
}

// forge bwd add-level <in.bwd> <out.bwd> <name> <left> <top> <right> <bottom>
//   [--region <name>] [--def <regionDef>] [--proximity] [--sea]
//   [--host-region <name>] [--uid <n>]
// Reads an existing compiled world (the retail FinalAlbion.bwd the user already
// has — this is the untouched base world, NOT a clone of the new level), adds a
// freshly synthesized map + dedicated region for <name>, and writes out.bwd. All
// other records stay byte-identical.
int bwdAddLevel(const std::vector<std::string>& args) {
    if (args.size() < 7) {
        std::fprintf(stderr,
                     "usage: forge bwd add-level <in.bwd> <out.bwd> <name> "
                     "<left> <top> <right> <bottom> [--region <name>] "
                     "[--def <regionDef>] [--proximity] [--sea] "
                     "[--host-region <name>] [--uid <n>]\n");
        return 1;
    }
    forge::bwd::NewLevel spec;
    const std::string inPath = args[0];
    const std::string outPath = args[1];
    spec.levelName = args[2];
    spec.left = std::atoi(args[3].c_str());
    spec.top = std::atoi(args[4].c_str());
    spec.right = std::atoi(args[5].c_str());
    spec.bottom = std::atoi(args[6].c_str());
    for (size_t i = 7; i < args.size(); ++i) {
        if (args[i] == "--region" && i + 1 < args.size()) spec.regionName = args[++i];
        else if (args[i] == "--def" && i + 1 < args.size()) spec.regionDef = args[++i];
        else if (args[i] == "--proximity") spec.loadedOnProximity = true;
        else if (args[i] == "--sea") spec.isSea = true;
        else if (args[i] == "--host-region" && i + 1 < args.size())
            spec.alsoContainInRegion = args[++i];
        else if (args[i] == "--uid" && i + 1 < args.size())
            spec.mapUid = std::strtoull(args[++i].c_str(), nullptr, 10);
        else {
            std::fprintf(stderr, "unknown option %s\n", args[i].c_str());
            return 1;
        }
    }

    forge::bwd::File file;
    try {
        file = forge::bwd::File::parse(inPath);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "failed to read %s: %s\n", inPath.c_str(), e.what());
        return 1;
    }
    const int mapsBefore = file.mapCount();
    const int regionsBefore = file.regionCount();

    forge::bwd::AssignedSlots slots;
    try {
        slots = file.addLevel(spec);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "add-level failed: %s\n", e.what());
        return 1;
    }
    file.write(outPath);

    std::printf("added level '%s'\n", spec.levelName.c_str());
    std::printf("  map slot     %d  (%d -> %d maps)\n", slots.mapSlot, mapsBefore,
                file.mapCount());
    std::printf("  region slot  %d  (%d -> %d regions)  contains=[%d]\n",
                slots.regionSlot, regionsBefore, file.regionCount(), slots.mapSlot);
    std::printf("  box          (%d,%d)-(%d,%d)  uid=%llu\n", spec.left, spec.top,
                spec.right, spec.bottom,
                static_cast<unsigned long long>(file.maps()[slots.mapSlot - 1].mapUid));
    if (!spec.alsoContainInRegion.empty())
        std::printf("  also hosted in region '%s'\n", spec.alsoContainInRegion.c_str());
    std::printf("wrote %s\n", outPath.c_str());
    std::printf("NOTE: the new region is slot %d. Runtime probes confirm appended "
                "regions are loaded beyond vanilla's count, but dedicated-region "
                "membership still needs GetRegionNumberMapIsIn verification.\n",
                slots.regionSlot);
    return 0;
}

// forge wld compile <FinalAlbion.wld> <FinalAlbion_RT.stb> <out.bwd>
// PHASE 3: compile the compiled binary world map (.bwd) from the text .wld (the
// single source of truth) + STB map dimensions. No donor .bwd needed. Proven
// byte-exact against retail (tests/test_bwd.cpp).
int wldCompile(const std::string& wldPath, const std::string& stbPath,
               const std::string& outPath) {
    const auto wld = forge::wld::File::parse(wldPath);

    std::map<std::string, std::pair<int, int>> dimMap;
    try {
        const auto stb = forge::stb::Archive::open(stbPath);
        for (const auto& sm : stb.staticMaps()) {
            const auto rec = stb.readStaticMapRecord(sm);
            if (rec.size() >= forge::stbinfo::kInfoBlockSize) {
                const auto ib = forge::stbinfo::readInfoBlock(rec.data());
                dimMap[lowered(sm.levelName)] = {ib.mapWidth, ib.mapHeight};
            }
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "wld compile: cannot read STB dims: %s\n", e.what());
        return 1;
    }
    forge::bwd::DimSource dims = [&](const std::string& levelName, int& w, int& h) {
        auto it = dimMap.find(lowered(levelName));
        if (it == dimMap.end()) return false;
        w = it->second.first;
        h = it->second.second;
        return true;
    };

    forge::bwd::File f;
    try {
        f = forge::bwd::compileFromWld(wld, dims);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "wld compile failed: %s\n", e.what());
        return 1;
    }
    f.write(outPath);
    std::printf("compiled %zu maps / %zu regions -> %s (%zu bytes)\n",
                f.maps().size(), f.regions().size(), outPath.c_str(),
                f.serialize().size());
    return 0;
}

// forge world add-level <root> <name> <left> <top> <right> <bottom> [opts]
// The one-call, no-clone path: mutate BOTH the compiled FinalAlbion.bwd (what the
// engine loads) AND the text FinalAlbion.wld (the human-editable source of truth)
// under <root>/data/Levels so they stay coherent. Untouched records stay
// byte-identical in the .bwd. Pristine originals are backed up to <file>.bak.
int worldAddLevel(const std::vector<std::string>& args) {
    namespace fs = std::filesystem;
    if (args.size() < 6) {
        std::fprintf(stderr,
                     "usage: forge world add-level <root> <name> <left> <top> "
                     "<right> <bottom> [--region <n>] [--display <n>] "
                     "[--def <d>] [--minimap <g>] [--proximity] [--sea] "
                     "[--host-region <n>] [--uid <n>] [--no-backup]\n");
        return 1;
    }
    const fs::path root = args[0];
    forge::bwd::NewLevel spec;
    spec.levelName = args[1];
    spec.left = std::atoi(args[2].c_str());
    spec.top = std::atoi(args[3].c_str());
    spec.right = std::atoi(args[4].c_str());
    spec.bottom = std::atoi(args[5].c_str());
    std::string displayName;
    bool backup = true;
    for (size_t i = 6; i < args.size(); ++i) {
        if (args[i] == "--region" && i + 1 < args.size()) spec.regionName = args[++i];
        else if (args[i] == "--display" && i + 1 < args.size()) displayName = args[++i];
        else if (args[i] == "--def" && i + 1 < args.size()) spec.regionDef = args[++i];
        else if (args[i] == "--minimap" && i + 1 < args.size()) spec.minimapGraphic = args[++i];
        else if (args[i] == "--proximity") spec.loadedOnProximity = true;
        else if (args[i] == "--sea") spec.isSea = true;
        else if (args[i] == "--host-region" && i + 1 < args.size())
            spec.alsoContainInRegion = args[++i];
        else if (args[i] == "--uid" && i + 1 < args.size())
            spec.mapUid = std::strtoull(args[++i].c_str(), nullptr, 10);
        else if (args[i] == "--no-backup") backup = false;
        else {
            std::fprintf(stderr, "unknown option %s\n", args[i].c_str());
            return 1;
        }
    }
    spec.regionDisplayName = displayName;

    const fs::path levelsDir = root / "data" / "Levels";
    const fs::path bwdPath = levelsDir / "FinalAlbion.bwd";
    const fs::path wldPath = levelsDir / "FinalAlbion.wld";
    if (!fs::exists(bwdPath)) {
        std::fprintf(stderr, "world add-level: %s not found\n", bwdPath.string().c_str());
        return 1;
    }
    if (!fs::exists(wldPath)) {
        std::fprintf(stderr, "world add-level: %s not found\n", wldPath.string().c_str());
        return 1;
    }

    // Parse + mutate both before writing either, so a validation failure leaves
    // the install untouched.
    forge::bwd::File bwd;
    forge::wld::File wld = forge::wld::File::parse(wldPath);
    forge::bwd::AssignedSlots slots;
    try {
        bwd = forge::bwd::File::parse(bwdPath);
        slots = bwd.addLevel(spec);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "world add-level (bwd): %s\n", e.what());
        return 1;
    }
    const uint64_t uid = bwd.maps()[slots.mapSlot - 1].mapUid;

    const std::string wldLevelName = "FinalAlbion\\" + spec.levelName + ".lev";
    try {
        forge::wld::Map wm;
        wm.index = slots.mapSlot;
        wm.mapX = spec.left;
        wm.mapY = spec.top;
        wm.levelName = wldLevelName;
        wm.levelScriptName = spec.levelName;
        wm.mapUid = static_cast<uint32_t>(uid);
        wm.isSea = spec.isSea;
        wm.loadedOnPlayerProximity = spec.loadedOnProximity;
        wld.addMap(wm);

        forge::wld::Region wr;
        wr.index = slots.regionSlot;
        wr.regionName = spec.regionName.empty() ? spec.levelName : spec.regionName;
        wr.displayName = displayName.empty() ? wr.regionName : displayName;
        wr.regionDef = spec.regionDef;
        wr.containsMaps = {wldLevelName};
        wr.seesMaps = {wldLevelName};
        wld.addRegion(wr);

        if (!spec.alsoContainInRegion.empty())
            wld.addMapToRegion(spec.alsoContainInRegion, wldLevelName);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "world add-level (wld): %s\n", e.what());
        return 1;
    }

    // Both mutations succeeded — back up pristine originals (once) and write.
    if (backup) {
        for (const auto& p : {bwdPath, wldPath}) {
            const fs::path bak = p.string() + ".bak";
            std::error_code ec;
            if (!fs::exists(bak)) fs::copy_file(p, bak, ec);
        }
    }
    bwd.write(bwdPath);
    {
        const std::string text = wld.serialize();
        std::ofstream out(wldPath, std::ios::binary);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
    }

    std::printf("added level '%s' to %s\n", spec.levelName.c_str(),
                levelsDir.string().c_str());
    std::printf("  map slot     %d   box (%d,%d)-(%d,%d)  uid=%llu\n", slots.mapSlot,
                spec.left, spec.top, spec.right, spec.bottom,
                static_cast<unsigned long long>(uid));
    std::printf("  region slot  %d   name '%s'  contains=[%d]\n", slots.regionSlot,
                (spec.regionName.empty() ? spec.levelName : spec.regionName).c_str(),
                slots.mapSlot);
    std::printf("  updated FinalAlbion.bwd + FinalAlbion.wld%s\n",
                backup ? " (originals backed up to .bak)" : "");
    std::printf("NEXT: add the LEV+TNG to FinalAlbion.wad and an STB static-map "
                "record with matching origin (%d,%d) and size, then `forge "
                "validate %s`.\n",
                spec.left, spec.top, root.string().c_str());
    if (slots.regionSlot > 141)
        std::printf("NOTE: region %d is past the vanilla count (141); verify it "
                    "resolves at runtime before relying on the dedicated region.\n",
                    slots.regionSlot);
    return 0;
}

// forge world install-level <root> <name> <x> <y> --from-donor <Donor> [opts]
// The FULL-package one-shot: wire a new level into ALL FOUR containers in-place —
// WLD + BWD (register map+region), WAD (clone the donor LEV/TNG entries, optionally
// swapping in custom bytes), and STB (append the terrain chunk + an origin-patched
// common record). Cloning the donor's chunk reuses the donor GEOMETRY at the new
// origin, which renders correctly only if the chunk was retargeted first
// (`forge stb bake-heightfield`); pass that via --chunk. Pristine originals -> .bak.
int worldInstallLevel(const std::vector<std::string>& args) {
    namespace fs = std::filesystem;
    if (args.size() < 5) {
        std::fprintf(stderr,
                     "usage: forge world install-level <root> <name> <x> <y> "
                     "--from-donor <DonorLevelName> [--lev <file.lev>] "
                     "[--tng <file.tng>] [--chunk <retargeted_chunk.bin>] "
                     "[--region <n>] [--display <n>] [--def <d>] [--proximity] "
                     "[--sea] [--no-backup]\n");
        return 1;
    }
    const fs::path root = args[0];
    const std::string name = args[1];
    const int x = std::atoi(args[2].c_str());
    const int y = std::atoi(args[3].c_str());
    std::string donor, levFile, tngFile, chunkFile, regionName, displayName, regionDef;
    bool backup = true, proximity = false, isSea = false;
    for (size_t i = 4; i < args.size(); ++i) {
        if (args[i] == "--from-donor" && i + 1 < args.size()) donor = args[++i];
        else if (args[i] == "--lev" && i + 1 < args.size()) levFile = args[++i];
        else if (args[i] == "--tng" && i + 1 < args.size()) tngFile = args[++i];
        else if (args[i] == "--chunk" && i + 1 < args.size()) chunkFile = args[++i];
        else if (args[i] == "--region" && i + 1 < args.size()) regionName = args[++i];
        else if (args[i] == "--display" && i + 1 < args.size()) displayName = args[++i];
        else if (args[i] == "--def" && i + 1 < args.size()) regionDef = args[++i];
        else if (args[i] == "--proximity") proximity = true;
        else if (args[i] == "--sea") isSea = true;
        else if (args[i] == "--no-backup") backup = false;
        else { std::fprintf(stderr, "unknown option %s\n", args[i].c_str()); return 1; }
    }
    if (donor.empty()) {
        std::fprintf(stderr, "world install-level: --from-donor is required\n");
        return 1;
    }
    // Retail FinalAlbion map origins are all aligned to the 32-world-unit
    // terrain-cell grid.  Relocating a donor by a fractional grid offset leaves
    // its foreground patches off-grid: the container bounds still agree, but
    // decoded topology no longer rebuilds exactly (the 3328,2296 ForgeTest64
    // probe fell from 105/105 to 25/105 layers).  Reject this before touching
    // any of the four world containers.
    if (x % 32 != 0 || y % 32 != 0) {
        const auto nearestGrid = [](int value) {
            const int rem = value % 32;
            if (rem == 0) return value;
            const int lower = value - (rem < 0 ? rem + 32 : rem);
            return (value - lower < 16) ? lower : lower + 32;
        };
        std::fprintf(stderr,
                     "world install-level: origin (%d,%d) is off the retail "
                     "32-unit terrain grid; nearest aligned origin is (%d,%d). "
                     "No files changed.\n",
                     x, y, nearestGrid(x), nearestGrid(y));
        return 1;
    }

    const fs::path levelsDir = root / "data" / "Levels";
    const fs::path bwdPath = levelsDir / "FinalAlbion.bwd";
    const fs::path wldPath = levelsDir / "FinalAlbion.wld";
    const fs::path wadPath = levelsDir / "FinalAlbion.wad";
    const fs::path stbPath = levelsDir / "FinalAlbion_RT.stb";
    for (const auto& p : {bwdPath, wldPath, wadPath, stbPath}) {
        if (!fs::exists(p)) {
            std::fprintf(stderr, "world install-level: %s not found\n",
                         p.string().c_str());
            return 1;
        }
    }
    // Validate ALL optional input files up front, before mutating any container.
    // (This command writes WLD/BWD/WAD then STB; a missing input discovered
    // mid-run would otherwise leave the install partially written.)
    for (const auto& in : {levFile, tngFile, chunkFile}) {
        if (!in.empty() && !fs::exists(in)) {
            std::fprintf(stderr, "world install-level: input file not found: %s\n",
                         in.c_str());
            return 1;
        }
    }

    const std::string donorLev = "Data\\Levels\\FinalAlbion\\" + donor + ".lev";
    const std::string donorTng = "Data\\Levels\\FinalAlbion\\" + donor + ".tng";
    const std::string newLev = "Data\\Levels\\FinalAlbion\\" + name + ".lev";
    const std::string newTng = "Data\\Levels\\FinalAlbion\\" + name + ".tng";

    // Donor dimensions + terrain from the STB (the .wld carries no map size).
    int donorW = 0, donorH = 0;
    std::vector<uint8_t> donorChunk, commonRecord;
    try {
        const auto stb = forge::stb::Archive::open(stbPath);
        const forge::stb::StaticMap* sm = nullptr;
        for (const auto& m : stb.staticMaps())
            if (lowered(m.levelName) == lowered(donorLev)) { sm = &m; break; }
        if (!sm) {
            std::fprintf(stderr, "world install-level: donor '%s' not a static map\n",
                         donor.c_str());
            return 1;
        }
        commonRecord = stb.readStaticMapRecord(*sm);
        if (commonRecord.size() < forge::stbinfo::kInfoBlockSize) {
            std::fprintf(stderr, "world install-level: donor common record too small\n");
            return 1;
        }
        const auto ib = forge::stbinfo::readInfoBlock(commonRecord.data());
        donorW = ib.mapWidth;
        donorH = ib.mapHeight;
        const forge::stb::Entry* chunkEntry = stb.findEntry(donorLev);
        if (!chunkEntry) {
            std::fprintf(stderr, "world install-level: donor chunk entry missing\n");
            return 1;
        }
        donorChunk = stb.read(*chunkEntry);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "world install-level: STB read failed: %s\n", e.what());
        return 1;
    }
    const int right = x + donorW, bottom = y + donorH;

    // --- Staged, atomic write ------------------------------------------------
    // Build every container into a `.tmp` beside the original (same volume, so the
    // final rename is atomic), reading only from the untouched originals. Commit
    // (backup + rename) happens ONLY after all four temps are written, so any
    // failure mid-build leaves the install exactly as it was.
    const fs::path bwdTmp = bwdPath.string() + ".tmp";
    const fs::path wldTmp = wldPath.string() + ".tmp";
    const fs::path wadTmp = wadPath.string() + ".tmp";
    const fs::path wadTmp2 = wadPath.string() + ".tmp2";
    const fs::path stbTmp = stbPath.string() + ".tmp";
    const std::vector<fs::path> temps = {bwdTmp, wldTmp, wadTmp, wadTmp2, stbTmp};
    auto cleanup = [&]() {
        std::error_code ec;
        for (const auto& t : temps) fs::remove(t, ec);
    };

    forge::bwd::AssignedSlots slots;
    fs::path wadFinal;
    const bool chunkRetargeted = !chunkFile.empty();
    try {
        // 1) BWD + WLD -> temps.
        forge::bwd::NewLevel spec;
        spec.levelName = name;
        spec.left = x; spec.top = y; spec.right = right; spec.bottom = bottom;
        spec.loadedOnProximity = proximity;
        spec.isSea = isSea;
        spec.regionName = regionName;
        spec.regionDisplayName = displayName;
        spec.regionDef = regionDef;
        forge::bwd::File bwd = forge::bwd::File::parse(bwdPath);
        slots = bwd.addLevel(spec);
        const uint64_t uid = bwd.maps()[slots.mapSlot - 1].mapUid;
        bwd.write(bwdTmp);

        forge::wld::File wld = forge::wld::File::parse(wldPath);
        forge::wld::Map wm;
        wm.index = slots.mapSlot;
        wm.mapX = x; wm.mapY = y;
        wm.levelName = "FinalAlbion\\" + name + ".lev";
        wm.levelScriptName = name;
        wm.mapUid = static_cast<uint32_t>(uid);
        wm.isSea = isSea; wm.loadedOnPlayerProximity = proximity;
        wld.addMap(wm);
        forge::wld::Region wr;
        wr.index = slots.regionSlot;
        wr.regionName = regionName.empty() ? name : regionName;
        wr.displayName = displayName.empty() ? wr.regionName : displayName;
        wr.regionDef = regionDef;
        wr.containsMaps = {wm.levelName};
        wr.seesMaps = {wm.levelName};
        wld.addRegion(wr);
        {
            const std::string text = wld.serialize();
            std::ofstream out(wldTmp, std::ios::binary);
            if (!out) throw std::runtime_error("cannot write " + wldTmp.string());
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
            if (!out) throw std::runtime_error("write failed for " + wldTmp.string());
        }

        // 2) WAD: clone donor LEV/TNG -> wadTmp, then optional custom-byte repack.
        std::vector<forge::wad::CloneEntry> clones = {{donorLev, newLev},
                                                      {donorTng, newTng}};
        forge::wad::appendClonedEntries(wadPath, clones, wadTmp);
        std::map<std::string, std::vector<uint8_t>> repl;
        if (!levFile.empty()) repl[newLev] = readAllBytes(levFile);
        if (!tngFile.empty()) repl[newTng] = readAllBytes(tngFile);
        if (!repl.empty()) {
            forge::wad::repack(wadTmp, repl, wadTmp2);
            wadFinal = wadTmp2;
        } else {
            wadFinal = wadTmp;
        }

        // 3) STB: append chunk + origin-patched common record -> stbTmp.
        std::vector<uint8_t> chunk =
            chunkRetargeted ? readAllBytes(chunkFile) : donorChunk;
        auto ib = forge::stbinfo::readInfoBlock(commonRecord.data());
        ib.worldX = x;
        ib.worldY = y;
        ib.cameraMapBounds[0] = static_cast<float>(x);       // minX
        ib.cameraMapBounds[1] = static_cast<float>(y);       // minY
        ib.cameraMapBounds[3] = static_cast<float>(right);   // maxX
        ib.cameraMapBounds[4] = static_cast<float>(bottom);  // maxY
        const auto patched = forge::stbinfo::writeInfoBlock(ib);
        std::vector<uint8_t> record = commonRecord;
        std::copy(patched.begin(), patched.end(), record.begin());
        forge::stb::appendStaticMap(stbPath, stbTmp, newLev, newLev, chunk, record);
    } catch (const std::exception& e) {
        cleanup();
        std::fprintf(stderr,
                     "world install-level: staging failed — install UNTOUCHED: %s\n",
                     e.what());
        return 1;
    }

    // --- Commit: every temp built OK. Back up originals, then atomic-rename. ---
    try {
        if (backup) {
            for (const auto& p : {bwdPath, wldPath, wadPath, stbPath}) {
                const fs::path bak = p.string() + ".bak";
                std::error_code ec;
                if (!fs::exists(bak)) fs::copy_file(p, bak, ec);
            }
        }
        auto commit = [](const fs::path& tmp, const fs::path& orig) {
            std::error_code ec;
            fs::rename(tmp, orig, ec);
            if (ec) { // fallback for platforms where rename won't overwrite
                fs::remove(orig, ec);
                fs::rename(tmp, orig, ec);
                if (ec)
                    throw std::runtime_error("commit rename failed for " +
                                             orig.string() + ": " + ec.message());
            }
        };
        commit(bwdTmp, bwdPath);
        commit(wldTmp, wldPath);
        commit(wadFinal, wadPath);
        commit(stbTmp, stbPath);
        cleanup(); // drop the unused WAD temp (only one of wadTmp/wadTmp2 is used)
    } catch (const std::exception& e) {
        cleanup();
        std::fprintf(stderr,
                     "world install-level: COMMIT failed after staging: %s\n"
                     "  Restore from the .bak files if the install looks partial.\n",
                     e.what());
        return 1;
    }

    std::printf("installed level '%s' into all containers at %s\n", name.c_str(),
                levelsDir.string().c_str());
    std::printf("  map slot %d / region slot %d, box (%d,%d)-(%d,%d)\n",
                slots.mapSlot, slots.regionSlot, x, y, right, bottom);
    std::printf("  WLD+BWD registered; WAD cloned %s/%s%s; STB record appended\n",
                (name + ".lev").c_str(), (name + ".tng").c_str(),
                (levFile.empty() && tngFile.empty()) ? " (donor bytes)"
                                                     : " (custom bytes)");
    if (!chunkRetargeted)
        std::printf("  WARNING: STB reuses the DONOR chunk geometry at the new "
                    "origin — it will render mismatched/white unless retargeted. "
                    "Bake first (forge stb bake-heightfield) and pass --chunk.\n");
    std::printf("NEXT: `forge validate %s` (bounds+count cross-checks).\n",
                root.string().c_str());
    if (slots.regionSlot > 141)
        std::printf("NOTE: region %d is past the vanilla count (141); verify it "
                    "resolves at runtime before relying on the dedicated region.\n",
                    slots.regionSlot);
    return 0;
}

// forge world attach-map <root> <levelName> --region <hostRegion> [--no-sees] [--no-backup]
// Membership-only edit: add an EXISTING map's slot to an existing region's
// contains[] (+sees[]) in BOTH the .bwd and the text .wld. This is an explicit
// compatibility path for packages whose dedicated tail-region membership does
// not resolve. The runtime vector probe disproved a hard 141-region load cap;
// attaching to an existing region changes ownership and should not be automatic.
// WAD/STB are untouched.
int worldAttachMap(const std::vector<std::string>& args) {
    namespace fs = std::filesystem;
    if (args.size() < 4) {
        std::fprintf(stderr,
                     "usage: forge world attach-map <root> <levelName> --region "
                     "<hostRegion> [--no-sees] [--no-backup]\n");
        return 1;
    }
    const fs::path root = args[0];
    const std::string levelName = args[1];
    std::string host;
    bool sees = true, backup = true;
    for (size_t i = 2; i < args.size(); ++i) {
        if (args[i] == "--region" && i + 1 < args.size()) host = args[++i];
        else if (args[i] == "--no-sees") sees = false;
        else if (args[i] == "--no-backup") backup = false;
        else { std::fprintf(stderr, "unknown option %s\n", args[i].c_str()); return 1; }
    }
    if (host.empty()) {
        std::fprintf(stderr, "world attach-map: --region <hostRegion> is required\n");
        return 1;
    }

    const fs::path levelsDir = root / "data" / "Levels";
    const fs::path bwdPath = levelsDir / "FinalAlbion.bwd";
    const fs::path wldPath = levelsDir / "FinalAlbion.wld";
    if (!fs::exists(bwdPath) || !fs::exists(wldPath)) {
        std::fprintf(stderr, "world attach-map: %s or %s not found\n",
                     bwdPath.string().c_str(), wldPath.string().c_str());
        return 1;
    }

    const fs::path bwdTmp = bwdPath.string() + ".tmp";
    const fs::path wldTmp = wldPath.string() + ".tmp";
    auto cleanup = [&]() {
        std::error_code ec;
        fs::remove(bwdTmp, ec); fs::remove(wldTmp, ec);
    };

    int mapSlot = 0, hostSlot = 0;
    try {
        // --- BWD: find the map slot + host region, add membership -> temp.
        forge::bwd::File bwd = forge::bwd::File::parse(bwdPath);
        for (size_t i = 0; i < bwd.maps().size(); ++i) {
            const auto& m = bwd.maps()[i];
            std::string stem = m.levelName;
            const size_t slash = stem.find_last_of("\\/");
            if (slash != std::string::npos) stem = stem.substr(slash + 1);
            const size_t dot = stem.rfind('.');
            if (dot != std::string::npos) stem = stem.substr(0, dot);
            if (lowered(m.scriptName) == lowered(levelName) ||
                lowered(stem) == lowered(levelName)) { mapSlot = int(i + 1); break; }
        }
        if (mapSlot == 0)
            throw std::runtime_error("map '" + levelName + "' not found in .bwd");
        forge::bwd::Region* hostRegion = bwd.findRegion(host);
        if (!hostRegion)
            throw std::runtime_error("host region '" + host + "' not found in .bwd");
        for (size_t i = 0; i < bwd.regions().size(); ++i)
            if (&bwd.regions()[i] == hostRegion) { hostSlot = int(i + 1); break; }
        if (hostSlot > 141)
            std::printf("WARNING: host region %d ('%s') is itself past the vanilla "
                        "141 cap and will NOT resolve at runtime. Pick a <=141 "
                        "region.\n", hostSlot, host.c_str());
        auto addUnique = [](std::vector<int32_t>& v, int32_t s) {
            if (std::find(v.begin(), v.end(), s) == v.end()) v.push_back(s);
        };
        for (auto& region : bwd.regions()) {
            auto& contains = region.contains;
            contains.erase(std::remove(contains.begin(), contains.end(), mapSlot),
                           contains.end());
        }
        addUnique(hostRegion->contains, mapSlot);
        if (sees) addUnique(hostRegion->sees, mapSlot);
        bwd.write(bwdTmp);

        // --- WLD: mirror the membership -> temp.
        forge::wld::File wld = forge::wld::File::parse(wldPath);
        const std::string wldLevel = "FinalAlbion\\" + levelName + ".lev";
        wld.setMapOwner(host, wldLevel);
        wld.addMapToRegion(host, wldLevel, sees);
        const std::string text = wld.serialize();
        std::ofstream out(wldTmp, std::ios::binary);
        if (!out) throw std::runtime_error("cannot write " + wldTmp.string());
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out) throw std::runtime_error("write failed for " + wldTmp.string());
    } catch (const std::exception& e) {
        cleanup();
        std::fprintf(stderr,
                     "world attach-map: staging failed — install UNTOUCHED: %s\n",
                     e.what());
        return 1;
    }

    // --- Commit: backup + atomic rename both.
    try {
        if (backup) {
            for (const auto& p : {bwdPath, wldPath}) {
                const fs::path bak = p.string() + ".bak";
                std::error_code ec;
                if (!fs::exists(bak)) fs::copy_file(p, bak, ec);
            }
        }
        auto commit = [](const fs::path& tmp, const fs::path& orig) {
            std::error_code ec;
            fs::rename(tmp, orig, ec);
            if (ec) {
                fs::remove(orig, ec);
                fs::rename(tmp, orig, ec);
                if (ec) throw std::runtime_error("commit rename failed for " +
                                                 orig.string() + ": " + ec.message());
            }
        };
        commit(bwdTmp, bwdPath);
        commit(wldTmp, wldPath);
    } catch (const std::exception& e) {
        cleanup();
        std::fprintf(stderr, "world attach-map: COMMIT failed: %s\n", e.what());
        return 1;
    }

    std::printf("attached map '%s' (slot %d) to region '%s' (slot %d) — "
                "contains%s updated in .bwd + .wld\n",
                levelName.c_str(), mapSlot, host.c_str(), hostSlot,
                sees ? "/sees" : "");
    std::printf("NEXT: `forge validate %s`, then deploy .bwd+.wld and test the "
                "teleport — GetRegionNumberMapIsIn(%d) should now resolve to region "
                "%d.\n", root.string().c_str(), mapSlot, hostSlot);
    return 0;
}

int worldInspect(const std::string& root, bool jsonOutput) {
    const auto workspace = forge::worldworkspace::Workspace::open(root);
    size_t infos = 0, warnings = 0, errors = 0;
    for (const auto& issue : workspace.issues()) {
        switch (issue.severity) {
            case forge::worldworkspace::Severity::Info: ++infos; break;
            case forge::worldworkspace::Severity::Warning: ++warnings; break;
            case forge::worldworkspace::Severity::Error: ++errors; break;
        }
    }

    if (jsonOutput) {
        json levels = json::array();
        for (const auto& level : workspace.levels()) {
            levels.push_back({
                {"index", level.map.index},
                {"map_uid", level.map.mapUid},
                {"x", level.map.mapX},
                {"y", level.map.mapY},
                {"level", level.map.levelName},
                {"tng", level.tngName},
                {"lev_source", level.lev.effectiveSource()},
                {"tng_source", level.tng.effectiveSource()},
                {"static_map", level.inStaticBank},
                {"owning_regions", level.owningRegions},
                {"visible_from_regions", level.visibleFromRegions},
            });
        }
        json issues = json::array();
        for (const auto& issue : workspace.issues()) {
            issues.push_back({
                {"severity", forge::worldworkspace::severityName(issue.severity)},
                {"code", issue.code},
                {"subject", issue.subject},
                {"message", issue.message},
            });
        }
        std::puts(json{
            {"levels_directory", workspace.levelsDirectory().string()},
            {"clean", workspace.clean()},
            {"maps", workspace.levels().size()},
            {"regions", workspace.world().regions().size()},
            {"wad", {
                {"present", workspace.hasWad()},
                {"entries", workspace.wadEntryCount()},
            }},
            {"static_bank", {
                {"present", workspace.hasStaticBank()},
                {"maps", workspace.staticMapCount()},
            }},
            {"issue_counts", {
                {"info", infos}, {"warning", warnings}, {"error", errors},
            }},
            {"levels", std::move(levels)},
            {"issues", std::move(issues)},
        }.dump(2).c_str());
    } else {
        std::printf("world workspace: %s\n", workspace.levelsDirectory().string().c_str());
        std::printf("maps: %zu, regions: %zu\n", workspace.levels().size(),
                    workspace.world().regions().size());
        std::printf("wad: %s (%zu entries), static bank: %s (%zu maps)\n",
                    workspace.hasWad() ? "present" : "absent",
                    workspace.wadEntryCount(),
                    workspace.hasStaticBank() ? "present" : "absent",
                    workspace.staticMapCount());
        for (const auto& level : workspace.levels()) {
            std::printf("%4d  (%5d,%5d)  %-38s  LEV=%-14s TNG=%-14s STB=%s "
                        "regions=%zu/%zu\n",
                        level.map.index, level.map.mapX, level.map.mapY,
                        level.map.levelName.c_str(),
                        level.lev.effectiveSource().c_str(),
                        level.tng.effectiveSource().c_str(),
                        level.inStaticBank ? "yes" : "no",
                        level.owningRegions.size(), level.visibleFromRegions.size());
        }
        for (const auto& issue : workspace.issues()) {
            std::printf("[%s] %s: %s — %s\n",
                        forge::worldworkspace::severityName(issue.severity),
                        issue.code.c_str(), issue.subject.c_str(),
                        issue.message.c_str());
        }
        std::printf("issues: %zu info, %zu warnings, %zu errors — %s\n",
                    infos, warnings, errors,
                    workspace.clean() ? "LOADOUT COMPLETE" : "INCOMPLETE");
    }
    return workspace.clean() ? 0 : 1;
}

int levelCreateFromDonor(const std::vector<std::string>& args) {
    // root donor new x y out [options]
    if (args.size() < 6) return usage();
    forge::worldworkspace::CreateLevelRequest request;
    request.sourceRoot = args[0];
    request.donorLevelName = args[1];
    request.newLevelName = args[2];
    request.mapX = std::stoi(args[3]);
    request.mapY = std::stoi(args[4]);
    request.outputProject = args[5];
    bool jsonOutput = false;
    for (size_t i = 6; i < args.size(); ++i) {
        const std::string& option = args[i];
        auto next = [&]() -> const std::string& {
            if (++i >= args.size())
                throw std::invalid_argument(option + " requires a value");
            return args[i];
        };
        if (option == "--script") request.levelScriptName = next();
        else if (option == "--region") request.regionName = next();
        else if (option == "--not-visible") request.visibleFromRegion = false;
        else if (option == "--allow-origin-collision")
            request.allowOriginCollision = true;
        else if (option == "--no-stb") request.cloneStaticMap = false;
        else if (option == "--with-wad") request.cloneIntoWad = true;
        else if (option == "--json") jsonOutput = true;
        else throw std::invalid_argument(
            "level create-from-donor: unknown option " + option);
    }

    const auto result = forge::worldworkspace::createLevelFromDonor(request);
    if (jsonOutput) {
        std::puts(json{
            {"project", result.projectRoot.string()},
            {"world", result.worldPath.string()},
            {"lev", result.levPath.string()},
            {"tng", result.tngPath.string()},
            {"stb", result.staticMapCloned ? json(result.stbPath.string()) : json(nullptr)},
            {"wad", result.wadEntriesCloned ? json(result.wadPath.string()) : json(nullptr)},
            {"map_index", result.mapIndex},
            {"map_uid", result.mapUid},
            {"lev_bytes", result.levBytes},
            {"tng_bytes", result.tngBytes},
            {"static_map_cloned", result.staticMapCloned},
            {"wad_entries_cloned", result.wadEntriesCloned},
        }.dump(2).c_str());
    } else {
        std::printf("created donor-derived level project: %s\n",
                    result.projectRoot.string().c_str());
        std::printf("map: index=%d uid=%u at (%d,%d), LEV=%zu bytes, TNG=%zu bytes\n",
                    result.mapIndex, result.mapUid, request.mapX, request.mapY,
                    result.levBytes, result.tngBytes);
        std::printf("staged: %s\n        %s\n        %s\n",
                    result.worldPath.string().c_str(),
                    result.levPath.string().c_str(),
                    result.tngPath.string().c_str());
        if (result.staticMapCloned)
            std::printf("        %s (donor static map cloned and translated)\n",
                        result.stbPath.string().c_str());
        if (result.wadEntriesCloned)
            std::printf("        %s (%zu donor WAD entries cloned)\n",
                        result.wadPath.string().c_str(),
                        result.wadEntriesCloned);
    }
    return 0;
}

int catalogInfo(const std::string& path) {
    const auto catalog = forge::catalog::load(path);
    std::printf("catalog generated %s\ngame root: %s\n\ncounts:\n",
                catalog.generatedOn.c_str(), catalog.gameRoot.c_str());
    for (const auto& [key, value] : catalog.counts) {
        std::printf("  %-45s %lld\n", key.c_str(), static_cast<long long>(value));
    }
    std::printf("\nBIG banks:\n");
    for (const auto& bank : catalog.bigBanks) {
        std::printf("  %-45s %8lld entries  %12lld bytes\n", bank.bank.c_str(),
                    static_cast<long long>(bank.entries),
                    static_cast<long long>(bank.bytes));
    }
    std::printf("\neditor modules:\n");
    for (const auto& mod : catalog.editorModules) {
        std::printf("  %s\n", mod.id.c_str());
    }
    return 0;
}

std::string lowered(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string stripQuotes(std::string value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

int bankCatalog(const std::string& path, const std::string& filter,
                bool jsonOutput) {
    const auto catalog = forge::bankcatalog::load(path);
    const std::string needle = lowered(filter);
    json rows = json::array();
    size_t shown = 0;

    auto add = [&](const forge::bankcatalog::Route& route,
                   const forge::bankcatalog::Header& header,
                   const forge::bankcatalog::Symbol* symbol) {
        if (jsonOutput) {
            json row = {{"bank", route.bankSymbol},
                        {"big", route.bigPath},
                        {"source_bank", route.sourceBankPath},
                        {"header", route.headerPath}};
            if (symbol != nullptr) {
                row["symbol"] = symbol->name;
                row["enum"] = symbol->enumName;
                row["id"] = symbol->id;
            } else {
                row["named_symbols"] = header.namedCount;
                row["path_symbols"] = header.pathCount;
                row["max_id"] = header.maxId;
            }
            rows.push_back(std::move(row));
        } else if (symbol != nullptr) {
            std::printf("%-36s %6u  %-24s  %s  [%s]\n",
                        symbol->name.c_str(), symbol->id,
                        route.bankSymbol.c_str(), route.bigPath.c_str(),
                        route.headerPath.c_str());
        } else {
            std::printf("%-28s %-40s %-24s named=%zu path=%zu max=%u\n",
                        route.bankSymbol.c_str(), route.bigPath.c_str(),
                        route.headerPath.c_str(), header.namedCount,
                        header.pathCount, header.maxId);
        }
        ++shown;
    };

    if (filter.empty()) {
        for (const auto& route : catalog.routes) {
            const auto it = catalog.headers.find(route.headerPath);
            if (it != catalog.headers.end()) add(route, it->second, nullptr);
        }
    } else {
        const auto exact = catalog.resolve(filter);
        if (!exact.empty()) {
            for (const auto& match : exact)
                add(*match.route, *match.header, match.symbol);
        } else {
            for (const auto& route : catalog.routes) {
                const auto it = catalog.headers.find(route.headerPath);
                if (it == catalog.headers.end()) continue;
                for (const auto& symbol : it->second.symbols) {
                    if (!symbol.named ||
                        lowered(symbol.name).find(needle) == std::string::npos)
                        continue;
                    add(route, it->second, &symbol);
                    if (shown >= 500) break;
                }
                if (shown >= 500) break;
            }
        }
    }

    if (jsonOutput) {
        std::puts(json{{"root", catalog.root.string()},
                       {"routes", catalog.routes.size()},
                       {"headers", catalog.headers.size()},
                       {"named_symbols", catalog.namedSymbolCount()},
                       {"path_symbols", catalog.pathSymbolCount()},
                       {"filter", filter},
                       {"matches", rows}}
                      .dump(2).c_str());
    } else {
        std::printf("%zu rows; %zu routes, %zu headers, %zu named + %zu path symbols\n",
                    shown, catalog.routes.size(), catalog.headers.size(),
                    catalog.namedSymbolCount(), catalog.pathSymbolCount());
    }
    return shown == 0 && !filter.empty() ? 1 : 0;
}

std::string thingPosition(const forge::tng::Thing& thing) {
    const auto* physics = thing.findCtc("CTCPhysicsStandard");
    if (physics == nullptr) return "-";
    std::string x, y, z;
    for (const auto& prop : physics->properties) {
        if (prop.key == "PositionX") x = prop.value;
        if (prop.key == "PositionY") y = prop.value;
        if (prop.key == "PositionZ") z = prop.value;
    }
    if (x.empty()) return "-";
    return x + "," + y + "," + z;
}

forge::bin::File openDefs(const std::string& gameRoot, const std::string& bin) {
    namespace fs = std::filesystem;
    const fs::path defsDir = fs::path(gameRoot) / "data" / "CompiledDefs";
    return forge::bin::File::open(defsDir / "names.bin",
                                  defsDir / (bin.empty() ? "game.bin" : bin));
}

int defsList(const std::string& gameRoot, const std::string& bin,
             const std::string& filter) {
    const auto file = openDefs(gameRoot, bin);
    const std::string needle = lowered(filter);
    size_t shown = 0;
    for (size_t i = 0; i < file.entries().size(); ++i) {
        const auto& entry = file.entries()[i];
        if (!needle.empty() &&
            lowered(entry.name).find(needle) == std::string::npos &&
            lowered(entry.definition).find(needle) == std::string::npos) {
            continue;
        }
        std::printf("%6zu  %-28s %6zu bytes  %s\n", i, entry.definition.c_str(),
                    entry.data.size(), entry.name.c_str());
        ++shown;
    }
    std::printf("%zu of %zu entries\n", shown, file.entries().size());
    return 0;
}

// Print a definition type's field schema (name, type, game.bin serialization
// order, retail memory offset where known) from the RE-derived def_schema.json.
// With no def-type, lists all def types and their field counts.
int defsSchema(const std::string& schemaPath, const std::string& defType,
               bool jsonOutput) {
    const auto schema = forge::defschema::Schema::load(schemaPath);

    if (defType.empty()) {
        if (jsonOutput) {
            json rows = json::array();
            for (const auto& d : schema.defs()) {
                rows.push_back({{"def", d.name},
                                {"fields", d.fields.size()},
                                {"retail_offsets", d.retailOffsets}});
            }
            std::puts(json{{"defs", rows}, {"count", schema.defs().size()}}
                          .dump(2)
                          .c_str());
            return 0;
        }
        for (const auto& d : schema.defs()) {
            std::printf("%-40s %3zu fields%s\n", d.name.c_str(), d.fields.size(),
                        d.retailOffsets ? "  (+retail offsets)" : "");
        }
        std::printf("\n%zu definition types\n", schema.defs().size());
        return 0;
    }

    const auto* def = schema.find(defType);
    if (def == nullptr) {
        std::fprintf(stderr, "defs schema: no def type named %s\n",
                     defType.c_str());
        return 1;
    }

    if (jsonOutput) {
        json fields = json::array();
        for (const auto& f : def->fields) {
            json fj = {{"name", f.name}, {"type", f.type},
                       {"donor_offset", f.donorOffset}};
            if (f.retailOffset) fj["retail_offset"] = *f.retailOffset;
            fields.push_back(fj);
        }
        json out = {{"def", def->name},
                    {"transfer_addr", def->transferAddr},
                    {"retail_offsets", def->retailOffsets},
                    {"fields", fields}};
        if (def->prefixLen >= 0) out["prefix_len"] = def->prefixLen;
        std::puts(out.dump(2).c_str());
        return 0;
    }

    std::printf("%s  (%zu fields, game.bin serialization order)\n",
                def->name.c_str(), def->fields.size());
    if (def->prefixLen >= 0)
        std::printf("     untagged base-class prefix: %d bytes (explicit)\n",
                    def->prefixLen);
    if (def->fields.empty() && def->prefixLen >= 0)
        std::printf("     Transfer writes zero fields — payload is prefix only\n");
    std::printf("%-4s %-14s %-34s %s\n", "#", "type", "name", "retail");
    for (size_t i = 0; i < def->fields.size(); ++i) {
        const auto& f = def->fields[i];
        char retail[16] = "";
        if (f.retailOffset) std::snprintf(retail, sizeof(retail), "+0x%x", *f.retailOffset);
        std::printf("%-4zu %-14s %-34s %s\n", i, f.type.c_str(),
                    f.name.empty() ? "<unnamed>" : f.name.c_str(), retail);
    }
    return 0;
}

int defsShow(const std::string& gameRoot, const std::string& which,
             const std::string& bin) {
    const auto file = openDefs(gameRoot, bin);
    const forge::bin::Entry* entry = file.find(which);
    size_t index = 0;
    if (entry != nullptr) {
        index = static_cast<size_t>(entry - file.entries().data());
    } else {
        try {
            index = std::stoul(which);
        } catch (const std::exception&) {
            std::fprintf(stderr, "defs show: no entry named %s\n", which.c_str());
            return 1;
        }
        if (index >= file.entries().size()) {
            std::fprintf(stderr, "defs show: index %zu out of range\n", index);
            return 1;
        }
        entry = &file.entries()[index];
    }

    std::printf("entry %zu: %s / %s (%zu bytes, indexInDefinition %d)\n", index,
                entry->definition.c_str(), entry->name.c_str(),
                entry->data.size(), entry->indexInDefinition);
    for (size_t i = 0; i < entry->data.size(); i += 16) {
        std::printf("%06zx  ", i);
        for (size_t j = i; j < i + 16 && j < entry->data.size(); ++j) {
            std::printf("%02x ", entry->data[j]);
        }
        std::printf("\n");
    }
    return 0;
}

// Decode one compiled-def entry into named, typed field VALUES using the
// RE-derived field-tag hash (reflected CRC-32, seed 0) and def_schema field
// order. See libs/forgecore/include/forge/defdecode.hpp for the format.
// Corpus regression guard: decode every entry, report clean vs issues per bin.
// A "clean" entry resolves to a schema type and decodes with all tags matching
// and zero leftover. Guards against def_schema drift (target: 100%).
int defsDecodeAll(const std::string& gameRoot, const std::string& schemaPath,
                  const std::string& bin) {
    const auto file = openDefs(gameRoot, bin);
    const auto schema = forge::defschema::Schema::load(schemaPath);

    size_t total = 0, clean = 0, noSchema = 0, dirty = 0;
    std::map<std::string, size_t> noSchemaTypes;
    std::vector<std::string> dirtyExamples;
    for (const auto& e : file.entries()) {
        ++total;
        const auto* def =
            forge::defdecode::resolveType(schema, e.definition, e.data);
        if (def == nullptr) {
            ++noSchema;
            noSchemaTypes[e.definition]++;
            continue;
        }
        const auto d = forge::defdecode::decode(e.data, *def);
        if (d.allTagsOk && d.leftover == 0) {
            ++clean;
        } else {
            ++dirty;
            if (dirtyExamples.size() < 15)
                dirtyExamples.push_back(e.definition + " / " +
                    (e.name.empty() ? "<unnamed>" : e.name));
        }
    }

    std::printf("%s: %zu entries — %zu clean (%.2f%%), %zu no-schema, %zu dirty\n",
                bin.empty() ? "game.bin" : bin.c_str(), total, clean,
                total ? 100.0 * clean / total : 0.0, noSchema, dirty);
    if (!noSchemaTypes.empty()) {
        std::printf("  no-schema def types:\n");
        for (const auto& [t, n] : noSchemaTypes)
            std::printf("    %-32s %zu entries\n", t.c_str(), n);
    }
    for (const auto& ex : dirtyExamples)
        std::printf("    dirty: %s\n", ex.c_str());
    return (noSchema == 0 && dirty == 0) ? 0 : 1;
}

int defsDecode(const std::string& gameRoot, const std::string& schemaPath,
               const std::string& which, const std::string& bin, bool jsonOutput) {
    const auto file = openDefs(gameRoot, bin);
    const auto schema = forge::defschema::Schema::load(schemaPath);

    const forge::bin::Entry* entry = file.find(which);
    size_t index = 0;
    if (entry != nullptr) {
        index = static_cast<size_t>(entry - file.entries().data());
    } else {
        try {
            index = std::stoul(which);
        } catch (const std::exception&) {
            std::fprintf(stderr, "defs decode: no entry named %s\n", which.c_str());
            return 1;
        }
        if (index >= file.entries().size()) {
            std::fprintf(stderr, "defs decode: index %zu out of range\n", index);
            return 1;
        }
        entry = &file.entries()[index];
    }

    const auto* def =
        forge::defdecode::resolveType(schema, entry->definition, entry->data);
    if (def == nullptr) {
        std::fprintf(stderr, "defs decode: no schema decodes def type %s\n",
                     entry->definition.c_str());
        return 1;
    }
    const auto decoded = forge::defdecode::decode(entry->data, *def);

    if (jsonOutput) {
        json fields = json::array();
        for (const auto& f : decoded.fields) {
            char tag[16];
            std::snprintf(tag, sizeof(tag), "%08x", f.tag);
            fields.push_back({{"name", f.name},
                              {"type", f.type},
                              {"tag", tag},
                              {"tag_ok", f.tagOk},
                              {"value", forge::defdecode::formatValue(f)}});
        }
        std::puts(json{{"index", index},
                       {"definition", entry->definition},
                       {"name", entry->name},
                       {"prefix_len", decoded.prefixLen},
                       {"leftover", decoded.leftover},
                       {"all_tags_ok", decoded.allTagsOk},
                       {"fields", fields}}
                      .dump(2)
                      .c_str());
        return 0;
    }

    std::printf("entry %zu: %s / %s  (prefix %zu bytes, %zu fields%s)\n", index,
                entry->definition.c_str(), entry->name.c_str(),
                decoded.prefixLen, decoded.fields.size(),
                decoded.leftover ? "" : ", leftover 0");
    for (const auto& f : decoded.fields) {
        std::printf("  %-30s %-22s %s%s\n",
                    f.name.empty() ? "<unnamed>" : f.name.c_str(),
                    f.type.c_str(),
                    forge::defdecode::formatValue(f).c_str(),
                    f.tagOk ? "" : "   [TAG MISMATCH]");
    }
    if (decoded.leftover)
        std::printf("  (%zu trailing bytes undecoded)\n", decoded.leftover);
    return 0;
}

// --------------------------------------------------------------------------
// Custom terrain textures. Two halves, both in forge::terraintex:
//   * theme -> textures.big id resolution (LEV palette slot -> ENGINE_THEME def
//     index -> the six CEngineThemeDef texture fields),
//   * PNG import: the encoder is FableTLC tools/texture_build.py (shelled out
//     to on purpose - it is the byte-proven writer), and forge independently
//     validates the resulting entry against the on-disk texture contract.
// --------------------------------------------------------------------------
std::string defaultDefSchemaPath() {
    namespace fs = std::filesystem;
    for (const char* cand : {"docs/re_reference/def_schema.json",
                             "../docs/re_reference/def_schema.json",
                             "D:/Code/FableForge-legacy/docs/re_reference/def_schema.json"})
        if (fs::exists(cand)) return cand;
    return {};
}

void printTextureValidation(const forge::terraintex::TextureValidation& v,
                            const char* indent) {
    const auto& i = v.info;
    std::printf("%s%s %ux%u (authored %ux%u) mips=%u frames=%u transparency=%u\n",
                indent, forge::terraintex::pixelFormatName(i.pixelFormat),
                i.allocWidth, i.allocHeight, i.frameWidth, i.frameHeight,
                i.mipLevels, i.frameCount, i.transparency);
    std::printf("%sFrameDataSize=%u MipSize0=%u (measured mip-0 region %zu, "
                "%zu LZO chunk(s)), payload accounted %zu\n",
                indent, i.frameDataSize, i.mipSize0, v.mip0RegionSize,
                v.chunkCount, v.consumed);
    for (const auto& w : v.warnings)
        std::printf("%s  warning: %s\n", indent, w.c_str());
    for (const auto& e : v.errors)
        std::printf("%s  ERROR: %s\n", indent, e.c_str());
    std::printf("%s%s\n", indent,
                v.ok ? "OK - matches the retail texture-entry contract"
                     : "INVALID - does not match the retail contract");
}

int textureImport(const std::string& srcBig, const std::string& outBig,
                  const std::string& entry, const std::string& png, bool add,
                  const std::string& bank, const std::string& format,
                  const std::string& dims, const std::string& toolsDir,
                  const std::string& python) {
    forge::terraintex::ImportRequest req;
    req.png = png;
    req.srcBig = srcBig;
    req.outBig = outBig;
    req.entryName = entry;
    req.add = add;
    if (!bank.empty()) req.subBank = bank;
    if (!format.empty()) req.format = format;
    req.dims = dims;
    req.toolsDir = toolsDir;
    req.python = python;

    const auto result = forge::terraintex::importPng(req);
    std::printf("$ %s\n", result.command.c_str());
    if (!result.output.empty()) std::printf("%s", result.output.c_str());
    if (result.exitCode != 0) {
        std::fprintf(stderr, "texture import: writer exited %d\n", result.exitCode);
        return 1;
    }
    std::printf("entry [%s] %s id=%u in %s\n", result.bankName.c_str(),
                entry.c_str(), result.entryId, outBig.c_str());
    printTextureValidation(result.validation, "  ");
    if (!result.validation.ok) return 1;
    std::printf("reference this texture by id %u (ENGINE_THEME BaseTexture / "
                "BackgroundTexture / CliffBaseTexture ...)\n", result.entryId);
    return 0;
}

int textureVerify(const std::string& bigPath, const std::string& entry) {
    std::string bank;
    uint32_t id = 0;
    const auto v = forge::terraintex::validateBigEntry(bigPath, entry, &bank, &id);
    std::printf("[%s] %s id=%u\n", bank.c_str(), entry.c_str(), id);
    printTextureValidation(v, "  ");
    return v.ok ? 0 : 1;
}

int textureVerifyBank(const std::string& bigPath, const std::string& bankFilter,
                      const std::string& nameFilter, size_t limit) {
    const auto file = forge::big::File::open(bigPath);
    size_t checked = 0, ok = 0, skipped = 0;
    std::map<std::string, size_t> failReasons;
    for (const auto& bank : file.banks()) {
        if (!bankFilter.empty() && bank.name != bankFilter) continue;
        for (const auto& entry : bank.entries) {
            if (limit && checked >= limit) break;
            if (!nameFilter.empty() &&
                entry.name.find(nameFilter) == std::string::npos) continue;
            if (entry.subHeader.size() < 34 || entry.length == 0) { ++skipped; continue; }
            forge::terraintex::TextureInfo info;
            std::string err;
            forge::terraintex::parseTextureInfo(entry.subHeader.data(),
                                                entry.subHeader.size(), info, err);
            if (forge::terraintex::pixelFormatTail(info.pixelFormat) == nullptr) {
                ++skipped;  // formats this checker does not model
                continue;
            }
            const auto v = forge::terraintex::validateTextureEntry(
                entry.subHeader, file.entryData(entry));
            ++checked;
            if (v.ok) { ++ok; continue; }
            failReasons[v.errors.front()]++;
            if (failReasons.size() <= 10)
                std::printf("  FAIL %-40s %s\n", entry.name.c_str(),
                            v.errors.front().c_str());
        }
    }
    std::printf("%zu/%zu entries match the texture contract (%zu skipped: "
                "no Info / empty / unmodelled format)\n", ok, checked, skipped);
    return ok == checked ? 0 : 1;
}

int terrainThemes(const std::string& levPath, const std::string& gameRoot,
                  const std::string& schemaPath, const std::string& texturesBig,
                  bool asJson) {
    const auto level = forge::lev::File::open(levPath);
    const auto library =
        forge::terraintex::ThemeLibrary::loadFromRoot(gameRoot, schemaPath);
    const auto rows = forge::terraintex::resolvePalette(level, library);

    std::map<uint32_t, std::string> textureNames;
    if (!texturesBig.empty()) {
        const auto big = forge::big::File::open(texturesBig);
        for (const auto& bank : big.banks())
            for (const auto& e : bank.entries)
                textureNames[e.id] = e.name;
    }
    auto texName = [&](uint32_t id) -> std::string {
        if (id == 0) return "-";
        auto it = textureNames.find(id);
        return it == textureNames.end() ? std::string() : it->second;
    };

    if (asJson) {
        json out = json::array();
        for (const auto& r : rows) {
            json entry = {{"slot", r.slot},
                          {"palette_name", r.paletteName},
                          {"def_index", r.defIndex},
                          {"def_name", r.defName},
                          {"resolved", r.resolved},
                          {"name_matches", r.nameMatches},
                          {"cells", r.cellCount},
                          {"base", {r.textures.base[0], r.textures.base[1],
                                    r.textures.base[2]}},
                          {"cliff", {r.textures.cliff[0], r.textures.cliff[1],
                                     r.textures.cliff[2]}}};
            if (!textureNames.empty())
                entry["base_names"] = {texName(r.textures.base[0]),
                                       texName(r.textures.base[1]),
                                       texName(r.textures.base[2])};
            out.push_back(entry);
        }
        std::puts(json{{"level", levPath}, {"themes", out}}.dump(2).c_str());
        return 0;
    }

    std::printf("%s: %d ground-theme palette slot(s) in use\n",
                level.source().c_str(), int(rows.size()));
    std::printf("%-5s %-34s %-7s %-19s %-19s %s\n", "slot", "palette name",
                "def", "base tex triple", "cliff tex triple", "cells");
    size_t unresolved = 0, nameMismatch = 0;
    for (const auto& r : rows) {
        char base[32], cliff[32];
        std::snprintf(base, sizeof(base), "%u,%u,%u", r.textures.base[0],
                      r.textures.base[1], r.textures.base[2]);
        std::snprintf(cliff, sizeof(cliff), "%u,%u,%u", r.textures.cliff[0],
                      r.textures.cliff[1], r.textures.cliff[2]);
        std::printf("%-5d %-34s %-7u %-19s %-19s %zu%s\n", r.slot,
                    r.paletteName.c_str(), r.defIndex,
                    r.resolved ? base : "(unresolved)",
                    r.resolved ? cliff : "", r.cellCount,
                    r.resolved && !r.nameMatches ? "  [def name differs]" : "");
        if (!r.resolved) ++unresolved;
        else if (!r.nameMatches) ++nameMismatch;
    }
    if (!textureNames.empty()) {
        std::printf("\ntexture ids referenced by these themes:\n");
        std::set<uint32_t> ids;
        for (const auto& r : rows) {
            if (!r.resolved) continue;
            for (int f = 0; f < 3; ++f) {
                if (r.textures.base[f]) ids.insert(r.textures.base[f]);
                if (r.textures.cliff[f]) ids.insert(r.textures.cliff[f]);
            }
        }
        for (uint32_t id : ids) {
            const std::string name = texName(id);
            std::printf("  %-6u %s\n", id,
                        name.empty() ? "(id not present in this bank)" : name.c_str());
        }
    }
    std::printf("\n%zu resolved, %zu unresolved, %zu name mismatches\n",
                rows.size() - unresolved, unresolved, nameMismatch);
    if (unresolved > 0)
        std::printf("unresolved slots hold def indices this install does not map "
                    "to an ENGINE_THEME (a level authored against another def "
                    "bank).\n    fix: forge lev themerebase <game-root> "
                    "<level.lev>\n");
    return unresolved == 0 ? 0 : 1;
}

// Which bank entries no ENGINE_THEME references - the genuinely safe slots to
// overwrite with a custom terrain texture.
int textureFreeSlots(const std::string& gameRoot, const std::string& texturesBig,
                     const std::string& schemaPath, const std::string& prefix,
                     bool asJson) {
    const auto library =
        forge::terraintex::ThemeLibrary::loadFromRoot(gameRoot, schemaPath);
    const auto referenced = library.referencedTextures();
    const auto big = forge::big::File::open(texturesBig);

    struct Row { std::string bank, name; uint32_t id; bool used; };
    std::vector<Row> freeRows, usedRows;
    for (const auto& bank : big.banks()) {
        for (const auto& e : bank.entries) {
            if (!prefix.empty() && e.name.rfind(prefix, 0) != 0) continue;
            Row row{bank.name, e.name, e.id, referenced.count(e.id) != 0};
            (row.used ? usedRows : freeRows).push_back(std::move(row));
        }
    }
    if (asJson) {
        json rows = json::array();
        for (const auto& r : freeRows)
            rows.push_back({{"bank", r.bank}, {"id", r.id}, {"name", r.name}});
        std::puts(json{{"theme_count", library.themes().size()},
                       {"referenced_textures", referenced.size()},
                       {"matched_prefix", freeRows.size() + usedRows.size()},
                       {"free", rows}}
                      .dump(2)
                      .c_str());
        return 0;
    }
    std::printf("%zu ENGINE_THEME defs reference %zu distinct texture ids\n",
                library.themes().size(), referenced.size());
    std::printf("entries matching \"%s\": %zu, of which %zu are referenced by a "
                "theme and %zu are FREE\n", prefix.c_str(),
                freeRows.size() + usedRows.size(), usedRows.size(), freeRows.size());
    for (const auto& r : freeRows)
        std::printf("  free  [%s] %-6u %s\n", r.bank.c_str(), r.id, r.name.c_str());
    return 0;
}

// Load a compiled-defs bin, save it to a scratch dir, reload, and compare
// entry-for-entry. The writer re-chunks/re-compresses so byte identity is not
// expected; semantic identity is the correctness bar.
int defsRoundtrip(const std::string& gameRoot, const std::string& bin) {
    namespace fs = std::filesystem;
    const auto original = openDefs(gameRoot, bin);

    const fs::path outDir = fs::temp_directory_path() / "forge_defs_roundtrip";
    fs::create_directories(outDir);
    const fs::path namesOut = outDir / "names.bin";
    const fs::path binOut = outDir / "roundtrip.bin";
    original.save(namesOut, binOut);

    const auto reloaded = forge::bin::File::open(namesOut, binOut);
    if (reloaded.entries().size() != original.entries().size()) {
        std::fprintf(stderr, "roundtrip FAILED: %zu entries became %zu\n",
                     original.entries().size(), reloaded.entries().size());
        return 1;
    }
    for (size_t i = 0; i < original.entries().size(); ++i) {
        const auto& a = original.entries()[i];
        const auto& b = reloaded.entries()[i];
        if (a.definition != b.definition || a.name != b.name ||
            a.data != b.data) {
            std::fprintf(stderr,
                         "roundtrip FAILED at entry %zu (%s / %s): "
                         "definition/name/payload mismatch\n",
                         i, a.definition.c_str(), a.name.c_str());
            return 1;
        }
    }
    fs::remove_all(outDir);
    std::printf("defs roundtrip OK: %zu entries semantically identical\n",
                original.entries().size());
    return 0;
}

// Record-level diff of a compiled-defs bin between two game roots — the atom of
// mod conflict detection. Diffing a mod against the base yields the mod's change
// set (added / removed / modified defs); intersecting change sets across mods
// finds conflicts (see docs/MOD_PACKS.md). Named defs are keyed by name; unnamed
// sub-defs are summarized by count since they have no stable cross-file key.
int defsDiff(const std::string& rootA, const std::string& rootB,
             const std::string& bin, bool jsonOutput) {
    const auto a = openDefs(rootA, bin);
    const auto b = openDefs(rootB, bin);

    std::map<std::string, const forge::bin::Entry*> byNameA, byNameB;
    size_t unnamedA = 0, unnamedB = 0;
    for (const auto& e : a.entries()) {
        if (e.name.empty()) ++unnamedA; else byNameA[e.name] = &e;
    }
    for (const auto& e : b.entries()) {
        if (e.name.empty()) ++unnamedB; else byNameB[e.name] = &e;
    }

    std::vector<std::string> added, removed, changed;
    for (const auto& [name, eb] : byNameB) {
        auto it = byNameA.find(name);
        if (it == byNameA.end()) {
            added.push_back(name);
        } else if (it->second->data != eb->data ||
                   it->second->definition != eb->definition) {
            changed.push_back(name);
        }
    }
    for (const auto& [name, ea] : byNameA) {
        if (byNameB.find(name) == byNameB.end()) removed.push_back(name);
    }
    std::sort(added.begin(), added.end());
    std::sort(removed.begin(), removed.end());
    std::sort(changed.begin(), changed.end());

    if (jsonOutput) {
        std::puts(json{{"bin", bin.empty() ? "game.bin" : bin},
                       {"added", added},
                       {"removed", removed},
                       {"changed", changed},
                       {"summary",
                        {{"added", added.size()},
                         {"removed", removed.size()},
                         {"changed", changed.size()},
                         {"unnamed_a", unnamedA},
                         {"unnamed_b", unnamedB}}}}
                      .dump(2)
                      .c_str());
        return 0;
    }

    const auto dump = [](const char* label, const std::vector<std::string>& v) {
        std::printf("%s: %zu\n", label, v.size());
        for (const auto& n : v) std::printf("  %s\n", n.c_str());
    };
    std::printf("diff %s: A=%zu entries, B=%zu entries\n",
                bin.empty() ? "game.bin" : bin.c_str(), a.entries().size(),
                b.entries().size());
    dump("added (in B, not A)", added);
    dump("removed (in A, not B)", removed);
    dump("changed (payload differs)", changed);
    if (unnamedA != unnamedB) {
        std::printf("unnamed sub-defs: A=%zu B=%zu (count changed)\n",
                    unnamedA, unnamedB);
    }
    std::printf("\nchange set: %zu added, %zu removed, %zu changed\n",
                added.size(), removed.size(), changed.size());
    return 0;
}

// Read-only multi-mod conflict report across all compiled-defs bins. For each
// bin, diffs every mod against base and intersects change sets to find records
// multiple mods touch (true conflicts, load-order-decided) — the inspect-before-
// merge view. No files are written. mods are in load order.
int modsAnalyze(const std::string& baseRoot,
                const std::vector<std::string>& modRoots, bool jsonOutput) {
    namespace fs = std::filesystem;
    const char* bins[] = {"game.bin", "script.bin", "frontend.bin"};

    json binReports = json::array();
    size_t grandChanges = 0, grandConflicts = 0;

    for (const char* bin : bins) {
        forge::bin::File base = openDefs(baseRoot, bin);
        std::map<std::string, std::vector<uint8_t>> baseData;
        for (const auto& e : base.entries()) {
            if (!e.name.empty()) baseData[e.name] = e.data;
        }

        // Per-mod change set + who-touched tracking.
        std::map<std::string, std::vector<std::string>> touchedBy;  // record -> mods
        std::map<std::string, size_t> modFootprint;
        size_t binChanges = 0;

        for (const auto& modRoot : modRoots) {
            const std::string modName = fs::path(modRoot).filename().string();
            const auto mod = openDefs(modRoot, bin);
            size_t foot = 0;
            for (const auto& me : mod.entries()) {
                if (me.name.empty()) continue;
                auto bit = baseData.find(me.name);
                const bool isAdd = bit == baseData.end();
                const bool isChange = !isAdd && me.data != bit->second;
                if (!isAdd && !isChange) continue;
                touchedBy[me.name].push_back(modName);
                ++foot;
                ++binChanges;
            }
            modFootprint[modName] = foot;
        }

        std::vector<std::pair<std::string, std::vector<std::string>>> conflicts;
        for (const auto& [rec, mods] : touchedBy) {
            if (mods.size() > 1) conflicts.emplace_back(rec, mods);
        }
        std::sort(conflicts.begin(), conflicts.end());
        grandChanges += binChanges;
        grandConflicts += conflicts.size();

        if (jsonOutput) {
            json foot = json::object();
            for (const auto& [m, f] : modFootprint) foot[m] = f;
            json crows = json::array();
            for (const auto& [rec, mods] : conflicts) {
                crows.push_back({{"record", rec}, {"mods", mods},
                                 {"winner", mods.back()}});
            }
            binReports.push_back({{"bin", bin},
                                  {"footprints", foot},
                                  {"changes", binChanges},
                                  {"conflicts", crows}});
        } else {
            std::printf("=== %s ===\n", bin);
            for (const auto& [m, f] : modFootprint) {
                std::printf("  %-24s %zu changes\n", m.c_str(), f);
            }
            std::printf("  conflicts (touched by >1 mod): %zu\n",
                        conflicts.size());
            for (const auto& [rec, mods] : conflicts) {
                std::string chain;
                for (size_t i = 0; i < mods.size(); ++i) {
                    chain += (i ? " -> " : "") + mods[i];
                }
                std::printf("    %-42s %s\n", rec.c_str(), chain.c_str());
            }
        }
    }

    if (jsonOutput) {
        std::puts(json{{"mods", modRoots},
                       {"bins", binReports},
                       {"summary",
                        {{"total_changes", grandChanges},
                         {"total_conflicts", grandConflicts}}}}
                      .dump(2)
                      .c_str());
    } else {
        std::printf("\nacross %zu mods: %zu total changes, %zu conflicts "
                    "(load order: winner last)\n",
                    modRoots.size(), grandChanges, grandConflicts);
    }
    return 0;
}

// Merge several mods' compiled-defs changes over a vanilla base into one bin.
// Each mod is diffed against base to get its change set (added/changed records);
// non-conflicting changes from all mods are composed, and same-record conflicts
// are resolved by load order (later mod wins) with a reported conflict list.
// A picks file (record<TAB>winner, one per line; winner = a mod's folder name or
// "vanilla") OVERRIDES load order PER CONFLICT — this is what the GUI's per-row
// winner picker produces, letting users resolve each conflict however they want.
// Proven on Aeon x Lost Content: ~98% of changes merge, ~2% are true conflicts.
// Writes <outDir>/names.bin + <outDir>/<bin>. mods are in load order.
int mergeDefs(const std::string& baseRoot, const std::string& outDir,
              const std::string& bin, const std::vector<std::string>& modRoots,
              const std::string& picksPath, const std::string& fieldSchemaPath,
              bool jsonOutput) {
    namespace fs = std::filesystem;

    // Per-conflict overrides: record -> winning mod ("vanilla" = keep base).
    const std::map<std::string, std::string> picks = loadPicks(picksPath);

    // Optional field-level schema. When present, records that >1 mod changes are
    // composed PER FIELD (different fields auto-merge; only same-field/differing
    // edits are true conflicts) instead of one mod winning the whole record.
    std::optional<forge::defschema::Schema> schema;
    if (!fieldSchemaPath.empty())
        schema = forge::defschema::Schema::load(fieldSchemaPath);

    auto merged = openDefs(baseRoot, bin);

    // Snapshot base payloads by name BEFORE mutating, for change detection.
    std::map<std::string, std::vector<uint8_t>> baseData;
    std::map<std::string, std::string> baseDef;  // name -> definition type
    std::map<std::string, size_t> nameToIndex;
    for (size_t i = 0; i < merged.entries().size(); ++i) {
        const auto& e = merged.entries()[i];
        if (e.name.empty()) continue;
        baseData[e.name] = e.data;
        baseDef[e.name] = e.definition;
        nameToIndex[e.name] = i;
    }

    // Gather every mod version that changes/adds each record, in load order.
    struct Version { std::string mod, definition; std::vector<uint8_t> data; };
    std::map<std::string, std::vector<Version>> changes;
    std::vector<std::string> order;  // first-seen order for stable output
    for (const auto& modRoot : modRoots) {
        const std::string modName = fs::path(modRoot).filename().string();
        const auto mod = openDefs(modRoot, bin);
        for (const auto& me : mod.entries()) {
            if (me.name.empty()) continue;
            auto bit = baseData.find(me.name);
            const bool isAdd = bit == baseData.end();
            const bool isChange = !isAdd && me.data != bit->second;
            if (!isAdd && !isChange) continue;  // identical to vanilla
            if (changes.find(me.name) == changes.end()) order.push_back(me.name);
            changes[me.name].push_back({modName, me.definition, me.data});
        }
    }

    // A residual conflict = a record (or a field within one) decided by load
    // order / a pick because >1 mod changed the same thing incompatibly.
    struct Conflict {
        std::string record;
        std::vector<std::string> mods;    // touching mods, in order
        std::string winner;               // whole-record winner, or "field-merged"
        bool fieldMerged = false;
        std::vector<forge::defdecode::FieldMerge::Conflict> fieldConflicts;
    };
    std::vector<Conflict> conflicts;
    size_t applied = 0, addedTotal = 0, overridden = 0;
    size_t fieldMergedRecords = 0, autoFields = 0, fieldConflictParts = 0;

    for (const auto& name : order) {
        const auto& vers = changes[name];
        auto idx = nameToIndex.find(name);
        const bool inBase = idx != nameToIndex.end();
        auto pk = picks.find(name);
        const std::string pick = pk != picks.end() ? pk->second : std::string();

        // Single mod touches it, and it exists (or is a lone add): apply as-is.
        if (vers.size() == 1) {
            const auto& v = vers.front();
            if (inBase) merged.setEntryData(idx->second, v.data);
            else { nameToIndex[name] = merged.addEntry(v.definition, name, v.data);
                   ++addedTotal; }
            ++applied;
            continue;
        }

        // >1 mod touches it -> conflict candidate. Try field-level merge first.
        Conflict c;
        c.record = name;
        for (const auto& v : vers) c.mods.push_back(v.mod);

        bool didFieldMerge = false;
        if (schema && inBase) {
            const auto* def = forge::defdecode::resolveType(
                *schema, baseDef[name], baseData[name]);
            bool aligned = def != nullptr;
            for (const auto& v : vers)
                if (v.definition != baseDef[name]) aligned = false;
            if (aligned) {
                const auto baseDec = forge::defdecode::decode(baseData[name], *def);
                std::vector<std::pair<std::string, forge::defdecode::Decoded>> dvs;
                for (const auto& v : vers)
                    dvs.emplace_back(v.mod, forge::defdecode::decode(v.data, *def));
                const auto fm = forge::defdecode::mergeFields(baseDec, dvs, pick);
                if (fm.ok) {
                    merged.setEntryData(idx->second, fm.payload);
                    ++applied;
                    ++fieldMergedRecords;
                    autoFields += fm.autoMerged.size();
                    fieldConflictParts += fm.conflicts.size();
                    c.fieldMerged = true;
                    c.fieldConflicts = fm.conflicts;
                    c.winner = fm.conflicts.empty() ? "field-merged (clean)"
                                                    : "field-merged";
                    didFieldMerge = true;
                }
            }
        }

        if (!didFieldMerge) {
            // Whole-record resolution by pick override, else load order.
            const bool keepBase = pick == "vanilla" || pick == "base" ||
                                  pick == "none" || pick == "-";
            const Version* win = nullptr;
            if (!pick.empty() && !keepBase) {
                for (const auto& v : vers) if (v.mod == pick) win = &v;
            }
            if (win == nullptr && !keepBase) win = &vers.back();  // load order
            if (win != nullptr) {
                if (inBase) merged.setEntryData(idx->second, win->data);
                else { nameToIndex[name] = merged.addEntry(win->definition, name,
                                                           win->data);
                       ++addedTotal; }
                ++applied;
                c.winner = win->mod;
            } else {
                c.winner = "vanilla";  // keepBase: leave base untouched
            }
            if (!pick.empty()) ++overridden;
        }

        // Only surface records with residual (undecided-by-content) conflicts:
        // whole-record conflicts, or field-merges that still had a field clash.
        if (!c.fieldMerged || !c.fieldConflicts.empty()) conflicts.push_back(c);
    }

    // Write a game-root layout (<outDir>/data/CompiledDefs/) so the result is a
    // drop-in overlay: directly diffable, stageable, and installable.
    const fs::path defsOut = fs::path(outDir) / "data" / "CompiledDefs";
    fs::create_directories(defsOut);
    merged.save(defsOut / "names.bin", defsOut / bin);

    if (jsonOutput) {
        json conflictRows = json::array();
        for (const auto& c : conflicts) {
            json fc = json::array();
            for (const auto& f : c.fieldConflicts)
                fc.push_back({{"field", f.part}, {"mods", f.mods},
                              {"winner", f.winner}});
            conflictRows.push_back({{"record", c.record}, {"mods", c.mods},
                                    {"winner", c.winner},
                                    {"field_merged", c.fieldMerged},
                                    {"field_conflicts", fc},
                                    {"overridden", picks.count(c.record) > 0}});
        }
        std::puts(json{{"bin", bin},
                       {"mods", modRoots},
                       {"field_level", schema.has_value()},
                       {"conflicts", conflictRows},
                       {"summary",
                        {{"applied", applied},
                         {"added", addedTotal},
                         {"record_conflicts", conflicts.size()},
                         {"field_merged_records", fieldMergedRecords},
                         {"auto_merged_fields", autoFields},
                         {"field_conflicts", fieldConflictParts},
                         {"overridden", overridden},
                         {"merged_entries", merged.entries().size()}}}}
                      .dump(2)
                      .c_str());
        return 0;
    }

    std::printf("merged %zu mod(s) into %s%s: %zu changes applied (%zu new "
                "records)\n",
                modRoots.size(), bin.c_str(),
                schema ? " [field-level]" : "", applied, addedTotal);
    if (schema) {
        const size_t wholeRecordConflicts =
            std::count_if(conflicts.begin(), conflicts.end(),
                          [](const Conflict& c) { return !c.fieldMerged; });
        std::printf("field merge: %zu records composed per-field, %zu fields "
                    "auto-merged, %zu field conflicts, %zu whole-record "
                    "conflicts (undecodable/add-add)\n",
                    fieldMergedRecords, autoFields, fieldConflictParts,
                    wholeRecordConflicts);
    }
    if (!conflicts.empty()) {
        std::printf("\nresidual conflicts (winner in [], * = pick override):\n");
        for (const auto& c : conflicts) {
            std::string chain;
            for (size_t i = 0; i < c.mods.size(); ++i)
                chain += (i ? ", " : "") + c.mods[i];
            const bool ov = picks.count(c.record) > 0;
            std::printf("  %-40s %s -> [%s]%s\n", c.record.c_str(),
                        chain.c_str(), c.winner.c_str(), ov ? " *" : "");
            for (const auto& f : c.fieldConflicts) {
                std::string fchain;
                for (size_t i = 0; i < f.mods.size(); ++i)
                    fchain += (i ? ", " : "") + f.mods[i];
                std::printf("      field %-28s %s -> [%s]\n", f.part.c_str(),
                            fchain.c_str(), f.winner.c_str());
            }
        }
    }
    std::printf("\nwrote %s/data/CompiledDefs/{names.bin,%s} (%zu entries)\n",
                outDir.c_str(), bin.c_str(), merged.entries().size());
    return 0;
}

int defsFamilies(const std::string& gameRoot) {
    const auto file = openDefs(gameRoot, "");
    const auto families = forge::bin::decodeObjectFamilies(file);
    for (const auto& family : families) {
        float total = 0.0f;
        for (const auto& member : family.members) total += member.weight;
        std::printf("%s (entry %u, %zu members, weight %g)\n",
                    family.name.c_str(), family.entryIndex,
                    family.members.size(), total);
        for (const auto& member : family.members) {
            const std::string& memberName =
                member.objectIndex < file.entries().size()
                    ? file.entries()[member.objectIndex].name
                    : std::string();
            std::printf("  %6g  %s\n", member.weight,
                        member.objectIndex == 0 ? "(no drop)"
                                                : memberName.c_str());
        }
    }
    const auto rewards = forge::bin::decodeContainerRewards(file);
    std::printf("\n%zu object families, %zu container-reward defs\n",
                families.size(), rewards.size());
    return 0;
}

struct ScriptRef {
    std::string level;
    std::string uid;
    std::string type;
    std::string definition;
    std::string scriptName;
    std::string scriptData;
    std::string position;
    std::string matchKind;
};

std::string scriptPrefix(const std::string& scriptName) {
    const size_t underscore = scriptName.find('_');
    if (underscore == std::string::npos || underscore == 0) return "OTHER";
    return scriptName.substr(0, underscore);
}

std::vector<std::string> cutsceneCommands(const forge::bin::Entry& entry);

std::vector<std::string> commandTokens(const std::string& command) {
    std::vector<std::string> tokens;
    size_t start = std::string::npos;
    for (size_t i = 0; i <= command.size(); ++i) {
        const bool word =
            i < command.size() &&
            (std::isalnum(static_cast<unsigned char>(command[i])) ||
             command[i] == '_');
        if (word && start == std::string::npos) {
            start = i;
        } else if (!word && start != std::string::npos) {
            tokens.push_back(command.substr(start, i - start));
            start = std::string::npos;
        }
    }
    return tokens;
}

int scriptRefs(const std::string& gameRoot, const std::string& levelFilter,
               bool jsonOutput) {
    namespace fs = std::filesystem;
    const fs::path tngDir = fs::path(gameRoot) / "data" / "Levels" / "FinalAlbion";
    if (!fs::exists(tngDir)) {
        std::fprintf(stderr, "script refs: %s not found\n", tngDir.string().c_str());
        return 1;
    }

    std::set<std::string> scriptDefNames;
    std::set<std::string> cutsceneRefs;
    try {
        const auto scripts = openDefs(gameRoot, "script.bin");
        for (const auto& entry : scripts.entries()) {
            if (!entry.name.empty()) scriptDefNames.insert(lowered(entry.name));
            if (entry.definition != "CCutsceneDef") continue;
            for (const auto& command : cutsceneCommands(entry)) {
                for (const auto& token : commandTokens(command)) {
                    if (token.find('_') != std::string::npos) {
                        cutsceneRefs.insert(lowered(token));
                    }
                }
            }
        }
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "script refs: warning: cannot open script.bin: %s\n",
                     ex.what());
    }

    std::vector<fs::path> paths;
    const std::string filter = lowered(levelFilter);
    for (const auto& entry : fs::directory_iterator(tngDir)) {
        if (lowered(entry.path().extension().string()) != ".tng") continue;
        if (!filter.empty() &&
            lowered(entry.path().stem().string()).find(filter) == std::string::npos) {
            continue;
        }
        paths.push_back(entry.path());
    }
    std::sort(paths.begin(), paths.end());

    std::vector<ScriptRef> refs;
    std::map<std::string, size_t> byPrefix;
    std::map<std::string, size_t> byMatch;
    for (const auto& path : paths) {
        const auto tng = forge::tng::File::parse(path);
        const std::string level = path.stem().string();
        for (const auto& thing : tng.things()) {
            std::string name = thing.scriptName();
            if (name.empty()) continue;
            name = stripQuotes(name);
            if (lowered(name) == "null") continue;

            std::string match = "-";
            if (scriptDefNames.count(lowered(name)) > 0) {
                match = "script.bin";
            } else if (cutsceneRefs.count(lowered(name)) > 0) {
                match = "cutscene-ref";
            } else if (name.rfind("CS_", 0) == 0) {
                match = "cutscene-name";
            } else if (name.rfind("CAM_", 0) == 0) {
                match = "camera-marker";
            } else if (name.rfind("MK_", 0) == 0) {
                match = "marker";
            }

            refs.push_back({level,
                            thing.find("UID").value_or("-"),
                            thing.type,
                            thing.definitionType(),
                            name,
                            stripQuotes(thing.find("ScriptData").value_or("-")),
                            thingPosition(thing),
                            match});
            ++byPrefix[scriptPrefix(name)];
            ++byMatch[match];
        }
    }

    if (jsonOutput) {
        json rows = json::array();
        for (const auto& ref : refs) {
            rows.push_back({{"level", ref.level},
                            {"uid", ref.uid},
                            {"type", ref.type},
                            {"script", ref.scriptName},
                            {"script_data", ref.scriptData},
                            {"definition", ref.definition},
                            {"link", ref.matchKind},
                            {"position", ref.position}});
        }
        json prefixes = json::object();
        for (const auto& [prefix, count] : byPrefix) prefixes[prefix] = count;
        json links = json::object();
        for (const auto& [match, count] : byMatch) links[match] = count;
        std::puts(json{{"refs", rows},
                       {"summary",
                        {{"refs", refs.size()},
                         {"tngs", paths.size()},
                         {"prefixes", prefixes},
                         {"links", links}}}}
                      .dump(2)
                      .c_str());
        return 0;
    }

    std::printf("%-28s %-22s %-12s %-30s %-28s %-14s %s\n", "level", "uid",
                "type", "script", "definition", "link", "position");
    for (const auto& ref : refs) {
        std::printf("%-28s %-22s %-12s %-30s %-28s %-14s %s\n",
                    ref.level.c_str(), ref.uid.c_str(), ref.type.c_str(),
                    ref.scriptName.c_str(), ref.definition.c_str(),
                    ref.matchKind.c_str(), ref.position.c_str());
    }
    std::printf("\n%zu script refs across %zu TNGs\n", refs.size(), paths.size());
    std::printf("prefixes:");
    for (const auto& [prefix, count] : byPrefix) {
        std::printf(" %s=%zu", prefix.c_str(), count);
    }
    std::printf("\nlinks:");
    for (const auto& [match, count] : byMatch) {
        std::printf(" %s=%zu", match.c_str(), count);
    }
    std::printf("\n");
    return 0;
}

std::vector<std::string> cutsceneCommands(const forge::bin::Entry& entry) {
    if (entry.definition != "CCutsceneDef") return {};
    return forge::cutscene::decodeCommands(entry.data);
}

std::string commandVerb(const std::string& command) {
    const size_t space = command.find(' ');
    const size_t comma = command.find(',');
    size_t end = std::min(space == std::string::npos ? command.size() : space,
                          comma == std::string::npos ? command.size() : comma);
    return command.substr(0, end);
}

// Curated corrections for the shipped "dead" cutscene commands (verbs the retail
// engine silently ignores) whose intent is an unambiguous typo of a verb that
// DOES exist. Applied by `forge script fixup`. Deliberately excludes ambiguous
// cases that may be intentional line-disables (nop, dGamePause, DoScriptName,
// AnimationPhysics, bare ClearCommands) or where the correct target is unclear
// (Hero.SetScriptedMode -> Add or Remove?): those are reported, never auto-fixed.
struct VerbFix {
    std::string_view from;  // dead action (.Verb) or global verb token
    std::string_view to;    // corrected token
    bool entity;            // true = match on the `.Verb` slice of NAME.Verb
};
constexpr std::array<VerbFix, 4> kVerbFixes = {{
    {".ClearActions", ".ClearCommands", true},
    {".SetEntityMaxWalkingSpeed", ".EntitySetMaxWalkingSpeed", true},
    {"GamePAuse", "GamePause", false},
    {"Fadeout", "FadeOut", false},
}};

// If the dead verb token has a safe correction, return the fixed token
// (preserving the entity name for scoped commands), else empty.
std::string suggestVerbFix(const std::string& token) {
    const size_t dot = token.find('.');
    if (dot != std::string::npos) {
        const std::string action = token.substr(dot);
        for (const auto& fix : kVerbFixes) {
            if (fix.entity && action == fix.from) {
                return token.substr(0, dot) + std::string(fix.to);
            }
        }
        return {};
    }
    for (const auto& fix : kVerbFixes) {
        if (!fix.entity && token == fix.from) return std::string(fix.to);
    }
    return {};
}

// Flag cutscene commands the retail engine would not dispatch (unknown verbs),
// plus commands that only run by prefix-match accident (verb token longer than
// the native verb it matched, e.g. `DoScriptFrame1` -> `DoScriptFrame`). These
// are Lionhead's shipped typos and become modder footguns; the GUI/validator
// surfaces them before a mod ships them again.
int scriptValidate(const std::string& gameRoot, const std::string& filter,
                   bool jsonOutput) {
    const auto scripts = openDefs(gameRoot, "script.bin");
    const std::string needle = lowered(filter);

    struct Flag {
        size_t count = 0;
        std::set<std::string> cutscenes;
        std::string example;
        std::string matched;  // native verb hit by prefix, empty if none
    };
    std::map<std::string, Flag> deadVerbs;     // no native handler at all
    std::map<std::string, Flag> looseVerbs;    // runs only via prefix slop
    size_t totalCommands = 0;
    size_t scannedCutscenes = 0;

    for (const auto& entry : scripts.entries()) {
        if (entry.definition != "CCutsceneDef") continue;
        if (!needle.empty() &&
            lowered(entry.name).find(needle) == std::string::npos) {
            continue;
        }
        ++scannedCutscenes;
        for (const auto& command : cutsceneCommands(entry)) {
            const std::string verb = commandVerb(command);
            if (verb.empty()) continue;
            ++totalCommands;
            const std::string_view native = forge::cutscene::resolveVerb(verb);
            if (native.empty()) {
                auto& flag = deadVerbs[verb];
                ++flag.count;
                flag.cutscenes.insert(entry.name);
                if (flag.example.empty()) flag.example = command;
                continue;
            }
            // A native match shorter than the compared token means dispatch
            // only succeeded because strncmp stopped at the native verb length.
            if (native.size() != forge::cutscene::comparedTokenLength(verb)) {
                auto& flag = looseVerbs[verb];
                ++flag.count;
                flag.cutscenes.insert(entry.name);
                if (flag.example.empty()) flag.example = command;
                flag.matched.assign(native);
            }
        }
    }

    if (jsonOutput) {
        const auto toRows = [](const std::map<std::string, Flag>& src) {
            json rows = json::array();
            for (const auto& [verb, flag] : src) {
                rows.push_back({{"verb", verb},
                                {"count", flag.count},
                                {"cutscenes", flag.cutscenes.size()},
                                {"example", flag.example},
                                {"matched", flag.matched},
                                {"fix", suggestVerbFix(verb)}});
            }
            return rows;
        };
        std::puts(json{{"dead", toRows(deadVerbs)},
                       {"loose", toRows(looseVerbs)},
                       {"summary",
                        {{"cutscenes", scannedCutscenes},
                         {"commands", totalCommands},
                         {"dead_verbs", deadVerbs.size()},
                         {"loose_verbs", looseVerbs.size()},
                         {"native_verbs", forge::cutscene::kNativeVerbs.size()}}}}
                      .dump(2)
                      .c_str());
        return 0;
    }

    std::printf("scanned %zu cutscenes, %zu commands against %zu native verbs\n\n",
                scannedCutscenes, totalCommands,
                forge::cutscene::kNativeVerbs.size());
    std::printf("dead commands (no handler, silently ignored): %zu\n",
                deadVerbs.size());
    for (const auto& [verb, flag] : deadVerbs) {
        const std::string fix = suggestVerbFix(verb);
        std::printf("  %-28s x%-5zu in %zu scenes   e.g. %s%s\n", verb.c_str(),
                    flag.count, flag.cutscenes.size(), flag.example.c_str(),
                    fix.empty() ? "" : ("   [fixup -> " + fix + "]").c_str());
    }
    std::printf("\nloose commands (run only by prefix-match accident): %zu\n",
                looseVerbs.size());
    for (const auto& [verb, flag] : looseVerbs) {
        std::printf("  %-28s -> %-20s x%-5zu   e.g. %s\n", verb.c_str(),
                    flag.matched.c_str(), flag.count, flag.example.c_str());
    }
    if (deadVerbs.empty() && looseVerbs.empty()) {
        std::printf("\nall commands dispatch cleanly.\n");
    }
    return 0;
}

// Reference listing of the 184 native cutscene verbs with dispatch order and a
// heuristic argument hint (see cutscene_verb_info.hpp for the argHint caveats).
int scriptVerbs(const std::string& filter, bool jsonOutput) {
    const std::string needle = lowered(filter);
    std::vector<forge::cutscene::VerbInfo> rows(
        forge::cutscene::kVerbInfo.begin(), forge::cutscene::kVerbInfo.end());
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
        return a.dispatchOrder < b.dispatchOrder;
    });

    if (jsonOutput) {
        json out = json::array();
        for (const auto& v : rows) {
            if (!needle.empty() &&
                lowered(std::string(v.name)).find(needle) == std::string::npos) {
                continue;
            }
            out.push_back({{"verb", v.name},
                           {"order", v.dispatchOrder},
                           {"arg_count", v.argCount},
                           {"arg_hint", v.argHint},
                           {"scoped", !v.name.empty() && v.name.front() == '.'}});
        }
        std::puts(json{{"verbs", out},
                       {"count", forge::cutscene::kVerbInfo.size()}}
                      .dump(2)
                      .c_str());
        return 0;
    }

    std::printf("%-5s %-30s %4s  %s\n", "ord", "verb", "args", "arg hint");
    size_t shown = 0;
    for (const auto& v : rows) {
        if (!needle.empty() &&
            lowered(std::string(v.name)).find(needle) == std::string::npos) {
            continue;
        }
        std::printf("%-5d %-30.*s %4d  %.*s\n", v.dispatchOrder,
                    static_cast<int>(v.name.size()), v.name.data(), v.argCount,
                    static_cast<int>(v.argHint.size()), v.argHint.data());
        ++shown;
    }
    std::printf("\n%zu of %zu native verbs (dispatch order; entity-scoped verbs "
                "start with '.')\n",
                shown, forge::cutscene::kVerbInfo.size());
    std::printf("arg hint is heuristic: float args may show as str, some "
                "entity verbs show 0 args.\n");
    return 0;
}

// Correct the shipped "dead" cutscene typos (see kVerbFixes) in script.bin.
// Dry-run by default: lists every fixable command occurrence. With --write it
// rewrites script.bin (semantic round-trip) after backing up the originals.
int scriptFixup(const std::string& gameRoot, bool write) {
    namespace fs = std::filesystem;
    const fs::path defsDir = fs::path(gameRoot) / "data" / "CompiledDefs";
    const fs::path namesPath = defsDir / "names.bin";
    const fs::path binPath = defsDir / "script.bin";
    auto file = forge::bin::File::open(namesPath, binPath);

    struct Change {
        std::string cutscene;
        std::string before;
        std::string after;
    };
    std::vector<Change> changes;
    size_t editedEntries = 0;

    for (size_t i = 0; i < file.entries().size(); ++i) {
        const auto& entry = file.entries()[i];
        if (entry.definition != "CCutsceneDef") continue;
        auto commands = cutsceneCommands(entry);
        bool touched = false;
        for (auto& command : commands) {
            const std::string token = std::string(forge::cutscene::verbToken(command));
            if (!forge::cutscene::resolveVerb(token).empty()) continue;  // dispatches
            const std::string fixedToken = suggestVerbFix(token);
            if (fixedToken.empty()) continue;
            const std::string before = command;
            command = fixedToken + command.substr(token.size());
            changes.push_back({entry.name, before, command});
            touched = true;
        }
        if (touched) {
            ++editedEntries;
            if (write) {
                file.setEntryData(
                    i, forge::cutscene::encodeCommands(entry.data, commands));
            }
        }
    }

    for (const auto& c : changes) {
        std::printf("%-34s %s\n    -> %s\n", c.cutscene.c_str(),
                    c.before.c_str(), c.after.c_str());
    }
    std::printf("\n%zu fixable commands in %zu cutscenes\n", changes.size(),
                editedEntries);

    if (!write) {
        std::printf("dry run: pass --write to apply (backs up script.bin/"
                    "names.bin to *.forgebak first).\n");
        return 0;
    }
    if (changes.empty()) {
        std::printf("nothing to write.\n");
        return 0;
    }

    for (const auto& p : {namesPath, binPath}) {
        const fs::path backup = fs::path(p).replace_extension(
            p.extension().string() + ".forgebak");
        if (!fs::exists(backup)) fs::copy_file(p, backup);
    }
    file.save(namesPath, binPath);
    std::printf("wrote %s (%zu entries edited). Reverting: restore the "
                "*.forgebak files.\n",
                binPath.string().c_str(), editedEntries);
    return 0;
}

int scriptCutscenes(const std::string& gameRoot, const std::string& filter,
                    bool jsonOutput) {
    const auto scripts = openDefs(gameRoot, "script.bin");
    const std::string needle = lowered(filter);
    size_t shown = 0;
    size_t total = 0;
    std::map<std::string, size_t> verbs;

    json rows = json::array();
    if (!jsonOutput) {
        std::printf("%-5s %-42s %6s  %s\n", "idx", "cutscene", "cmds",
                    "first command");
    }
    for (size_t i = 0; i < scripts.entries().size(); ++i) {
        const auto& entry = scripts.entries()[i];
        if (entry.definition != "CCutsceneDef") continue;
        ++total;
        if (!needle.empty() && lowered(entry.name).find(needle) == std::string::npos) {
            continue;
        }

        const auto commands = cutsceneCommands(entry);
        for (const auto& command : commands) {
            const std::string verb = commandVerb(command);
            if (!verb.empty()) ++verbs[verb];
        }
        if (jsonOutput) {
            rows.push_back({{"index", i},
                            {"name", entry.name},
                            {"bytes", entry.data.size()},
                            {"command_count", commands.size()},
                            {"first_command",
                             commands.empty() ? "" : commands.front()}});
        } else {
            std::printf("%-5zu %-42s %6zu  %s\n", i, entry.name.c_str(),
                        commands.size(),
                        commands.empty() ? "" : commands.front().c_str());
        }
        ++shown;
    }

    std::vector<std::pair<std::string, size_t>> sorted(verbs.begin(), verbs.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });

    if (jsonOutput) {
        json top = json::array();
        for (const auto& [verb, count] : sorted) {
            top.push_back({{"verb", verb}, {"count", count}});
        }
        std::puts(json{{"cutscenes", rows},
                       {"summary",
                        {{"shown", shown},
                         {"total", total},
                         {"top_commands", top}}}}
                      .dump(2)
                      .c_str());
        return 0;
    }

    std::printf("\n%zu of %zu cutscenes shown\n", shown, total);
    std::printf("top commands:");
    size_t emitted = 0;
    for (const auto& [verb, count] : sorted) {
        if (emitted++ >= 16) break;
        std::printf(" %s=%zu", verb.c_str(), count);
    }
    std::printf("\n");
    return 0;
}

int scriptCutscene(const std::string& gameRoot, const std::string& which,
                   bool jsonOutput) {
    const auto scripts = openDefs(gameRoot, "script.bin");
    const forge::bin::Entry* entry = scripts.find(which);
    size_t index = 0;
    if (entry != nullptr) {
        index = static_cast<size_t>(entry - scripts.entries().data());
    } else {
        try {
            index = std::stoul(which);
        } catch (const std::exception&) {
            std::fprintf(stderr, "script cutscene: no entry named %s\n",
                         which.c_str());
            return 1;
        }
        if (index >= scripts.entries().size()) {
            std::fprintf(stderr, "script cutscene: index %zu out of range\n",
                         index);
            return 1;
        }
        entry = &scripts.entries()[index];
    }

    if (entry->definition != "CCutsceneDef") {
        std::fprintf(stderr, "script cutscene: entry %zu is %s, not CCutsceneDef\n",
                     index, entry->definition.c_str());
        return 1;
    }

    const auto commands = cutsceneCommands(*entry);
    if (jsonOutput) {
        json rows = json::array();
        for (size_t i = 0; i < commands.size(); ++i) {
            rows.push_back({{"index", i},
                            {"verb", commandVerb(commands[i])},
                            {"command", commands[i]}});
        }
        std::puts(json{{"entry_index", index},
                       {"name", entry->name},
                       {"bytes", entry->data.size()},
                       {"commands", rows}}
                      .dump(2)
                      .c_str());
        return 0;
    }

    std::printf("entry %zu: %s (%zu bytes, %zu commands)\n", index,
                entry->name.c_str(), entry->data.size(), commands.size());
    for (size_t i = 0; i < commands.size(); ++i) {
        std::printf("%04zu  %-28s %s\n", i, commandVerb(commands[i]).c_str(),
                    commands[i].c_str());
    }
    return 0;
}

// Aggregate verb histogram across every CCutsceneDef in script.bin: total
// occurrences, how many cutscenes use the verb, and one example command. This
// is the GUI bootstrap payload for timeline labeling and verb pickers.
int scriptCommandStats(const std::string& gameRoot, bool jsonOutput) {
    const auto scripts = openDefs(gameRoot, "script.bin");

    struct VerbStat {
        size_t count = 0;
        size_t cutscenes = 0;
        std::string example;
    };
    std::map<std::string, VerbStat> stats;
    // `NAME.Verb` commands are entity-scoped; the action table collapses them to
    // `.Verb` so the real dispatch surface (global verbs + scoped actions) is
    // visible instead of one row per named cutscene actor.
    std::map<std::string, VerbStat> actions;
    size_t totalCommands = 0;
    size_t totalCutscenes = 0;

    for (const auto& entry : scripts.entries()) {
        if (entry.definition != "CCutsceneDef") continue;
        ++totalCutscenes;
        const auto commands = cutsceneCommands(entry);
        totalCommands += commands.size();
        std::set<std::string> seenHere;
        std::set<std::string> seenActionsHere;
        for (const auto& command : commands) {
            const std::string verb = commandVerb(command);
            if (verb.empty()) continue;
            auto& stat = stats[verb];
            ++stat.count;
            if (stat.example.empty()) stat.example = command;
            if (seenHere.insert(verb).second) ++stat.cutscenes;

            const size_t dot = verb.find('.');
            const std::string action =
                dot == std::string::npos ? verb : verb.substr(dot);
            auto& actionStat = actions[action];
            ++actionStat.count;
            if (actionStat.example.empty()) actionStat.example = command;
            if (seenActionsHere.insert(action).second) ++actionStat.cutscenes;
        }
    }

    const auto bySortedCount = [](const std::map<std::string, VerbStat>& src) {
        std::vector<std::pair<std::string, VerbStat>> sorted(src.begin(),
                                                             src.end());
        std::sort(sorted.begin(), sorted.end(),
                  [](const auto& a, const auto& b) {
                      if (a.second.count != b.second.count)
                          return a.second.count > b.second.count;
                      return a.first < b.first;
                  });
        return sorted;
    };
    const auto sorted = bySortedCount(stats);
    const auto sortedActions = bySortedCount(actions);

    if (jsonOutput) {
        const auto toRows = [](const auto& src, const char* key) {
            json rows = json::array();
            for (const auto& [name, stat] : src) {
                rows.push_back({{key, name},
                                {"count", stat.count},
                                {"cutscenes", stat.cutscenes},
                                {"example", stat.example}});
            }
            return rows;
        };
        std::puts(json{{"verbs", toRows(sorted, "verb")},
                       {"actions", toRows(sortedActions, "action")},
                       {"summary",
                        {{"total_commands", totalCommands},
                         {"total_cutscenes", totalCutscenes},
                         {"distinct_verbs", stats.size()},
                         {"distinct_actions", actions.size()}}}}
                      .dump(2)
                      .c_str());
        return 0;
    }

    std::printf("%-34s %7s %6s  %s\n", "verb", "count", "scenes", "example");
    for (const auto& [verb, stat] : sorted) {
        std::printf("%-34s %7zu %6zu  %s\n", verb.c_str(), stat.count,
                    stat.cutscenes, stat.example.c_str());
    }
    std::printf("\nactions (entity-scoped `NAME.Verb` collapsed to `.Verb`):\n");
    std::printf("%-34s %7s %6s  %s\n", "action", "count", "scenes", "example");
    for (const auto& [action, stat] : sortedActions) {
        std::printf("%-34s %7zu %6zu  %s\n", action.c_str(), stat.count,
                    stat.cutscenes, stat.example.c_str());
    }
    std::printf("\n%zu commands, %zu distinct verbs, %zu distinct actions "
                "across %zu cutscenes\n",
                totalCommands, stats.size(), actions.size(), totalCutscenes);
    return 0;
}

// List chest placements across the loose TNGs. Fixed per-chest contents come
// from the CTCChest block's ContainerContents[n] lines (retail populates the
// runtime reward list from them); chests without contents fall back to the
// def-level ObjectFamilies weighted-random model in game.bin. Key requirements
// live in CChestDef::OpenerObject/OpenersRequired inside the compiled def; the
// keys column here is derived from the OBJECT_SILVERKEY_CHEST_<N> def name.
int chestList(const std::string& gameRoot, const std::string& levelFilter) {
    namespace fs = std::filesystem;
    const fs::path tngDir = fs::path(gameRoot) / "data" / "Levels" / "FinalAlbion";
    if (!fs::exists(tngDir)) {
        std::fprintf(stderr, "chest list: %s not found\n", tngDir.string().c_str());
        return 1;
    }

    const std::string filter = lowered(levelFilter);
    int chests = 0, withContents = 0, files = 0;
    std::printf("%-28s %-22s %-38s %5s %-26s %s\n", "level", "uid", "definition",
                "keys", "position", "contents");

    std::vector<fs::path> paths;
    for (const auto& entry : fs::directory_iterator(tngDir)) {
        if (lowered(entry.path().extension().string()) != ".tng") continue;
        if (!filter.empty() &&
            lowered(entry.path().stem().string()).find(filter) == std::string::npos) {
            continue;
        }
        paths.push_back(entry.path());
    }
    std::sort(paths.begin(), paths.end());

    for (const auto& path : paths) {
        const auto tng = forge::tng::File::parse(path);
        ++files;
        for (const auto& thing : tng.things()) {
            const auto* chestBlock = thing.findCtc("CTCChest");
            const std::string definition = thing.definitionType();
            if (chestBlock == nullptr &&
                definition.find("CHEST") == std::string::npos) {
                continue;
            }
            ++chests;

            std::string keys = "-";
            const std::string silverPrefix = "OBJECT_SILVERKEY_CHEST_";
            if (definition.rfind(silverPrefix, 0) == 0) {
                keys = definition.substr(silverPrefix.size());
            }

            std::string position = "-";
            if (const auto* physics = thing.findCtc("CTCPhysicsStandard")) {
                std::string x, y, z;
                for (const auto& prop : physics->properties) {
                    if (prop.key == "PositionX") x = prop.value;
                    if (prop.key == "PositionY") y = prop.value;
                    if (prop.key == "PositionZ") z = prop.value;
                }
                if (!x.empty()) position = x + "," + y + "," + z;
            }

            std::string contents;
            if (chestBlock != nullptr) {
                for (const auto& prop : chestBlock->properties) {
                    if (prop.key.rfind("ContainerContents", 0) == 0) {
                        if (!contents.empty()) contents += " ";
                        contents += prop.value;
                    }
                }
            }
            if (!contents.empty()) ++withContents;

            std::string level = path.stem().string();
            std::printf("%-28s %-22s %-38s %5s %-26s %s\n", level.c_str(),
                        thing.find("UID").value_or("-").c_str(),
                        definition.c_str(), keys.c_str(), position.c_str(),
                        contents.empty() ? "(def-level random)" : contents.c_str());
        }
    }
    std::printf("\n%d chest placements across %d TNGs, %d with fixed contents\n",
                chests, files, withContents);
    return 0;
}

// Consistency check across the world file, the level archive, loose files, and
// thing placements — the native port of the planned FQT diagnostics.
int validate(const std::string& gameRoot) {
    namespace fs = std::filesystem;
    const fs::path levelsDir = fs::path(gameRoot) / "data" / "Levels";
    const fs::path wldPath = levelsDir / "FinalAlbion.wld";
    const fs::path wadPath = levelsDir / "FinalAlbion.wad";
    const fs::path stbPath = levelsDir / "FinalAlbion_RT.stb";

    if (!fs::exists(wldPath)) {
        std::fprintf(stderr, "validate: %s not found\n", wldPath.string().c_str());
        return 1;
    }

    const auto world = forge::wld::File::parse(wldPath);
    std::printf("world: %zu maps, %zu regions\n", world.maps().size(),
                world.regions().size());

    // Index the WAD (if still present) and loose files by lowercase archive name.
    std::set<std::string> wadNames;
    std::map<std::string, const forge::wad::Entry*> wadEntries;
    const forge::wad::Archive* archive = nullptr;
    forge::wad::Archive archiveStorage;
    if (fs::exists(wadPath)) {
        archiveStorage = forge::wad::Archive::open(wadPath);
        archive = &archiveStorage;
        for (const auto& entry : archive->entries()) {
            wadNames.insert(lowered(entry.name));
            wadEntries[lowered(entry.name)] = &entry;
        }
        std::printf("wad: %zu entries\n", archive->entries().size());
    } else {
        std::printf("wad: FinalAlbion.wad not present (unpacked install)\n");
    }

    std::set<std::string> stbStaticMaps;
    std::map<std::string, forge::stbinfo::StaticMapInfoBlock> stbBounds;
    int missingStbMap = 0;
    if (fs::exists(stbPath)) {
        const auto stb = forge::stb::Archive::open(stbPath);
        std::printf("stb: %zu entries, %zu static maps\n", stb.entries().size(),
                    stb.staticMaps().size());
        for (const auto& map : stb.staticMaps()) {
            stbStaticMaps.insert(lowered(map.levelName));
            try {
                const auto rec = stb.readStaticMapRecord(map);
                if (rec.size() >= forge::stbinfo::kInfoBlockSize)
                    stbBounds[lowered(map.levelName)] =
                        forge::stbinfo::readInfoBlock(rec.data());
            } catch (const std::exception&) {
                // A record we can't read is reported below as a bounds gap.
            }
        }
    } else {
        std::printf("stb: FinalAlbion_RT.stb not present\n");
    }

    int missingLev = 0, missingTng = 0, badRefs = 0, offGridOrigins = 0;
    int levFromWad = 0, levLoose = 0, tngFromWad = 0, tngLoose = 0;
    int identicalLooseShadows = 0, conflictingLooseShadows = 0;

    auto loosePath = [&](const std::string& levelRelative) {
        fs::path loose = levelsDir;
        for (size_t start = 0; start < levelRelative.size();) {
            size_t end = levelRelative.find('\\', start);
            if (end == std::string::npos) end = levelRelative.size();
            loose /= levelRelative.substr(start, end - start);
            start = end + 1;
        }
        return loose;
    };

    auto locate = [&](const std::string& levelRelative) -> int {
        // WLD names are like "FinalAlbion\Foo.lev"; WAD entries are
        // "Data\Levels\FinalAlbion\Foo.lev"; loose files sit under levelsDir.
        if (wadNames.count(lowered("Data\\Levels\\" + levelRelative)) > 0) return 1;
        const fs::path loose = loosePath(levelRelative);
        return fs::exists(loose) ? 2 : 0;
    };

    auto auditLooseShadow = [&](const std::string& levelRelative) {
        if (!archive) return;
        const fs::path loose = loosePath(levelRelative);
        if (!fs::is_regular_file(loose)) return;
        const auto found = wadEntries.find(
            lowered("Data\\Levels\\" + levelRelative));
        if (found == wadEntries.end()) return;
        if (archive->payloadEqualsFile(*found->second, loose)) {
            ++identicalLooseShadows;
        } else {
            ++conflictingLooseShadows;
            std::printf("  CONFLICTING LOOSE SHADOW: %s\n",
                        levelRelative.c_str());
        }
    };

    std::set<std::string> knownLevels;
    for (const auto& map : world.maps()) {
        knownLevels.insert(lowered(map.levelName));

        // Every retail FinalAlbion map begins on the 32-unit terrain grid.
        // Off-grid registration can remain internally bounds-consistent while
        // shifting 16x16 foreground patches away from their native grid, which
        // makes topology regeneration disagree with the serialized layers.
        if (map.mapX % 32 != 0 || map.mapY % 32 != 0) {
            ++offGridOrigins;
            std::printf("  OFF-GRID MAP ORIGIN: %s (map %d) at (%d,%d); "
                        "expected multiples of 32\n",
                        map.levelName.c_str(), map.index, map.mapX, map.mapY);
        }

        switch (locate(map.levelName)) {
            case 1: ++levFromWad; break;
            case 2: ++levLoose; break;
            default:
                ++missingLev;
                std::printf("  MISSING LEV: %s (map %d)\n", map.levelName.c_str(),
                            map.index);
        }
        auditLooseShadow(map.levelName);

        if (!stbStaticMaps.empty() &&
            stbStaticMaps.count(lowered("Data\\Levels\\" + map.levelName)) == 0) {
            ++missingStbMap;
            std::printf("  MISSING STB STATIC MAP: %s (map %d)\n",
                        map.levelName.c_str(), map.index);
        }

        std::string tngName = map.levelName;
        if (tngName.size() > 4) tngName.replace(tngName.size() - 4, 4, ".tng");
        switch (locate(tngName)) {
            case 1: ++tngFromWad; break;
            case 2: ++tngLoose; break;
            default:
                ++missingTng;
                std::printf("  MISSING TNG: %s (map %d)\n", tngName.c_str(),
                            map.index);
        }
        auditLooseShadow(tngName);
    }
    std::printf("lev: %d in wad, %d loose, %d missing\n", levFromWad, levLoose,
                missingLev);
    std::printf("tng: %d in wad, %d loose, %d missing\n", tngFromWad, tngLoose,
                missingTng);
    std::printf("loose shadows: %d identical, %d conflicting\n",
                identicalLooseShadows, conflictingLooseShadows);
    std::printf("stb static maps: %d missing\n", missingStbMap);
    std::printf("map origins: %d off the retail 32-unit grid\n", offGridOrigins);

    std::map<std::string, int> ownershipCounts;
    for (const auto& region : world.regions()) {
        for (const std::string& reference : region.containsMaps)
            ++ownershipCounts[lowered(reference)];
        for (const auto* list : {&region.containsMaps, &region.seesMaps}) {
            for (const std::string& reference : *list) {
                if (knownLevels.count(lowered(reference)) == 0) {
                    ++badRefs;
                    std::printf("  BAD REGION REF: %s -> %s\n",
                                region.regionName.c_str(), reference.c_str());
                }
            }
        }
    }
    std::printf("region map references: %d unresolved\n", badRefs);
    int unownedMaps = 0, multiplyOwnedMaps = 0;
    for (const auto& map : world.maps()) {
        const int owners = ownershipCounts[lowered(map.levelName)];
        if (owners == 0) {
            ++unownedMaps;
            std::printf("  UNOWNED MAP: %s\n", map.levelName.c_str());
        } else if (owners > 1) {
            ++multiplyOwnedMaps;
            std::printf("  MULTIPLY OWNED MAP: %s (%d regions)\n",
                        map.levelName.c_str(), owners);
        }
    }
    std::printf("region ownership: %d unowned, %d multiply owned\n",
                unownedMaps, multiplyOwnedMaps);

    // --- Compiled world (.bwd) coherence + STB bounds cross-check --------------
    // The engine loads FinalAlbion.bwd (not the text .wld) when
    // UseCompiledWorldFiles is TRUE (retail default). Every past "validate CLEAN
    // but fails live" case came from a check the old validate did NOT do: it read
    // only the .wld, never compared it to the compiled .bwd, and never compared
    // map bounds to the STB common-record bounds. These checks close that gap.
    int bwdIssues = 0, boundsMismatch = 0;
    const fs::path bwdPath = levelsDir / "FinalAlbion.bwd";
    if (!fs::exists(bwdPath)) {
        std::printf("bwd: FinalAlbion.bwd NOT PRESENT — the engine loads the "
                    "compiled .bwd when UseCompiledWorldFiles=TRUE (retail "
                    "default); .wld edits alone will not take effect at runtime\n");
        ++bwdIssues;
    } else {
        forge::bwd::File bwd;
        bool parsed = true;
        try {
            bwd = forge::bwd::File::parse(bwdPath);
        } catch (const std::exception& e) {
            std::printf("  BWD PARSE FAIL: %s\n", e.what());
            ++bwdIssues;
            parsed = false;
        }
        if (parsed) {
            std::printf("bwd: %zu maps, %zu regions\n", bwd.maps().size(),
                        bwd.regions().size());

            // (1) wld <-> bwd count coherence. A mismatch means the text source
            //     and the compiled artifact disagree — the .bwd was not
            //     regenerated after a .wld edit (or vice versa).
            if (bwd.maps().size() != world.maps().size()) {
                std::printf("  WLD/BWD MAP COUNT MISMATCH: wld %zu vs bwd %zu "
                            "(regenerate the .bwd from the .wld)\n",
                            world.maps().size(), bwd.maps().size());
                ++bwdIssues;
            }
            if (bwd.regions().size() != world.regions().size()) {
                std::printf("  WLD/BWD REGION COUNT MISMATCH: wld %zu vs bwd %zu\n",
                            world.regions().size(), bwd.regions().size());
                ++bwdIssues;
            }

            // Compare the runtime-critical membership lists even when other
            // inherited region metadata intentionally differs between WLD and
            // BWD. Names are resolved to compiled 1-based map slots.
            auto canonicalLevel = [&](std::string name) {
                name = lowered(std::move(name));
                const std::string prefix = "data\\levels\\";
                if (name.rfind(prefix, 0) == 0) name.erase(0, prefix.size());
                return name;
            };
            std::map<std::string, int32_t> bwdSlotByLevel;
            for (size_t slot = 0; slot < bwd.maps().size(); ++slot)
                bwdSlotByLevel[canonicalLevel(bwd.maps()[slot].levelName)] =
                    static_cast<int32_t>(slot + 1);
            int bwdMembershipMismatches = 0;
            const size_t comparableRegions =
                std::min(world.regions().size(), bwd.regions().size());
            for (size_t regionSlot = 0; regionSlot < comparableRegions;
                 ++regionSlot) {
                const auto resolveList = [&](const std::vector<std::string>& names) {
                    std::vector<int32_t> slots;
                    for (const auto& name : names) {
                        const auto found = bwdSlotByLevel.find(canonicalLevel(name));
                        if (found != bwdSlotByLevel.end()) slots.push_back(found->second);
                    }
                    std::sort(slots.begin(), slots.end());
                    return slots;
                };
                auto wldContains =
                    resolveList(world.regions()[regionSlot].containsMaps);
                auto wldSees = resolveList(world.regions()[regionSlot].seesMaps);
                auto bwdContains = bwd.regions()[regionSlot].contains;
                auto bwdSees = bwd.regions()[regionSlot].sees;
                std::sort(bwdContains.begin(), bwdContains.end());
                std::sort(bwdSees.begin(), bwdSees.end());
                if (wldContains != bwdContains || wldSees != bwdSees) {
                    ++bwdMembershipMismatches;
                    std::printf("  WLD/BWD REGION MEMBERSHIP MISMATCH: slot %zu "
                                "%s contains=%zu/%zu sees=%zu/%zu\n",
                                regionSlot + 1,
                                bwd.regions()[regionSlot].name.c_str(),
                                wldContains.size(), bwdContains.size(),
                                wldSees.size(), bwdSees.size());
                }
            }
            std::printf("wld/bwd region membership: %d mismatches\n",
                        bwdMembershipMismatches);
            bwdIssues += bwdMembershipMismatches;

            // The compiled world is the runtime ownership source. Validate its
            // slot partition independently of the text WLD so a stale or
            // hand-edited BWD cannot pass on count/bounds coherence alone.
            const auto bwdOwnership = bwd.auditOwnership();
            const int bwdInvalidOwnerSlots =
                static_cast<int>(bwdOwnership.invalidReferences.size());
            for (const auto& [regionSlot, mapSlot] :
                 bwdOwnership.invalidReferences) {
                std::printf("  BWD INVALID OWNER SLOT: region %s -> %d\n",
                            bwd.regions()[regionSlot - 1].name.c_str(), mapSlot);
            }
            int bwdUnownedMaps = 0, bwdMultiplyOwnedMaps = 0;
            for (size_t slot = 1; slot < bwdOwnership.ownerCounts.size(); ++slot) {
                if (bwdOwnership.ownerCounts[slot] == 0) {
                    ++bwdUnownedMaps;
                    std::printf("  BWD UNOWNED MAP: slot %zu %s\n", slot,
                                bwd.maps()[slot - 1].scriptName.c_str());
                } else if (bwdOwnership.ownerCounts[slot] > 1) {
                    ++bwdMultiplyOwnedMaps;
                    std::printf("  BWD MULTIPLY OWNED MAP: slot %zu %s (%d regions)\n",
                                slot, bwd.maps()[slot - 1].scriptName.c_str(),
                                bwdOwnership.ownerCounts[slot]);
                }
            }
            std::printf("bwd ownership: %d invalid slots, %d unowned, "
                        "%d multiply owned\n", bwdInvalidOwnerSlots,
                        bwdUnownedMaps, bwdMultiplyOwnedMaps);
            bwdIssues += bwdInvalidOwnerSlots + bwdUnownedMaps +
                         bwdMultiplyOwnedMaps;

            // (2) bwd <-> STB bounds cross-check (the 2026-08-12 failure class:
            //     world-map registration at one origin, STB common record at
            //     another; validate CLEAN and still white-out / region 0).
            //     Retail reuses level names across slots (e.g. DragonCliff_
            //     Filler_01 at two origins), and the STB is name-keyed, so only
            //     names that are UNIQUE in the .bwd can be disambiguated. Every
            //     custom level has a unique name, so this still covers new maps.
            std::map<std::string, int> nameCount;
            for (const auto& m : bwd.maps()) ++nameCount[lowered(m.levelName)];
            for (const auto& m : bwd.maps()) {
                if (nameCount[lowered(m.levelName)] != 1) continue; // ambiguous
                auto it = stbBounds.find(lowered(m.levelName));
                if (it == stbBounds.end()) continue; // STB-presence handled above
                const auto& ib = it->second;
                const int stbRight = ib.worldX + ib.mapWidth;
                const int stbBottom = ib.worldY + ib.mapHeight;
                if (ib.worldX != m.left || ib.worldY != m.top ||
                    stbRight != m.right || stbBottom != m.bottom) {
                    std::printf("  BWD/STB BOUNDS MISMATCH: %s  bwd (%d,%d)-(%d,%d) "
                                "vs stb (%d,%d)-(%d,%d)\n",
                                m.scriptName.c_str(), m.left, m.top, m.right,
                                m.bottom, ib.worldX, ib.worldY, stbRight, stbBottom);
                    ++boundsMismatch;
                }
            }

            // Advisory (not a failure): the runtime vector probe measured all 142
            // regions loaded, refuting a hard vanilla-count cap. Keep the warning
            // because dedicated tail-region membership still needs a live lookup.
            if (bwd.regions().size() > 141)
                std::printf("  NOTE: %zu regions (> vanilla 141). Regions past the "
                            "vanilla count are loaded by the engine; verify their "
                            "membership with GetRegionNumberMapIsIn before relying "
                            "on a dedicated region.\n",
                            bwd.regions().size());
        }
    }
    std::printf("bwd coherence: %d issues, %d STB bounds mismatches\n", bwdIssues,
                boundsMismatch);

    // Thing-level checks on every loose TNG (the editable surface).
    int tngFiles = 0, things = 0, noDefinition = 0, noUid = 0;
    const fs::path finalAlbionDir = levelsDir / "FinalAlbion";
    if (fs::exists(finalAlbionDir)) {
        for (const auto& entry : fs::directory_iterator(finalAlbionDir)) {
            if (lowered(entry.path().extension().string()) != ".tng") continue;
            ++tngFiles;
            const auto tng = forge::tng::File::parse(entry.path());
            for (const auto& thing : tng.things()) {
                ++things;
                if (thing.definitionType().empty()) {
                    ++noDefinition;
                    std::printf("  NO DEFINITION: %s '%s' in %s\n",
                                thing.type.c_str(), thing.scriptName().c_str(),
                                tng.source().c_str());
                }
                if (!thing.find("UID").has_value()) {
                    ++noUid;
                    std::printf("  NO UID: %s '%s' in %s\n", thing.type.c_str(),
                                thing.scriptName().c_str(), tng.source().c_str());
                }
            }
        }
    }
    std::printf("things: %d across %d loose TNGs, %d without DefinitionType, "
                "%d without UID\n", things, tngFiles, noDefinition, noUid);

    // --- ground-theme palette integrity ---------------------------------------
    // A LEV theme slot stores a GLOBAL game.bin def entry index, so a level
    // authored against a different game.bin silently addresses the wrong def.
    // It degrades quietly -- wrong passability seeding, minimap colour, camera-Z
    // -- rather than crashing, which is exactly why it needs a validator. Only
    // loose LEVs are checked; WAD-packed retail levels are correct by
    // construction and unpacking 800 of them here would be gratuitous.
    int themeWrong = 0, themeUnresolvable = 0, themeLevels = 0;
    const fs::path defsDir = fs::path(gameRoot) / "data" / "CompiledDefs";
    if (fs::exists(defsDir / "game.bin") && fs::exists(defsDir / "names.bin")) {
        try {
            const auto defs = forge::themepalette::openDefBank(gameRoot);
            const auto table = forge::themepalette::makeDefIndexTable(defs);
            for (const auto& map : world.maps()) {
                std::string relative = map.levelName;
                std::replace(relative.begin(), relative.end(), '\\', '/');
                const fs::path loose = levelsDir / relative;
                if (!fs::exists(loose)) continue;
                ++themeLevels;
                const auto report = forge::themepalette::auditLevel(loose, table);
                themeWrong += report.wrong;
                themeUnresolvable += report.unresolvable;
                for (const auto& issue : report.issues) {
                    std::printf("  THEME PALETTE %s: %s: %s\n",
                                issue.resolvable ? "STALE INDEX" : "UNKNOWN THEME",
                                map.levelName.c_str(),
                                forge::themepalette::formatIssue(issue).c_str());
                }
            }
            std::printf("theme palettes: %d loose levels checked, %d stale indices, "
                        "%d unknown themes\n", themeLevels, themeWrong,
                        themeUnresolvable);
            if (themeWrong > 0) {
                std::printf("    fix: forge lev themerebase <game-root> <level.lev>...\n");
            }
        } catch (const std::exception& e) {
            std::printf("  THEME PALETTE CHECK SKIPPED: %s\n", e.what());
        }
    }

    const bool clean = missingLev == 0 && missingTng == 0 && badRefs == 0 &&
                       missingStbMap == 0 && noDefinition == 0 && noUid == 0 &&
                       bwdIssues == 0 && boundsMismatch == 0 &&
                       offGridOrigins == 0 &&
                       conflictingLooseShadows == 0 &&
                       unownedMaps == 0 && multiplyOwnedMaps == 0 &&
                       themeWrong == 0 && themeUnresolvable == 0;
    std::printf("validate: %s\n", clean ? "CLEAN" : "ISSUES FOUND");
    return clean ? 0 : 1;
}

// List FSE API functions from the manifest. Optional filter matches name,
// scope, or category (case-insensitive).
int fseList(const std::string& manifestPath, const std::string& filter,
            bool jsonOutput) {
    const auto manifest = forge::fse::load(manifestPath);
    const std::string needle = lowered(filter);
    auto matches = [&](const forge::fse::Function& fn) {
        if (needle.empty()) return true;
        return lowered(fn.name).find(needle) != std::string::npos ||
               lowered(fn.scope).find(needle) != std::string::npos ||
               lowered(fn.category).find(needle) != std::string::npos;
    };

    if (jsonOutput) {
        json rows = json::array();
        for (const auto& fn : manifest.functions) {
            if (!matches(fn)) continue;
            json params = json::array();
            for (const auto& p : fn.parameters) {
                params.push_back({{"name", p.name},
                                  {"type", p.type},
                                  {"optional", p.optional}});
            }
            rows.push_back({{"name", fn.name},
                            {"scope", fn.scope},
                            {"return", fn.returnType},
                            {"blocking", fn.blocking},
                            {"category", fn.category},
                            {"parameters", params},
                            {"signature", fn.signature()},
                            {"description", fn.description}});
        }
        std::puts(json{{"functions", rows},
                       {"summary",
                        {{"fse_version", manifest.fseVersion},
                         {"api_version", manifest.apiVersion},
                         {"total", manifest.functions.size()},
                         {"shown", rows.size()}}}}
                      .dump(2)
                      .c_str());
        return 0;
    }

    size_t shown = 0;
    std::printf("%-8s %-4s %-38s %s\n", "scope", "blk", "function", "returns");
    for (const auto& fn : manifest.functions) {
        if (!matches(fn)) continue;
        std::printf("%-8s %-4s %-38s %s\n", fn.scope.c_str(),
                    fn.blocking ? "wait" : "", fn.name.c_str(),
                    fn.returnType.c_str());
        ++shown;
    }
    std::printf("\n%zu of %zu FSE functions (FSE %s, API %s)\n", shown,
                manifest.functions.size(), manifest.fseVersion.c_str(),
                manifest.apiVersion.c_str());
    return 0;
}

// Show one FSE function's full signature and description.
int fseShow(const std::string& manifestPath, const std::string& name,
            bool jsonOutput) {
    const auto manifest = forge::fse::load(manifestPath);
    const auto* fn = manifest.find(name);
    if (fn == nullptr) {
        std::fprintf(stderr, "fse show: no function named %s\n", name.c_str());
        return 1;
    }
    if (jsonOutput) {
        json params = json::array();
        for (const auto& p : fn->parameters) {
            params.push_back({{"name", p.name},
                              {"type", p.type},
                              {"optional", p.optional}});
        }
        std::puts(json{{"name", fn->name},
                       {"scope", fn->scope},
                       {"return", fn->returnType},
                       {"blocking", fn->blocking},
                       {"category", fn->category},
                       {"parameters", params},
                       {"signature", fn->signature()},
                       {"description", fn->description}}
                      .dump(2)
                      .c_str());
        return 0;
    }
    std::printf("%s\n\n", fn->signature().c_str());
    std::printf("scope:       %s\n", fn->scope.c_str());
    std::printf("category:    %s\n", fn->category.c_str());
    std::printf("blocking:    %s\n", fn->blocking ? "yes (waits)" : "no");
    if (!fn->description.empty()) {
        std::printf("description: %s\n", fn->description.c_str());
    }
    if (!fn->parameters.empty()) {
        std::printf("\nparameters:\n");
        for (const auto& p : fn->parameters) {
            std::printf("  %-16s %s%s\n", p.name.c_str(), p.type.c_str(),
                        p.optional ? "  (optional)" : "");
        }
    }
    return 0;
}

// --- quest node graphs (FQT visual-scripting port) ---------------------------

// List quest node definitions: the 104 curated FQT nodes, plus one raw node
// per FSE manifest function when --manifest is given.
int questNodes(const std::string& filter, bool jsonOutput,
               const std::string& manifestPath) {
    const auto registry =
        manifestPath.empty()
            ? forge::questnodes::Registry::curated()
            : forge::questnodes::Registry::withManifest(
                  forge::fse::load(manifestPath));

    const std::string needle = lowered(filter);
    auto matches = [&](const forge::questnodes::NodeDef& def) {
        if (needle.empty()) return true;
        return lowered(def.type).find(needle) != std::string::npos ||
               lowered(def.label).find(needle) != std::string::npos ||
               lowered(def.category).find(needle) != std::string::npos;
    };

    if (jsonOutput) {
        json rows = json::array();
        for (const auto& def : registry.nodes()) {
            if (!matches(def)) continue;
            json props = json::array();
            for (const auto& p : def.properties) {
                json pj = {{"name", p.name},
                           {"type", p.type},
                           {"label", p.label},
                           {"default", p.defaultValue}};
                if (!p.optionsSource.empty()) pj["options_source"] = p.optionsSource;
                props.push_back(std::move(pj));
            }
            json row = {{"type", def.type},
                        {"label", def.label},
                        {"category", def.category},
                        {"advanced", def.advanced},
                        {"source", def.source},
                        {"description", def.description},
                        {"properties", props},
                        {"code_template", def.codeTemplate}};
            if (def.hasBranching) {
                row["branch_labels"] = def.branchLabels.empty()
                                           ? json::array({"True", "False"})
                                           : json(def.branchLabels);
            }
            if (!def.fseCall.empty()) row["fse_call"] = def.fseCall;
            rows.push_back(std::move(row));
        }
        std::puts(json{{"nodes", rows},
                       {"summary",
                        {{"curated", registry.curatedCount()},
                         {"total", registry.nodes().size()},
                         {"shown", rows.size()}}}}
                      .dump(2)
                      .c_str());
        return 0;
    }

    size_t shown = 0;
    std::printf("%-10s %-34s %-30s %s\n", "category", "type", "label", "props");
    for (const auto& def : registry.nodes()) {
        if (!matches(def)) continue;
        std::string props;
        for (const auto& p : def.properties) {
            if (!props.empty()) props += " ";
            props += p.name + ":" + p.type;
        }
        std::printf("%-10s %-34s %-30s %s\n", def.category.c_str(),
                    def.type.c_str(), def.label.c_str(), props.c_str());
        ++shown;
    }
    std::printf("\n%zu of %zu node definitions (%zu curated%s)\n", shown,
                registry.nodes().size(), registry.curatedCount(),
                manifestPath.empty() ? "; pass --manifest for the full FSE set"
                                     : " + FSE manifest overlay");
    return 0;
}

// Compile a quest node graph (JSON) to an FSE Lua entity script.
int questCompile(const std::string& graphPath, const std::string& outPath,
                 const std::string& manifestPath) {
    const auto graph = forge::questnodes::loadGraph(graphPath);

    forge::fse::Manifest manifest;
    forge::questnodes::CompileOptions options;
    forge::questnodes::Registry registry =
        forge::questnodes::Registry::curated();
    if (!manifestPath.empty()) {
        manifest = forge::fse::load(manifestPath);
        registry = forge::questnodes::Registry::withManifest(manifest);
        options.validateAgainst = &manifest;
    }

    const auto result =
        forge::questnodes::compileEntityScript(graph, registry, options);
    for (const auto& warning : result.warnings) {
        std::fprintf(stderr, "warning: %s\n", warning.c_str());
    }

    if (outPath.empty()) {
        std::fputs(result.lua.c_str(), stdout);
    } else {
        std::ofstream out(outPath, std::ios::binary);
        if (!out) {
            std::fprintf(stderr, "quest compile: cannot write %s\n",
                         outPath.c_str());
            return 1;
        }
        out << result.lua;
        std::fprintf(stderr, "wrote %s (%zu bytes, %zu warnings)\n",
                     outPath.c_str(), result.lua.size(),
                     result.warnings.size());
    }
    return 0;
}

// --- quest deploy / doctor (three-part FSE invariant) ------------------------

#ifdef _WIN32
// A live Fable.exe holds quests.lua/.qst state in memory and can clobber (or
// be clobbered by) our writes; deploy refuses to touch a running install.
bool fableExeRunning() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool running = false;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"Fable.exe") == 0) {
                running = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return running;
}
#endif

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t nl = text.find('\n', start);
        if (nl == std::string::npos) {
            if (start < text.size()) lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, nl - start));
        start = nl + 1;
    }
    for (auto& l : lines)
        if (!l.empty() && l.back() == '\r') l.pop_back();
    return lines;
}

// Minimal diff for the dry-run report: trims the common line prefix/suffix
// and prints the middle as -/+ lines (our edits are localized, so this is
// exactly the changed region).
void printLineDiff(const std::string& before, const std::string& after) {
    const auto a = splitLines(before);
    const auto b = splitLines(after);
    size_t prefix = 0;
    while (prefix < a.size() && prefix < b.size() && a[prefix] == b[prefix])
        ++prefix;
    size_t suffix = 0;
    while (suffix < a.size() - prefix && suffix < b.size() - prefix &&
           a[a.size() - 1 - suffix] == b[b.size() - 1 - suffix])
        ++suffix;
    for (size_t i = prefix; i < a.size() - suffix; ++i)
        std::printf("    - %s\n", a[i].c_str());
    for (size_t i = prefix; i < b.size() - suffix; ++i)
        std::printf("    + %s\n", b[i].c_str());
}

// forge quest deploy <graph.json> <game-root> [--dry-run] [--active|--dormant]
//                    [--id N] [--manifest <fse-manifest.json>]
// Stages all three artifacts an FSE quest needs: compiled Lua, quests.lua
// entry, AddQuest line in FinalAlbion.qst (see forge/questdeploy.hpp).
int questDeploy(const std::string& graphPath, const std::string& gameRoot,
                bool dryRun, const forge::questdeploy::DeployOptions& options) {
    namespace qd = forge::questdeploy;
    const auto graph = forge::questnodes::loadGraph(graphPath);
    const auto plan = qd::planDeploy(graph, gameRoot, options);

    for (const auto& warning : plan.warnings)
        std::fprintf(stderr, "warning: %s\n", warning.c_str());

    std::printf("quest %s -> %s  (id %lld%s, %s in .qst)\n",
                plan.questName.c_str(), gameRoot.c_str(),
                static_cast<long long>(plan.id),
                plan.idWasAuto ? " auto" : "",
                plan.active ? "ACTIVE" : "dormant");

    if (dryRun) {
        std::puts("dry run — nothing written. Planned changes:");
        const std::pair<const qd::FileChange*, const char*> changes[] = {
            {&plan.script, "compiled Lua"},
            {&plan.registry, "FSE quest registry"},
            {&plan.qst, "engine activation list"},
        };
        for (const auto& [change, what] : changes) {
            if (!change->changed()) {
                std::printf("  unchanged  %s\n", change->path.string().c_str());
                continue;
            }
            std::printf("  %s  %s (%s, %zu bytes%s)\n",
                        change->existed ? "update " : "create ",
                        change->path.string().c_str(), what,
                        change->after.size(),
                        change->existed ? ", backup to .bak" : "");
            if (change == &plan.script && !change->existed) {
                std::printf("    + <%zu lines of generated Lua>\n",
                            splitLines(change->after).size());
            } else {
                printLineDiff(change->before, change->after);
            }
        }
        return 0;
    }

#ifdef _WIN32
    if (fableExeRunning()) {
        std::fprintf(stderr,
                     "quest deploy: Fable.exe is running — close the game "
                     "first (or use --dry-run to preview)\n");
        return 1;
    }
#endif

    const auto written = qd::applyDeploy(plan);
    for (const auto& path : written)
        std::printf("  wrote %s\n", path.string().c_str());
    if (written.empty()) std::puts("  everything already up to date");
    std::printf("deployed %s: script + quests.lua entry + AddQuest line "
                "(%zu file(s) written, backups in *.bak)\n",
                plan.questName.c_str(), written.size());
    return 0;
}

// forge quest doctor <game-root> [--json] — audits the three-part invariant
// for every quest in FSE/quests.lua. The "Steam verify broke my quests"
// 5-second diagnosis.
int questDoctor(const std::string& gameRoot, bool jsonOutput) {
    const auto report = forge::questdeploy::doctor(gameRoot);

    if (jsonOutput) {
        json issues = json::array();
        for (const auto& issue : report.issues)
            issues.push_back({{"quest", issue.quest},
                              {"problem", issue.problem},
                              {"fix", issue.fix}});
        std::puts(json{{"game_root", gameRoot},
                       {"quests_lua", report.questsLuaPath.string()},
                       {"qst", report.qstPath.string()},
                       {"quests_checked", report.questsChecked},
                       {"healthy", report.healthy()},
                       {"issues", issues}}
                      .dump(2)
                      .c_str());
        return report.healthy() ? 0 : 1;
    }

    std::printf("quest doctor: %s\n", gameRoot.c_str());
    std::printf("  registry: %s (%zu quests)\n",
                report.questsLuaPath.string().c_str(), report.questsChecked);
    if (report.healthy()) {
        std::puts("  all quests healthy: Lua present, .qst line present, ids "
                  "unique");
        return 0;
    }
    for (const auto& issue : report.issues) {
        std::printf("  [%s] %s\n",
                    issue.quest.empty() ? "-" : issue.quest.c_str(),
                    issue.problem.c_str());
        std::printf("      fix: %s\n", issue.fix.c_str());
    }
    std::printf("%zu issue(s) found\n", report.issues.size());
    return 1;
}

// Author a custom quest card by CLONING a retail OBJECT_QUEST_CARD_* donor: the
// donor's CQuestCardDef (card content) and its OBJECT (the name AddQuestCard
// resolves) are cloned, the clone's known scalar fields are patched, the pair is
// appended, and the OBJECT is relinked to the new card def. Writes the modified
// game.bin + names.bin to <out-root>/data/CompiledDefs (leaving the source game
// untouched); pass --in-place to write back into <game-root>.
void writeAllBytes(const std::string& path, const std::vector<uint8_t>& data);

struct QuestCardTextOutput {
    forge::big::File file;
    std::vector<forge::textbig::UpsertResult> entries;
};

// Set a UI/CUIDef entry's States[stateIndex].GraphicIndex (the sprite's texture id)
// in game.bin. Quest-card orb art = UI_QUEST_SPRITE_CORE/OPTIONAL/VIGNETTE with
// State[0] texture 5892/5894/5896 (docs/QUEST_CARD_TEXTURE_BINDING.md). Writes to
// <out-root>/data/CompiledDefs (or --in-place). Cumulative into one out-root.
int uiSetGraphic(const std::string& gameRoot, const std::string& schemaPath,
                 const std::string& entryName, uint32_t textureId,
                 uint32_t stateIndex, const std::string& outRootArg, bool inPlace) {
    namespace fs = std::filesystem;
    const fs::path retailDefs = fs::path(gameRoot) / "data" / "CompiledDefs";
    const fs::path outputDefs =
        inPlace ? retailDefs : fs::path(outRootArg) / "data" / "CompiledDefs";
    const fs::path sourceDefs =
        (!inPlace && fs::exists(outputDefs / "game.bin") &&
         fs::exists(outputDefs / "names.bin"))
            ? outputDefs
            : retailDefs;

    auto file = forge::bin::File::open(sourceDefs / "names.bin", sourceDefs / "game.bin");
    const auto schema = forge::defschema::Schema::load(schemaPath);

    const auto& entries = file.entries();
    size_t idx = entries.size();
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].name == entryName) { idx = i; break; }
    if (idx == entries.size()) {
        std::fprintf(stderr, "ui set-graphic: no entry named %s\n", entryName.c_str());
        return 1;
    }

    uint32_t before = 0;
    std::vector<uint8_t> patched;
    try {
        before = forge::uidef::getStateGraphicIndex(entries[idx].data, schema, stateIndex);
        patched = forge::uidef::setStateGraphicIndex(entries[idx].data, schema,
                                                     stateIndex, textureId);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "ui set-graphic: %s\n", e.what());
        return 1;
    }
    file.setEntryData(idx, std::move(patched));

    fs::create_directories(outputDefs);
    file.save(outputDefs / "names.bin", outputDefs / "game.bin");
    std::printf("ui set-graphic: %s  States[%u].GraphicIndex %u -> %u\n  wrote %s\n",
                entryName.c_str(), stateIndex, before, textureId,
                (outputDefs / "game.bin").string().c_str());
    return 0;
}

// Append a custom UI/CUIDef sprite (clone of `src`, e.g. UI_QUEST_SPRITE_CORE) with
// its own texture, for the per-card art render detour. Writes to
// <out-root>/data/CompiledDefs (or --in-place). Cumulative into one out-root.
int uiAddSprite(const std::string& gameRoot, const std::string& schemaPath,
                const std::string& newName, const std::string& src,
                uint32_t textureId, uint32_t stateIndex,
                const std::string& outRootArg, bool inPlace) {
    namespace fs = std::filesystem;
    const fs::path retailDefs = fs::path(gameRoot) / "data" / "CompiledDefs";
    const fs::path outputDefs =
        inPlace ? retailDefs : fs::path(outRootArg) / "data" / "CompiledDefs";
    const fs::path sourceDefs =
        (!inPlace && fs::exists(outputDefs / "game.bin") &&
         fs::exists(outputDefs / "names.bin"))
            ? outputDefs
            : retailDefs;

    auto file = forge::bin::File::open(sourceDefs / "names.bin", sourceDefs / "game.bin");
    const auto schema = forge::defschema::Schema::load(schemaPath);

    forge::uidef::AddSpriteResult r;
    try {
        r = forge::uidef::addSpriteClone(file, schema, src, newName, textureId, stateIndex);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "ui add-sprite: %s\n", e.what());
        return 1;
    }

    fs::create_directories(outputDefs);
    file.save(outputDefs / "names.bin", outputDefs / "game.bin");
    std::printf("ui add-sprite: %s  (clone of %s, State[%u].GraphicIndex=%u)\n"
                "  new entry %zu\n  wrote %s\n",
                r.name.c_str(), r.source.c_str(), stateIndex, r.graphicIndex, r.index,
                (outputDefs / "game.bin").string().c_str());
    return 0;
}

// Set an OBJECT's Graphic.modelId (the mesh it renders). For OBJECT_QUEST_CARD this
// is the card body model (graphics.big MBANK id, e.g. MESH_QUEST_CARD_*). Writes to
// <out-root>/data/CompiledDefs (or --in-place).
int uiSetCardModel(const std::string& gameRoot, const std::string& schemaPath,
                   const std::string& entryName, uint32_t modelId,
                   const std::string& outRootArg, bool inPlace) {
    namespace fs = std::filesystem;
    const fs::path retailDefs = fs::path(gameRoot) / "data" / "CompiledDefs";
    const fs::path outputDefs =
        inPlace ? retailDefs : fs::path(outRootArg) / "data" / "CompiledDefs";
    const fs::path sourceDefs =
        (!inPlace && fs::exists(outputDefs / "game.bin") &&
         fs::exists(outputDefs / "names.bin"))
            ? outputDefs
            : retailDefs;

    auto file = forge::bin::File::open(sourceDefs / "names.bin", sourceDefs / "game.bin");
    const auto schema = forge::defschema::Schema::load(schemaPath);

    const auto& entries = file.entries();
    size_t idx = entries.size();
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].name == entryName) { idx = i; break; }
    if (idx == entries.size()) {
        std::fprintf(stderr, "ui set-cardmodel: no entry named %s\n", entryName.c_str());
        return 1;
    }
    const auto* def = forge::defdecode::resolveType(schema, entries[idx].definition,
                                                    entries[idx].data);
    if (def == nullptr) {
        std::fprintf(stderr, "ui set-cardmodel: could not resolve def type for %s\n",
                     entryName.c_str());
        return 1;
    }
    uint32_t before = 0;
    std::vector<uint8_t> patched;
    try {
        before = forge::uidef::getGraphicModelId(entries[idx].data, *def);
        patched = forge::uidef::setGraphicModelId(entries[idx].data, *def, modelId);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "ui set-cardmodel: %s\n", e.what());
        return 1;
    }
    file.setEntryData(idx, std::move(patched));
    fs::create_directories(outputDefs);
    file.save(outputDefs / "names.bin", outputDefs / "game.bin");
    std::printf("ui set-cardmodel: %s  Graphic.modelId %u -> %u\n  wrote %s\n",
                entryName.c_str(), before, modelId,
                (outputDefs / "game.bin").string().c_str());
    return 0;
}

int questCard(const std::string& gameRoot, const std::string& schemaPath,
              const std::string& newName, const std::string& donor,
              const std::string& outRootArg,
              const forge::questcard::CardPatch& patch, bool inPlace,
              bool overwriteDonor, bool fromScratch,
              bool jsonOutput, QuestCardTextOutput* customText = nullptr) {
    namespace fs = std::filesystem;
    const fs::path retailDefs = fs::path(gameRoot) / "data" / "CompiledDefs";
    const fs::path outputDefs =
        inPlace ? retailDefs
                : fs::path(outRootArg) / "data" / "CompiledDefs";
    // Make repeated authoring into one output root cumulative. If the output
    // already contains a valid pair, clone the next card from that pair rather
    // than restarting from retail and discarding the previous custom card.
    const fs::path sourceDefs =
        (!inPlace && fs::exists(outputDefs / "names.bin") &&
         fs::exists(outputDefs / "game.bin"))
            ? outputDefs
            : retailDefs;
    const fs::path namesIn = sourceDefs / "names.bin";
    const fs::path binIn = sourceDefs / "game.bin";

    auto file = forge::bin::File::open(namesIn, binIn);
    const auto schema = forge::defschema::Schema::load(schemaPath);

    forge::questcard::AuthorResult r;
    try {
        r = fromScratch
                ? forge::questcard::authorFromScratch(file, schema, newName, patch)
            : overwriteDonor
                ? forge::questcard::overwriteDonor(file, schema, donor, patch)
                : forge::questcard::author(file, schema, donor, newName, patch);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "quest card: %s\n", e.what());
        return 1;
    }

    // Destination: --in-place writes back into game-root, else <out-root>.
    fs::path outDir = outputDefs;
    fs::create_directories(outDir);
    file.save(outDir / "names.bin", outDir / "game.bin");

    fs::path textOut;
    if (customText != nullptr) {
        textOut = inPlace
                      ? fs::path(gameRoot) / "data" / "lang" / "English" / "text.big"
                      : fs::path(outRootArg) / "data" / "lang" / "English" / "text.big";
        fs::create_directories(textOut.parent_path());
        writeAllBytes(textOut.string(), customText->file.serialize());
        // Do not claim a successful combined card+text authoring operation if
        // the companion archive cannot be parsed back.
        (void)forge::big::File::open(textOut);
    }

    if (jsonOutput) {
        json textRows = json::array();
        if (customText != nullptr)
            for (const auto& e : customText->entries)
                textRows.push_back({{"name", e.name}, {"id", e.id},
                                    {"mode", e.added ? "add" : "replace"}});
        std::puts(json{{"object_name", r.objectName},
                       {"object_index", r.objectIndex},
                       {"card_def_index", r.cardDefIndex},
                       {"donor_object", r.donorObject},
                       {"donor_card_index", r.donorCard},
                       {"mode", r.appended ? "append" : "overwrite-donor"},
                       {"patched", r.patched},
                       {"preserved", r.preserved},
                       {"out", (outDir / "game.bin").string()},
                       {"text_big", textOut.empty() ? json(nullptr)
                                                    : json(textOut.string())},
                       {"text_entries", textRows}}
                      .dump(2)
                      .c_str());
        return 0;
    }
    std::printf("authored %s (%s mode)\n", r.objectName.c_str(),
                r.appended ? "append" : "overwrite-donor");
    if (r.appended) {
        std::printf("  cloned donor OBJECT %s -> new OBJECT entry %zu\n",
                    r.donorObject.c_str(), r.objectIndex);
        std::printf("  cloned donor CQuestCardDef #%s -> new entry %zu (relinked)\n",
                    r.donorCard.c_str(), r.cardDefIndex);
    } else {
        std::printf("  patched existing donor OBJECT %s / CQuestCardDef #%zu\n",
                    r.donorObject.c_str(), r.cardDefIndex);
        std::printf("  definition count and all global indices preserved\n");
    }
    std::printf("  patched fields (%zu):", r.patched.size());
    for (const auto& f : r.patched) std::printf(" %s", f.c_str());
    std::printf("\n  clone-preserved fields (%zu):", r.preserved.size());
    for (const auto& f : r.preserved) std::printf(" %s", f.c_str());
    std::printf("\n  wrote %s\n", (outDir / "game.bin").string().c_str());
    if (!textOut.empty()) {
        std::printf("  wrote %s\n", textOut.string().c_str());
        for (const auto& e : customText->entries)
            std::printf("    %s = TextID %u\n", e.name.c_str(), e.id);
    }
    std::printf("  use from a quest: Quest:AddQuestCard(\"%s\", scriptName, false, true)\n",
                r.objectName.c_str());
    return 0;
}

// Edit ONE field of an existing compiled-def entry in place, by CRC tag (the
// ergonomic single-field editor for UI authoring). Takes a direct bin path and a
// sibling names.bin (override with --names), locates the field via the schema,
// parses <value> into the field's typed bytes, rewrites only that value, and
// writes the modified bin (+ names.bin) to --out or a "<bin>.set-field.bin"
// sibling. Untouched entries stay byte-identical (semantic re-serialize).
int defsSetField(const std::string& binPath, const std::string& namesPath,
                 const std::string& schemaPath, const std::string& entry,
                 const std::string& field, const std::string& value,
                 const std::string& outPath, bool jsonOutput) {
    namespace fs = std::filesystem;
    const fs::path binIn(binPath);
    const fs::path namesIn =
        namesPath.empty() ? binIn.parent_path() / "names.bin" : fs::path(namesPath);

    auto file = forge::bin::File::open(namesIn, binIn);
    const auto schema = forge::defschema::Schema::load(schemaPath);

    forge::defedit::SetFieldResult r;
    try {
        // Prefer an exact name match; fall back to a numeric index for
        // unnamed sub-defs (e.g. "12345").
        if (file.find(entry) != nullptr) {
            r = forge::defedit::setField(file, schema, entry, field, value);
        } else {
            size_t idx = 0;
            try {
                idx = std::stoul(entry);
            } catch (const std::exception&) {
                std::fprintf(stderr, "defs set-field: no entry named %s\n",
                             entry.c_str());
                return 1;
            }
            r = forge::defedit::setField(file, schema, idx, field, value);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "defs set-field: %s\n", e.what());
        return 1;
    }

    // Destination bin: --out or a "<stem>.set-field<ext>" sibling. names.bin is
    // written alongside the output bin (unchanged name set => byte-identical).
    fs::path binOut;
    if (!outPath.empty()) {
        binOut = fs::path(outPath);
    } else {
        binOut = binIn.parent_path() /
                 (binIn.stem().string() + ".set-field" + binIn.extension().string());
    }
    if (binOut.has_parent_path()) fs::create_directories(binOut.parent_path());
    const fs::path namesOut = binOut.parent_path() / "names.bin";
    file.save(namesOut, binOut);

    if (jsonOutput) {
        std::puts(json{{"bin", binOut.string()},
                       {"names", namesOut.string()},
                       {"entry_index", r.entryIndex},
                       {"definition", r.definition},
                       {"entry_name", r.entryName},
                       {"field", r.field},
                       {"type", r.type},
                       {"old_value", r.oldValue},
                       {"new_value", r.newValue},
                       {"old_payload_size", r.oldPayloadSize},
                       {"new_payload_size", r.newPayloadSize}}
                      .dump(2)
                      .c_str());
        return 0;
    }
    std::printf("set-field: entry %zu  %s%s%s\n", r.entryIndex,
                r.definition.c_str(), r.entryName.empty() ? "" : " / ",
                r.entryName.c_str());
    std::printf("  %s (%s): %s -> %s\n", r.field.c_str(), r.type.c_str(),
                r.oldValue.c_str(), r.newValue.c_str());
    std::printf("  payload %zu -> %zu bytes\n", r.oldPayloadSize,
                r.newPayloadSize);
    std::printf("  wrote %s\n", binOut.string().c_str());
    return 0;
}

// --- forge controls ---------------------------------------------------------
// CControlsDef.Controls is a Vector<CActionInputControl> of fixed 28-byte records
// (action -> device, per ControllerType). On-disk encoding is confirmed against
// retail game.bin (see forge/controls.hpp). These commands read and rewrite those
// bindings as a pure data edit -- no native patch is needed to remap keys.

// Human-readable device value for a binding: PDB-proven enum name + raw int.
static std::string controlsDeviceName(int type, int dev) {
    std::string_view n =
        type == forge::controls::PAD        ? forge::controls::xboxButtonName(dev)
        : type == forge::controls::KEYBOARD ? forge::controls::inputKeyName(dev)
        : type == forge::controls::MOUSE    ? forge::controls::mouseButtonName(dev)
                                            : std::string_view{};
    return n.empty() ? std::to_string(dev)
                     : std::string(n) + " (" + std::to_string(dev) + ")";
}

static std::string controlsActionName(int action) {
    std::string_view n = forge::controls::gameActionName(action);
    return n.empty() ? std::to_string(action)
                     : std::string(n) + " (" + std::to_string(action) + ")";
}

static const char* schemeLabel(const std::vector<forge::controls::InputBinding>& b) {
    int pad = 0, key = 0, mouse = 0;
    for (const auto& x : b) {
        if (x.type == forge::controls::PAD) ++pad;
        else if (x.type == forge::controls::KEYBOARD) ++key;
        else if (x.type == forge::controls::MOUSE) ++mouse;
    }
    if (pad >= key && pad >= mouse) return "PAD";
    return key >= mouse ? "KEYBOARD" : "MOUSE";
}

// Locate the Controls vector inside a def payload by its field tag, WITHOUT a
// schema (the shipped CControlsDef schema is a partial donor and does not clean-
// decode retail entries; the Controls vector may also ride inside a larger def
// chunk). Returns the offset of the leading [u32 count], or SIZE_MAX if the entry
// has no well-formed 28-stride binding vector. Validates by requiring every
// record's ControllerType to be 1/2/3, which rejects spurious tag collisions.
static size_t findControlsVector(const std::vector<uint8_t>& data) {
    const uint32_t tag = forge::defdecode::fieldTag("Controls");
    const uint8_t tb[4] = {uint8_t(tag), uint8_t(tag >> 8), uint8_t(tag >> 16),
                           uint8_t(tag >> 24)};
    for (size_t i = 0; i + 8 <= data.size(); ++i) {
        if (std::memcmp(data.data() + i, tb, 4) != 0) continue;
        const size_t voff = i + 4;  // [u32 count] begins here
        uint32_t count;
        std::memcpy(&count, data.data() + voff, 4);
        if (count == 0 || count > 4096) continue;
        const size_t need = 4 + static_cast<size_t>(count) * forge::controls::kRecordSize;
        if (voff + need > data.size()) continue;
        bool ok = true;
        for (uint32_t k = 0; k < count && ok; ++k) {
            int32_t ty;
            std::memcpy(&ty, data.data() + voff + 4 + static_cast<size_t>(k) * 28 + 4, 4);
            ok = (ty >= 1 && ty <= 3);
        }
        if (ok) return voff;
    }
    return SIZE_MAX;
}

// Slice the [u32 count][count x 28B] Controls buffer beginning at `voff`.
static std::vector<uint8_t> sliceControls(const std::vector<uint8_t>& data, size_t voff) {
    uint32_t count;
    std::memcpy(&count, data.data() + voff, 4);
    const size_t n = 4 + static_cast<size_t>(count) * forge::controls::kRecordSize;
    return std::vector<uint8_t>(data.begin() + voff, data.begin() + voff + n);
}

// forge controls list <bin> [--names <n>]
int controlsList(const std::string& binPath, const std::string& namesPath) {
    namespace fs = std::filesystem;
    const fs::path binIn(binPath);
    const fs::path namesIn =
        namesPath.empty() ? binIn.parent_path() / "names.bin" : fs::path(namesPath);
    auto file = forge::bin::File::open(namesIn, binIn);
    int found = 0;
    const auto& entries = file.entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        const size_t voff = findControlsVector(entries[i].data);
        if (voff == SIZE_MAX) continue;
        ++found;
        const auto binds = forge::controls::parse(sliceControls(entries[i].data, voff));
        std::printf("entry %zu  %s  [scheme=%s, %zu bindings]\n", i,
                    entries[i].name.empty() ? "(unnamed)" : entries[i].name.c_str(),
                    schemeLabel(binds), binds.size());
        for (const auto& b : binds) {
            const char* t = b.type == forge::controls::PAD       ? "pad"
                            : b.type == forge::controls::KEYBOARD ? "key"
                            : b.type == forge::controls::MOUSE    ? "mouse"
                                                                  : "?";
            std::printf("    %-40s -> %-5s %s\n", controlsActionName(b.action).c_str(),
                        t, controlsDeviceName(b.type, b.device()).c_str());
        }
    }
    if (found == 0) {
        std::fprintf(stderr, "controls list: no Controls bindings found in %s\n",
                     binPath.c_str());
        return 1;
    }
    return 0;
}

// forge controls set-binding <bin> <entry> <action> <pad|key|mouse> <value>
//        [--occurrence N] [--names <n>] [--out <bin>]
int controlsSetBinding(const std::string& binPath, const std::string& namesPath,
                       const std::string& entrySel, int action, int type, int device,
                       int occurrence, const std::string& outPath) {
    namespace fs = std::filesystem;
    const fs::path binIn(binPath);
    const fs::path namesIn =
        namesPath.empty() ? binIn.parent_path() / "names.bin" : fs::path(namesPath);
    auto file = forge::bin::File::open(namesIn, binIn);

    // Resolve the entry: exact name, else numeric index.
    const auto& entries = file.entries();
    size_t idx = entries.size();
    if (const auto* e = file.find(entrySel); e != nullptr) {
        for (size_t i = 0; i < entries.size(); ++i)
            if (&entries[i] == e) { idx = i; break; }
    } else {
        try {
            idx = std::stoul(entrySel);
        } catch (const std::exception&) {
            std::fprintf(stderr, "controls set-binding: no entry named %s\n",
                         entrySel.c_str());
            return 1;
        }
    }
    if (idx >= entries.size()) {
        std::fprintf(stderr, "controls set-binding: entry index %s out of range\n",
                     entrySel.c_str());
        return 1;
    }

    // Mutate the Controls records in place (bytes only; length is preserved, so
    // this is safe regardless of whether the rest of the def has a schema).
    std::vector<uint8_t> data = entries[idx].data;
    const size_t voff = findControlsVector(data);
    if (voff == SIZE_MAX) {
        std::fprintf(stderr,
                     "controls set-binding: entry %s has no Controls bindings\n",
                     entrySel.c_str());
        return 1;
    }
    std::vector<uint8_t> value = sliceControls(data, voff);
    const int changed =
        forge::controls::setBinding(value, action, type, device, occurrence);
    if (changed == 0) {
        std::fprintf(stderr,
                     "controls set-binding: action %d not found (or bad args); "
                     "nothing changed\n",
                     action);
        return 1;
    }
    std::copy(value.begin(), value.end(), data.begin() + voff);
    file.setEntryData(idx, data);

    fs::path binOut = outPath.empty()
                          ? binIn.parent_path() / (binIn.stem().string() +
                                                   ".controls" + binIn.extension().string())
                          : fs::path(outPath);
    if (binOut.has_parent_path()) fs::create_directories(binOut.parent_path());
    const fs::path namesOut = binOut.parent_path() / "names.bin";
    file.save(namesOut, binOut);

    const char* t = type == forge::controls::PAD       ? "pad"
                    : type == forge::controls::KEYBOARD ? "key"
                                                        : "mouse";
    std::printf("controls set-binding: entry %zu %s\n", idx,
                entries[idx].name.c_str());
    std::printf("  %s -> %s %s  (%d record%s changed)\n",
                controlsActionName(action).c_str(), t,
                controlsDeviceName(type, device).c_str(), changed,
                changed == 1 ? "" : "s");
    std::printf("  wrote %s\n", binOut.string().c_str());
    return 0;
}

// --- forge gamedata ---------------------------------------------------------
// The live data spine: dump the COMPLETE, mod-aware creature/object/region lists
// straight from the install's game.bin (+ optional .wld), the modern replacement
// for FQT's hardcoded GameData.cs.
int gamedataList(const std::string& root, const std::string& wldPath, bool full) {
    namespace fs = std::filesystem;
    forge::gamedata::Catalog c;
    try {
        c = forge::gamedata::readGameRoot(root, wldPath.empty() ? fs::path{} : fs::path(wldPath));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "gamedata: %s\n", e.what());
        return 1;
    }
    std::printf("gamedata (live from %s):\n", root.c_str());
    std::printf("  creatures : %zu\n", c.creatures.size());
    std::printf("  objects   : %zu\n", c.objects.size());
    std::printf("  regions   : %zu\n", c.regions.size());
    if (full) {
        for (const auto& s : c.creatures) std::printf("CREATURE\t%s\n", s.c_str());
        for (const auto& s : c.objects) std::printf("OBJECT\t%s\n", s.c_str());
        for (const auto& s : c.regions) std::printf("REGION\t%s\n", s.c_str());
    }
    return 0;
}

// --- forge quest master -----------------------------------------------------
// Automate the community's FSE_Master ritual: generate FSE_Master.lua, flip its
// FinalAlbion.qst line to active, and print the quests.lua registration.
int questMaster(const std::string& qstPath,
                const std::vector<forge::questproject::GlobalState>& globals, int id,
                const std::string& luaOut, const std::string& qstOut) {
    namespace fs = std::filesystem;
    forge::questproject::MasterQuest m;
    m.id = id;
    m.globals = globals;

    forge::qst::File qstFile = forge::qst::File::parse(qstPath);
    const auto outcome = forge::questproject::ensureMasterQuestActive(qstFile, m.name);
    const char* what =
        outcome == forge::questproject::MasterQstOutcome::AlreadyActive ? "already active"
        : outcome == forge::questproject::MasterQstOutcome::Activated   ? "activated (FALSE -> TRUE)"
                                                                        : "added";
    const fs::path qstDest = qstOut.empty() ? fs::path(qstPath) : fs::path(qstOut);
    std::ofstream(qstDest, std::ios::binary) << qstFile.serialize();
    std::printf("quest master: FSE_Master %s in %s\n", what, qstPath.c_str());
    std::printf("  wrote %s\n", qstDest.string().c_str());

    const auto lua = forge::questproject::compileMasterQuestScript(m);
    const fs::path luaDest = luaOut.empty() ? fs::path(m.name + ".lua") : fs::path(luaOut);
    if (luaDest.has_parent_path()) fs::create_directories(luaDest.parent_path());
    std::ofstream(luaDest, std::ios::binary) << lua.lua;
    std::printf("  wrote %s (%zu global%s)\n", luaDest.string().c_str(), m.globals.size(),
                m.globals.size() == 1 ? "" : "s");

    std::printf("  quests.lua registration (id %d MUST be the lowest so it loads first):\n", m.id);
    std::fputs(forge::questproject::masterRegistrationSnippet(m).c_str(), stdout);
    return 0;
}

// --- bsdiff .patch ingestion -------------------------------------------------
std::vector<uint8_t> readAllBytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
}

void writeAllBytes(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write " + path);
    f.write(reinterpret_cast<const char*>(data.data()), (std::streamsize)data.size());
}

int patchInfo(const std::string& patchPath) {
    const auto patch = readAllBytes(patchPath);
    const auto h = forge::bspatch::readHeader(patch);
    std::printf("BSDIFF40 patch: %s\n", patchPath.c_str());
    std::printf("  control block (bz2): %llu bytes\n", (unsigned long long)h.ctrlLen);
    std::printf("  diff block (bz2):    %llu bytes\n", (unsigned long long)h.diffLen);
    std::printf("  extra block (bz2):   %llu bytes\n",
                (unsigned long long)(patch.size() - 32 - h.ctrlLen - h.diffLen));
    std::printf("  produces new file:   %llu bytes\n", (unsigned long long)h.newSize);
    return 0;
}

int patchApply(const std::string& oldPath, const std::string& patchPath,
               const std::string& outPath) {
    const auto oldFile = readAllBytes(oldPath);
    const auto patch = readAllBytes(patchPath);
    const auto out = forge::bspatch::apply(oldFile, patch);
    writeAllBytes(outPath, out);
    std::printf("applied %s to %s -> %s (%zu bytes)\n", patchPath.c_str(),
                oldPath.c_str(), outPath.c_str(), out.size());
    return 0;
}

// List an .fmp (or any BIG) archive: banks and their changed entries. An .fmp is
// a record-delta package, so this is a mod's footprint by container.
int fmpList(const std::string& path, bool jsonOutput) {
    const auto file = forge::big::File::open(path);
    size_t totalEntries = 0;
    for (const auto& b : file.banks()) totalEntries += b.entries.size();

    if (jsonOutput) {
        json banks = json::array();
        for (const auto& b : file.banks()) {
            if (b.entries.empty()) continue;
            json ents = json::array();
            for (const auto& e : b.entries)
                ents.push_back({{"name", e.name}, {"definition", e.definition},
                                {"length", e.length}});
            banks.push_back({{"bank", b.name}, {"count", b.entries.size()},
                             {"entries", ents}});
        }
        std::puts(json{{"file", path},
                       {"content_type", file.contentType()},
                       {"version", file.version()},
                       {"banks", banks},
                       {"total_entries", totalEntries}}
                      .dump(2)
                      .c_str());
        return 0;
    }

    std::printf(".fmp/BIG: %s (contentType %u, %zu banks, %zu entries)\n",
                path.c_str(), file.contentType(), file.banks().size(),
                totalEntries);
    for (const auto& b : file.banks()) {
        if (b.entries.empty()) continue;
        std::printf("\n[%s] %zu entries\n", b.name.c_str(), b.entries.size());
        for (const auto& e : b.entries) {
            std::printf("  %-46s %-22s %8u bytes\n", e.name.c_str(),
                        e.definition.empty() ? "-" : e.definition.c_str(),
                        e.length);
        }
    }
    return 0;
}

// Extract every .fmp entry's raw payload to <outdir>/<bank>/<name>.bin. For
// inspection and asset recovery. Asset banks (graphics/textures) hold raw
// (possibly compressed) blobs; BIN banks hold compiled-def record bytes.
int fmpExtract(const std::string& fmpPath, const std::string& outDir,
               const std::string& bankFilter) {
    namespace fs = std::filesystem;
    const auto pkg = forge::big::File::open(fmpPath);
    const std::string needle = lowered(bankFilter);
    size_t written = 0;

    for (const auto& bank : pkg.banks()) {
        if (bank.entries.empty()) continue;
        if (!needle.empty() && lowered(bank.name).find(needle) == std::string::npos)
            continue;
        const fs::path bankDir = fs::path(outDir) / bank.name;
        fs::create_directories(bankDir);
        for (const auto& e : bank.entries) {
            // Sanitize the symbol name for a filename.
            std::string safe = e.name.empty() ? ("entry_" + std::to_string(e.id))
                                              : e.name;
            for (char& c : safe)
                if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
                    c == '"' || c == '<' || c == '>' || c == '|')
                    c = '_';
            writeAllBytes((bankDir / (safe + ".bin")).string(), pkg.entryData(e));
            ++written;
        }
    }
    std::printf("extracted %zu entries from %s -> %s\n", written, fmpPath.c_str(),
                outDir.c_str());
    return 0;
}

// Decode text.big string entries. `show` prints one entry's full decode (content
// + speech/speaker/tags); `list` prints a content preview for matching entries.
int textShow(const std::string& path, const std::string& which, bool jsonOutput) {
    const auto big = forge::big::File::open(path);
    const forge::big::Entry* found = nullptr;
    // by-name across banks, else by numeric id
    for (const auto& b : big.banks())
        for (const auto& e : b.entries)
            if (e.name == which) { found = &e; break; }
    if (found == nullptr) {
        try {
            uint32_t id = (uint32_t)std::stoul(which);
            for (const auto& b : big.banks())
                for (const auto& e : b.entries)
                    if (e.id == id) { found = &e; break; }
        } catch (const std::exception&) {}
    }
    if (found == nullptr) {
        std::fprintf(stderr, "text show: no entry '%s'\n", which.c_str());
        return 1;
    }
    const auto e = forge::textbig::decode(big.entryData(*found), (int)found->type);

    if (jsonOutput) {
        json tags = json::array();
        for (const auto& t : e.tags)
            tags.push_back({{"position", t.position}, {"name", t.name}});
        json members = json::array();
        for (auto m : e.groupMembers) members.push_back(m);
        std::puts(json{{"name", found->name}, {"id", found->id}, {"type", e.type},
                       {"content", e.content}, {"speech_bank", e.speechBank},
                       {"speaker", e.speaker}, {"identifier", e.identifier},
                       {"tags", tags}, {"group_members", members}}
                      .dump(2).c_str());
        return 0;
    }
    std::printf("%s (id %u, type %d)\n", found->name.c_str(), found->id, e.type);
    if (e.type == 1) {
        std::printf("  group of %zu:", e.groupMembers.size());
        for (auto m : e.groupMembers) std::printf(" %u", m);
        std::printf("\n");
        return 0;
    }
    std::printf("  content: %s\n", e.content.c_str());
    if (!e.speechBank.empty()) std::printf("  speech-bank: %s\n", e.speechBank.c_str());
    if (!e.speaker.empty()) std::printf("  speaker: %s\n", e.speaker.c_str());
    for (const auto& t : e.tags)
        std::printf("  tag @%d: %s\n", t.position, t.name.c_str());
    return 0;
}

int textList(const std::string& path, const std::string& filter, bool jsonOutput) {
    const auto big = forge::big::File::open(path);
    const std::string needle = lowered(filter);
    json rows = json::array();
    size_t shown = 0;
    for (const auto& b : big.banks()) {
        for (const auto& e : b.entries) {
            if (e.type != 0) continue;
            if (!needle.empty() && lowered(e.name).find(needle) == std::string::npos)
                continue;
            std::string content;
            try { content = forge::textbig::decode(big.entryData(e), 0).content; }
            catch (const std::exception&) { content = "<decode error>"; }
            if (jsonOutput) {
                rows.push_back({{"name", e.name}, {"id", e.id}, {"content", content}});
            } else {
                if (content.size() > 70) content = content.substr(0, 67) + "...";
                std::printf("  %-40s %s\n", e.name.c_str(), content.c_str());
            }
            ++shown;
        }
    }
    if (jsonOutput) std::puts(json{{"count", shown}, {"entries", rows}}.dump(2).c_str());
    else std::printf("%zu string entries%s\n", shown,
                     needle.empty() ? "" : (" matching \"" + filter + "\"").c_str());
    return 0;
}

int textSet(const std::string& path, const std::string& name,
            const std::string& content, const std::string& outPath,
            bool inPlace, std::optional<uint32_t> requestedId,
            const std::string& donor, const std::string& speaker,
            const std::string& speechBank, bool jsonOutput) {
    namespace fs = std::filesystem;
    auto file = forge::big::File::open(path);
    forge::textbig::Entry value;
    value.type = 0;
    value.content = content;
    value.identifier = name;
    value.speaker = speaker;
    value.speechBank = speechBank;
    const auto result =
        forge::textbig::upsertString(file, name, std::move(value), requestedId, donor);

    const fs::path destination = inPlace ? fs::path(path) : fs::path(outPath);
    if (!destination.parent_path().empty())
        fs::create_directories(destination.parent_path());
    writeAllBytes(destination.string(), file.serialize());

    // Reopen the emitted archive before reporting success. This catches
    // structural BIG writer regressions at the mutation boundary.
    const auto verified = forge::big::File::open(destination);
    const forge::big::Entry* found = nullptr;
    for (const auto& bank : verified.banks())
        for (const auto& candidate : bank.entries)
            if (candidate.name == name) found = &candidate;
    if (found == nullptr || found->id != result.id ||
        forge::textbig::decode(verified.entryData(*found), (int)found->type).content !=
            content)
        throw std::runtime_error("text set: output failed reopen verification");

    if (jsonOutput) {
        std::puts(json{{"name", result.name},
                       {"id", result.id},
                       {"bank", result.bank},
                       {"mode", result.added ? "add" : "replace"},
                       {"out", destination.string()}}
                      .dump(2).c_str());
    } else {
        std::printf("%s text %s (id %u) in %s\n",
                    result.added ? "added" : "replaced", result.name.c_str(),
                    result.id, result.bank.c_str());
        std::printf("  wrote %s\n", destination.string().c_str());
    }
    return 0;
}

int textImport(const std::string& path, const std::string& manifestPath,
               const std::string& outPath, bool inPlace,
               const std::string& donorOverride, bool jsonOutput) {
    namespace fs = std::filesystem;
    std::ifstream manifestStream(manifestPath);
    if (!manifestStream)
        throw std::runtime_error("text import: cannot open " + manifestPath);
    json manifest;
    manifestStream >> manifest;
    if (!manifest.contains("entries") || !manifest["entries"].is_array())
        throw std::runtime_error("text import: manifest requires an entries array");

    auto file = forge::big::File::open(path);
    const std::string defaultSpeaker = manifest.value("speaker", "");
    const std::string defaultSpeechBank = manifest.value("speech_bank", "");
    const std::string defaultDonor =
        donorOverride.empty() ? manifest.value("donor", "") : donorOverride;
    std::set<std::string> seen;
    std::vector<forge::textbig::UpsertResult> results;
    std::map<std::string, std::string> expectedContent;
    for (const auto& row : manifest["entries"]) {
        if (!row.is_object() || !row.contains("name") || !row.contains("content"))
            throw std::runtime_error(
                "text import: every entry requires string name and content");
        const std::string name = row.at("name").get<std::string>();
        if (!seen.insert(name).second)
            throw std::runtime_error("text import: duplicate manifest name " + name);
        forge::textbig::Entry value;
        value.type = 0;
        value.content = row.at("content").get<std::string>();
        value.identifier = name;
        value.speaker = row.value("speaker", defaultSpeaker);
        value.speechBank = row.value("speech_bank", defaultSpeechBank);
        std::optional<uint32_t> id;
        if (row.contains("id")) id = row.at("id").get<uint32_t>();
        const std::string donor = row.value("donor", defaultDonor);
        results.push_back(forge::textbig::upsertString(
            file, name, std::move(value), id, donor));
        expectedContent[name] = row.at("content").get<std::string>();
    }
    if (results.empty())
        throw std::runtime_error("text import: manifest has no entries");

    const fs::path destination = inPlace ? fs::path(path) : fs::path(outPath);
    if (!destination.parent_path().empty())
        fs::create_directories(destination.parent_path());
    writeAllBytes(destination.string(), file.serialize());

    const auto verified = forge::big::File::open(destination);
    std::map<std::string, const forge::big::Entry*> emitted;
    for (const auto& bank : verified.banks())
        for (const auto& entry : bank.entries)
            emitted[entry.name] = &entry;
    for (const auto& result : results) {
        const auto it = emitted.find(result.name);
        if (it == emitted.end() || it->second->id != result.id ||
            forge::textbig::decode(verified.entryData(*it->second),
                                   (int)it->second->type).content !=
                expectedContent.at(result.name))
            throw std::runtime_error(
                "text import: output failed reopen verification for " + result.name);
    }

    if (jsonOutput) {
        json rows = json::array();
        for (const auto& result : results)
            rows.push_back({{"name", result.name}, {"id", result.id},
                            {"mode", result.added ? "add" : "replace"}});
        std::puts(json{{"count", results.size()},
                       {"out", destination.string()},
                       {"entries", rows}}
                      .dump(2).c_str());
    } else {
        std::printf("imported %zu text entries\n", results.size());
        for (const auto& result : results)
            std::printf("  %-48s TextID %u (%s)\n", result.name.c_str(),
                        result.id, result.added ? "added" : "replaced");
        std::printf("  wrote %s\n", destination.string().c_str());
    }
    return 0;
}

// List a retail BIG asset bank: summary + per-bank counts, capped entry preview
// (or all entries whose name contains `nameFilter`). Handles the huge banks
// (textures/graphics .big) without flooding.
int bigList(const std::string& path, const std::string& nameFilter, bool jsonOutput) {
    const auto file = forge::big::File::open(path);
    const std::string needle = lowered(nameFilter);
    size_t total = 0;
    for (const auto& b : file.banks()) total += b.entries.size();

    if (jsonOutput) {
        json banks = json::array();
        for (const auto& b : file.banks()) {
            json ents = json::array();
            for (const auto& e : b.entries) {
                if (!needle.empty() &&
                    lowered(e.name).find(needle) == std::string::npos)
                    continue;
                ents.push_back({{"name", e.name}, {"length", e.length},
                                {"type", e.type}, {"id", e.id}});
            }
            banks.push_back({{"bank", b.name}, {"count", b.entries.size()},
                             {"entries", ents}});
        }
        std::puts(json{{"file", path}, {"content_type", file.contentType()},
                       {"total_entries", total}, {"banks", banks}}
                      .dump(2).c_str());
        return 0;
    }

    std::printf("BIG: %s (contentType %u, %zu banks, %zu entries)\n", path.c_str(),
                file.contentType(), file.banks().size(), total);
    for (const auto& b : file.banks()) {
        std::printf("\n[%s] %zu entries\n", b.name.c_str(), b.entries.size());
        size_t shown = 0, matched = 0;
        for (const auto& e : b.entries) {
            if (!needle.empty() &&
                lowered(e.name).find(needle) == std::string::npos)
                continue;
            ++matched;
            if (needle.empty() && shown >= 12) continue;  // preview cap
            std::printf("  %-48s %8u bytes\n", e.name.c_str(), e.length);
            ++shown;
        }
        if (needle.empty() && b.entries.size() > shown)
            std::printf("  ... %zu more (filter by name to see them)\n",
                        b.entries.size() - shown);
        else if (!needle.empty())
            std::printf("  (%zu match \"%s\")\n", matched, nameFilter.c_str());
    }
    return 0;
}

// Apply an .fmp's BIN-record banks onto a base game-root, producing a full
// drop-in root. This normalizes an .fmp mod into the same root-based shape the
// diff/merge pipeline consumes: each .fmp entry (name, definition, raw payload)
// replaces or adds the matching compiled-def record.
// NOTE: the .fmp's *LinkMetaData banks (names.bin offset / cross-bin ID fixups)
// are NOT applied here — for a same-vanilla base the raw records line up, and
// record/field-level merge only needs the payloads. Full link fixup + names.bin
// merge is a follow-up for producing a directly-playable install from an .fmp.
int fmpApply(const std::string& baseRoot, const std::string& fmpPath,
             const std::string& outRoot) {
    namespace fs = std::filesystem;
    const auto pkg = forge::big::File::open(fmpPath);

    const fs::path baseDefs = fs::path(baseRoot) / "data" / "CompiledDefs";
    const fs::path defsOut = fs::path(outRoot) / "data" / "CompiledDefs";
    fs::create_directories(defsOut);

    // game.bin gets the GameBINEntries records; its save() rewrites names.bin
    // preserving existing offsets and appending any new record names, so the
    // copied script/frontend bins still resolve against it.
    auto game = openDefs(baseRoot, "game.bin");
    size_t replaced = 0, added = 0;
    if (const auto* bank = pkg.findBank("GameBINEntries")) {
        for (const auto& e : bank->entries) {
            auto data = pkg.entryData(e);
            const forge::bin::Entry* existing =
                e.name.empty() ? nullptr : game.find(e.name);
            if (existing != nullptr) {
                game.setEntryData((size_t)(existing - game.entries().data()),
                                  std::move(data));
                ++replaced;
            } else {
                game.addEntry(e.definition, e.name, std::move(data));
                ++added;
            }
        }
    }
    game.save(defsOut / "names.bin", defsOut / "game.bin");

    // Copy the untouched sibling bins so the out-root is a complete install.
    for (const char* sib : {"script.bin", "frontend.bin"}) {
        if (fs::exists(baseDefs / sib))
            fs::copy_file(baseDefs / sib, defsOut / sib,
                          fs::copy_options::overwrite_existing);
    }
    // Warn if the .fmp changes script/frontend defs — not applied yet (shared
    // names.bin needs a multi-bin pass).
    for (const char* b : {"ScriptBINEntries", "FrontEndBINEntries"}) {
        const auto* bank = pkg.findBank(b);
        if (bank != nullptr && !bank->entries.empty())
            std::fprintf(stderr,
                         "  warning: %s has %zu entries, NOT applied (multi-bin "
                         "shared-names apply is a follow-up)\n",
                         b, bank->entries.size());
    }

    std::printf("applied %s onto %s -> %s (game.bin: %zu replaced, %zu added)\n",
                fmpPath.c_str(), baseRoot.c_str(), outRoot.c_str(), replaced, added);
    return 0;
}

// Export the game.bin changes of a modded root (vs base) as an .fmp: a BIG
// archive whose GameBINEntries bank holds the added/changed records. Writes the
// standard 16-bank .fmp skeleton (contentType 510). Raw-record export — the
// GameBINLinkMetaData / names banks are left empty (fine for FableForge's own
// apply/merge; full link generation for Fable-Explorer parity is a follow-up).
int fmpExport(const std::string& baseRoot, const std::string& moddedRoot,
              const std::string& outFmp) {
    const auto base = openDefs(baseRoot, "game.bin");
    const auto mod = openDefs(moddedRoot, "game.bin");

    std::map<std::string, const forge::bin::Entry*> baseByName;
    for (const auto& e : base.entries())
        if (!e.name.empty()) baseByName[e.name] = &e;

    forge::big::File fmp;
    fmp.setVersion(101);
    fmp.setContentType(510);
    static const char* kBanks[] = {
        "GameBINEntries", "GameBINLinkMetaData", "ScriptBINEntries",
        "ScriptBINLinkMetaData", "FrontEndBINEntries", "FrontEndBINLinkMetaData",
        "names", "graphics", "graphicsLinkMetaData", "maintextures", "guitextures",
        "frontendtextures", "effects", "text", "FinalAlbionWAD", "FinalAlbionSTB"};
    for (uint32_t i = 0; i < 16; ++i) fmp.addBank(kBanks[i], i);
    forge::big::Bank* gameBank = const_cast<forge::big::Bank*>(fmp.findBank("GameBINEntries"));

    size_t changed = 0, added = 0;
    for (const auto& me : mod.entries()) {
        if (me.name.empty()) continue;
        auto it = baseByName.find(me.name);
        const bool isAdd = it == baseByName.end();
        const bool isChange = !isAdd && it->second->data != me.data;
        if (!isAdd && !isChange) continue;

        forge::big::Entry e;
        e.name = me.name;
        e.id = (uint32_t)me.indexInDefinition;
        e.subHeader.assign(me.definition.begin(), me.definition.end());
        e.subHeader.push_back(0);           // ASCIIZ def-type
        e.data = me.data;
        gameBank->entries.push_back(std::move(e));
        if (isAdd) ++added; else ++changed;
    }

    writeAllBytes(outFmp, fmp.serialize());
    std::printf("exported %zu game.bin records (%zu changed, %zu added) -> %s\n",
                changed + added, changed, added, outFmp.c_str());
    return 0;
}

// Apply a bsdiff game.bin.patch onto a base game-root, producing a full root
// (patched game.bin + copied sibling bins). Used to normalize .patch mods.
void patchToRoot(const std::string& baseRoot, const std::string& patchPath,
                 const std::string& outRoot) {
    namespace fs = std::filesystem;
    const fs::path baseDefs = fs::path(baseRoot) / "data" / "CompiledDefs";
    const fs::path defsOut = fs::path(outRoot) / "data" / "CompiledDefs";
    fs::create_directories(defsOut);
    const auto oldFile = readAllBytes((baseDefs / "game.bin").string());
    const auto patch = readAllBytes(patchPath);
    writeAllBytes((defsOut / "game.bin").string(),
                  forge::bspatch::apply(oldFile, patch));
    for (const char* sib : {"names.bin", "script.bin", "frontend.bin"})
        if (fs::exists(baseDefs / sib))
            fs::copy_file(baseDefs / sib, defsOut / sib,
                          fs::copy_options::overwrite_existing);
}

// Top-level heterogeneous mod merge: normalize each source (game-root dir, .fmp,
// or bsdiff .patch) to a game-root, then run the field-level game.bin merge over
// all of them in load order, writing a drop-in overlay (optionally staged).
int modsMerge(const std::string& baseRoot, const std::string& outDir,
              const std::vector<std::string>& sources,
              const std::string& fieldSchema, bool doStage, bool jsonOutput) {
    namespace fs = std::filesystem;
    const fs::path tmp = fs::temp_directory_path() / "forge_mods_merge";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    std::vector<std::string> roots;
    for (size_t i = 0; i < sources.size(); ++i) {
        const std::string& s = sources[i];
        const std::string ext = fs::path(s).extension().string();
        if (ext == ".fmp" || ext == ".FMP") {
            const std::string root = (tmp / ("src" + std::to_string(i))).string();
            fmpApply(baseRoot, s, root);
            roots.push_back(root);
        } else if (ext == ".patch" || ext == ".PATCH") {
            const std::string root = (tmp / ("src" + std::to_string(i))).string();
            patchToRoot(baseRoot, s, root);
            roots.push_back(root);
        } else if (fs::exists(fs::path(s) / "data" / "CompiledDefs" / "game.bin")) {
            roots.push_back(s);  // already a game-root
        } else {
            std::fprintf(stderr, "mods merge: unrecognized source (need dir/.fmp/"
                                 ".patch): %s\n", s.c_str());
            return 1;
        }
    }

    std::printf("normalized %zu source(s) -> %zu game-root(s); field-level merge:\n",
                sources.size(), roots.size());
    const int rc = mergeDefs(baseRoot, outDir, "game.bin", roots, /*picks*/ "",
                             fieldSchema, jsonOutput);
    if (rc != 0) return rc;

    // --- Level TNG merge (loose .tng under data/Levels of dir sources) ---------
    // .fmp/.patch temp roots carry no loose TNGs (their level data lives in a WAD
    // bank — a follow-up), so only real dir sources contribute here.
    const fs::path baseLevels = fs::path(baseRoot) / "data" / "Levels";
    // key (path relative to data/Levels) -> [(label, absPath) that differ from base]
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> tngChangers;
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> qstChangers;
    if (fs::exists(baseLevels)) {
        for (size_t si = 0; si < sources.size(); ++si) {
            const std::string& s = sources[si];
            const fs::path srcLevels = fs::path(s) / "data" / "Levels";
            if (!fs::is_directory(srcLevels)) continue;
            const std::string label = fs::path(s).filename().string();
            for (auto& de : fs::recursive_directory_iterator(srcLevels)) {
                if (!de.is_regular_file()) continue;
                const std::string ext = de.path().extension().string();
                if (ext != ".tng" && ext != ".qst") continue;
                const std::string key =
                    fs::relative(de.path(), srcLevels).generic_string();
                const fs::path basePath = baseLevels / key;
                // Changed vs base (or new) => a candidate for this level.
                bool differs = true;
                if (fs::exists(basePath))
                    differs = readAllBytes(de.path().string()) !=
                              readAllBytes(basePath.string());
                if (differs)
                    (ext == ".tng" ? tngChangers : qstChangers)[key].push_back(
                        {label, de.path().string()});
            }
        }
    }

    size_t tngCopied = 0, tngMerged = 0, tngThingConflicts = 0;
    for (const auto& [key, changers] : tngChangers) {
        const fs::path outTng = fs::path(outDir) / "data" / "Levels" / key;
        const fs::path baseTng = baseLevels / key;
        if (changers.size() == 1 || !fs::exists(baseTng)) {
            // Single editor (or no base to 3-way against): take the last version.
            fs::create_directories(outTng.parent_path());
            fs::copy_file(changers.back().second, outTng,
                          fs::copy_options::overwrite_existing);
            ++tngCopied;
        } else {
            std::vector<std::string> modTngs;
            for (const auto& [lbl, path] : changers) modTngs.push_back(path);
            const auto tr = mergeTngFiles(baseTng.string(), modTngs, outTng.string());
            tngThingConflicts += tr.conflicts;
            ++tngMerged;
        }
    }
    if (!tngChangers.empty())
        std::printf("level TNG merge: %zu levels (%zu thing-merged, %zu single-"
                    "editor copies), %zu thing conflicts\n",
                    tngChangers.size(), tngMerged, tngCopied, tngThingConflicts);

    // --- Quest-registry QST merge (data/Levels/*.qst of dir sources) -----------
    // Statement-level union keyed by quest name: flag flips in place, new
    // quests appended; same-name flag conflicts take the load-order winner.
    size_t qstCopied = 0, qstMerged = 0, qstConflicts = 0;
    for (const auto& [key, changers] : qstChangers) {
        const fs::path outQst = fs::path(outDir) / "data" / "Levels" / key;
        const fs::path baseQst = baseLevels / key;
        if (changers.size() == 1 || !fs::exists(baseQst)) {
            // Single editor (or no base to 3-way against): take the last version.
            fs::create_directories(outQst.parent_path());
            fs::copy_file(changers.back().second, outQst,
                          fs::copy_options::overwrite_existing);
            ++qstCopied;
        } else {
            std::vector<std::string> modQsts;
            for (const auto& [lbl, path] : changers) modQsts.push_back(path);
            const auto qr = mergeQstFiles(baseQst.string(), modQsts,
                                          outQst.string(), /*picks*/ {});
            qstConflicts += qr.conflicts.size();
            ++qstMerged;
        }
    }
    if (!qstChangers.empty())
        std::printf("quest QST merge: %zu registries (%zu statement-merged, "
                    "%zu single-editor copies), %zu quest conflicts\n",
                    qstChangers.size(), qstMerged, qstCopied, qstConflicts);

    if (doStage) {
        const auto result = forge::stage::apply(baseRoot, outDir);
        std::printf("staged %zu file(s) onto %s (%zu backed up)\n",
                    result.staged.size(), baseRoot.c_str(), result.backedUp.size());
    }
    return 0;
}

// Read a Fable "FableSave!" save and print its decoded HEADER section (world/
// region/save meta + gameplay flags). Read-only. Hero stats (gold/morality) are
// in the entity graph and not decoded yet — see forge/save.hpp.
int saveRead(const std::string& path, bool jsonOutput) {
    const auto s = forge::save::File::read(path);
    if (jsonOutput) {
        json fields = json::array();
        for (const auto& f : s.header)
            fields.push_back({{"name", f.name}, {"type", f.type},
                              {"value", f.value}, {"tag_ok", f.tagOk}});
        json stats = json::array();
        for (const auto& f : s.heroStats)
            stats.push_back({{"name", f.name}, {"type", f.type}, {"value", f.value}});
        char sig[16];
        std::snprintf(sig, sizeof(sig), "%08x", s.signature);
        std::puts(json{{"file", path}, {"signature", sig},
                       {"file_size", s.fileSize},
                       {"header_ulen", s.chunk0Ulen},
                       {"entities_ulen", s.chunk1Ulen},
                       {"section", s.sectionName},
                       {"all_tags_ok", s.allTagsOk},
                       {"header", fields},
                       {"hero_stats", stats}}
                      .dump(2)
                      .c_str());
        return 0;
    }
    std::printf("FableSave!: %s\n", path.c_str());
    std::printf("  signature %08x, %zu bytes (HEADER %zu / ENTITIES %zu inflated)\n",
                s.signature, s.fileSize, s.chunk0Ulen, s.chunk1Ulen);
    std::printf("  [%s] section, all tags %s\n", s.sectionName.c_str(),
                s.allTagsOk ? "verified" : "MISMATCH");
    for (const auto& f : s.header)
        std::printf("    %-34s %-8s %s%s\n", f.name.c_str(), f.type.c_str(),
                    f.value.c_str(), f.tagOk ? "" : "   [TAG MISMATCH]");
    if (!s.heroStats.empty()) {
        std::printf("  [CTCHeroStats] hero live stats\n");
        for (const auto& f : s.heroStats)
            std::printf("    %-34s %-8s %s\n", f.name.c_str(), f.type.c_str(),
                        f.value.c_str());
    } else {
        std::printf("  (hero stats not found — CTCHeroStats cell absent)\n");
    }
    return 0;
}

// Hidden: read a BIG/.fmp and re-serialize it (validates forge::big::serialize).
int fmpRewrite(const std::string& inPath, const std::string& outPath) {
    const auto f = forge::big::File::open(inPath);
    writeAllBytes(outPath, f.serialize());
    std::printf("rewrote %s -> %s\n", inPath.c_str(), outPath.c_str());
    return 0;
}

// Hidden: decompress a raw bzip2 stream (for validating forge::bunzip).
int bunzipFile(const std::string& inPath, const std::string& outPath) {
    const auto in = readAllBytes(inPath);
    const auto out = forge::bunzip::decompress(in.data(), in.size());
    writeAllBytes(outPath, out);
    std::printf("bunzip %s -> %s (%zu bytes)\n", inPath.c_str(), outPath.c_str(),
                out.size());
    return 0;
}

// --- foliage: named local-detail scenery palette ---------------------------
json foliageEntryJson(const forge::foliage::FoliageType& e) {
    json j = {
        {"paletteIndex", e.paletteIndex},
        {"meshIdx", e.meshIdx},
        {"meshName", e.meshName},
        {"texture", e.gbankTexture},
        {"label", e.label},
        {"category", forge::foliage::categoryName(e.category)},
        {"primType", forge::foliage::primTypeName(e.primType)},
        {"fadeStart", e.fadeStart},
        {"fadeEnd", e.fadeEnd},
    };
    if (e.instanceCount >= 0) j["instanceCount"] = e.instanceCount;
    if (e.shadowMeshIdx) j["shadowMeshIdx"] = e.shadowMeshIdx;
    if (e.zspriteMeshIdx) j["zspriteMeshIdx"] = e.zspriteMeshIdx;
    return j;
}

void foliagePrintTable(const std::vector<forge::foliage::FoliageType>& entries) {
    std::printf("  %-4s %-8s %-9s %-8s %-30s %-24s %s\n",
                "type", "count", "class", "meshIdx", "label", "texture", "category");
    for (const auto& e : entries) {
        char count[16];
        if (e.instanceCount >= 0) std::snprintf(count, sizeof(count), "%d", e.instanceCount);
        else std::snprintf(count, sizeof(count), "-");
        const char* cls = e.primType == forge::foliage::PrimType::RepeatedMesh ? "grass"
                        : e.primType == forge::foliage::PrimType::ZSpriteBatch ? "z-sprite"
                        : "near";
        std::printf("  %-4d %-8s %-9s %-8u %-30s %-24s %s\n",
                    e.paletteIndex, count, cls, e.meshIdx,
                    e.label.c_str(),
                    e.gbankTexture.empty() ? "-" : e.gbankTexture.c_str(),
                    forge::foliage::categoryName(e.category));
    }
}

int foliagePalette(bool jsonOutput) {
    const auto& cat = forge::foliage::brushCatalog();
    if (jsonOutput) {
        json arr = json::array();
        for (const auto& e : cat) arr.push_back(foliageEntryJson(e));
        std::puts(json{{"source", "built-in (StartOakValeWest, RECOVERED-HIGH)"},
                       {"count", cat.size()},
                       {"types", arr}}
                      .dump(2)
                      .c_str());
        return 0;
    }
    std::printf("foliage brush palette: %zu named scenery types (built-in, StartOakValeWest)\n",
                cat.size());
    foliagePrintTable(cat);
    return 0;
}

int foliageRead(const std::vector<std::string>& args, bool jsonOutput) {
    // args: <record.bin> [--offset <n>]
    if (args.empty()) return usage();
    std::optional<size_t> offset;
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--offset" && i + 1 < args.size()) {
            offset = static_cast<size_t>(std::strtoul(args[++i].c_str(), nullptr, 0));
        }
    }
    const auto pal = forge::foliage::readRecordFile(args[0], offset);
    if (jsonOutput) {
        json arr = json::array();
        for (const auto& e : pal.entries) arr.push_back(foliageEntryJson(e));
        std::puts(json{{"source", pal.source},
                       {"offset", pal.offset},
                       {"count", pal.entries.size()},
                       {"types", arr}}
                      .dump(2)
                      .c_str());
        return 0;
    }
    std::printf("%s: %zu-entry local-detail palette @ +0x%zx\n",
                pal.source.c_str(), pal.entries.size(), pal.offset);
    foliagePrintTable(pal.entries);
    return 0;
}

int foliageMeshInfo(const std::string& path, bool jsonOutput) {
    const auto meshes = forge::foliage::readMeshBoundingSpheres(path);
    if (jsonOutput) {
        json rows = json::array();
        for (const auto& [meshIdx, mesh] : meshes)
            rows.push_back({{"meshIdx", meshIdx}, {"centre", {mesh.x, mesh.y, mesh.z}},
                            {"radius", mesh.radius}, {"polyCount", mesh.polyCount}});
        std::puts(json{{"source", path}, {"count", meshes.size()}, {"meshes", rows}}
                      .dump(2).c_str());
        return 0;
    }
    size_t polygonKnown = 0;
    for (const auto& [meshIdx, mesh] : meshes) {
        if (mesh.polyCountKnown()) ++polygonKnown;
        std::printf("%5u sphere=(%.4f,%.4f,%.4f) r=%.4f polyCount=%u\n",
                    meshIdx, mesh.x, mesh.y, mesh.z, mesh.radius, mesh.polyCount);
    }
    std::printf("%zu mesh spheres; %zu polygon counts decoded\n",
                meshes.size(), polygonKnown);
    return 0;
}

int foliageInstances(const std::vector<std::string>& args, bool jsonOutput) {
    // args: <chunk.lev> [xlo xhi ylo yhi]
    if (args.empty()) return usage();
    std::optional<forge::foliage::ScanBounds> bounds;
    if (args.size() >= 5) {
        bounds = forge::foliage::ScanBounds{
            std::stof(args[1]), std::stof(args[2]),
            std::stof(args[3]), std::stof(args[4])};
    }
    const auto scan = forge::foliage::scanInstances(args[0], bounds);
    const size_t rotations=std::count_if(scan.instances.begin(),scan.instances.end(),
        [](const forge::foliage::Instance& instance){return instance.hasRotation;});
    float minimumScale=std::numeric_limits<float>::infinity(),maximumScale=0.0f;
    for(const auto& instance:scan.instances)if(instance.hasRotation){
        minimumScale=std::min(minimumScale,instance.scale);maximumScale=std::max(maximumScale,instance.scale);
    }
    // Map paletteIndex -> label from the named catalog.
    const auto& cat = forge::foliage::brushCatalog();
    auto typeLabel = [&](int idx) -> const char* {
        for (const auto& e : cat)
            if (e.paletteIndex == idx) return e.label.c_str();
        return "(unknown type)";
    };
    if (jsonOutput) {
        json types = json::array();
        for (const auto& t : scan.perType)
            types.push_back({{"paletteIndex", t.paletteIndex},
                             {"label", typeLabel(t.paletteIndex)},
                             {"count", t.count}});
        std::puts(json{{"source", scan.source},
                       {"framesDecoded", scan.framesDecoded},
                       {"instanceFrames", scan.instanceFrames},
                       {"instances", scan.instances.size()},
                       {"rotations", rotations},
                       {"scale",{{"minimum",rotations?minimumScale:0.0f},{"maximum",maximumScale}}},
                       {"bound", scan.boundInstances},
                       {"unbound", scan.unboundInstances},
                       {"distinctCells", scan.distinctCells},
                       {"perType", types},
                       {"extent", {{"xlo", scan.minX}, {"xhi", scan.maxX},
                                   {"ylo", scan.minY}, {"yhi", scan.maxY},
                                   {"zlo", scan.minZ}, {"zhi", scan.maxZ}}}}
                      .dump(2)
                      .c_str());
        return 0;
    }
    std::printf("%s\n", scan.source.c_str());
    std::printf("  %zu baked instances across %d instance-frames (%d LZO frames decoded)\n",
                scan.instances.size(), scan.instanceFrames, scan.framesDecoded);
    if (!scan.instances.empty()) {
        std::printf("  world extent: X[%.0f..%.0f] Y[%.0f..%.0f] Z[%.1f..%.1f]  %d cells (16x16)\n",
                    scan.minX, scan.maxX, scan.minY, scan.maxY, scan.minZ, scan.maxZ,
                    scan.distinctCells);
        std::printf("  bound to a type: %d   unbound: %d\n",
                    scan.boundInstances, scan.unboundInstances);
        std::printf("  source-backed rotations: %zu\n",rotations);
        if(rotations)std::printf("  source-backed scale: %.6f..%.6f\n",minimumScale,maximumScale);
        for (const auto& t : scan.perType)
            std::printf("    type %-2d  %-30s %6d\n",
                        t.paletteIndex, typeLabel(t.paletteIndex), t.count);
    }
    return 0;
}

int uiCloneObjectModel(const std::string& gameRoot, const std::string& schemaPath,
                       const std::string& donorName, const std::string& newName,
                       uint32_t modelId, const std::string& outRootArg) {
    namespace fs = std::filesystem;
    const fs::path retailDefs = fs::path(gameRoot) / "data" / "CompiledDefs";
    const fs::path outputDefs = fs::path(outRootArg) / "data" / "CompiledDefs";
    const fs::path sourceDefs =
        (fs::exists(outputDefs / "game.bin") && fs::exists(outputDefs / "names.bin"))
            ? outputDefs : retailDefs;
    auto file = forge::bin::File::open(sourceDefs / "names.bin", sourceDefs / "game.bin");
    const auto schema = forge::defschema::Schema::load(schemaPath);
    forge::uidef::CloneObjectModelResult r;
    try {
        r = forge::uidef::cloneObjectModel(file, schema, donorName, newName, modelId);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "ui clone-object-model: %s\n", e.what());
        return 1;
    }
    fs::create_directories(outputDefs);
    file.save(outputDefs / "names.bin", outputDefs / "game.bin");
    std::printf("ui clone-object-model: %s -> %s (entry %zu -> %zu)\n"
                "  Graphic.modelId %u -> %u\n  rewritten self refs:",
                donorName.c_str(), newName.c_str(), r.donorIndex, r.index,
                r.oldModelId, r.modelId);
    for (size_t off : r.rewrittenSelfReferenceOffsets) std::printf(" %zu", off);
    std::printf("\n  wrote %s\n", (outputDefs / "game.bin").string().c_str());
    return 0;
}

int worldAddTiledRegion(const std::string& rootArg,
                        const std::string& manifestPath,
                        const std::string& regionName) {
    namespace fs = std::filesystem;
    if (regionName.empty())
        throw std::runtime_error("world add-tiled-region: region name is empty");
    std::ifstream manifestInput(manifestPath);
    if (!manifestInput)
        throw std::runtime_error("world add-tiled-region: cannot open manifest");
    nlohmann::json manifest;
    manifestInput >> manifest;
    if (!manifest.contains("maps") || !manifest["maps"].is_array() ||
        manifest["maps"].empty())
        throw std::runtime_error("world add-tiled-region: manifest.maps is empty");

    const fs::path manifestBase = fs::absolute(fs::path(manifestPath)).parent_path();
    const fs::path levelsDir = fs::path(rootArg) / "data" / "Levels";
    const fs::path bwdPath = levelsDir / "FinalAlbion.bwd";
    const fs::path wldPath = levelsDir / "FinalAlbion.wld";
    const fs::path stbPath = levelsDir / "FinalAlbion_RT.stb";
    forge::bwd::File bwd = forge::bwd::File::parse(bwdPath);
    forge::wld::File wld = forge::wld::File::parse(wldPath);
    const auto stb = forge::stb::Archive::open(stbPath);
    if (bwd.findRegion(regionName) || wld.findRegion(regionName))
        throw std::runtime_error("world add-tiled-region: region already exists");

    struct Tile {
        std::string fullName, stem;
        forge::stbinfo::StaticMapInfoBlock info;
    };
    std::vector<Tile> tiles;
    tiles.reserve(manifest["maps"].size());
    for (const auto& item : manifest["maps"]) {
        for (const char* field : {"levelName", "entryName", "commonRecord"})
            if (!item.contains(field) || !item[field].is_string())
                throw std::runtime_error(std::string("world add-tiled-region: missing ") + field);
        const std::string fullName = item["levelName"].get<std::string>();
        fs::path recordPath = item["commonRecord"].get<std::string>();
        if (!recordPath.is_absolute()) recordPath = manifestBase / recordPath;
        const auto record = readAllBytes(recordPath.string());
        if (record.size() < forge::stbinfo::kInfoBlockSize)
            throw std::runtime_error("world add-tiled-region: truncated common record");
        const auto info = forge::stbinfo::readInfoBlock(record.data());
        if (info.mapWidth <= 0 || info.mapHeight <= 0)
            throw std::runtime_error("world add-tiled-region: invalid map dimensions");
        const std::string prefix = "Data\\Levels\\FinalAlbion\\";
        if (fullName.rfind(prefix, 0) != 0 || fullName.size() <= prefix.size() + 4 ||
            fullName.substr(fullName.size() - 4) != ".lev")
            throw std::runtime_error("world add-tiled-region: unsupported levelName " + fullName);
        const std::string stem = fullName.substr(prefix.size(),
                                                  fullName.size() - prefix.size() - 4);
        if (bwd.findMap(fullName) || wld.findMap("FinalAlbion\\" + stem + ".lev"))
            throw std::runtime_error("world add-tiled-region: map already exists: " + stem);
        const auto* entry = stb.findEntry(item["entryName"].get<std::string>());
        const auto mapIt = std::find_if(stb.staticMaps().begin(), stb.staticMaps().end(),
            [&](const auto& map) { return map.levelName == fullName; });
        if (!entry || mapIt == stb.staticMaps().end())
            throw std::runtime_error("world add-tiled-region: STB registration missing: " + stem);
        const auto liveRecord = stb.readStaticMapRecord(*mapIt);
        const auto liveInfo = forge::stbinfo::readInfoBlock(liveRecord.data());
        if (liveInfo.bankFileIndex != int32_t(entry->id) ||
            liveInfo.worldX != info.worldX || liveInfo.worldY != info.worldY ||
            liveInfo.mapWidth != info.mapWidth || liveInfo.mapHeight != info.mapHeight)
            throw std::runtime_error("world add-tiled-region: STB/common mismatch: " + stem);
        tiles.push_back({fullName, stem, liveInfo});
    }

    auto overlaps = [](int al, int at, int ar, int ab,
                       int bl, int bt, int br, int bb) {
        return al < br && ar > bl && at < bb && ab > bt;
    };
    for (size_t i = 0; i < tiles.size(); ++i) {
        const auto& a = tiles[i].info;
        for (const auto& existing : bwd.maps())
            if (overlaps(a.worldX, a.worldY, a.worldX + a.mapWidth,
                         a.worldY + a.mapHeight, existing.left, existing.top,
                         existing.right, existing.bottom))
                throw std::runtime_error("world add-tiled-region: tile overlaps existing map: " +
                                         tiles[i].stem + " / " + existing.scriptName);
        for (size_t j = 0; j < i; ++j) {
            const auto& b = tiles[j].info;
            if (overlaps(a.worldX, a.worldY, a.worldX + a.mapWidth,
                         a.worldY + a.mapHeight, b.worldX, b.worldY,
                         b.worldX + b.mapWidth, b.worldY + b.mapHeight))
                throw std::runtime_error("world add-tiled-region: tiles overlap");
        }
    }

    forge::bwd::Region binaryRegion;
    binaryRegion.name = regionName;
    binaryRegion.displayName = regionName;
    binaryRegion.creatureGen = 1;
    binaryRegion.soundThemes = 1;
    forge::wld::Region textRegion;
    textRegion.regionName = regionName;
    textRegion.displayName = regionName;
    uint64_t nextUid = bwd.maxMapUid() + 1;
    for (const auto& tile : tiles) {
        forge::bwd::MapInfo bm;
        bm.levelName = tile.fullName;
        bm.scriptName = tile.stem;
        bm.left = tile.info.worldX;
        bm.top = tile.info.worldY;
        bm.right = bm.left + tile.info.mapWidth;
        bm.bottom = bm.top + tile.info.mapHeight;
        bm.mapUid = nextUid++;
        const int slot = bwd.addMap(bm);
        binaryRegion.contains.push_back(slot);
        binaryRegion.sees.push_back(slot);

        forge::wld::Map wm;
        wm.index = slot;
        wm.mapX = bm.left;
        wm.mapY = bm.top;
        wm.levelName = "FinalAlbion\\" + tile.stem + ".lev";
        wm.levelScriptName = tile.stem;
        wm.mapUid = uint32_t(bm.mapUid);
        wld.addMap(wm);
        textRegion.containsMaps.push_back(wm.levelName);
        textRegion.seesMaps.push_back(wm.levelName);
    }
    const int regionSlot = bwd.addRegion(std::move(binaryRegion));
    textRegion.index = regionSlot;
    wld.addRegion(std::move(textRegion));

    bwd.write(bwdPath);
    const std::string text = wld.serialize();
    std::ofstream wldOutput(wldPath, std::ios::binary);
    if (!wldOutput) throw std::runtime_error("world add-tiled-region: cannot write WLD");
    wldOutput.write(text.data(), std::streamsize(text.size()));
    if (!wldOutput) throw std::runtime_error("world add-tiled-region: failed writing WLD");
    std::printf("added %zu contiguous maps to one region '%s' (slot %d)\n",
                tiles.size(), regionName.c_str(), regionSlot);
    return 0;
}

// Resolve retail level-transition links across a complete loose TNG corpus.
// CTCDRegionExit and CTCActionUseScriptedHook both serialize destination UIDs,
// but they are distinct mechanisms and are reported separately. This command
// intentionally discovers target types from data instead of assuming that an
// interactive hook must point at the same definition as a walkable exit.
int tngTransitionAudit(const std::string& levelsDir, const std::string& wldPath) {
    namespace fs = std::filesystem;
    struct LocatedThing {
        fs::path path;
        const forge::tng::Thing* thing = nullptr;
    };
    struct LoadedTng {
        fs::path path;
        forge::tng::File file;
    };
    std::vector<LoadedTng> files;
    for (const auto& entry : fs::recursive_directory_iterator(levelsDir)) {
        if (!entry.is_regular_file() ||
            lowered(entry.path().extension().string()) != ".tng")
            continue;
        files.push_back({entry.path(), forge::tng::File::parse(entry.path())});
    }
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
        return a.path.generic_string() < b.path.generic_string();
    });

    const auto world = forge::wld::File::parse(wldPath);
    std::map<std::string, uint64_t> mapUidByStem;
    std::map<std::string, std::vector<std::string>> regionsByMapStem;
    for (const auto& map : world.maps()) {
        fs::path levelPath(map.levelName);
        mapUidByStem[lowered(levelPath.stem().string())] = map.mapUid;
    }
    for (const auto& region : world.regions()) {
        for (const auto& levelName : region.containsMaps) {
            const auto stem = lowered(fs::path(levelName).stem().string());
            regionsByMapStem[stem].push_back(region.regionName);
        }
    }
    std::map<std::string, std::vector<LocatedThing>> byRuntimeUid;
    size_t things = 0, malformedUids = 0;
    auto numericUid = [](std::string_view value) {
        if (value.empty()) return false;
        return std::all_of(value.begin(), value.end(), [](unsigned char c) {
            return std::isdigit(c) != 0;
        });
    };
    size_t filesWithoutMapUid = 0;
    for (const auto& loaded : files) {
        const auto mapUid = mapUidByStem.find(lowered(loaded.path.stem().string()));
        if (mapUid == mapUidByStem.end()) {
            ++filesWithoutMapUid;
            continue;
        }
        for (const auto& thing : loaded.file.things()) {
            ++things;
            const auto uid = thing.find("UID");
            if (!uid || !numericUid(*uid)) {
                ++malformedUids;
                continue;
            }
            const uint64_t serialized = std::stoull(*uid);
            // Retail map-specific TNG serialization stores local IDs under the
            // 0xFFFFFE00 sentinel. Runtime/cross-map references replace that
            // high component with the destination WLD MapUID:
            //   Greatwood exit 0x027A89000000006F -> LookoutPoint MapUID
            //   0x027A89, whose entrance is serialized as ...0000006F.
            const uint64_t runtime = (uint64_t(mapUid->second) << 40) |
                                     (serialized & UINT64_C(0xffffffff));
            byRuntimeUid[std::to_string(runtime)].push_back(
                {loaded.path, &thing});
        }
    }
    size_t duplicateUids = 0;
    for (const auto& [uid, owners] : byRuntimeUid) {
        if (owners.size() < 2) continue;
        ++duplicateUids;
        std::printf("DUPLICATE UID %s (%zu owners)\n", uid.c_str(), owners.size());
        for (const auto& owner : owners)
            std::printf("  %s %s\n", owner.path.string().c_str(),
                        owner.thing->definitionType().c_str());
    }

    size_t exits = 0, hooks = 0, guildPedestals = 0;
    size_t resolved = 0, unresolved = 0;
    size_t zeroLinks = 0, malformedLinks = 0;
    std::map<std::string, size_t> exitTargets, hookTargets;
    std::map<std::string, size_t> guildPedestalsByRegion;
    std::map<std::string, size_t> guildPedestalScripts;
    auto auditLink = [&](const LoadedTng& loaded, const forge::tng::Thing& thing,
                         const forge::tng::CtcBlock& ctc, const char* mechanism,
                         std::map<std::string, size_t>& targetTypes) {
        const auto property = std::find_if(
            ctc.properties.begin(), ctc.properties.end(), [](const auto& p) {
                return lowered(p.key) == "entranceconnectedtouid";
            });
        if (property == ctc.properties.end() || property->value == "0") {
            ++zeroLinks;
            return;
        }
        if (!numericUid(property->value)) {
            ++malformedLinks;
            std::printf("MALFORMED %s LINK %s in %s\n", mechanism,
                        property->value.c_str(), loaded.path.string().c_str());
            return;
        }
        const auto found = byRuntimeUid.find(property->value);
        if (found == byRuntimeUid.end() || found->second.size() != 1) {
            ++unresolved;
            std::printf("UNRESOLVED %s LINK %s from %s UID %s\n", mechanism,
                        property->value.c_str(), loaded.path.string().c_str(),
                        thing.find("UID").value_or("?").c_str());
            return;
        }
        ++resolved;
        const auto& destination = found->second.front();
        ++targetTypes[destination.thing->definitionType()];
    };
    for (const auto& loaded : files) {
        for (const auto& thing : loaded.file.things()) {
            if (thing.findCtc("CTCTeleporter")) {
                ++guildPedestals;
                ++guildPedestalScripts[thing.scriptName().empty()
                                           ? "<NULL>"
                                           : thing.scriptName()];
                const auto stem = lowered(loaded.path.stem().string());
                const auto owners = regionsByMapStem.find(stem);
                if (owners == regionsByMapStem.end()) {
                    ++guildPedestalsByRegion["<map absent from WLD region>"];
                } else {
                    for (const auto& region : owners->second)
                        ++guildPedestalsByRegion[region];
                }
            }
            if (const auto* exit = thing.findCtc("CTCDRegionExit")) {
                ++exits;
                auditLink(loaded, thing, *exit, "region-exit", exitTargets);
            }
            if (const auto* hook = thing.findCtc("CTCActionUseScriptedHook")) {
                const auto teleport = std::find_if(
                    hook->properties.begin(), hook->properties.end(),
                    [](const auto& p) {
                        return lowered(p.key) == "teleporttoregionentrance";
                    });
                if (teleport == hook->properties.end() ||
                    lowered(teleport->value) != "true")
                    continue;
                ++hooks;
                auditLink(loaded, thing, *hook, "interactive-teleporter",
                          hookTargets);
            }
        }
    }
    std::printf("transition corpus: %zu TNGs, %zu things, %zu region exits, "
                "%zu interactive teleporters, %zu guild-map pedestals, "
                "%zu TNGs absent from WLD\n",
                files.size(), things, exits, hooks, guildPedestals,
                filesWithoutMapUid);
    for (const auto& [type, count] : exitTargets)
        std::printf("  region-exit target %-32s %zu\n", type.c_str(), count);
    for (const auto& [type, count] : hookTargets)
        std::printf("  teleporter target  %-32s %zu\n", type.c_str(), count);
    for (const auto& [region, count] : guildPedestalsByRegion)
        std::printf("  guild pedestal     %-32s %zu\n", region.c_str(), count);
    for (const auto& [script, count] : guildPedestalScripts)
        std::printf("  pedestal script    %-32s %zu\n", script.c_str(), count);
    if (guildPedestals)
        std::printf("  note: CTCTeleporter placement is not activation; retail initializes "
                    "it inactive and activation is script/save state\n");
    std::printf("transition links: %zu resolved, %zu unresolved, %zu zero/missing, "
                "%zu malformed; UIDs: %zu duplicate values, %zu malformed things\n",
                resolved, unresolved, zeroLinks, malformedLinks, duplicateUids,
                malformedUids);
    return (unresolved || malformedLinks || duplicateUids) ? 1 : 0;
}

int levImportWorldHeightmap(const std::vector<std::string>& args) {
    if (args.size() < 6 || args.size() > 7) return usage();
    auto file = forge::lev::File::open(args[0]);
    const int sourceWidth = std::stoi(args[3]);
    const int sourceHeight = std::stoi(args[4]);
    const float spacing = std::stof(args[5]);
    const float heightBias = args.size() == 7 ? std::stof(args[6]) : 0.0f;
    const auto raw = readAllBytes(args[2]);
    if (sourceWidth < 2 || sourceHeight < 2)
        throw std::invalid_argument(
            "lev import-world-heightmap: source dimensions must be at least 2x2");
    const size_t sampleCount = static_cast<size_t>(sourceWidth) * sourceHeight;
    if (raw.size() != sampleCount * 4)
        throw std::invalid_argument(
            "lev import-world-heightmap: raw file must contain src-w*src-h float32 samples");
    std::vector<float> samples(sampleCount);
    std::memcpy(samples.data(), raw.data(), raw.size());
    auto field = forge::terrain::fromWorldHeightRaster(
        samples, sourceWidth, sourceHeight, spacing, heightBias);
    if (field.width() != file.width() || field.height() != file.height()) {
        throw std::invalid_argument(
            "lev import-world-heightmap: donor LEV dimensions do not match source world extent (need " +
            std::to_string(field.width()) + "x" + std::to_string(field.height()) + ")");
    }
    field.writeTo(file);
    file.save(args[1]);
    std::printf("imported %dx%d absolute-height raster at %.6g spacing -> %dx%d TLC map, "
                "height bias %.6g -> %s\n",
                sourceWidth, sourceHeight, spacing, field.width(), field.height(),
                heightBias, args[1].c_str());
    return 0;
}

int terrainSplitWorldHeightmap(const std::vector<std::string>& args) {
    if (args.size() != 8) return usage();
    const int sourceWidth = std::stoi(args[1]);
    const int sourceHeight = std::stoi(args[2]);
    const float spacing = std::stof(args[3]);
    const float heightBias = std::stof(args[4]);
    if (sourceWidth < 2 || sourceHeight < 2)
        throw std::invalid_argument("terrain split-world-heightmap: invalid source dimensions");
    const auto raw = readAllBytes(args[0]);
    const size_t count = static_cast<size_t>(sourceWidth) * sourceHeight;
    if (raw.size() != count * sizeof(float))
        throw std::invalid_argument(
            "terrain split-world-heightmap: raw size does not match dimensions");
    std::vector<float> samples(count);
    std::memcpy(samples.data(), raw.data(), raw.size());
    const auto source = forge::terrain::fromWorldHeightRaster(
        samples, sourceWidth, sourceHeight, spacing, heightBias);
    auto parseLayout = [](const std::string& csv) {
        std::vector<int> values;
        size_t start = 0;
        while (start <= csv.size()) {
            const size_t comma = csv.find(',', start);
            const std::string token = csv.substr(start, comma - start);
            if (token.empty())
                throw std::invalid_argument("terrain split-world-heightmap: empty tile dimension");
            values.push_back(std::stoi(token));
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        return values;
    };
    const auto widths = parseLayout(args[5]);
    const auto heights = parseLayout(args[6]);
    const auto tiles = forge::terrain::splitHeightfield(source, widths, heights);
    const std::filesystem::path outDir(args[7]);
    std::filesystem::create_directories(outDir);
    json plan = {
        {"source", args[0]}, {"source_vertices", {sourceWidth, sourceHeight}},
        {"source_spacing", spacing}, {"height_bias", heightBias},
        {"extent_cells", {source.width(), source.height()}},
        {"tile_widths", widths}, {"tile_heights", heights},
        {"sample_order", "row-major y*vertices_x+x; no axis flip"},
        {"tiles", json::array()}
    };
    for (size_t i = 0; i < tiles.size(); ++i) {
        const auto& tile = tiles[i];
        const std::string name = "tile_" + std::to_string(i) + ".f32le.raw";
        std::vector<uint8_t> bytes(
            static_cast<size_t>(tile.heightfield.cellsX()) *
            tile.heightfield.cellsY() * sizeof(float));
        size_t offset = 0;
        for (int y = 0; y < tile.heightfield.cellsY(); ++y)
            for (int x = 0; x < tile.heightfield.cellsX(); ++x) {
                const float value = tile.heightfield.at(x, y);
                std::memcpy(bytes.data() + offset, &value, sizeof(value));
                offset += sizeof(value);
            }
        writeAllBytes((outDir / name).string(), bytes);
        plan["tiles"].push_back({
            {"index", i}, {"file", name},
            {"origin_cells", {tile.originX, tile.originY}},
            {"cells", {tile.heightfield.width(), tile.heightfield.height()}},
            {"vertices", {tile.heightfield.cellsX(), tile.heightfield.cellsY()}},
            {"bytes", bytes.size()}
        });
    }
    std::ofstream manifest(outDir / "tile_plan.json");
    if (!manifest) throw std::runtime_error("cannot write terrain tile plan");
    manifest << plan.dump(2) << '\n';
    std::printf("split %dx%d source -> %zu seam-identical tiles (%dx%d cells) -> %s\n",
                sourceWidth, sourceHeight, tiles.size(), source.width(), source.height(),
                outDir.string().c_str());
    return 0;
}

int terrainVerifyTiledLevels(const std::vector<std::string>& args) {
    if (args.size() != 3) return usage();
    std::ifstream stream(args[0]);
    if (!stream) throw std::runtime_error("verify-tiled-levels: cannot open tile plan");
    json plan;
    stream >> plan;
    const int sourceWidth = plan.at("source_vertices").at(0).get<int>();
    const int sourceHeight = plan.at("source_vertices").at(1).get<int>();
    const float spacing = plan.at("source_spacing").get<float>();
    const float bias = plan.at("height_bias").get<float>();
    const auto raw = readAllBytes(plan.at("source").get<std::string>());
    if (raw.size() != static_cast<size_t>(sourceWidth) * sourceHeight * sizeof(float))
        throw std::runtime_error("verify-tiled-levels: source raster size mismatch");
    std::vector<float> samples(static_cast<size_t>(sourceWidth) * sourceHeight);
    std::memcpy(samples.data(), raw.data(), raw.size());
    const auto source = forge::terrain::fromWorldHeightRaster(
        samples, sourceWidth, sourceHeight, spacing, bias);
    size_t compared = 0, mismatched = 0, tiles = 0;
    for (const auto& tile : plan.at("tiles")) {
        const size_t index = tile.at("index").get<size_t>();
        std::ostringstream leaf;
        leaf << args[2] << std::setw(2) << std::setfill('0') << index << ".lev";
        const auto levelPath = std::filesystem::path(args[1]) / leaf.str();
        const auto lev = forge::lev::File::open(levelPath);
        const int originX = tile.at("origin_cells").at(0).get<int>();
        const int originY = tile.at("origin_cells").at(1).get<int>();
        const int width = tile.at("cells").at(0).get<int>();
        const int height = tile.at("cells").at(1).get<int>();
        if (lev.width() != width || lev.height() != height)
            throw std::runtime_error("verify-tiled-levels: LEV dimensions disagree for " +
                                     levelPath.string());
        ++tiles;
        for (int y = 0; y <= height; ++y) {
            for (int x = 0; x <= width; ++x) {
                const float expected = source.at(originX + x, originY + y);
                const float actual = lev.heightAt(x, y);
                uint32_t expectedBits = 0, actualBits = 0;
                std::memcpy(&expectedBits, &expected, sizeof(expectedBits));
                std::memcpy(&actualBits, &actual, sizeof(actualBits));
                ++compared;
                mismatched += expectedBits != actualBits;
            }
        }
    }
    std::printf("verified %zu tiled LEVs against source: %zu float32 samples, "
                "%zu bit mismatches\n", tiles, compared, mismatched);
    return mismatched ? 1 : 0;
}

int sceneAudit(const std::vector<std::string>& args) {
    if(args.size()<3) throw std::invalid_argument(
        "scene audit: <game-root> <def-schema.json> <level.tng>");
    const std::filesystem::path root=args[0], schemaPath=args[1], tngPath=args[2];
    auto uppercase=[](std::string s){for(char& c:s)c=char(std::toupper((unsigned char)c));return s;};
    const auto defs=forge::bin::File::open(root/"data"/"CompiledDefs"/"names.bin",
                                           root/"data"/"CompiledDefs"/"game.bin");
    const auto schema=forge::defschema::Schema::load(schemaPath);
    const auto things=forge::tng::File::parse(tngPath);
    const auto graphics=forge::big::File::open(root/"data"/"graphics"/"graphics.big");
    const auto* bank=graphics.findBank("MBANK_ALLMESHES");
    if(!bank) throw std::runtime_error("scene audit: MBANK_ALLMESHES missing");
    std::map<uint32_t,const forge::big::Entry*> meshes;
    std::map<std::string,uint32_t> meshNames;
    for(const auto& e:bank->entries){meshes[e.id]=&e;meshNames[uppercase(e.name)]=e.id;}
    size_t positioned=0,resolved=0,noDef=0,noGraphic=0,missingMesh=0,decodeFail=0;
    size_t helperCount=0,compoundHelpers=0,skinnedInstances=0;
    std::map<std::string,size_t> omissions, helperKinds, resolvedTypes, skinnedDefs;
    std::set<uint32_t> diffuseTextures, missingDiffuseTextures;
    const auto textures=forge::big::File::open(root/"data"/"graphics"/"pc"/"textures.big");
    std::set<uint32_t> textureIds;
    std::map<uint32_t,const forge::big::Entry*> textureEntries;
    for(const auto& b:textures.banks())for(const auto& e:b.entries){textureIds.insert(e.id);textureEntries[e.id]=&e;}
    for(const auto& thing:things.things()) {
        if(!thing.findCtc("CTCPhysicsStandard")&&!thing.findCtc("CTCPhysicsNavigator"))continue;
        ++positioned;uint32_t model=0;
        if(const auto ov=thing.find("GraphicOverride")) {
            std::string name=*ov;if(name.size()>=2&&name.front()=='"'&&name.back()=='"')name=name.substr(1,name.size()-2);
            auto it=meshNames.find(uppercase(name));if(it!=meshNames.end())model=it->second;
        }
        const auto* def=defs.find(thing.definitionType());
        if(!def){++noDef;++omissions[thing.definitionType()+" [definition missing]"];continue;}
        if(!model) try {
            const auto decoded=forge::defdecode::decode(*def,schema);
            for(const auto& field:decoded.fields)if(field.name=="Graphic"&&field.value.size()>=8){
                std::memcpy(&model,field.value.data()+4,4);break;
            }
        } catch(const std::exception&) {}
        if(!model){++noGraphic;++omissions[thing.definitionType()+" [no direct graphic]"];continue;}
        auto me=meshes.find(model);if(me==meshes.end()){++missingMesh;++omissions[thing.definitionType()+" [mesh id missing]"];continue;}
        try {
            const auto geometry=forge::meshpreview::decodeLod0(
                graphics.entryData(*me->second),me->second->type);
            ++resolved;++resolvedTypes[thing.type];helperCount+=geometry.helpers.size();
            if(geometry.boneCount){++skinnedInstances;++skinnedDefs[thing.definitionType()];}
            for(const auto& m:geometry.materials)if(m.diffuseTexture){
                diffuseTextures.insert(uint32_t(m.diffuseTexture));
                if(!textureIds.count(uint32_t(m.diffuseTexture)))missingDiffuseTextures.insert(uint32_t(m.diffuseTexture));
            }
            for(const auto& h:geometry.helpers) {
                if(h.name.empty()) ++helperKinds["<unresolved crc "+std::to_string(h.nameCrc)+">"];
                else ++helperKinds[h.name];
                if(h.name.starts_with("HDMY_CREATE")) ++compoundHelpers;
            }
        } catch(const std::exception& e){++decodeFail;++omissions[thing.definitionType()+" [decode: "+e.what()+"]"];}
    }
    std::printf("scene audit: %s\n",tngPath.string().c_str());
    std::printf("  positioned=%zu resolved=%zu no-def=%zu no-graphic=%zu missing-mesh=%zu decode-fail=%zu\n",
        positioned,resolved,noDef,noGraphic,missingMesh,decodeFail);
    std::printf("  helpers=%zu compound-create-helpers=%zu distinct=%zu\n",helperCount,compoundHelpers,helperKinds.size());
    std::printf("  skinned-instances=%zu skinned-definitions=%zu diffuse-textures=%zu missing-textures=%zu\n",
        skinnedInstances,skinnedDefs.size(),diffuseTextures.size(),missingDiffuseTextures.size());
    size_t decodedTextures=0;std::map<std::string,size_t> textureErrors;
    for(uint32_t id:diffuseTextures)if(auto it=textureEntries.find(id);it!=textureEntries.end()){
        const auto mip=forge::terraintex::decodeMip0(it->second->subHeader,textures.entryData(*it->second));
        if(mip.ok)++decodedTextures;else ++textureErrors[mip.error];
    }
    std::printf("  preview-decodable-textures=%zu/%zu texture-error-kinds=%zu\n",
        decodedTextures,diffuseTextures.size(),textureErrors.size());
    for(const auto& [error,count]:textureErrors)std::printf("    texture-error %4zu  %s\n",count,error.c_str());
    for(const auto& [name,count]:resolvedTypes)std::printf("    resolved-type %4zu  %s\n",count,name.c_str());
    for(const auto& [name,count]:skinnedDefs)std::printf("    skinned %4zu  %s\n",count,name.c_str());
    for(const auto& [name,count]:helperKinds)std::printf("    helper %4zu  %s\n",count,name.c_str());
    for(const auto& [name,count]:omissions)std::printf("    omitted %4zu  %s\n",count,name.c_str());
    return 0;
}

int meshAudit(const std::filesystem::path& graphicsPath, bool asJson) {
    const auto graphics=forge::big::File::open(graphicsPath);
    const auto* bank=graphics.findBank("MBANK_ALLMESHES");
    if(!bank) throw std::runtime_error("mesh audit: MBANK_ALLMESHES missing");
    size_t decoded=0,failed=0,skipped=0,vertices=0,triangles=0,empty=0;
    size_t helpers=0,unresolvedHelpers=0,compoundHelpers=0;
    size_t oddStartStripBlocks=0,nonzeroStaticBases=0;
    size_t materials=0,alphaEnabledMaterials=0,unknown40Materials=0;
    size_t glowMaterials=0,alphaMapMaterials=0;
    size_t unresolvedMaterialTriangles=0;
    std::map<std::pair<uint32_t,uint32_t>,size_t> layouts;
    std::map<std::string,size_t> errors;
    std::map<uint32_t,size_t> skippedTypes;
    json failures=json::array();
    for(const auto& entry:bank->entries) {
      // decodeLod0's compiled-render-model contract covers these BIG entry
      // types. The same bank also stores collision and animation payloads.
      if(entry.type!=1&&entry.type!=2&&entry.type!=4&&entry.type!=5) {
        ++skipped;++skippedTypes[entry.type];continue;
      }
      try {
        const auto geometry=forge::meshpreview::decodeLod0(graphics.entryData(entry),entry.type);
        ++decoded;vertices+=geometry.vertices.size();triangles+=geometry.triangles.size();
        if(geometry.empty())++empty;
        helpers+=geometry.helpers.size();
        for(const auto& material:geometry.materials) {
            ++materials;
            alphaEnabledMaterials+=material.alphaEnabled;
            unknown40Materials+=material.unknown40!=0;
            glowMaterials+=material.glowStrength!=0;
            alphaMapMaterials+=material.alphaMapTexture!=0;
        }
        for(const auto& triangle:geometry.triangles)
            unresolvedMaterialTriangles += triangle.material < 0 ||
                size_t(triangle.material) >= geometry.materials.size();
        for(const auto& helper:geometry.helpers){
            if(helper.name.empty())++unresolvedHelpers;
            if(helper.name.starts_with("HDMY_CREATE"))++compoundHelpers;
        }
        for(const auto& primitive:geometry.primitives) {
            ++layouts[{primitive.vertexStride,primitive.vertexFormat}];
            oddStartStripBlocks+=primitive.oddStartStripBlocks;
            nonzeroStaticBases+=primitive.staticMinimumIndex!=0;
        }
      } catch(const std::exception& e) {
        ++failed;++errors[e.what()];
        failures.push_back({{"id",entry.id},{"name",entry.name},{"type",entry.type},{"error",e.what()}});
      }
    }
    if(asJson) {
        json layoutRows=json::array(),errorRows=json::array();
        for(const auto& [key,count]:layouts)
            layoutRows.push_back({{"stride",key.first},{"format",key.second},{"primitives",count}});
        for(const auto& [error,count]:errors)errorRows.push_back({{"error",error},{"count",count}});
        std::printf("%s\n",json{{"source",graphicsPath.string()},{"entries",bank->entries.size()},
            {"decoded",decoded},{"failed",failed},{"skipped",skipped},{"skippedTypes",skippedTypes},
            {"empty",empty},{"vertices",vertices},
            {"triangles",triangles},{"helpers",helpers},{"unresolvedHelpers",unresolvedHelpers},
            {"compoundCreateHelpers",compoundHelpers},{"oddStartStripBlocks",oddStartStripBlocks},
            {"materials",materials},{"alphaEnabledMaterials",alphaEnabledMaterials},
            {"unknown40Materials",unknown40Materials},{"glowMaterials",glowMaterials},
            {"alphaMapMaterials",alphaMapMaterials},
            {"unresolvedMaterialTriangles",unresolvedMaterialTriangles},
            {"nonzeroStaticBases",nonzeroStaticBases},{"layouts",layoutRows},{"errors",errorRows},
            {"failures",failures}}.dump(2).c_str());
    } else {
        std::printf("mesh audit: %s\n",graphicsPath.string().c_str());
        std::printf("  entries=%zu decoded=%zu failed=%zu skipped-non-render=%zu empty=%zu vertices=%zu triangles=%zu\n",
            bank->entries.size(),decoded,failed,skipped,empty,vertices,triangles);
        std::printf("  helpers=%zu unresolved-helpers=%zu compound-create-helpers=%zu\n",
            helpers,unresolvedHelpers,compoundHelpers);
        std::printf("  materials=%zu alpha-enabled=%zu glow=%zu alpha-map=%zu unknown40-nonzero=%zu\n",
            materials,alphaEnabledMaterials,glowMaterials,alphaMapMaterials,unknown40Materials);
        std::printf("  unresolved-material-triangles=%zu\n",unresolvedMaterialTriangles);
        std::printf("  face-coverage odd-start-strip-blocks=%zu nonzero-static-bases=%zu\n",
            oddStartStripBlocks,nonzeroStaticBases);
        for(const auto& [type,count]:skippedTypes)
            std::printf("    skipped type=%u entries=%zu\n",type,count);
        for(const auto& [key,count]:layouts)
            std::printf("    layout stride=%2u format=%2u primitives=%zu\n",key.first,key.second,count);
        for(const auto& [error,count]:errors)
            std::printf("    error %5zu  %s\n",count,error.c_str());
        size_t shown=0;for(const auto& failure:failures) {
            if(shown++==20){std::printf("    ... %zu additional failures (use --json for all)\n",failed-20);break;}
            std::printf("    failed id=%u type=%u name=%s: %s\n",failure["id"].get<uint32_t>(),
                failure["type"].get<uint32_t>(),failure["name"].get<std::string>().c_str(),
                failure["error"].get<std::string>().c_str());
        }
    }
    return failed?2:0;
}

int sceneAuditAll(const std::filesystem::path& root,
                  const std::filesystem::path& schemaPath,
                  const std::filesystem::path& tngRoot) {
    auto uppercase=[](std::string s){for(char& c:s)c=char(std::toupper((unsigned char)c));return s;};
    const auto defs=forge::bin::File::open(root/"data"/"CompiledDefs"/"names.bin",
                                           root/"data"/"CompiledDefs"/"game.bin");
    const auto schema=forge::defschema::Schema::load(schemaPath);
    const auto graphics=forge::big::File::open(root/"data"/"graphics"/"graphics.big");
    const auto* bank=graphics.findBank("MBANK_ALLMESHES");
    if(!bank)throw std::runtime_error("scene audit-all: MBANK_ALLMESHES missing");
    std::map<uint32_t,const forge::big::Entry*> meshes;
    std::map<std::string,uint32_t> meshNames;
    for(const auto& e:bank->entries){meshes[e.id]=&e;meshNames[uppercase(e.name)]=e.id;}
    const auto textures=forge::big::File::open(root/"data"/"graphics"/"pc"/"textures.big");
    std::map<uint32_t,const forge::big::Entry*> textureEntries;
    for(const auto& b:textures.banks())for(const auto& e:b.entries)textureEntries[e.id]=&e;

    std::vector<std::filesystem::path> paths;
    for(const auto& item:std::filesystem::recursive_directory_iterator(tngRoot))
        if(item.is_regular_file()&&uppercase(item.path().extension().string())==".TNG")paths.push_back(item.path());
    std::sort(paths.begin(),paths.end());
    size_t parsedFiles=0,parseFailures=0,thingsTotal=0,positioned=0,resolved=0;
    size_t noDef=0,noGraphic=0,missingMesh=0,decodeFail=0,helperCount=0,compoundHelpers=0;
    std::map<std::string,size_t> missingDefs,noGraphics,parseErrors,decodeErrors;
    std::map<uint32_t,forge::meshpreview::Geometry> decodedMeshes;
    std::map<uint32_t,std::string> failedMeshes;
    std::set<uint32_t> usedMeshes,diffuseTextures,missingTextures;
    for(const auto& path:paths) {
        forge::tng::File things;
        try{things=forge::tng::File::parse(path);++parsedFiles;}
        catch(const std::exception& e){++parseFailures;++parseErrors[e.what()];continue;}
        thingsTotal+=things.things().size();
        for(const auto& thing:things.things()) {
            if(!thing.findCtc("CTCPhysicsStandard")&&!thing.findCtc("CTCPhysicsNavigator"))continue;
            ++positioned;uint32_t model=0;
            if(const auto ov=thing.find("GraphicOverride")) {
                std::string name=*ov;if(name.size()>=2&&name.front()=='"'&&name.back()=='"')name=name.substr(1,name.size()-2);
                if(auto it=meshNames.find(uppercase(name));it!=meshNames.end())model=it->second;
            }
            const auto* def=defs.find(thing.definitionType());
            if(!def){++noDef;++missingDefs[thing.definitionType()];continue;}
            if(!model)try{
                const auto decoded=forge::defdecode::decode(*def,schema);
                for(const auto& field:decoded.fields)if(field.name=="Graphic"&&field.value.size()>=8){
                    std::memcpy(&model,field.value.data()+4,4);break;
                }
            }catch(const std::exception&){}
            if(!model){++noGraphic;++noGraphics[thing.definitionType()];continue;}
            const auto me=meshes.find(model);
            if(me==meshes.end()){++missingMesh;continue;}
            usedMeshes.insert(model);
            if(!decodedMeshes.count(model)&&!failedMeshes.count(model))try{
                decodedMeshes.emplace(model,forge::meshpreview::decodeLod0(
                    graphics.entryData(*me->second),me->second->type));
            }catch(const std::exception& e){failedMeshes[model]=e.what();}
            if(auto failure=failedMeshes.find(model);failure!=failedMeshes.end()){
                ++decodeFail;++decodeErrors[failure->second];continue;
            }
            ++resolved;const auto& geometry=decodedMeshes.at(model);
            helperCount+=geometry.helpers.size();
            for(const auto& h:geometry.helpers)if(h.name.starts_with("HDMY_CREATE"))++compoundHelpers;
            for(const auto& material:geometry.materials)if(material.diffuseTexture) {
                const uint32_t id=uint32_t(material.diffuseTexture);diffuseTextures.insert(id);
                if(!textureEntries.count(id))missingTextures.insert(id);
            }
        }
    }
    size_t decodableTextures=0;std::map<std::string,size_t> textureErrors;
    for(uint32_t id:diffuseTextures)if(auto it=textureEntries.find(id);it!=textureEntries.end()){
        const auto mip=forge::terraintex::decodeMip0(it->second->subHeader,textures.entryData(*it->second));
        if(mip.ok)++decodableTextures;else ++textureErrors[mip.error];
    }
    std::printf("scene audit-all: %s\n",tngRoot.string().c_str());
    std::printf("  files=%zu parsed=%zu parse-failures=%zu things=%zu positioned=%zu\n",
        paths.size(),parsedFiles,parseFailures,thingsTotal,positioned);
    std::printf("  resolved=%zu no-def=%zu no-graphic=%zu missing-mesh=%zu decode-fail=%zu\n",
        resolved,noDef,noGraphic,missingMesh,decodeFail);
    std::printf("  distinct-meshes=%zu decoded-meshes=%zu failed-meshes=%zu helpers=%zu compound-create-helpers=%zu\n",
        usedMeshes.size(),decodedMeshes.size(),failedMeshes.size(),helperCount,compoundHelpers);
    std::printf("  diffuse-textures=%zu missing-textures=%zu preview-decodable=%zu/%zu\n",
        diffuseTextures.size(),missingTextures.size(),decodableTextures,diffuseTextures.size());
    auto printTop=[](const char* label,const std::map<std::string,size_t>& rows){
        std::vector<std::pair<std::string,size_t>> sorted(rows.begin(),rows.end());
        std::sort(sorted.begin(),sorted.end(),[](const auto& a,const auto& b){return a.second>b.second;});
        size_t shown=0;for(const auto& [name,count]:sorted){if(shown++==20)break;
            std::printf("    %-14s %6zu  %s\n",label,count,name.c_str());}
    };
    printTop("missing-def",missingDefs);printTop("no-graphic",noGraphics);
    printTop("parse-error",parseErrors);printTop("decode-error",decodeErrors);
    printTop("texture-error",textureErrors);
    return (parseFailures||missingMesh||decodeFail||!missingTextures.empty())?2:0;
}

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);

    try {
        if (args.size() >= 3 && args[0] == "save" && args[1] == "read") {
            const bool asJson = !args.empty() && args.back() == "--json";
            return saveRead(args[2], asJson);
        }
        if (args.size() >= 4 && args[0] == "text" && args[1] == "show") {
            const bool asJson = !args.empty() && args.back() == "--json";
            return textShow(args[2], args[3], asJson);
        }
        if (args.size() >= 3 && args[0] == "text" && args[1] == "list") {
            const bool asJson = !args.empty() && args.back() == "--json";
            const std::string filter =
                args.size() > 3 && args[3] != "--json" ? args[3] : "";
            return textList(args[2], filter, asJson);
        }
        if (args.size() >= 5 && args[0] == "text" && args[1] == "set") {
            const std::string path = args[2];
            const std::string name = args[3];
            const std::string content = args[4];
            std::string outPath, donor, speaker, speechBank;
            std::optional<uint32_t> requestedId;
            bool inPlace = false, asJson = false;
            for (size_t i = 5; i < args.size(); ++i) {
                const std::string& a = args[i];
                auto next = [&]() -> std::string {
                    if (i + 1 >= args.size())
                        throw std::invalid_argument("text set: missing value after " + a);
                    return args[++i];
                };
                if (a == "--out") outPath = next();
                else if (a == "--in-place") inPlace = true;
                else if (a == "--id") requestedId = (uint32_t)std::stoul(next());
                else if (a == "--donor") donor = next();
                else if (a == "--speaker") speaker = next();
                else if (a == "--speech-bank") speechBank = next();
                else if (a == "--json") asJson = true;
                else throw std::invalid_argument("text set: unknown option " + a);
            }
            if (outPath.empty() == !inPlace)
                throw std::invalid_argument(
                    "text set: choose exactly one of --out <text.big> or --in-place");
            return textSet(path, name, content, outPath, inPlace, requestedId,
                           donor, speaker, speechBank, asJson);
        }
        if (args.size() >= 4 && args[0] == "text" && args[1] == "import") {
            const std::string path = args[2];
            const std::string manifest = args[3];
            std::string outPath, donor;
            bool inPlace = false, asJson = false;
            for (size_t i = 4; i < args.size(); ++i) {
                const std::string& a = args[i];
                auto next = [&]() -> std::string {
                    if (i + 1 >= args.size())
                        throw std::invalid_argument(
                            "text import: missing value after " + a);
                    return args[++i];
                };
                if (a == "--out") outPath = next();
                else if (a == "--in-place") inPlace = true;
                else if (a == "--donor") donor = next();
                else if (a == "--json") asJson = true;
                else throw std::invalid_argument("text import: unknown option " + a);
            }
            if (outPath.empty() == !inPlace)
                throw std::invalid_argument(
                    "text import: choose exactly one of --out <text.big> or --in-place");
            return textImport(path, manifest, outPath, inPlace, donor, asJson);
        }
        // `big` = the generic Lionhead BIGB container (retail asset banks); `fmp`
        // is the same container specialized to mod packages. Same reader.
        if (args.size() >= 3 && args[0] == "big" && args[1] == "list") {
            const bool asJson = !args.empty() && args.back() == "--json";
            const std::string filter =
                args.size() > 3 && args[3] != "--json" ? args[3] : "";
            return bigList(args[2], filter, asJson);
        }
        if (args.size() >= 4 && args[0] == "big" && args[1] == "extract") {
            return fmpExtract(args[2], args[3], args.size() > 4 ? args[4] : "");
        }
        if (args.size() >= 3 && args[0] == "fmp" && args[1] == "list") {
            const bool asJson = !args.empty() && args.back() == "--json";
            return fmpList(args[2], asJson);
        }
        if (args.size() >= 5 && args[0] == "fmp" && args[1] == "apply") {
            return fmpApply(args[2], args[3], args[4]);
        }
        if (args.size() >= 4 && args[0] == "fmp" && args[1] == "extract") {
            return fmpExtract(args[2], args[3], args.size() > 4 ? args[4] : "");
        }
        if (args.size() >= 5 && args[0] == "fmp" && args[1] == "export") {
            return fmpExport(args[2], args[3], args[4]);
        }
        if (args.size() >= 3 && args[0] == "patch" && args[1] == "info") {
            return patchInfo(args[2]);
        }
        if (args.size() >= 5 && args[0] == "patch" && args[1] == "apply") {
            return patchApply(args[2], args[3], args[4]);
        }
        if (args.size() >= 4 && args[0] == "patch" && args[1] == "_bunzip") {
            return bunzipFile(args[2], args[3]);
        }
        if (args.size() >= 4 && args[0] == "fmp" && args[1] == "_rewrite") {
            return fmpRewrite(args[2], args[3]);
        }
        if (args.size() >= 3 && args[0] == "wad" && args[1] == "list") {
            return wadList(args[2]);
        }
        if (args.size() >= 4 && args[0] == "wad" && args[1] == "extract") {
            return wadExtract(args[2], args[3], args.size() > 4 ? args[4] : "");
        }
        if (args.size() >= 4 && args[0] == "wad" && args[1] == "metadata") {
            return wadMetadata(args[2], args[3]);
        }
        if (args.size() >= 5 && args[0] == "wad" &&
            args[1] == "append-native-batch") {
            return wadAppendNativeBatch(args[2], args[3], args[4]);
        }
        if (args.size() >= 5 && args[0] == "wad" && args[1] == "repack") {
            return wadRepack(args[2], args[3], args[4]);
        }
        if (args.size() >= 4 && args[0] == "wad" && args[1] == "diff") {
            const bool asJson =
                std::find(args.begin(), args.end(), "--json") != args.end();
            const bool deep =
                std::find(args.begin(), args.end(), "--deep") != args.end();
            return wadDiff(args[2], args[3], asJson, deep);
        }
        if (args.size() >= 3 && args[0] == "stage") {
            return stageApply(args[1], args[2]);
        }
        if (args.size() >= 2 && args[0] == "unstage") {
            return stageRevert(args[1]);
        }
        if (args.size() >= 3 && args[0] == "stb" && args[1] == "list") {
            return stbList(args[2]);
        }
        if (args.size() == 4 && args[0] == "stb" && args[1] == "diff") {
            return stbDiff(args[2], args[3]);
        }
        if (args.size() >= 4 && args[0] == "stb" && args[1] == "extract") {
            return stbExtract(args[2], args[3], args.size() > 4 ? args[4] : "");
        }
        if (args.size() >= 5 && args[0] == "stb" && args[1] == "record") {
            return stbRecord(args[2], args[3], args[4]);
        }
        if (args.size() >= 8 && args[0] == "stb" && args[1] == "append") {
            return stbAppend(args[2], args[3], args[4], args[5], args[6], args[7]);
        }
        if (args.size() >= 5 && args[0] == "stb" && args[1] == "append-batch") {
            return stbAppendBatch(args[2], args[3], args[4]);
        }
        if (args.size() >= 5 && args[0] == "stb" &&
            args[1] == "replace-static-map-batch") {
            return stbAppendBatch(args[2], args[3], args[4], true);
        }
        if (args.size() >= 5 && args[0] == "stb" &&
            args[1] == "replace-static-map-batch-relayout") {
            return stbAppendBatch(args[2], args[3], args[4], 2);
        }
        if (args.size() >= 5 && args[0] == "stb" && args[1] == "terrainrecord") {
            bool includeLocalDetail = true, includeLandscape = true;
            for (size_t i = 5; i < args.size(); ++i) {
                if (args[i] == "--no-local-detail") includeLocalDetail = false;
                else if (args[i] == "--no-landscape") includeLandscape = false;
                else throw std::invalid_argument("stb terrainrecord: unknown option " + args[i]);
            }
            return stbTerrainRecord(args[2], args[3], args[4],
                                    includeLocalDetail, includeLandscape);
        }
        if (args.size() >= 6 && args[0] == "stb" && args[1] == "replace") {
            const auto payload = readAllBytes(args[5]);
            forge::stb::replaceEntryPayload(args[2], args[3], args[4], payload);
            std::printf("replaced %s (%zu bytes) -> %s\n", args[4].c_str(),
                        payload.size(), args[3].c_str());
            return 0;
        }
        if (args.size() == 8 && args[0] == "stb" && args[1] == "replace-static-map") {
            forge::stb::replaceStaticMap(args[2], args[3], args[4], args[5],
                                         readAllBytes(args[6]), readAllBytes(args[7]));
            std::printf("replaced static map %s and common record -> %s\n",
                        args[4].c_str(), args[3].c_str());
            return 0;
        }
        if (args.size() >= 3 && args[0] == "stb" && args[1] == "create-background") {
            return stbCreateBackground(std::vector<std::string>(args.begin() + 2, args.end()));
        }
        if (args.size() == 5 && args[0] == "stb" &&
            args[1] == "refit-common-height-bounds") {
            auto common = readAllBytes(args[2]);
            if (common.size() < forge::stbinfo::kInfoBlockSize)
                throw std::runtime_error("static-map common record is shorter than its InfoBlock");
            const auto lev = forge::lev::File::open(args[3]);
            float minHeight = std::numeric_limits<float>::max();
            float maxHeight = -std::numeric_limits<float>::max();
            for (int y = 0; y < lev.cellsY(); ++y)
                for (int x = 0; x < lev.cellsX(); ++x) {
                    const float height = lev.heightAt(x, y);
                    minHeight = std::min(minHeight, height);
                    maxHeight = std::max(maxHeight, height);
                }
            auto info = forge::stbinfo::readInfoBlock(common.data());
            if (info.mapWidth != lev.width() || info.mapHeight != lev.height())
                throw std::runtime_error("common-record and LEV dimensions disagree");
            forge::stbbake::setRetailCameraHeightBounds(info, minHeight, maxHeight);
            const auto encoded = forge::stbinfo::writeInfoBlock(info);
            std::copy(encoded.begin(), encoded.end(), common.begin());
            writeAllBytes(args[4], common);
            std::printf("refitted camera height bounds to %.9g..%.9g -> %s\n",
                        double(info.cameraMapBounds[2]),
                        double(info.cameraMapBounds[5]), args[4].c_str());
            return 0;
        }
        if (args.size() == 6 && args[0] == "stb" &&
            args[1] == "relocate-common") {
            auto common = readAllBytes(args[2]);
            if (common.size() < forge::stbinfo::kInfoBlockSize)
                throw std::runtime_error("static-map common record is shorter than its InfoBlock");
            auto info = forge::stbinfo::readInfoBlock(common.data());
            const int worldX = std::stoi(args[4]), worldY = std::stoi(args[5]);
            const float dx = float(worldX - info.worldX);
            const float dy = float(worldY - info.worldY);
            info.worldX = worldX; info.worldY = worldY;
            info.cameraMapBounds[0] += dx; info.cameraMapBounds[3] += dx;
            info.cameraMapBounds[1] += dy; info.cameraMapBounds[4] += dy;
            const auto encoded = forge::stbinfo::writeInfoBlock(info);
            std::copy(encoded.begin(), encoded.end(), common.begin());
            writeAllBytes(args[3], common);
            return 0;
        }
        if (args.size() == 4 && args[0] == "stb" && args[1] == "create-solid-inline") {
            return stbCreateSolidInline(std::vector<std::string>(args.begin() + 2, args.end()));
        }
        if (args.size() >= 3 && args[0] == "stb" && args[1] == "create-terrain") {
            return stbCreateTerrain(std::vector<std::string>(args.begin() + 2, args.end()));
        }
        if (args.size() >= 4 && args[0] == "stb" && args[1] == "backgroundtreeinfo") {
            return stbBackgroundTreeInfo(
                std::vector<std::string>(args.begin() + 2, args.end()));
        }
        if (args.size() >= 4 && args[0] == "stb" && args[1] == "patchverts") {
            return stbPatchVerts(std::vector<std::string>(args.begin() + 2, args.end()));
        }
        if (args.size() >= 3 && args[0] == "stb" && args[1] == "patchinfo") {
            const auto raw = readAllBytes(args[2]);
            const auto chunk = forge::stbbake::parseChunk(raw);
            std::printf("chunk: %zu bytes, %zu frames\n", raw.size(), chunk.frameIndices.size());
            size_t patches = 0;
            for (size_t fi = 0; fi < chunk.frameIndices.size(); ++fi) {
                std::vector<uint8_t> body;
                try { body = forge::stbbake::decodeFrame(chunk, fi); }
                catch (const std::exception&) { continue; }
                const auto h = forge::stbbake::parsePatchHeader(body);
                if (!h.valid) continue;
                const auto pb = forge::stbbake::parsePatchBody(body);
                if (!pb.valid || pb.waterOnly) continue;
                const auto verts = forge::stbbake::decodePatchVertices(pb);
                uint16_t minX = 0xffff, minY = 0xffff, maxX = 0, maxY = 0;
                uint16_t minAux0 = 0xffff, minAux1 = 0xffff, maxAux0 = 0, maxAux1 = 0;
                float minZ = 1.0e30f, maxZ = -1.0e30f;
                std::set<uint32_t> normals;
                std::set<uint16_t> aux0Values, aux1Values;
                for (const auto& v : verts) {
                    minX = std::min(minX, v.gridX); maxX = std::max(maxX, v.gridX);
                    minY = std::min(minY, v.gridY); maxY = std::max(maxY, v.gridY);
                    minZ = std::min(minZ, v.height); maxZ = std::max(maxZ, v.height);
                    minAux0 = std::min(minAux0, v.uv0); maxAux0 = std::max(maxAux0, v.uv0);
                    minAux1 = std::min(minAux1, v.uv1); maxAux1 = std::max(maxAux1, v.uv1);
                    normals.insert(v.packedNormal);
                    aux0Values.insert(v.uv0);
                    aux1Values.insert(v.uv1);
                }
                std::printf("frame %zu: patch=%ux%u coord=(%u,%u) verts=%u "
                            "grid=(%u,%u)..(%u,%u) z=%.3f..%.3f "
                            "normals=%zu normalMid=%08x aux0=%u..%u(%zu) "
                            "aux1=%u..%u(%zu) layout=17+%zu+%zu+%zu+%zu total=%zu",
                            fi, h.pw, h.ph, h.coord0, h.coord1, h.vertexCount,
                            minX, minY, maxX, maxY, minZ, maxZ, normals.size(),
                            unsigned(verts[(size_t(h.ph / 2) * (h.pw + 1)) +
                                           size_t(h.pw / 2)].packedNormal),
                            minAux0, maxAux0, aux0Values.size(),
                            minAux1, maxAux1, aux1Values.size(),
                            pb.texture.size(), pb.vbBlock.size(), pb.ibBlock.size(),
                            pb.trailer.size(), body.size());
                if (!pb.trailer.empty()) {
                    std::printf(" trailer=");
                    const size_t shown = std::min<size_t>(pb.trailer.size(), 32);
                    for (size_t i = 0; i < shown; ++i)
                        std::printf("%02x", unsigned(pb.trailer[i]));
                    if (shown < pb.trailer.size()) std::printf("...");
                }
                std::printf("\n");
                ++patches;
            }
            std::printf("mesh patches: %zu\n", patches);
            return patches ? 0 : 1;
        }
        if (args.size() == 5 && args[0] == "stb" && args[1] == "frame-extract") {
            const auto raw = readAllBytes(args[2]);
            const auto chunk = forge::stbbake::parseChunk(raw);
            const size_t frameIndex = static_cast<size_t>(std::stoul(args[3]));
            if (frameIndex >= chunk.frameIndices.size())
                throw std::out_of_range("frame-extract: frame index out of range");
            const auto body = forge::stbbake::decodeFrame(chunk, frameIndex);
            writeAllBytes(args[4], body);
            std::printf("extracted decoded frame %zu (%zu bytes) -> %s\n",
                        frameIndex, body.size(), args[4].c_str());
            return 0;
        }
        if (args.size() >= 3 && args[0] == "stb" && args[1] == "foregroundinfo") {
            std::optional<std::string> vertexDumpPath;
            std::optional<std::string> triangleDumpPath;
            std::optional<std::string> indexDumpPath;
            bool verifyTopology = false;
            bool verifyRoundtrip = false;
            bool verifyBaseCoverage = false;
            for (size_t i = 3; i < args.size(); ++i) {
                if (args[i] == "--vertices" && i + 1 < args.size()) {
                    vertexDumpPath = args[++i];
                } else if (args[i] == "--triangles" && i + 1 < args.size()) {
                    triangleDumpPath = args[++i];
                } else if (args[i] == "--indices" && i + 1 < args.size()) {
                    indexDumpPath = args[++i];
                } else if (args[i] == "--verify-topology") {
                    verifyTopology = true;
                } else if (args[i] == "--verify-roundtrip") {
                    verifyRoundtrip = true;
                } else if (args[i] == "--verify-base-coverage") {
                    verifyBaseCoverage = true;
                } else {
                    std::fprintf(stderr, "foregroundinfo: unknown option %s\n", args[i].c_str());
                    return 1;
                }
            }
            std::ofstream vertexDump;
            std::ofstream triangleDump;
            std::ofstream indexDump;
            if (vertexDumpPath) {
                vertexDump.open(*vertexDumpPath, std::ios::binary | std::ios::trunc);
                if (!vertexDump) {
                    std::fprintf(stderr, "foregroundinfo: cannot create %s\n",
                                 vertexDumpPath->c_str());
                    return 1;
                }
                vertexDump << std::setprecision(
                    std::numeric_limits<float>::max_digits10);
                vertexDump << "frame\tlayer\tmapping\tvertex\tx\ty\tz\tpacked_normal"
                              "\tblend\tcliff_u\tcliff_v\n";
            }
            if (triangleDumpPath) {
                triangleDump.open(*triangleDumpPath, std::ios::binary | std::ios::trunc);
                if (!triangleDump) {
                    std::fprintf(stderr, "foregroundinfo: cannot create %s\n",
                                 triangleDumpPath->c_str());
                    return 1;
                }
                triangleDump << "frame\tlayer\tmapping\ttexture0\ttexture1\ttexture2\tstrip_triangle"
                                "\ta\tb\tc\tax\tay\tbx\tby\tcx\tcy\n";
            }
            if (indexDumpPath) {
                indexDump.open(*indexDumpPath, std::ios::binary | std::ios::trunc);
                if (!indexDump) {
                    std::fprintf(stderr, "foregroundinfo: cannot create %s\n",
                                 indexDumpPath->c_str());
                    return 1;
                }
                indexDump << "frame\tlayer\tmapping\tindex_offset\tvertex\tx\ty\n";
            }
            const auto raw = readAllBytes(args[2]);
            const auto chunk = forge::stbbake::parseChunk(raw);
            size_t foregroundFrames = 0;
            size_t topologyVerified = 0;
            size_t topologyFailed = 0;
            size_t roundtripVerified = 0;
            size_t roundtripFailed = 0;
            size_t baseCoverageVerified = 0;
            size_t baseCoverageFailed = 0;
            for (size_t fi = 0; fi < chunk.frameIndices.size(); ++fi) {
                std::vector<uint8_t> body;
                try { body = forge::stbbake::decodeFrame(chunk, fi); }
                catch (const std::exception&) { continue; }
                size_t pos = 0;
                auto read16 = [&](uint16_t& value) -> bool {
                    if (pos + 2 > body.size()) return false;
                    value = uint16_t(body[pos]) | (uint16_t(body[pos + 1]) << 8);
                    pos += 2;
                    return true;
                };
                auto read32 = [&](uint32_t& value) -> bool {
                    if (pos + 4 > body.size()) return false;
                    value = uint32_t(body[pos]) | (uint32_t(body[pos + 1]) << 8) |
                            (uint32_t(body[pos + 2]) << 16) |
                            (uint32_t(body[pos + 3]) << 24);
                    pos += 4;
                    return true;
                };

                uint16_t layerCount = 0;
                if (!read16(layerCount) || layerCount == 0 || layerCount > 64) continue;
                struct LayerSummary {
                    struct Vertex {
                        uint16_t x = 0, y = 0;
                        float z = 0.0f;
                        uint32_t normal = 0;
                        uint8_t blend = 0, cliffU = 0, cliffV = 0;
                    };
                    uint16_t vertices = 0, polys = 0;
                    uint8_t mapping = 0;
                    uint32_t textures[3] = {};
                    bool shared = false;
                    float illumination = 0.0f;
                    uint16_t minX = 0xffff, minY = 0xffff, maxX = 0, maxY = 0;
                    float minZ = std::numeric_limits<float>::max();
                    float maxZ = -std::numeric_limits<float>::max();
                    uint8_t minCliffV = 0xff, maxCliffV = 0;
                    uint8_t minCliffU = 0xff, maxCliffU = 0;
                    uint8_t minBlend = 0xff, maxBlend = 0;
                    std::set<uint32_t> normals;
                    std::vector<Vertex> vertexData;
                    std::vector<uint16_t> indices;
                };
                std::vector<LayerSummary> layers;
                layers.reserve(layerCount);
                bool valid = true;
                for (uint16_t li = 0; li < layerCount && valid; ++li) {
                    LayerSummary layer;
                    uint32_t minMip = 0, minBumpMip = 0, illuminationBits = 0;
                    if (!read16(layer.vertices) || !read16(layer.polys) || pos >= body.size()) {
                        valid = false; break;
                    }
                    layer.mapping = body[pos++];
                    if (layer.vertices == 0 || layer.vertices > 4096 || layer.mapping > 4 ||
                        !read32(layer.textures[0]) || !read32(layer.textures[1]) ||
                        !read32(layer.textures[2]) || pos >= body.size()) {
                        valid = false; break;
                    }
                    layer.shared = body[pos++] != 0;
                    if (!read32(minMip) || !read32(minBumpMip) || !read32(illuminationBits)) {
                        valid = false; break;
                    }
                    std::memcpy(&layer.illumination, &illuminationBits, sizeof(layer.illumination));
                    const size_t vertexBytes = size_t(layer.vertices) * 15;
                    if (pos + vertexBytes > body.size()) { valid = false; break; }
                    for (uint16_t vi = 0; vi < layer.vertices; ++vi) {
                        const uint8_t* v = &body[pos + size_t(vi) * 15];
                        const uint16_t x = uint16_t(v[0]) | (uint16_t(v[1]) << 8);
                        const uint16_t y = uint16_t(v[2]) | (uint16_t(v[3]) << 8);
                        float z;
                        std::memcpy(&z, v + 4, sizeof(z));
                        const uint32_t normal = uint32_t(v[8]) | (uint32_t(v[9]) << 8) |
                                                (uint32_t(v[10]) << 16) | (uint32_t(v[11]) << 24);
                        // LoadForeground stores stream tail bytes in reverse runtime-field
                        // order: stream[12]->Blend(+0x16), [13]->CliffU(+0x15),
                        // [14]->CliffV(+0x14).
                        const uint8_t blend = v[12], cliffU = v[13], cliffV = v[14];
                        layer.vertexData.push_back({x, y, z, normal, blend, cliffU, cliffV});
                        layer.minX = std::min(layer.minX, x); layer.maxX = std::max(layer.maxX, x);
                        layer.minY = std::min(layer.minY, y); layer.maxY = std::max(layer.maxY, y);
                        layer.minZ = std::min(layer.minZ, z); layer.maxZ = std::max(layer.maxZ, z);
                        layer.minCliffV = std::min(layer.minCliffV, cliffV);
                        layer.maxCliffV = std::max(layer.maxCliffV, cliffV);
                        layer.minCliffU = std::min(layer.minCliffU, cliffU);
                        layer.maxCliffU = std::max(layer.maxCliffU, cliffU);
                        layer.minBlend = std::min(layer.minBlend, blend);
                        layer.maxBlend = std::max(layer.maxBlend, blend);
                        layer.normals.insert(normal);
                    }
                    pos += vertexBytes;
                    if (!layer.shared) {
                        const size_t indexBytes = (size_t(layer.polys) + 2) * 2;
                        if (pos + indexBytes > body.size()) { valid = false; break; }
                        layer.indices.reserve(size_t(layer.polys) + 2);
                        for (size_t ii = 0; ii < size_t(layer.polys) + 2; ++ii) {
                            const size_t at = pos + ii * 2;
                            layer.indices.push_back(uint16_t(body[at]) |
                                                    (uint16_t(body[at + 1]) << 8));
                        }
                        pos += indexBytes;
                    }
                    layers.push_back(std::move(layer));
                }
                if (!valid || layers.size() != layerCount || pos >= body.size()) continue;
                const uint8_t hasWater = body[pos++];
                if (hasWater > 1 || (hasWater == 0 && pos != body.size())) continue;
                if (verifyRoundtrip) {
                    try {
                        const auto parsed = forge::stbbake::parseForegroundFrame(body);
                        if (forge::stbbake::serializeForegroundFrame(parsed) == body)
                            ++roundtripVerified;
                        else
                            ++roundtripFailed;
                    } catch (const std::exception&) {
                        ++roundtripFailed;
                    }
                }
                if (verifyBaseCoverage) {
                    using Triangle = std::array<uint32_t, 3>;
                    std::set<Triangle> covered;
                    std::map<uint32_t, unsigned> baseBlend;
                    uint16_t patchX = 0xffff, patchY = 0xffff;
                    for (const auto& layer : layers) {
                        if (layer.mapping != 0) continue;
                        patchX = std::min<uint16_t>(patchX, uint16_t(layer.minX / 16 * 16));
                        patchY = std::min<uint16_t>(patchY, uint16_t(layer.minY / 16 * 16));
                        for (const auto& vertex : layer.vertexData) {
                            const uint32_t key = (uint32_t(vertex.x) << 16) | vertex.y;
                            baseBlend[key] += vertex.blend;
                        }
                        if (layer.shared) continue;
                        for (size_t ti = 0; ti + 2 < layer.indices.size(); ++ti) {
                            const uint16_t ids[3] = {layer.indices[ti], layer.indices[ti + 1],
                                                     layer.indices[ti + 2]};
                            if (ids[0] == ids[1] || ids[1] == ids[2] || ids[0] == ids[2] ||
                                ids[0] >= layer.vertexData.size() ||
                                ids[1] >= layer.vertexData.size() ||
                                ids[2] >= layer.vertexData.size()) continue;
                            Triangle triangle{};
                            for (int corner = 0; corner < 3; ++corner) {
                                const auto& vertex = layer.vertexData[ids[corner]];
                                triangle[corner] = (uint32_t(vertex.x) << 16) | vertex.y;
                            }
                            std::sort(triangle.begin(), triangle.end());
                            covered.insert(triangle);
                        }
                    }
                    static constexpr int offsets[2][2][3][2] = {
                        {{{0,0},{1,0},{1,1}},{{0,0},{0,1},{1,1}}},
                        {{{0,0},{1,0},{0,1}},{{1,0},{0,1},{1,1}}},
                    };
                    bool full = patchX != 0xffff && patchY != 0xffff;
                    for (int x = 0; full && x < 16; ++x)
                        for (int y = 0; full && y < 16; ++y)
                            for (int triangleIndex = 0; triangleIndex < 2; ++triangleIndex) {
                                Triangle triangle{};
                                for (int corner = 0; corner < 3; ++corner) {
                                    const uint32_t vx = uint32_t(patchX + x +
                                        offsets[(x ^ y) & 1][triangleIndex][corner][0]);
                                    const uint32_t vy = uint32_t(patchY + y +
                                        offsets[(x ^ y) & 1][triangleIndex][corner][1]);
                                    triangle[corner] = (vx << 16) | vy;
                                }
                                std::sort(triangle.begin(), triangle.end());
                                bool sharedCovers = false;
                                for (const auto& layer : layers)
                                    sharedCovers |= layer.mapping == 0 && layer.shared;
                                full &= sharedCovers || covered.count(triangle) != 0;
                            }
                    for (int x = 0; full && x <= 16; ++x)
                        for (int y = 0; full && y <= 16; ++y)
                            full &= baseBlend[(uint32_t(patchX + x) << 16) |
                                              uint32_t(patchY + y)] > 0;
                    baseCoverageVerified += full;
                    baseCoverageFailed += !full;
                    if (!full)
                        std::printf("    base coverage MISSING triangle or nonzero vertex blend\n");
                }

                const auto& segment = chunk.segments[chunk.frameIndices[fi]];
                std::printf("frame %zu @0x%zx span=%zu: foreground layers=%u "
                            "water=%u consumed=%zu/%zu\n",
                            fi, segment.start, segment.end - segment.start,
                            unsigned(layerCount), unsigned(hasWater), pos, body.size());
                for (size_t li = 0; li < layers.size(); ++li) {
                    const auto& layer = layers[li];
                    std::printf("  layer %zu: tex=(%u,%u,%u) illum=%g mapping=%u "
                                "verts=%u polys=%u shared=%u xy=(%u,%u)..(%u,%u) "
                                "z=%.3f..%.3f normals=%zu cliffV=%u..%u "
                                "cliffU=%u..%u blend=%u..%u\n",
                                li, layer.textures[0], layer.textures[1], layer.textures[2],
                                layer.illumination, unsigned(layer.mapping), layer.vertices,
                                layer.polys, layer.shared ? 1u : 0u,
                                layer.minX, layer.minY, layer.maxX, layer.maxY,
                                layer.minZ, layer.maxZ, layer.normals.size(),
                                unsigned(layer.minCliffV), unsigned(layer.maxCliffV),
                                unsigned(layer.minCliffU), unsigned(layer.maxCliffU),
                                unsigned(layer.minBlend), unsigned(layer.maxBlend));
                    if (vertexDump) {
                        for (size_t vi = 0; vi < layer.vertexData.size(); ++vi) {
                            const auto& v = layer.vertexData[vi];
                            vertexDump << fi << '\t' << li << '\t'
                                       << unsigned(layer.mapping) << '\t' << vi << '\t'
                                       << v.x << '\t' << v.y << '\t' << v.z << '\t'
                                       << "0x" << std::hex << v.normal << std::dec << '\t'
                                       << unsigned(v.blend) << '\t' << unsigned(v.cliffU) << '\t'
                                       << unsigned(v.cliffV) << '\n';
                        }
                    }
                    if (triangleDump) {
                        for (size_t ti = 0; ti + 2 < layer.indices.size(); ++ti) {
                            const uint16_t a = layer.indices[ti];
                            const uint16_t b = layer.indices[ti + 1];
                            const uint16_t c = layer.indices[ti + 2];
                            if (a == b || b == c || a == c ||
                                a >= layer.vertexData.size() ||
                                b >= layer.vertexData.size() ||
                                c >= layer.vertexData.size())
                                continue;
                            const auto& va = layer.vertexData[a];
                            const auto& vb = layer.vertexData[b];
                            const auto& vc = layer.vertexData[c];
                            triangleDump << fi << '\t' << li << '\t'
                                         << unsigned(layer.mapping) << '\t'
                                         << layer.textures[0] << '\t' << layer.textures[1] << '\t'
                                         << layer.textures[2] << '\t' << ti << '\t'
                                         << a << '\t' << b << '\t' << c << '\t'
                                         << va.x << '\t' << va.y << '\t'
                                         << vb.x << '\t' << vb.y << '\t'
                                         << vc.x << '\t' << vc.y << '\n';
                        }
                    }
                    if (indexDump) {
                        for (size_t ii = 0; ii < layer.indices.size(); ++ii) {
                            const uint16_t vertex = layer.indices[ii];
                            indexDump << fi << '\t' << li << '\t'
                                      << unsigned(layer.mapping) << '\t' << ii << '\t'
                                      << vertex << '\t';
                            if (vertex < layer.vertexData.size()) {
                                const auto& v = layer.vertexData[vertex];
                                indexDump << v.x << '\t' << v.y;
                            } else {
                                indexDump << "INVALID\tINVALID";
                            }
                            indexDump << '\n';
                        }
                    }
                    if (verifyTopology && !layer.shared) {
                        using Triangle = std::array<uint32_t, 3>;
                        std::set<Triangle> triangles;
                        for (size_t ti = 0; ti + 2 < layer.indices.size(); ++ti) {
                            const uint16_t ids[3] = {layer.indices[ti], layer.indices[ti + 1],
                                                     layer.indices[ti + 2]};
                            if (ids[0] == ids[1] || ids[1] == ids[2] || ids[0] == ids[2] ||
                                ids[0] >= layer.vertexData.size() ||
                                ids[1] >= layer.vertexData.size() ||
                                ids[2] >= layer.vertexData.size()) continue;
                            Triangle triangle{};
                            for (int corner = 0; corner < 3; ++corner) {
                                const auto& vertex = layer.vertexData[ids[corner]];
                                triangle[corner] = (uint32_t(vertex.x) << 16) | vertex.y;
                            }
                            std::sort(triangle.begin(), triangle.end());
                            triangles.insert(triangle);
                        }
                        const int patchX = int(layer.minX) / 16 * 16;
                        const int patchY = int(layer.minY) / 16 * 16;
                        static constexpr int offsets[2][2][3][2] = {
                            {{{0, 0}, {1, 0}, {1, 1}}, {{0, 0}, {0, 1}, {1, 1}}},
                            {{{0, 0}, {1, 0}, {0, 1}}, {{1, 0}, {0, 1}, {1, 1}}},
                        };
                        std::array<bool, 512> mask{};
                        for (int x = 0; x < 16; ++x) {
                            for (int y = 0; y < 16; ++y) {
                                for (int triangleIndex = 0; triangleIndex < 2;
                                     ++triangleIndex) {
                                    Triangle triangle{};
                                    for (int corner = 0; corner < 3; ++corner) {
                                        const uint32_t vx = uint32_t(patchX + x +
                                            offsets[(x ^ y) & 1][triangleIndex][corner][0]);
                                        const uint32_t vy = uint32_t(patchY + y +
                                            offsets[(x ^ y) & 1][triangleIndex][corner][1]);
                                        triangle[corner] = (vx << 16) | vy;
                                    }
                                    std::sort(triangle.begin(), triangle.end());
                                    mask[(x * 16 + y) * 2 + triangleIndex] =
                                        triangles.count(triangle) != 0;
                                }
                            }
                        }
                        const auto rebuilt = forge::terrain::buildLayerTopology(mask);
                        bool exact = rebuilt.indices == layer.indices &&
                                     rebuilt.vertices.size() == layer.vertexData.size();
                        for (size_t vi = 0; exact && vi < rebuilt.vertices.size(); ++vi) {
                            exact = int(rebuilt.vertices[vi].x) + patchX ==
                                        layer.vertexData[vi].x &&
                                    int(rebuilt.vertices[vi].y) + patchY ==
                                        layer.vertexData[vi].y;
                        }
                        topologyVerified += exact;
                        topologyFailed += !exact;
                        if (!exact) {
                            std::printf("    topology MISMATCH rebuilt verts=%zu indices=%zu\n",
                                        rebuilt.vertices.size(), rebuilt.indices.size());
                        }
                    }
                }
                ++foregroundFrames;
            }
            std::printf("foreground frames: %zu\n", foregroundFrames);
            if (verifyTopology) {
                std::printf("topology exact: %zu/%zu\n", topologyVerified,
                            topologyVerified + topologyFailed);
            }
            if (verifyRoundtrip) {
                std::printf("foreground roundtrip exact: %zu/%zu\n", roundtripVerified,
                            roundtripVerified + roundtripFailed);
            }
            if (verifyBaseCoverage) {
                std::printf("base coverage complete: %zu/%zu\n", baseCoverageVerified,
                            baseCoverageVerified + baseCoverageFailed);
            }
            return foregroundFrames && topologyFailed == 0 && roundtripFailed == 0 &&
                   baseCoverageFailed == 0 ? 0 : 1;
        }
        // forge texture import <src.big> <out.big> <entry> <image.png> [--add] ...
        if (args.size() >= 6 && args[0] == "texture" && args[1] == "import") {
            bool add = false;
            std::string bank, format, dims, tools, python;
            for (size_t i = 6; i < args.size(); ++i) {
                const std::string& a = args[i];
                auto next = [&]() -> std::string {
                    return i + 1 < args.size() ? args[++i] : std::string();
                };
                if (a == "--add") add = true;
                else if (a == "--bank") bank = next();
                else if (a == "--format") format = next();
                else if (a == "--dims") dims = next();
                else if (a == "--tools") tools = next();
                else if (a == "--python") python = next();
                else {
                    std::fprintf(stderr, "texture import: unknown option %s\n", a.c_str());
                    return 2;
                }
            }
            return textureImport(args[2], args[3], args[4], args[5], add, bank,
                                 format, dims, tools, python);
        }
        if (args.size() >= 4 && args[0] == "texture" && args[1] == "verify") {
            return textureVerify(args[2], args[3]);
        }
        if (args.size() >= 3 && args[0] == "texture" && args[1] == "verify-bank") {
            std::string bank, filter;
            size_t limit = 0;
            for (size_t i = 3; i < args.size(); ++i) {
                const std::string& a = args[i];
                auto next = [&]() -> std::string {
                    return i + 1 < args.size() ? args[++i] : std::string();
                };
                if (a == "--bank") bank = next();
                else if (a == "--filter") filter = next();
                else if (a == "--limit") limit = size_t(std::stoul(next()));
                else {
                    std::fprintf(stderr, "texture verify-bank: unknown option %s\n",
                                 a.c_str());
                    return 2;
                }
            }
            return textureVerifyBank(args[2], bank, filter, limit);
        }
        if (args.size() >= 4 && args[0] == "texture" && args[1] == "free-slots") {
            std::string schema, prefix = "UNASSIGNED_";
            bool asJson = false;
            for (size_t i = 4; i < args.size(); ++i) {
                const std::string& a = args[i];
                auto next = [&]() -> std::string {
                    return i + 1 < args.size() ? args[++i] : std::string();
                };
                if (a == "--schema") schema = next();
                else if (a == "--prefix") prefix = next();
                else if (a == "--all") prefix.clear();
                else if (a == "--json") asJson = true;
                else {
                    std::fprintf(stderr, "texture free-slots: unknown option %s\n",
                                 a.c_str());
                    return 2;
                }
            }
            if (schema.empty()) schema = defaultDefSchemaPath();
            if (schema.empty()) {
                std::fprintf(stderr, "texture free-slots: --schema <def_schema.json> "
                                     "required (no default found)\n");
                return 2;
            }
            return textureFreeSlots(args[2], args[3], schema, prefix, asJson);
        }
        // forge terrain themes <level.lev> --root <game-root> [...]
        if (args.size() >= 3 && args[0] == "terrain" && args[1] == "themes") {
            std::string root, schema, textures;
            bool asJson = false;
            for (size_t i = 3; i < args.size(); ++i) {
                const std::string& a = args[i];
                auto next = [&]() -> std::string {
                    return i + 1 < args.size() ? args[++i] : std::string();
                };
                if (a == "--root") root = next();
                else if (a == "--schema") schema = next();
                else if (a == "--textures") textures = next();
                else if (a == "--json") asJson = true;
                else {
                    std::fprintf(stderr, "terrain themes: unknown option %s\n", a.c_str());
                    return 2;
                }
            }
            if (root.empty()) {
                std::fprintf(stderr, "terrain themes: --root <game-root> is required\n");
                return 2;
            }
            if (schema.empty()) schema = defaultDefSchemaPath();
            if (schema.empty()) {
                std::fprintf(stderr, "terrain themes: --schema <def_schema.json> "
                                     "required (no default found)\n");
                return 2;
            }
            return terrainThemes(args[2], root, schema, textures, asJson);
        }
        if (args.size() >= 5 && args[0] == "stb" && args[1] == "settex") {
            // forge stb settex <chunk.bin> <out.bin> --map OLD:NEW [--map ...]
            // Reassign foreground-layer terrain textures across a chunk: rewrite the
            // per-layer texture triple (foreground/background/bumpmap global indices)
            // wherever OLD appears -> NEW. Same decoded body size (u32->u32), so
            // emitChunk with preservePhysicalLayout keeps every GATED landscape/
            // local-detail pointer stable (no rebase). This is the "assign a texture
            // to a layer" paint primitive; NEW must be a texture the global bank has.
            const std::string inPath = args[2];
            const std::string outPath = args[3];
            std::map<uint32_t, uint32_t> remap;
            for (size_t i = 4; i < args.size(); ++i) {
                if (args[i] == "--map" && i + 1 < args.size()) {
                    const std::string s = args[++i];
                    const size_t c = s.find(':');
                    if (c == std::string::npos) {
                        std::fprintf(stderr, "settex: --map needs OLD:NEW\n");
                        return 1;
                    }
                    remap[uint32_t(std::strtoul(s.substr(0, c).c_str(), nullptr, 0))] =
                        uint32_t(std::strtoul(s.substr(c + 1).c_str(), nullptr, 0));
                } else {
                    std::fprintf(stderr, "settex: unknown option %s\n", args[i].c_str());
                    return 1;
                }
            }
            if (remap.empty()) {
                std::fprintf(stderr, "settex: at least one --map OLD:NEW required\n");
                return 1;
            }
            const auto raw = readAllBytes(inPath);
            const auto chunk = forge::stbbake::parseChunk(raw);
            forge::stbbake::EmitOptions opt;
            opt.codec = forge::stbbake::FrameCodec::RawPassthrough;
            // Relay mode: recompressed frames can exceed their donor slot, so let
            // emitChunk shift downstream segments + rewire the quad dir on a page
            // boundary. GATED landscape/local-detail pointers are logical control-
            // stream positions (not physical frame offsets), so no InfoBlock rebase
            // is needed (stbbake.hpp emitChunk contract).
            opt.frameAlign = 0; // tight relay (retail packs frames tightly, not page-aligned)
            opt.highCompressionEdits = true;

            size_t framesEdited = 0, refsChanged = 0;
            for (size_t fi = 0; fi < chunk.frameIndices.size(); ++fi) {
                std::vector<uint8_t> body;
                try { body = forge::stbbake::decodeFrame(chunk, fi); }
                catch (const std::exception&) { continue; }
                size_t pos = 0;
                auto rd16 = [&](uint16_t& v) -> bool {
                    if (pos + 2 > body.size()) return false;
                    v = uint16_t(body[pos]) | (uint16_t(body[pos + 1]) << 8);
                    pos += 2; return true;
                };
                auto rd32at = [&](size_t p, uint32_t& v) -> bool {
                    if (p + 4 > body.size()) return false;
                    v = uint32_t(body[p]) | (uint32_t(body[p + 1]) << 8) |
                        (uint32_t(body[p + 2]) << 16) | (uint32_t(body[p + 3]) << 24);
                    return true;
                };
                auto wr32at = [&](size_t p, uint32_t v) {
                    body[p] = uint8_t(v); body[p + 1] = uint8_t(v >> 8);
                    body[p + 2] = uint8_t(v >> 16); body[p + 3] = uint8_t(v >> 24);
                };
                uint16_t layerCount = 0;
                if (!rd16(layerCount) || layerCount == 0 || layerCount > 64) continue;
                bool valid = true, frameChanged = false;
                size_t frameRefs = 0;
                for (uint16_t li = 0; li < layerCount && valid; ++li) {
                    uint16_t vertices = 0, polys = 0;
                    if (!rd16(vertices) || !rd16(polys) || pos >= body.size()) { valid = false; break; }
                    const uint8_t mapping = body[pos++];
                    if (vertices == 0 || vertices > 4096 || mapping > 4 || pos + 12 > body.size()) {
                        valid = false; break;
                    }
                    // Texture triple at [pos, pos+4, pos+8]: remap in place.
                    for (int t = 0; t < 3; ++t) {
                        uint32_t tex = 0;
                        if (!rd32at(pos + size_t(t) * 4, tex)) { valid = false; break; }
                        auto it = remap.find(tex);
                        if (it != remap.end() && it->second != tex) {
                            wr32at(pos + size_t(t) * 4, it->second);
                            ++frameRefs; frameChanged = true;
                        }
                    }
                    if (!valid) break;
                    pos += 12;                 // triple
                    if (pos >= body.size()) { valid = false; break; }
                    const bool shared = body[pos++] != 0;
                    pos += 12;                 // minMip + minBumpMip + illum
                    const size_t vertexBytes = size_t(vertices) * 15;
                    if (pos + vertexBytes > body.size()) { valid = false; break; }
                    pos += vertexBytes;
                    if (!shared) {
                        const size_t indexBytes = (size_t(polys) + 2) * 2;
                        if (pos + indexBytes > body.size()) { valid = false; break; }
                        pos += indexBytes;
                    }
                }
                if (!valid || pos >= body.size()) continue; // not a foreground frame
                const uint8_t hasWater = body[pos++];
                if (hasWater > 1 || (hasWater == 0 && pos != body.size())) continue;
                if (frameChanged) {
                    forge::stbbake::FrameEdit edit;
                    edit.frameIndex = fi;
                    edit.newBody = std::move(body);   // same length as decoded
                    opt.edits.push_back(std::move(edit));
                    ++framesEdited; refsChanged += frameRefs;
                }
            }
            if (framesEdited == 0) {
                std::fprintf(stderr, "settex: no foreground layers referenced any "
                             "of the mapped texture ids; nothing to do\n");
                return 1;
            }
            const auto emitted = forge::stbbake::emitChunk(chunk, opt);
            if (!emitted.ok) {
                for (const auto& n : emitted.notes) std::fprintf(stderr, "%s\n", n.c_str());
                std::fprintf(stderr, "settex: emitChunk failed (edited frame may not "
                             "fit its donor page)\n");
                return 1;
            }
            writeAllBytes(outPath, emitted.chunk);
            std::printf("settex: remapped %zu texture refs across %zu foreground "
                        "frames; %zu frames re-encoded, %zu quad entries rewired -> "
                        "%s (%zu bytes)\n",
                        refsChanged, framesEdited, emitted.framesReencoded,
                        emitted.quadEntriesRewired, outPath.c_str(),
                        emitted.chunk.size());
            return 0;
        }
        if (args.size() >= 7 && args[0] == "stb" && args[1] == "bake-heightfield") {
            const auto raw = readAllBytes(args[2]);
            const auto lev = forge::lev::File::open(args[3]);
            const int worldX = std::stoi(args[4]);
            const int worldY = std::stoi(args[5]);
            forge::stbbake::HeightfieldBakeOptions options;
            std::vector<std::unique_ptr<forge::lev::File>> ownedNeighbors;
            options.themes.resize(256);
            std::string themesFromDefsRoot, themeSchemaPath;
            for (size_t i = 7; i < args.size();) {
                if (args[i] == "--neighbor" && i + 3 < args.size()) {
                    ownedNeighbors.push_back(std::make_unique<forge::lev::File>(forge::lev::File::open(args[i + 1])));
                    options.neighbors.push_back({ownedNeighbors.back().get(), std::stoi(args[i + 2]), std::stoi(args[i + 3])});
                    i += 4;
                } else if (args[i] == "--world" && i + 1 < args.size()) {
                    options.worldPath = args[i + 1];
                    i += 2;
                } else if (args[i] == "--levels-root" && i + 1 < args.size()) {
                    options.levelsRoot = args[i + 1];
                    i += 2;
                } else if (args[i] == "--level" && i + 1 < args.size()) {
                    options.levelName = args[i + 1];
                    i += 2;
                } else if (args[i] == "--rebuild-direction-mask") {
                    options.rebuildDirectionMask = true;
                    ++i;
                } else if (args[i] == "--rebuild-topology") {
                    options.rebuildTopology = true;
                    ++i;
                } else if (args[i] == "--any-size") {
                    options.requireCanonicalSize = false;
                    ++i;
                } else if (args[i] == "--themes-from-defs" && i + 1 < args.size()) {
                    themesFromDefsRoot = args[i + 1];
                    i += 2;
                } else if (args[i] == "--theme-schema" && i + 1 < args.size()) {
                    themeSchemaPath = args[i + 1];
                    i += 2;
                } else if (args[i] == "--theme-material" && i + 7 < args.size()) {
                    const size_t slot = size_t(std::stoul(args[i + 1]));
                    if (slot >= options.themes.size())
                        throw std::invalid_argument("bake-heightfield: theme slot exceeds 255");
                    auto& theme = options.themes[slot];
                    theme.available = true;
                    for (int field = 0; field < 3; ++field) {
                        theme.base.textures[field] = uint32_t(std::stoul(args[i + 2 + field]));
                        theme.cliff.textures[field] = uint32_t(std::stoul(args[i + 5 + field]));
                    }
                    i += 8;
                } else {
                    throw std::invalid_argument(
                        "bake-heightfield: invalid or incomplete neighbor option");
                }
            }
            // Painless path: derive every theme material straight from the LEV
            // palette (slot -> ENGINE_THEME def index -> the six texture fields)
            // instead of making the user type six ids per slot. Explicit
            // --theme-material entries still win.
            if (!themesFromDefsRoot.empty()) {
                if (themeSchemaPath.empty()) themeSchemaPath = defaultDefSchemaPath();
                if (themeSchemaPath.empty())
                    throw std::invalid_argument(
                        "bake-heightfield: --themes-from-defs needs --theme-schema "
                        "<def_schema.json> (no default found)");
                const auto library = forge::terraintex::ThemeLibrary::loadFromRoot(
                    themesFromDefsRoot, themeSchemaPath);
                const auto resolved = forge::terraintex::paletteMaterials(lev, library);
                size_t filled = 0;
                for (size_t slot = 0; slot < options.themes.size(); ++slot) {
                    if (options.themes[slot].available || !resolved[slot].available)
                        continue;
                    options.themes[slot] = resolved[slot];
                    ++filled;
                }
                std::printf("resolved %zu theme material(s) from %s ground-theme "
                            "palette via ENGINE_THEME defs\n", filled,
                            args[3].c_str());
            }
            const auto baked = forge::stbbake::bakeHeightfield(raw, lev, worldX, worldY, options);
            for (const auto& note : baked.notes) std::printf("%s\n", note.c_str());
            writeAllBytes(args[6], baked.chunk);
            return 0;
        }
        if (args.size() >= 2 && args[0] == "stbvalidate") {
            return stbValidate(std::vector<std::string>(args.begin() + 1, args.end()));
        }
        if (args.size() >= 2 && args[0] == "stbretarget") {
            return stbRetarget(std::vector<std::string>(args.begin() + 1, args.end()));
        }
        if (args.size() >= 3 && args[0] == "tng" && args[1] == "list") {
            return tngList(args[2]);
        }
        if (args.size() == 4 && args[0] == "tng" &&
            args[1] == "transition-audit") {
            return tngTransitionAudit(args[2], args[3]);
        }
        if (args.size() >= 4 && args[0] == "tng" && args[1] == "conflicts") {
            const bool asJson = !args.empty() && args.back() == "--json";
            std::vector<std::string> modPaths;
            for (size_t i = 3; i < args.size(); ++i) {
                if (args[i] != "--json") modPaths.push_back(args[i]);
            }
            return tngConflicts(args[2], modPaths, asJson);
        }
        if (args.size() >= 5 && args[0] == "tng" && args[1] == "merge") {
            const bool asJson = !args.empty() && args.back() == "--json";
            std::vector<std::string> modPaths;
            for (size_t i = 4; i < args.size(); ++i) {
                if (args[i] != "--json") modPaths.push_back(args[i]);
            }
            return tngMerge(args[2], modPaths, args[3], asJson);
        }
        if (args.size() >= 3 && args[0] == "tng" && args[1] == "roundtrip") {
            return tngRoundtrip(args[2]);
        }
        if (args.size() >= 3 && args[0] == "tng" && args[1] == "place") {
            return tngPlace(std::vector<std::string>(args.begin() + 2, args.end()));
        }
        if (args.size() >= 3 && args[0] == "qst" && args[1] == "list") {
            return qstList(args[2], !args.empty() && args.back() == "--json");
        }
        if (args.size() >= 3 && args[0] == "qst" && args[1] == "roundtrip") {
            return qstRoundtrip(args[2]);
        }
        if (args.size() >= 5 && args[0] == "qst" && args[1] == "merge") {
            const bool asJson = !args.empty() && args.back() == "--json";
            std::string picksPath;
            std::vector<std::string> modPaths;
            for (size_t i = 4; i < args.size(); ++i) {
                if (args[i] == "--json") continue;
                if (args[i] == "--picks" && i + 1 < args.size()) {
                    picksPath = args[++i];
                    continue;
                }
                modPaths.push_back(args[i]);
            }
            return qstMerge(args[2], modPaths, args[3], picksPath, asJson);
        }
        if (args.size() >= 3 && args[0] == "lev" && args[1] == "info") {
            return levInfo(args[2]);
        }
        if (args.size() >= 3 && args[0] == "lev" &&
            (args[1] == "themecheck" || args[1] == "themerebase")) {
            return levThemePalette({args.begin() + 2, args.end()},
                                   args[1] == "themerebase");
        }
        if (args.size() >= 3 && args[0] == "lev" && args[1] == "themegrid") {
            return levThemeGrid(args[2], args.size() > 3 ? args[3] : "");
        }
        if (args.size() >= 3 && args[0] == "lev" && args[1] == "sculpt") {
            return levSculpt(std::vector<std::string>(args.begin() + 2, args.end()));
        }
        if (args.size() >= 3 && args[0] == "lev" && args[1] == "import-heightmap") {
            return levImportHeightmap(std::vector<std::string>(args.begin() + 2, args.end()));
        }
        if (args.size() >= 3 && args[0] == "lev" && args[1] == "import-world-heightmap") {
            return levImportWorldHeightmap(std::vector<std::string>(args.begin() + 2, args.end()));
        }
        if (args.size() >= 3 && args[0] == "terrain" &&
            args[1] == "split-world-heightmap") {
            return terrainSplitWorldHeightmap(
                std::vector<std::string>(args.begin() + 2, args.end()));
        }
        if (args.size() == 5 && args[0] == "terrain" &&
            args[1] == "verify-tiled-levels") {
            return terrainVerifyTiledLevels(
                std::vector<std::string>(args.begin() + 2, args.end()));
        }
        if (args.size() >= 3 && args[0] == "lev" && args[1] == "author-surface") {
            return levAuthorSurface(std::vector<std::string>(args.begin() + 2, args.end()));
        }
        if (args.size() >= 3 && args[0] == "lev" &&
            args[1] == "import-f2-preview-materials") {
            return levImportF2PreviewMaterials(
                std::vector<std::string>(args.begin() + 2, args.end()));
        }
        if (args.size() >= 3 && args[0] == "lev" && args[1] == "paint-theme") {
            return levPaintTheme(std::vector<std::string>(args.begin() + 2, args.end()));
        }
        if (args.size() >= 3 && args[0] == "lev" && args[1] == "paint-walkable") {
            return levPaintBoolean(std::vector<std::string>(args.begin() + 2, args.end()), false);
        }
        if (args.size() >= 3 && args[0] == "lev" && args[1] == "paint-preferred") {
            return levPaintBoolean(std::vector<std::string>(args.begin() + 2, args.end()), true);
        }
        if (args.size() >= 3 && args[0] == "lev" && args[1] == "rebuild-nav") {
            return levRebuildNav(std::vector<std::string>(args.begin() + 2, args.end()));
        }
        if (args.size() >= 3 && args[0] == "lev" && args[1] == "stitch") {
            return levStitch(std::vector<std::string>(args.begin() + 2, args.end()));
        }
        if (args.size() >= 3 && args[0] == "lev" && args[1] == "stitch-region") {
            return levStitchRegion(std::vector<std::string>(args.begin() + 2, args.end()));
        }
        if (args.size() >= 3 && args[0] == "wld" && args[1] == "info") {
            return wldInfo(args[2], args.size() > 3 ? args[3] : "");
        }
        if (args.size() == 7 && args[0] == "wld" &&
            args[1] == "translate-prefix") {
            auto world = forge::wld::File::parse(args[2]);
            const std::string prefix = args[4];
            const int dx = std::stoi(args[5]), dy = std::stoi(args[6]);
            std::vector<std::string> selected;
            for (const auto& map : world.maps())
                if (map.levelName.rfind(prefix, 0) == 0)
                    selected.push_back(map.levelName);
            if (selected.empty())
                throw std::runtime_error("wld translate-prefix matched no maps");
            for (const auto& name : selected) {
                const auto* map = world.findMap(name);
                world.relocateMap(name, map->mapX + dx, map->mapY + dy);
            }
            const std::string encoded = world.serialize();
            std::ofstream out(args[3], std::ios::binary | std::ios::trunc);
            if (!out) throw std::runtime_error("cannot create translated WLD");
            out.write(encoded.data(), std::streamsize(encoded.size()));
            if (!out) throw std::runtime_error("failed writing translated WLD");
            std::printf("translated %zu WLD maps by (%d,%d) -> %s\n",
                        selected.size(), dx, dy, args[3].c_str());
            return 0;
        }
        if (args.size() >= 3 && args[0] == "bwd" && args[1] == "info") {
            return bwdInfo(args[2], args.size() > 3 ? args[3] : "");
        }
        if (args.size() >= 3 && args[0] == "bwd" && args[1] == "roundtrip") {
            return bwdRoundtrip(args[2]);
        }
        if (args.size() >= 6 && args[0] == "bwd" && args[1] == "set-region-name") {
            return bwdSetRegionName(std::vector<std::string>(args.begin() + 2, args.end()));
        }
        if (args.size() >= 5 && args[0] == "bwd" && args[1] == "add-region") {
            return bwdAddRegion(
                std::vector<std::string>(args.begin() + 2, args.end()));
        }
        if (args.size() >= 7 && args[0] == "bwd" && args[1] == "set-owner") {
            return bwdSetOwner(
                std::vector<std::string>(args.begin() + 2, args.end()));
        }
        if (args.size() >= 3 && args[0] == "bwd" && args[1] == "add-level") {
            return bwdAddLevel(
                std::vector<std::string>(args.begin() + 2, args.end()));
        }
        if (args.size() >= 5 && args[0] == "wld" && args[1] == "compile") {
            return wldCompile(args[2], args[3], args[4]);
        }
        if (args.size() >= 3 && args[0] == "world" && args[1] == "inspect") {
            return worldInspect(args[2], !args.empty() && args.back() == "--json");
        }
        if (args.size() >= 8 && args[0] == "world" && args[1] == "add-level") {
            return worldAddLevel(
                std::vector<std::string>(args.begin() + 2, args.end()));
        }
        if (args.size() >= 5 && args[0] == "world" &&
            args[1] == "add-tiled-region") {
            return worldAddTiledRegion(args[2], args[3], args[4]);
        }
        if (args.size() >= 7 && args[0] == "world" && args[1] == "install-level") {
            return worldInstallLevel(
                std::vector<std::string>(args.begin() + 2, args.end()));
        }
        if (args.size() >= 5 && args[0] == "world" && args[1] == "attach-map") {
            return worldAttachMap(
                std::vector<std::string>(args.begin() + 2, args.end()));
        }
        if (args.size() >= 8 && args[0] == "level" &&
            args[1] == "create-from-donor") {
            return levelCreateFromDonor(
                std::vector<std::string>(args.begin() + 2, args.end()));
        }
        if (args.size() >= 2 && args[0] == "foliage" && args[1] == "palette") {
            return foliagePalette(!args.empty() && args.back() == "--json");
        }
        if (args.size() >= 3 && args[0] == "foliage" && args[1] == "read") {
            const bool asJson = !args.empty() && args.back() == "--json";
            std::vector<std::string> rest(args.begin() + 2, args.end());
            if (asJson && !rest.empty() && rest.back() == "--json") rest.pop_back();
            return foliageRead(rest, asJson);
        }
        if (args.size() >= 3 && args[0] == "foliage" && args[1] == "meshinfo") {
            return foliageMeshInfo(args[2], !args.empty() && args.back() == "--json");
        }
        if (args.size() >= 3 && args[0] == "foliage" && args[1] == "instances") {
            const bool asJson = !args.empty() && args.back() == "--json";
            std::vector<std::string> rest(args.begin() + 2, args.end());
            if (asJson && !rest.empty() && rest.back() == "--json") rest.pop_back();
            return foliageInstances(rest, asJson);
        }
        if(args.size()>=5&&args[0]=="scene"&&args[1]=="audit")
            return sceneAudit({args[2],args[3],args[4]});
        if(args.size()>=5&&args[0]=="scene"&&args[1]=="audit-all")
            return sceneAuditAll(args[2],args[3],args[4]);
        if(args.size()>=3&&args[0]=="mesh"&&args[1]=="audit")
            return meshAudit(args[2],!args.empty()&&args.back()=="--json");
        if (args.size() >= 3 && args[0] == "catalog" && args[1] == "info") {
            return catalogInfo(args[2]);
        }
        if (args.size() >= 2 && args[0] == "bank-catalog") {
            const bool asJson = !args.empty() && args.back() == "--json";
            const std::string filter =
                args.size() > 2 && args[2] != "--json" ? args[2] : "";
            return bankCatalog(args[1], filter, asJson);
        }
        if (args.size() >= 3 && args[0] == "fse" && args[1] == "list") {
            const bool asJson = !args.empty() && args.back() == "--json";
            const std::string filter =
                args.size() > 3 && args[3] != "--json" ? args[3] : "";
            return fseList(args[2], filter, asJson);
        }
        if (args.size() >= 4 && args[0] == "fse" && args[1] == "show") {
            return fseShow(args[2], args[3],
                           args.size() > 4 && args[4] == "--json");
        }
        if (args.size() >= 2 && args[0] == "quest" && args[1] == "nodes") {
            bool asJson = false;
            std::string filter;
            std::string manifestPath;
            for (size_t i = 2; i < args.size(); ++i) {
                if (args[i] == "--json") { asJson = true; continue; }
                if (args[i] == "--manifest" && i + 1 < args.size()) {
                    manifestPath = args[++i];
                    continue;
                }
                if (filter.empty()) filter = args[i];
            }
            return questNodes(filter, asJson, manifestPath);
        }
        if (args.size() >= 3 && args[0] == "quest" && args[1] == "master") {
            std::string qstOut, luaOut;
            int id = 50000;
            std::vector<forge::questproject::GlobalState> globals;
            std::vector<std::string> pos;
            for (size_t i = 2; i < args.size(); ++i) {
                const std::string& a = args[i];
                auto next = [&]() -> std::string {
                    return i + 1 < args.size() ? args[++i] : std::string();
                };
                if (a == "--out") qstOut = next();
                else if (a == "--lua-out") luaOut = next();
                else if (a == "--id") { try { id = std::stoi(next()); } catch (...) {} }
                else if (a == "--global") {
                    // name:type[:default] (type is bool|int|string)
                    const std::string spec = next();
                    forge::questproject::GlobalState g;
                    const auto c1 = spec.find(':');
                    if (c1 == std::string::npos) {
                        g.name = spec;
                    } else {
                        g.name = spec.substr(0, c1);
                        const std::string rest = spec.substr(c1 + 1);
                        const auto c2 = rest.find(':');
                        if (c2 == std::string::npos) {
                            g.type = rest;
                        } else {
                            g.type = rest.substr(0, c2);
                            g.defaultValue = rest.substr(c2 + 1);
                        }
                    }
                    globals.push_back(g);
                } else {
                    pos.push_back(a);
                }
            }
            if (pos.empty()) {
                std::fprintf(stderr,
                             "quest master: usage: forge quest master <finalalbion.qst> "
                             "[--global name:type[:default]]... [--id N] "
                             "[--lua-out <FSE_Master.lua>] [--out <qst>]\n");
                return 2;
            }
            return questMaster(pos[0], globals, id, luaOut, qstOut);
        }
        if (args.size() >= 2 && args[0] == "gamedata") {
            std::string wldPath;
            bool full = false;
            std::vector<std::string> pos;
            for (size_t i = 1; i < args.size(); ++i) {
                const std::string& a = args[i];
                auto next = [&]() -> std::string {
                    return i + 1 < args.size() ? args[++i] : std::string();
                };
                if (a == "--wld") wldPath = next();
                else if (a == "--full") full = true;
                else pos.push_back(a);
            }
            if (pos.empty()) {
                std::fprintf(stderr, "gamedata: usage: forge gamedata <game-root> "
                                     "[--wld <finalalbion.wld>] [--full]\n");
                return 2;
            }
            return gamedataList(pos[0], wldPath, full);
        }
        if (args.size() >= 3 && args[0] == "quest" && args[1] == "compile") {
            std::string outPath;
            std::string manifestPath;
            for (size_t i = 3; i < args.size(); ++i) {
                if (args[i] == "--out" && i + 1 < args.size()) {
                    outPath = args[++i];
                    continue;
                }
                if (args[i] == "--manifest" && i + 1 < args.size()) {
                    manifestPath = args[++i];
                    continue;
                }
            }
            return questCompile(args[2], outPath, manifestPath);
        }
        if (args.size() >= 4 && args[0] == "quest" && args[1] == "deploy") {
            bool dryRun = false;
            std::string manifestPath;
            forge::questdeploy::DeployOptions options;
            for (size_t i = 4; i < args.size(); ++i) {
                if (args[i] == "--dry-run") { dryRun = true; continue; }
                if (args[i] == "--active") { options.activeFlag = 1; continue; }
                if (args[i] == "--dormant") { options.activeFlag = 0; continue; }
                if (args[i] == "--id" && i + 1 < args.size()) {
                    options.id = std::stoll(args[++i]);
                    continue;
                }
                if (args[i] == "--manifest" && i + 1 < args.size()) {
                    manifestPath = args[++i];
                    continue;
                }
                std::fprintf(stderr, "quest deploy: unknown option %s\n",
                             args[i].c_str());
                return 2;
            }
            forge::fse::Manifest manifest;
            if (!manifestPath.empty()) {
                manifest = forge::fse::load(manifestPath);
                options.manifest = &manifest;
            }
            return questDeploy(args[2], args[3], dryRun, options);
        }
        if (args.size() >= 3 && args[0] == "quest" && args[1] == "doctor") {
            return questDoctor(args[2], !args.empty() && args.back() == "--json");
        }
        // forge quest card <game-root> <schema.json> <NAME> --donor <OBJECT_QUEST_CARD_*>
        //   [--out <out-root> | --in-place] [--overwrite-donor] [--json]
        //   [--bank-catalog <UnifiedFable-dir>]
        //   [--quest-name N|TEXT_*] [--quest-summary N|TEXT_*]
        //   [--quest-objective N|TEXT_*] [--success-summary N|TEXT_*]
        //   [--gold N] [--renown N] [--boasts N]
        //   [--epilogue N] [--core 0|1] [--vignette 0|1] [--exclusive 0|1]
        //   [--can-cancel 0|1]
        if (args.size() >= 5 && args[0] == "quest" && args[1] == "card") {
            const std::string gameRoot = args[2];
            const std::string schemaPath = args[3];
            const std::string newName = args[4];
            std::string donor, outRoot, bankCatalogRoot, textDonor;
            std::optional<std::string> titleText, summaryText, objectiveText,
                                       successText;
            bool inPlace = false, overwriteDonor = false, fromScratch = false,
                 asJson = false;
            std::optional<bool> emitQuestFlag;  // unset -> default (on for --from-scratch)
            std::string questScriptName, questRegion;
            int32_t questId = 50100;
            forge::questcard::CardPatch patch;
            auto asI32 = [](const std::string& s) {
                return int32_t(std::stol(s));
            };
            for (size_t i = 5; i + 1 < args.size(); ++i) {
                if (args[i] == "--bank-catalog") {
                    bankCatalogRoot = args[i + 1];
                    break;
                }
            }
            std::optional<forge::bankcatalog::Catalog> symbolCatalog;
            if (!bankCatalogRoot.empty())
                symbolCatalog = forge::bankcatalog::load(bankCatalogRoot);
            // Display-text fields (QuestName/QuestSummary/SuccessSummary) are numeric
            // text.big TextIDs. A 0/negative id resolves to the engine's empty default
            // string -> the card shows a BLANK title/summary (the "displays but empty"
            // trap, root-caused in docs/QUEST_CARD_EMPTY_FIX.md). Reject it early;
            // omit the flag to inherit the donor's valid id.
            bool textIdError = false;
            auto asTextId = [&](const std::string& flag, const std::string& s,
                                bool rejectEmpty = true) -> int32_t {
                int32_t v = 0;
                size_t consumed = 0;
                try {
                    v = int32_t(std::stol(s, &consumed));
                } catch (const std::exception&) {
                    consumed = 0;
                }
                if (consumed != s.size()) {
                    if (!symbolCatalog) {
                        throw std::invalid_argument(
                            flag + " received symbol " + s +
                            "; add --bank-catalog <UnifiedFable-dir>");
                    }
                    const auto matches = symbolCatalog->resolve(s);
                    const forge::bankcatalog::Symbol* resolved = nullptr;
                    for (const auto& match : matches) {
                        if (match.symbol->enumName != "EGameText") continue;
                        if (resolved != nullptr && resolved->id != match.symbol->id)
                            throw std::runtime_error("quest card: ambiguous text symbol " + s);
                        resolved = match.symbol;
                    }
                    if (resolved == nullptr)
                        throw std::invalid_argument("quest card: unknown EGameText symbol " + s);
                    v = static_cast<int32_t>(resolved->id);
                    std::fprintf(stderr, "quest card: %s %s -> TextID %d\n",
                                 flag.c_str(), s.c_str(), v);
                }
                if (rejectEmpty && v <= 0) {
                    std::fprintf(stderr,
                        "quest card: %s TextID %d renders BLANK (resolves to the engine's "
                        "empty default string). Pass a valid text.big id, or omit %s to "
                        "inherit the donor's text.\n", flag.c_str(), v, flag.c_str());
                    textIdError = true;
                }
                return v;
            };
            auto asBool = [](const std::string& s) { return s != "0"; };
            for (size_t i = 5; i < args.size(); ++i) {
                const std::string& a = args[i];
                auto next = [&]() -> std::string {
                    return i + 1 < args.size() ? args[++i] : std::string();
                };
                if (a == "--donor") donor = next();
                else if (a == "--out") outRoot = next();
                else if (a == "--bank-catalog") { (void)next(); }
                else if (a == "--in-place") inPlace = true;
                else if (a == "--overwrite-donor") overwriteDonor = true;
                else if (a == "--from-scratch") fromScratch = true;
                else if (a == "--emit-quest") emitQuestFlag = true;
                else if (a == "--no-emit-quest") emitQuestFlag = false;
                else if (a == "--quest-script-name") questScriptName = next();
                else if (a == "--region") questRegion = next();
                else if (a == "--quest-id") questId = asI32(next());
                else if (a == "--json") asJson = true;
                else if (a == "--quest-name") patch.questName = asTextId("--quest-name", next());
                else if (a == "--quest-summary") patch.questSummary = asTextId("--quest-summary", next());
                else if (a == "--quest-objective") patch.questObjective = asTextId("--quest-objective", next(), false);
                else if (a == "--success-summary") patch.successSummary = asTextId("--success-summary", next());
                else if (a == "--title-text") titleText = next();
                else if (a == "--summary-text") summaryText = next();
                else if (a == "--objective-text") objectiveText = next();
                else if (a == "--success-text") successText = next();
                else if (a == "--text-donor") textDonor = next();
                else if (a == "--gold") patch.goldReward = asI32(next());
                else if (a == "--renown") patch.renownReward = asI32(next());
                else if (a == "--boasts") patch.numBoasts = asI32(next());
                else if (a == "--epilogue") patch.questEpilogue = uint32_t(std::stoul(next()));
                else if (a == "--core") patch.isCoreQuest = asBool(next());
                else if (a == "--vignette") patch.isVignette = asBool(next());
                else if (a == "--exclusive") patch.isExclusive = asBool(next());
                else if (a == "--can-cancel") patch.canPlayerCancel = asBool(next());
                else {
                    std::fprintf(stderr, "quest card: unknown option %s\n", a.c_str());
                    return 2;
                }
            }
            if (textIdError) return 2; // a display-text field was 0/negative (blank)
            if (fromScratch && (overwriteDonor || !donor.empty())) {
                std::fprintf(stderr, "quest card: --from-scratch cannot be combined "
                                     "with --donor/--overwrite-donor (it clones the "
                                     "engine's OBJECT_QUEST_CARD_TEMPLATE)\n");
                return 2;
            }
            if (!fromScratch && donor.empty()) {
                std::fprintf(stderr, "quest card: --donor <OBJECT_QUEST_CARD_*> "
                                     "is required (or pass --from-scratch)\n");
                return 2;
            }
            if (outRoot.empty() && !inPlace) {
                std::fprintf(stderr, "quest card: give --out <out-root> or "
                                     "--in-place\n");
                return 2;
            }
            if ((titleText && patch.questName) ||
                (summaryText && patch.questSummary) ||
                (objectiveText && patch.questObjective) ||
                (successText && patch.successSummary)) {
                std::fprintf(stderr,
                    "quest card: custom text and numeric/symbol TextID flags are "
                    "mutually exclusive for the same field\n");
                return 2;
            }

            // Stem shared by authored text symbols and the companion script's
            // objective reference (TEXT_QUEST_CARD_<STEM>_OBJECTIVE).
            std::string stem;
            stem.reserve(newName.size());
            for (unsigned char c : newName) {
                if (std::isalnum(c)) stem.push_back((char)std::toupper(c));
                else stem.push_back('_');
            }

            std::optional<QuestCardTextOutput> authoredText;
            if (titleText || summaryText || objectiveText || successText) {
                namespace fs = std::filesystem;
                const fs::path retailText =
                    fs::path(gameRoot) / "data" / "lang" / "English" / "text.big";
                const fs::path outputText =
                    inPlace ? retailText
                            : fs::path(outRoot) / "data" / "lang" / "English" /
                                  "text.big";
                // Repeated card-authoring calls against one output root append
                // to the prior output instead of restarting from retail.
                const fs::path sourceText =
                    (!inPlace && fs::exists(outputText)) ? outputText : retailText;
                authoredText.emplace();
                authoredText->file = forge::big::File::open(sourceText);

                const std::string prefix = "TEXT_QUEST_CARD_" + stem;
                auto addText = [&](const char* suffix,
                                   const std::optional<std::string>& content) {
                    if (!content) return std::optional<int32_t>{};
                    forge::textbig::Entry value;
                    value.type = 0;
                    value.content = *content;
                    const std::string symbol = prefix + suffix;
                    value.identifier = symbol;
                    const auto added = forge::textbig::upsertString(
                        authoredText->file, symbol, std::move(value),
                        std::nullopt, textDonor);
                    if (added.id > (uint32_t)std::numeric_limits<int32_t>::max())
                        throw std::runtime_error(
                            "quest card: allocated TextID exceeds int32 range");
                    authoredText->entries.push_back(added);
                    return std::optional<int32_t>((int32_t)added.id);
                };
                if (auto id = addText("_TITLE", titleText)) patch.questName = *id;
                if (auto id = addText("_SUMMARY", summaryText))
                    patch.questSummary = *id;
                if (auto id = addText("_OBJECTIVE", objectiveText))
                    patch.questObjective = *id;
                if (auto id = addText("_SUCCESS", successText))
                    patch.successSummary = *id;
            }
            const int rc = questCard(gameRoot, schemaPath, newName, donor, outRoot,
                                     patch, inPlace, overwriteDonor, fromScratch,
                                     asJson, authoredText ? &*authoredText : nullptr);
            // Companion quest: content-first cards need a quest script to populate
            // the runtime card (objective/gold/renown). Default ON for --from-scratch
            // (the card is otherwise blank-content); opt-in otherwise. --no-emit-quest
            // suppresses it.
            const bool emitQuest = emitQuestFlag.value_or(fromScratch);
            if (rc == 0 && emitQuest) {
                namespace fs = std::filesystem;
                forge::questcard::CompanionScriptSpec spec;
                spec.questName =
                    questScriptName.empty() ? newName : questScriptName;
                spec.objectName = "OBJECT_QUEST_CARD_" + newName;
                spec.questId = questId;
                spec.gold = patch.goldReward;
                spec.renown = patch.renownReward;
                if (objectiveText)
                    spec.objectiveSymbol = "TEXT_QUEST_CARD_" + stem + "_OBJECTIVE";
                if (!questRegion.empty()) spec.region = questRegion;
                const auto script = forge::questcard::generateCompanionScript(spec);

                const fs::path fseDir =
                    (inPlace ? fs::path(gameRoot) : fs::path(outRoot)) / "FSE" /
                    spec.questName;
                fs::create_directories(fseDir);
                std::ofstream(fseDir / (spec.questName + ".lua"), std::ios::binary)
                    << script.luaScript;
                std::ofstream(fseDir / "quests.lua", std::ios::binary)
                    << script.questsLua;
                std::printf("  companion quest -> %s\n",
                            (fseDir / (spec.questName + ".lua")).string().c_str());
                std::printf("  add to FSE/quests.lua: (see %s)\n",
                            (fseDir / "quests.lua").string().c_str());
                std::printf("  FinalAlbion.qst: %s  (auto-added by ForgeFSE "
                            "self-heal once the quest is in FSE/quests.lua; "
                            "survives Steam verify)\n", script.qstLine.c_str());
                std::printf("  add to FSE/Master/FSE_Master.lua: %s\n",
                            script.masterActivate.c_str());
            }
            return rc;
        }
        // forge ui set-graphic <game-root> <entryName> <textureId>
        //   [--state N] [--schema <schema.json>] [--out <out-root>|--in-place]
        // Set a UI/CUIDef sprite's texture (nested States[N].GraphicIndex). Quest-card
        // orb art: UI_QUEST_SPRITE_CORE/OPTIONAL/VIGNETTE, State 0 = tex 5892/5894/5896.
        if (args.size() >= 5 && args[0] == "ui" && args[1] == "set-graphic") {
            const std::string gameRoot = args[2];
            const std::string entryName = args[3];
            long textureId = -1;
            try { textureId = std::stol(args[4]); }
            catch (...) { std::fprintf(stderr, "ui set-graphic: <textureId> must be a number\n"); return 2; }
            uint32_t stateIndex = 0;
            std::string outRoot, schemaPath;
            bool inPlace = false;
            for (size_t i = 5; i < args.size(); ++i) {
                const std::string& a = args[i];
                auto next = [&]() -> std::string { return i + 1 < args.size() ? args[++i] : std::string(); };
                if (a == "--state") stateIndex = (uint32_t)std::stoul(next());
                else if (a == "--schema") schemaPath = next();
                else if (a == "--out") outRoot = next();
                else if (a == "--in-place") inPlace = true;
                else { std::fprintf(stderr, "ui set-graphic: unknown option %s\n", a.c_str()); return 2; }
            }
            if (outRoot.empty() && !inPlace) {
                std::fprintf(stderr, "ui set-graphic: give --out <out-root> or --in-place\n");
                return 2;
            }
            if (schemaPath.empty()) {
                namespace fs = std::filesystem;
                for (const char* cand : {"docs/re_reference/def_schema.json",
                                         "../docs/re_reference/def_schema.json"})
                    if (fs::exists(cand)) { schemaPath = cand; break; }
                if (schemaPath.empty()) {
                    std::fprintf(stderr, "ui set-graphic: --schema <schema.json> required "
                                         "(no default def_schema.json found)\n");
                    return 2;
                }
            }
            return uiSetGraphic(gameRoot, schemaPath, entryName, (uint32_t)textureId,
                                stateIndex, outRoot, inPlace);
        }
        // forge ui add-sprite <game-root> <newName> --from <srcEntry> --graphic <id>
        //   [--state N] [--schema <schema.json>] [--out <out-root>|--in-place]
        // Append a custom UI/CUIDef sprite (clone) for the per-card art detour.
        if (args.size() >= 5 && args[0] == "ui" && args[1] == "add-sprite") {
            const std::string gameRoot = args[2];
            const std::string newName = args[3];
            std::string src, outRoot, schemaPath;
            long textureId = -1;
            uint32_t stateIndex = 0;
            bool inPlace = false;
            for (size_t i = 4; i < args.size(); ++i) {
                const std::string& a = args[i];
                auto next = [&]() -> std::string { return i + 1 < args.size() ? args[++i] : std::string(); };
                if (a == "--from") src = next();
                else if (a == "--graphic") textureId = std::stol(next());
                else if (a == "--state") stateIndex = (uint32_t)std::stoul(next());
                else if (a == "--schema") schemaPath = next();
                else if (a == "--out") outRoot = next();
                else if (a == "--in-place") inPlace = true;
                else { std::fprintf(stderr, "ui add-sprite: unknown option %s\n", a.c_str()); return 2; }
            }
            if (src.empty() || textureId < 0) {
                std::fprintf(stderr, "ui add-sprite: --from <srcEntry> and --graphic <id> are required\n");
                return 2;
            }
            if (outRoot.empty() && !inPlace) {
                std::fprintf(stderr, "ui add-sprite: give --out <out-root> or --in-place\n");
                return 2;
            }
            if (schemaPath.empty()) {
                namespace fs = std::filesystem;
                for (const char* cand : {"docs/re_reference/def_schema.json",
                                         "../docs/re_reference/def_schema.json"})
                    if (fs::exists(cand)) { schemaPath = cand; break; }
                if (schemaPath.empty()) {
                    std::fprintf(stderr, "ui add-sprite: --schema <schema.json> required\n");
                    return 2;
                }
            }
            return uiAddSprite(gameRoot, schemaPath, newName, src, (uint32_t)textureId,
                               stateIndex, outRoot, inPlace);
        }
        if (args.size() >= 6 && args[0] == "ui" && args[1] == "clone-object-model") {
            const std::string gameRoot = args[2], donor = args[3], newName = args[4];
            uint32_t meshId = 0;
            try { meshId = (uint32_t)std::stoul(args[5]); }
            catch (...) { std::fprintf(stderr, "ui clone-object-model: invalid meshId\n"); return 2; }
            std::string outRoot, schemaPath;
            for (size_t i = 6; i < args.size(); ++i) {
                if (args[i] == "--out" && i + 1 < args.size()) outRoot = args[++i];
                else if (args[i] == "--schema" && i + 1 < args.size()) schemaPath = args[++i];
                else { std::fprintf(stderr, "ui clone-object-model: unknown option %s\n", args[i].c_str()); return 2; }
            }
            if (outRoot.empty()) {
                std::fprintf(stderr, "ui clone-object-model: --out <out-root> is required\n");
                return 2;
            }
            if (schemaPath.empty()) {
                namespace fs = std::filesystem;
                for (const char* cand : {"docs/re_reference/def_schema.json",
                                         "../docs/re_reference/def_schema.json"})
                    if (fs::exists(cand)) { schemaPath = cand; break; }
            }
            if (schemaPath.empty()) {
                std::fprintf(stderr, "ui clone-object-model: --schema <schema.json> required\n");
                return 2;
            }
            return uiCloneObjectModel(gameRoot, schemaPath, donor, newName, meshId, outRoot);
        }
        // forge ui set-cardmodel <game-root> <objectName> <meshId>
        //   [--schema <schema.json>] [--out <out-root>|--in-place]
        // Set an OBJECT's Graphic.modelId (card body mesh for OBJECT_QUEST_CARD).
        if (args.size() >= 5 && args[0] == "ui" && args[1] == "set-cardmodel") {
            const std::string gameRoot = args[2];
            const std::string entryName = args[3];
            long meshId = -1;
            try { meshId = std::stol(args[4]); }
            catch (...) { std::fprintf(stderr, "ui set-cardmodel: <meshId> must be a number\n"); return 2; }
            std::string outRoot, schemaPath;
            bool inPlace = false;
            for (size_t i = 5; i < args.size(); ++i) {
                const std::string& a = args[i];
                auto next = [&]() -> std::string { return i + 1 < args.size() ? args[++i] : std::string(); };
                if (a == "--schema") schemaPath = next();
                else if (a == "--out") outRoot = next();
                else if (a == "--in-place") inPlace = true;
                else { std::fprintf(stderr, "ui set-cardmodel: unknown option %s\n", a.c_str()); return 2; }
            }
            if (outRoot.empty() && !inPlace) {
                std::fprintf(stderr, "ui set-cardmodel: give --out <out-root> or --in-place\n");
                return 2;
            }
            if (schemaPath.empty()) {
                namespace fs = std::filesystem;
                for (const char* cand : {"docs/re_reference/def_schema.json",
                                         "../docs/re_reference/def_schema.json"})
                    if (fs::exists(cand)) { schemaPath = cand; break; }
                if (schemaPath.empty()) {
                    std::fprintf(stderr, "ui set-cardmodel: --schema <schema.json> required\n");
                    return 2;
                }
            }
            return uiSetCardModel(gameRoot, schemaPath, entryName, (uint32_t)meshId,
                                  outRoot, inPlace);
        }
        // forge defs set-field <bin> [--names <names.bin>] <entryName> <field>
        //   <value> [--schema <schema.json>] [--out <out.bin>] [--json]
        if (args.size() >= 5 && args[0] == "defs" && args[1] == "set-field") {
            std::string binPath, namesPath, schemaPath, outPath;
            bool asJson = false;
            std::vector<std::string> positional;  // bin, entry, field, value
            for (size_t i = 2; i < args.size(); ++i) {
                const std::string& a = args[i];
                auto next = [&]() -> std::string {
                    return i + 1 < args.size() ? args[++i] : std::string();
                };
                if (a == "--names") namesPath = next();
                else if (a == "--schema") schemaPath = next();
                else if (a == "--out") outPath = next();
                else if (a == "--json") asJson = true;
                else positional.push_back(a);
            }
            if (positional.size() < 4) {
                std::fprintf(stderr,
                             "defs set-field: usage: forge defs set-field <bin> "
                             "[--names <names.bin>] <entryName> <field> <value> "
                             "[--schema <schema.json>] [--out <out.bin>]\n");
                return 2;
            }
            binPath = positional[0];
            const std::string entry = positional[1];
            const std::string field = positional[2];
            const std::string value = positional[3];
            if (schemaPath.empty()) {
                // Default to the RE-derived schema shipped in the repo.
                namespace fs = std::filesystem;
                for (const char* cand :
                     {"docs/re_reference/def_schema.json",
                      "../docs/re_reference/def_schema.json"}) {
                    if (fs::exists(cand)) { schemaPath = cand; break; }
                }
                if (schemaPath.empty()) {
                    std::fprintf(stderr,
                                 "defs set-field: --schema <schema.json> is "
                                 "required (no default def_schema.json found)\n");
                    return 2;
                }
            }
            return defsSetField(binPath, namesPath, schemaPath, entry, field,
                                value, outPath, asJson);
        }
        if (args.size() >= 3 && args[0] == "defs" && args[1] == "list") {
            return defsList(args[2], args.size() > 3 ? args[3] : "",
                            args.size() > 4 ? args[4] : "");
        }
        if (args.size() >= 3 && args[0] == "controls" && args[1] == "list") {
            std::string namesPath;
            std::vector<std::string> pos;
            for (size_t i = 2; i < args.size(); ++i) {
                const std::string& a = args[i];
                auto next = [&]() -> std::string {
                    return i + 1 < args.size() ? args[++i] : std::string();
                };
                if (a == "--names") namesPath = next();
                else pos.push_back(a);
            }
            if (pos.empty()) {
                std::fprintf(stderr, "controls list: usage: forge controls list "
                                     "<bin> [--names <n>]\n");
                return 2;
            }
            return controlsList(pos[0], namesPath);
        }
        if (args.size() >= 6 && args[0] == "controls" && args[1] == "set-binding") {
            std::string namesPath, outPath;
            int occurrence = -1;
            std::vector<std::string> pos;  // bin, entry, action, devtype, value
            for (size_t i = 2; i < args.size(); ++i) {
                const std::string& a = args[i];
                auto next = [&]() -> std::string {
                    return i + 1 < args.size() ? args[++i] : std::string();
                };
                if (a == "--names") namesPath = next();
                else if (a == "--out") outPath = next();
                else if (a == "--occurrence") { try { occurrence = std::stoi(next()); } catch (...) {} }
                else pos.push_back(a);
            }
            if (pos.size() < 5) {
                std::fprintf(stderr,
                             "controls set-binding: usage: forge controls set-binding "
                             "<bin> <entry> <action> <pad|key|mouse> <value> "
                             "[--occurrence N] [--names <n>] [--out <bin>]\n");
                return 2;
            }
            const std::string& dt = pos[3];
            int type = 0;
            if (dt == "pad") type = forge::controls::PAD;
            else if (dt == "key" || dt == "keyboard") type = forge::controls::KEYBOARD;
            else if (dt == "mouse") type = forge::controls::MOUSE;
            else {
                std::fprintf(stderr, "controls set-binding: device type must be "
                                     "pad|key|mouse (got %s)\n", dt.c_str());
                return 2;
            }
            // <action> and <value> accept either a decimal int or the PDB-proven
            // enum constant (GAME_ACTION_*, and XBOX_PAD_*/KB_*/MOUSE_* per device).
            std::int32_t action = 0;
            if (!forge::controls::gameActionValue(pos[2], action)) {
                try { action = std::stoi(pos[2]); }
                catch (...) {
                    std::fprintf(stderr, "controls set-binding: <action> '%s' is not "
                                         "an integer or a GAME_ACTION_* name\n",
                                 pos[2].c_str());
                    return 2;
                }
            }
            std::int32_t value = 0;
            bool valueOk = false;
            if (type == forge::controls::PAD)
                valueOk = forge::controls::xboxButtonValue(pos[4], value);
            else if (type == forge::controls::KEYBOARD)
                valueOk = forge::controls::inputKeyValue(pos[4], value);
            else
                valueOk = forge::controls::mouseButtonValue(pos[4], value);
            if (!valueOk) {
                try { value = std::stoi(pos[4]); valueOk = true; }
                catch (...) {}
            }
            if (!valueOk) {
                std::fprintf(stderr, "controls set-binding: <value> '%s' is not an "
                                     "integer or a valid enum name for %s\n",
                             pos[4].c_str(), dt.c_str());
                return 2;
            }
            return controlsSetBinding(pos[0], namesPath, pos[1], action, type, value,
                                      occurrence, outPath);
        }
        if (args.size() >= 4 && args[0] == "defs" && args[1] == "show") {
            return defsShow(args[2], args[3], args.size() > 4 ? args[4] : "");
        }
        if (args.size() >= 5 && args[0] == "defs" && args[1] == "decode" &&
            args[4] == "--all") {
            const std::string binName = args.size() > 5 ? args[5] : "";
            return defsDecodeAll(args[2], args[3], binName);
        }
        if (args.size() >= 5 && args[0] == "defs" && args[1] == "decode") {
            const bool asJson = !args.empty() && args.back() == "--json";
            const std::string binName =
                args.size() > 5 && args[5] != "--json" ? args[5] : "";
            return defsDecode(args[2], args[3], args[4], binName, asJson);
        }
        if (args.size() >= 3 && args[0] == "defs" && args[1] == "families") {
            return defsFamilies(args[2]);
        }
        if (args.size() >= 3 && args[0] == "defs" && args[1] == "schema") {
            const bool asJson = !args.empty() && args.back() == "--json";
            const std::string defType =
                args.size() > 3 && args[3] != "--json" ? args[3] : "";
            return defsSchema(args[2], defType, asJson);
        }
        if (args.size() >= 3 && args[0] == "defs" && args[1] == "roundtrip") {
            return defsRoundtrip(args[2], args.size() > 3 ? args[3] : "");
        }
        if (args.size() >= 4 && args[0] == "defs" && args[1] == "diff") {
            const bool asJson = !args.empty() && args.back() == "--json";
            const std::string binName =
                args.size() > 4 && args[4] != "--json" ? args[4] : "";
            return defsDiff(args[2], args[3], binName, asJson);
        }
        if (args.size() >= 4 && args[0] == "mods" && args[1] == "analyze") {
            const bool asJson = !args.empty() && args.back() == "--json";
            std::vector<std::string> modRoots;
            for (size_t i = 3; i < args.size(); ++i) {
                if (args[i] != "--json") modRoots.push_back(args[i]);
            }
            return modsAnalyze(args[2], modRoots, asJson);
        }
        if (args.size() >= 5 && args[0] == "mods" && args[1] == "merge") {
            const bool asJson = !args.empty() && args.back() == "--json";
            bool doStage = false;
            std::string fieldSchema;
            std::vector<std::string> sources;
            bool seenWith = false;
            for (size_t i = 4; i < args.size(); ++i) {
                if (args[i] == "--json") continue;
                if (args[i] == "--stage") { doStage = true; continue; }
                if (args[i] == "--fields" && i + 1 < args.size()) {
                    fieldSchema = args[++i];
                    continue;
                }
                if (args[i] == "--with") { seenWith = true; continue; }
                sources.push_back(args[i]);
            }
            (void)seenWith;
            return modsMerge(args[2], args[3], sources, fieldSchema, doStage, asJson);
        }
        if (args.size() >= 6 && args[0] == "defs" && args[1] == "merge") {
            const bool asJson = !args.empty() && args.back() == "--json";
            std::vector<std::string> modRoots;
            std::string picksPath, fieldSchemaPath;
            for (size_t i = 5; i < args.size(); ++i) {
                if (args[i] == "--json") continue;
                if (args[i] == "--picks" && i + 1 < args.size()) {
                    picksPath = args[++i];
                    continue;
                }
                if (args[i] == "--fields" && i + 1 < args.size()) {
                    fieldSchemaPath = args[++i];
                    continue;
                }
                modRoots.push_back(args[i]);
            }
            return mergeDefs(args[2], args[3], args[4], modRoots, picksPath,
                             fieldSchemaPath, asJson);
        }
        if (args.size() >= 3 && args[0] == "script" && args[1] == "refs") {
            const bool asJson = !args.empty() && args.back() == "--json";
            const std::string filter =
                args.size() > 3 && args[3] != "--json" ? args[3] : "";
            return scriptRefs(args[2], filter, asJson);
        }
        if (args.size() >= 3 && args[0] == "script" && args[1] == "cutscenes") {
            const bool asJson = !args.empty() && args.back() == "--json";
            const std::string filter =
                args.size() > 3 && args[3] != "--json" ? args[3] : "";
            return scriptCutscenes(args[2], filter, asJson);
        }
        if (args.size() >= 3 && args[0] == "script" && args[1] == "command-stats") {
            return scriptCommandStats(args[2],
                                      args.size() > 3 && args[3] == "--json");
        }
        if (args.size() >= 2 && args[0] == "script" && args[1] == "verbs") {
            const bool asJson = !args.empty() && args.back() == "--json";
            const std::string filter =
                args.size() > 2 && args[2] != "--json" ? args[2] : "";
            return scriptVerbs(filter, asJson);
        }
        if (args.size() >= 3 && args[0] == "script" && args[1] == "validate") {
            const bool asJson = !args.empty() && args.back() == "--json";
            const std::string filter =
                args.size() > 3 && args[3] != "--json" ? args[3] : "";
            return scriptValidate(args[2], filter, asJson);
        }
        if (args.size() >= 3 && args[0] == "script" && args[1] == "fixup") {
            const bool write = args.size() > 3 && args[3] == "--write";
            return scriptFixup(args[2], write);
        }
        if (args.size() >= 4 && args[0] == "script" && args[1] == "cutscene") {
            return scriptCutscene(args[2], args[3],
                                  args.size() > 4 && args[4] == "--json");
        }
        if (args.size() >= 3 && args[0] == "chest" && args[1] == "list") {
            return chestList(args[2], args.size() > 3 ? args[3] : "");
        }
        if (args.size() >= 2 && args[0] == "validate") {
            return validate(args[1]);
        }
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }

    return usage();
}
