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

int usage() {
    std::puts(
        "FableForge " ALBION_VERSION " -- Fable: The Lost Chapters level editor (maps -> .glb / .obj, levels, terrain, textures)\n"
        "\n"
        "usage:\n"
        "  forge list   [--install <fable-root>]\n"
        "  forge info   <map|file.lev> [--install <fable-root>]\n"
        "  forge export <map|file.lev> [--out <file.glb|file.obj>] [options]\n"
        "  forge new-level <donor> <name> [--at x,y] [--region <host>] [--dedicated] [--no-rebake] [--install <root>]\n"
        "  forge blank-level <name> [--size WxH] [--at x,y] [--region <host>] [--template <map>] [--theme <slot|name>] [--height <h>] [--install <root>]\n"
        "      new-level / blank-level: [--own-region [new|<filler region>]] [--merge-into <filler>] [--display <name>] [--no-minimap]\n"
        "      (own region = a NEW region slot (`new`; complete on a game started after it) or a retail filler slot taken over; baked MINIMAP_<NAME>)\n"
        "  forge world  [--install <root>]                      every map's box, region and baked origin\n"
        "  forge world-move <map> <x> <y> [<map> <x> <y> ...] [--install <root>]\n"
        "      (relocate maps: WLD/BWD placement + STB chunks translated to the new origins, touching neighbours re-baked)\n"
        "  forge world-owner <map> <region>   |   forge world-sees <region> <map> <0|1>   (region edits; world --regions lists them)\n"
        "  forge theme-add <png> <NAME> [--donor <ENGINE_THEME>] [--cliff <png>] [--install <root>]\n"
        "      (a ground theme from your own texture: appended to textures.big + a new ENGINE_THEME in game.bin; paint it from the editor)\n"
        "  forge textures [filter] [--bank <bank>]              (list textures.big entries: id, size, format, bank)\n"
        "  forge texture-export <name> <out.png>   |   texture-replace <name> <image>   |   texture-add <name> <image> [--bank B] [--format dxt1|dxt3|argb8888]\n"
        "  forge entrance <map> [x y [z]]                    (show / set the map's region entrance in FinalAlbion.gtg; z defaults to the ground)\n"
        "  forge backups   |   forge restore [--forget]      (every .atlas-orig / .atlas-created under the install; restore puts the retail files back)\n"
        "  forge region-props <region> [--def <REGION_DEF>] [--minimap <MINIMAP_X>] [--display <name>] [--worldmap 0|1]   (a region's def/minimap/name, WLD + BWD)\n"
        "  forge world-stitch <map> [<map2>] [--feather <cells>|auto] [--dry-run] [--install <root>]\n"
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
        const fs::path dir = fs::temp_directory_path() / "FableForge";
        fs::create_directories(dir);
        tempOut = dir / (name + ".lev");
        std::ofstream(tempOut, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        return tempOut;
    }
    throw std::runtime_error("no map named '" + name + "' in " + wadPath.string() + " (try: forge list)");
}

}  // namespace albion::cli
