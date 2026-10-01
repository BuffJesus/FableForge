#include "leveledit.hpp"
#include "forge/tng.hpp"
#include <chrono>
#include <cstring>
#include <fstream>
#include <cstdlib>
#include <iostream>
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::exit(1); } } while(false)

std::string thing(int uid,const std::string& editor,const char* physics="CTCPhysicsStandard") {
    return "NewThing Object;\r\nUID "+std::to_string(uid)+";\r\nDefinitionType \"OBJECT_BARREL_BREAKABLE\";\r\nPlayer -1;\r\nObjectScale 1.0;\r\nStart"+physics+";\r\nPositionX 2.0;\r\nPositionY 3.0;\r\nPositionZ 4.0;\r\nRHSetForwardX 0.0;\r\nRHSetForwardY 1.0;\r\nRHSetForwardZ 0.0;\r\nRHSetUpX 0.0;\r\nRHSetUpY 0.0;\r\nRHSetUpZ 1.0;\r\nEnd"+physics+";\r\n"+editor+"CustomField 321;\r\nEndThing;\r\n";
}
std::filesystem::path flatLevel() {
    // Minimal version-6404 LEV, 8x8 ground with all vertices at z=4.
    std::vector<uint8_t> b;
    auto u32=[&](uint32_t n) {for(int k=0;k<4;++k)b.push_back(uint8_t(n>>(8*k)));};
    u32(25);b.push_back(4);b.push_back(25);b.insert(b.end(),3,0);
    u32(0);u32(0);u32(0);u32(0);
    b.insert(b.end(),{22,8,0,0,8});u32(7);u32(0);u32(8);u32(8);b.push_back(0);
    b.insert(b.end(),33792,0);u32(1);u32(1);b.insert(b.end(),33792,0);
    for(int i=0;i<81;++i) {
        uint8_t cell[21]={};const float z=4.f/2048;std::memcpy(cell+5,&z,4);
        cell[13]=255;b.insert(b.end(),cell,cell+21);
    }
    const uint32_t nav=uint32_t(b.size());std::memcpy(b.data()+21,&nav,4);u32(0);u32(0);
    const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path=std::filesystem::temp_directory_path()/("forge-lock-"+std::to_string(stamp)+".lev");
    std::ofstream(path,std::ios::binary).write(reinterpret_cast<const char*>(b.data()),std::streamsize(b.size()));
    return path;
}
int main() {
    using albion::editor::Document;using albion::editor::Frame;
    const auto empty=std::string("StartCTCEditor;\r\nEndCTCEditor;\r\n");
    const auto locked=std::string("StartCTCEditor;\r\nLockedInPlace TRUE;\r\nEndCTCEditor;\r\n");
    const auto original="Version 2;\r\n"+thing(1,locked)+thing(2,empty,"CTCPhysicsNavigator")+thing(3,"");
    Document doc;std::string error;CHECK(doc.openText("Locks",original,error));
    CHECK(doc.isLocked(0) && !doc.isLocked(1) && !doc.isLocked(2) && !doc.isLocked(999));
    CHECK(!doc.setLocked(2,true) && !doc.setLocked(999,true));
    CHECK(doc.setLocked(0,true) && !doc.canUndo());
    Frame frame;CHECK(doc.frameOf(0,frame));frame.pos[0]+=8;frame.scale=2;frame.forward[0]=1;
    doc.setFrame(0,frame);doc.remove(0);doc.setProperty(0,"ObjectScale","3");
    CHECK(!doc.setPropertyValue(0,"CTCPhysicsStandard","PositionX","7.0"));
    CHECK(!doc.setPropertyValue(0,"CTCPhysicsStandard","RHSetUpZ","0.0"));
    CHECK(!doc.setPropertyValue(0,"","ObjectScale","3.0"));
    CHECK(doc.text()==original && !doc.canUndo());
    // Content ownership remains editable; locking protects placement, not the whole object.
    CHECK(doc.setPropertyValue(0,"","Player","2"));CHECK(doc.undo() && doc.text()==original);
    CHECK(doc.setLocked(0,false) && !doc.isLocked(0));
    doc.setFrame(0,frame);CHECK(doc.undo() && doc.undo() && doc.text()==original);
    CHECK(doc.redo() && !doc.isLocked(0));CHECK(doc.undo());
    CHECK(doc.setLocked(1,true) && doc.isLocked(1));
    CHECK(!doc.setPropertyValue(1,"CTCPhysicsNavigator","PositionZ","9.0"));
    CHECK(doc.undo() && doc.text()==original);
    // Mixed groups move/delete the unlocked member, and remain a single undo step.
    doc.beginBatch();doc.setFrame(0,frame);doc.setFrame(1,frame);doc.endBatch();
    Frame unchanged,moved;CHECK(doc.frameOf(0,unchanged) && doc.frameOf(1,moved));
    CHECK(unchanged.pos[0]==2 && moved.pos[0]==10);CHECK(doc.undo() && doc.text()==original);
    doc.beginBatch();doc.remove(1);doc.remove(0);doc.endBatch();
    CHECK(doc.thingCount()==2 && doc.uidOf(0)==1);CHECK(doc.undo() && doc.text()==original);
    // Copies are creation operations and retain lock metadata; movement requires unlock.
    auto copy=doc.duplicate(0);CHECK(doc.thingCount()==4 && doc.isLocked(copy));CHECK(doc.undo());
    const auto fragment=doc.extract({0});const float at[]={20,30,40};
    const auto pasted=doc.paste(fragment,at,false);CHECK(pasted.size()==1);
    CHECK(doc.isLocked(pasted[0]) && doc.frameOf(pasted[0],moved) && moved.pos[0]==20);
    CHECK(doc.undo() && doc.text()==original);
    // Explicit property unlock remains usable.
    CHECK(doc.setPropertyValue(0,"CTCEditor","LockedInPlace","FALSE") && !doc.isLocked(0));
    doc.remove(0);CHECK(doc.thingCount()==2);CHECK(doc.undo() && doc.undo() && doc.text()==original);
    // Terrain reseating also observes locks and synchronizes creature initial position.
    const std::string initial="InitialPosX 2;\r\nInitialPosY 3;\r\nInitialPosZ 4;\r\n";
    auto floating=thing(3,empty);
    floating.replace(floating.find("PositionZ 4.0;"),std::strlen("PositionZ 4.0;"),"PositionZ 12.0;");
    auto ownedChild=thing(4,"StartCTCOwnedEntity;\r\nOwnerUID 2;\r\nEndCTCOwnedEntity;\r\n"+locked);
    ownedChild.replace(ownedChild.find("PositionZ 4.0;"),std::strlen("PositionZ 4.0;"),"PositionZ 12.0;");
    auto grandchild=thing(5,"StartCTCOwnedEntity;\r\nOwnerUID 4;\r\nEndCTCOwnedEntity;\r\n"+empty);
    CHECK(doc.openText("Reseat","Version 2;\r\n"+thing(1,locked+initial)+thing(2,empty+initial,"CTCPhysicsNavigator")+floating+ownedChild+grandchild,error));
    const auto path=flatLevel();CHECK(doc.loadLevel(path,error));
    const auto before=doc.terrain();std::vector<Document::VertexHeight> edits;
    for(int y=0;y<9;++y)for(int x=0;x<9;++x)edits.push_back({x,y,9});
    CHECK(doc.setVertexHeights(edits));
    CHECK(doc.reseatThings(before)==0);   // grounded things already follow the height edit
    Frame stillFloating,child,nested;
    CHECK(doc.frameOf(0,unchanged) && doc.frameOf(1,moved) && doc.frameOf(2,stillFloating) && doc.frameOf(3,child) && doc.frameOf(4,nested));
    CHECK(unchanged.pos[2]==4 && moved.pos[2]==9);
    CHECK(stillFloating.pos[2]==12 && child.pos[2]==17 && nested.pos[2]==9);
    const auto parsed=forge::tng::File::parseText(doc.text());
    CHECK(std::stof(*parsed.things()[0].find("InitialPosZ"))==4);
    CHECK(std::stof(*parsed.things()[1].find("InitialPosZ"))==9);
    CHECK(doc.undo() && doc.frameOf(1,moved) && doc.frameOf(3,child) && doc.frameOf(4,nested) && moved.pos[2]==4 && child.pos[2]==12 && nested.pos[2]==4);
    for(auto& edit:edits)edit.h=4.5f;
    CHECK(doc.setVertexHeights(edits));
    CHECK(doc.frameOf(1,moved) && doc.frameOf(3,child) && doc.frameOf(4,nested) && moved.pos[2]==4.5f && child.pos[2]==12.5f && nested.pos[2]==4.5f);
    CHECK(doc.reseatThings(before)==0);  // the manual repair action must not double a small lift
    CHECK(doc.undo() && doc.frameOf(1,moved) && doc.frameOf(3,child) && doc.frameOf(4,nested) && moved.pos[2]==4 && child.pos[2]==12 && nested.pos[2]==4);
    albion::editor::TerrainBrush brush;
    brush.mode=albion::editor::TerrainBrush::Mode::Raise;
    brush.x=4;brush.y=4;brush.radius=20;brush.exactStep=true;brush.step=5;
    doc.beginStroke(brush);doc.applyBrush(brush,0.016f);doc.endStroke();
    CHECK(doc.frameOf(0,unchanged) && doc.frameOf(1,moved) && doc.frameOf(2,stillFloating) && doc.frameOf(3,child) && doc.frameOf(4,nested));
    CHECK(unchanged.pos[2]==4 && moved.pos[2]==9);
    CHECK(stillFloating.pos[2]==12 && child.pos[2]==17 && nested.pos[2]==9);
    CHECK(doc.undo() && doc.frameOf(1,moved) && doc.frameOf(3,child) && doc.frameOf(4,nested) && moved.pos[2]==4 && child.pos[2]==12 && nested.pos[2]==4);
    // An incomplete pack write must preserve any prior output and leave the
    // terrain edit unsaved so the user can retry after fixing the source bank.
    CHECK(doc.setVertexHeights(edits) && doc.terrainDirty());
    const auto pack=std::filesystem::path(path.string()+".pack");
    const auto levOut=pack/"data"/"Levels"/"FinalAlbion"/"Reseat.lev";
    const auto chunkOut=pack/"stb"/"Reseat.chunk";
    const auto recordOut=pack/"stb"/"Reseat.record";
    std::filesystem::create_directories(levOut.parent_path());
    std::filesystem::create_directories(chunkOut.parent_path());
    std::ofstream(levOut,std::ios::binary)<<"old lev";
    std::ofstream(chunkOut,std::ios::binary)<<"old chunk";
    std::ofstream(recordOut,std::ios::binary)<<"old record";
    std::vector<std::string> notes;
    CHECK(!doc.deployTerrainToPack(path.parent_path(),pack,notes,error));
    auto contents=[](const std::filesystem::path& p) {std::ifstream f(p,std::ios::binary);return std::string(std::istreambuf_iterator<char>(f),{});};
    CHECK(contents(levOut)=="old lev" && contents(chunkOut)=="old chunk" && contents(recordOut)=="old record");
    CHECK(doc.terrainDirty() && notes.size()==1);
    std::filesystem::remove(levOut);std::filesystem::remove(chunkOut);std::filesystem::remove(recordOut);
    notes.clear();error.clear();
    CHECK(!doc.deployTerrainToPack(path.parent_path(),pack,notes,error));
    CHECK(!std::filesystem::exists(levOut) && !std::filesystem::exists(chunkOut) && !std::filesystem::exists(recordOut));
    CHECK(doc.terrainDirty() && notes.size()==1);
    const auto game=std::filesystem::path(path.string()+".game");
    const auto gameLev=game/"data"/"Levels"/"FinalAlbion"/"Reseat.lev";
    notes.clear();error.clear();
    CHECK(!doc.deployTerrain(game,notes,error));
    CHECK(!std::filesystem::exists(gameLev) && doc.terrainDirty());
    std::filesystem::remove(gameLev.parent_path());std::filesystem::remove(gameLev.parent_path().parent_path());
    std::filesystem::remove(gameLev.parent_path().parent_path().parent_path());std::filesystem::remove(game);
    std::filesystem::remove(levOut.parent_path());std::filesystem::remove(levOut.parent_path().parent_path());
    std::filesystem::remove(levOut.parent_path().parent_path().parent_path());std::filesystem::remove(chunkOut.parent_path());
    std::filesystem::remove(pack);
    std::filesystem::remove(path);
    std::cout << "locked things checks passed\n";
}
