#include "leveledit.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace fs=std::filesystem;
using albion::editor::Document;
using albion::editor::Frame;
namespace {
std::string thing(const char* type,int uid,const std::string& initial) {
    const std::string physics=std::string(type)=="AICreature"?"CTCPhysicsNavigator":"CTCPhysicsStandard";
    return "NewThing "+std::string(type)+";\r\nPlayer 4;\r\nUID "+std::to_string(uid)+
        ";\r\nDefinitionType \"CREATURE_TEST\";\r\nScriptName NULL;\r\nStart"+physics+
        ";\r\nPositionX 10;\r\nPositionY 20;\r\nPositionZ 3;\r\n"
        "RHSetForwardX 0;\r\nRHSetForwardY 1;\r\nRHSetForwardZ 0;\r\n"
        "RHSetUpX 0;\r\nRHSetUpY 0;\r\nRHSetUpZ 1;\r\nEnd"+physics+";\r\n"+
        initial+"Health 37.125;\r\nEndThing;\r\n\r\n";
}
void write(const fs::path& path,const std::string& bytes) {
    std::ofstream stream(path,std::ios::binary); stream<<bytes;
    if(!stream) throw std::runtime_error("could not create scratch fixture");
}
bool value(const Document& doc,size_t index,const char* field,float expected) {
    const auto actual=doc.file().things().at(index).find(field);
    return actual && std::abs(std::stof(*actual)-expected)<1e-5f;
}
bool absent(const Document& doc,size_t index,const char* field) { return !doc.file().things().at(index).find(field); }
}
int main() {
    int checks=0,failures=0;
    auto check=[&](bool okay,const char* message){++checks;if(!okay){++failures;std::fprintf(stderr,"creature frame: %s\n",message);}};
    fs::path scratch;
    try {
        const auto stamp=std::chrono::high_resolution_clock::now().time_since_epoch().count();
        for(int attempt=0;attempt<100;++attempt) {
            const fs::path candidate=fs::temp_directory_path()/("fableforge-creatureframe-"+std::to_string(stamp)+"-"+std::to_string(attempt));
            if(fs::create_directory(candidate)) {scratch=fs::absolute(candidate);break;}
        }
        if(scratch.empty()) throw std::runtime_error("could not allocate unique scratch root");
        const fs::path levels=scratch/"data"/"Levels";
        fs::create_directories(levels/"FinalAlbion");
        write(levels/"FinalAlbion.wld",
            "START_INITIAL_QUESTS;\r\nEND_INITIAL_QUESTS;\r\nMapUIDCount 2;\r\nThingManagerUIDCount 10;\r\n"
            "NewMap 1;\r\nMapX 320;\r\nMapY 640;\r\nLevelName \"Source.lev\";\r\nMapUID 1;\r\nEndMap;\r\n"
            "NewMap 2;\r\nMapX 1024;\r\nMapY -512;\r\nLevelName \"Destination.lev\";\r\nMapUID 2;\r\nEndMap;\r\n");
        const std::string prefix="Version 2;\r\nXXXSectionStart NULL;\r\n";
        const std::string original=prefix+
            thing("AICreature",1,"InitialPosX 330.0;\r\nInitialPosY 660.0;\r\nInitialPosZ 3.0;\r\n")+
            thing("AICreature",2,"InitialPosX 330.000;\r\nInitialPosZ 3.000;\r\n")+
            thing("Object",3,"")+thing("AICreature",4,"")+"XXXSectionEnd;\r\n";
        const std::string empty=prefix+"XXXSectionEnd;\r\n";
        write(levels/"FinalAlbion"/"Source.tng",original);
        write(levels/"FinalAlbion"/"Destination.tng",empty);
        Document source,destination; std::string error;
        if(!source.open(scratch,"Source",{},error) || !destination.open(scratch,"Destination",{},error)) throw std::runtime_error(error);
        check(source.text()==original,"opening scratch loose TNG is byte exact");
        Frame frame; if(!source.frameOf(0,frame)) throw std::runtime_error("fixture frame missing");
        frame.pos[0]=15;frame.pos[1]=28;frame.pos[2]=7;
        source.setFrame(0,frame); const auto moved=source.text();
        check(value(source,0,"InitialPosX",335)&&value(source,0,"InitialPosY",668)&&value(source,0,"InitialPosZ",7),"moving creature updates existing world-space initial position");
        check(source.undo()&&source.text()==original,"move undo restores exact original bytes");
        check(source.redo()&&source.text()==moved,"move redo restores exact moved bytes");
        check(source.undo()&&source.text()==original,"restore source before partial-field case");
        source.frameOf(1,frame);frame.pos[0]=12;frame.pos[1]=23;frame.pos[2]=9; source.setFrame(1,frame);
        check(value(source,1,"InitialPosX",332)&&value(source,1,"InitialPosZ",9)&&absent(source,1,"InitialPosY"),"partial initial position updates only existing axes");
        check(source.undo()&&source.text()==original,"partial-axis undo byte exact");
        source.frameOf(2,frame);frame.pos[0]+=2;source.setFrame(2,frame);
        check(absent(source,2,"InitialPosX")&&absent(source,2,"InitialPosY")&&absent(source,2,"InitialPosZ"),"ordinary object gains no initial-position fields");
        check(source.undo()&&source.text()==original,"ordinary object undo byte exact");
        source.frameOf(3,frame);frame.pos[1]+=2;source.setFrame(3,frame);
        check(absent(source,3,"InitialPosX")&&absent(source,3,"InitialPosY")&&absent(source,3,"InitialPosZ"),"creature without initial fields keeps them absent");
        check(source.undo()&&source.text()==original,"absent-field undo byte exact");
        check(source.setPropertyValue(0,"CTCPhysicsNavigator","PositionY","25"),"property grid accepts navigator position edit");
        check(value(source,0,"InitialPosX",330)&&value(source,0,"InitialPosY",665)&&value(source,0,"InitialPosZ",3),"property edit synchronizes all existing initial axes");
        const auto propertyMove=source.text();
        check(source.undo()&&source.text()==original,"property position undo byte exact and single step");
        check(source.redo()&&source.text()==propertyMove,"property position redo byte exact");
        check(source.undo()&&source.text()==original,"restore source after property edit");

        // Same map-local position in a different world origin must still rebase.
        const auto fragment=source.extract({0}); const float at[]={10,20,3};
        const auto pasted=destination.paste(fragment,at,false);
        check(pasted.size()==1,"cross-map fragment inserted");
        if(pasted.size()==1) {
            const size_t index=pasted[0]; destination.frameOf(index,frame);
            check(frame.pos[0]==10&&frame.pos[1]==20&&frame.pos[2]==3,"paste preserves requested identical local position");
            check(value(destination,index,"InitialPosX",1034)&&value(destination,index,"InitialPosY",-492)&&value(destination,index,"InitialPosZ",3),"paste rebases initial coordinates to destination origin");
            const auto afterPaste=destination.text();
            check(destination.undo()&&destination.text()==empty,"paste undo byte exact");
            check(destination.redo()&&destination.text()==afterPaste,"paste redo byte exact");
        }
        // The authored spawn point may intentionally differ from the current frame.
        const std::string independent=prefix+thing("AICreature",1,"InitialPosX 1234.125;\r\nInitialPosY -44.25;\r\nInitialPosZ 19.75;\r\n")+"XXXSectionEnd;\r\n";
        if(!source.openText("Raw",independent,error))throw std::runtime_error(error);
        source.frameOf(0,frame);frame.forward[0]=1;frame.forward[1]=0;frame.scale=2;source.setFrame(0,frame);
        check(value(source,0,"InitialPosX",1234.125f)&&value(source,0,"InitialPosY",-44.25f)&&value(source,0,"InitialPosZ",19.75f),"rotation and scale preserve independent authored initial position");
        check(source.undo()&&source.text()==independent,"rotation/scale undo byte exact");
        source.frameOf(0,frame);frame.pos[0]=11;frame.pos[1]=22;frame.pos[2]=6;source.setFrame(0,frame);
        check(value(source,0,"InitialPosX",11)&&value(source,0,"InitialPosY",22)&&value(source,0,"InitialPosZ",6),"openText after world document resets world origin");
        check(source.undo()&&source.text()==independent,"raw-document move undo byte exact");
    } catch(const std::exception& e) {++failures;std::fprintf(stderr,"creature frame exception: %s\n",e.what());}
    // Remove only the unique directory successfully created by this process.
    if(!scratch.empty() && failures==0) fs::remove_all(scratch);
    else if(!scratch.empty()) std::fprintf(stderr,"fixture retained: %s\n",scratch.string().c_str());
    std::printf("creature frame: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
