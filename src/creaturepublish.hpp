#pragma once
#include "creatureimport.hpp"
#include "definitionclone.hpp"
#include "forge/modorder.hpp"
#include "forge/terraintex.hpp"
#include <array>
#include <functional>

namespace albion::creatureimport {
namespace fs=std::filesystem;
struct Sources {fs::path names,definitions,graphics,textures;};
struct FileProof {fs::path resolved;std::string sha256;};
struct TextureAppend {fs::path image;std::string name,format;FileProof proof;};
inline TextureAppend captureTexture(const fs::path& image,std::string name,std::string format="dxt1") {
    if(!fs::is_regular_file(image)) throw std::runtime_error("creature publication: texture image is not a file");
    return {image,std::move(name),std::move(format),{paths::resolvedPath(image),forge::modorder::sha256Of(image)}};
}
using SourceProof=std::array<FileProof,4>;
inline SourceProof captureSources(const Sources& source) {
    SourceProof proof;size_t index=0;
    for(const auto& path:{source.names,source.definitions,source.graphics,source.textures}) {
        if(!fs::is_regular_file(path)) throw std::runtime_error("creature publication: source is not a file");
        proof[index++]={paths::resolvedPath(path),forge::modorder::sha256Of(path)};
    }
    return proof;
}
inline void requireSourceProof(const Sources& source,const SourceProof& expected) {
    const auto current=captureSources(source);
    for(size_t i=0;i<current.size();++i)
        if(current[i].resolved!=expected[i].resolved || current[i].sha256!=expected[i].sha256)
            throw std::runtime_error("creature publication: source changed at input "+std::to_string(i)+
                (current[i].resolved!=expected[i].resolved?" (resolved path)":" (content)"));
}
inline void verifyGraphics(const forge::big::File& expected,const forge::big::File& actual) {
    if(expected.magic()!=actual.magic() || expected.version()!=actual.version() || expected.banks().size()!=actual.banks().size())
        throw std::runtime_error("creature publication: graphics readback header differs");
    for(size_t i=0;i<expected.banks().size();++i) {
        const auto& a=expected.banks()[i];const auto& b=actual.banks()[i];
        if(a.name!=b.name || a.id!=b.id || a.blockSize!=b.blockSize || a.entries.size()!=b.entries.size())
            throw std::runtime_error("creature publication: graphics bank readback differs");
        for(size_t j=0;j<a.entries.size();++j) {
            const auto& x=a.entries[j];const auto& y=b.entries[j];
            if(x.name!=y.name || x.id!=y.id || x.type!=y.type || x.magic!=y.magic || x.devFileType!=y.devFileType ||
               x.devCrc!=y.devCrc || x.devSources!=y.devSources || x.subHeader!=y.subHeader || expected.entryData(x)!=actual.entryData(y))
                throw std::runtime_error("creature publication: graphics entry readback differs");
        }
    }
}
// Optional diffuse texture is appended inside the same staged transaction.
// Other material maps continue to refer to unique existing texture IDs.
// Expected source proof is captured when the caller selects its inputs/draft.
inline MeshPlan publishMesh(const Sources& source,const SourceProof& proof,
        const fs::path& destinationRoot,const fs::path& graphicsRelative,
        const forge::defschema::Schema& schema,const MeshRequest& request,
        const meshimport::Model& model,const std::vector<forge::meshcompose::Material>& materials,
        bool trackRecovery=true,const std::function<void()>& beforeInstall={},const TextureAppend* textureAppend=nullptr) {
    const auto fail=[](const char* reason){throw std::runtime_error(std::string("creature publication: ")+reason);};
    if(source.names.filename()!="names.bin" || source.definitions.filename()!="game.bin" ||
       paths::resolvedPath(source.names.parent_path())!=paths::resolvedPath(source.definitions.parent_path()))
        fail("source definitions require paired game.bin/names.bin");
    if(graphicsRelative!=fs::path("data/graphics/graphics.big") && graphicsRelative!=fs::path("data/graphics/pc/graphics.big"))
        fail("unsupported graphics destination");
    const auto root=fs::absolute(destinationRoot).lexically_normal(),resolvedRoot=paths::resolvedPath(root);
    const std::array<fs::path,4> targets{root/"data/CompiledDefs/names.bin",root/"data/CompiledDefs/game.bin",
                                       root/graphicsRelative,root/"data/graphics/pc/textures.big"};
    std::array<FileProof,4> destinationProof;std::array<bool,4> present{};
    requireSourceProof(source,proof);
    for(size_t i=0;i<targets.size();++i) {
        const auto resolved=paths::resolvedPath(targets[i]);const auto relative=resolved.lexically_relative(resolvedRoot);
        if(relative.empty() || relative.is_absolute() || *relative.begin()=="..") fail("destination escapes install root");
        for(size_t j=0;j<i;++j) {
            std::error_code ec;
            if(resolved==destinationProof[j].resolved || fs::equivalent(targets[i],targets[j],ec)) fail("destination files alias");
        }
        const std::array<fs::path,4> inputPaths{source.names,source.definitions,source.graphics,source.textures};
        for(size_t j=0;j<inputPaths.size();++j) {
            std::error_code ec;
            if((resolved==proof[j].resolved || fs::equivalent(targets[i],inputPaths[j],ec)) &&
               (i!=j || resolved!=proof[j].resolved)) fail("destination aliases an unrelated or hardlinked source");
        }
        present[i]=fs::exists(targets[i]);
        if(present[i] && !fs::is_regular_file(targets[i])) fail("destination is not a file");
        destinationProof[i]={resolved,present[i]?forge::modorder::sha256Of(targets[i]):std::string()};
        if(present[i] && destinationProof[i].sha256!=proof[i].sha256) fail("destination differs from loaded source");
        if(!present[i] && trackRecovery) for(const char* suffix:{backups::kOrigSuffix,backups::kLegacyOrigSuffix,
            backups::kCreatedSuffix,backups::kLegacyCreatedSuffix,backups::kStagedSuffix,backups::kOverlaySuffix})
            if(fs::exists(fs::symlink_status(fs::path(targets[i].string()+suffix)))) fail("absent destination has recovery records");
    }
    const auto validate=[&] {
        if(backups::gameRunningIn(root) || backups::gameRunningIn(resolvedRoot)) fail("close Fable before publishing");
        requireSourceProof(source,proof);
        if(textureAppend && (!fs::is_regular_file(textureAppend->image) || paths::resolvedPath(textureAppend->image)!=textureAppend->proof.resolved ||
            forge::modorder::sha256Of(textureAppend->image)!=textureAppend->proof.sha256)) fail("texture source changed");
        for(size_t i=0;i<targets.size();++i)
            if(paths::resolvedPath(targets[i])!=destinationProof[i].resolved || fs::exists(targets[i])!=present[i] ||
               (present[i] && forge::modorder::sha256Of(targets[i])!=destinationProof[i].sha256)) fail("destination changed");
    };
    validate();
    const auto textures=forge::big::File::open(source.textures);const forge::big::Bank* textureBank=nullptr;
    for(const auto& bank:textures.banks()) if(bank.name=="GBANK_MAIN_PC") {if(textureBank) fail("ambiguous texture bank");textureBank=&bank;}
    uint32_t appendedTextureId=0;auto effectiveMaterials=materials;
    if(textureAppend) {
        if(textures.magic()!="BIGB" || !textureBank || textureBank->entries.empty()) fail("texture append requires nonempty BIGB GBANK_MAIN_PC");
        if(textureAppend->name.empty() || textureAppend->name.size()>=80 ||
            !std::all_of(textureAppend->name.begin(),textureAppend->name.end(),[](char c){return (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_';}))
            fail("invalid new texture name");
        for(const auto& bank:textures.banks()) for(const auto& entry:bank.entries)
            if(entry.name==textureAppend->name) fail("new texture name already exists");
        for(const auto& entry:textureBank->entries) appendedTextureId=std::max(appendedTextureId,entry.id);
        if(appendedTextureId==UINT32_MAX) fail("no free texture entry ID");
        ++appendedTextureId;
        for(auto& material:effectiveMaterials) material.diffuseId=appendedTextureId;
    }
    for(const auto& material:materials) {
        for(uint32_t id:{textureAppend?0u:material.diffuseId,material.bumpId,material.reflectId,material.illumId,
                        material.decalId>0?uint32_t(material.decalId):0u}) if(id) {
            if(!textureBank || std::count_if(textureBank->entries.begin(),textureBank->entries.end(),[&](const auto& e){return e.id==id;})!=1)
                fail("missing or ambiguous material texture ID");
        }
    }
    auto plan=prepareMesh(forge::bin::File::open(source.names,source.definitions),forge::big::File::open(source.graphics),schema,request,model,effectiveMaterials);
    detail::PendingBanks pending(root,".forge-creature-import-");
    std::optional<forge::big::File> publishedTextures;
    if(textureAppend) {
        const auto preparedTexture=pending.prepare("data/graphics/pc/textures.big");
        forge::terraintex::ImportRequest textureRequest;
        textureRequest.png=textureAppend->image;textureRequest.srcBig=source.textures;textureRequest.outBig=preparedTexture;
        textureRequest.entryName=textureAppend->name;textureRequest.format=textureAppend->format;textureRequest.add=true;
        const auto imported=forge::terraintex::importPng(textureRequest);
        if(!imported.ok) {
            std::string error="creature publication: texture import failed";
            for(const auto& reason:imported.validation.errors) error+="; "+reason;
            throw std::runtime_error(error);
        }
        publishedTextures=forge::big::File::openFully(preparedTexture);
        const auto* preparedBank=publishedTextures->findBank("GBANK_MAIN_PC");
        if(!preparedBank || preparedBank->entries.size()!=textureBank->entries.size()+1 || imported.entryId!=appendedTextureId ||
            preparedBank->entries.back().id!=appendedTextureId || preparedBank->entries.back().name!=textureAppend->name || preparedBank->entries.back().type!=0)
            fail("prepared texture append differs");
        auto expectedTextures=textures;
        expectedTextures.findBank("GBANK_MAIN_PC")->entries.push_back(preparedBank->entries.back());
        // Own payload for the added record, rather than its staged file offsets.
        auto& added=expectedTextures.findBank("GBANK_MAIN_PC")->entries.back();added.data=publishedTextures->entryData(added);
        verifyGraphics(expectedTextures,*publishedTextures);
    }
    const auto preparedNames=pending.prepare("data/CompiledDefs/names.bin"),preparedBank=pending.prepare("data/CompiledDefs/game.bin");
    plan.definitions.save(preparedNames,preparedBank);
    definitionwrite::verifyCloneRecords(plan.definitions,forge::bin::File::open(preparedNames,preparedBank),plan.definitions.entries().size());
    const auto oldNames=definitionwrite::cloneBytes(source.names),newNames=definitionwrite::cloneBytes(preparedNames);
    if(oldNames.size()<20 || newNames.size()<oldNames.size() ||
       !std::equal(oldNames.begin(),oldNames.begin()+8,newNames.begin()) ||
       !std::equal(oldNames.begin()+16,oldNames.end(),newNames.begin()+16)) fail("prepared name-pool prefix changed");
    const auto preparedGraphics=pending.prepare(graphicsRelative);const auto bytes=plan.graphics.serialize();
    {std::ofstream stream(preparedGraphics,std::ios::binary);stream.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
     stream.close();if(!stream) fail("cannot prepare graphics bank");}
    auto publishedGraphics=forge::big::File::openFully(preparedGraphics);
    verifyGraphics(plan.graphics,publishedGraphics);
    auto publishedDefinitions=forge::bin::File::open(preparedNames,preparedBank);
    if(trackRecovery) for(size_t i=0;i<(textureAppend?4u:3u);++i) if(!present[i]) {
        const auto marker=pending.prepare(fs::path(targets[i].lexically_relative(root).string()+backups::kCreatedSuffix));
        std::ofstream stream(marker,std::ios::binary);stream<<"created by FableForge\n";stream.close();if(!stream) fail("cannot prepare creation marker");
    }
    if(beforeInstall) beforeInstall();validate();
    std::string error;if(!pending.install(trackRecovery,error)) throw std::runtime_error("creature publication: "+error);
    // No fallible read after commit, and no stale lazy offsets in an in-place
    // result. Staged output was fully reopened and verified before installation.
    plan.definitions=std::move(publishedDefinitions);plan.graphics=std::move(publishedGraphics);
    plan.textures=std::move(publishedTextures);plan.textureId=appendedTextureId;
    return plan;
}
} // namespace albion::creatureimport
