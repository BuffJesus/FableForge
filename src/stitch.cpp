#include "stitch.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

#include "forge/wad.hpp"
#include "leveledit.hpp"

namespace fs = std::filesystem;

namespace albion::editor {

namespace {

std::string lower(std::string s) { for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c))); return s; }

// loose .lev wins; otherwise the WAD entry through a temp copy (lev::File is path based)
fs::path levPathFor(const fs::path& gameRoot, const std::string& stem) {
    const fs::path loose = gameRoot / "data" / "Levels" / "FinalAlbion" / (stem + ".lev");
    if (fs::exists(loose)) return loose;
    const fs::path wadPath = gameRoot / "data" / "Levels" / "FinalAlbion.wad";
    const auto wad = forge::wad::Archive::open(wadPath);
    const std::string want = lower(stem + ".lev");
    for (const auto& e : wad.entries())
        if (lower(fs::path(e.name).filename().string()) == want) {
            const auto bytes = wad.read(e);
            const fs::path tmp = fs::temp_directory_path() / "Albion Atlas" / "stitch";
            fs::create_directories(tmp);
            const fs::path out = tmp / (stem + ".lev");
            std::ofstream(out, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
            return out;
        }
    throw std::runtime_error(stem + ".lev is neither loose nor in FinalAlbion.wad");
}

} // namespace

bool sharedEdge(const WorldMapBox& a, const WorldMapBox& b, bool& horizontal, int& x0, int& y0, int& len) {
    // a vertical seam: one box's right vertex column is the other's left one
    if (a.x + a.w == b.x || b.x + b.w == a.x) {
        const int lo = std::max(a.y, b.y), hi = std::min(a.y + a.h, b.y + b.h);
        if (hi - lo >= 1) { horizontal = false; x0 = a.x + a.w == b.x ? b.x : a.x; y0 = lo; len = hi - lo + 1; return true; }
    }
    if (a.y + a.h == b.y || b.y + b.h == a.y) {
        const int lo = std::max(a.x, b.x), hi = std::min(a.x + a.w, b.x + b.w);
        if (hi - lo >= 1) { horizontal = true; y0 = a.y + a.h == b.y ? b.y : a.y; x0 = lo; len = hi - lo + 1; return true; }
    }
    return false;
}

bool stitchEdges(const fs::path& gameRoot, const WorldLayout& layout, const std::string& mapA, const std::string& mapB,
                 const StitchOptions& options, StitchReport& report, std::vector<std::string>& notes, std::string& error) {
    report = StitchReport{};
    report.a = mapA; report.b = mapB;
    const WorldMapBox* a = layout.find(mapA);
    const WorldMapBox* b = layout.find(mapB);
    if (!a) { error = "no map named " + mapA; return false; }
    if (!b) { error = "no map named " + mapB; return false; }
    bool horizontal = false; int x0 = 0, y0 = 0, len = 0;
    if (!sharedEdge(*a, *b, horizontal, x0, y0, len)) { error = mapA + " and " + mapB + " do not share an edge"; return false; }
    Document docA, docB;
    try {
        std::string warn;
        if (!docA.open(gameRoot, a->name, levPathFor(gameRoot, a->name), warn)) { error = mapA + ": " + warn; return false; }
        if (!docB.open(gameRoot, b->name, levPathFor(gameRoot, b->name), warn)) { error = mapB + ": " + warn; return false; }
    } catch (const std::exception& e) { error = e.what(); return false; }
    if (!docA.hasTerrain()) { error = mapA + " has no terrain (.lev)"; return false; }
    if (!docB.hasTerrain()) { error = mapB + " has no terrain (.lev)"; return false; }
    // the box says where the LEV stands; the LEV says how big it is
    if (docA.cellsX() != a->w + 1 || docA.cellsY() != a->h + 1) { error = mapA + ": LEV grid " + std::to_string(docA.cellsX() - 1) + "x" + std::to_string(docA.cellsY() - 1) + " differs from the placed box " + std::to_string(a->w) + "x" + std::to_string(a->h); return false; }
    if (docB.cellsX() != b->w + 1 || docB.cellsY() != b->h + 1) { error = mapB + ": LEV grid " + std::to_string(docB.cellsX() - 1) + "x" + std::to_string(docB.cellsY() - 1) + " differs from the placed box " + std::to_string(b->w) + "x" + std::to_string(b->h); return false; }

    const auto& ta = docA.terrain();
    const auto& tb = docB.terrain();
    auto heightAt = [](const TerrainState& t, int cx, int x, int y) { return t.heights[size_t(y) * cx + x]; };
    // the seam: per shared vertex the two heights and their mean
    std::vector<Document::VertexHeight> editsA, editsB;
    report.sharedVertices = len;
    for (int i = 0; i < len; ++i) {
        const int wx = horizontal ? x0 + i : x0, wy = horizontal ? y0 : y0 + i;
        report.maxStep = std::max(report.maxStep, std::fabs(heightAt(ta, docA.cellsX(), wx - a->x, wy - a->y) - heightAt(tb, docB.cellsX(), wx - b->x, wy - b->y)));
    }
    const int feather = options.feather >= 0 ? options.feather : std::clamp(int(std::ceil(report.maxStep)), 4, 32);
    report.feather = feather;
    // inward direction for each map: +1 when the seam is the map's low edge (x=0 / y=0), -1 when its high edge
    const int dirA = horizontal ? (y0 == a->y ? 1 : -1) : (x0 == a->x ? 1 : -1);
    const int dirB = horizontal ? (y0 == b->y ? 1 : -1) : (x0 == b->x ? 1 : -1);
    for (int i = 0; i < len; ++i) {
        const int wx = horizontal ? x0 + i : x0, wy = horizontal ? y0 : y0 + i;
        const int ax = wx - a->x, ay = wy - a->y, bx = wx - b->x, by = wy - b->y;
        const float ha = heightAt(ta, docA.cellsX(), ax, ay), hb = heightAt(tb, docB.cellsX(), bx, by);
        const float target = 0.5f * (ha + hb);
        // feather: the correction fades linearly over `feather` cells into each map
        for (int k = 0; k <= feather; ++k) {
            const float wgt = feather ? 1.0f - float(k) / float(feather + 1) : 1.0f;
            if (k > 0 && wgt <= 0) break;
            const int axk = horizontal ? ax : ax + dirA * k, ayk = horizontal ? ay + dirA * k : ay;
            const int bxk = horizontal ? bx : bx + dirB * k, byk = horizontal ? by + dirB * k : by;
            if (axk >= 0 && ayk >= 0 && axk < docA.cellsX() && ayk < docA.cellsY())
                editsA.push_back({axk, ayk, heightAt(ta, docA.cellsX(), axk, ayk) + (target - ha) * wgt});
            if (bxk >= 0 && byk >= 0 && bxk < docB.cellsX() && byk < docB.cellsY())
                editsB.push_back({bxk, byk, heightAt(tb, docB.cellsX(), bxk, byk) + (target - hb) * wgt});
        }
    }
    notes.push_back("seam " + mapA + " | " + mapB + ": " + std::to_string(len) + " shared vertices, largest step " + std::to_string(report.maxStep));
    if (report.maxStep < 1e-4f) { notes.push_back("  already tight, nothing to do"); return true; }
    if (!options.deploy) return true;
    const TerrainState beforeA = docA.terrain(), beforeB = docB.terrain();
    if (!docA.setVertexHeights(editsA) || !docB.setVertexHeights(editsB)) { error = "could not apply the seam heights"; return false; }
    if (!docA.deployTerrain(gameRoot, notes, error)) { error = mapA + ": " + error; return false; }
    if (!docB.deployTerrain(gameRoot, notes, error)) { error = mapB + ": " + error; return false; }
    // placed things that stood on the old ground follow it (.tng loose + WAD);
    // the chunk's own foliage keeps its Z (open)
    auto reseat = [&](Document& doc, const TerrainState& before) {
        const size_t n = doc.reseatThings(before);
        if (!n) return true;
        if (!doc.saveLoose(gameRoot, error) || !doc.deployWad(gameRoot, error)) { error = doc.mapName() + ": " + error; return false; }
        doc.markSaved();
        report.thingsReseated += n;
        notes.push_back("  " + doc.mapName() + ": " + std::to_string(n) + " placed thing(s) re-seated on the new ground");
        return true;
    };
    if (!reseat(docA, beforeA) || !reseat(docB, beforeB)) return false;
    report.stitched = true;
    notes.push_back("  stitched (feather " + std::to_string(feather) + " cells)");
    return true;
}

bool stitchNeighbours(const fs::path& gameRoot, const WorldLayout& layout, const std::string& map, const StitchOptions& options,
                      std::vector<StitchReport>& reports, std::vector<std::string>& notes, std::string& error) {
    const WorldMapBox* box = layout.find(map);
    if (!box) { error = "no map named " + map; return false; }
    bool ok = true;
    std::string firstError;
    for (const auto* n : layout.touching(*box, box->x, box->y)) {
        bool horizontal = false; int x0 = 0, y0 = 0, len = 0;
        if (!sharedEdge(*box, *n, horizontal, x0, y0, len)) continue;   // corner contact only
        if (!n->inStb) { notes.push_back("seam " + box->name + " | " + n->name + ": neighbour has no terrain chunk, skipped"); continue; }
        StitchReport r; std::string err;
        if (!stitchEdges(gameRoot, layout, box->name, n->name, options, r, notes, err)) {
            ok = false;
            if (firstError.empty()) firstError = err;
            notes.push_back("  failed: " + err);
        }
        reports.push_back(r);
    }
    if (!ok) error = firstError;
    return ok;
}

} // namespace albion::editor
