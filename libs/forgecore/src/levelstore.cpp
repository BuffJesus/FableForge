#include "forge/levelstore.hpp"

#include "forge/wad.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <system_error>

namespace forge::levelstore {
namespace {

namespace fs = std::filesystem;

std::string lowered(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

bool isFile(const fs::path& p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}

std::vector<uint8_t> readAll(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + p.string());
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

} // namespace

std::string Layout::describe() const {
    switch (kind) {
    case Kind::Wad: return "FinalAlbion.wad";
    case Kind::Loose: return "the loose Data\\Levels\\FinalAlbion files";
    default: return "no levels (neither FinalAlbion.wad nor loose Data\\Levels\\FinalAlbion\\*.lev)";
    }
}

Layout detect(const fs::path& gameRoot) {
    Layout l;
    l.levelsDir = gameRoot / "data" / "Levels";
    l.looseDir = l.levelsDir / "FinalAlbion";
    std::error_code ec;
    if (fs::is_directory(l.looseDir, ec))
        for (const auto& e : fs::directory_iterator(l.looseDir, ec))
            if (e.is_regular_file(ec) && lowered(e.path().extension().string()) == ".lev") ++l.looseLevels;
    if (isFile(l.levelsDir / "FinalAlbion.wad")) {
        l.kind = Kind::Wad;
        l.wad = l.levelsDir / "FinalAlbion.wad";
    } else if (l.looseLevels > 0) {
        l.kind = Kind::Loose;
    }
    if (fs::is_directory(l.levelsDir, ec))
        for (const auto& e : fs::directory_iterator(l.levelsDir, ec)) {
            const std::string name = lowered(e.path().filename().string());
            if (e.is_regular_file(ec) && name != "finalalbion.wad" && name.size() > 4 &&
                name.compare(name.size() - 4, 4, ".wad") == 0 && name.find("finalalbion") != std::string::npos) {
                l.renamedWad = e.path();
                break;
            }
        }
    return l;
}

std::vector<Level> listLevels(const Layout& layout) {
    std::vector<Level> out;
    if (layout.hasWad()) {
        const auto wad = wad::Archive::open(layout.wad);
        for (const auto& e : wad.entries()) {
            const fs::path p(e.name);
            if (lowered(p.extension().string()) != ".lev") continue;
            out.push_back({p.stem().string(), e.size});
        }
    } else if (layout.looseOnly()) {
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(layout.looseDir, ec)) {
            if (!e.is_regular_file(ec) || lowered(e.path().extension().string()) != ".lev") continue;
            out.push_back({e.path().stem().string(), static_cast<uint64_t>(e.file_size(ec))});
        }
    }
    std::sort(out.begin(), out.end(), [](const Level& a, const Level& b) { return lowered(a.stem) < lowered(b.stem); });
    return out;
}

fs::path loosePath(const Layout& layout, const std::string& fileName) {
    return layout.looseDir / fileName;
}

std::optional<std::vector<uint8_t>> readFile(const Layout& layout, const std::string& fileName) {
    const fs::path loose = loosePath(layout, fileName);
    if (isFile(loose)) return readAll(loose);
    if (!layout.hasWad()) return std::nullopt;
    const auto wad = wad::Archive::open(layout.wad);
    const std::string want = lowered(fileName);
    for (const auto& e : wad.entries())
        if (lowered(fs::path(e.name).filename().string()) == want) return wad.read(e);
    return std::nullopt;
}

std::vector<uint8_t> requireFile(const Layout& layout, const std::string& fileName) {
    auto bytes = readFile(layout, fileName);
    if (!bytes) throw std::runtime_error(fileName + " is neither loose nor in " + layout.describe());
    return std::move(*bytes);
}

} // namespace forge::levelstore
