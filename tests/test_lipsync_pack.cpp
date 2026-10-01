#include "modpack.hpp"
#include "forge/big.hpp"
#include "forge/lipsync.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace fs=std::filesystem;

static void require(bool good,const char* message) {
    if(!good) throw std::runtime_error(message);
}

static uint8_t firstWeight(const fs::path& path,const std::string& bankName,
                           uint32_t soundId) {
    const auto archive=forge::big::File::open(path);
    const auto* bank=archive.findBank(bankName);
    require(bank,"lip sync bank absent");
    for(const auto& record:bank->entries) if(record.id==soundId) {
        const auto value=forge::lipsync::decode(archive.entryData(record),record.subHeader);
        require(!value.frames.empty() && !value.frames[0].empty(),"lip sync frame absent");
        return value.frames[0][0].weight;
    }
    throw std::runtime_error("lip sync Sound ID absent");
}

int main() {
    try {
        const auto root=fs::temp_directory_path()/
            ("fableforge_lipsync_pack_"+std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        const auto base=root/"base",packA=root/"A",packB=root/"B";
        const auto outAB=root/"AB",outBA=root/"BA";
        const auto relative=fs::path("data")/"lang"/"English"/"dialogue.big";
        fs::create_directories((base/relative).parent_path());
        forge::lipsync::Entry original;
        original.dictionary={{7,"MM"}};original.fps=43;
        original.frames={{{7,200}},{{7,255}}};
        original.setDuration(2.0f/43);
        forge::big::File source;
        source.setMagic("BIGB");
        auto& main=source.addBank("LIPSYNC_ENGLISH_MAIN",1);
        forge::big::Entry record;
        record.id=1;record.type=1;record.name="Dialogue_1";
        record.data=forge::lipsync::encode(original);
        record.length=uint32_t(record.data.size());
        record.subHeader=forge::lipsync::encodeInfo(original);
        main.entries.push_back(record);
        record.id=2;record.name="Dialogue_2";
        main.entries.push_back(record);
        auto& script=source.addBank("LIPSYNC_ENGLISH_SCRIPT",2);
        record.id=1;record.name="ScriptDialogue_1";
        script.entries.push_back(record);
        {
            const auto bytes=source.serialize();
            std::ofstream output(base/relative,std::ios::binary);
            output.write(reinterpret_cast<const char*>(bytes.data()),
                         std::streamsize(bytes.size()));
            require(bool(output),"base BIG write");
        }
        std::string error;
        require(albion::modpack::create(packA,"A",error) &&
                albion::modpack::create(packB,"B",error),"pack creation");
        auto a=original,b=original,s=original,other=original;
        a.frames[0][0].weight=50;
        b.frames[0][0].weight=150;
        s.frames[0][0].weight=100;
        other.frames[0][0].weight=80;
        const std::vector<forge::lipsync::ArchiveEdit> aEdits={
            {"LIPSYNC_ENGLISH_MAIN",1,a}};
        const std::vector<forge::lipsync::ArchiveEdit> bEdits={
            {"LIPSYNC_ENGLISH_MAIN",1,b},
            {"LIPSYNC_ENGLISH_MAIN",2,other},
            {"LIPSYNC_ENGLISH_SCRIPT",1,s}};
        require(albion::modpack::addLipSync(packA,"English",aEdits,error) &&
                albion::modpack::addLipSync(packB,"English",bEdits,error),
                "pack lip sync recipe creation");
        const auto reloaded=albion::modpack::load(packB);
        require(reloaded.lipSync.size()==3 &&
                forge::lipsync::encode(reloaded.lipSync[0].value)==
                    forge::lipsync::encode(b),"pack recipe manifest roundtrip");
        const auto reportA=albion::modpack::apply(packA,base,outAB);
        const auto reportB=albion::modpack::apply(packB,base,outAB);
        require(reportA.errors.empty() && reportB.errors.empty() &&
                firstWeight(outAB/relative,"LIPSYNC_ENGLISH_MAIN",1)==150 &&
                firstWeight(outAB/relative,"LIPSYNC_ENGLISH_MAIN",2)==80 &&
                firstWeight(outAB/relative,"LIPSYNC_ENGLISH_SCRIPT",1)==100,
                "A then B pack composition");
        const auto reverseB=albion::modpack::apply(packB,base,outBA);
        const auto reverseA=albion::modpack::apply(packA,base,outBA);
        require(reverseB.errors.empty() && reverseA.errors.empty() &&
                firstWeight(outBA/relative,"LIPSYNC_ENGLISH_MAIN",1)==50 &&
                firstWeight(outBA/relative,"LIPSYNC_ENGLISH_MAIN",2)==80 &&
                firstWeight(outBA/relative,"LIPSYNC_ENGLISH_SCRIPT",1)==100,
                "B then A pack composition");
        const std::vector<forge::lipsync::ArchiveEdit> bad={
            {"LIPSYNC_FRENCH_MAIN",1,a}};
        require(!albion::modpack::addLipSync(packA,"English",bad,error),
                "cross-language bank recipe accepted");
        fs::remove_all(root);
        std::cout << "lip sync pack: separate lines and banks merged; same line follows pack order\n";
        return 0;
    } catch(const std::exception& ex) {
        std::cerr << "lip sync pack: " << ex.what() << '\n';
        return 1;
    }
}
