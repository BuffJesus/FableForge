#pragma once
#include "definitionwrite.hpp"
#include <iterator>
namespace albion::definitionwrite {
inline std::vector<uint8_t> cloneBytes(const std::filesystem::path& path) {
    std::ifstream stream(path,std::ios::binary);
    if(!stream) throw std::runtime_error("definition clone: cannot read "+path.string());
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(stream),{});
}
inline void verifyCloneRecords(const forge::bin::File& expected,const forge::bin::File& actual,size_t count) {
    if(expected.entries().size()<count || actual.entries().size()!=count)
        throw std::runtime_error("definition clone: record count changed");
    for(size_t i=0;i<count;++i) {
        const auto& a=expected.entries()[i];const auto& b=actual.entries()[i];
        if(a.name!=b.name || a.definition!=b.definition || a.indexInDefinition!=b.indexInDefinition || a.data!=b.data)
            throw std::runtime_error("definition clone: record changed at index "+std::to_string(i));
    }
}
// Clone publication appends a name pool and bank in one rollback-capable pair.
// Existing outputs must still be the exact loaded namespace and record set.
inline void publishClone(const forge::bin::File& bank,const std::filesystem::path& sourceNames,
                         const std::filesystem::path& sourceBank,const std::filesystem::path& destination,
                         bool trackRecovery=true) {
    namespace fs=std::filesystem;
    const auto target=fs::absolute(destination).lexically_normal(),names=target.parent_path()/"names.bin";
    const auto sameFile=[](const fs::path& a,const fs::path& b) {
        std::error_code error;return paths::resolvedPath(a)==paths::resolvedPath(b) || fs::equivalent(a,b,error);
    };
    if(target.filename().empty() || sameFile(target,names) || sameFile(target,sourceNames) || sameFile(names,sourceBank))
        throw std::runtime_error("definition clone: bank and name-pool paths must be distinct");
    for(const auto& pair:{std::pair{target,sourceBank},std::pair{names,sourceNames}})
        if(sameFile(pair.first,pair.second) && paths::resolvedPath(pair.first)!=paths::resolvedPath(pair.second))
            throw std::runtime_error("definition clone: output aliases a source through a hard link");
    requireOffline(target);requireOffline(names);
    const auto source=forge::bin::File::open(sourceNames,sourceBank);
    if(bank.entries().size()!=source.entries().size()+1 || bank.originalNames()!=source.originalNames())
        throw std::runtime_error("definition clone: expected one appended record in the loaded namespace");
    verifyCloneRecords(bank,source,source.entries().size());
    const auto& clone=bank.entries().back();
    if(clone.name.empty() || std::any_of(source.entries().begin(),source.entries().end(),[&](const auto& e){return e.name==clone.name;}))
        throw std::runtime_error("definition clone: appended name is not unique");
    if(std::none_of(source.entries().begin(),source.entries().end(),[&](const auto& e){return e.definition==clone.definition && e.data==clone.data;}))
        throw std::runtime_error("definition clone: appended payload does not match a donor");
    const auto originalNames=cloneBytes(sourceNames);
    const bool newBank=!fs::exists(target),newNames=!fs::exists(names);
    const auto checkDestination=[&] {
        if(newBank!=!fs::exists(target) || newNames!=!fs::exists(names))
            throw std::runtime_error("definition clone: destination appeared or disappeared");
        if(!newNames && cloneBytes(names)!=originalNames)
            throw std::runtime_error("definition clone: destination name pool differs from the loaded source");
        if(!newBank) {
            if(newNames) throw std::runtime_error("definition clone: existing destination lacks its name pool");
            verifyCloneRecords(source,forge::bin::File::open(names,target),source.entries().size());
        }
    };
    checkDestination();
    if(trackRecovery) for(const auto& path:{target,names}) if(!fs::exists(path))
        for(const char* suffix:{backups::kOrigSuffix,backups::kLegacyOrigSuffix,backups::kCreatedSuffix,
                               backups::kLegacyCreatedSuffix,backups::kStagedSuffix,backups::kOverlaySuffix})
            if(fs::exists(fs::symlink_status(fs::path(path.string()+suffix))))
                throw std::runtime_error("definition clone: absent output has recovery records");
    detail::PendingBanks pending(target.parent_path(),"forge-def-clone-");
    const auto preparedBank=pending.prepare(target.filename()),preparedNames=pending.prepare("names.bin");
    bank.save(preparedNames,preparedBank);
    const auto prepared=forge::bin::File::open(preparedNames,preparedBank);
    verifyCloneRecords(bank,prepared,bank.entries().size());
    const auto newPool=cloneBytes(preparedNames);
    if(originalNames.size()<20 || newPool.size()<originalNames.size() ||
       !std::equal(originalNames.begin(),originalNames.begin()+8,newPool.begin()) ||
       !std::equal(originalNames.begin()+16,originalNames.end(),newPool.begin()+16))
        throw std::runtime_error("definition clone: prepared name-pool prefix changed");
    if(trackRecovery) for(const auto& item:{std::pair{target,newBank},std::pair{names,newNames}}) if(item.second) {
        const auto marker=pending.prepare(fs::path(item.first.filename().string()+backups::kCreatedSuffix));
        std::ofstream stream(marker,std::ios::binary);stream<<"created by FableForge\n";stream.close();
        if(!stream) throw std::runtime_error("definition clone: cannot prepare creation marker");
    }
    requireOffline(target);requireOffline(names);
    const auto current=forge::bin::File::open(sourceNames,sourceBank);
    verifyCloneRecords(source,current,source.entries().size());
    if(cloneBytes(sourceNames)!=originalNames) throw std::runtime_error("definition clone: source name pool changed during preparation");
    checkDestination();
    std::string error;if(!pending.install(trackRecovery,error)) throw std::runtime_error("definition clone: "+error);
}
} // namespace albion::definitionwrite
