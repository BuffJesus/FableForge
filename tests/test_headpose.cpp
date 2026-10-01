#include "forge/animation.hpp"
#include "forge/big.hpp"
#include "forge/headpose.hpp"
#include "forge/lipsync_preset.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

static void require(bool good,const char* message) {
    if(!good) throw std::runtime_error(message);
}

static bool near(float a,float b,float tolerance=0.0005f) {
    return std::abs(a-b)<=tolerance;
}

int main(int argc,char** argv) {
    try {
        forge::meshpreview::Geometry mesh;
        forge::meshpreview::Bone root;
        root.name="Scene Root";root.inverseBind={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        auto jaw=root;jaw.name="Jaw";jaw.parent=0;
        mesh.bones={root,jaw};
        forge::meshpreview::Vertex vertex;
        vertex.x=1;vertex.nx=1;vertex.ny=0;vertex.nz=0;
        vertex.skinned=true;vertex.joints[0]=1;vertex.weights[0]=1;
        mesh.vertices.push_back(vertex);
        forge::animation::Animation animation;
        forge::animation::Track track;
        track.boneName="JAW";track.frameCount=1;track.positionFactor=1;
        track.rotations={{{0,0,0.70710678f,0.70710678f}}};
        track.positions={{1,0,0}};
        animation.tracks.push_back(track);
        forge::headpose::AnimationMap clips={{"AH",&animation}};
        forge::lipsync::Pose mouth;
        mouth.restWeight=0;mouth.visemes={{1,"AH",1}};
        auto result=forge::headpose::evaluate(mesh,mouth,clips);
        require(result.posedBones==1 && result.skin.size()==2,"synthetic jaw track matching");
        auto point=forge::headpose::transformPoint(result.skin[1],{1,0,0});
        require(near(point[0],1) && near(point[1],-1) && near(point[2],0),
                "synthetic jaw rotation and translation");
        const auto deformed=forge::headpose::skin(mesh,result);
        require(near(deformed.vertices[0].x,1) && near(deformed.vertices[0].y,-1) &&
                near(deformed.vertices[0].nx,0) && near(deformed.vertices[0].ny,-1) &&
                near(mesh.vertices[0].y,0),"CPU skinning positions, normals and source isolation");
        mouth.visemes[0].weight=0.5f;mouth.restWeight=0.5f;
        result=forge::headpose::evaluate(mesh,mouth,clips);
        point=forge::headpose::transformPoint(result.skin[1],{1,0,0});
        require(near(point[0],1.20710678f) && near(point[1],-0.70710678f),
                "half phoneme weight should blend over bind pose");
        // Skeletal base translation stays fixed, although its rotation may animate.
        mesh.bones[1].name="Bip01 Head";
        animation.tracks[0].boneName="bip01head";
        result=forge::headpose::evaluate(mesh,mouth,clips);
        point=forge::headpose::transformPoint(result.skin[1],{0,0,0});
        require(near(point[0],0) && near(point[1],0),"skeletal base translation lock");

        forge::meshpreview::Geometry face;
        auto left=root;left.name="EYE_SET_L";
        auto right=root;right.name="EYE_SET_R";
        left.inverseBind[3]=-4;right.inverseBind[3]=4;
        face.bones={root,left,right};
        forge::meshpreview::Vertex socket;
        socket.x=4;socket.y=-2;socket.z=0;
        face.vertices.push_back(socket);
        socket.x=-4;face.vertices.push_back(socket);
        face.materials.push_back({});
        forge::meshpreview::Geometry eyeball;
        eyeball.vertices.resize(3);
        eyeball.vertices[0].y=1;
        eyeball.vertices[1].x=1;
        eyeball.vertices[2].z=1;
        eyeball.triangles.push_back({0,1,2,0});
        eyeball.materials.push_back({});
        auto helperFace=face;
        forge::headpose::attachEyes(face,eyeball,3,2);
        require(face.vertices.size()==8 && face.triangles.size()==2 &&
                face.triangles[0].material==1 && face.triangles[1].material==1 &&
                face.vertices[2].joints[0]==1 && face.vertices[5].joints[0]==2 &&
                face.vertices[2].skinned && face.vertices[5].skinned &&
                near(face.vertices[2].x,4) && near(face.vertices[3].x,2) &&
                near(face.vertices[5].x,-4),
                "eye meshes attach to both skeleton sockets with distinct material");
        forge::meshpreview::Helper mount;
        mount.name="EYE_SET_L";
        const float transform[12]={1,0,0,0,1,0,0,0,1,7,0,0};
        std::copy(std::begin(transform),std::end(transform),std::begin(mount.matrix));
        helperFace.helpers.push_back(mount);
        forge::headpose::attachEyes(helperFace,eyeball,1,2);
        require(near(helperFace.vertices[2].x,7),
                "retail EYE_SET helper overrides bone bind mount");
        const auto still=forge::headpose::skin(face,
            forge::headpose::evaluate(face,{},{}));
        require(near(still.vertices[2].x,face.vertices[2].x) &&
                near(still.vertices[5].x,face.vertices[5].x),
                "neutral skin keeps attached eyes in place");
        forge::headpose::Pose shifted;
        shifted.skin.assign(face.bones.size(),root.inverseBind);
        shifted.skin[1][3]=1;
        const auto following=forge::headpose::skin(face,shifted);
        require(near(following.vertices[2].x,face.vertices[2].x+1) &&
                near(following.vertices[5].x,face.vertices[5].x),
                "only the posed eye attachment follows its bone");

        if(argc>1) {
            const auto path=std::filesystem::path(argv[1]);
            const auto archive=forge::big::File::open(path);
            const auto* bank=archive.findBank("MBANK_ALLMESHES");
            require(bank,"retail mesh bank missing");
            size_t totalPosed=0,totalMoved=0;
            for(const auto& preset:forge::lipsync::headPresets()) {
                const auto assets=forge::lipsync::inspectHeadPreset(archive,preset);
                require(assets.complete(),"retail head preset incomplete");
                const auto geometry=forge::meshpreview::readLod0(path,assets.meshId);
                if(assets.eyeMeshId) {
                    auto withEyes=geometry;
                    const auto eyes=forge::meshpreview::readLod0(path,assets.eyeMeshId);
                    forge::headpose::attachEyes(withEyes,eyes,preset.eyeSides,
                                                preset.eyeRenderSize);
                    const size_t attached=(preset.eyeSides&1)+((preset.eyeSides>>1)&1);
                    require(withEyes.vertices.size()==geometry.vertices.size()+attached*eyes.vertices.size() &&
                            withEyes.triangles.size()==geometry.triangles.size()+attached*eyes.triangles.size(),
                            "retail eye pair was not attached");
                }
                const forge::lipsync::Pose neutral;
                const auto resting=forge::headpose::evaluate(geometry,neutral,{});
                require(resting.skin.size()==geometry.bones.size() && !resting.posedBones,
                        "neutral retail head pose size");
                for(const auto& matrix:resting.skin)
                    for(size_t k=0;k<16;++k)
                        require(near(matrix[k],k%5==0?1.0f:0.0f,1e-6f),
                                "neutral retail head must be identity");
                const auto neutralSkin=forge::headpose::skin(geometry,resting);
                for(size_t i=0;i<geometry.vertices.size();++i) {
                    const auto& before=geometry.vertices[i];
                    const auto& after=neutralSkin.vertices[i];
                    require(near(before.x,after.x,1e-4f) &&
                            near(before.y,after.y,1e-4f) &&
                            near(before.z,after.z,1e-4f),
                            "neutral retail head should not move vertices");
                }
                const auto& first=preset.tracks.front();
                const forge::big::Entry* entry=nullptr;
                for(const auto& candidate:bank->entries)
                    if(candidate.name==first.animation) {entry=&candidate;break;}
                require(entry && entry->type==9,"retail phoneme animation absent");
                const auto clip=forge::animation::decode(archive.entryData(*entry));
                forge::lipsync::Pose active;
                active.restWeight=0;active.visemes={{1,first.symbol,1}};
                const auto posed=forge::headpose::evaluate(geometry,active,
                                                         {{first.symbol,&clip}});
                require(posed.posedBones>0,"retail phoneme matches no head bones");
                size_t moved=0;
                for(const auto& vertex:geometry.vertices) if(vertex.skinned) {
                    const std::array<float,3> base={vertex.x,vertex.y,vertex.z};
                    std::array<float,3> destination{};
                    float sum=0;
                    for(size_t k=0;k<4;++k) if(vertex.weights[k]>0) {
                        require(vertex.joints[k]<posed.skin.size(),"skin joint out of bounds");
                        const auto p=forge::headpose::transformPoint(
                            posed.skin[vertex.joints[k]],base);
                        for(size_t axis=0;axis<3;++axis)
                            destination[axis]+=p[axis]*vertex.weights[k];
                        sum+=vertex.weights[k];
                    }
                    if(sum>0 && std::hypot(destination[0]-base[0]*sum,
                                           destination[1]-base[1]*sum,
                                           destination[2]-base[2]*sum)>0.001f) ++moved;
                }
                require(moved>0,"retail phoneme moves no skinned vertices");
                const auto deformedRetail=forge::headpose::skin(geometry,posed);
                auto bounds=[](const forge::meshpreview::Geometry& g) {
                    std::array<float,6> box={1e30f,1e30f,1e30f,-1e30f,-1e30f,-1e30f};
                    for(const auto& v:g.vertices) {
                        box[0]=std::min(box[0],v.x);box[1]=std::min(box[1],v.y);
                        box[2]=std::min(box[2],v.z);box[3]=std::max(box[3],v.x);
                        box[4]=std::max(box[4],v.y);box[5]=std::max(box[5],v.z);
                    }
                    return box;
                };
                const auto baseBox=bounds(geometry),poseBox=bounds(deformedRetail);
                const float diagonal=std::hypot(baseBox[3]-baseBox[0],
                    baseBox[4]-baseBox[1],baseBox[5]-baseBox[2]);
                const float centerShift=0.5f*std::hypot(
                    poseBox[0]+poseBox[3]-baseBox[0]-baseBox[3],
                    poseBox[1]+poseBox[4]-baseBox[1]-baseBox[4],
                    poseBox[2]+poseBox[5]-baseBox[2]-baseBox[5]);
                require(centerShift<0.15f*diagonal,
                        "retail phoneme shifted the whole head out of frame");
                float worstShift=centerShift;
                for(const auto& track:preset.tracks) {
                    const forge::big::Entry* phoneme=nullptr;
                    for(const auto& candidate:bank->entries)
                        if(candidate.name==track.animation) {phoneme=&candidate;break;}
                    require(phoneme,"retail phoneme animation missing");
                    const auto animation=forge::animation::decode(archive.entryData(*phoneme));
                    forge::lipsync::Pose expression;
                    expression.visemes={{1,track.symbol,1}};
                    const auto bones=forge::headpose::evaluate(geometry,expression,
                                                               {{track.symbol,&animation}});
                    const auto box=bounds(forge::headpose::skin(geometry,bones));
                    const float shift=0.5f*std::hypot(
                        box[0]+box[3]-baseBox[0]-baseBox[3],
                        box[1]+box[4]-baseBox[1]-baseBox[4],
                        box[2]+box[5]-baseBox[2]-baseBox[5]);
                    worstShift=std::max(worstShift,shift);
                    require(shift<0.15f*diagonal,
                            "retail phoneme shifted the whole head out of frame");
                }
                totalPosed+=posed.posedBones;totalMoved+=moved;
                std::cout << preset.name << ": " << posed.posedBones
                          << " posed bones, " << moved << " moved vertices, worst center shift "
                          << worstShift << " / " << diagonal << " bounds diagonal\n";
            }
            std::cout << "head poses: " << totalPosed << " posed bones, "
                      << totalMoved << " moved vertices\n";
        }
        return 0;
    } catch(const std::exception& ex) {
        std::cerr << "head pose: " << ex.what() << '\n';
        return 1;
    }
}
