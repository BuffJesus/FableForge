#include "forge/lipsync_preset.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>

static void require(bool okay,const char* message) {
    if(!okay) throw std::runtime_error(message);
}

int main(int argc,char** argv) {
    try {
        const auto& presets=forge::lipsync::headPresets();
        require(presets.size()==18 && presets[3].name=="Demon Door" &&
                presets[0].eyeMesh=="MESH_EYE_BLUE_DARK" &&
                presets[0].eyeSides==1 && presets[1].eyeSides==3 &&
                presets[0].eyeRenderSize==1.21f &&
                presets[1].eyeRenderSize==1.34f &&
                presets[2].eyeRenderSize==1.34f &&
                presets[4].eyeRenderSize==1.30f &&
                presets[3].eyeMesh.empty() &&
                presets[3].tracks[0].symbol=="AH" &&
                presets[3].tracks[0].animation=="ANIM_DEMON_DOOR_PHONEME_AI" &&
                presets[3].tracks[5].animation=="ANIM_DEMON_DOOR_PHONEME_ST",
                "retail head preset mapping");
        forge::big::File empty;
        const auto absent=forge::lipsync::inspectHeadPreset(empty,presets[0]);
        require(!absent.complete() && absent.missing.size()==1 &&
                absent.missing[0]=="MBANK_ALLMESHES","missing graphics bank");
        auto& synthetic=empty.addBank("MBANK_ALLMESHES",1);
        forge::big::Entry duplicated;
        duplicated.name=presets[0].mesh;duplicated.id=1;
        duplicated.type=5;duplicated.length=1;
        synthetic.entries.push_back(duplicated);
        duplicated.id=2;synthetic.entries.push_back(duplicated);
        const auto ambiguous=forge::lipsync::inspectHeadPreset(empty,presets[0]);
        require(!ambiguous.meshId && !ambiguous.missing.empty() &&
                ambiguous.missing[0]==presets[0].mesh,"ambiguous mesh name accepted");
        if(argc>1) {
            const auto graphics=forge::big::File::open(std::filesystem::path(argv[1]));
            for(const auto& preset:presets) {
                const auto assets=forge::lipsync::inspectHeadPreset(graphics,preset);
                if(!assets.complete()) {
                    std::cerr << preset.name << " missing:";
                    for(const auto& item:assets.missing) std::cerr << ' ' << item;
                    std::cerr << '\n';
                }
                require(assets.complete() && assets.animationIds.size()==preset.tracks.size(),
                        "retail head preset incomplete");
                require(preset.eyeMesh.empty()==(assets.eyeMeshId==0),
                        "retail eye mesh resolution");
                std::cout << preset.name << ": mesh " << assets.meshId << ", "
                          << assets.animationIds.size() << " poses\n";
            }
        } else std::cout << "lipsync presets: fixture passed\n";
        return 0;
    } catch(const std::exception& ex) {
        std::cerr << "lipsync presets: " << ex.what() << '\n';
        return 1;
    }
}
