#include "forge/creaturepose.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace forge::creaturepose {
namespace {
[[noreturn]] void fail(const char* reason) {throw std::runtime_error(std::string("creature pose: ")+reason);}
template<class T> bool finite(const T& values) {
    return std::all_of(values.begin(),values.end(),[](float value){return std::isfinite(value);});
}
Matrix multiply(const Matrix& a,const Matrix& b) {
    Matrix result{};
    for(size_t r=0;r<4;++r)for(size_t c=0;c<4;++c) {
        double value=0;
        for(size_t k=0;k<4;++k)value+=double(a[r*4+k])*b[k*4+c];
        result[r*4+c]=float(value);
    }
    if(!finite(result))fail("matrix overflow");
    return result;
}
Matrix local(const Frame& frame,const std::array<float,3>& position) {
    if(!finite(frame.rotation)||!finite(position))fail("nonfinite frame");
    const auto [x,y,z,w]=frame.rotation;
    const double norm=double(x)*x+double(y)*y+double(z)*z+double(w)*w;
    const float factor=norm ? float(2/norm) : 1;
    if(!std::isfinite(float(norm))||!std::isfinite(factor))fail("unsupported quaternion magnitude");
    Matrix result{1-factor*(y*y+z*z),factor*(x*y+z*w),factor*(x*z-y*w),position[0],
                  factor*(x*y-z*w),1-factor*(x*x+z*z),factor*(y*z+x*w),position[1],
                  factor*(x*z+y*w),factor*(y*z-x*w),1-factor*(x*x+y*y),position[2],
                  0,0,0,1};
    if(!finite(result))fail("quaternion matrix overflow");
    return result;
}
}
Pose evaluate(std::span<const Frame> frames,std::span<const int32_t> parents,
              std::span<const std::array<float,3>> scales,std::span<const Matrix> inverseBinds) {
    const auto count=frames.size();
    if(count>65536 || parents.size()!=count || inverseBinds.size()!=count ||
       (!scales.empty()&&scales.size()!=count))fail("skeleton/pose size mismatch");
    for(size_t i=0;i<count;++i) {
        if(parents[i]>=0&&size_t(parents[i])>=i)fail("parent must precede child");
        if(!finite(inverseBinds[i])||(!scales.empty()&&!finite(scales[i])))fail("nonfinite bone input");
        if(inverseBinds[i][12]!=0||inverseBinds[i][13]!=0||inverseBinds[i][14]!=0||inverseBinds[i][15]!=1)
            fail("inverse bind must be affine");
    }
    Pose result;result.global.resize(count);result.skin.resize(count);
    for(size_t i=0;i<count;++i) {
        auto position=frames[i].position;
        const auto parent=parents[i];
        if(parent>=0&&!scales.empty())
            for(size_t axis=0;axis<3;++axis)position[axis]*=scales[size_t(parent)][axis];
        const auto matrix=local(frames[i],position);
        result.global[i]=parent<0 ? matrix : multiply(result.global[size_t(parent)],matrix);
    }
    // Scale only after every hierarchy matrix is complete: descendants must not
    // inherit ancestor geometry scale through the accumulated parent matrix.
    for(size_t i=0;i<count;++i) {
        if(!scales.empty())for(size_t row=0;row<3;++row)for(size_t col=0;col<3;++col)
            result.global[i][row*4+col]*=scales[i][col];
        if(!finite(result.global[i]))fail("scaled matrix overflow");
        result.skin[i]=multiply(result.global[i],inverseBinds[i]);
    }
    return result;
}
Pose bindPose(const meshpreview::Geometry& mesh,std::span<const std::array<float,3>> scales) {
    const auto count=mesh.bones.size();
    if(mesh.boneCount!=count || count>65536)fail("decoded skeleton count mismatch");
    if(count && (!mesh.skeletonBlocks || mesh.skeletonBlocks->keyframes.size()!=count*48))
        fail("native bind keyframes unavailable");
    std::vector<Frame> frames(count);std::vector<int32_t> parents(count);std::vector<Matrix> binds(count);
    for(size_t i=0;i<count;++i) {
        const auto* source=mesh.skeletonBlocks->keyframes.data()+i*48;
        std::memcpy(frames[i].rotation.data(),source,16);
        std::memcpy(frames[i].position.data(),source+16,12);
        parents[i]=mesh.bones[i].parent;binds[i]=mesh.bones[i].inverseBind;
    }
    return evaluate(frames,parents,scales,binds);
}
} // namespace forge::creaturepose
