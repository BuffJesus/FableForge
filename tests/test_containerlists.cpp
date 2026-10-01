#include "leveledit.hpp"
#include "forge/tng.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::exit(1); } } while(false)
int main() {
    const std::string original="Version 2;\r\nXXXSectionStart NULL;\r\nNewThing Object;\r\nUID 1;\r\nDefinitionType \"OBJECT_BARREL_BREAKABLE\";\r\nStartCTCContainerRewardHero;\r\nEndCTCContainerRewardHero;\r\nStartCTCEditor;\r\nEndCTCEditor;\r\nCustomField 123;\r\nEndThing;\r\nXXXSectionEnd;\r\n";
    albion::editor::Document doc; std::string error;
    CHECK(doc.openText("Containers",original,error));
    CHECK(doc.ctcBlocksOf(0)==std::vector<std::string>({"CTCContainerRewardHero","CTCEditor"}));
    CHECK(doc.ctcBlocksOf(9).empty());
    CHECK(!doc.addListEntry(0,"CTCChest","ContainerContents","\"OBJECT_APPLE\""));
    CHECK(!doc.canUndo());
    for (const auto* name:{"OBJECT_APPLE","OBJECT_CARROT","OBJECT_RED_MEAT"})
        CHECK(doc.addListEntry(0,"CTCContainerRewardHero","ContainerContents",std::string("\"")+name+"\""));
    CHECK(doc.text().find("StartCTCContainerRewardHero;\r\nContainerContents[0]")!=std::string::npos);
    CHECK(doc.removeListEntry(0,"CTCContainerRewardHero","ContainerContents",1));
    CHECK(doc.listEntries(0,"CTCContainerRewardHero","ContainerContents")==std::vector<std::string>({"\"OBJECT_APPLE\"","\"OBJECT_RED_MEAT\""}));
    const auto parsed=forge::tng::File::parseText(doc.text());
    CHECK(parsed.things()[0].find("CustomField").value()=="123");
    CHECK(parsed.things()[0].findCtc("CTCContainerRewardHero")->properties.size()==2);
    CHECK(doc.text().find("ContainerContents[2]")==std::string::npos);
    CHECK(doc.setPropertyValue(0,"CTCContainerRewardHero","ContainerContents[0]","\"OBJECT_CARROT\""));
    CHECK(doc.undo() && doc.undo() && doc.undo() && doc.undo() && doc.undo());
    CHECK(doc.text()==original);
    CHECK(!doc.canUndo());
    CHECK(doc.addListEntry(0,"CTCContainerRewardHero","ContainerContents","\"OBJECT_APPLE\""));
    CHECK(doc.removeListEntry(0,"CTCContainerRewardHero","ContainerContents",0));
    CHECK(doc.listEntries(0,"CTCContainerRewardHero","ContainerContents").empty());
    CHECK(doc.text()==original);
    CHECK(doc.undo() && doc.listEntries(0,"CTCContainerRewardHero","ContainerContents").size()==1);
    for (const auto& body:std::vector<std::string>{
        "ContainerContents[1] \"OBJECT_APPLE\";\r\n",
        "ContainerContents[0] \"OBJECT_APPLE\";\r\nContainerContents[0] \"OBJECT_CARROT\";\r\n",
        "ContainerContents[00] \"OBJECT_APPLE\";\r\n",
        "ContainerContents[0] 42;\r\n"}) {
        auto malformed=original;
        malformed.insert(malformed.find("EndCTCContainerRewardHero;"),body);
        CHECK(doc.openText("Malformed",malformed,error));
        CHECK(!doc.listEditable(0,"CTCContainerRewardHero","ContainerContents"));
        CHECK(!doc.addListEntry(0,"CTCContainerRewardHero","ContainerContents","\"OBJECT_APPLE\""));
        CHECK(!doc.removeListEntry(0,"CTCContainerRewardHero","ContainerContents",0));
        CHECK(doc.text()==malformed && !doc.canUndo());
    }
    auto duplicateBlock=original;
    duplicateBlock.insert(duplicateBlock.find("StartCTCEditor;"),"StartCTCContainerRewardHero;\r\nEndCTCContainerRewardHero;\r\n");
    CHECK(doc.openText("DuplicateBlock",duplicateBlock,error));
    CHECK(!doc.listEditable(0,"CTCContainerRewardHero","ContainerContents"));
    CHECK(doc.openText("Clean",original,error));
    CHECK(!doc.addListEntry(0,"CTCContainerRewardHero","ContainerContents","\"OBJECT_APPLE\"\nInjected TRUE"));
    CHECK(doc.text()==original && !doc.canUndo());
    std::cout << "Container list editing and byte-exact undo passed\n";
}
