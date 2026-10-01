#include "forge/big.hpp"
#include "forge/lipsync.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

static void require(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main(int argc,char** argv) {
    try {
        // A fixed external byte fixture checks field order and little-endian
        // values; the retail pass below checks the full corpus when provided.
        const std::vector<uint8_t> raw={
            1,0,0,0, 7,'M','M',0, 43,0,0,0, 2,0,0,0,
            1,7,255, 0};
        const std::array<uint8_t,4> info={0,0,0x80,0x3f};
        const auto entry=forge::lipsync::decode(raw,info);
        require(entry.dictionary.size()==1 && entry.dictionary[0].id==7 &&
                entry.dictionary[0].symbol=="MM" && entry.fps==43 &&
                entry.frames.size()==2 && entry.frames[0].size()==1 &&
                entry.frames[0][0].weight==255 && entry.frames[1].empty() &&
                entry.duration()==1.0f,"fixture decode");
        require(forge::lipsync::encode(entry)==raw &&
                forge::lipsync::encodeInfo(entry)==std::vector<uint8_t>(info.begin(),info.end()),
                "fixture encode");
        auto curve=entry;
        curve.dictionary={{1,"AH"},{2,"SZ"}};
        curve.fps=2;
        curve.frames={{{1,128}},{{2,255}}};
        const auto first=forge::lipsync::sample(curve,-1.0);
        const auto middle=forge::lipsync::sample(curve,0.25);
        const auto last=forge::lipsync::sample(curve,10.0);
        const auto near=[](float a,float b) {return std::abs(a-b)<0.00001f;};
        require(first.frame==0 && near(first.visemes[0].weight,128.0f/255) &&
                near(first.visemes[1].weight,0) &&
                near(first.restWeight,127.0f/255) &&
                middle.frame==0 && near(middle.visemes[0].weight,64.0f/255) &&
                near(middle.visemes[1].weight,0.5f) &&
                near(middle.restWeight,0.5f-64.0f/255) &&
                last.frame==1 && near(last.visemes[1].weight,1) &&
                near(last.restWeight,0),"lip sync pose interpolation");
        curve.frames={{},{}};
        const auto silent=forge::lipsync::sample(curve,0.25);
        require(near(silent.restWeight,1) && near(silent.visemes[0].weight,0),
                "silent lip sync pose");
        curve.frames={{{1,255},{2,255}}};
        const auto overfull=forge::lipsync::sample(curve,0);
        require(near(overfull.visemes[0].weight,0.5f) &&
                near(overfull.visemes[1].weight,0.5f) &&
                near(overfull.restWeight,0),"overfull pose normalization");
        for(size_t n:{size_t(0),size_t(3),raw.size()-1}) {
            bool rejected=false;
            try {forge::lipsync::decode(std::span<const uint8_t>(raw).first(n),info);}
            catch(const std::runtime_error&) {rejected=true;}
            require(rejected,"truncated payload accepted");
        }
        auto bad=raw;
        bad.push_back(99);
        bool rejected=false;
        try {forge::lipsync::decode(bad,info);}
        catch(const std::runtime_error&) {rejected=true;}
        require(rejected,"trailing byte accepted");
        rejected=false;
        try {forge::lipsync::decode(raw,std::span<const uint8_t>(info).first(3));}
        catch(const std::runtime_error&) {rejected=true;}
        require(rejected,"short Info accepted");

        forge::big::File synthetic;
        synthetic.setMagic("BIGB");
        auto& main=synthetic.addBank("LIPSYNC_ENGLISH_MAIN",1);
        forge::big::Entry donor;
        donor.id=1;donor.type=1;donor.name="Dialogue_1";
        donor.data=raw;donor.length=uint32_t(raw.size());
        donor.subHeader.assign(info.begin(),info.end());
        donor.devSources={"","SPEAKER_FEMALE1"};
        main.entries.push_back(donor);
        auto& script=synthetic.addBank("LIPSYNC_ENGLISH_SCRIPT",2);
        donor.name="ScriptDialogue_1";
        script.entries.push_back(donor); // the same ID is valid in another bank
        auto editedFixture=entry;
        editedFixture.frames[0][0].weight=128;
        const auto changed=forge::lipsync::upsert(synthetic,"LIPSYNC_ENGLISH_MAIN",1,editedFixture);
        require(!changed.added && changed.name=="Dialogue_1" &&
                synthetic.findBank("LIPSYNC_ENGLISH_SCRIPT")->entries[0].data==raw,
                "same-bank edit");
        const auto added=forge::lipsync::upsert(synthetic,"LIPSYNC_ENGLISH_MAIN",2,entry);
        require(added.added && added.name=="Dialogue_2" &&
                synthetic.findBank("LIPSYNC_ENGLISH_MAIN")->entries[1].devSources==donor.devSources &&
                synthetic.findBank("LIPSYNC_ENGLISH_SCRIPT")->entries.size()==1,
                "same-bank add");
        auto frameEdit=entry;
        forge::lipsync::setWeight(frameEdit,0,"AH",127);
        require(frameEdit.dictionary.size()==2 && frameEdit.dictionary[1].id==0 &&
                frameEdit.frames[0].size()==2 && frameEdit.frames[0][1].weight==127,
                "new phoneme uses a free dictionary ID");
        forge::lipsync::setWeight(frameEdit,0,"AH",64);
        require(frameEdit.frames[0].size()==2 && frameEdit.frames[0][1].weight==64,
                "editing a phoneme must not add another key");
        forge::lipsync::removeWeight(frameEdit,0,"AH");
        require(frameEdit.frames[0].size()==1 && frameEdit.dictionary.size()==2,
                "removing a key preserves dictionary IDs");
        forge::lipsync::insertFrameAfter(frameEdit,0);
        require(frameEdit.frames.size()==3 && frameEdit.frames[1].empty() &&
                std::abs(frameEdit.duration()-3.0f/43)<1e-6f,
                "inserted frame and duration");
        forge::lipsync::eraseFrame(frameEdit,1);
        require(frameEdit.frames.size()==2 && std::abs(frameEdit.duration()-2.0f/43)<1e-6f,
                "removed frame and duration");
        require(forge::lipsync::decode(forge::lipsync::encode(frameEdit),
                forge::lipsync::encodeInfo(frameEdit)).frames.size()==2,
                "edited line roundtrip");
        const auto scratchBase=fs::temp_directory_path()/
            ("fableforge_lipsync_write_"+std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(scratchBase);
        const auto scratchInput=scratchBase/"source.big";
        const auto scratchOutput=scratchBase/"output.big";
        {
            const auto image=synthetic.serialize();
            std::ofstream stream(scratchInput,std::ios::binary);
            stream.write(reinterpret_cast<const char*>(image.data()),
                         std::streamsize(image.size()));
            require(bool(stream),"synthetic source write");
        }
        const std::array edits={forge::lipsync::ArchiveEdit{
            "LIPSYNC_ENGLISH_MAIN",1,frameEdit},
            forge::lipsync::ArchiveEdit{"LIPSYNC_ENGLISH_SCRIPT",1,entry}};
        forge::lipsync::writeScratchArchive(scratchInput,scratchOutput,edits);
        const auto exported=forge::big::File::open(scratchOutput);
        const auto* exportedMain=exported.findBank("LIPSYNC_ENGLISH_MAIN");
        require(exportedMain && exportedMain->entries.size()==2 &&
                exported.entryData(exportedMain->entries[0])==forge::lipsync::encode(frameEdit) &&
                exported.findBank("LIPSYNC_ENGLISH_SCRIPT")->entries.size()==1,
                "scratch output reparses and preserves the other sub-bank");
        rejected=false;
        try {forge::lipsync::writeScratchArchive(scratchInput,scratchOutput,edits);}
        catch(const std::runtime_error&) {rejected=true;}
        require(rejected,"scratch writer overwrote an existing output");
        fs::remove(scratchOutput);
        fs::remove(scratchInput);
        fs::remove(scratchBase);
        rejected=false;
        try {forge::lipsync::upsert(synthetic,"LIPSYNC_ENGLISH_MISSING",2,entry);}
        catch(const std::runtime_error&) {rejected=true;}
        require(rejected,"missing bank accepted");

        if(argc>1) {
            const auto bank=forge::big::File::openFully(fs::path(argv[1]));
            size_t exact=0,empty=0;
            for(const auto& sub:bank.banks()) {
                if(sub.name.rfind("LIPSYNC",0)!=0) continue;
                for(const auto& record:sub.entries) {
                    if(record.type!=1 || record.length==0) {++empty;continue;}
                    const auto payload=bank.entryData(record);
                    const auto decoded=forge::lipsync::decode(payload,record.subHeader);
                    if(forge::lipsync::encode(decoded)!=payload ||
                       forge::lipsync::encodeInfo(decoded)!=record.subHeader)
                        throw std::runtime_error("retail byte difference in "+sub.name+"/"+record.name);
                    ++exact;
                }
            }
            require(exact>10000,"too few retail lipsync entries checked");
            std::cout << "lipsync: " << exact << " retail entries byte exact, "
                      << empty << " empty/non-type-1 skipped\n";

            auto output=bank;
            const auto* sourceMain=bank.findBank("LIPSYNC_ENGLISH_MAIN");
            require(sourceMain && !sourceMain->entries.empty(),"retail MAIN bank missing");
            const auto& sourceEntry=sourceMain->entries.front();
            auto edited=forge::lipsync::decode(bank.entryData(sourceEntry),sourceEntry.subHeader);
            require(!edited.frames.empty() && !edited.frames[0].empty(),"retail donor lacks first key");
            edited.frames[0][0].weight ^= 1;
            const auto modified=forge::lipsync::upsert(output,sourceMain->name,sourceEntry.id,edited);
            uint32_t nextId=0;
            for(const auto& candidate:sourceMain->entries) nextId=std::max(nextId,candidate.id);
            ++nextId;
            const auto inserted=forge::lipsync::upsert(output,sourceMain->name,nextId,edited);
            require(!modified.added && inserted.added && inserted.name=="Dialogue_"+std::to_string(nextId),
                    "retail upsert routing");

            const auto bytes=output.serialize();
            const bool keepScratch=argc>2;
            const auto scratch=keepScratch ? fs::path(argv[2]) : fs::temp_directory_path()/
                ("fableforge_lipsync_upsert_"+std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count())+".big");
            {
                std::ofstream out(scratch,std::ios::binary);
                require(bool(out),"cannot open scratch archive");
                out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
                require(bool(out),"cannot write scratch archive");
            }
            const auto reread=forge::big::File::openFully(scratch);
            require(reread.banks().size()==bank.banks().size(),"rebuilt bank count");
            size_t preserved=0;
            for(const auto& sourceBank:bank.banks()) {
                const auto* rebuilt=reread.findBank(sourceBank.name);
                require(rebuilt && rebuilt->entries.size()==sourceBank.entries.size()+
                        size_t(sourceBank.name==sourceMain->name) &&
                        rebuilt->id==sourceBank.id && rebuilt->blockSize==sourceBank.blockSize,
                        "rebuilt bank metadata/count");
                for(size_t i=0;i<sourceBank.entries.size();++i) {
                    const auto& before=sourceBank.entries[i];
                    const auto& after=rebuilt->entries[i];
                    require(before.id==after.id && before.name==after.name &&
                            before.type==after.type && before.magic==after.magic &&
                            before.devFileType==after.devFileType && before.devCrc==after.devCrc &&
                            before.devSources==after.devSources,
                            "rebuilt metadata changed");
                    if(sourceBank.name==sourceMain->name && before.id==sourceEntry.id) {
                        require(reread.entryData(after)==forge::lipsync::encode(edited),"retail edit missing");
                    } else {
                        require(reread.entryData(after)==bank.entryData(before) &&
                                after.subHeader==before.subHeader,"untouched retail entry changed");
                        ++preserved;
                    }
                }
            }
            const auto* rebuiltMain=reread.findBank(sourceMain->name);
            require(rebuiltMain->entries.back().id==nextId &&
                    rebuiltMain->entries.back().name==inserted.name &&
                    reread.entryData(rebuiltMain->entries.back())==forge::lipsync::encode(edited),
                    "retail add missing");
            std::error_code cleanupError;
            if(!keepScratch) fs::remove(scratch,cleanupError);
            std::cout << "lipsync: scratch archive edit/add readable; " << preserved
                      << " original records preserved\n";
        } else std::cout << "lipsync: fixtures passed (pass dialogue.big for retail corpus)\n";
        return 0;
    } catch(const std::exception& ex) {
        std::cerr << "lipsync: " << ex.what() << '\n';
        return 1;
    }
}
