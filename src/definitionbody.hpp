#pragma once
#include "definitionclone.hpp"
namespace albion::definitionwrite {
struct CreatureBodyContract {
    const forge::defschema::Schema& schema;
    const forge::big::File& graphics;
    const std::vector<uint8_t>& sourceBankBytes;
    const std::vector<uint8_t>& sourceNamesBytes;
    size_t entryIndex,bodyPart,alternative;
    uint32_t modelId;
};
// Binding uses graphics directory identities, not mesh payload equivalence.
inline bool sameBodyGraphics(const forge::big::File& a,const forge::big::File& b) {
    if(a.banks().size()!=b.banks().size())return false;
    for(size_t i=0;i<a.banks().size();++i) {
        const auto& x=a.banks()[i];const auto& y=b.banks()[i];
        if(x.name!=y.name || x.id!=y.id || x.entries.size()!=y.entries.size())return false;
        for(size_t j=0;j<x.entries.size();++j) {
            const auto& p=x.entries[j];const auto& q=y.entries[j];
            if(p.id!=q.id || p.type!=q.type || p.name!=q.name || p.length!=q.length ||
               p.dataOffset!=q.dataOffset || p.subHeader!=q.subHeader)return false;
        }
    }
    return true;
}
inline void publishCreatureBody(const forge::bin::File& proposed,
        const std::filesystem::path& sourceNames,const std::filesystem::path& sourceBank,
        const std::filesystem::path& graphicsPath,const std::filesystem::path& destination,
        const CreatureBodyContract& contract,bool trackRecovery=true) {
    namespace fs=std::filesystem;
    const auto target=fs::absolute(destination).lexically_normal(),names=target.parent_path()/"names.bin";
    if(sourceBank.filename()!="game.bin" || target.filename()!="game.bin")
        throw std::runtime_error("body binding: source and destination must be game.bin");
    const auto sameFile=[](const fs::path& a,const fs::path& b) {
        std::error_code error;return paths::resolvedPath(a)==paths::resolvedPath(b) || fs::equivalent(a,b,error);
    };
    if(sameFile(target,sourceNames) || sameFile(names,sourceBank))
        throw std::runtime_error("body binding: bank/name-pool source aliases conflict");
    for(const auto& output:{target,names}) {
        if(sameFile(output,graphicsPath))throw std::runtime_error("body binding: output aliases graphics");
        for(const auto& source:{sourceBank,sourceNames})
            if(sameFile(output,source) && paths::resolvedPath(output)!=paths::resolvedPath(source))
                throw std::runtime_error("body binding: output hardlinks a source");
    }
    const bool newBank=!fs::exists(target),newNames=!fs::exists(names);
    const auto validate=[&] {
        requireOffline(target);requireOffline(names);
        if(cloneBytes(sourceBank)!=contract.sourceBankBytes || cloneBytes(sourceNames)!=contract.sourceNamesBytes)
            throw std::runtime_error("body binding: source banks changed");
        if(!sameBodyGraphics(contract.graphics,forge::big::File::open(graphicsPath)))
            throw std::runtime_error("body binding: graphics directory changed");
        const auto destinationGraphics=target.parent_path().parent_path()/"graphics"/"graphics.big";
        if(fs::exists(destinationGraphics) &&
           !sameBodyGraphics(contract.graphics,forge::big::File::open(destinationGraphics)))
            throw std::runtime_error("body binding: destination graphics directory differs");
        if(newBank!=!fs::exists(target) || newNames!=!fs::exists(names))
            throw std::runtime_error("body binding: destination appeared or disappeared");
        if(!newNames && cloneBytes(names)!=contract.sourceNamesBytes)
            throw std::runtime_error("body binding: destination namespace differs");
        if(!newBank && (newNames || cloneBytes(target)!=contract.sourceBankBytes))
            throw std::runtime_error("body binding: destination differs from loaded source");
    };
    validate();
    auto verified=forge::bin::File::open(sourceNames,sourceBank);
    forge::defedit::setCreatureBodyModel(verified,contract.schema,contract.entryIndex,
        contract.bodyPart,contract.alternative,contract.modelId,contract.graphics);
    verifyEntries(verified,proposed);
    publish(proposed,sourceNames,target,trackRecovery,validate);
}
} // namespace albion::definitionwrite
