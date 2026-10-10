#include "forge/big.hpp"
#include "forge/bigstorage.hpp"
#include "forge/temporarydirectory.hpp"
#include "forge/textbig.hpp"
#include <fstream>
#include <chrono>
#include <iostream>
#include <stdexcept>

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
static uint32_t word(const std::vector<uint8_t>& bytes, size_t at) {
    require(at <= bytes.size() && bytes.size() - at >= 4, "truncated test word");
    return uint32_t(bytes[at]) | (uint32_t(bytes[at+1]) << 8) |
        (uint32_t(bytes[at+2]) << 16) | (uint32_t(bytes[at+3]) << 24);
}
static void patch(std::vector<uint8_t>& bytes, size_t at, uint32_t value) {
    for (unsigned i=0; i<4; ++i) bytes.at(at+i) = uint8_t(value>>(8*i));
}
static void storageChecks(const std::string& bankName, uint32_t bankVersion) {
    forge::big::File file; file.setMagic("BIGB"); file.setVersion(100);
    auto& bank = file.addBank(bankName,bankVersion); bank.blockSize = 32;
    forge::big::Entry entry;
    entry.magic=42; entry.id=1; entry.name="a"; entry.data={1,2,3}; bank.entries.push_back(entry);
    entry.id=4; entry.name="b"; entry.data={4,5}; bank.entries.push_back(entry);
    const auto bytes=file.serialize();
    forge::TemporaryDirectory directory(std::filesystem::temp_directory_path(),"forge-big-storage");
    const auto path=directory.path()/"test.big";
    auto accepts=[&](const std::vector<uint8_t>& data) {
        { std::ofstream out(path,std::ios::binary); out.write(reinterpret_cast<const char*>(data.data()),data.size()); }
        try { const auto result=forge::bigstorage::validateEnvelope(path);
              return result.banks==1 && result.entries==2 && result.payloadBytes==5 && result.zeroPaddingBytes==45;
        } catch (const std::runtime_error&) { return false; }
    };
    require(accepts(bytes),"supported BIGB envelope refused");
    const size_t footer=word(bytes,8), bankFields=footer+4+bank.name.size()+1;
    const size_t table=word(bytes,bankFields+8), first=table+12, second=first+41;
    for (const auto [at,value] : std::vector<std::pair<size_t,uint32_t>>{
        {4,101},{12,0},{footer,2},{bankFields,123},{bankFields+4,3},{bankFields+8,0},
        {bankFields+12,4},{bankFields+16,3},{table+8,3},{first,0},{second+4,1},
        {first+12,0xffffffffu},{first+16,33},{second+16,32}}) {
        auto bad=bytes; patch(bad,at,value); require(!accepts(bad),"malformed BIGB envelope accepted");
    }
    auto bad=bytes; bad[16]=1; require(!accepts(bad),"unowned BIGB bytes accepted");
    bad=bytes;bad[second+28]='a';require(!accepts(bad),"duplicate BIGB names accepted");
    bad=bytes;bad.push_back(0);require(!accepts(bad),"trailing BIGB byte accepted");
    for (size_t length=0; length<bytes.size(); ++length)
        require(!accepts({bytes.begin(),bytes.begin()+length}),"truncated BIGB accepted");
}
static void textDecodeTruncationChecks() {
    const std::vector<uint8_t> narrators={2,0,0,0,'H','E','R','O',0,'G','U','I','L','D',0};
    require(forge::textbig::decodeNarrators(narrators)==std::vector<std::string>{"HERO","GUILD"},
            "narrator names or header order changed");
    require(forge::textbig::decodeNarrators({0,0,0,0}).empty(),"empty narrator list refused");
    require(forge::textbig::decodeNarrators({1,0,0,0,0})==std::vector<std::string>{""},
            "empty narrator name refused");
    auto refusesNarrators=[](const std::vector<uint8_t>& header) {
        try { (void)forge::textbig::decodeNarrators(header); }
        catch(const std::runtime_error&) { return true; }
        return false;
    };
    for(size_t length=0;length<narrators.size();++length)
        require(refusesNarrators({narrators.begin(),narrators.begin()+length}),"truncated narrator header accepted");
    require(refusesNarrators({255,255,255,255}),"unbounded narrator count accepted");
    require(refusesNarrators({0,0,0,0,0}),"trailing narrator bytes accepted");
    require(refusesNarrators({1,0,0,0,128,0}),"non-ASCII narrator name accepted");
    forge::textbig::Entry entry; entry.content="A complete line";
    entry.speechBank="Dialogue"; entry.speaker="HERO"; entry.identifier="TXT_TEST";
    entry.tags.push_back({0,"ANIM:SCRIPT_CHEER_1"});
    const auto bytes=forge::textbig::encode(entry);
    require(forge::textbig::decode(bytes,0).tags.front().name==entry.tags.front().name,
            "complete tagged string refused");
    for(size_t length=0;length<bytes.size();++length) {
        bool refused=false;
        try { (void)forge::textbig::decode({bytes.begin(),bytes.begin()+length},0); }
        catch(const std::runtime_error&) { refused=true; }
        require(refused,"truncated tagged text payload accepted");
    }
}
static void textInsertionChecks() {
    for (const auto& magic : {std::string("BIGB"),std::string("B\0\0\0",4)}) {
        forge::big::File file; file.setMagic(magic);
        auto& bank=file.addBank("TEXT_ENGLISH_MAIN",106);
        forge::big::Entry donor; donor.name="donor"; donor.id=1; donor.magic=0;
        donor.data={1,2,3}; bank.entries.push_back(donor);
        forge::textbig::Entry value; value.content="New line";
        forge::textbig::upsertString(file,"new",value);
        require(bank.entries.size()==2 && bank.entries[1].magic==(magic=="BIGB"?42u:0u),
                "new text entry has wrong container marker");
        require(bank.entries[0].magic==0 && bank.entries[0].data==donor.data,"text donor was changed");
    }
    forge::big::File exhausted; exhausted.setMagic("BIGB");
    auto& bank=exhausted.addBank("TEXT_ENGLISH_MAIN",106);
    forge::textbig::Entry value; value.content="Line"; value.identifier="donor";
    forge::big::Entry donor; donor.name="donor"; donor.id=UINT32_MAX; donor.magic=42;
    donor.data=forge::textbig::encode(value); bank.entries.push_back(donor);
    const auto before=exhausted.serialize();
    auto refuses=[&](std::optional<uint32_t> id) {
        try { forge::textbig::upsertString(exhausted,"new",value,id); }
        catch (const std::runtime_error&) { return true; }
        return false;
    };
    require(refuses(std::nullopt),"automatic text ID allocation wrapped");
    require(exhausted.serialize()==before,"failed automatic allocation changed bank");
    require(refuses(0) && refuses(UINT32_MAX),"invalid explicit text ID accepted");
    require(exhausted.serialize()==before,"failed explicit allocation changed bank");
    const auto added=forge::textbig::upsertString(exhausted,"new",value,7);
    require(added.added && added.id==7 && bank.entries.size()==2 && bank.entries[0].id==UINT32_MAX,
            "free explicit text ID refused or donor changed");
    const auto stable=exhausted.serialize();
    for (int field=0; field<5; ++field) {
        auto invalid=value;const std::string nul("a\0b",3);
        if(field==0)invalid.content=nul;
        if(field==1)invalid.speechBank=nul;
        if(field==2)invalid.speaker=nul;
        if(field==3)invalid.identifier=nul;
        if(field==4)invalid.tags.push_back({0,nul});
        bool refused=false;
        try { forge::textbig::upsertString(exhausted,"donor",invalid); }
        catch(const std::runtime_error& error) { refused=std::string(error.what()).find("embedded NUL")!=std::string::npos; }
        require(refused && exhausted.serialize()==stable,"unrepresentable string changed text bank");
    }
}
static void backingSerializationChecks() {
    forge::big::File source; source.setMagic("BIGB");
    auto& bank=source.addBank("BACKING",122); bank.blockSize=32;
    for (uint32_t i=0;i<128;++i) {
        forge::big::Entry entry; entry.magic=42; entry.id=i+1;
        entry.name="entry_"+std::to_string(i); entry.subHeader={uint8_t(i),0,7};
        entry.data=std::vector<uint8_t>(i%17,uint8_t(i)); bank.entries.push_back(entry);
    }
    const auto bytes=source.serialize();
    forge::TemporaryDirectory directory(std::filesystem::temp_directory_path(),"forge-big-backing");
    const auto path=directory.path()/"source.big";
    auto write=[&](const auto& data) {
        std::ofstream out(path,std::ios::binary); out.write(reinterpret_cast<const char*>(data.data()),data.size());
        require(bool(out),"backing fixture write failed");
    };
    write(bytes);
    auto lazy=forge::big::File::open(path), eager=forge::big::File::openFully(path);
    require(lazy.serialize()==bytes && eager.serialize()==bytes,"backed no-op serialization changed bytes");
    lazy.banks()[0].entries[3].data={9,8,7}; eager.banks()[0].entries[3].data={9,8,7};
    require(lazy.serialize()==eager.serialize(),"lazy and eager edited serialization differ");
    write(std::vector<uint8_t>(bytes.begin(),bytes.begin()+20));
    bool refused=false;
    try { (void)lazy.serialize(); } catch(const std::runtime_error&) { refused=true; }
    require(refused,"truncated backing payload accepted");
    std::filesystem::remove(path);
    require(eager.serialize().size()>20,"eager serialization requires original file");
    refused=false;
    try { (void)lazy.serialize(); } catch(const std::runtime_error&) { refused=true; }
    require(refused,"missing backing payload accepted");
}
static void textDonorChecks() {
    forge::big::File file;file.setMagic("BIGB");
    forge::textbig::Entry value;value.content="Line";
    forge::big::Entry entry;entry.name="same";entry.id=1;entry.magic=42;
    entry.data=forge::textbig::encode(value);entry.subHeader={1,0,0,0};
    file.addBank("FIRST",106).entries.push_back(entry);
    entry.id=2;entry.subHeader={2,0,0,0};file.addBank("SECOND",106).entries.push_back(entry);
    const auto before=file.serialize();bool refused=false;
    try { forge::textbig::upsertString(file,"new",value,std::nullopt,"same"); }
    catch(const std::runtime_error& error) { refused=std::string(error.what()).find("ambiguous donor")!=std::string::npos; }
    require(refused && file.serialize()==before,"ambiguous named donor silently selected");
    // The documented automatic donor policy still chooses the first type-0 row.
    const auto automatic=forge::textbig::upsertString(file,"new",value);
    require(automatic.bank=="FIRST" && file.banks()[0].entries.back().subHeader==std::vector<uint8_t>({1,0,0,0}),
            "automatic donor policy changed");
    value.content="Replacement";
    require(!forge::textbig::upsertString(file,"new",value,std::nullopt,"same").added,
            "irrelevant ambiguous donor blocked replacement");
}
int main(int argc, char** argv) { try {
    if (argc == 3 && std::string_view(argv[1]) == "--envelope") {
        const auto result=forge::bigstorage::validateEnvelope(argv[2]);
        std::cout << "banks=" << result.banks << " entries=" << result.entries
                  << " payload_bytes=" << result.payloadBytes << " zero_padding=" << result.zeroPaddingBytes << '\n';
        return 0;
    }
    if (argc == 3) {
        const std::filesystem::path destination = argv[2];
        require(!std::filesystem::exists(destination), "scratch output must be new");
        const auto started=std::chrono::steady_clock::now();
        const auto bytes = forge::big::File::open(argv[1]).serialize();
        const auto serialized=std::chrono::steady_clock::now();
        std::cout << "Serialization seconds: " << std::chrono::duration<double>(serialized-started).count() << std::endl;
        std::ofstream output(destination, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        require(bool(output), "scratch output write failed");
        output.close(); require(bool(output),"scratch output close failed");
        std::cout << "Write seconds: " << std::chrono::duration<double>(std::chrono::steady_clock::now()-serialized).count() << std::endl;
        std::cout << "Serialized scratch bank: " << bytes.size() << " bytes\n";
        return 0;
    }
    require(argc == 1, "usage: fableforge_bigwrite_tests [input scratch-output]");
    storageChecks("GBANK_MAIN_PC",122);
    storageChecks("PARTICLE_MAIN_PC",111);
    textDecodeTruncationChecks();
    textInsertionChecks();
    textDonorChecks();
    backingSerializationChecks();
    forge::big::File file;
    file.setMagic("BIGB"); file.setVersion(100); file.setContentType(999);
    auto& bank = file.addBank("TEST", 106); bank.blockSize = 2048;
    forge::big::Entry entry;
    entry.magic = 42; entry.id = 1; entry.name = "first"; entry.data = {1,2,3};
    bank.entries.push_back(entry);
    entry.id = 2; entry.name = "second"; entry.data = {4,5};
    bank.entries.push_back(entry);
    entry.id = 3; entry.name = "empty"; entry.data.clear();
    bank.entries.push_back(entry);
    const auto bytes = file.serialize();
    const auto directory = word(bytes, 8);
    require(word(bytes,12) == bytes.size()-directory, "BIGB directory length is stale");
    require(word(bytes,directory) == 1, "wrong bank count");
    // Independent bounded traversal of the emitted table and payloads.
    size_t table = word(bytes,directory+4+5+8);
    const size_t tableEnd = table + word(bytes,directory+4+5+12);
    require(tableEnd == directory, "table extent mismatch");
    require(word(bytes,table) == 1 && word(bytes,table+4) == 0 && word(bytes,table+8) == 3,
            "histogram mismatch");
    table += 12;
    size_t previousEnd = 16;
    for (size_t i=0; i<bank.entries.size(); ++i) {
        const auto& expected = bank.entries[i];
        require(word(bytes,table) == 42 && word(bytes,table+4) == expected.id, "entry identity changed");
        const auto length = word(bytes,table+12), offset = word(bytes,table+16);
        require(offset % 2048 == 0 && offset >= previousEnd, "BIGB payload alignment ignored");
        require(length == expected.data.size() && uint64_t(offset)+length <= directory, "payload extent mismatch");
        for (size_t p=previousEnd; p<offset; ++p) require(bytes[p] == 0, "nonzero alignment padding");
        for (size_t p=0; p<length; ++p) require(bytes[offset+p] == expected.data[p], "payload changed");
        previousEnd = size_t(offset)+length;
        table += 28 + word(bytes,table+24);
        require(word(bytes,table+4) == 0, "unexpected dependencies"); table += 8;
        table += 4 + word(bytes,table);
    }
    require(table == tableEnd, "entry table not consumed exactly");
    file.addBank("A_LONGER_EMPTY_BANK", 1);
    const auto appended = file.serialize();
    require(word(appended,12) == appended.size()-word(appended,8), "added bank did not resize directory header");
    // FMP's fourth word is a content kind, not the retail directory length.
    file.setMagic(std::string("B\0\0\0",4)); file.setContentType(510);
    require(word(file.serialize(),12) == 510, "FMP content type overwritten");
    std::cout << "BIGB directory sizing, alignment, payload preservation and FMP header checks passed\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; } }
