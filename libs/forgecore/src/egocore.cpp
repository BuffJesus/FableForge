#include "forge/egocore.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>

#include "forge/bin.hpp"
#include "forge/defdecode.hpp"
#include "forge/defschema.hpp"

namespace forge::egocore {

namespace fs = std::filesystem;

namespace {

std::string readText(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + p.string());
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void writeText(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write " + p.string());
    out << s;
}

// Comments (// and /* */) blanked so a "#definition" inside one is not a block start.
std::string maskComments(const std::string& s) {
    std::string out = s;
    for (size_t i = 0; i + 1 < out.size();) {
        if (out[i] == '/' && out[i + 1] == '/') {
            while (i < out.size() && out[i] != '\n') out[i++] = ' ';
        } else if (out[i] == '/' && out[i + 1] == '*') {
            out[i] = out[i + 1] = ' '; i += 2;
            while (i + 1 < out.size() && !(out[i] == '*' && out[i + 1] == '/')) { if (out[i] != '\n') out[i] = ' '; ++i; }
            if (i + 1 < out.size()) { out[i] = out[i + 1] = ' '; i += 2; }
        } else ++i;
    }
    return out;
}

struct Block { size_t start = 0, end = 0; std::string type, name; };   // [start, end) in the text

std::vector<Block> blocks(const std::string& masked) {
    std::vector<Block> out;
    size_t cursor = 0;
    while (true) {
        const size_t s = masked.find("#definition", cursor);
        if (s == std::string::npos) break;
        const size_t e = masked.find("#end_definition", s);
        if (e == std::string::npos) break;
        Block b; b.start = s; b.end = e + 15;
        const size_t eol = masked.find('\n', s);
        std::string header = masked.substr(s, eol == std::string::npos ? std::string::npos : eol - s);
        header.erase(std::remove(header.begin(), header.end(), '\r'), header.end());
        std::istringstream ss(header);
        std::string kw; ss >> kw >> b.type >> b.name;
        out.push_back(b);
        cursor = b.end;
    }
    return out;
}

bool sameLine(const std::string& lineHeader, const std::string& type, const std::string& name) {
    std::istringstream ss(lineHeader);
    std::string kw, t, n; ss >> kw >> t >> n;
    return t == type && n == name;
}

bool runDefc(const fs::path& defc, const fs::path& in, const fs::path& out, std::string& log) {
    fs::create_directories(out);
    const fs::path logFile = out / "defc.log";
    const std::string cmd = "\"\"" + defc.string() + "\" -i \"" + in.string() + "\" -o \"" + out.string() + "\" > \"" + logFile.string() + "\" 2>&1\"";
    const int rc = std::system(cmd.c_str());
    std::error_code ec;
    if (fs::exists(logFile, ec)) log = readText(logFile);
    return rc == 0 && fs::exists(out / "game.bin", ec) && fs::exists(out / "names.bin", ec);
}

void copyTree(const fs::path& from, const fs::path& to, const std::vector<std::string>& skipTop = {}) {
    std::error_code ec;
    fs::create_directories(to, ec);
    for (const auto& de : fs::recursive_directory_iterator(from, ec)) {
        if (!de.is_regular_file(ec)) continue;
        const fs::path rel = fs::relative(de.path(), from, ec);
        bool skip = false;
        for (const auto& s : skipTop) { std::string first = rel.begin()->string(); std::transform(first.begin(), first.end(), first.begin(), ::tolower); if (first == s) skip = true; }
        if (skip) continue;
        fs::create_directories((to / rel).parent_path(), ec);
        fs::copy_file(de.path(), to / rel, fs::copy_options::overwrite_existing, ec);
    }
}

} // namespace

std::string mergeDefText(const std::string& target, const std::string& mod, Report& report, std::vector<std::string>* addedBlocks) {
    std::string out = target;
    const std::string maskedMod = maskComments(mod);
    for (const Block& mb : blocks(maskedMod)) {
        const std::string modBlock = mod.substr(mb.start, mb.end - mb.start);
        const std::string maskedOut = maskComments(out);
        bool replaced = false;
        for (const Block& tb : blocks(maskedOut)) {
            if (tb.type == mb.type && tb.name == mb.name) {
                out.replace(tb.start, tb.end - tb.start, modBlock);
                ++report.blocksReplaced; replaced = true;
                break;
            }
        }
        if (!replaced) { out += "\n\n" + modBlock; ++report.blocksAdded; if (addedBlocks) addedBlocks->push_back(modBlock); }
    }
    return out;
}

std::string mergeDefText(const std::string& target, const std::string& mod, Report& report) {
    return mergeDefText(target, mod, report, nullptr);
}

fs::path findDefc(const Paths& paths) {
    std::error_code ec;
    if (!paths.defc.empty() && fs::exists(paths.defc, ec)) return paths.defc;
    if (const char* env = std::getenv("FORGE_DEFC"); env && *env && fs::exists(env, ec)) return env;
    return "defc.exe";   // PATH
}

bool normaliseDefs(const fs::path& modFolder, const fs::path& gameRoot, const fs::path& outRoot, const Paths& paths, Report& report) {
    std::error_code ec;
    report.modName = modFolder.filename().string();
    fs::path modDefs = modFolder / "Data" / "Defs";
    if (!fs::is_directory(modDefs, ec)) modDefs = modFolder / "data" / "Defs";
    if (!fs::is_directory(modDefs, ec)) { report.notes.push_back("no Data/Defs in the mod folder"); return false; }
    fs::path text = paths.defsText;
    if (text.empty()) if (const char* env = std::getenv("FORGE_DEFS_TEXT"); env && *env) text = env;
    if (text.empty()) text = gameRoot / "Data" / "Defs";
    if (!fs::is_directory(text, ec) || !fs::exists(text / "RetailHeaders", ec) && !fs::exists(text / "DevHeaders", ec) && fs::directory_iterator(text, ec) == fs::directory_iterator()) {
        report.notes.push_back("no text Data/Defs tree (set FORGE_DEFS_TEXT or put the retail text defs under <game root>/Data/Defs); the .def overrides cannot be compiled");
        return false;
    }
    const fs::path defc = findDefc(paths);
    // the overlay: a copy of the tree with the mod's blocks merged in
    const fs::path work = fs::temp_directory_path() / "forge_egocore" / report.modName;
    fs::remove_all(work, ec);
    // Both trees carry the mod's ADDED definitions (so the two compiles share one index space
    // and an added record shifts nothing between them); only the mod tree carries the
    // replacements. The base compile then differs from the mod compile exactly where the
    // mod changed a field.
    const fs::path treeBase = work / "base", treeMod = work / "mod";
    copyTree(text, treeMod);
    copyTree(text, treeBase);
    std::set<std::string> addedNames;   // the definitions the mod adds (by name): the only "new records"
    auto noteAdded = [&](const std::string& blockText) {
        for (const Block& b : blocks(maskComments(blockText))) addedNames.insert(b.name);
    };
    for (const auto& de : fs::recursive_directory_iterator(modDefs, ec)) {
        if (!de.is_regular_file(ec)) continue;
        const fs::path rel = fs::relative(de.path(), modDefs, ec);
        std::string ext = de.path().extension().string(); std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        const fs::path target = treeMod / rel, targetBase = treeBase / rel;
        if (ext == ".def" && fs::exists(target, ec)) {
            std::vector<std::string> added;
            const std::string modText = readText(de.path());
            writeText(target, mergeDefText(readText(target), modText, report, &added));
            if (!added.empty()) {
                std::string baseText = readText(targetBase);
                for (const auto& b : added) { baseText += "\n\n" + b; noteAdded(b); }
                writeText(targetBase, baseText);
            }
        } else {
            if (ext == ".def") noteAdded(readText(de.path()));
            fs::create_directories(target.parent_path(), ec);
            fs::copy_file(de.path(), target, fs::copy_options::overwrite_existing, ec);
            fs::create_directories(targetBase.parent_path(), ec);
            fs::copy_file(de.path(), targetBase, fs::copy_options::overwrite_existing, ec);   // a whole new file: added everywhere
            if (ext == ".def") ++report.blocksAdded;
        }
        if (ext == ".def") ++report.defFiles;
    }
    // compile both
    std::string logBase, logMod;
    const fs::path outBase = work / "out_base", outMod = work / "out_mod";
    if (!runDefc(defc, treeBase, outBase, logBase)) { report.notes.push_back("defc failed on the retail text tree (" + defc.string() + "): " + logBase.substr(0, 400)); return false; }
    if (!runDefc(defc, treeMod, outMod, logMod)) { report.notes.push_back("defc failed with the mod's overrides: " + logMod.substr(0, 600)); return false; }
    // the diff, field by field, onto retail
    auto compiledBase = bin::File::open(outBase / "names.bin", outBase / "game.bin");
    auto compiledMod = bin::File::open(outMod / "names.bin", outMod / "game.bin");
    auto retail = bin::File::open(gameRoot / "data" / "CompiledDefs" / "names.bin", gameRoot / "data" / "CompiledDefs" / "game.bin");
    std::optional<defschema::Schema> schema;
    if (!paths.schema.empty() && fs::exists(paths.schema, ec)) schema = defschema::Schema::load(paths.schema);
    std::map<std::string, size_t> retailIndex;
    for (size_t i = 0; i < retail.entries().size(); ++i) if (!retail.entries()[i].name.empty()) retailIndex[retail.entries()[i].name] = i;
    // Def references travel as entry indices (some typed CDefIndex, many plain int32 like
    // GroupDef / MaterialDef), and the overlay's added records shift every index after them.
    // Two 4-byte values that resolve to the same entry NAME in their own compile are the same
    // reference; a changed reference lands in retail as retail's index of that name.
    auto nameAt = [](const bin::File& f, uint32_t idx) -> std::string {
        return idx < f.entries().size() ? f.entries()[idx].name : std::string();
    };
    auto u32 = [](const std::vector<uint8_t>& v) { uint32_t x = 0; std::memcpy(&x, v.data(), 4); return x; };
    auto isIntField = [](const defdecode::DecodedField& f) {
        return f.value.size() == 4 && (f.type == "int32" || f.type == "uint32" || f.type == "CDefIndex" || f.type == "J" || f.type == "?" || f.type.empty());
    };
    // records the mod adds: in retail with their compiled payload, 4-byte references re-pointed
    // by name into retail's index space (composite fields keep the compiled values; reported)
    std::vector<std::string> newNames;
    for (const auto& m : compiledMod.entries()) {
        if (m.name.empty() || retailIndex.count(m.name) || !addedNames.count(m.name)) continue;
        retailIndex[m.name] = retail.addEntry(m.definition, m.name, m.data);
        newNames.push_back(m.name);
        ++report.recordsNew;
    }
    for (const auto& nn : newNames) {
        const bin::Entry* m = compiledMod.find(nn);
        if (!m || !schema) continue;
        const auto* def = defdecode::resolveType(*schema, m->definition, m->data);
        if (!def) { report.notes.push_back(nn + ": new record kept as compiled (no schema type; references inside it are in the compiler's index space)"); continue; }
        auto dm = defdecode::decode(m->data, *def);
        if (!dm.clean()) continue;
        size_t remapped = 0;
        for (auto& f : dm.fields) {
            if (!isIntField(f)) continue;
            const std::string target = nameAt(compiledMod, u32(f.value));
            if (target.empty()) continue;
            const auto tgt = retailIndex.find(target);
            if (tgt == retailIndex.end()) continue;
            const uint32_t ri = uint32_t(tgt->second);
            std::vector<uint8_t> v(4); std::memcpy(v.data(), &ri, 4);
            if (v != f.value) { f.value = v; ++remapped; }
        }
        if (remapped) retail.setEntryData(retailIndex[nn], defdecode::encode(dm));
    }
    for (const auto& m : compiledMod.entries()) {
        if (m.name.empty()) continue;
        const bin::Entry* b = compiledBase.find(m.name);
        if (!b) continue;                      // added above
        if (b->data == m.data) continue;
        const auto rit = retailIndex.find(m.name);
        if (rit == retailIndex.end()) { ++report.recordsSkipped; report.notes.push_back(m.name + ": changed by the mod but not in retail's game.bin; skipped"); continue; }
        const bin::Entry& r = retail.entries()[rit->second];
        std::vector<uint8_t> payload;
        if (schema) {
            const auto* def = defdecode::resolveType(*schema, r.definition, r.data);
            if (def) {
                const auto dr = defdecode::decode(r.data, *def), db = defdecode::decode(b->data, *def), dm = defdecode::decode(m.data, *def);
                if (dr.clean() && db.clean() && dm.clean() && dr.fields.size() == db.fields.size() && db.fields.size() == dm.fields.size()) {
                    defdecode::Decoded merged = dr;
                    size_t changed = 0;
                    for (size_t f = 0; f < dm.fields.size(); ++f) {
                        if (dm.fields[f].value == db.fields[f].value) continue;
                        if (isIntField(dm.fields[f]) && isIntField(db.fields[f])) {
                            const std::string nb = nameAt(compiledBase, u32(db.fields[f].value)), nm = nameAt(compiledMod, u32(dm.fields[f].value));
                            if (!nb.empty() && nb == nm) continue;   // the same reference, shifted
                            if (!nm.empty()) {
                                const auto tgt = retailIndex.find(nm);
                                if (tgt != retailIndex.end()) {
                                    const uint32_t ri = uint32_t(tgt->second);
                                    std::vector<uint8_t> v(4); std::memcpy(v.data(), &ri, 4);
                                    merged.fields[f].value = v; ++changed;
                                    continue;
                                }
                            }
                        }
                        merged.fields[f].value = dm.fields[f].value; ++changed;
                    }
                    if (changed) { payload = defdecode::encode(merged); report.fieldsApplied += changed; }
                    else continue;   // only the header or shifted references differed
                }
            }
        }
        if (payload.empty()) {
            // no schema, or an opaque type: the whole compiled payload replaces the record when the sizes agree
            if (m.data.size() == r.data.size()) payload = m.data;
            else { ++report.recordsSkipped; report.notes.push_back(m.name + ": changed by the mod but not field-decodable and a different size; skipped"); continue; }
        }
        retail.setEntryData(rit->second, payload);
        ++report.recordsChanged;
    }
    const fs::path defsOut = outRoot / "data" / "CompiledDefs";
    fs::create_directories(defsOut, ec);
    retail.save(defsOut / "names.bin", defsOut / "game.bin");
    for (const char* sib : {"script.bin", "frontend.bin"})
        if (fs::exists(gameRoot / "data" / "CompiledDefs" / sib, ec))
            fs::copy_file(gameRoot / "data" / "CompiledDefs" / sib, defsOut / sib, fs::copy_options::overwrite_existing, ec);
    return true;
}

void installDll(const fs::path& modFolder, const fs::path& gameRoot, const fs::path& outRoot, Report& report) {
    std::error_code ec;
    const std::string name = modFolder.filename().string();
    report.modName = name;
    report.hasDll = fs::exists(modFolder / (name + ".dll"), ec);
    copyTree(modFolder, outRoot / "Mods" / name, {"data"});
    // Mods.ini: keep every existing line, ensure the FSE core line, add ours once
    std::vector<std::string> lines;
    fs::path ini = gameRoot / "Mods.ini";
    if (!fs::exists(ini, ec)) ini = gameRoot / "mods.ini";
    if (fs::exists(outRoot / "Mods.ini", ec)) ini = outRoot / "Mods.ini";   // earlier packs of the same build
    if (fs::exists(ini, ec)) { std::istringstream in(readText(ini)); std::string l; while (std::getline(in, l)) { if (!l.empty() && l.back() == '\r') l.pop_back(); lines.push_back(l); } }
    const std::string ours = name + "\\" + name + ".dll=1";
    bool haveSection = false, haveOurs = false, haveCore = false;
    for (const auto& l : lines) {
        if (l.rfind("[Mods]", 0) == 0) haveSection = true;
        if (l.rfind(name + "\\", 0) == 0) haveOurs = true;
        if (l.rfind("FableScriptExtender.dll", 0) == 0) haveCore = true;
    }
    if (!haveSection) lines.push_back("[Mods]");
    auto it = std::find_if(lines.begin(), lines.end(), [](const std::string& l) { return l.rfind("[Mods]", 0) == 0; });
    if (!haveCore) it = lines.insert(it + 1, "FableScriptExtender.dll=1");
    if (!haveOurs) {
        // after the last .dll line of the section
        auto pos = it + 1;
        while (pos != lines.end() && !(pos->size() && (*pos)[0] == '[') ) ++pos;
        if (report.hasDll) lines.insert(pos, ours);
    }
    std::string text;
    for (const auto& l : lines) text += l + "\n";
    writeText(outRoot / "Mods.ini", text);
}

} // namespace forge::egocore
