#include "modpack.hpp"

#include <fstream>
#include <stdexcept>

#include "meshimport.hpp"
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
    return p;
}

void save(const fs::path& folder, const Pack& pack) {
    json j;
    j["version"] = pack.version;
    j["name"] = pack.name;
    j["models"] = json::array();
    for (const auto& m : pack.models)
        j["models"].push_back({{"name", m.name}, {"model", m.model}, {"texture", m.texture}, {"donor", m.donor}, {"collision", m.collision}});
    j["groundThemes"] = json::array();
    for (const auto& t : pack.groundThemes)
        j["groundThemes"].push_back({{"name", t.name}, {"png", t.png}, {"cliffPng", t.cliffPng}, {"donor", t.donor}});
    fs::create_directories(folder);
    std::ofstream(folder / kFileName) << j.dump(2);
}

bool create(const fs::path& folder, const std::string& name, std::string& error) {
    std::error_code ec;
    if (isPack(folder)) { error = folder.string() + " is already a pack"; return false; }
    fs::create_directories(folder / "assets", ec);
    if (ec) { error = "cannot create " + folder.string() + ": " + ec.message(); return false; }
    Pack p;
    p.name = name.empty() ? folder.filename().string() : name;
    save(folder, p);
    return true;
}

namespace {
// copy a source file into assets/ (an existing copy of the same name is replaced); "" stays ""
bool intoAssets(const fs::path& folder, std::string& path, std::string& error) {
    if (path.empty()) return true;
    std::error_code ec;
    const fs::path src(path);
    if (!fs::is_regular_file(src, ec)) { error = "no such file: " + path; return false; }
    const fs::path dst = folder / "assets" / src.filename();
    fs::create_directories(dst.parent_path(), ec);
    if (!fs::equivalent(src, dst, ec)) fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    if (ec) { error = "cannot copy " + path + ": " + ec.message(); return false; }
    path = (fs::path("assets") / src.filename()).generic_string();
    return true;
}
bool validName(const std::string& n) {
    if (n.empty() || n.size() > 96) return false;
    for (const char c : n) if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}
} // namespace

bool addModel(const fs::path& folder, ModelRecipe recipe, std::string& error) {
    if (!isPack(folder)) { error = folder.string() + " is not a FableForge pack"; return false; }
    if (!validName(recipe.name)) { error = "model name must be A-Z, 0-9 and _"; return false; }
    if (!intoAssets(folder, recipe.model, error) || !intoAssets(folder, recipe.texture, error)) return false;
    Pack p = load(folder);
    std::erase_if(p.models, [&](const ModelRecipe& m) { return m.name == recipe.name; });
    p.models.push_back(std::move(recipe));
    save(folder, p);
    return true;
}

bool addGroundTheme(const fs::path& folder, GroundThemeRecipe recipe, std::string& error) {
    if (!isPack(folder)) { error = folder.string() + " is not a FableForge pack"; return false; }
    if (!validName(recipe.name)) { error = "theme name must be A-Z, 0-9 and _"; return false; }
    if (!intoAssets(folder, recipe.png, error) || !intoAssets(folder, recipe.cliffPng, error)) return false;
    Pack p = load(folder);
    std::erase_if(p.groundThemes, [&](const GroundThemeRecipe& t) { return t.name == recipe.name; });
    p.groundThemes.push_back(std::move(recipe));
    save(folder, p);
    return true;
}

ApplyReport apply(const fs::path& folder, const fs::path& baseRoot, const fs::path& outRoot) {
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
    return rep;
}

} // namespace albion::modpack
