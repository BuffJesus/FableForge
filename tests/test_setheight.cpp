#include "leveledit.hpp"
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
using albion::editor::Document;
using albion::editor::Frame;
namespace fs=std::filesystem;
int checks=0;
#define CHECK(x) do { ++checks; if(!(x)) throw std::runtime_error(std::string("line ")+std::to_string(__LINE__)+": " #x); } while(false)
std::string thing(int uid,int x,bool locked=false) {
 return "NewThing AICreature;\r\nUID "+std::to_string(uid)+";\r\nDefinitionType \"TEST\";\r\nObjectScale 2.5;\r\nStartCTCPhysicsNavigator;\r\nPositionX "+std::to_string(x)+";\r\nPositionY 3;\r\nPositionZ 20;\r\nRHSetForwardX 0;\r\nRHSetForwardY 1;\r\nRHSetForwardZ 0;\r\nRHSetUpX 0;\r\nRHSetUpY 0;\r\nRHSetUpZ 1;\r\nEndCTCPhysicsNavigator;\r\nStartCTCEditor;\r\nLockedInPlace "+(locked?"TRUE":"FALSE")+";\r\nEndCTCEditor;\r\nInitialPosX 99;\r\nInitialPosZ 19;\r\nUnknownData 321.125;\r\nEndThing;\r\n";
}
void level(const fs::path& path) {
 std::vector<uint8_t> b;
 auto u32=[&](uint32_t n){for(int k=0;k<4;++k)b.push_back(uint8_t(n>>(8*k)));};
 u32(25);b.push_back(4);b.push_back(25);b.insert(b.end(),3,0);
 u32(0);u32(0);u32(0);u32(0);
 b.insert(b.end(),{22,8,0,0,8});u32(7);u32(0);u32(8);u32(8);b.push_back(0);
 b.insert(b.end(),33792,0);u32(1);u32(1);b.insert(b.end(),33792,0);
 for(int y=0;y<9;++y)for(int x=0;x<9;++x){uint8_t cell[21]={};float z=float(x+2)/2048;std::memcpy(cell+5,&z,4);cell[13]=255;b.insert(b.end(),cell,cell+21);}
 uint32_t nav=uint32_t(b.size());std::memcpy(b.data()+21,&nav,4);u32(0);u32(0);
 std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(b.data()),std::streamsize(b.size()));CHECK(bool(out));
}
int main(){
 fs::path scratch;
 try {
  auto stamp=std::chrono::high_resolution_clock::now().time_since_epoch().count();
  for(int i=0;i<100;++i){auto p=fs::temp_directory_path()/("forge-setheight-"+std::to_string(stamp)+"-"+std::to_string(i));if(fs::create_directory(p)){scratch=p;break;}}
  CHECK(!scratch.empty());level(scratch/"slope.lev");
  const std::string original="Version 2;\r\n"+thing(1,2)+thing(2,6)+thing(3,3,true)+"NewThing Marker;\r\nUID 4;\r\nEndThing;\r\n"+thing(5,100);
  Document d;std::string error;CHECK(d.openText("Height",original,error));
  CHECK(!d.setHeight(0,12)&&d.text()==original&&!d.canUndo());
  CHECK(d.loadLevel(scratch/"slope.lev",error));
  CHECK(d.groundHeight(2,3)==4 && d.groundHeight(6,3)==8);
  auto rev=d.revision();
  CHECK(!d.setHeight(0,std::numeric_limits<float>::quiet_NaN()));
  CHECK(!d.setHeight(0,std::numeric_limits<float>::infinity()));
  CHECK(!d.setHeight(2,12)&&!d.setHeight(3,12)&&!d.setHeight(99,12)&&!d.setHeight(4,12));
  CHECK(d.text()==original&&d.revision()==rev&&!d.canUndo());
  CHECK(d.setHeight(0,20)&&d.revision()==rev&&!d.canUndo()&&d.text()==original);
  d.beginBatch();CHECK(d.setHeight(0,-10));CHECK(d.setHeight(1,6));CHECK(!d.setHeight(2,6));d.endBatch();
  Frame a,b,c;CHECK(d.frameOf(0,a)&&d.frameOf(1,b)&&d.frameOf(2,c));
  CHECK(a.pos[2]==4&&b.pos[2]==8&&c.pos[2]==20);
  CHECK(a.pos[0]==2&&a.pos[1]==3&&a.forward[0]==0&&a.forward[1]==1&&a.up[2]==1&&a.scale==2.5f);
  CHECK(std::stof(*d.file().things()[0].find("InitialPosZ"))==4);
  CHECK(std::stof(*d.file().things()[0].find("InitialPosX"))==2&&!d.file().things()[0].find("InitialPosY"));
  CHECK(d.text().find("UnknownData 321.125;\r\n")!=std::string::npos);
  auto changed=d.text();rev=d.revision();CHECK(d.setHeight(0,-100)&&d.revision()==rev&&d.text()==changed);
  CHECK(d.undo()&&d.text()==original&&!d.canUndo());CHECK(d.redo()&&d.text()==changed);
  CHECK(d.setHeight(1,14)&&d.frameOf(1,b)&&b.pos[2]==14);CHECK(d.undo()&&d.text()==changed);
  CHECK(d.undo()&&d.text()==original);rev=d.revision();CHECK(d.setHeight(0,20)&&d.revision()==rev&&d.canRedo());
  // Background writes own their LEV, and completion advances only the saved
  // baseline. A later stroke and its undo must remain in the active document.
  CHECK(d.setVertexHeights({{2,3,12}}));
  auto written=d.terrainWriteSnapshot();
  CHECK(d.setVertexHeights({{2,3,16}}));
  CHECK(written.groundHeight(2,3)==12 && d.groundHeight(2,3)==16);
  CHECK(written.saveTerrainLoose(scratch/"output",error));
  CHECK(d.terrainDirty() && d.acceptTerrainWrite(written) && d.terrainDirty());
  CHECK(d.savedTerrain()->heights[3*d.cellsX()+2]==12);
  CHECK(d.undo() && !d.terrainDirty() && d.groundHeight(2,3)==12);
  CHECK(d.undo() && d.terrainDirty() && d.groundHeight(2,3)==4);
  CHECK(d.redo() && !d.terrainDirty());
  auto repeat=d.terrainWriteSnapshot();
  CHECK(repeat.saveTerrainLoose(scratch/"repeat",error));
  CHECK(d.acceptTerrainWrite(repeat) && !d.terrainDirty());
  CHECK(d.loadLevel(scratch/"slope.lev",error));
  CHECK(!d.acceptTerrainWrite(written) && !d.terrainDirty());
  auto oldSession=d.terrainWriteSnapshot();
  CHECK(d.openText("Height",original,error));
  CHECK(!d.acceptTerrainWrite(oldSession));
  fs::remove_all(scratch);std::cout<<checks<<" set-height checks passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<" (scratch retained: "<<scratch<<")\n";return 1;}
}
