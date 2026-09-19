#include "forge/bwd.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <stdexcept>

namespace forge::bwd {

namespace {

// Little-endian byte reader over an in-memory buffer. Mirrors the Reader in
// tools/wld_bwd.py so parse/serialize stay byte-for-byte faithful.
class Reader {
public:
    explicit Reader(const std::vector<uint8_t>& data) : d_(data) {}

    uint32_t u32() {
        need(4);
        uint32_t v;
        std::memcpy(&v, d_.data() + o_, 4);
        o_ += 4;
        return v;
    }
    int32_t i32() { return static_cast<int32_t>(u32()); }
    uint64_t u64() {
        need(8);
        uint64_t v;
        std::memcpy(&v, d_.data() + o_, 8);
        o_ += 8;
        return v;
    }
    uint8_t u8() {
        need(1);
        return d_[o_++];
    }
    void raw(uint8_t* out, size_t n) {
        need(n);
        std::memcpy(out, d_.data() + o_, n);
        o_ += n;
    }
    std::string pstr() {
        const uint32_t n = u32();
        if (n > 0x10000)
            throw std::runtime_error("bwd: presized string length " +
                                     std::to_string(n) + " (layout drift)");
        need(n);
        std::string s(reinterpret_cast<const char*>(d_.data() + o_), n);
        o_ += n;
        return s;
    }
    size_t offset() const { return o_; }
    size_t size() const { return d_.size(); }

private:
    void need(size_t n) const {
        if (o_ + n > d_.size())
            throw std::runtime_error("bwd: unexpected end of file at offset " +
                                     std::to_string(o_));
    }
    const std::vector<uint8_t>& d_;
    size_t o_ = 0;
};

class Writer {
public:
    void u32(uint32_t v) {
        const uint8_t b[4] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8),
                              static_cast<uint8_t>(v >> 16),
                              static_cast<uint8_t>(v >> 24)};
        buf_.insert(buf_.end(), b, b + 4);
    }
    void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
    void u64(uint64_t v) {
        u32(static_cast<uint32_t>(v));
        u32(static_cast<uint32_t>(v >> 32));
    }
    void u8(uint8_t v) { buf_.push_back(v); }
    void raw(const uint8_t* p, size_t n) { buf_.insert(buf_.end(), p, p + n); }
    void pstr(const std::string& s) {
        u32(static_cast<uint32_t>(s.size()));
        buf_.insert(buf_.end(), s.begin(), s.end());
    }
    std::vector<uint8_t> take() { return std::move(buf_); }

private:
    std::vector<uint8_t> buf_;
};

MapInfo parseMap(Reader& r) {
    MapInfo m;
    m.levelName = r.pstr();
    m.scriptName = r.pstr();
    m.used = r.u8();
    m.loadedOnProximity = r.u8();
    m.isSea = r.u8();
    m.left = r.i32();
    m.right = r.i32();
    m.top = r.i32();
    m.bottom = r.i32();
    m.flag2 = r.u8();
    m.mapUid = r.u64();
    return m;
}

void writeMap(Writer& w, const MapInfo& m) {
    w.pstr(m.levelName);
    w.pstr(m.scriptName);
    w.u8(m.used);
    w.u8(m.loadedOnProximity);
    w.u8(m.isSea);
    w.i32(m.left);
    w.i32(m.right);
    w.i32(m.top);
    w.i32(m.bottom);
    w.u8(m.flag2);
    w.u64(m.mapUid);
}

Region parseRegion(Reader& r) {
    Region g;
    const int32_t nContains = r.i32();
    const int32_t nSees = r.i32();
    if (nContains < 0 || nSees < 0 || nContains > 0x10000 || nSees > 0x10000)
        throw std::runtime_error("bwd: region list count out of range (layout drift)");
    g.contains.reserve(nContains);
    for (int32_t i = 0; i < nContains; ++i) g.contains.push_back(r.i32());
    g.sees.reserve(nSees);
    for (int32_t i = 0; i < nSees; ++i) g.sees.push_back(r.i32());
    g.name = r.pstr();
    g.displayName = r.pstr();
    g.regionDef = r.pstr();
    g.minimapGraphic = r.pstr();
    g.onWorldMap = r.u8();
    g.creatureGen = r.u8();
    g.soundThemes = r.u8();
    r.raw(g.minimapScale, 4);
    g.mmOffX = r.i32();
    g.mmOffY = r.i32();
    g.wmOffX = r.i32();
    g.wmOffY = r.i32();
    const int32_t nExits = r.i32();
    if (nExits < 0 || nExits > 0x10000)
        throw std::runtime_error("bwd: region exit count out of range (layout drift)");
    g.exits.reserve(nExits);
    for (int32_t i = 0; i < nExits; ++i) {
        RegionExit e;
        e.name = r.pstr();
        r.raw(e.vec, 8);
        g.exits.push_back(std::move(e));
    }
    return g;
}

void writeRegion(Writer& w, const Region& g) {
    w.i32(static_cast<int32_t>(g.contains.size()));
    w.i32(static_cast<int32_t>(g.sees.size()));
    for (int32_t v : g.contains) w.i32(v);
    for (int32_t v : g.sees) w.i32(v);
    w.pstr(g.name);
    w.pstr(g.displayName);
    w.pstr(g.regionDef);
    w.pstr(g.minimapGraphic);
    w.u8(g.onWorldMap);
    w.u8(g.creatureGen);
    w.u8(g.soundThemes);
    w.raw(g.minimapScale, 4);
    w.i32(g.mmOffX);
    w.i32(g.mmOffY);
    w.i32(g.wmOffX);
    w.i32(g.wmOffY);
    w.i32(static_cast<int32_t>(g.exits.size()));
    for (const RegionExit& e : g.exits) {
        w.pstr(e.name);
        w.raw(e.vec, 8);
    }
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

} // namespace

File File::parse(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("bwd: cannot open " + path.string());
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    return parse(data);
}

File File::parse(const std::vector<uint8_t>& data) {
    Reader r(data);
    File f;
    const uint32_t mapCount = r.u32();
    if (mapCount == 0 || mapCount > 0x100000)
        throw std::runtime_error("bwd: implausible map count " +
                                 std::to_string(mapCount));
    f.maps_.reserve(mapCount - 1);
    for (uint32_t i = 1; i < mapCount; ++i) f.maps_.push_back(parseMap(r));
    const uint32_t regionCount = r.u32();
    if (regionCount == 0 || regionCount > 0x100000)
        throw std::runtime_error("bwd: implausible region count " +
                                 std::to_string(regionCount));
    f.regions_.reserve(regionCount - 1);
    for (uint32_t i = 1; i < regionCount; ++i) f.regions_.push_back(parseRegion(r));
    if (r.offset() != r.size())
        throw std::runtime_error("bwd: trailing bytes: consumed " +
                                 std::to_string(r.offset()) + " of " +
                                 std::to_string(r.size()));
    return f;
}

std::vector<uint8_t> File::serialize() const {
    Writer w;
    w.u32(static_cast<uint32_t>(maps_.size()) + 1);
    for (const MapInfo& m : maps_) writeMap(w, m);
    w.u32(static_cast<uint32_t>(regions_.size()) + 1);
    for (const Region& g : regions_) writeRegion(w, g);
    return w.take();
}

void File::write(const std::filesystem::path& path) const {
    const std::vector<uint8_t> data = serialize();
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("bwd: cannot write " + path.string());
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
}

const MapInfo* File::findMap(std::string_view key) const {
    const std::string needle = lower(std::string(key));
    for (const MapInfo& m : maps_) {
        if (lower(m.scriptName) == needle || lower(m.levelName) == needle) return &m;
    }
    return nullptr;
}

Region* File::findRegion(std::string_view name) {
    const std::string needle = lower(std::string(name));
    for (Region& g : regions_)
        if (lower(g.name) == needle) return &g;
    return nullptr;
}

const Region* File::findRegion(std::string_view name) const {
    const std::string needle = lower(std::string(name));
    for (const Region& g : regions_)
        if (lower(g.name) == needle) return &g;
    return nullptr;
}

uint64_t File::maxMapUid() const {
    uint64_t m = 0;
    for (const MapInfo& map : maps_) m = std::max(m, map.mapUid);
    return m;
}

OwnershipAudit File::auditOwnership() const {
    OwnershipAudit audit;
    audit.ownerCounts.assign(maps_.size() + 1, 0);
    for (size_t region = 0; region < regions_.size(); ++region) {
        for (const int32_t slot : regions_[region].contains) {
            if (slot < 1 || static_cast<size_t>(slot) > maps_.size()) {
                audit.invalidReferences.emplace_back(region + 1, slot);
            } else {
                ++audit.ownerCounts[static_cast<size_t>(slot)];
            }
        }
    }
    return audit;
}

int File::addMap(MapInfo map) {
    maps_.push_back(std::move(map));
    return static_cast<int>(maps_.size()); // 1-based slot
}

int File::addRegion(Region region) {
    regions_.push_back(std::move(region));
    return static_cast<int>(regions_.size()); // 1-based slot
}

AssignedSlots File::addLevel(const NewLevel& spec) {
    if (spec.levelName.empty())
        throw std::runtime_error("bwd: addLevel requires a levelName");
    if (findMap(spec.levelName))
        throw std::runtime_error("bwd: a map named '" + spec.levelName +
                                 "' already exists");

    MapInfo m;
    m.levelName = "Data\\Levels\\FinalAlbion\\" + spec.levelName + ".lev";
    m.scriptName = spec.levelName;
    m.used = 1;
    m.loadedOnProximity = spec.loadedOnProximity ? 1 : 0;
    m.isSea = spec.isSea ? 1 : 0;
    m.left = spec.left;
    m.right = spec.right;
    m.top = spec.top;
    m.bottom = spec.bottom;
    m.flag2 = 1;
    m.mapUid = spec.mapUid ? spec.mapUid : (maxMapUid() + 1);

    AssignedSlots out;
    out.mapSlot = addMap(std::move(m));

    Region g;
    g.contains.push_back(out.mapSlot);
    g.sees.push_back(out.mapSlot);
    g.name = spec.regionName.empty() ? spec.levelName : spec.regionName;
    g.displayName = spec.regionDisplayName.empty() ? g.name : spec.regionDisplayName;
    g.regionDef = spec.regionDef;      // may be empty (matches loaded ForgeTest)
    g.minimapGraphic = spec.minimapGraphic;
    g.onWorldMap = 0;
    g.creatureGen = 1;
    g.soundThemes = 1;
    // minimapScale already defaults to 1.0f; offsets/exits default empty.
    out.regionSlot = addRegion(std::move(g));

    if (!spec.alsoContainInRegion.empty()) {
        Region* host = findRegion(spec.alsoContainInRegion);
        if (!host)
            throw std::runtime_error("bwd: alsoContainInRegion '" +
                                     spec.alsoContainInRegion + "' not found");
        if (std::find(host->contains.begin(), host->contains.end(), out.mapSlot) ==
            host->contains.end())
            host->contains.push_back(out.mapSlot);
    }

    return out;
}

File compileFromWld(const forge::wld::File& wld, const DimSource& dims) {
    File f;

    // Maps: place by 1-based NewMap index (retail indices are contiguous 1..N
    // and slot-ordered). Reject gaps/dups so a malformed .wld fails loudly.
    const size_t n = wld.maps().size();
    std::vector<const forge::wld::Map*> byIndex(n, nullptr);
    for (const auto& m : wld.maps()) {
        if (m.index < 1 || static_cast<size_t>(m.index) > n)
            throw std::runtime_error("bwd compile: map index " +
                                     std::to_string(m.index) + " out of range");
        if (byIndex[m.index - 1])
            throw std::runtime_error("bwd compile: duplicate map index " +
                                     std::to_string(m.index));
        byIndex[m.index - 1] = &m;
    }

    std::map<std::string, int> slotOf; // lower(wld LevelName) -> 1-based slot
    for (size_t i = 0; i < n; ++i) {
        const forge::wld::Map* m = byIndex[i];
        if (!m)
            throw std::runtime_error("bwd compile: missing map slot " +
                                     std::to_string(i + 1));
        MapInfo mi;
        mi.levelName = "Data\\Levels\\" + m->levelName;
        mi.scriptName = m->levelScriptName;
        mi.used = 1;
        mi.flag2 = 1;
        mi.loadedOnProximity = m->loadedOnPlayerProximity ? 1 : 0;
        mi.isSea = m->isSea ? 1 : 0;
        mi.left = m->mapX;
        mi.top = m->mapY;
        int w = 0, h = 0;
        if (!dims(mi.levelName, w, h))
            throw std::runtime_error("bwd compile: no dimensions for " +
                                     mi.levelName);
        mi.right = m->mapX + w;
        mi.bottom = m->mapY + h;
        mi.mapUid = m->mapUid;
        f.addMap(std::move(mi));
        if (!slotOf.emplace(lower(m->levelName), static_cast<int>(i + 1)).second)
            throw std::runtime_error("bwd compile: duplicate level name " +
                                     m->levelName +
                                     " (region references would be ambiguous)");
    }

    // Regions: same slot-ordered placement; resolve contains/sees names->slots.
    const size_t rn = wld.regions().size();
    std::vector<const forge::wld::Region*> rByIndex(rn, nullptr);
    for (const auto& g : wld.regions()) {
        if (g.index < 1 || static_cast<size_t>(g.index) > rn)
            throw std::runtime_error("bwd compile: region index " +
                                     std::to_string(g.index) + " out of range");
        if (rByIndex[g.index - 1])
            throw std::runtime_error("bwd compile: duplicate region index " +
                                     std::to_string(g.index));
        rByIndex[g.index - 1] = &g;
    }
    auto resolve = [&](const std::string& levelName) -> int {
        auto it = slotOf.find(lower(levelName));
        if (it == slotOf.end())
            throw std::runtime_error("bwd compile: region references unknown map " +
                                     levelName);
        return it->second;
    };
    for (size_t i = 0; i < rn; ++i) {
        const forge::wld::Region* g = rByIndex[i];
        if (!g)
            throw std::runtime_error("bwd compile: missing region slot " +
                                     std::to_string(i + 1));
        Region rg;
        for (const auto& name : g->containsMaps) rg.contains.push_back(resolve(name));
        for (const auto& name : g->seesMaps) rg.sees.push_back(resolve(name));
        rg.name = g->regionName;
        rg.displayName = g->displayName;
        rg.regionDef = g->regionDef;
        rg.minimapGraphic = g->minimapGraphic;
        rg.onWorldMap = g->appearOnWorldMap ? 1 : 0;
        rg.creatureGen = 1;
        rg.soundThemes = 1;
        const float scale = g->minimapScale;
        std::memcpy(rg.minimapScale, &scale, 4);
        rg.mmOffX = static_cast<int32_t>(g->miniMapOffsetX);
        rg.mmOffY = static_cast<int32_t>(g->miniMapOffsetY);
        rg.wmOffX = static_cast<int32_t>(g->worldMapOffsetX);
        rg.wmOffY = static_cast<int32_t>(g->worldMapOffsetY);
        for (const auto& e : g->exitTextOffsets) {
            RegionExit re;
            re.name = e.name;
            const float x = e.x, y = e.y;
            std::memcpy(re.vec, &x, 4);
            std::memcpy(re.vec + 4, &y, 4);
            rg.exits.push_back(std::move(re));
        }
        f.addRegion(std::move(rg));
    }

    return f;
}

} // namespace forge::bwd
