#pragma once
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "pendingbanks.hpp"
#include "textdestination.hpp"
#include "textmetadata.hpp"
#include "forge/big.hpp"
#include "forge/textbig.hpp"
namespace albion::textwrite {
// Retail string headers index the bank's existing narrator list. Headerless
// generic string fixtures have no index to synchronize.
inline std::vector<uint8_t> synchronizeSpeakerHeader(forge::big::File& file,
                                                    const forge::textbig::UpsertResult& result) {
    for(auto& bank:file.banks()) {
        if(bank.name!=result.bank) continue;
        auto found=std::find_if(bank.entries.begin(),bank.entries.end(),[&](const auto& record){
            return record.name==result.name && record.id==result.id && record.type==0;
        });
        if(found==bank.entries.end()) throw std::runtime_error("text write: speaker entry identity changed");
        if(found->subHeader.empty()) {
            if(std::any_of(bank.entries.begin(),bank.entries.end(),[](const auto& record){return record.type==2;}))
                throw std::runtime_error("text write: missing speaker header in a narrator bank");
            return {};
        }
        if(found->subHeader.size()!=4) throw std::runtime_error("text write: unsupported speaker header");
        const forge::big::Entry* narrators=nullptr;
        for(const auto& record:bank.entries) if(record.type==2) {
            if(narrators || record.name!="[NarratorList]" || record.length!=0)
                throw std::runtime_error("text write: malformed or ambiguous narrator list");
            narrators=&record;
        }
        if(!narrators) throw std::runtime_error("text write: speaker header has no narrator list");
        const auto names=forge::textbig::decodeNarrators(narrators->subHeader);
        const auto& info=found->subHeader;
        const uint32_t previous=uint32_t(info[0])|uint32_t(info[1])<<8|uint32_t(info[2])<<16|uint32_t(info[3])<<24;
        if(previous>=names.size()) throw std::runtime_error("text write: speaker header index is out of range");
        const auto value=forge::textbig::decode(file.entryData(*found),0);
        std::optional<uint32_t> index;
        for(size_t i=0;i<names.size();++i) if(names[i]==value.speaker) {
            if(index) throw std::runtime_error("text write: speaker name is ambiguous in the narrator list");
            index=uint32_t(i);
        }
        if(!index) throw std::runtime_error("text write: speaker is not in the existing narrator list");
        for(unsigned i=0;i<4;++i) found->subHeader[i]=uint8_t(*index>>(i*8));
        return found->subHeader;
    }
    throw std::runtime_error("text write: speaker bank identity changed");
}
template<class Verifier>
void publishTextBank(const forge::big::File& bank,const std::filesystem::path& destination,
                     bool makeBackups,Verifier&& verify,bool trackCreation=false) {
    namespace fs=std::filesystem;
    const auto target=fs::absolute(destination).lexically_normal();
    if(target.filename().empty()) throw std::runtime_error("text write: output needs a filename");
    requireTextDestinationOffline(target);
    const bool created=trackCreation && !fs::exists(target);
    if(created) {
        for(const char* suffix:{backups::kOrigSuffix,backups::kLegacyOrigSuffix,backups::kCreatedSuffix,
                              backups::kLegacyCreatedSuffix,backups::kStagedSuffix,backups::kOverlaySuffix})
            if(fs::exists(fs::symlink_status(fs::path(target.string()+suffix))))
                throw std::runtime_error("Text destination has recovery records; restore or resolve them before creating a bank.");
    }
    const auto bytes=bank.serialize();
    albion::detail::PendingBanks pending(target.parent_path(),".forge-text-write-");
    const auto prepared=pending.prepare(target.filename());
    std::ofstream stream;
    stream.exceptions(std::ios::failbit|std::ios::badbit);
    stream.open(prepared,std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
    stream.close();
    const auto verified=forge::big::File::open(prepared);
    verify(verified);
    if(created) {
        const auto marker=pending.prepare(fs::path(target.filename().string()+backups::kCreatedSuffix));
        std::ofstream markerStream;markerStream.exceptions(std::ios::failbit|std::ios::badbit);
        markerStream.open(marker,std::ios::binary);markerStream<<"created by FableForge\n";markerStream.close();
        if(fs::exists(target)) throw std::runtime_error("Text destination appeared during preparation; refresh before editing.");
    }
    requireTextDestinationOffline(target);
    std::string error;
    if(!pending.install(makeBackups,error)) throw std::runtime_error("text write: "+error);
}

inline void applyMetadata(forge::textbig::Entry& value,const MetadataChanges& changes) {
    for(const auto* field:{&changes.speechBank,&changes.identifier})
        if(*field && !editableMetadata(**field))
            throw std::runtime_error("Text metadata must be ASCII and smaller than 4096 bytes.");
    if(changes.speaker) value.speaker=*changes.speaker;
    if(changes.speechBank) value.speechBank=*changes.speechBank;
    if(changes.identifier) value.identifier=*changes.identifier;
    if(changes.tags) {
        for(const auto& tag:*changes.tags) if(!editableMetadata(tag.name) &&
            std::none_of(value.tags.begin(),value.tags.end(),[&](const auto& old){return old.name==tag.name && old.position==tag.position;}))
            throw std::runtime_error("New tag names must be ASCII and smaller than 4096 bytes.");
        value.tags=*changes.tags;
    }
}
// Preserve metadata unless explicitly requested and refuse stale snapshots.
inline void replaceContent(const std::filesystem::path& source,
                           const std::filesystem::path& destination,
                           const std::string& name, const std::string& bankName,
                           uint32_t id, const forge::textbig::Entry& expected,
                           const std::string& content,
                           const MetadataChanges& changes={},
                           const std::vector<uint8_t>& expectedHeader={}) {
    auto file=forge::big::File::open(source);
    forge::big::Entry* selected=nullptr;
    for(auto& bank:file.banks()) for(auto& record:bank.entries) {
        if(record.name!=name) continue;
        if(selected) throw std::runtime_error("Text name is ambiguous; no files were changed.");
        if(bank.name!=bankName || record.id!=id || record.type!=0)
            throw std::runtime_error("Text identity changed; refresh before editing.");
        selected=&record;
    }
    if(!selected) throw std::runtime_error("Text entry disappeared; refresh before editing.");
    const auto originalPayload=file.entryData(*selected);
    auto value=forge::textbig::decode(originalPayload,0);
    if(forge::textbig::encode(value)!=originalPayload)
        throw std::runtime_error("Text payload has unsupported bytes; this string cannot be edited safely.");
    if(forge::textbig::encode(value)!=forge::textbig::encode(expected))
        throw std::runtime_error("Text changed on disk; refresh before editing.");
    value.content=content;
    if(changes.speaker) {
        if(selected->subHeader!=expectedHeader) throw std::runtime_error("Speaker header changed on disk; refresh before editing.");
    }
    applyMetadata(value,changes);
    const auto payload=forge::textbig::encode(value);
    selected->data=payload;selected->length=uint32_t(payload.size());selected->dataOffset=0;
    if(changes.speaker) synchronizeSpeakerHeader(file,{id,name,bankName,false});
    const auto header=selected->subHeader;
    publishTextBank(file,destination,true,[&](const auto& verified) {
        const forge::big::Entry* found=nullptr;
        for(const auto& bank:verified.banks()) for(const auto& record:bank.entries)
            if(record.name==name) {
                if(found || bank.name!=bankName || record.id!=id || record.type!=0)
                    throw std::runtime_error("Text identity failed prepared-bank verification.");
                found=&record;
            }
        if(!found || found->subHeader!=header || verified.entryData(*found)!=payload)
            throw std::runtime_error("Text content failed prepared-bank verification.");
    },true);
}
inline forge::textbig::UpsertResult addStringFromDonor(
        const std::filesystem::path& source,const std::filesystem::path& destination,
        const std::string& donorName,const std::string& bankName,uint32_t donorId,
        const forge::textbig::Entry& expected,const std::string& name,const std::string& content,
        const MetadataChanges& changes={},
        const std::vector<uint8_t>& expectedHeader={}) {
    auto file=forge::big::File::open(source);
    const forge::big::Entry* donor=nullptr;
    for(const auto& bank:file.banks()) for(const auto& record:bank.entries) {
        if(record.name==name) throw std::runtime_error("That text name already exists; choose a new name.");
        if(record.name!=donorName) continue;
        if(donor) throw std::runtime_error("Donor name is ambiguous; choose a unique string.");
        if(bank.name!=bankName || record.id!=donorId || record.type!=0)
            throw std::runtime_error("Donor identity changed; refresh before adding a string.");
        donor=&record;
    }
    if(!donor) throw std::runtime_error("Donor disappeared; refresh before adding a string.");
    const auto originalPayload=file.entryData(*donor);
    auto value=forge::textbig::decode(originalPayload,0);
    if(forge::textbig::encode(value)!=originalPayload)
        throw std::runtime_error("Donor payload has unsupported bytes; choose another string.");
    if(forge::textbig::encode(value)!=forge::textbig::encode(expected))
        throw std::runtime_error("Donor changed on disk; refresh before adding a string.");
    value.content=content;value.identifier=name;
    if(changes.identifier && *changes.identifier!=name)
        throw std::runtime_error("A new string uses its new name as identifier.");
    if(changes.speaker) {
        if(donor->subHeader!=expectedHeader) throw std::runtime_error("Donor speaker header changed on disk; refresh before adding.");
    }
    applyMetadata(value,changes);
    const auto payload=forge::textbig::encode(value);
    const auto result=forge::textbig::upsertString(file,name,value,std::nullopt,donorName);
    if(!result.added || result.bank!=bankName) throw std::runtime_error("New string identity was not allocated as expected.");
    const auto header=changes.speaker?synchronizeSpeakerHeader(file,result):expectedHeader;
    publishTextBank(file,destination,true,[&](const auto& verified) {
        const forge::big::Entry* found=nullptr;
        for(const auto& bank:verified.banks()) for(const auto& record:bank.entries)
            if(record.name==name) {
                if(found || bank.name!=result.bank || record.id!=result.id || record.type!=0)
                    throw std::runtime_error("New string identity failed prepared-bank verification.");
                found=&record;
            }
        if(!found || (changes.speaker && found->subHeader!=header) || verified.entryData(*found)!=payload)
            throw std::runtime_error("New string content failed prepared-bank verification.");
    },true);
    return result;
}
} // namespace albion::textwrite
