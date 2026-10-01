#include "forge/headpose.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace forge::headpose {
namespace {

constexpr Matrix identity={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};

Matrix multiply(const Matrix& a,const Matrix& b) {
    Matrix out{};
    for(int row=0;row<4;++row)
        for(int col=0;col<4;++col)
            for(int k=0;k<4;++k) out[row*4+col]+=a[row*4+k]*b[k*4+col];
    return out;
}

Matrix inverseAffine(const Matrix& m) {
    const double a=m[0],b=m[1],c=m[2],d=m[4],e=m[5],f=m[6],
                 g=m[8],h=m[9],i=m[10];
    const double det=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
    if(!std::isfinite(det) || std::abs(det)<1e-12)
        throw std::runtime_error("head pose: singular inverse bind matrix");
    Matrix result=identity;
    result[0]=float((e*i-f*h)/det); result[1]=float((c*h-b*i)/det);
    result[2]=float((b*f-c*e)/det); result[4]=float((f*g-d*i)/det);
    result[5]=float((a*i-c*g)/det); result[6]=float((c*d-a*f)/det);
    result[8]=float((d*h-e*g)/det); result[9]=float((b*g-a*h)/det);
    result[10]=float((a*e-b*d)/det);
    for(int row=0;row<3;++row)
        result[row*4+3]=-(result[row*4]*m[3]+result[row*4+1]*m[7]+
                           result[row*4+2]*m[11]);
    return result;
}

using Quaternion=std::array<float,4>;
struct Components {
    std::array<float,3> translation{},scale{};
    Quaternion rotation={0,0,0,1};
};

Quaternion normalized(Quaternion q) {
    double length=0;
    for(float v:q) length+=double(v)*v;
    if(length<1e-20 || !std::isfinite(length)) return {0,0,0,1};
    for(float& v:q) v=float(v/std::sqrt(length));
    return q;
}

Components decompose(const Matrix& m) {
    Components out;
    out.translation={m[3],m[7],m[11]};
    float r[3][3]{};
    for(int col=0;col<3;++col) {
        double length=0;
        for(int row=0;row<3;++row) length+=double(m[row*4+col])*m[row*4+col];
        if(length<1e-20 || !std::isfinite(length))
            throw std::runtime_error("head pose: degenerate bind scale");
        out.scale[col]=float(std::sqrt(length));
        for(int row=0;row<3;++row) r[row][col]=m[row*4+col]/out.scale[col];
    }
    // Keep mirrored bind transforms in their original handedness.
    const float determinant=r[0][0]*(r[1][1]*r[2][2]-r[1][2]*r[2][1])-
        r[0][1]*(r[1][0]*r[2][2]-r[1][2]*r[2][0])+
        r[0][2]*(r[1][0]*r[2][1]-r[1][1]*r[2][0]);
    if(determinant<0) {
        out.scale[0]=-out.scale[0];
        for(int row=0;row<3;++row) r[row][0]=-r[row][0];
    }
    const float trace=r[0][0]+r[1][1]+r[2][2];
    Quaternion q{};
    if(trace>0) {
        const float s=2*std::sqrt(trace+1);
        q={(r[2][1]-r[1][2])/s,(r[0][2]-r[2][0])/s,
           (r[1][0]-r[0][1])/s,s*0.25f};
    } else if(r[0][0]>r[1][1] && r[0][0]>r[2][2]) {
        const float s=2*std::sqrt(1+r[0][0]-r[1][1]-r[2][2]);
        q={s*0.25f,(r[0][1]+r[1][0])/s,(r[0][2]+r[2][0])/s,
           (r[2][1]-r[1][2])/s};
    } else if(r[1][1]>r[2][2]) {
        const float s=2*std::sqrt(1+r[1][1]-r[0][0]-r[2][2]);
        q={(r[0][1]+r[1][0])/s,s*0.25f,(r[1][2]+r[2][1])/s,
           (r[0][2]-r[2][0])/s};
    } else {
        const float s=2*std::sqrt(1+r[2][2]-r[0][0]-r[1][1]);
        q={(r[0][2]+r[2][0])/s,(r[1][2]+r[2][1])/s,s*0.25f,
           (r[1][0]-r[0][1])/s};
    }
    out.rotation=normalized(q);
    return out;
}

Matrix compose(const Components& c) {
    const auto q=normalized(c.rotation);
    const float x=q[0],y=q[1],z=q[2],w=q[3];
    return {(1-2*(y*y+z*z))*c.scale[0],2*(x*y-z*w)*c.scale[1],
            2*(x*z+y*w)*c.scale[2],c.translation[0],
            2*(x*y+z*w)*c.scale[0],(1-2*(x*x+z*z))*c.scale[1],
            2*(y*z-x*w)*c.scale[2],c.translation[1],
            2*(x*z-y*w)*c.scale[0],2*(y*z+x*w)*c.scale[1],
            (1-2*(x*x+y*y))*c.scale[2],c.translation[2],
            0,0,0,1};
}

std::string cleanName(const std::string& name) {
    std::string out;
    for(unsigned char ch:name) if(std::isalnum(ch)) out.push_back(char(std::tolower(ch)));
    return out;
}

struct Active {
    float weight=0;
    std::unordered_map<std::string,const animation::Track*> tracks;
};

} // namespace

std::array<float,3> transformPoint(const Matrix& m,const std::array<float,3>& p) {
    return {m[0]*p[0]+m[1]*p[1]+m[2]*p[2]+m[3],
            m[4]*p[0]+m[5]*p[1]+m[6]*p[2]+m[7],
            m[8]*p[0]+m[9]*p[1]+m[10]*p[2]+m[11]};
}

Pose evaluate(const meshpreview::Geometry& mesh,const lipsync::Pose& mouth,
              const AnimationMap& animations) {
    Pose result;
    const size_t count=mesh.bones.size();
    result.skin.assign(count,identity);
    if(!count) return result;
    std::unordered_map<std::string,float> weights;
    for(const auto& viseme:mouth.visemes)
        if(std::isfinite(viseme.weight) && viseme.weight>0)
            weights[viseme.symbol]+=viseme.weight;
    if(std::isfinite(mouth.restWeight) && mouth.restWeight>0)
        weights["MM"]+=mouth.restWeight;
    float total=0;
    for(const auto& [_,weight]:weights) total+=weight;
    if(total>1) for(auto& [_,weight]:weights) weight/=total;
    std::vector<Active> active;
    float assigned=0;
    for(const auto& [symbol,weight]:weights) {
        if(weight<=0.001f) continue;
        const auto it=animations.find(symbol);
        if(it==animations.end() || !it->second) continue;
        Active item;item.weight=weight;
        for(const auto& track:it->second->tracks)
            item.tracks.try_emplace(cleanName(track.boneName),&track);
        active.push_back(std::move(item));
        assigned+=weight;
    }
    if(active.empty()) return result;
    const float unassigned=std::max(0.0f,1.0f-assigned);
    std::vector<Matrix> bindGlobal(count),locals(count),globals(count);
    for(size_t i=0;i<count;++i) bindGlobal[i]=inverseAffine(mesh.bones[i].inverseBind);
    for(size_t i=0;i<count;++i) {
        const int parent=mesh.bones[i].parent;
        if(parent>=int(count) || parent==int(i))
            throw std::runtime_error("head pose: invalid bone parent");
        locals[i]=parent<0?bindGlobal[i]:multiply(mesh.bones[size_t(parent)].inverseBind,bindGlobal[i]);
        const std::string name=cleanName(mesh.bones[i].name);
        if(name.empty() || name=="bip01" || name=="root" || name=="bip01pelvis") continue;
        bool found=false;
        for(const auto& item:active) if(item.tracks.contains(name)) {found=true;break;}
        if(!found) continue;
        const auto bind=decompose(locals[i]);
        Components blended=bind;
        blended.rotation={};blended.translation={};
        for(int axis=0;axis<4;++axis) blended.rotation[axis]=bind.rotation[axis]*unassigned;
        for(int axis=0;axis<3;++axis) blended.translation[axis]=bind.translation[axis]*unassigned;
        const bool lockTranslation=name.rfind("bip01",0)==0;
        for(const auto& item:active) {
            Quaternion rotation=bind.rotation;
            auto position=bind.translation;
            if(const auto it=item.tracks.find(name);it!=item.tracks.end()) {
                const auto sampled=animation::evaluate(*it->second,0);
                // 3DAF stores the quaternion for Fable's row-vector transform.
                // Our local matrices use column vectors, so conjugate it here.
                if(sampled.hasRotation) rotation={-sampled.rotation[0],
                    -sampled.rotation[1],-sampled.rotation[2],sampled.rotation[3]};
                if(sampled.hasPosition && !lockTranslation) position=sampled.position;
            }
            float dot=0;
            for(int axis=0;axis<4;++axis) dot+=rotation[axis]*bind.rotation[axis];
            if(dot<0) for(float& v:rotation) v=-v;
            for(int axis=0;axis<4;++axis) blended.rotation[axis]+=rotation[axis]*item.weight;
            for(int axis=0;axis<3;++axis) blended.translation[axis]+=position[axis]*item.weight;
        }
        locals[i]=compose(blended);
        ++result.posedBones;
    }
    std::vector<uint8_t> state(count);
    std::function<void(size_t)> visit=[&](size_t i) {
        if(state[i]==2) return;
        if(state[i]==1) throw std::runtime_error("head pose: cyclic bone hierarchy");
        state[i]=1;
        const int parent=mesh.bones[i].parent;
        if(parent>=0) {
            visit(size_t(parent));
            globals[i]=multiply(globals[size_t(parent)],locals[i]);
        } else globals[i]=locals[i];
        result.skin[i]=multiply(globals[i],mesh.bones[i].inverseBind);
        state[i]=2;
    };
    for(size_t i=0;i<count;++i) visit(i);
    return result;
}

meshpreview::Geometry skin(const meshpreview::Geometry& mesh,const Pose& pose) {
    meshpreview::Geometry out=mesh;
    if(pose.skin.size()!=mesh.bones.size())
        throw std::runtime_error("head pose: skin matrix count differs from skeleton");
    for(size_t i=0;i<mesh.vertices.size();++i) {
        const auto& source=mesh.vertices[i];
        if(!source.skinned) continue;
        std::array<float,3> position{},normal{};
        float total=0;
        for(size_t k=0;k<4;++k) {
            const float weight=source.weights[k];
            if(weight<=0) continue;
            if(source.joints[k]>=pose.skin.size())
                throw std::runtime_error("head pose: skin joint out of bounds");
            const auto& matrix=pose.skin[source.joints[k]];
            const auto point=transformPoint(matrix,{source.x,source.y,source.z});
            for(size_t axis=0;axis<3;++axis) {
                position[axis]+=point[axis]*weight;
                normal[axis]+=(matrix[axis*4]*source.nx+
                               matrix[axis*4+1]*source.ny+
                               matrix[axis*4+2]*source.nz)*weight;
            }
            total+=weight;
        }
        if(total<=0) continue;
        auto& target=out.vertices[i];
        target.x=position[0]/total;target.y=position[1]/total;target.z=position[2]/total;
        const float length=std::hypot(normal[0],normal[1],normal[2]);
        if(length>1e-8f) {
            target.nx=normal[0]/length;target.ny=normal[1]/length;
            target.nz=normal[2]/length;
        }
    }
    return out;
}

void attachEyes(meshpreview::Geometry& head,const meshpreview::Geometry& eye,
                uint8_t sides,float renderSize) {
    if(eye.empty()) throw std::runtime_error("head pose: eye mesh has no geometry");
    if(!sides || (sides&~3u)) throw std::runtime_error("head pose: invalid eye sides");
    if(!std::isfinite(renderSize) || renderSize<=0 || renderSize>10)
        throw std::runtime_error("head pose: invalid eye render size");
    std::array<size_t,2> slots{};
    for(size_t side=0;side<2;++side) {
        if(!(sides&(1u<<side))) continue;
        const char* name=side==0?"EYE_SET_L":"EYE_SET_R";
        const auto it=std::find_if(head.bones.begin(),head.bones.end(),
            [name](const auto& bone){return bone.name==name;});
        if(it==head.bones.end()) throw std::runtime_error("head pose: eye attachment bone missing");
        slots[side]=size_t(it-head.bones.begin());
    }
    if(head.materials.size()+eye.materials.size()>size_t(INT32_MAX))
        throw std::runtime_error("head pose: too many eye materials");
    const int32_t materialBase=int32_t(head.materials.size());
    head.materials.insert(head.materials.end(),eye.materials.begin(),eye.materials.end());
    const size_t faceVertices=head.vertices.size();
    for(size_t side=0;side<2;++side) {
        if(!(sides&(1u<<side))) continue;
        const auto bone=slots[side];
        Matrix bind=inverseAffine(head.bones[bone].inverseBind);
        // The EYE_SET helper is the retail model-space mount. Its 3x4 payload
        // stores three basis columns followed by the translation. The bone is
        // retained for skinning so the mounted eye follows facial poses.
        const char* mountName=side==0?"EYE_SET_L":"EYE_SET_R";
        const auto mount=std::find_if(head.helpers.begin(),head.helpers.end(),
            [mountName](const auto& h){return h.name==mountName;});
        if(mount!=head.helpers.end()) {
            const float* m=mount->matrix;
            bind={m[0],m[3],m[6],m[9],
                  m[1],m[4],m[7],m[10],
                  m[2],m[5],m[8],m[11],
                  0,0,0,1};
        }
        const uint32_t vertexBase=uint32_t(head.vertices.size());
        std::vector<meshpreview::Vertex> placed;
        placed.reserve(eye.vertices.size());
        float eyeFront=std::numeric_limits<float>::infinity();
        for(const auto& original:eye.vertices) {
            auto vertex=original;
            // The eye mesh faces +Y in its own space; the head faces -Y.
            const auto p=transformPoint(bind,{-original.x*renderSize,
                -original.y*renderSize,original.z*renderSize});
            vertex.x=p[0];vertex.y=p[1];vertex.z=p[2];
            eyeFront=std::min(eyeFront,vertex.y);
            const auto n=std::array<float,3>{
                -bind[0]*original.nx-bind[1]*original.ny+bind[2]*original.nz,
                -bind[4]*original.nx-bind[5]*original.ny+bind[6]*original.nz,
                -bind[8]*original.nx-bind[9]*original.ny+bind[10]*original.nz};
            const float length=std::hypot(n[0],n[1],n[2]);
            if(length>1e-8f) {
                vertex.nx=n[0]/length;vertex.ny=n[1]/length;vertex.nz=n[2]/length;
            }
            vertex.skinned=true;vertex.joints={uint16_t(bone),0,0,0};
            vertex.weights={1,0,0,0};
            placed.push_back(vertex);
        }
        float socketFront=std::numeric_limits<float>::infinity();
        for(size_t i=0;i<faceVertices;++i) {
            const auto& v=head.vertices[i];
            if(std::abs(v.x-bind[3])<2 && std::abs(v.z-bind[11])<2)
                socketFront=std::min(socketFront,v.y);
        }
        // The socket bone alone leaves the Bandit's iris behind face geometry.
        // Keep the eye front behind the nearest socket surface while allowing
        // enough of it through the opening to remain visible.
        const float forward=std::isfinite(socketFront)
            ?std::clamp(eyeFront-socketFront-0.7f,0.0f,5.0f):0.0f;
        for(auto& vertex:placed) {
            vertex.y-=forward;
            head.vertices.push_back(vertex);
        }
        for(const auto& original:eye.triangles) {
            auto triangle=original;
            triangle.a+=vertexBase;triangle.b+=vertexBase;triangle.c+=vertexBase;
            if(triangle.material>=0) triangle.material+=materialBase;
            head.triangles.push_back(triangle);
        }
    }
}

} // namespace forge::headpose
