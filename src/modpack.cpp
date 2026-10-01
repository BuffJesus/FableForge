#include "modpack.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <fstream>
#include <map>
#include <iterator>
#include <cstring>
#include <stdexcept>

#include "forge/stb.hpp"
#include "forge/wad.hpp"
#include "meshimport.hpp"
#include "pendingbanks.hpp"
#include "nlohmann/json.hpp"
#include "worldedit.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace albion::modpack {

bool isPack(const fs::path& folder) {
    std::error_code ec;
    return fs::is_regular_file(folder / kFileName, ec);
}

Pack load(const fs::path& folder) {
    std::ifstream in(folder / kFileName);
    if (!in) throw std::runtime_error("no " + (folder / kFileName).string());
    json j;
    try { in >> j; } catch (const std::exception& e) { throw std::runtime_error(std::string(kFileName) + ": " + e.what()); }
    Pack p;
    p.version = j.value("version", 1);
    p.name = j.value("name", folder.filename().string());
    for (const auto& m : j.value("models", json::array())) {
        ModelRecipe r;
        r.name = m.value("name", ""); r.model = m.value("model", ""); r.texture = m.value("texture", "");
        r.donor = m.value("donor", r.donor); r.collision = m.value("collision", true);
        p.models.push_back(std::move(r));
    }
    for (const auto& t : j.value("groundThemes", json::array())) {
        GroundThemeRecipe r;
        r.name = t.value("name", ""); r.png = t.value("png", ""); r.cliffPng = t.value("cliffPng", ""); r.donor = t.value("donor", r.donor);
        p.groundThemes.push_back(std::move(r));
    }
    for (const auto& row : j.value("lipSync", json::array())) {
        LipSyncRecipe recipe;
        recipe.language=row.at("language").get<std::string>();
        recipe.bank=row.at("bank").get<std::string>();
        recipe.soundId=row.at("soundId").get<uint32_t>();
        recipe.value.fps=row.at("fps").get<uint32_t>();
        recipe.value.durationBits=row.at("durationBits").get<uint32_t>();
        for(const auto& item:row.at("dictionary")) {
            const int id=item.at("id").get<int>();
            if(id<0 || id>255) throw std::runtime_error("lipSync dictionary ID out of range");
            recipe.value.dictionary.push_back({uint8_t(id),item.at("symbol").get<std::string>()});
        }
        for(const auto& frame:row.at("frames")) {
            forge::lipsync::Frame keys;
            for(const auto& item:frame) {
                if(!item.is_array() || item.size()!=2)
                    throw std::runtime_error("lipSync frame key must be [ID, weight]");
                const int id=item[0].get<int>(),weight=item[1].get<int>();
                if(id<0 || id>255 || weight<0 || weight>255)
                    throw std::runtime_error("lipSync frame key out of byte range");
                keys.push_back({uint8_t(id),uint8_t(weight)});
            }
            recipe.value.frames.push_back(std::move(keys));
        }
        p.lipSync.push_back(std::move(recipe));
    }
    for (const auto& r : j.value("requires", json::array())) if (r.is_string()) p.masters.push_back(r.get<std::string>());
    return p;
}

static void writeManifest(const fs::path& path, const Pack& pack) {
    json j;
    j["version"] = pack.version;
    j["name"] = pack.name;
    j["models"] = json::array();
    for (const auto& m : pack.models)
        j["models"].push_back({{"name", m.name}, {"model", m.model}, {"texture", m.texture}, {"donor", m.donor}, {"collision", m.collision}});
    j["groundThemes"] = json::array();
    for (const auto& t : pack.groundThemes)
        j["groundThemes"].push_back({{"name", t.name}, {"png", t.png}, {"cliffPng", t.cliffPng}, {"donor", t.donor}});
    j["lipSync"]=json::array();
    for(const auto& recipe:pack.lipSync) {
        json dictionary=json::array(),frames=json::array();
        for(const auto& viseme:recipe.value.dictionary)
            dictionary.push_back({{"id",viseme.id},{"symbol",viseme.symbol}});
        for(const auto& frame:recipe.value.frames) {
            json keys=json::array();
            for(const auto& key:frame) keys.push_back(json::array({key.id,key.weight}));
            frames.push_back(std::move(keys));
        }
        j["lipSync"].push_back({{"language",recipe.language},{"bank",recipe.bank},
            {"soundId",recipe.soundId},{"fps",recipe.value.fps},
            {"durationBits",recipe.value.durationBits},
            {"dictionary",std::move(dictionary)},{"frames",std::move(frames)}});
    }
    if (!pack.masters.empty()) j["requires"] = pack.masters;
    std::ofstream out(path, std::ios::binary);
    out << j.dump(2);
    out.close();
    if (!out) throw std::runtime_error("cannot write pack manifest: " + path.string());
}

void save(const fs::path& folder, const Pack& pack) {
    detail::PendingBanks pending(folder, ".forge-pack-edit-");
    writeManifest(pending.prepare(kFileName), pack);
    std::string error;
    if (!pending.install(false, error)) throw std::runtime_error(error);
}

bool create(const fs::path& folder, const std::string& name, std::string& error) {
    try {
        std::error_code ec;
        if (isPack(folder)) { error = folder.string() + " is already a pack"; return false; }
        fs::create_directories(folder / "assets", ec);
        if (ec) { error = "cannot create " + folder.string() + ": " + ec.message(); return false; }
        Pack p;
        p.name = name.empty() ? folder.filename().string() : name;
        save(folder, p);
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

namespace {
// Each recipe and input role owns its files. Basename collisions must not alter
// another recipe (or turn a theme's base and cliff into the same image).
bool intoAssets(detail::PendingBanks& pending, const fs::path& relativeRoot, std::string& path, std::string& error,
                fs::path* prepared = nullptr) {
    if (path.empty()) return true;
    std::error_code ec;
    const fs::path src(path);
    if (!fs::is_regular_file(src, ec)) { error = "no such file: " + path; return false; }
    const fs::path relative = relativeRoot / src.filename();
    const fs::path dst = pending.prepare(relative);
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    if (ec) { error = "cannot copy " + path + ": " + ec.message(); return false; }
    path = relative.generic_string();
    if (prepared) *prepared = dst;
    return true;
}

bool stageGltfBuffers(detail::PendingBanks& pending, const fs::path& source, const fs::path& prepared,
                      const fs::path& modelFolder, std::string& error) {
    std::string extension = source.extension().string();
    for (auto& c : extension) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (extension != ".gltf") return true;
    json document;
    { std::ifstream in(prepared, std::ios::binary); in >> document; }
    if (!document.contains("buffers")) return true;
    auto& buffers = document.at("buffers");
    if (!buffers.is_array()) { error = "glTF buffers must be an array"; return false; }
    bool changed = false;
    for (size_t i = 0; i < buffers.size(); ++i) {
        auto& buffer = buffers[i];
        if (!buffer.contains("uri")) { error = "a .gltf buffer is missing its URI"; return false; }
        const auto uri = buffer.at("uri").get<std::string>();
        if (uri.starts_with("data:")) continue;
        std::string path = (source.parent_path() / uri).string();
        if (!intoAssets(pending, modelFolder / "buffers" / std::to_string(i), path, error)) return false;
        buffer["uri"] = fs::path(path).lexically_relative(modelFolder).generic_string();
        changed = true;
    }
    if (changed) {
        std::ofstream out(prepared, std::ios::binary);
        out << document.dump(2);
        out.close();
        if (!out) { error = "cannot write staged glTF buffer references"; return false; }
    }
    return true;
}
bool validName(const std::string& n) {
    if (n.empty() || n.size() > 96) return false;
    for (const char c : n) if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}
bool validLipBank(const std::string& language,const std::string& bank) {
    if(language.empty()) return false;
    std::string upper;
    for(unsigned char ch:language) {
        if(!std::isalpha(ch)) return false;
        upper.push_back(char(std::toupper(ch)));
    }
    for(const char* suffix:{"MAIN","MAIN_2","SCRIPT","SCRIPT_2"})
        if(bank=="LIPSYNC_"+upper+"_"+suffix) return true;
    return false;
}
} // namespace

std::vector<std::string> masterProblems(const Pack& pack, size_t self, const std::vector<OrderEntry>& order) {
    std::vector<std::string> out;
    auto lower = [](std::string v) { for (auto& c : v) c = char(std::tolower(static_cast<unsigned char>(c))); return v; };
    for (const auto& want : pack.masters) {
        size_t at = order.size();
        for (size_t i = 0; i < order.size(); ++i)
            if (i != self && (lower(order[i].name) == lower(want) || lower(order[i].packName) == lower(want))) { at = i; break; }
        if (at == order.size()) out.push_back("needs " + want + ", which is not in the order");
        else if (!order[at].enabled) out.push_back("needs " + want + ", which is disabled");
        else if (at > self) out.push_back("needs " + want + " loaded before it");
    }
    return out;
}

bool addModel(const fs::path& folder, ModelRecipe recipe, std::string& error) {
    try {
        if (!isPack(folder)) { error = folder.string() + " is not a FableForge pack"; return false; }
        if (!validName(recipe.name)) { error = "model name must be A-Z, 0-9 and _"; return false; }
        if (recipe.model.empty()) { error = "model path is missing"; return false; }
        Pack p = load(folder);
        detail::PendingBanks pending(folder, ".forge-pack-edit-");
        const fs::path assets = fs::path("assets") / "models" / recipe.name;
        const fs::path modelSource = recipe.model;
        fs::path preparedModel;
        if (!intoAssets(pending, assets / "model", recipe.model, error, &preparedModel) ||
            !stageGltfBuffers(pending, modelSource, preparedModel, assets / "model", error) ||
            !intoAssets(pending, assets / "texture", recipe.texture, error)) return false;
        std::erase_if(p.models, [&](const ModelRecipe& m) { return m.name == recipe.name; });
        p.models.push_back(std::move(recipe));
        writeManifest(pending.prepare(kFileName), p);
        return pending.install(false, error);
    } catch (const std::exception& e) { error = e.what(); return false; }
}

bool addGroundTheme(const fs::path& folder, GroundThemeRecipe recipe, std::string& error) {
    try {
        if (!isPack(folder)) { error = folder.string() + " is not a FableForge pack"; return false; }
        if (!validName(recipe.name)) { error = "theme name must be A-Z, 0-9 and _"; return false; }
        if (recipe.png.empty()) { error = "ground texture path is missing"; return false; }
        Pack p = load(folder);
        detail::PendingBanks pending(folder, ".forge-pack-edit-");
        const fs::path assets = fs::path("assets") / "themes" / recipe.name;
        if (!intoAssets(pending, assets / "base", recipe.png, error) ||
            !intoAssets(pending, assets / "cliff", recipe.cliffPng, error)) return false;
        std::erase_if(p.groundThemes, [&](const GroundThemeRecipe& t) { return t.name == recipe.name; });
        p.groundThemes.push_back(std::move(recipe));
        writeManifest(pending.prepare(kFileName), p);
        return pending.install(false, error);
    } catch (const std::exception& e) { error = e.what(); return false; }
}

bool addLipSync(const fs::path& folder,const std::string& language,
                std::span<const forge::lipsync::ArchiveEdit> edits,std::string& error) {
    try {
        if(!isPack(folder)) throw std::runtime_error(folder.string()+" is not a FableForge pack");
        if(edits.empty()) throw std::runtime_error("no lip sync edits staged");
        Pack pack=load(folder);
        for(const auto& edit:edits) {
            if(!validLipBank(language,edit.bankName) || !edit.soundId)
                throw std::runtime_error("lip sync language, bank or Sound ID is invalid");
            // Validate the recipe's binary representation before changing the manifest.
            const auto data=forge::lipsync::encode(edit.value);
            forge::lipsync::decode(data,forge::lipsync::encodeInfo(edit.value));
            std::erase_if(pack.lipSync,[&](const LipSyncRecipe& old) {
                return old.language==language && old.bank==edit.bankName &&
                       old.soundId==edit.soundId;
            });
            pack.lipSync.push_back({language,edit.bankName,edit.soundId,edit.value});
        }
        save(folder,pack);
        return true;
    } catch(const std::exception& ex) {error=ex.what();return false;}
}

ApplyReport apply(const fs::path& folder, const fs::path& baseRoot, const fs::path& outRoot,
                  const std::set<std::string>& skipLipLanguages) {
    ApplyReport rep;
    Pack p;
    try { p = load(folder); } catch (const std::exception& e) { rep.errors.push_back(e.what()); return rep; }
    // themes first: a model's texture never depends on them, and a theme is the cheaper step
    for (const auto& t : p.groundThemes) {
        editor::CustomThemeRequest req;
        req.png = folder / t.png; req.name = t.name; req.donor = t.donor;
        if (!t.cliffPng.empty()) req.cliffPng = folder / t.cliffPng;
        editor::CustomThemeResult out;
        std::string err;
        if (!editor::createCustomTheme(baseRoot, outRoot, req, out, err)) { rep.errors.push_back(t.name + ": " + err); continue; }
        rep.added.push_back(t.name + " (ENGINE_THEME def " + std::to_string(out.defIndex) + ", texture " + std::to_string(out.baseTexture) + ")");
        for (const auto& n : out.notes) rep.notes.push_back(n);
    }
    for (const auto& m : p.models) {
        meshimport::ImportRequest req;
        req.model = folder / m.model; req.name = m.name; req.donor = m.donor; req.collision = m.collision;
        if (!m.texture.empty()) req.texturePng = folder / m.texture;
        meshimport::ImportResult out;
        std::string err;
        if (!meshimport::importModel(baseRoot, outRoot, req, out, err)) { rep.errors.push_back(m.name + ": " + err); continue; }
        rep.added.push_back(out.objectName + " (mesh " + std::to_string(out.meshId) + ", def " + std::to_string(out.defIndex) + ")");
        for (const auto& n : out.notes) rep.notes.push_back(n);
    }
    std::map<std::string,std::vector<forge::lipsync::ArchiveEdit>> dialogue;
    std::set<std::string> reportedSkipped;
    for(const auto& recipe:p.lipSync) {
        if(skipLipLanguages.contains(recipe.language)) {
            if(reportedSkipped.insert(recipe.language).second)
                rep.notes.push_back(recipe.language+
                    " lip sync recipes skipped: a later whole-file dialogue.big wins");
            continue;
        }
        if(!validLipBank(recipe.language,recipe.bank) || !recipe.soundId) {
            rep.errors.push_back("invalid lip sync recipe language/bank/ID");
            continue;
        }
        dialogue[recipe.language].push_back({recipe.bank,recipe.soundId,recipe.value});
    }
    for(const auto& [language,edits]:dialogue) {
        const fs::path relative=fs::path("data")/"lang"/language/"dialogue.big";
        const fs::path output=outRoot/relative;
        const fs::path source=fs::exists(output)?output:baseRoot/relative;
        if(!fs::is_regular_file(source)) {
            rep.errors.push_back(language+": source dialogue.big is missing");
            continue;
        }
        fs::path temporary=output;
        temporary+=std::string(".recipe.")+std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        try {
            forge::lipsync::writeScratchArchive(source,temporary,edits);
            if(fs::exists(output)) {
                fs::copy_file(temporary,output,fs::copy_options::overwrite_existing);
                fs::remove(temporary);
            } else fs::rename(temporary,output);
            rep.added.push_back(language+" dialogue: "+std::to_string(edits.size())+
                                " lip sync line(s)");
        } catch(const std::exception& ex) {
            std::error_code ignored;
            fs::remove(temporary,ignored);
            rep.errors.push_back(language+": "+ex.what());
        }
    }
    return rep;
}

bool writeStaticMapChunk(const fs::path& stb, const std::string& mapName, const std::vector<uint8_t>& chunk,
                         const std::vector<uint8_t>& record, std::string& error) {
    try {
        auto lower = [](std::string v) { for (auto& c : v) c = char(std::tolower(static_cast<unsigned char>(c))); return v; };
        const auto archive = forge::stb::Archive::open(stb);
        const forge::stb::StaticMap* map = nullptr;
        const std::string want = lower(mapName) + ".lev";
        for (const auto& m : archive.staticMaps())
            if (lower(fs::path(m.levelName).filename().string()) == want) { map = &m; break; }
        if (!map) {   // a level the pack adds: its chunk + record are appended
            const std::string lev = "Data\\Levels\\FinalAlbion\\" + mapName + ".lev";
            const fs::path tmp = stb.string() + ".forge-tmp";
            forge::stb::appendStaticMaps(stb, tmp, {{lev, lev, chunk, record}});
            fs::rename(tmp, stb);
            return true;
        }
        const auto oldRecord = archive.readStaticMapRecord(*map);
        if (oldRecord.size() < 8 || record.size() != oldRecord.size()) { error = mapName + ": the record size differs from the STB's"; return false; }
        uint32_t bankIndex = 0; std::memcpy(&bankIndex, oldRecord.data() + 4, 4);
        const forge::stb::Entry* entry = nullptr;
        for (const auto& e : archive.entries()) if (e.id == bankIndex) { entry = &e; break; }
        if (!entry) { error = mapName + ": static-map bank entry not found"; return false; }
        if (chunk.size() == entry->size) {
            std::fstream io(stb, std::ios::binary | std::ios::in | std::ios::out);
            if (!io) { error = "cannot open " + stb.string(); return false; }
            io.seekp(std::streamoff(entry->offset));
            io.write(reinterpret_cast<const char*>(chunk.data()), std::streamsize(chunk.size()));
            // the record was baked against another STB: its pointers follow this one's slot
            const auto live = forge::stb::rebaseCommonRecord(record, *map, oldRecord);
            io.seekp(std::streamoff(map->absoluteOffset));
            io.write(reinterpret_cast<const char*>(live.data()), std::streamsize(live.size()));
            if (!io) { error = "write to " + stb.string() + " failed"; return false; }
        } else {
            std::vector<forge::stb::StaticMapAppend> batch;
            batch.push_back({map->levelName, entry->name, chunk, record});
            const fs::path tmp = stb.string() + ".forge-tmp";
            forge::stb::replaceStaticMapsRelayout(stb, tmp, batch);
            fs::rename(tmp, stb);
        }
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

StbReport applyStaticMaps(const fs::path& folder, const fs::path& baseRoot, const fs::path& outRoot) {
    StbReport rep;
    std::error_code ec;
    const fs::path dir = folder / "stb";
    if (!fs::is_directory(dir, ec)) return rep;
    const fs::path rel = fs::path("data") / "Levels" / "FinalAlbion_RT.stb";
    const fs::path outStb = outRoot / rel;
    for (const auto& de : fs::directory_iterator(dir, ec)) {
        if (!de.is_regular_file(ec) || de.path().extension() != ".chunk") continue;
        const std::string map = de.path().stem().string();
        const fs::path recPath = dir / (map + ".record");
        if (!fs::exists(recPath, ec)) { rep.errors.push_back(map + ": " + recPath.filename().string() + " is missing"); continue; }
        auto slurp = [](const fs::path& p) { std::ifstream f(p, std::ios::binary); return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {}); };
        if (!fs::exists(outStb, ec)) {   // the first layer to touch the STB starts from the base one
            fs::create_directories(outStb.parent_path(), ec);
            fs::copy_file(baseRoot / rel, outStb, fs::copy_options::overwrite_existing, ec);
            if (ec) { rep.errors.push_back("cannot copy the base FinalAlbion_RT.stb: " + ec.message()); return rep; }
        }
        std::string err;
        if (writeStaticMapChunk(outStb, map, slurp(de.path()), slurp(recPath), err)) rep.maps.push_back(map);
        else rep.errors.push_back(err);
    }
    return rep;
}

namespace {
class PackWorkspace {
    fs::path parent_, path_;
public:
    explicit PackWorkspace(const char* prefix) {
        parent_ = fs::absolute(fs::temp_directory_path() / "FableForge").lexically_normal();
        fs::create_directories(parent_);
        static std::atomic<uint64_t> serial{0};
        for (;;) {
            const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
            const auto candidate = parent_ / (std::string(prefix) + std::to_string(tick) + "-" + std::to_string(serial++));
            if (fs::create_directory(candidate)) { path_ = candidate; break; }
        }
    }
    PackWorkspace(const PackWorkspace&) = delete;
    PackWorkspace& operator=(const PackWorkspace&) = delete;
    ~PackWorkspace() {
        if (!path_.empty() && path_.parent_path() == parent_) {
            std::error_code error;
            fs::remove_all(path_, error);
        }
    }
    const fs::path& path() const { return path_; }
};

std::vector<uint8_t> slurpFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("cannot read " + p.string());
    std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(f), {});
    if (f.bad()) throw std::runtime_error("cannot finish reading " + p.string());
    return bytes;
}
void putFile(const fs::path& p, const std::vector<uint8_t>& b) {
    fs::create_directories(p.parent_path());
    std::ofstream o(p, std::ios::binary | std::ios::trunc);
    o.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
    o.close();
    if (!o) throw std::runtime_error("cannot write " + p.string());
}
std::string lowerS(std::string v) { for (auto& c : v) c = char(std::tolower(static_cast<unsigned char>(c))); return v; }
// level stem (lower) -> (chunk FNV-1a hash, record) of every static map in an STB; with
// `bytes`, the chunks of the maps named in `want` are kept too
struct MapBytes { uint64_t hash = 0; std::vector<uint8_t> record, chunk; };
std::map<std::string, MapBytes> staticMapsOf(const fs::path& stb, const std::vector<std::string>* want = nullptr) {
    std::map<std::string, MapBytes> out;
    const auto a = forge::stb::Archive::open(stb);
    std::map<uint32_t, const forge::stb::Entry*> byId;
    for (const auto& e : a.entries()) byId[e.id] = &e;
    for (const auto& m : a.staticMaps()) {
        const std::string key = lowerS(fs::path(m.levelName).stem().string());
        if (want && std::find(want->begin(), want->end(), key) == want->end()) continue;
        auto rec = a.readStaticMapRecord(m);
        uint32_t bank = 0;
        if (rec.size() >= 8) std::memcpy(&bank, rec.data() + 4, 4);
        const auto it = byId.find(bank);
        if (it == byId.end()) continue;
        auto chunk = a.read(*it->second);
        uint64_t h = 1469598103934665603ull;
        for (const uint8_t c : chunk) { h ^= c; h *= 1099511628211ull; }
        out[key] = {h ^ chunk.size(), std::move(rec), want ? std::move(chunk) : std::vector<uint8_t>{}};
    }
    return out;
}
} // namespace

bool prepareShadow(const fs::path& gameRoot, const fs::path& pack, const fs::path& shadow, bool viewOnly, std::string& error) try {
    const fs::path lv = fs::path("data") / "Levels";
    std::error_code ec;
    fs::remove_all(shadow, ec);
    fs::create_directories(shadow / lv / "FinalAlbion", ec);
    fs::create_directories(shadow / "data" / "CompiledDefs", ec);
    if (ec) { error = "cannot create " + shadow.string() + ": " + ec.message(); return false; }
    auto copy = [&](const fs::path& from, const fs::path& to) {
        if (!fs::exists(from, ec)) return true;
        fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
        if (ec) { error = "cannot copy " + from.string() + ": " + ec.message(); return false; }
        return true;
    };
    const std::vector<std::string> world = {"FinalAlbion.bwd", "FinalAlbion.wld"};
    const std::vector<std::string> rest = {"FinalAlbion.wad", "FinalAlbion_RT.stb", "FinalAlbion.gtg"};
    for (const auto& f : world) if (!copy(gameRoot / lv / f, shadow / lv / f)) return false;
    if (!viewOnly) {
        for (const auto& f : rest) if (!copy(gameRoot / lv / f, shadow / lv / f)) return false;
        for (const char* f : {"game.bin", "names.bin"})
            if (!copy(gameRoot / "data" / "CompiledDefs" / f, shadow / "data" / "CompiledDefs" / f)) return false;
        if (fs::is_directory(gameRoot / lv / "FinalAlbion", ec))   // a loose-level install's levels
            for (const auto& de : fs::directory_iterator(gameRoot / lv / "FinalAlbion", ec))
                if (de.is_regular_file(ec) && !copy(de.path(), shadow / lv / "FinalAlbion" / de.path().filename())) return false;
    }
    // the pack over it
    for (const auto& f : viewOnly ? world : std::vector<std::string>{"FinalAlbion.bwd", "FinalAlbion.wld", "FinalAlbion.gtg"})
        if (!copy(pack / lv / f, shadow / lv / f)) return false;
    if (viewOnly) return true;
    if (fs::is_directory(pack / lv / "FinalAlbion", ec))
        for (const auto& de : fs::directory_iterator(pack / lv / "FinalAlbion", ec))
            if (de.is_regular_file(ec) && !copy(de.path(), shadow / lv / "FinalAlbion" / de.path().filename())) return false;
    const fs::path stbDir = pack / "stb";
    if (fs::is_directory(stbDir, ec))
        for (const auto& de : fs::directory_iterator(stbDir, ec)) {
            if (!de.is_regular_file(ec) || de.path().extension() != ".chunk") continue;
            const std::string map = de.path().stem().string();
            if (!writeStaticMapChunk(shadow / lv / "FinalAlbion_RT.stb", map, slurpFile(de.path()), slurpFile(stbDir / (map + ".record")), error)) return false;
        }
    return true;
} catch (const std::exception& e) {
    error = e.what(); return false;
}

fs::path viewShadowRoot() {
    static const PackWorkspace view("pack-view-");
    return view.path();
}

bool intoPack(const fs::path& gameRoot, const fs::path& pack, const std::function<bool(const fs::path&, std::string&)>& op,
              std::vector<std::string>& notes, std::string& error) try {
    const PackWorkspace workspace("pack-shadow-");
    const fs::path& shadow = workspace.path();
    if (!prepareShadow(gameRoot, pack, shadow, false, error)) return false;
    if (!op(shadow, error)) return false;
    const auto rep = capture(shadow, gameRoot, pack);
    if (!rep.errors.empty()) { error = rep.errors.front(); return false; }
    std::string what;
    for (const auto& f : rep.files) what += (what.empty() ? "" : ", ") + fs::path(f).filename().string();
    for (const auto& m : rep.maps) what += (what.empty() ? "" : ", ") + ("static map " + m);
    for (const auto& f : rep.removed) what += (what.empty() ? "" : ", ") + ("removed override " + fs::path(f).filename().string());
    notes.push_back("into pack " + pack.filename().string() + ": " + (what.empty() ? std::string("no difference from the game") : what));
    return true;
} catch (const std::exception& e) {
    error = e.what(); return false;
}

CaptureReport capture(const fs::path& shadowRoot, const fs::path& baseRoot, const fs::path& pack) {
    CaptureReport rep;
    const fs::path lv = fs::path("data") / "Levels";
    std::error_code ec;
    try {
        if (!fs::is_directory(baseRoot) || !fs::is_directory(shadowRoot))
            throw std::runtime_error("capture needs existing base and shadow directories");
        if (fs::exists(pack) && (fs::equivalent(pack, baseRoot) || fs::equivalent(pack, shadowRoot)))
            throw std::runtime_error("capture destination must differ from its base and shadow");
        detail::PendingBanks pending(pack, ".forge-pack-capture-");
        auto removeOverride = [&](const fs::path& relative) {
            if (!fs::exists(pack / relative)) return;
            pending.remove(relative);
            rep.removed.push_back(relative.generic_string());
        };
        // world files
        for (const char* f : {"FinalAlbion.wld", "FinalAlbion.bwd", "FinalAlbion.gtg"}) {   // gtg: region entrances (a whole-file layer)
            const fs::path a = shadowRoot / lv / f, b = baseRoot / lv / f;
            if (!fs::exists(a)) continue;
            const auto bytes = slurpFile(a);
            if (fs::exists(b) && slurpFile(b) == bytes) { removeOverride(lv / f); continue; }
            putFile(pending.prepare(lv / f), bytes);
            rep.files.push_back((lv / f).generic_string());
        }
        // level files: the shadow's WAD entries + loose files against the base's
        std::map<std::string, std::vector<uint8_t>> baseLevels;   // leaf (lower) -> bytes
        auto collect = [](const fs::path& root, std::map<std::string, std::vector<uint8_t>>& into, std::map<std::string, std::string>* spelling) {
            const fs::path wad = root / "data" / "Levels" / "FinalAlbion.wad";
            if (fs::exists(wad)) {
                const auto a = forge::wad::Archive::open(wad);
                for (const auto& e : a.entries()) {
                    const std::string leaf = fs::path(e.name).filename().string();
                    const std::string ext = lowerS(fs::path(leaf).extension().string());
                    if (ext != ".lev" && ext != ".tng") continue;
                    into[lowerS(leaf)] = a.read(e);
                    if (spelling) (*spelling)[lowerS(leaf)] = leaf;
                }
            }
            const fs::path loose = root / "data" / "Levels" / "FinalAlbion";
            std::error_code e2;
            if (fs::is_directory(loose, e2))
                for (const auto& de : fs::directory_iterator(loose, e2)) {
                    const std::string ext = lowerS(de.path().extension().string());
                    if (!de.is_regular_file(e2) || (ext != ".lev" && ext != ".tng")) continue;
                    into[lowerS(de.path().filename().string())] = slurpFile(de.path());   // loose over WAD, like the tools
                    if (spelling) (*spelling)[lowerS(de.path().filename().string())] = de.path().filename().string();
                }
        };
        std::map<std::string, std::vector<uint8_t>> shadowLevels;
        std::map<std::string, std::string> spelling;
        collect(baseRoot, baseLevels, nullptr);
        collect(shadowRoot, shadowLevels, &spelling);
        for (const auto& [k, bytes] : shadowLevels) {
            const auto it = baseLevels.find(k);
            if (it != baseLevels.end() && it->second == bytes) { removeOverride(lv / "FinalAlbion" / spelling[k]); continue; }
            putFile(pending.prepare(lv / "FinalAlbion" / spelling[k]), bytes);
            rep.files.push_back((lv / "FinalAlbion" / spelling[k]).generic_string());
        }
        // static maps: a new or changed chunk (a re-laid bank rebases every record, so the
        // chunk is the test; the record goes along for its info block)
        const fs::path shadowStb = shadowRoot / lv / "FinalAlbion_RT.stb", baseStb = baseRoot / lv / "FinalAlbion_RT.stb";
        if (fs::exists(shadowStb) && fs::exists(baseStb)) {
            const auto baseMaps = staticMapsOf(baseStb);
            auto mapName = [&](const std::string& key) {
                const auto it = spelling.find(key + ".lev");
                return it == spelling.end() ? key : fs::path(it->second).stem().string();
            };
            std::vector<std::string> changed;
            for (const auto& [k, mb] : staticMapsOf(shadowStb)) {
                const auto it = baseMaps.find(k);
                if (it == baseMaps.end() || it->second.hash != mb.hash) changed.push_back(k);
                else {
                    const auto name = mapName(k);
                    removeOverride(fs::path("stb") / (name + ".chunk"));
                    removeOverride(fs::path("stb") / (name + ".record"));
                }
            }
            for (const auto& [k, cr] : staticMapsOf(shadowStb, &changed)) {
                const auto name = mapName(k);
                putFile(pending.prepare(fs::path("stb") / (name + ".chunk")), cr.chunk);
                putFile(pending.prepare(fs::path("stb") / (name + ".record")), cr.record);
                rep.maps.push_back(name);
            }
        }
        std::string error;
        if (!pending.install(false, error)) throw std::runtime_error(error);
    } catch (const std::exception& e) {
        rep.files.clear(); rep.maps.clear(); rep.removed.clear(); rep.errors.push_back(e.what());
    }
    return rep;
}

} // namespace albion::modpack
