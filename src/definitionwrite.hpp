#pragma once
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include "forge/bin.hpp"
#include "pendingbanks.hpp"
#include "resolvedpath.hpp"
namespace albion::definitionwrite {
inline void requireOffline(const std::filesystem::path& destination) {
    namespace fs=std::filesystem;
    for(const auto& path:{fs::absolute(destination).lexically_normal(),paths::resolvedPath(destination)}) {
        auto folder=path.parent_path();auto name=folder.filename().string();
        std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return char(std::tolower(c));});
        auto data=folder.parent_path();auto dataName=data.filename().string();
        std::transform(dataName.begin(),dataName.end(),dataName.begin(),[](unsigned char c){return char(std::tolower(c));});
        if(name=="compileddefs" && dataName=="data" && backups::gameRunningIn(data.parent_path()))
            throw std::runtime_error("definition write: close Fable.exe in the destination install before writing");
    }
}
inline void verifyEntries(const forge::bin::File& expected,const forge::bin::File& actual) {
    if(expected.originalNames()!=actual.originalNames() || expected.entries().size()!=actual.entries().size())
        throw std::runtime_error("definition write: prepared name pool or record count differs");
    for(size_t i=0;i<expected.entries().size();++i) {
        const auto& a=expected.entries()[i];const auto& b=actual.entries()[i];
        if(a.name!=b.name || a.definition!=b.definition || a.indexInDefinition!=b.indexInDefinition || a.data!=b.data)
            throw std::runtime_error("definition write: prepared record differs at index "+std::to_string(i));
    }
}
// Existing-entry edits add no names. Retain the original pool bytes and publish
// only a missing destination pool; an existing pool must decode every identity.
inline void publish(const forge::bin::File& bank,const std::filesystem::path& sourceNames,
                    const std::filesystem::path& destination,bool trackRecovery=false) {
    namespace fs=std::filesystem;
    const auto target=fs::absolute(destination).lexically_normal();
    const auto names=target.parent_path()/"names.bin";
    if(target.filename().empty() || paths::resolvedPath(target)==paths::resolvedPath(names) ||
       paths::resolvedPath(target)==paths::resolvedPath(sourceNames))
        throw std::runtime_error("definition write: bank output must be distinct from names.bin");
    std::error_code identityError;
    if(fs::equivalent(target,sourceNames,identityError)) throw std::runtime_error("definition write: bank output aliases the source name pool");
    requireOffline(target);requireOffline(names);
    const bool newBank=!fs::exists(target),newNames=!fs::exists(names);
    if(!newNames && !fs::is_regular_file(names)) throw std::runtime_error("definition write: destination names.bin is not a file");
    if(trackRecovery) for(const auto& file:{target,names}) if(!fs::exists(file))
        for(const char* suffix:{backups::kOrigSuffix,backups::kLegacyOrigSuffix,backups::kCreatedSuffix,
                              backups::kLegacyCreatedSuffix,backups::kStagedSuffix,backups::kOverlaySuffix})
            if(fs::exists(fs::symlink_status(fs::path(file.string()+suffix))))
                throw std::runtime_error("definition write: absent destination has recovery records");
    detail::PendingBanks pending(target.parent_path(),"forge-def-prepare-");
    const auto preparedBank=pending.prepare(target.filename());
    // File::save normalizes pool headers. Its temporary pool is never published.
    const auto normalizedNames=preparedBank.parent_path()/"pool-normalized.bin";
    bank.save(normalizedNames,preparedBank);
    verifyEntries(bank,forge::bin::File::open(normalizedNames,preparedBank));
    const auto originalNames=preparedBank.parent_path()/"pool-original.bin";
    fs::copy_file(sourceNames,originalNames,fs::copy_options::none);
    verifyEntries(bank,forge::bin::File::open(originalNames,preparedBank));
    if(newNames) {
        const auto preparedNames=pending.prepare("names.bin");
        fs::copy_file(originalNames,preparedNames,fs::copy_options::none);
    } else verifyEntries(bank,forge::bin::File::open(names,preparedBank));
    if(trackRecovery) for(const auto& item:{std::pair{target,newBank},std::pair{names,newNames}}) if(item.second) {
        const auto marker=pending.prepare(fs::path(item.first.filename().string()+backups::kCreatedSuffix));
        std::ofstream stream(marker,std::ios::binary);stream<<"created by FableForge\n";stream.close();
        if(!stream) throw std::runtime_error("definition write: cannot prepare creation marker");
    }
    requireOffline(target);requireOffline(names);
    if(newBank && fs::exists(target)) throw std::runtime_error("definition write: bank destination appeared during preparation");
    if(newNames && fs::exists(names)) throw std::runtime_error("definition write: name pool destination appeared during preparation");
    if(!newNames) verifyEntries(bank,forge::bin::File::open(names,preparedBank));
    std::string error;if(!pending.install(trackRecovery,error)) throw std::runtime_error("definition write: "+error);
}
} // namespace albion::definitionwrite
