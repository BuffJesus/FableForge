#include "leveledit.hpp"
#include <algorithm>
#include <cstdio>
#include <stdexcept>

using albion::editor::Document;
namespace {
std::string fixture(const std::string& blocks) {
    return "Version 2;\r\nXXXSectionStart NULL;\r\nNewThing Object;\r\nUID 42;\r\n"
        "DefinitionType \"OBJECT_FIXTURE\";\r\nUnknownTop \"retain me\";\r\n"+blocks+
        "StartCTCEditor;\r\nLockedInPlace TRUE;\r\nUnknownEditor 77;\r\nEndCTCEditor;\r\n"
        "Health 13.500;\r\nEndThing;\r\nXXXSectionEnd;\r\n";
}
const Document::KnownProperty* find(const std::vector<Document::KnownProperty>& rows,const char* ctc,const char* key) {
    const auto found=std::find_if(rows.begin(),rows.end(),[&](const auto& row){return row.row.ctc==ctc && row.row.key==key;});
    return found==rows.end()?nullptr:&*found;
}
}
int main() {
    int checks=0,failures=0;
    auto check=[&](bool okay,const char* label){++checks;if(!okay){++failures;std::fprintf(stderr,"component overrides: %s\n",label);}};
    try {
        const std::string initial=fixture("StartCTCDoor;\r\nOpaqueDoor 19.250;\r\nEndCTCDoor;\r\n"
            "StartCTCStockItem;\r\nPrice 25;\r\nOpaqueStock \"odd value\";\r\nEndCTCStockItem;\r\n"
            "StartCTCDRegionExit;\r\nEndCTCDRegionExit;\r\n"
            "StartCTCLight;\r\nOverridden FALSE;\r\nEndCTCLight;\r\n"
            "StartCTCActionUseScriptedHook;\r\nEndCTCActionUseScriptedHook;\r\n"
            "StartCTCCreatureGenerator;\r\nCreatureFamilies[0] \"FAMILY_UNKNOWN\";\r\nEndCTCCreatureGenerator;\r\n");
        Document doc; std::string error;
        if(!doc.openText("fixture",initial,error))throw std::runtime_error(error);
        auto rows=doc.knownComponentProperties(0);
        const auto* missing=find(rows,"CTCDoor","Open");
        check(missing&&!missing->present&&missing->row.value.empty()&&missing->row.kind==Document::PropertyRow::Kind::Bool,"missing bool carries no invented default");
        const auto* explicitPrice=find(rows,"CTCStockItem","Price");
        check(explicitPrice&&explicitPrice->present&&explicitPrice->row.value=="25","explicit override value retained");
        check(!find(rows,"CTCLight","Overridden")&&!find(rows,"CTCActionUseScriptedHook","Usable")&&!find(rows,"CTCCreatureGenerator","CreatureFamilies[n]"),"coupled fields and indexed lists excluded");
        check(std::all_of(rows.begin(),rows.end(),[](const auto& row){return !row.row.ctc.empty() && row.row.ctc!="CTCEditor";}),"general and editor controls excluded");
        const auto legacy=doc.propertiesOf(0);
        check(std::none_of(legacy.begin(),legacy.end(),[](const auto& row){return row.ctc=="CTCDoor"&&row.key=="Open";}),"legacy properties list remains explicit only");
        check(doc.text()==initial&&!doc.canUndo(),"enumeration does not mutate document");
        check(doc.isLocked(0)&&doc.setComponentOverride(0,"CTCDoor","Open","TRUE"),"nontransform override allowed on locked thing");
        const auto set=doc.text();
        std::string expected=initial; expected.insert(expected.find("EndCTCDoor;"),"Open TRUE;\r\n");
        check(set==expected,"insertion changes only intended line");
        check(doc.undo()&&doc.text()==initial,"override undo byte exact");
        check(doc.redo()&&doc.text()==set,"override redo byte exact");
        check(doc.resetComponentOverride(0,"CTCDoor","oPeN")&&doc.text()==initial,"reset removes explicit override preserving unknown bytes");
        check(doc.undo()&&doc.text()==set,"reset undo restores explicit override");
        check(doc.redo()&&doc.text()==initial,"reset redo exact");
        const auto revision=doc.revision();
        check(doc.resetComponentOverride(0,"CTCDoor","Open")&&doc.revision()==revision,"reset absent field is a no-op");
        check(!doc.setComponentOverride(0,"CTCDoor","Open","1")&&!doc.setComponentOverride(0,"CTCDoor","Open","TRUE;\nEndThing;"),"bool validates syntax and rejects injection");
        check(!doc.setComponentOverride(0,"CTCStockItem","Price","1.5")&&!doc.setComponentOverride(0,"CTCStockItem","Price","2147483648"),"integer rejects fractional and overflowing values");
        check(!doc.setComponentOverride(0,"CTCStockItem","Price","-1")&&!doc.setComponentOverride(0,"CTCStockItem","Price","1000001"),"integer range enforced");
        check(!doc.setComponentOverride(0,"CTCDRegionExit","Radius","NaN")&&!doc.setComponentOverride(0,"CTCDRegionExit","Radius","inf")&&!doc.setComponentOverride(0,"CTCDRegionExit","Radius","1e300"),"float rejects nonfinite and overflow");
        check(!doc.setComponentOverride(0,"CTCDRegionExit","Radius","0")&&!doc.setComponentOverride(0,"CTCDRegionExit","Radius","22.01"),"float metadata range enforced");
        check(doc.text()==initial&&doc.revision()==revision,"rejected edits leave text and history unchanged");
        check(doc.setComponentOverride(0,"CTCDRegionExit","Radius","0.01"),"float lower range endpoint accepted");
        check(doc.undo()&&doc.text()==initial,"float insert undo exact");
        check(doc.setComponentOverride(0,"CTCStockItem","Price","1000000"),"integer upper endpoint accepted");
        check(doc.undo()&&doc.text()==initial,"explicit integer edit undo exact");
        rows=doc.knownComponentProperties(0);
        const auto* radius=find(rows,"CTCCreatureGenerator","GenerationRadius");
        check(radius&&radius->row.kind==Document::PropertyRow::Kind::Float,"integer radius widget metadata retains float serialization type");
        check(doc.setComponentOverride(0,"CTCCreatureGenerator","GenerationRadius","10.5"),"float-serialized generation radius accepts fraction");
        check(doc.undo()&&doc.text()==initial,"fractional generation radius undo exact");
        check(doc.setComponentOverride(0,"CTCCreatureGenerator","SelfTriggerRadius","10.5"),"float-serialized self-trigger radius accepts fraction");
        check(doc.undo()&&doc.text()==initial,"fractional self-trigger radius undo exact");
        check(!doc.setComponentOverride(0,"CTCSearchableContainer","NumberOfTimesToSearch","2")&&!doc.resetComponentOverride(0,"CTCSearchableContainer","NumberOfTimesToSearch"),"absent component never created");
        check(!doc.setComponentOverride(0,"CTCDoor","DoorTriggerType","1")&&!doc.setComponentOverride(0,"","ObjectScale","2")&&!doc.setComponentOverride(0,"CTCEditor","LockedInPlace","FALSE"),"unknown fields and transforms cannot bypass allowlist or lock");
        check(doc.isLocked(0)&&doc.text()==initial,"lock and unknown data preserved");
        check(doc.knownComponentProperties(99).empty()&&!doc.setComponentOverride(99,"CTCDoor","Open","TRUE"),"invalid thing rejected");

        const auto duplicate=fixture("StartCTCDoor;\r\nEndCTCDoor;\r\nStartCTCDoor;\r\nEndCTCDoor;\r\n");
        if(!doc.openText("duplicate",duplicate,error))throw std::runtime_error(error);
        check(doc.knownComponentProperties(0).empty()&&!doc.setComponentOverride(0,"CTCDoor","Open","FALSE"),"duplicate component blocks rejected");
        const auto duplicateKeys=fixture("StartCTCDoor;\r\nOpen TRUE;\r\nopen FALSE;\r\nEndCTCDoor;\r\n");
        if(!doc.openText("duplicatekeys",duplicateKeys,error))throw std::runtime_error(error);
        check(doc.knownComponentProperties(0).empty()&&!doc.resetComponentOverride(0,"CTCDoor","Open"),"case-insensitive duplicate keys rejected");
        check(doc.text()==duplicateKeys&&!doc.canUndo(),"ambiguous fixture untouched");
        std::string lf=fixture("StartCTCDoor;\r\nEndCTCDoor;\r\n");
        lf.erase(std::remove(lf.begin(),lf.end(),'\r'),lf.end());
        if(!doc.openText("lf",lf,error))throw std::runtime_error(error);
        check(doc.setComponentOverride(0,"CTCDoor","Open","FALSE")&&doc.text().find('\r')==std::string::npos,"line ending style preserved");
        check(doc.undo()&&doc.text()==lf,"LF fixture byte-exact undo");
        check(doc.setComponentOverride(0,"CTCDoor","Open","true")&&doc.text().find("Open TRUE;")!=std::string::npos,"accepted boolean normalized for existing GUI");
        const auto lowerKey=fixture("StartCTCDoor;\r\nopen TRUE;\r\nEndCTCDoor;\r\n");
        if(!doc.openText("lowerkey",lowerKey,error))throw std::runtime_error(error);
        check(doc.resetComponentOverride(0,"CTCDoor","Open")&&doc.text().find("open TRUE;")==std::string::npos,"reset removes actual lowercase source key");
        check(doc.undo()&&doc.text()==lowerKey,"lowercase key reset undo byte exact");
        const auto explosion=fixture("StartCTCExplodingObject;\r\nRadius 10.5;\r\nEndCTCExplodingObject;\r\n");
        if(!doc.openText("explosion",explosion,error))throw std::runtime_error(error);
        check(doc.setComponentOverride(0,"CTCExplodingObject","Radius","11.5"),"explicit float-serialized explosion radius accepts fraction");
        check(doc.undo()&&doc.text()==explosion,"fractional explosion radius undo exact");
    } catch(const std::exception& error) {++failures;std::fprintf(stderr,"component overrides exception: %s\n",error.what());}
    std::printf("component overrides: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
