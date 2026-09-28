#include "forge/worldmerge.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <set>

namespace forge::worldmerge {

namespace {

std::string lower(std::string v) {
    for (auto& c : v) c = char(std::tolower(static_cast<unsigned char>(c)));
    return v;
}

std::string stem(const std::string& levelName) {
    const size_t s = levelName.find_last_of("\\/");
    std::string leaf = s == std::string::npos ? levelName : levelName.substr(s + 1);
    const size_t dot = leaf.rfind('.');
    return dot == std::string::npos ? leaf : leaf.substr(0, dot);
}

// a region's contains / sees as level names (lower-cased keys, original spelling kept)
struct NameList {
    std::vector<std::string> names;   // original spelling, in order
    bool has(const std::string& n) const {
        const std::string k = lower(n);
        return std::any_of(names.begin(), names.end(), [&](const std::string& x) { return lower(x) == k; });
    }
    void add(const std::string& n) { if (!has(n)) names.push_back(n); }
    void remove(const std::string& n) {
        const std::string k = lower(n);
        names.erase(std::remove_if(names.begin(), names.end(), [&](const std::string& x) { return lower(x) == k; }), names.end());
    }
};

NameList namesOf(const bwd::File& f, const std::vector<int32_t>& slots) {
    NameList l;
    for (const int32_t s : slots)
        if (s >= 1 && size_t(s) <= f.maps().size()) l.names.push_back(f.maps()[size_t(s - 1)].levelName);
    return l;
}

// one merged region: the record (lists empty) + its lists by name
struct MRegion {
    bwd::Region rec;
    NameList contains, sees;
};

// field-by-field owner tracking: "<record>|<field>" -> layer
struct Owners {
    std::map<std::string, std::string> by;
    void set(Report& rep, const std::string& key, const std::string& layer, bool differsFromCurrent) {
        auto it = by.find(key);
        if (it != by.end() && it->second != layer && differsFromCurrent)
            rep.conflicts.push_back(key.substr(0, key.find('|')) + " " + key.substr(key.find('|') + 1) + ": " + it->second + " -> " + layer + " (the later wins)");
        by[key] = layer;
    }
};

bool sameBox(const bwd::MapInfo& a, const bwd::MapInfo& b) {
    return a.left == b.left && a.right == b.right && a.top == b.top && a.bottom == b.bottom;
}

// map fields other than the level name; each a (name, equal, copy) triple
template <class F> void forMapFields(F&& f) {
    f("box", [](const bwd::MapInfo& a, const bwd::MapInfo& b) { return sameBox(a, b); },
      [](bwd::MapInfo& d, const bwd::MapInfo& s) { d.left = s.left; d.right = s.right; d.top = s.top; d.bottom = s.bottom; });
    f("script name", [](const bwd::MapInfo& a, const bwd::MapInfo& b) { return a.scriptName == b.scriptName; },
      [](bwd::MapInfo& d, const bwd::MapInfo& s) { d.scriptName = s.scriptName; });
    f("flags", [](const bwd::MapInfo& a, const bwd::MapInfo& b) { return a.used == b.used && a.loadedOnProximity == b.loadedOnProximity && a.isSea == b.isSea && a.flag2 == b.flag2; },
      [](bwd::MapInfo& d, const bwd::MapInfo& s) { d.used = s.used; d.loadedOnProximity = s.loadedOnProximity; d.isSea = s.isSea; d.flag2 = s.flag2; });
    f("uid", [](const bwd::MapInfo& a, const bwd::MapInfo& b) { return a.mapUid == b.mapUid; },
      [](bwd::MapInfo& d, const bwd::MapInfo& s) { d.mapUid = s.mapUid; });
}

template <class F> void forRegionFields(F&& f) {
    f("name", [](const bwd::Region& a, const bwd::Region& b) { return a.name == b.name; }, [](bwd::Region& d, const bwd::Region& s) { d.name = s.name; });
    f("display name", [](const bwd::Region& a, const bwd::Region& b) { return a.displayName == b.displayName; }, [](bwd::Region& d, const bwd::Region& s) { d.displayName = s.displayName; });
    f("def", [](const bwd::Region& a, const bwd::Region& b) { return a.regionDef == b.regionDef; }, [](bwd::Region& d, const bwd::Region& s) { d.regionDef = s.regionDef; });
    f("minimap", [](const bwd::Region& a, const bwd::Region& b) {
        return a.minimapGraphic == b.minimapGraphic && std::memcmp(a.minimapScale, b.minimapScale, 4) == 0 && a.mmOffX == b.mmOffX && a.mmOffY == b.mmOffY;
    }, [](bwd::Region& d, const bwd::Region& s) { d.minimapGraphic = s.minimapGraphic; std::memcpy(d.minimapScale, s.minimapScale, 4); d.mmOffX = s.mmOffX; d.mmOffY = s.mmOffY; });
    f("world map", [](const bwd::Region& a, const bwd::Region& b) { return a.onWorldMap == b.onWorldMap && a.wmOffX == b.wmOffX && a.wmOffY == b.wmOffY; },
      [](bwd::Region& d, const bwd::Region& s) { d.onWorldMap = s.onWorldMap; d.wmOffX = s.wmOffX; d.wmOffY = s.wmOffY; });
    f("flags", [](const bwd::Region& a, const bwd::Region& b) { return a.creatureGen == b.creatureGen && a.soundThemes == b.soundThemes; },
      [](bwd::Region& d, const bwd::Region& s) { d.creatureGen = s.creatureGen; d.soundThemes = s.soundThemes; });
    f("exit texts", [](const bwd::Region& a, const bwd::Region& b) {
        if (a.exits.size() != b.exits.size()) return false;
        for (size_t i = 0; i < a.exits.size(); ++i)
            if (a.exits[i].name != b.exits[i].name || std::memcmp(a.exits[i].vec, b.exits[i].vec, 8) != 0) return false;
        return true;
    }, [](bwd::Region& d, const bwd::Region& s) { d.exits = s.exits; });
}

} // namespace

std::string wldLevelName(const std::string& bwdLevelName) {
    const std::string pre = "data\\levels\\";
    if (lower(bwdLevelName.substr(0, pre.size())) == pre) return bwdLevelName.substr(pre.size());
    return bwdLevelName;
}

bwd::File merge(const bwd::File& base, const std::vector<Layer>& layers, Report& rep) {
    const size_t nb = base.maps().size(), nr = base.regions().size();
    std::vector<bwd::MapInfo> maps = base.maps();
    std::vector<MRegion> regions;
    for (const auto& r : base.regions()) {
        MRegion m; m.rec = r; m.rec.contains.clear(); m.rec.sees.clear();
        m.contains = namesOf(base, r.contains); m.sees = namesOf(base, r.sees);
        regions.push_back(std::move(m));
    }
    Owners owners;
    std::map<std::string, std::string> listOps;   // "<region>|<c|s>|<level>" -> "+layer" / "-layer"

    auto findMap = [&](const std::string& levelName) -> int {
        const std::string k = lower(levelName);
        for (size_t i = 0; i < maps.size(); ++i) if (lower(maps[i].levelName) == k) return int(i);
        return -1;
    };
    auto applyLists = [&](size_t ri, const NameList& was, const NameList& now, bool sees, const std::string& layer) {
        NameList& dst = sees ? regions[ri].sees : regions[ri].contains;
        const std::string rkey = "region " + regions[ri].rec.name;
        for (const auto& n : now.names)
            if (!was.has(n)) {
                const std::string k = rkey + (sees ? "|sees " : "|contains ") + stem(n);
                auto it = listOps.find(k);
                if (it != listOps.end() && it->second[0] == '-' && it->second.substr(1) != layer)
                    rep.conflicts.push_back(k.substr(0, k.find('|')) + " " + k.substr(k.find('|') + 1) + ": removed by " + it->second.substr(1) + ", added back by " + layer + " (the later wins)");
                listOps[k] = "+" + layer;
                dst.add(n);
            }
        for (const auto& n : was.names)
            if (!now.has(n)) {
                const std::string k = rkey + (sees ? "|sees " : "|contains ") + stem(n);
                auto it = listOps.find(k);
                if (it != listOps.end() && it->second[0] == '+' && it->second.substr(1) != layer)
                    rep.conflicts.push_back(k.substr(0, k.find('|')) + " " + k.substr(k.find('|') + 1) + ": added by " + it->second.substr(1) + ", removed by " + layer + " (the later wins)");
                listOps[k] = "-" + layer;
                dst.remove(n);
            }
    };

    for (const auto& L : layers) {
        const auto& mm = L.bwd.maps();
        const auto& mr = L.bwd.regions();
        if (mm.size() < nb) rep.notes.push_back(L.label + ": its FinalAlbion.bwd has " + std::to_string(mm.size()) + " maps, fewer than the base's " + std::to_string(nb) + " -- the missing base maps are kept");
        if (mr.size() < nr) rep.notes.push_back(L.label + ": its FinalAlbion.bwd has fewer regions than the base -- the missing base regions are kept");
        // base maps, by slot
        for (size_t i = 0; i < std::min(nb, mm.size()); ++i) {
            const auto& b = base.maps()[i];
            const auto& m = mm[i];
            const std::string rk = "map " + stem(b.levelName);
            if (lower(m.levelName) != lower(b.levelName)) {
                const bool differs = lower(maps[i].levelName) != lower(m.levelName);
                owners.set(rep, rk + "|level", L.label, differs);
                maps[i].levelName = m.levelName;
                rep.changed.push_back(rk + ": slot " + std::to_string(i + 1) + " now loads " + stem(m.levelName) + " (" + L.label + ")");
            }
            forMapFields([&](const char* field, auto eq, auto copy) {
                if (eq(b, m)) return;
                owners.set(rep, rk + "|" + field, L.label, !eq(maps[i], m));
                copy(maps[i], m);
                rep.changed.push_back(rk + ": " + field + " (" + L.label + ")");
            });
        }
        // appended maps, by level name
        for (size_t i = nb; i < mm.size(); ++i) {
            const auto& m = mm[i];
            const int at = findMap(m.levelName);
            if (at >= 0 && size_t(at) < nb) {
                rep.notes.push_back(L.label + ": appends " + stem(m.levelName) + ", which the base already has at slot " + std::to_string(at + 1) + " -- ignored");
                continue;
            }
            if (at >= 0) {   // an earlier mod appended the same level: field-wise, the later wins
                const std::string rk = "map " + stem(m.levelName);
                forMapFields([&](const char* field, auto eq, auto copy) {
                    if (eq(maps[size_t(at)], m)) return;
                    owners.set(rep, rk + "|" + field, L.label, true);
                    if (std::string(field) != "uid") copy(maps[size_t(at)], m);
                });
                continue;
            }
            bwd::MapInfo add = m;
            uint64_t maxUid = 0;
            bool clash = false;
            for (const auto& x : maps) { maxUid = std::max(maxUid, x.mapUid); if (x.mapUid == add.mapUid) clash = true; }
            if (clash) {
                rep.notes.push_back("map " + stem(m.levelName) + " (" + L.label + "): MapUID " + std::to_string(add.mapUid) + " is taken, given " + std::to_string(maxUid + 1));
                add.mapUid = maxUid + 1;
            }
            for (size_t j = 0; j < maps.size(); ++j) {
                const auto& x = maps[j];
                if (add.left < x.right && x.left < add.right && add.top < x.bottom && x.top < add.bottom)
                    rep.notes.push_back("map " + stem(m.levelName) + " (" + L.label + ") overlaps " + stem(x.levelName) + " on the world grid");
            }
            maps.push_back(add);
            owners.by["map " + stem(m.levelName) + "|added"] = L.label;
            rep.added.push_back("map " + stem(m.levelName) + " (slot " + std::to_string(maps.size()) + ") from " + L.label);
        }
        // base regions, by slot
        for (size_t i = 0; i < std::min(nr, mr.size()); ++i) {
            const auto& b = base.regions()[i];
            const auto& m = mr[i];
            const std::string rk = "region " + b.name;
            forRegionFields([&](const char* field, auto eq, auto copy) {
                if (eq(b, m)) return;
                owners.set(rep, rk + "|" + field, L.label, !eq(regions[i].rec, m));
                copy(regions[i].rec, m);
                rep.changed.push_back(rk + ": " + field + " (" + L.label + ")");
            });
            const NameList bc = namesOf(base, b.contains), mc = namesOf(L.bwd, m.contains);
            const NameList bs = namesOf(base, b.sees), ms = namesOf(L.bwd, m.sees);
            bool listChanged = false;
            for (const auto& n : mc.names) if (!bc.has(n)) listChanged = true;
            for (const auto& n : bc.names) if (!mc.has(n)) listChanged = true;
            for (const auto& n : ms.names) if (!bs.has(n)) listChanged = true;
            for (const auto& n : bs.names) if (!ms.has(n)) listChanged = true;
            if (!listChanged) continue;
            applyLists(i, bc, mc, false, L.label);
            applyLists(i, bs, ms, true, L.label);
            rep.changed.push_back(rk + ": maps (" + L.label + ")");
        }
        // appended regions, by name
        for (size_t i = nr; i < mr.size(); ++i) {
            const auto& m = mr[i];
            size_t at = regions.size();
            for (size_t j = 0; j < regions.size(); ++j) if (lower(regions[j].rec.name) == lower(m.name)) at = j;
            if (at < nr) { rep.notes.push_back(L.label + ": appends region " + m.name + ", which the base already has -- ignored"); continue; }
            MRegion add; add.rec = m; add.rec.contains.clear(); add.rec.sees.clear();
            add.contains = namesOf(L.bwd, m.contains); add.sees = namesOf(L.bwd, m.sees);
            if (at < regions.size()) {
                owners.set(rep, "region " + m.name + "|record", L.label, true);
                regions[at] = std::move(add);
                continue;
            }
            regions.push_back(std::move(add));
            rep.added.push_back("region " + m.name + " (slot " + std::to_string(regions.size()) + ") from " + L.label);
        }
    }

    // resolve the lists to the merged slots; a map owned twice keeps its last owner
    bwd::File out = base;
    out.maps() = maps;
    out.regions().clear();
    for (auto& r : regions) {
        bwd::Region rec = r.rec;
        for (const auto* list : {&r.contains, &r.sees}) {
            auto& dst = list == &r.contains ? rec.contains : rec.sees;
            for (const auto& n : list->names) {
                const int at = findMap(n);
                if (at < 0) { rep.notes.push_back("region " + rec.name + " names " + stem(n) + ", which no mod adds -- dropped"); continue; }
                dst.push_back(int32_t(at + 1));
            }
        }
        out.regions().push_back(std::move(rec));
    }
    const auto audit = out.auditOwnership();
    for (size_t s = 1; s < audit.ownerCounts.size(); ++s)
        if (audit.ownerCounts[s] > 1 && (s > nb || base.auditOwnership().ownerCounts[s] <= 1))
            rep.notes.push_back("map " + stem(maps[s - 1].levelName) + " is owned by " + std::to_string(audit.ownerCounts[s]) + " regions after the merge");
    return out;
}

wld::File mirror(const wld::File& baseWld, const bwd::File& baseBwd, const bwd::File& merged, Report& rep) {
    wld::File w = baseWld;
    const size_t nb = baseBwd.maps().size(), nr = baseBwd.regions().size();
    for (size_t i = 0; i < std::min(nb, merged.maps().size()); ++i) {
        const auto& b = baseBwd.maps()[i];
        const auto& m = merged.maps()[i];
        const std::string wn = wldLevelName(b.levelName);
        if (!w.findMap(wn)) { rep.notes.push_back("WLD: " + stem(b.levelName) + " is not in the base FinalAlbion.wld -- not mirrored"); continue; }
        if (lower(m.levelName) != lower(b.levelName)) rep.notes.push_back("WLD: slot " + std::to_string(i + 1) + "'s new level name is not mirrored (the BWD carries it)");
        if (m.left != b.left || m.top != b.top) w.relocateMap(wn, m.left, m.top);
        if (m.isSea != b.isSea || m.loadedOnProximity != b.loadedOnProximity) rep.notes.push_back("WLD: " + stem(b.levelName) + "'s flags are not mirrored (the BWD carries them)");
    }
    for (size_t i = nb; i < merged.maps().size(); ++i) {
        const auto& m = merged.maps()[i];
        wld::Map add;
        add.mapX = m.left; add.mapY = m.top;
        add.levelName = wldLevelName(m.levelName);
        add.levelScriptName = m.scriptName.empty() ? stem(m.levelName) : m.scriptName;
        add.mapUid = uint32_t(m.mapUid);
        add.isSea = m.isSea != 0; add.loadedOnPlayerProximity = m.loadedOnProximity != 0;
        try { w.addMap(add); }
        catch (const std::exception&) {
            add.mapUid = 0;   // the text's own UID space differs: next free
            try { w.addMap(add); } catch (const std::exception& e) { rep.notes.push_back(std::string("WLD: ") + e.what()); }
        }
    }
    auto names = [](const bwd::File& f, const std::vector<int32_t>& slots) {
        std::vector<std::string> v;
        for (const int32_t s : slots) if (s >= 1 && size_t(s) <= f.maps().size()) v.push_back(wldLevelName(f.maps()[size_t(s - 1)].levelName));
        return v;
    };
    auto has = [](const std::vector<std::string>& v, const std::string& n) {
        return std::any_of(v.begin(), v.end(), [&](const std::string& x) { return lower(x) == lower(n); });
    };
    for (size_t i = 0; i < std::min(nr, merged.regions().size()); ++i) {
        const auto& b = baseBwd.regions()[i];
        const auto& m = merged.regions()[i];
        std::string name = b.name;
        if (!w.findRegion(name)) { if (b.name != m.name || b.contains != m.contains || b.sees != m.sees) rep.notes.push_back("WLD: region " + b.name + " is not in the base FinalAlbion.wld -- not mirrored"); continue; }
        try {
            if (m.displayName != b.displayName) w.setRegionText(name, "NewDisplayName", m.displayName);
            if (m.regionDef != b.regionDef) w.setRegionText(name, "RegionDef", m.regionDef);
            if (m.minimapGraphic != b.minimapGraphic) w.setRegionText(name, "MiniMapGraphic", m.minimapGraphic);
            const auto bc = names(baseBwd, b.contains), mc = names(merged, m.contains);
            const auto bs = names(baseBwd, b.sees), ms = names(merged, m.sees);
            for (const auto& n : bc) if (!has(mc, n)) w.removeMapFromRegion(name, n, false);
            for (const auto& n : bs) if (!has(ms, n)) w.removeMapFromRegion(name, n, true);
            for (const auto& n : mc) if (!has(bc, n)) w.addMapToRegion(name, n, has(ms, n) && !has(bs, n));
            for (const auto& n : ms) if (!has(bs, n) && !has(mc, n)) rep.notes.push_back("WLD: region " + m.name + " seeing " + n + " is not mirrored (the BWD carries it)");
            if (m.name != b.name) w.setRegionText(name, "RegionName", m.name);
        } catch (const std::exception& e) { rep.notes.push_back(std::string("WLD: region ") + b.name + ": " + e.what()); }
    }
    for (size_t i = nr; i < merged.regions().size(); ++i) {
        const auto& m = merged.regions()[i];
        wld::Region add;
        add.regionName = m.name; add.displayName = m.displayName; add.regionDef = m.regionDef;
        add.appearOnWorldMap = m.onWorldMap != 0; add.minimapGraphic = m.minimapGraphic;
        std::memcpy(&add.minimapScale, m.minimapScale, 4);
        add.containsMaps = names(merged, m.contains);
        add.seesMaps = names(merged, m.sees);
        try { w.addRegion(add); } catch (const std::exception& e) { rep.notes.push_back(std::string("WLD: ") + e.what()); }
    }
    return w;
}

} // namespace forge::worldmerge
