#include "leveledit.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using albion::editor::Document;
int checks=0;
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error(std::to_string(__LINE__)+": " #x);}while(false)
std::string owned(const std::string& value){return "StartCTCOwnedEntity;\r\nOwnerUID "+value+";\r\nEndCTCOwnedEntity;\r\n";}
std::string thing(const std::string& uid,const std::string& body={}){return "NewThing Object;\r\nUID "+uid+";\r\n"+body+"EndThing;\r\n";}
int main(){try{
 Document d;std::string error;
 auto open=[&](const std::string& text){CHECK(d.openText("Ownership","Version 2;\r\n"+text,error));};
 auto equal=[&](std::vector<size_t> roots,std::vector<size_t> wanted){const auto before=d.text();const auto rev=d.revision();const auto actual=d.ownedDescendants(roots);if(actual!=wanted){std::cerr<<"check "<<checks<<" actual:";for(auto i:actual)std::cerr<<i<<",";std::cerr<<" expected:";for(auto i:wanted)std::cerr<<i<<",";std::cerr<<"\n";}CHECK(actual==wanted);CHECK(d.text()==before&&d.revision()==rev&&!d.canUndo());};
 open(thing("1")+thing("2",owned("1"))+thing("3",owned("2"))+thing("4")+thing("5",owned("4")));
 equal({0},{1,2});equal({0,3},{1,4,2});equal({0,1,0,999},{2});equal({},{});equal({999},{});
 open(thing("1",owned("3"))+thing("2",owned("1"))+thing("3",owned("2"))+thing("4",owned("4")));
 equal({0},{1,2});equal({0,2},{1});equal({3},{});
 // Ambiguous identity poisons edges even if only one duplicate has an owner.
 open(thing("1")+thing("2",owned("1"))+thing("2")+thing("3",owned("2")));
 equal({0},{});equal({1},{});
 open(thing("1")+thing("2",owned("1")+"uid 22;\r\n")+thing("3",owned("2")));
 equal({0},{});
 // Duplicate blocks/fields, including equal values and mixed case, are ambiguous.
 open(thing("1")+thing("2",owned("1")+owned("1"))+thing("3","StartCTCOwnedEntity;\r\nOwnerUID 1;\r\nowneruid 1;\r\nEndCTCOwnedEntity;\r\n")+thing("4",owned("2")));
 equal({0},{});
 // Zero, signed, overflowing and partially parsed identifiers never become links.
 for(const auto value:{"0","-1","+1","1junk","18446744073709551616"}){
  open(thing("1")+thing("2",owned(value)));equal({0},{});
  open(thing(value)+thing("2",owned(value)));equal({0},{});
 }
 open(thing("18446744073709551615")+thing("2",owned("\"18446744073709551615\"")));
 equal({0},{1});
 // Frames and editor locks do not affect this read-only graph query.
 open(thing("1")+thing("2","StartCTCOWNEDENTITY;\r\nowneruid 1;\r\nEndCTCOWNEDENTITY;\r\nStartCTCEditor;\r\nLockedInPlace TRUE;\r\nEndCTCEditor;\r\n"));equal({0},{1});
 open(thing("1")+thing("2","StartCTCOwnedEntity;\r\nEndCTCOwnedEntity;\r\n")+thing("3",owned("99")));equal({0},{});
 // Owner motion is a rigid parent delta, recursively applied. A child lock
 // blocks direct edits but not its owner's native movement path.
 auto physics=[](float x,float y,float z){return "StartCTCPhysicsStandard;\r\nPositionX "+std::to_string(x)+";\r\nPositionY "+std::to_string(y)+";\r\nPositionZ "+std::to_string(z)+";\r\nEndCTCPhysicsStandard;\r\n";};
 open(thing("1",physics(0,0,0))+thing("2",owned("1")+physics(2,0,1)+"StartCTCEditor;\r\nLockedInPlace TRUE;\r\nEndCTCEditor;\r\n")+
      thing("3",owned("2")+physics(3,0,1))+thing("4",physics(4,0,0)));
 CHECK(d.isLocked(1));
 albion::editor::Frame root,child,grandchild,unowned;
 CHECK(d.frameOf(0,root)&&d.frameOf(1,child)&&d.frameOf(2,grandchild)&&d.frameOf(3,unowned));
 const std::string baseline=d.text();
 root.pos[0]=10;root.forward[0]=0;root.forward[1]=1;
 auto changed=d.ownedFramesAfter({{0,root}});
 CHECK(changed.size()==2&&changed[0].first==1&&changed[1].first==2);
 CHECK(std::abs(changed[0].second.pos[0]-10)<.001f&&std::abs(changed[0].second.pos[1]-2)<.001f);
 CHECK(std::abs(changed[1].second.pos[0]-10)<.001f&&std::abs(changed[1].second.pos[1]-3)<.001f);
 CHECK(std::abs(changed[0].second.pos[2]-1)<.001f&&std::abs(changed[1].second.pos[2]-1)<.001f);
 CHECK(std::abs(changed[0].second.forward[0])<.001f&&std::abs(changed[0].second.forward[1]-1)<.001f);
 d.setFrame(1,changed[0].second);CHECK(d.text()==baseline);
 d.beginBatch();d.setFrame(0,root);for(const auto& [index,frame]:changed)d.setOwnedFrame(index,frame);d.endBatch();
 CHECK(d.frameOf(1,child)&&d.frameOf(2,grandchild)&&d.frameOf(3,unowned));
 CHECK(std::abs(child.pos[1]-2)<.001f&&std::abs(grandchild.pos[1]-3)<.001f&&unowned.pos[0]==4);
 d.undo();CHECK(d.text()==baseline);
 root.pos[0]=0;root.forward[0]=1;root.forward[1]=0;root.scale=2;
 changed=d.ownedFramesAfter({{0,root}});
 CHECK(changed.size()==2&&std::abs(changed[0].second.pos[0]-2)<.001f&&std::abs(changed[1].second.pos[0]-3)<.001f);
 // Section relocation preserves each block verbatim and follows nested owners.
 const std::string sectioned="XXXSectionStart NULL;\r\n"+
     thing("1", "UnknownProperty keep_me;\r\n")+thing("4")+
     "XXXSectionEnd;\r\nXXXSectionStart Q_TEST;\r\n"+
     thing("2",owned("1"))+thing("3",owned("2"))+thing("5",owned("99"))+
     "XXXSectionEnd;\r\n";
 open(sectioned);
 const std::string sectionBaseline=d.text();
 auto rootIndex=d.indexOfUid(1);
 CHECK(rootIndex.has_value());
 const auto moved=d.moveToSection(*rootIndex,"Q_TEST");
 CHECK(moved&&d.uidOf(*moved)==1);
 for(uint64_t uid:{1ull,2ull,3ull}) {const auto i=d.indexOfUid(uid);CHECK(i&&d.sectionOf(*i)=="Q_TEST");}
 CHECK(d.sectionOf(*d.indexOfUid(4))=="NULL"&&d.sectionOf(*d.indexOfUid(5))=="Q_TEST");
 CHECK(d.text().find("UnknownProperty keep_me;\r\n")!=std::string::npos);
 CHECK(d.undo()&&d.text()==sectionBaseline);
 // A root already in the target section still brings its children across.
 rootIndex=d.indexOfUid(1);
 const auto kept=d.moveToSection(*rootIndex,"NULL");
 CHECK(kept&&d.uidOf(*kept)==1);
 CHECK(d.sectionOf(*d.indexOfUid(2))=="NULL"&&d.sectionOf(*d.indexOfUid(3))=="NULL");
 CHECK(d.sectionOf(*d.indexOfUid(5))=="Q_TEST");
 CHECK(d.undo()&&d.text()==sectionBaseline);
 // Day/night creates the destination section and relocates the same tree.
 rootIndex=d.indexOfUid(1);
 const auto day=d.setDayNight(*rootIndex,1);
 CHECK(day&&d.uidOf(*day)==1);
 for(uint64_t uid:{1ull,2ull,3ull}) {const auto i=d.indexOfUid(uid);CHECK(i&&d.sectionOf(*i)=="NULL%DayOnly");}
 CHECK(d.undo()&&d.text()==sectionBaseline);
 // Native owner deletion includes locked children; keeping them detaches only
 // the direct survivor, so the nested child remains linked to its live parent.
 open(thing("1")+thing("2",owned("1")+"StartCTCEditor;\r\nLockedInPlace TRUE;\r\nEndCTCEditor;\r\n")+
      thing("3",owned("2"))+thing("4"));
 const std::string deleteBaseline=d.text();
 CHECK(d.removeWithOwned({0},true)==3);
 CHECK(d.thingCount()==1&&d.uidOf(0)==4);
 CHECK(d.undo()&&d.text()==deleteBaseline);
 CHECK(d.removeWithOwned({0},false)==1);
 CHECK(d.thingCount()==3);
 const auto childIndex=d.indexOfUid(2),grandchildIndex=d.indexOfUid(3);
 CHECK(childIndex&&grandchildIndex);
 bool childDetached=false,grandchildLinked=false;
 for(const auto& link:d.linksOf(*childIndex)) if(link.field=="OwnerUID") childDetached=link.target==0;
 for(const auto& link:d.linksOf(*grandchildIndex)) if(link.field=="OwnerUID") grandchildLinked=link.target==2;
 CHECK(childDetached&&grandchildLinked);
 for(const auto& issue:d.validate()) CHECK(issue.rule!="LINK");
 CHECK(d.undo()&&d.text()==deleteBaseline);
 CHECK(d.removeWithOwned({1},true)==0&&d.text()==deleteBaseline);
 open(thing("1")+thing("2","StartCTCOWNEDENTITY;\r\nowneruid 1;\r\nEndCTCOWNEDENTITY;\r\n"));
 CHECK(d.removeWithOwned({0},false)==1);
 CHECK(d.thingCount()==1);
 bool lowerDetached=false;
 for(const auto& link:d.linksOf(0)) if(link.field=="OwnerUID") lowerDetached=link.target==0;
 CHECK(lowerDetached);
 open(thing("1")+thing("2",owned("1"))+thing("3",owned("2")));
 CHECK(d.removeWithOwned({0,1},false)==2);
 CHECK(d.thingCount()==1&&d.uidOf(0)==3);
 bool grandchildDetached=false;
 for(const auto& link:d.linksOf(0)) if(link.field=="OwnerUID") grandchildDetached=link.target==0;
 CHECK(grandchildDetached);
 // A single copy does not stay attached to its source's owner. Copying the
 // selected chain reconnects only the copies, leaving original links intact.
 open(thing("1")+thing("2",owned("1"))+thing("3",owned("2"))+thing("4"));
 const std::string copyBaseline=d.text();
 const size_t lone=d.duplicate(1);
 CHECK(d.uidOf(lone)!=2);
 bool loneDetached=false;
 for(const auto& link:d.linksOf(lone)) if(link.field=="OwnerUID") loneDetached=link.target==0;
 CHECK(loneDetached);
 CHECK(d.undo()&&d.text()==copyBaseline);
 const auto copies=d.duplicateGroup({0,1,2});
 CHECK(copies.size()==3);
 const uint64_t newRoot=d.uidOf(copies[0]),newChild=d.uidOf(copies[1]),newGrandchild=d.uidOf(copies[2]);
 CHECK(newRoot!=1&&newChild!=2&&newGrandchild!=3&&newRoot!=newChild&&newChild!=newGrandchild);
 bool childRemapped=false,grandchildRemapped=false;
 for(const auto& link:d.linksOf(copies[1])) if(link.field=="OwnerUID") childRemapped=link.target==newRoot;
 for(const auto& link:d.linksOf(copies[2])) if(link.field=="OwnerUID") grandchildRemapped=link.target==newChild;
 CHECK(childRemapped&&grandchildRemapped);
 CHECK(d.ownedDescendants({copies[0]}).size()==2);
 CHECK(d.undo()&&d.text()==copyBaseline);
 // A copied internal route follows its copied target. A route to an object
 // outside the selection continues to name that original object.
 const std::string routeTo2="StartCTCPreCalculatedNavigationRoute;\r\nThingToCalculateRouteToUID 2;\r\nEndCTCPreCalculatedNavigationRoute;\r\n";
 const std::string routeTo4="StartCTCPreCalculatedNavigationRoute;\r\nThingToCalculateRouteToUID 4;\r\nEndCTCPreCalculatedNavigationRoute;\r\n";
 open(thing("1",routeTo2)+thing("2")+thing("3",routeTo4)+thing("4"));
 const std::string routeBaseline=d.text();
 const auto routeCopies=d.duplicateGroup({0,1,2});
 CHECK(routeCopies.size()==3);
 bool internalRoute=false,externalRoute=false;
 for(const auto& link:d.linksOf(routeCopies[0]))
     if(link.field=="ThingToCalculateRouteToUID") internalRoute=link.target==d.uidOf(routeCopies[1]);
 for(const auto& link:d.linksOf(routeCopies[2]))
     if(link.field=="ThingToCalculateRouteToUID") externalRoute=link.target==4;
 CHECK(internalRoute&&externalRoute);
 CHECK(d.undo()&&d.text()==routeBaseline);
 // Reciprocal spouse references must move together, without altering the
 // original couple or leaving one copy attached to an original.
 const auto creature=[](int uid,int spouse){return "NewThing AICreature;\r\nUID "+std::to_string(uid)+
     ";\r\nSpouseCreatureUID "+std::to_string(spouse)+";\r\nEndThing;\r\n";};
 open(creature(1,2)+creature(2,1));
 const std::string spouseBaseline=d.text();
 const auto spouses=d.duplicateGroup({0,1});
 CHECK(spouses.size()==2);
 CHECK(d.file().things()[spouses[0]].find("SpouseCreatureUID")==std::to_string(d.uidOf(spouses[1])));
 CHECK(d.file().things()[spouses[1]].find("SpouseCreatureUID")==std::to_string(d.uidOf(spouses[0])));
 CHECK(d.file().things()[0].find("SpouseCreatureUID")=="2");
 CHECK(d.file().things()[2].find("SpouseCreatureUID")=="1");
 CHECK(d.undo()&&d.text()==spouseBaseline);
 open(thing("1")+thing("2",owned("1"))+thing("3",owned("2"))+thing("4"));
 const auto partial=d.duplicateGroup({1,2});
 CHECK(partial.size()==2);
 bool partialRootDetached=false,partialChildLinked=false;
 for(const auto& link:d.linksOf(partial[0])) if(link.field=="OwnerUID") partialRootDetached=link.target==0;
 for(const auto& link:d.linksOf(partial[1])) if(link.field=="OwnerUID") partialChildLinked=link.target==d.uidOf(partial[0]);
 CHECK(partialRootDetached&&partialChildLinked);
 CHECK(d.undo()&&d.text()==copyBaseline);
 open(thing("1","StartCTCOwnedEntity;\r\nEndCTCOwnedEntity;\r\n"));
 const size_t missingLinkCopy=d.duplicate(0);
 CHECK(d.file().thingBlockText(missingLinkCopy).find("OwnerUID")==std::string::npos);
 // Deleting a target clears every unambiguous incoming field, including both
 // halves of the region-exit pair, without changing unrelated block text.
 open(thing("1")+
      thing("2","StartCTCVillageMember;\r\nVillageUID 1;\r\nEndCTCVillageMember;\r\n")+
      thing("3",owned("1"))+
      thing("4","StartCTCDRegionExit;\r\nEntranceConnectedToUID 1;\r\nEndCTCDRegionExit;\r\nStartCTCActionUseScriptedHook;\r\nEntranceConnectedToUID 1;\r\nEndCTCActionUseScriptedHook;\r\n")+
      thing("5","HomeBuildingUID 1;\r\nUnknownProperty keep_me;\r\n"));
 const std::string linksBaseline=d.text();
 CHECK(d.remove(0)==5);
 CHECK(d.thingCount()==4);
 for(size_t i=0;i<d.thingCount();++i)
     for(const auto& link:d.linksOf(i)) CHECK(link.target!=1);
 CHECK(d.file().thingBlockText(*d.indexOfUid(4)).find("EntranceConnectedToUID 1;")==std::string::npos);
 CHECK(d.text().find("UnknownProperty keep_me;")!=std::string::npos);
 for(const auto& issue:d.validate()) CHECK(issue.rule!="LINK");
 CHECK(d.undo()&&d.text()==linksBaseline);
 open(thing("1")+thing("2",owned("1"))+
      thing("3","StartCTCVillageMember;\r\nVillageUID 1;\r\nEndCTCVillageMember;\r\n"));
 size_t clearedOnSurvivors=0;
 CHECK(d.removeWithOwned({0},true,&clearedOnSurvivors)==2);
 CHECK(clearedOnSurvivors==1&&d.thingCount()==1&&d.uidOf(0)==3);
 for(const auto& link:d.linksOf(0)) CHECK(link.target!=1);
 // A surviving duplicate UID still names a possible target; leave its links.
 open(thing("1")+thing("1")+thing("2",owned("1")));
 CHECK(d.remove(0)==0);
 bool stillLinked=false;
 for(const auto& link:d.linksOf(1)) if(link.field=="OwnerUID") stillLinked=link.target==1;
 CHECK(stillLinked);
 open(thing("1")+thing("2",owned("1junk"))+
      thing("3","StartCTCOwnedEntity;\r\nOwnerUID 1;\r\nowneruid 1;\r\nEndCTCOwnedEntity;\r\n"));
 CHECK(d.remove(0)==0);
 CHECK(d.text().find("OwnerUID 1junk;")!=std::string::npos);
 CHECK(d.text().find("OwnerUID 1;\r\nowneruid 1;")!=std::string::npos);
 // Area deletion includes helpers, skips locked things, detaches surviving
 // children and clears incoming links in one exact undo step.
 open(thing("1",physics(1,1,0))+thing("2",physics(2,2,0)+
      "StartCTCEditor;\r\nLockedInPlace TRUE;\r\nEndCTCEditor;\r\n")+
      thing("3",physics(3,3,0)+owned("1"))+
      thing("4",physics(9,9,0)+"StartCTCVillageMember;\r\nVillageUID 1;\r\nEndCTCVillageMember;\r\n"));
 CHECK((d.thingsInRect(2,2,0,0)==std::vector<size_t>{0,1}));
 const std::string areaBaseline=d.text();
 size_t skipped=0,areaCleared=0;
 CHECK(d.removeThingsInRect(2,2,0,0,&skipped,&areaCleared)==1);
 CHECK(skipped==1&&areaCleared==1&&d.thingCount()==3);
 CHECK(d.uidOf(0)==2&&d.uidOf(1)==3&&d.uidOf(2)==4);
 for(size_t i:{1u,2u}) for(const auto& link:d.linksOf(i)) CHECK(link.target!=1);
 CHECK(d.undo()&&d.text()==areaBaseline);
 CHECK(d.removeThingsInRect(0,0,0,0,&skipped,&areaCleared)==0&&skipped==0&&areaCleared==0);
 CHECK(!d.canUndo());
 // Track deletion repairs the two surviving segments in one undo step.
 open("");
 const size_t a=d.placeTrackNode(0,0,0,"TrackTempName1");
 const size_t b=d.placeTrackNode(1,0,0,"TrackTempName2");
 const size_t c=d.placeTrackNode(2,0,0,"TrackTempName3");
 std::string trackError;
 CHECK(d.linkTrackNodes(a,b,trackError)&&d.linkTrackNodes(b,c,trackError));
 const std::string trackBaseline=d.text();
 CHECK(d.remove(b)==0);
 CHECK(d.thingCount()==2);
 for(const auto& issue:d.validate()) CHECK(issue.rule!="TRACK");
 CHECK(d.undo()&&d.text()==trackBaseline);
 // A direct clone is a standalone node, while copy/brush and old clipboard
 // fragments refuse track nodes before they can carry asymmetric links.
 const size_t clone=d.duplicate(b);
 CHECK(d.uidOf(clone)!=d.uidOf(b));
 const auto cloneText=d.file().thingBlockText(clone);
 CHECK(cloneText.find("LinkedToUID1 0;")!=std::string::npos&&
       cloneText.find("LinkedToUID2 0;")!=std::string::npos&&
       cloneText.find("Start TRUE;")!=std::string::npos&&
       cloneText.find("End TRUE;")!=std::string::npos);
 CHECK(d.tracks().size()==2);
 for(const auto& issue:d.validate()) CHECK(issue.rule!="TRACK");
 CHECK(d.undo()&&d.text()==trackBaseline);
 CHECK(d.extract({a,b,c}).empty());
 const auto oldTrack=d.file().thingBlockText(b);
 albion::editor::Document::Fragment stale;
 stale.items.push_back({oldTrack,{},false});
 const size_t beforeTrackPaste=d.thingCount();
 const float at[3]={0,0,0};
 CHECK(d.paste(stale,at,false).empty()&&d.thingCount()==beforeTrackPaste);
 CHECK(d.text()==trackBaseline);
 // The native copy filter also excludes the other four thing classes.
 open("NewThing Village;\r\nUID 1;\r\nEndThing;\r\n"
      "NewThing Switch;\r\nUID 2;\r\nEndThing;\r\n"
      "NewThing PhysicalSwitch;\r\nUID 3;\r\nEndThing;\r\n"
      "NewThing Marker;\r\nUID 4;\r\nEndThing;\r\n"
      "NewThing Object;\r\nUID 5;\r\nEndThing;\r\n"
      "NewThing Object;\r\nUID 6;\r\nStartCTCCreatedEntity;\r\nEndCTCCreatedEntity;\r\nEndThing;\r\n");
 CHECK(d.extract({0,1,2,3,4,5}).items.size()==1);
 CHECK(d.isEditBrushCopyable(4)&&!d.isEditBrushCopyable(0)&&!d.isEditBrushCopyable(1)&&
       !d.isEditBrushCopyable(2)&&!d.isEditBrushCopyable(3)&&!d.isEditBrushCopyable(5));
 const std::string villageBaseline=d.text();
 bool villageRejected=false;
 try { d.duplicateGroup({4,0}); } catch(const std::invalid_argument&) { villageRejected=true; }
 CHECK(villageRejected&&d.text()==villageBaseline);
 std::cout<<checks<<" owned graph checks passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
