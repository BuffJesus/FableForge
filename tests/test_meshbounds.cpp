#include "forge/meshpreview.hpp"
#include "forge/meshcompose.hpp"
#include "forge/big.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>

namespace mp=forge::meshpreview;
namespace fs=std::filesystem;
namespace {
void sphere(std::vector<uint8_t>& bytes,size_t offset,float x,float y,float z,float radius) {
    const float data[]={x,y,z,radius};std::memcpy(bytes.data()+offset,data,sizeof(data));
}
bool sameGeometry(const mp::Geometry& a,const mp::Geometry& b) {
    if(a.vertices.size()!=b.vertices.size() || a.triangles.size()!=b.triangles.size())return false;
    for(size_t i=0;i<a.vertices.size();++i)if(a.vertices[i].x!=b.vertices[i].x || a.vertices[i].y!=b.vertices[i].y || a.vertices[i].z!=b.vertices[i].z)return false;
    for(size_t i=0;i<a.triangles.size();++i)if(a.triangles[i].a!=b.triangles[i].a || a.triangles[i].b!=b.triangles[i].b || a.triangles[i].c!=b.triangles[i].c)return false;
    return true;
}
}
int main(int argc,char** argv) {
    int checks=0,failures=0;
    auto check=[&](bool okay,const char* label){++checks;if(!okay){++failures;std::fprintf(stderr,"mesh bounds: %s\n",label);}};
    fs::path scratch;
    try {
        forge::meshcompose::Primitive primitive;
        primitive.verts={{0,0,0},{4,0,0},{0,3,0}};primitive.faces={{0,1,2}};
        for(uint32_t physics:{0u,0x7fffffffu,0x80000000u,0xfffffffeu}) {
            const auto mesh=forge::meshcompose::composeStatic("MESH_PHYSICS_ID",{primitive},{{}},false,physics);
            uint32_t saved=0;std::memcpy(&saved,mesh.info.data(),4);
            check(saved==physics,"physics bank reference retains unsigned ID bits");
        }
        bool reservedPhysics=false;
        try {(void)forge::meshcompose::composeStatic("MESH_PHYSICS_ID",{primitive},{{}},false,0xffffffffu);}
        catch(const std::invalid_argument& e) {reservedPhysics=std::string(e.what()).find("reserved no-hull sentinel")!=std::string::npos;}
        check(reservedPhysics,"native no-hull sentinel cannot name a collision entry");
        for(uint32_t texture:{0x7fffffffu,0x80000000u,0xffffffffu}) {
            forge::meshcompose::Material material;
            material.diffuseId=texture;material.bumpId=texture-1;
            material.reflectId=texture-2;material.illumId=texture-3;
            const auto textured=forge::meshcompose::composeStatic("MESH_TEXTURE_IDS",{primitive},{material},false);
            const auto decoded=mp::decodeLod0(textured.payload,1);
            const auto& maps=decoded.materials.at(0);
            check(uint64_t(maps.diffuseTexture)==texture && uint64_t(maps.bumpTexture)==texture-1 &&
                  uint64_t(maps.reflectionTexture)==texture-2 && uint64_t(maps.alphaMapTexture)==texture-3,
                  "all four material texture IDs retain unsigned values");
            check(maps.textureFlags==15,"high texture IDs retain enabled map flags");
            uint32_t count=0;std::memcpy(&count,textured.info.data()+56,4);
            check(count==4,"high texture IDs remain in the descriptor dependency list");
            if(count==4) for(size_t slot=0;slot<4;++slot) {
                uint32_t id=0;std::memcpy(&id,textured.info.data()+60+slot*4,4);
                check(id==texture-slot,"descriptor texture IDs preserve exact bits and order");
            }
        }
        const std::string embeddedZero=std::string("MESH_NAME")+'\0'+"suffix";
        for(int field=0;field<3;++field) {
            bool rejectedName=false;
            try {
                if(field==2) (void)forge::meshcompose::composePhysicsUncompressed({primitive},embeddedZero);
                else (void)forge::meshcompose::composeStatic(field==0 ? embeddedZero : "MESH_NAME",
                    {primitive},{{field==1 ? embeddedZero : "material"}},false);
            }catch(const std::invalid_argument&) {rejectedName=true;}
            check(rejectedName,"embedded NUL in mesh/material/collision name refuses composition");
        }
        {
            std::vector<forge::meshcompose::Primitive> many(65535,primitive);
            const std::string name="MESH_BLOCK_LIMIT";
            const auto edge=forge::meshcompose::composeStatic(name,many,{{"material"}},false);
            // Static header: name+NUL, animated byte, bounds(40), helpers(10),
            // four i32 counts, cloth byte, then total static blocks(u16).
            const size_t blocks=name.size()+1+1+40+10+16+1;
            check(edge.payload.at(blocks)==255 && edge.payload.at(blocks+1)==255,
                  "static block count retains 65535 boundary");
            many.push_back(primitive);bool rejectedBlocks=false;
            try {(void)forge::meshcompose::composeStatic(name,many,{{"material"}},false);}
            catch(const std::invalid_argument&) {rejectedBlocks=true;}
            check(rejectedBlocks,"static block count overflow refuses instead of wrapping to zero");
        }
        for(float origin : {2e9f,-2e9f,2e38f}) {
            auto distant=primitive;
            const float extent=origin==2e38f ? 1e33f : 1024.f;
            distant.verts={{origin,0,0},{origin+extent,0,0},{origin,extent,0}};
            distant.normals={{0,0,1},{0,0,1},{0,0,1}};
            const auto generated=forge::meshcompose::composeStatic("MESH_DISTANT_TEST",{distant},{{"material"}},false);
            check(generated.bbMin.x==origin && generated.bbMax.x==origin+extent,"distant X bounds come from vertices");
            check(std::isfinite(generated.sphereCentre.x) && std::isfinite(generated.sphereRadius) &&
                  generated.sphereRadius<extent*2,"distant sphere stays finite and local to geometry");
            distant.normals.clear();
            const auto automatic=forge::meshcompose::composeStatic("MESH_DISTANT_NORMALS",{distant},{{"material"}},false);
            for(const auto& vertex:mp::decodeLod0(automatic.payload,1).vertices)
                check(std::abs(vertex.nx)<0.003f && std::abs(vertex.ny)<0.003f && vertex.nz>0.99f,
                      "large finite triangles retain generated surface normals");
        }
        auto invalidPrimitive=primitive;invalidPrimitive.verts[0].x=std::numeric_limits<float>::quiet_NaN();
        bool rejected=false;
        try {(void)forge::meshcompose::composeStatic("MESH_INVALID_TEST",{invalidPrimitive},{{"material"}},false);}
        catch(const std::invalid_argument&) {rejected=true;}
        check(rejected,"non-finite geometry refuses composition");
        const float maxCoordinate=std::numeric_limits<float>::max();
        invalidPrimitive.verts={{-maxCoordinate,-maxCoordinate,-maxCoordinate},
                                {maxCoordinate,maxCoordinate,maxCoordinate},
                                {-maxCoordinate,maxCoordinate,maxCoordinate}};
        rejected=false;
        try {(void)forge::meshcompose::composeStatic("MESH_INVALID_TEST",{invalidPrimitive},{{"material"}},false);}
        catch(const std::invalid_argument&) {rejected=true;}
        check(rejected,"unrepresentable sphere refuses finite geometry");
        forge::meshcompose::Primitive first,second;
        first.verts.resize(32768);first.faces={{0,1,32767}};second=first;
        const auto hull=forge::meshcompose::composePhysicsUncompressed({first,second},"LIMITS");
        const char* trisTag="TRIS";
        const auto tris=std::search(hull.begin(),hull.end(),trisTag,trisTag+4);
        check(tris!=hull.end() && hull.end()-tris>=24,"boundary hull has a triangle table");
        if(tris!=hull.end() && hull.end()-tris>=24)
            check(tris[20]==255 && tris[21]==255,"boundary hull retains vertex index 65535");
        second.verts.push_back({});second.faces={{0,1,32768}};rejected=false;
        try {(void)forge::meshcompose::composePhysicsUncompressed({first,second},"LIMITS");}
        catch(const std::invalid_argument&) {rejected=true;}
        check(rejected,"collision vertex overflow refuses instead of clamping triangle indices");
        auto invalidFace=primitive;invalidFace.faces={{0,1,3}};rejected=false;
        try {(void)forge::meshcompose::composePhysicsUncompressed({invalidFace},"LIMITS");}
        catch(const std::invalid_argument&) {rejected=true;}
        check(rejected,"collision index outside primitive refuses");
        auto repeated=primitive;
        repeated.verts={{-0.f,0,0},{0.f,0,0},{1,2,3},{0,0,0},{4,5,6},{1,2,3}};
        repeated.faces={{0,2,4}};
        const auto repeatedHull=forge::meshcompose::composePhysicsUncompressed({repeated},"REPEATED");
        const char* uniqueTag="UNIV";
        const auto unique=std::search(repeatedHull.begin(),repeatedHull.end(),uniqueTag,uniqueTag+4);
        check(unique!=repeatedHull.end() && repeatedHull.end()-unique>=36,"repeated hull has a unique-vertex table");
        if(unique!=repeatedHull.end() && repeatedHull.end()-unique>=36) {
            const uint32_t expected[]={0,0,1,0,2,1};
            for(size_t i=0;i<6;++i) {
                uint32_t value=0;std::memcpy(&value,&unique[12+i*4],4);
                check(value==expected[i],"unique vertex IDs preserve first occurrence and signed-zero equality");
            }
        }
        repeated.verts[0].x=std::numeric_limits<float>::quiet_NaN();rejected=false;
        try {(void)forge::meshcompose::composePhysicsUncompressed({repeated},"INVALID");}
        catch(const std::invalid_argument&) {rejected=true;}
        check(rejected,"non-finite collision position refuses before ordered lookup");
        check(forge::meshcompose::packNormal({1e30f,0,0})==1023,"large finite authored normal retains its direction");
        rejected=false;
        try {(void)forge::meshcompose::packNormal({std::numeric_limits<float>::quiet_NaN(),0,0});}
        catch(const std::invalid_argument&) {rejected=true;}
        check(rejected,"non-finite authored normal refuses packing");
        auto composed=forge::meshcompose::composeStatic("MESH_BOUNDS_TEST",{primitive},{{"material"}},false);
        const auto baseline=mp::decodeLod0(composed.payload,1);
        check(baseline.boundingSphere.has_value(),"composed payload retains sphere");
        const size_t offset=size_t(std::find(composed.payload.begin(),composed.payload.end(),uint8_t(0))-composed.payload.begin())+2;
        sphere(composed.payload,offset,8,9,10,20);
        sphere(composed.info,4,-1,-2,-3,40);
        auto decoded=mp::decodeLod0(composed.payload,1);
        check(decoded.boundingSphere && decoded.boundingSphere->centre[0]==8 && decoded.boundingSphere->centre[2]==10 && decoded.boundingSphere->radius==20 && decoded.boundingSphere->source==mp::BoundingSphere::Source::Payload,"authored payload retained without recomputing");
        check(sameGeometry(baseline,decoded),"metadata retention preserves decoded geometry");
        auto lods=mp::decodeLods(composed.payload,composed.info,1);
        check(lods.size()==1 && lods[0].boundingSphere && lods[0].boundingSphere->radius==20,"payload takes precedence over descriptor");
        sphere(composed.payload,offset,8,9,10,0);
        decoded=mp::decodeLod0(composed.payload,1);
        check(!decoded.boundingSphere && sameGeometry(baseline,decoded),"zero radius omitted without rejecting geometry");
        lods=mp::decodeLods(composed.payload,composed.info,1);
        check(lods[0].boundingSphere && lods[0].boundingSphere->radius==40 && lods[0].boundingSphere->centre[1]==-2 && lods[0].boundingSphere->source==mp::BoundingSphere::Source::Descriptor,"invalid payload uses descriptor sphere");
        const float invalid[]={-1,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()};
        for(float value:invalid) {
            sphere(composed.payload,offset,0,0,0,value);
            decoded=mp::decodeLod0(composed.payload,1);
            check(!decoded.boundingSphere && sameGeometry(decoded,baseline),"invalid radius does not invalidate mesh");
        }
        for(int axis=0;axis<3;++axis) {
            sphere(composed.payload,offset,1,2,3,4);
            const float bad=std::numeric_limits<float>::quiet_NaN();std::memcpy(composed.payload.data()+offset+axis*4,&bad,4);
            decoded=mp::decodeLod0(composed.payload,1);
            check(!decoded.boundingSphere && !decoded.empty(),"nonfinite centre omitted");
        }
        sphere(composed.info,4,0,0,0,0);
        lods=mp::decodeLods(composed.payload,composed.info,1);
        check(!lods[0].boundingSphere && !lods[0].empty(),"both invalid spheres leave geometry usable");
        bool threw=false;
        try {mp::decodeLod0(std::vector<uint8_t>(composed.payload.begin(),composed.payload.begin()+offset+39),1);}catch(const std::exception&){threw=true;}
        check(threw,"truncated fixed header still rejected");
        threw=false;try {mp::decodeLods(composed.payload,{},1);}catch(const std::exception&){threw=true;}
        check(threw,"missing LOD descriptor still rejected");

        // Exercise readLod0's descriptor fallback through an isolated synthetic BIG.
        const auto stamp=std::chrono::high_resolution_clock::now().time_since_epoch().count();
        for(int attempt=0;attempt<100;++attempt) {
            const auto candidate=fs::temp_directory_path()/("fableforge-meshbounds-"+std::to_string(stamp)+"-"+std::to_string(attempt));
            if(fs::create_directory(candidate)){scratch=fs::absolute(candidate);break;}
        }
        if(scratch.empty())throw std::runtime_error("cannot create scratch directory");
        forge::big::File bank;bank.setMagic("BIGB");bank.setVersion(1);
        forge::big::Entry entry;entry.id=42;entry.type=1;entry.name="MESH_BOUNDS_TEST";entry.data=composed.payload;
        sphere(composed.info,4,4,5,6,30);entry.subHeader=composed.info;
        bank.addBank("MBANK_ALLMESHES",0).entries.push_back(entry);
        auto save=[&] {const auto bytes=bank.serialize();std::ofstream file(scratch/"graphics.big",std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));if(!file)throw std::runtime_error("scratch BIG write failed");};
        save();decoded=mp::readLod0(scratch/"graphics.big",42);
        check(decoded.boundingSphere && decoded.boundingSphere->radius==30 && decoded.boundingSphere->source==mp::BoundingSphere::Source::Descriptor,"bank entry read applies descriptor fallback");
        bank.banks()[0].entries[0].subHeader.clear();save();decoded=mp::readLod0(scratch/"graphics.big",42);
        check(!decoded.boundingSphere && !decoded.empty(),"missing optional entry bounds do not reject mesh");
        bank.banks()[0].entries[0].subHeader.assign(19,0);save();decoded=mp::readLod0(scratch/"graphics.big",42);
        check(!decoded.boundingSphere && !decoded.empty(),"short optional entry bounds ignored");
        if(argc>1) {
            const auto retail=mp::readLod0(fs::path(argv[1]),435);
            check(retail.boundingSphere && retail.boundingSphere->source==mp::BoundingSphere::Source::Payload,"retail dust circle reads authored payload sphere");
            if(retail.boundingSphere) {
                const auto& s=*retail.boundingSphere;
                check(std::abs(s.centre[0]-.31062749f)<1e-5 && std::abs(s.centre[1]+.169793889f)<1e-5 && std::abs(s.centre[2]-2.423764467f)<1e-5 && std::abs(s.radius-16.88743019f)<1e-5,"retail 435 authored numeric reference");
                float lo=INFINITY,hi=-INFINITY;for(const auto& vertex:retail.vertices){lo=std::min(lo,vertex.z);hi=std::max(hi,vertex.z);}
                check(std::abs((lo+hi)*.5f-s.centre[2])>.7f,"retail authored centre meaningfully differs from geometry bounds");
                std::printf("retail435 sphere centre %.9g %.9g %.9g radius %.9g; derived Z %.9g\n",s.centre[0],s.centre[1],s.centre[2],s.radius,(lo+hi)*.5f);
            }
        }
    }catch(const std::exception& e){++failures;std::fprintf(stderr,"mesh bounds exception: %s\n",e.what());}
    if(!scratch.empty() && failures==0)fs::remove_all(scratch);
    else if(!scratch.empty())std::fprintf(stderr,"retained fixture: %s\n",scratch.string().c_str());
    std::printf("mesh bounds: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
