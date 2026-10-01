#include "forge/big.hpp"
#include "forge/dialoguetext.hpp"
#include "forge/textbig.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace fs=std::filesystem;

static void require(bool okay,const char* message) {
    if(!okay) throw std::runtime_error(message);
}

static void put32(std::vector<uint8_t>& out,uint32_t value) {
    for(int i=0;i<4;++i) out.push_back(uint8_t(value>>(8*i)));
}

static void write(const fs::path& path,const std::vector<uint8_t>& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream stream(path,std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 std::streamsize(bytes.size()));
    require(bool(stream),"fixture write failed");
}

int main(int argc,char** argv) {
    try {
        const auto root=fs::temp_directory_path()/
            ("fableforge_dialoguetext_"+std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        const auto textPath=root/"data"/"lang"/"English"/"text.big";
        const auto defs=root/"data"/"Defs";
        forge::big::File file;
        file.setMagic("BIGB");
        auto& bank=file.addBank("TEXT_ENGLISH_MAIN",1);
        auto add=[&](uint32_t id,const std::string& name,const std::string& content,
                     const std::string& speechBank) {
            forge::textbig::Entry value;
            value.content=content;value.speechBank=speechBank;
            value.speaker="SPEAKER";value.identifier=name;
            forge::big::Entry record;
            record.id=id;record.type=0;record.name=name;
            record.data=forge::textbig::encode(value);
            record.length=uint32_t(record.data.size());
            bank.entries.push_back(std::move(record));
        };
        add(1,"TEXT_DEMO","Hello from main","Dialogue.lug");
        add(2,"TEXT_SCRIPT","Hello from script","ScriptDialogue.lut");
        add(3,"TEXT_ORPHAN","No mapped clip","Dialogue.lug");
        write(textPath,file.serialize());
        std::vector<uint8_t> main,script,empty;
        put32(main,1);put32(main,0xF1AA0C29u);put32(main,7);
        put32(script,1);put32(script,0x0629162Bu);put32(script,7);
        put32(empty,0);
        write(defs/"dialoguesnds.bin",main);
        write(defs/"dialoguesnds2.bin",empty);
        write(defs/"scriptdialoguesnds.bin",script);
        write(defs/"scriptdialoguesnds2.bin",empty);
        const auto index=forge::dialoguetext::Index::open(textPath,defs,"English");
        const auto* a=index.find("LIPSYNC_ENGLISH_MAIN",7);
        const auto* b=index.find("LIPSYNC_ENGLISH_SCRIPT",7);
        require(index.resolvedCount()==2 && a && b && a->size()==1 && b->size()==1 &&
                a->front().content=="Hello from main" &&
                b->front().content=="Hello from script" &&
                !index.find("LIPSYNC_ENGLISH_MAIN",8),
                "bank-scoped text/Sound ID join failed");
        const auto mainSearch=index.search("LIPSYNC_ENGLISH_MAIN","hello",10);
        const auto scriptSearch=index.search("LIPSYNC_ENGLISH_SCRIPT","SPEAKER",10);
        require(mainSearch.size()==1 && mainSearch[0].soundId==7 &&
                mainSearch[0].line.name=="TEXT_DEMO" &&
                scriptSearch.size()==1 && scriptSearch[0].soundId==7 &&
                scriptSearch[0].line.name=="TEXT_SCRIPT" &&
                index.search("LIPSYNC_ENGLISH_MAIN","script",10).empty() &&
                index.search("LIPSYNC_ENGLISH_MAIN","hello",0).empty(),
                "bank-scoped dialogue search failed");
        write(defs/"dialoguesnds.bin",{1,0,0});
        const auto browse=index.search("","",10);
        const auto byId=index.search("","7",10);
        require(browse.size()==2 && byId.size()==2 &&
                browse[0].lipsyncBank=="LIPSYNC_ENGLISH_MAIN" &&
                browse[1].lipsyncBank=="LIPSYNC_ENGLISH_SCRIPT" &&
                index.search("","",1).size()==1 &&
                index.search("LIPSYNC_ENGLISH_MAIN","",10).size()==1 &&
                index.search("","hello",10).size()==2,
                "browse/all-bank search lost a line or its bank identity");
        bool rejected=false;
        try { forge::dialoguetext::Index::open(textPath,defs,"English"); }
        catch(const std::exception&) { rejected=true; }
        require(rejected,"truncated sound name table accepted");
        fs::remove_all(root);
        if(argc>1) {
            const fs::path install=argv[1];
            const auto retail=forge::dialoguetext::Index::open(
                install/"data"/"lang"/"English"/"text.big",
                install/"data"/"Defs","English");
            const auto* door=retail.find("LIPSYNC_ENGLISH_SCRIPT",5080);
            require(retail.resolvedCount()>=20000 && door && door->size()==1 &&
                    door->front().name=="TEXT_QST_088_EAT_PIES_INTRO_40" &&
                    door->front().content=="I want beefy! Blubbery! Plump! Porcine!" &&
                    door->front().speaker=="DEMON DOOR",
                    "retail demon door subtitle join failed");
            const auto beefy=retail.search("LIPSYNC_ENGLISH_SCRIPT","beefy",10);
            require(beefy.size()==1 && beefy[0].soundId==5080 &&
                    beefy[0].line.name=="TEXT_QST_088_EAT_PIES_INTRO_40",
                    "retail subtitle search did not find the Demon Door");
            std::cout << "dialogue text: " << retail.resolvedCount()
                      << " retail voiced lines joined; demon door 5080 exact\n";
        } else std::cout << "dialogue text: bank and Sound ID fixture passed\n";
        return 0;
    } catch(const std::exception& ex) {
        std::cerr << "dialogue text: " << ex.what() << '\n';
        return 1;
    }
}
