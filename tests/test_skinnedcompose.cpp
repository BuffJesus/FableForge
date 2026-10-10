#include "forge/meshcompose.hpp"
#include "forge/meshpreview.hpp"
#include "forge/big.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace mc=forge::meshcompose;
namespace mp=forge::meshpreview;
static void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
static void word(std::vector<uint8_t>& out,uint32_t value) {for(unsigned i=0;i<4;++i) out.push_back(uint8_t(value>>(8*i)));}
static void real(std::vector<uint8_t>& out,float value) {word(out,std::bit_cast<uint32_t>(value));}
static mp::SkeletonBlocks donor(size_t count=32) {
    mp::SkeletonBlocks s;
    for(size_t i=0;i<count;++i) {
        s.indices.push_back(uint8_t(100+i));s.indices.push_back(0);
        const auto name="BONE_"+std::to_string(i);s.names.insert(s.names.end(),name.begin(),name.end());s.names.push_back(0);
        word(s.bones,0);word(s.bones,0xffffffffu);word(s.bones,0);
        for(float f:{1.f,0.f,0.f,0.f,1.f,0.f,0.f,0.f,1.f,0.f,0.f,0.f}) real(s.bones,f);
        for(size_t k=0;k<12;++k) {if(k==7 || k==11) word(s.keyframes,0x7fc01234u);else real(s.keyframes,k==3 || (k>=8 && k<=10)?1.f:0.f);}
        for(size_t k=0;k<16;++k) {if(k>=12) word(s.inverseBinds,0x7fc05678u);else real(s.inverseBinds,k==0 || k==5 || k==10?1.f:0.f);}
    }
    return s;
}
static bool same(const mp::SkeletonBlocks& a,const mp::SkeletonBlocks& b) {
    return a.indices==b.indices && a.names==b.names && a.bones==b.bones && a.keyframes==b.keyframes && a.inverseBinds==b.inverseBinds;
}
static mc::SkinnedPrimitive triangle() {
    mc::SkinnedPrimitive p;p.geometry.verts={{0,0,0},{1,0,0},{0,1,0}};p.geometry.faces={{0,1,2}};
    p.skin={{{0,5},{1,3},{2,2},{3,1}},{{2,1}},{{1,1}}};return p;
}
static mc::SkinnedPrimitive fan() {
    mc::SkinnedPrimitive p;p.geometry.verts.push_back({0,0,0});p.skin.push_back({{0,1}});
    for(uint16_t i=1;i<=20;++i) {
        const float angle=float(i)*.2f;p.geometry.verts.push_back({std::cos(angle),std::sin(angle),float(i)/20});p.skin.push_back({{i,1}});
        if(i>1) p.geometry.faces.push_back({0,uint32_t(i-1),i});
    }
    return p;
}
static void checkMesh(const mc::Composed& mesh,const mc::SkinnedPrimitive& source,const mp::SkeletonBlocks& skeleton,bool multi) {
    const auto g=mp::decodeLod0(mesh.payload,5);
    require(g.skeletonBlocks && same(*g.skeletonBlocks,skeleton),"donor spans changed in LOD0");
    require(g.materials.size()==1 && g.materials[0].diffuseTexture==123,"skinned material/sentinel mismatch");
    require(g.triangles.size()==source.geometry.faces.size(),"triangle count mismatch");
    require(multi?g.animatedBlockCount>1:g.animatedBlockCount==1,"animated block partition mismatch");
    if(multi) require(g.vertices.size()>source.geometry.verts.size(),"shared palette-boundary vertices not duplicated");
    for(size_t i=0;i<g.triangles.size();++i) {
        const auto& t=g.triangles[i];const std::array<uint32_t,3> actual={t.a,t.b,t.c};
        for(size_t k=0;k<3;++k) {
            const auto index=source.geometry.faces[i][k];const auto& want=source.geometry.verts[index];const auto& v=g.vertices.at(actual[k]);
            require(v.x==want.x && v.y==want.y && v.z==want.z && v.skinned,"face position/winding/skin changed");
            float sum=0;for(float w:v.weights) sum+=w;require(std::abs(sum-1)<1e-6,"weights do not sum to one");
            if(multi) require(v.joints[0]==source.skin[index][0].bone && v.weights[0]==1,"partition changed per-corner bone");
        }
    }
    if(!multi) {
        const auto& v=g.vertices[0];require(v.joints[0]==0 && v.joints[1]==1 && v.joints[2]==2 && v.weights[3]==0,"top-three influences changed");
        for(size_t k=0;k<3;++k) require(std::abs(v.weights[k]-float(source.skin[0][k].weight/10))<=1.f/255,"weight quantization changed");
    }
    uint32_t size=0;std::memcpy(&size,mesh.info.data()+48,4);require(size<mesh.payload.size(),"missing ghost LOD");
    const std::vector<uint8_t> ghost(mesh.payload.begin()+size,mesh.payload.end());const auto ghostGeometry=mp::decodeLod0(ghost,5);
    require(ghostGeometry.vertices.empty() && ghostGeometry.skeletonBlocks && same(*ghostGeometry.skeletonBlocks,skeleton),"ghost skeleton changed");
    require(mp::decodeLods(mesh.payload,mesh.info,5).size()==1,"descriptor included ghost LOD");
}
template<class F> static void refuses(F action) {
    bool refused=false;try {action();}catch(const std::invalid_argument&) {refused=true;}require(refused,"invalid authored mesh accepted");
}
static void write(const std::filesystem::path& path,const std::vector<uint8_t>& bytes) {
    std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
    require(bool(out),"cannot write isolated composed fixture");
}
int main(int argc,char** argv) {
    try {
        const auto skeleton=donor();mc::Material material;material.diffuseId=123;
        for(bool compress:{false,true}) {
            checkMesh(mc::composeSkinned("MESH_TRIANGLE",{triangle()},{material},skeleton,compress),triangle(),skeleton,false);
            checkMesh(mc::composeSkinned("MESH_FAN",{fan()},{material},skeleton,compress),fan(),skeleton,true);
        }
        const auto invoke=[&](const mc::SkinnedPrimitive& p,const std::optional<mp::SkeletonBlocks>& s=std::nullopt) {return mc::composeSkinned("MESH_BAD",{p},{material},s?*s:skeleton);};
        auto p=triangle();p.skin.pop_back();refuses([&]{invoke(p);});
        p=triangle();p.skin[0][0].bone=32;refuses([&]{invoke(p);});
        p=triangle();p.skin[0][0].weight=-1;refuses([&]{invoke(p);});
        p=triangle();p.skin[0][0].weight=std::numeric_limits<double>::quiet_NaN();refuses([&]{invoke(p);});
        p=triangle();p.skin[0][0].weight=std::numeric_limits<double>::infinity();refuses([&]{invoke(p);});
        p=triangle();p.skin[0][1].bone=0;refuses([&]{invoke(p);});
        p=triangle();p.geometry.faces[0][0]=3;refuses([&]{invoke(p);});
        p=triangle();p.geometry.material=1;refuses([&]{invoke(p);});
        p=triangle();p.geometry.uvs={{0,0}};refuses([&]{invoke(p);});
        p=triangle();p.geometry.normals={{0,0,1}};refuses([&]{invoke(p);});
        p=triangle();p.geometry.verts[0].x=std::numeric_limits<float>::infinity();refuses([&]{invoke(p);});
        auto s=skeleton;s.indices.pop_back();refuses([&]{invoke(triangle(),s);});
        s=skeleton;s.bones.pop_back();refuses([&]{invoke(triangle(),s);});
        s=skeleton;s.keyframes.pop_back();refuses([&]{invoke(triangle(),s);});
        s=skeleton;s.inverseBinds.pop_back();refuses([&]{invoke(triangle(),s);});
        s=skeleton;s.names.back()='x';refuses([&]{invoke(triangle(),s);});
        s=skeleton;std::fill(s.bones.begin()+4,s.bones.begin()+8,0);refuses([&]{invoke(triangle(),s);}); // self cycle
        s=skeleton;const uint32_t nan=0x7fc00000;std::memcpy(s.inverseBinds.data(),&nan,4);refuses([&]{invoke(triangle(),s);});
        p=triangle();p.skin[0].clear();const auto root=mp::decodeLod0(invoke(p).payload,5);require(root.vertices[0].joints[0]==0 && root.vertices[0].weights[0]==1,"empty authored list did not bind root");
        p=triangle();p.skin[0]={{0,std::numeric_limits<double>::max()},{1,std::numeric_limits<double>::max()}};
        const auto huge=mp::decodeLod0(invoke(p).payload,5);require(huge.vertices[0].weights[0]+huge.vertices[0].weights[1]==1,"finite large weights overflowed");
        mc::SkinnedPrimitive overflow;
        for(uint32_t i=0;i<mc::maxPrimitiveVertices;++i) {
            overflow.geometry.verts.push_back({float(i),0,0});overflow.skin.push_back({{uint16_t(i%32),1}});
            if(i>1) overflow.geometry.faces.push_back({0,i-1,i});
        }
        bool partitionOverflow=false;
        try {invoke(overflow);}catch(const std::invalid_argument& e) {partitionOverflow=std::string(e.what()).find("after palette partition")!=std::string::npos;}
        require(partitionOverflow,"palette duplication overflow not specifically refused");
        const auto two=mc::composeSkinned("MESH_TWO",{triangle(),fan()},{material},skeleton);
        const auto twoGeometry=mp::decodeLod0(two.payload,5);
        require(twoGeometry.primitiveCount==2 && twoGeometry.triangles.size()==20 && twoGeometry.vertices.size()==two.vertices,
                "multiple skinned primitives lost geometry");
        auto badMaterial=material;badMaterial.nativeFlags=std::array<uint8_t,5>{0,0,0,1,0};
        refuses([&]{mc::composeSkinned("MESH_BAD",{triangle()},{badMaterial},skeleton);});
        refuses([&]{mc::composeSkinned("NOT_A_MESH",{triangle()},{material},skeleton);});
        refuses([&]{mc::composeSkinned(std::string("MESH_\0BAD",9),{triangle()},{material},skeleton);});
        if(argc==4 && std::string(argv[1])=="--retail") {
            const auto archive=forge::big::File::open(argv[2]);const auto* bank=archive.findBank("MBANK_ALLMESHES");require(bank,"missing meshes bank");
            const auto output=std::filesystem::absolute(argv[3]);require(!std::filesystem::exists(output),"retail output must be fresh");
            std::filesystem::create_directories(output);
            for(uint32_t id:{4521u,5121u,5150u,3985u,5110u}) {
                const auto e=std::find_if(bank->entries.begin(),bank->entries.end(),[&](const auto& e){return e.id==id;});require(e!=bank->entries.end() && e->type==5,"missing donor");
                const auto geometry=mp::decodeLod0(archive.entryData(*e),5);require(bool(geometry.skeletonBlocks),"missing donor spans");
                for(bool compressed:{false,true}) {
                    const auto stem=std::to_string(id)+(compressed?"-compressed":"-stored");
                    const auto mesh=mc::composeSkinned("MESH_CUSTOM_"+std::to_string(id),{triangle()},{material},*geometry.skeletonBlocks,compressed);
                    checkMesh(mesh,triangle(),*geometry.skeletonBlocks,false);write(output/(stem+".mesh"),mesh.payload);write(output/(stem+".info"),mesh.info);
                }
            }
            for(bool compressed:{false,true}) {const auto mesh=mc::composeSkinned("MESH_CUSTOM_FAN",{fan()},{material},skeleton,compressed);
                const auto stem=compressed?"fan-compressed":"fan-stored";write(output/(std::string(stem)+".mesh"),mesh.payload);write(output/(std::string(stem)+".info"),mesh.info);}
        }else require(argc==1,"expected --retail graphics.big fresh-output-directory");
        std::cout<<"skinned composition: compressed/stored single/multi palettes, raw skeleton/ghost, authored weights and refusal gates PASS\n";
        return 0;
    }catch(const std::exception& e) {std::cerr<<"skinned composition: "<<e.what()<<'\n';return 1;}
}
