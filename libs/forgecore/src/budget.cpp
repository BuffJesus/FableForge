#include "forge/budget.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <set>
#include <sstream>

namespace forge::budget {

namespace {

uint32_t powerOfTwoUp(uint32_t v) {
    uint32_t p = 1;
    while (p < v && p < 0x80000000u) p <<= 1;
    return p;
}

std::string lowerCopy(std::string s) {
    for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

void sortLines(std::vector<Line>& lines, bool byBytes) {
    std::sort(lines.begin(), lines.end(), [byBytes](const Line& a, const Line& b) {
        const uint64_t ka = byBytes ? a.bytes : (a.triangles ? a.triangles : a.count);
        const uint64_t kb = byBytes ? b.bytes : (b.triangles ? b.triangles : b.count);
        if (ka != kb) return ka > kb;
        return a.name < b.name;
    });
}

} // namespace

Kind kindOfThingType(const std::string& type) {
    const std::string t = lowerCopy(type);
    if (t == "building") return kBuildings;
    if (t == "creature" || t == "aicreature") return kCreatures;
    if (t == "object") return kObjects;
    return kOthers;
}

uint64_t textureMemory(const terraintex::TextureInfo& info) {
    if (info.allocWidth == 0 || info.allocHeight == 0) return 0;
    uint32_t w = powerOfTwoUp(info.allocWidth), h = powerOfTwoUp(info.allocHeight);
    uint32_t d = info.depth == 0 ? 1 : info.depth;
    const uint32_t mips = info.mipLevels == 0 ? 1 : info.mipLevels;
    uint64_t total = 0;
    for (uint32_t level = 0; level < mips; ++level) {
        total += uint64_t(terraintex::mipRawLength(info.pixelFormat, w, h)) * d;
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
        d = std::max(1u, d / 2);
    }
    return total;
}

Report survey(const std::vector<Item>& items, const MeshLookup& mesh, const TextureLookup& texture,
              const Options& options) {
    Report r;
    std::map<std::string, Line> defs;
    std::map<uint32_t, Line> meshes;
    std::map<uint32_t, MeshCost> meshCache;
    std::set<uint32_t> textureIds;
    for (const auto& item : items) {
        if (!(options.include & item.kind)) continue;
        if (item.mesh == 0) { ++r.skippedNoGraphic; continue; }
        ++r.things;
        Line& d = defs[item.name];
        d.name = item.name;
        ++d.count;
        auto cached = meshCache.find(item.mesh);
        if (cached == meshCache.end()) cached = meshCache.emplace(item.mesh, mesh ? mesh(item.mesh) : MeshCost{}).first;
        const MeshCost& mc = cached->second;
        Line& m = meshes[item.mesh];
        if (m.name.empty()) m.name = mc.name.empty() ? "mesh " + std::to_string(item.mesh) : mc.name;
        ++m.count;
        if (!mc.ok) continue;
        for (uint32_t t : mc.textures) if (t) textureIds.insert(t);
    }
    // triangles / vertices: each mesh once, or once per instance
    for (auto& [id, m] : meshes) {
        const MeshCost& mc = meshCache[id];
        if (!mc.ok) { r.problems.push_back(m.name + ": the mesh could not be read (" + std::to_string(m.count) + " uses not counted)"); continue; }
        const uint64_t times = options.countAllDuplications ? m.count : 1;
        m.triangles = uint64_t(mc.triangles) * times;
        m.vertices = uint64_t(mc.vertices) * times;
        r.triangles += m.triangles;
        r.vertices += m.vertices;
        r.meshes.push_back(m);
    }
    // texture memory: each distinct texture once
    for (uint32_t id : textureIds) {
        const TextureCost tc = texture ? texture(id) : TextureCost{};
        Line t;
        t.name = tc.name.empty() ? "texture " + std::to_string(id) : tc.name;
        if (!tc.ok) { r.problems.push_back(t.name + ": not in the texture bank"); continue; }
        t.bytes = tc.bytes;
        t.count = 1;
        r.textureBytes += tc.bytes;
        r.textures.push_back(t);
    }
    for (auto& [name, d] : defs) r.definitions.push_back(d);
    sortLines(r.definitions, false);
    sortLines(r.meshes, false);
    sortLines(r.textures, true);
    return r;
}

std::string formatBytes(uint64_t bytes) {
    char buf[32];
    if (bytes >= 1024ull * 1024ull) std::snprintf(buf, sizeof buf, "%.1f MB", double(bytes) / (1024.0 * 1024.0));
    else if (bytes >= 1024ull) std::snprintf(buf, sizeof buf, "%.0f KB", double(bytes) / 1024.0);
    else std::snprintf(buf, sizeof buf, "%llu B", static_cast<unsigned long long>(bytes));
    return buf;
}

std::string toText(const Report& r, const std::string& title, const Options& o) {
    std::ostringstream s;
    s << "Budget survey: " << title << "\n";
    s << "Includes:";
    if (o.include & kBuildings) s << " buildings";
    if (o.include & kCreatures) s << " creatures";
    if (o.include & kObjects) s << " objects";
    if (o.include & kOthers) s << " others";
    if (o.include & kLocalDetail) s << " local-detail";
    s << (o.countAllDuplications ? "; every instance counted" : "; each mesh counted once") << "\n\n";
    s << "Number of things: " << r.things << "\n";
    s << "Triangles: " << r.triangles << "\n";
    s << "Vertices: " << r.vertices << "\n";
    s << "Total texture memory: " << r.textureBytes << " bytes (" << formatBytes(r.textureBytes) << ")\n";
    if (r.skippedNoGraphic) s << "Without a graphic (not counted): " << r.skippedNoGraphic << "\n";
    s << "\nThings per definition:\n";
    for (const auto& d : r.definitions) s << "  " << d.count << "  " << d.name << "\n";
    s << "\nTriangles per mesh:\n";
    for (const auto& m : r.meshes) s << "  " << m.triangles << " tris, " << m.vertices << " verts, " << m.count << " uses  " << m.name << "\n";
    s << "\nDetailed textures summary:\n";
    for (const auto& t : r.textures) s << "  " << formatBytes(t.bytes) << "  " << t.name << "\n";
    if (!r.problems.empty()) {
        s << "\nProblems:\n";
        for (const auto& p : r.problems) s << "  " << p << "\n";
    }
    return s.str();
}

} // namespace forge::budget
