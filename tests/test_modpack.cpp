#include "modpack.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;
namespace pack = albion::modpack;

static void require(bool okay, const std::string& why) {
    if (!okay) throw std::runtime_error(why);
}
static void write(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << bytes;
    out.close();
    require(bool(out), "fixture write failed");
}
static std::string read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    require(bool(in), "fixture read failed: " + path.string());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
static auto snapshot(const fs::path& root) {
    std::map<fs::path, std::string> result;
    for (const auto& item : fs::recursive_directory_iterator(root))
        if (item.is_regular_file()) result[item.path().lexically_relative(root)] = read(item.path());
    return result;
}

int main() {
    const fs::path root = fs::temp_directory_path() / ("FableForgeModPack-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        require(fs::create_directory(root), "fixture directory already exists");
        const auto folder = root / "pack";
        std::string error;
        require(pack::create(folder, "test", error), error);
        write(root / "first/model.obj", "first model");
        write(root / "second/model.obj", "second model");
        write(root / "first/texture.png", "first texture");
        write(root / "second/texture.png", "second texture");
        pack::ModelRecipe a;
        a.name = "MODEL_A"; a.model = (root / "first/model.obj").string();
        require(pack::addModel(folder, a, error), error);
        const auto original = snapshot(folder);
        auto b = a; b.name = "MODEL_B"; b.model = (root / "second/model.obj").string();
        b.texture = (root / "missing.png").string();
        require(!pack::addModel(folder, b, error), "missing texture accepted");
        require(snapshot(folder) == original, "failed model add changed an existing asset or manifest");
        b.texture = (root / "second/texture.png").string();
        require(pack::addModel(folder, b, error), error);
        auto saved = pack::load(folder);
        require(saved.models.size() == 2 && read(folder / saved.models[0].model) == "first model" &&
                read(folder / saved.models[1].model) == "second model", "same-basename models collided");
        pack::GroundThemeRecipe theme;
        theme.name = "GROUND_A"; theme.png = (root / "first/texture.png").string();
        theme.cliffPng = (root / "second/texture.png").string();
        require(pack::addGroundTheme(folder, theme, error), error);
        saved = pack::load(folder);
        require(read(folder / saved.groundThemes[0].png) == "first texture" &&
                read(folder / saved.groundThemes[0].cliffPng) == "second texture", "base/cliff texture names collided");
        require(read(folder / saved.models[1].texture) == "second texture", "theme add changed model texture");
        const auto complete = snapshot(folder);
        theme.name = "GROUND_B"; theme.cliffPng = (root / "missing.png").string();
        require(!pack::addGroundTheme(folder, theme, error) && snapshot(folder) == complete,
                "failed theme add changed pack files");
        require(!pack::create(folder, "overwrite", error) && snapshot(folder) == complete,
                "creating an existing pack changed it");
        const auto malformed = root / "malformed";
        write(malformed / pack::kFileName, "{invalid manifest");
        const auto invalidBefore = snapshot(malformed);
        require(!pack::addModel(malformed, a, error) && snapshot(malformed) == invalidBefore,
                "malformed manifest add escaped or changed files");
        const auto blocked = root / "blocked";
        fs::create_directories(blocked / pack::kFileName);
        require(!pack::create(blocked, "blocked", error), "creation reported success without a manifest");
        write(root / "first/model.obj", "replacement model");
#ifdef _WIN32
        const auto manifest = folder / pack::kFileName;
        const HANDLE handle = CreateFileW(manifest.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                          nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(handle != INVALID_HANDLE_VALUE, "cannot lock manifest fixture");
        bool refused = false;
        try { refused = !pack::addModel(folder, a, error); }
        catch (...) { CloseHandle(handle); throw; }
        CloseHandle(handle);
        require(refused && snapshot(folder) == complete, "late manifest failure changed pack files");
#endif
        require(pack::addModel(folder, a, error), error);
        saved = pack::load(folder);
        require(saved.models.size() == 2, "recipe replacement appended a duplicate");
        for (const auto& model : saved.models)
            require(read(folder / model.model) == (model.name == "MODEL_A" ? "replacement model" : "second model"),
                    "recipe replacement changed the wrong model");
        for (const auto& item : fs::directory_iterator(folder))
            require(!item.path().filename().string().starts_with(".forge-"), "staging directory leaked");
        require(fs::equivalent(fs::canonical(root).parent_path(), fs::temp_directory_path()) && root.filename().string().starts_with("FableForgeModPack-"),
                "unexpected cleanup root");
        fs::remove_all(root);
        std::cout << "mod pack input/failure checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << " (fixtures retained at " << root.string() << ")\n";
        return 1;
    }
}
