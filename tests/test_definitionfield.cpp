#include "definitionfield.hpp"
#include <iostream>

int main() try {
    const auto require=[](bool ok,const char* message) {if(!ok) throw std::runtime_error(message);};
    forge::defdecode::DecodedField field;field.name="TextValue";field.type="CWideString";
    const std::vector<std::pair<std::string,std::vector<uint8_t>>> valid={
        {"",{0,0}}, {"A",{65,0,0,0}}, {"caf\xc3\xa9",{99,0,97,0,102,0,0xe9,0,0,0}},
        {"\xce\xa9",{0xa9,3,0,0}}, {"\xe6\xbc\xa2",{0x22,0x6f,0,0}},
        {"\xf0\x9f\x99\x82",{0x3d,0xd8,0x42,0xde,0,0}}
    };
    for(const auto& [text,bytes]:valid) {
        field.value=bytes;
        require(albion::definitionwrite::fieldEditable(field),"Valid bounded Unicode original refused");
        require(albion::definitionwrite::fieldValue(field)==text,"Draft conversion changed original text");
    }
    const std::vector<std::vector<uint8_t>> invalid={
        {},{65},{65,0},{0,0xd8,0,0},{0,0xdc,0,0},{0,0xd8,65,0,0,0},
        {65,0,0,0,66,0,0,0},{0,0,0xff,0xff},{10,0,0,0},{127,0,0,0},{128,0,0,0},{159,0,0,0}
    };
    for(const auto& bytes:invalid) {
        field.value=bytes;
        require(!albion::definitionwrite::fieldEditable(field),"Malformed/control/trailing wide original accepted");
    }
    const std::string suffix="\xe6\xbc\xa2\xf0\x9f\x99\x82";
    for(size_t prefix:{size_t(120),size_t(121)}) {
        field.value.clear();forge::unicode::appendUtf16z(field.value,std::string(prefix,'X')+suffix);
        require(albion::definitionwrite::fieldEditable(field)==(prefix==120),"UTF-8 byte draft limit differs");
    }
    field.type="CCharString";field.value={0xe9,0};
    require(!albion::definitionwrite::fieldEditable(field),"Narrow legacy bytes became Unicode editable");
    field.value={65,0};require(albion::definitionwrite::fieldEditable(field),"Narrow ASCII regression");
    const std::vector<std::string> invalidText={std::string("A\0B",3),"\x80","\xc0\x80","\xc2","\xed\xa0\x80","\xf4\x90\x80\x80","\xc2\x80","\xc2\x9f"};
    for(const auto& text:invalidText)
        require(!albion::definitionwrite::printableWideText(text),"Malformed/control UTF-8 draft accepted");
    forge::bin::File groups;groups.addEntry("THING_GROUP","3",{});groups.addEntry("ENGINE_THEME_GROUP","THEMES",{});
    forge::bin::Entry owner;owner.definition="OBJECT";
    for(const auto* category:{"OBJECT","BUILDING","CREATURE"}) {
        owner.definition=category;
        require(albion::definitionread::groupChoice(groups,owner,0) && !albion::definitionread::groupChoice(groups,owner,1),"Thing group family differs");
    }
    owner.definition="ENGINE_THEME";
    require(!albion::definitionread::groupChoice(groups,owner,0) && albion::definitionread::groupChoice(groups,owner,1),"Theme group family differs");
    require(!albion::definitionread::groupChoice(groups,owner,size_t(-1)),"Out-of-range group accepted");
    owner.definition="UNKNOWN";require(!albion::definitionread::groupFamily(owner),"Unknown category inferred");
    std::cout<<"Definition draft Unicode, exact originals, malformed data, byte limits and group families: PASS\n";
    return 0;
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
