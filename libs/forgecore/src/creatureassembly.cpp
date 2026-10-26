#include "forge/creatureassembly.hpp"
#include <stdexcept>
#include "forge/bin.hpp"
#include "forge/creaturepose.hpp"
#include "forge/headpose.hpp"

namespace forge::creatureassembly {
namespace {
[[noreturn]] void fail(const std::string& reason) {throw std::runtime_error("creature assembly: "+reason);}
}
Assembly assemble(std::span<const uint8_t> creaturePrefix,uint32_t seed,bool decapitated,
                  const Sources& sources) {
    if(!sources.skeletalName||!sources.boneConfig||!sources.mesh)fail("missing source resolver");
    Assembly result;
    const auto layout=creatureappearance::read(creaturePrefix);
    result.selection=creatureappearance::select(layout,seed,decapitated);
    if(result.selection.parts.empty())fail("no substituted body parts; base-mesh appearance is not assembled");
    // Targets stay alive in `prepared` while `weighted` spans them.
    std::vector<std::vector<boneconfig::Bone>> prepared;
    for(const auto& morph:result.selection.skeletal) {
        auto name=sources.skeletalName(morph.nameOffset);
        prepared.push_back(boneconfig::prepare(boneconfig::read(sources.boneConfig(name))));
        result.skeletalTargets.push_back(std::move(name));
    }
    std::vector<boneconfig::WeightedTarget> weighted;
    for(size_t i=0;i<prepared.size();++i)weighted.push_back({prepared[i],result.selection.skeletal[i].amount});
    result.combined=boneconfig::combine(weighted);
    for(const auto& selected:result.selection.parts) {
        Part part;part.body=selected.body;part.alternative=selected.alternative;part.mesh=selected.mesh;
        part.textures=creatureappearance::composeTextures(selected.textures,result.selection.globalTextures);
        const auto mesh=sources.mesh(selected.mesh);
        if(mesh.empty())fail("part mesh "+std::to_string(selected.mesh)+" has no geometry");
        std::vector<uint32_t> crcs;
        for(const auto& bone:mesh.bones) {
            if(bone.nameCrc!=bin::nameCrc(bone.name))
                fail("mesh "+std::to_string(selected.mesh)+" bone CRC differs from its name");
            crcs.push_back(bone.nameCrc);
        }
        part.boneScales=boneconfig::mapScales(result.combined,crcs);
        for(const auto& scale:part.boneScales)part.scaledBones+=scale!=std::array<float,3>{1,1,1};
        const auto pose=creaturepose::bindPose(mesh,part.boneScales);
        part.posed=headpose::skin(mesh,headpose::Pose{pose.skin,pose.skin.size()});
        result.parts.push_back(std::move(part));
    }
    return result;
}
meshpreview::Geometry merge(const Assembly& assembly) {
    meshpreview::Geometry out;
    for(const auto& part:assembly.parts) {
        const auto vertexBase=out.vertices.size(),materialBase=out.materials.size();
        if(vertexBase+part.posed.vertices.size()>UINT32_MAX)fail("merged vertex count overflow");
        out.vertices.insert(out.vertices.end(),part.posed.vertices.begin(),part.posed.vertices.end());
        // composeTextures leaves one entry per original ID; look up from the
        // authored IDs so a replacement is never itself replaced.
        const auto composed=[&](uint32_t id) {
            if(!id)return id; // 0 = no texture
            for(const auto& replacement:part.textures)if(replacement.original==id)return replacement.replacement;
            return id;
        };
        for(auto material:part.posed.materials) {
            material.diffuseTexture=composed(material.diffuseTexture);
            material.bumpTexture=composed(material.bumpTexture);
            out.materials.push_back(std::move(material));
        }
        for(auto triangle:part.posed.triangles) {
            triangle.a+=uint32_t(vertexBase);triangle.b+=uint32_t(vertexBase);triangle.c+=uint32_t(vertexBase);
            if(triangle.material>=0)triangle.material+=int32_t(materialBase);
            out.triangles.push_back(triangle);
        }
        out.primitiveCount+=part.posed.primitiveCount;
    }
    return out;
}
} // namespace forge::creatureassembly
