#include "modpack.hpp"
#include "meshimport.hpp"

#include <bit>
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
        const std::string gltf = R"({"asset":{"version":"2.0"},"buffers":[{"uri":"first/geometry.bin","byteLength":36},{"uri":"second/geometry.bin","byteLength":36}],"bufferViews":[{"buffer":1,"byteLength":36}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],"extras":{"note":"keep this metadata"}})";
        auto positions = [](float extent) {
            std::string bytes;
            for (float value : {0.f, 0.f, 0.f, extent, 0.f, 0.f, 0.f, extent, 0.f}) {
                const auto bits = std::bit_cast<uint32_t>(value);
                for (unsigned shift = 0; shift < 32; shift += 8) bytes += char(bits >> shift);
            }
            return bytes;
        };
        write(root / "gltf/model.gltf", gltf);
        write(root / "gltf/first/geometry.bin", positions(1));
        write(root / "gltf/second/geometry.bin", positions(2));
        pack::ModelRecipe sidecar; sidecar.name = "SIDECAR"; sidecar.model = (root / "gltf/model.gltf").string();
        require(pack::addModel(folder, sidecar, error), error);
        saved = pack::load(folder);
        const auto modelPath = folder / saved.models.back().model;
        const auto geometry = albion::meshimport::loadModel(modelPath);
        require(geometry.prims.size() == 1 && geometry.prims[0].verts.size() == 3 &&
                geometry.prims[0].verts[1].x == 200, "packed glTF sidecar geometry changed");
        require(read(root / "gltf/model.gltf") == gltf && read(modelPath).find("keep this metadata") != std::string::npos,
                "packaging changed source JSON or lost metadata");
        const auto withSidecar = snapshot(folder);
        auto brokenGltf = gltf;
        brokenGltf.replace(brokenGltf.find("second/geometry.bin"), std::string("second/geometry.bin").size(), "missing.bin");
        write(root / "gltf/broken.gltf", brokenGltf);
        sidecar.name = "MISSING_SIDECAR"; sidecar.model = (root / "gltf/broken.gltf").string();
        require(!pack::addModel(folder, sidecar, error) && snapshot(folder) == withSidecar,
                "missing sidecar was accepted or changed pack files");
        const auto captureBase = root / "capture_base", captureShadow = root / "capture_shadow";
        write(captureBase / "data/Levels/FinalAlbion.wld", "base world");
        write(captureBase / "data/Levels/FinalAlbion/CaptureMap.tng", "base map data");
        write(captureShadow / "data/Levels/FinalAlbion.wld", "new world");
        write(folder / "data/Levels/FinalAlbion.wld", "previous packed world");
        write(captureShadow / "data/Levels/FinalAlbion.wad", "invalid archive");
        const auto beforeCapture = snapshot(folder);
        const auto failedCapture = pack::capture(captureShadow, captureBase, folder);
        require(!failedCapture.errors.empty() && snapshot(folder) == beforeCapture,
                "failed world capture partially replaced pack files");
        fs::remove(captureShadow / "data/Levels/FinalAlbion.wad");
        write(captureShadow / "data/Levels/FinalAlbion/CaptureMap.tng", "new map data");
        const auto captured = pack::capture(captureShadow, captureBase, folder);
        require(captured.errors.empty() && captured.files.size() == 2 &&
                read(folder / "data/Levels/FinalAlbion.wld") == "new world" &&
                read(folder / "data/Levels/FinalAlbion/CaptureMap.tng") == "new map data",
                "successful world capture lost a changed layer");
#ifdef _WIN32
        write(captureShadow / "data/Levels/FinalAlbion.wld", "replacement world");
        write(captureShadow / "data/Levels/FinalAlbion/CaptureMap.tng", "replacement map data");
        const auto beforeLockedCapture = snapshot(folder);
        const auto capturedMap = folder / "data/Levels/FinalAlbion/CaptureMap.tng";
        const HANDLE mapHandle = CreateFileW(capturedMap.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(mapHandle != INVALID_HANDLE_VALUE, "cannot lock captured map fixture");
        pack::CaptureReport lockedCapture;
        try {
            lockedCapture = pack::capture(captureShadow, captureBase, folder);
            require(!lockedCapture.errors.empty() && snapshot(folder) == beforeLockedCapture,
                    "late capture replacement did not roll back");
            write(captureShadow / "data/Levels/FinalAlbion.wld", "base world");
            lockedCapture = pack::capture(captureShadow, captureBase, folder);
        }
        catch (...) { CloseHandle(mapHandle); throw; }
        CloseHandle(mapHandle);
        require(!lockedCapture.errors.empty() && lockedCapture.files.empty() && lockedCapture.maps.empty() && lockedCapture.removed.empty() &&
                snapshot(folder) == beforeLockedCapture, "late capture failure left changed/removed layers or reported success");
#endif
        write(captureShadow / "data/Levels/FinalAlbion.wld", "base world");
        write(captureShadow / "data/Levels/FinalAlbion/CaptureMap.tng", "base map data");
        const auto reverted = pack::capture(captureShadow, captureBase, folder);
        require(reverted.errors.empty() && reverted.files.empty() && reverted.removed.size() == 2 &&
                snapshot(folder) == withSidecar, "capture retained reverted overrides or changed unrelated recipes");
        const auto baseBeforeAlias = snapshot(captureBase);
        const auto shadowBeforeAlias = snapshot(captureShadow);
        require(!pack::capture(captureShadow, captureBase, captureBase).errors.empty() && snapshot(captureBase) == baseBeforeAlias,
                "capture into base was accepted or changed files");
        require(!pack::capture(captureShadow, captureBase, captureShadow).errors.empty() && snapshot(captureShadow) == shadowBeforeAlias,
                "capture into shadow was accepted or changed files");
        const auto missingRecordPack = root / "missing_record_pack";
        write(missingRecordPack / "stb/Map.chunk", "chunk without record");
        require(!pack::prepareShadow(captureBase, missingRecordPack, root / "prepared_shadow", false, error) &&
                error.find("Map.record") != std::string::npos,
                "missing record preparation escaped or lost its error");
        std::vector<std::string> shadowNotes;
        const auto independentPack = root / "independent_pack";
        require(pack::create(independentPack, "independent", error), error);
        fs::path outerShadowPath;
        const bool isolated = pack::intoPack(captureBase, independentPack, [&](const fs::path& outer, std::string& why) {
            outerShadowPath = outer;
            write(outer / "owned.marker", "outer operation");
            std::vector<std::string> innerNotes;
            std::string innerError;
            const bool nested = pack::intoPack(captureBase, independentPack, [&](const fs::path& inner, std::string& err) {
                require(inner != outer, "pack operations shared their shadow directory");
                err = "deliberate nested refusal"; return false;
            }, innerNotes, innerError);
            if (nested || innerError != "deliberate nested refusal" || !fs::exists(outer / "owned.marker")) {
                why = "nested operation erased the outer shadow: " + innerError; return false;
            }
            return true;
        }, shadowNotes, error);
        require(isolated, error);
        require(!fs::exists(outerShadowPath), "completed operation leaked its shadow");
        fs::path failedShadowPath;
        const auto beforeThrownOperation = snapshot(independentPack);
        require(!pack::intoPack(captureBase, independentPack, [&](const fs::path& shadow, std::string&) -> bool {
            failedShadowPath = shadow;
            throw std::runtime_error("deliberate operation exception");
        }, shadowNotes, error) && error == "deliberate operation exception" && !fs::exists(failedShadowPath) &&
                snapshot(independentPack) == beforeThrownOperation, "throwing pack operation leaked files or escaped");
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
