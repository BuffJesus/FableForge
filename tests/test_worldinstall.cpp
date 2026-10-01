#include "forge/worldinstall.hpp"
#include "forge/bwd.hpp"
#include "forge/temporarydirectory.hpp"
#include <filesystem>
#include <limits>
#include <array>
#include <iostream>
#include <stdexcept>

static void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main(int argc, char** argv) {
    try {
        // Scratch integration runner for the core's default .bak policy.
        if (argc == 2) {
            forge::worldinstall::Request request;
            request.gameRoot=argv[1]; request.donorLevelName="TeleporterGreatwood";
            request.newLevelName="CoreInstallProbe"; request.hostRegion="Greatwood";
            request.worldX=request.worldY=6400;
            (void)forge::worldinstall::installLevel(request);
            std::cout << "Core installation completed\n";
            return 0;
        }
        check(argc==1,"usage: worldinstall_tests [scratch-install-root]");
        namespace fs = std::filesystem;
        forge::TemporaryDirectory scratch(fs::temp_directory_path(), "FableForgeWorldInstall-");
        const auto root = scratch.path();
        fs::create_directories(root / "data" / "Levels");
        forge::bwd::File file;
        forge::bwd::MapInfo donor;
        donor.levelName="Data\\Levels\\FinalAlbion\\Donor.lev"; donor.scriptName="Donor";
        donor.left=8128; donor.right=8192; donor.top=0; donor.bottom=64;
        file.maps().push_back(donor);
        file.write(root / "data" / "Levels" / "FinalAlbion.bwd");
        const auto origin=forge::worldinstall::suggestOrigin(root,"Donor");
        check(origin.x>=0 && origin.y>=0 && origin.x<=8128 && origin.y<=8128,
              "suggested origin puts the donor beyond the world grid");
        check(origin.x%32==0 && origin.y%32==0, "suggested origin is not aligned");
        check(origin.x+64<=donor.left || origin.y>=donor.bottom, "suggested origin overlaps donor");
        forge::worldinstall::validatePlacement(8128,8128,64,64);
        const std::array<std::array<int64_t,4>,9> invalid{{
            {{8192,0,64,64}}, {{0,8192,64,64}}, {{-32,0,64,64}}, {{1,0,64,64}},
            {{std::numeric_limits<int>::max()-31,0,64,64}}, {{0,0,0,64}}, {{0,0,64,-1}},
            {{0,0,8193,64}}, {{0,0,std::numeric_limits<int64_t>::max(),64}}
        }};
        for (const auto& value : invalid) {
            bool refused=false;
            try { forge::worldinstall::validatePlacement(int(value[0]),int(value[1]),value[2],value[3]); }
            catch (const std::runtime_error&) { refused=true; }
            check(refused,"invalid placement accepted");
        }
        auto full=donor; full.scriptName="Full"; full.levelName="Full.lev";
        full.left=full.top=0; full.right=full.bottom=8192;
        file.maps().push_back(full);
        file.write(root / "data" / "Levels" / "FinalAlbion.bwd");
        bool refused=false;
        try { (void)forge::worldinstall::suggestOrigin(root,"Donor"); }
        catch (const std::runtime_error& e) { refused=std::string(e.what()).find("no free")!=std::string::npos; }
        check(refused,"full world returned an unusable origin");
        file.maps()[0].left=std::numeric_limits<int>::min();
        file.maps()[0].right=std::numeric_limits<int>::max();
        file.write(root / "data" / "Levels" / "FinalAlbion.bwd");
        refused=false;
        try { (void)forge::worldinstall::suggestOrigin(root,"Donor"); }
        catch (const std::runtime_error&) { refused=true; }
        check(refused,"overflowing donor dimensions accepted");
        std::cout << "World installation placement checks passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
