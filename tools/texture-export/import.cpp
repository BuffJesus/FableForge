#include "import.hpp"
#include "backups.hpp"
#include "forge/modorder.hpp"
#include "forge/temporarydirectory.hpp"
#include "pendingbanks.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <set>
#include <string_view>
#include <stdexcept>

namespace fs=std::filesystem;
namespace tp=albion::textureproject;

namespace textureimport {
namespace {
void need(bool condition,const std::string& message) {if(!condition) throw std::runtime_error(message);}
tp::Options options() {tp::Options o;o.imageSize=true;o.maxDimension=2048;o.unsignedArchiveOffsets=true;return o;}
constexpr const char* archiveDllSha="6d94484d9303d63a0423353f858e9787d099487de9b3928e5619b752f9832b52";

// Live CGraphicDataBank handles for the disk banks whose sizes the runtime corrects.
const std::map<std::string,std::string> runtimeHandles{
    {"GBANK_MAIN_PC","GBANK_MAIN"},{"GBANK_FRONT_END_PC","GBANK_FRONT_END"}};
const char* const sizeArchives[]{"data/graphics/pc/textures.big","data/graphics/pc/frontend.big"};

std::uint16_t word(const std::vector<std::uint8_t>& bytes,size_t at) {return std::uint16_t(bytes[at]|bytes[at+1]<<8);}
void put32(std::vector<unsigned char>& out,std::uint32_t v) {for(int i=0;i<4;++i) out.push_back(static_cast<unsigned char>(v>>(8*i)));}
void put16(std::vector<unsigned char>& out,std::uint16_t v) {out.push_back(static_cast<unsigned char>(v));out.push_back(static_cast<unsigned char>(v>>8));}

void writeAtomically(const fs::path& target,const std::vector<unsigned char>& bytes) {
    fs::create_directories(target.parent_path());
    const auto staged=target.string()+".forge-new";
    {std::ofstream out(staged,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));out.close();need(bool(out),"Could not write "+target.filename().string());}
    fs::rename(staged,target);
}

// Ensure Mods.ini enables the DLL. Returns false when the install has no Mods.ini.
bool registerMod(const fs::path& root,const std::string& entry) {
    const auto ini=root/"Mods.ini";
    if(!fs::is_regular_file(ini)) return false;
    std::ifstream in(ini,std::ios::binary);need(bool(in),"Could not read Mods.ini.");
    std::stringstream text;text<<in.rdbuf();in.close();
    const std::string original=text.str();std::string content;
    const std::string newline=original.find("\r\n")!=std::string::npos?"\r\n":"\n";
    auto trim=[](std::string_view value) {
        const auto begin=value.find_first_not_of(" \t\r");
        return begin==value.npos?std::string_view{}:value.substr(begin,value.find_last_not_of(" \t\r")-begin+1);
    };
    auto equal=[](std::string_view a,std::string_view b) {
        return a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin(),[](unsigned char x,unsigned char y){return std::tolower(x)==std::tolower(y);});
    };
    bool section=false,seenSection=false,closedSection=false,found=false;
    size_t insertion=std::string::npos;
    for(size_t pos=0;pos<original.size();) {
        const auto end=original.find('\n',pos),next=end==std::string::npos?original.size():end+1;
        const auto line=std::string_view(original).substr(pos,next-pos);
        auto value=trim(line.substr(0,end==std::string::npos?line.size():line.size()-1));
        if(!value.empty() && value.front()=='[') {
            const auto close=value.find(']');
            if(close!=value.npos) {
                if(section) {insertion=content.size();closedSection=true;}
                section=!seenSection && equal(trim(value.substr(1,close-1)),"Mods");
                if(section)seenSection=true;
            }
        }
        const auto equals=line.find('=');
        if(section && !value.empty() && value.front()!=';' && equals!=line.npos && equal(trim(line.substr(0,equals)),entry)) {
            // Replace only this active key's boolean value. Comments, spacing,
            // other keys and every line outside the first Mods section survive.
            const auto comment=line.find(';',equals+1);
            size_t valueEnd=comment==line.npos?line.size():comment;
            while(valueEnd>equals+1 && (line[valueEnd-1]==' ' || line[valueEnd-1]=='\t' || line[valueEnd-1]=='\r' || line[valueEnd-1]=='\n'))--valueEnd;
            size_t valueBegin=equals+1;
            while(valueBegin<valueEnd && (line[valueBegin]==' ' || line[valueBegin]=='\t'))++valueBegin;
            content.append(line.substr(0,valueBegin));content+='1';content.append(line.substr(valueEnd));found=true;
        } else content.append(line);
        pos=next;
    }
    if(!found) {
        if(!seenSection) {
            if(!content.empty() && content.back()!='\n')content+=newline;
            content+="[Mods]"+newline;insertion=content.size();
        } else if(!closedSection) insertion=content.size();
        std::string registration;
        if(insertion && content[insertion-1]!='\n')registration+=newline;
        registration+=entry+"=1"+newline;content.insert(insertion,registration);
    }
    if(content==original)return true;
    std::string error;need(albion::backups::backupOnce(ini,error),"Could not back up Mods.ini: "+error);
    writeAtomically(ini,std::vector<unsigned char>(content.begin(),content.end()));
    return true;
}
}

Plan review(const fs::path& root,const fs::path& exportFolder,tp::Progress* progress) {
    Plan plan;
    try {
        need(fs::is_regular_file(root/"data/graphics/pc/textures.big"),"Choose the Fable game folder containing data\\graphics\\pc\\textures.big.");
        for(const auto& folder:{exportFolder,exportFolder/"Front end"}) {
            if(!fs::is_regular_file(folder/"texture-project.json")) continue;
            Project project;project.folder=folder;project.archive=root/tp::projectArchiveRelative(folder);
            need(fs::is_regular_file(project.archive),"Missing game archive "+project.archive.string());
            project.destinationHash=forge::modorder::sha256Of(project.archive);
            // Every import rebuilds original + all edits: after a first import the
            // live archive already holds earlier edits, so review the kept original.
            const auto original=fs::path(project.archive.string()+albion::backups::kOrigSuffix);
            if(fs::is_regular_file(original)) {
                // Review checks the archive's file name, so present the original under it.
                forge::TemporaryDirectory scratch(root,".forge-texture-review-");
                const auto alias=scratch.path()/project.archive.filename();
                std::error_code linked;fs::create_hard_link(original,alias,linked);
                if(linked) fs::copy_file(original,alias);
                project.review=tp::review(alias,folder,options(),progress);
                if(project.review.ok && !project.review.changed &&
                   project.destinationHash!=forge::modorder::sha256Of(original))++plan.restoredArchives;
            } else project.review=tp::review(project.archive,folder,options(),progress);
            need(project.review.ok,project.review.error);
            need(project.review.afterBytes<=0xffffffffull,"Rebuilt archive reaches the 4 GiB format limit. Reduce the selected textures or dimensions.");
            if(project.review.afterBytes>=0x80000000ull) {
                need(project.archive.filename()=="textures.big","Archives above 2 GiB are currently verified only for textures.big.");
                plan.needsArchive4GB=true;
            }
            plan.changed+=project.review.changed;plan.unchanged+=project.review.unchanged;plan.missing+=project.review.missing;
            plan.projects.push_back(std::move(project));
        }
        need(!plan.projects.empty(),"Choose the \"Fable textures\" folder made by Fable Texture Export (it holds texture-project.json).");
    } catch(const std::exception& e) {plan.error=e.what();}
    return plan;
}

std::vector<unsigned char> sizeData(const fs::path& root,size_t& resized,unsigned& largest) {
    struct Table {std::string handle;std::uint32_t slots;std::vector<unsigned char> rows;std::uint32_t count=0;};
    std::vector<Table> tables;
    resized=0;largest=0;
    for(const char* relative:sizeArchives) {
        const auto current=root/relative,original=fs::path(current.string()+albion::backups::kOrigSuffix);
        if(!fs::is_regular_file(current) || !fs::is_regular_file(original)) continue;
        const auto before=forge::big::File::open(original),after=forge::big::File::open(current);
        for(const auto& bank:after.banks()) {
            const auto handle=runtimeHandles.find(bank.name);
            if(handle==runtimeHandles.end()) continue;
            const auto old=std::find_if(before.banks().begin(),before.banks().end(),[&](const auto& b){return b.name==bank.name;});
            need(old!=before.banks().end() && old->entries.size()==bank.entries.size(),bank.name+": entry count differs from the original archive");
            std::map<std::uint32_t,const forge::big::Entry*> originals;
            for(const auto& entry:old->entries) originals[entry.id]=&entry;
            Table table{handle->second,std::uint32_t(bank.entries.size()+2)};
            std::vector<const forge::big::Entry*> sorted;
            for(const auto& entry:bank.entries) sorted.push_back(&entry);
            std::sort(sorted.begin(),sorted.end(),[](auto a,auto b){return a->id<b->id;});
            for(size_t i=0;i<sorted.size();++i) need(sorted[i]->id==i+1,bank.name+": entry ids are not contiguous");
            for(const auto* entry:sorted) {
                const auto found=originals.find(entry->id);
                need(found!=originals.end() && found->second->name==entry->name,bank.name+": entry "+std::to_string(entry->id)+" changed identity");
                const auto& a=found->second->subHeader;const auto& b=entry->subHeader;
                need(a.size()>=28 && b.size()>=28,bank.name+": short texture header");
                need(word(a,10)==word(b,10),entry->name+": frame count changed");
                const std::uint16_t width=word(a,6),height=word(a,8);
                if(width==word(b,6) && height==word(b,8)) continue;
                need(width && height,entry->name+": zero original frame size");
                largest=std::max({largest,unsigned(word(b,0)),unsigned(word(b,2))});
                put32(table.rows,entry->id);table.rows.insert(table.rows.end(),b.begin(),b.begin()+28);
                put16(table.rows,width);put16(table.rows,height);++table.count;
            }
            resized+=table.count;
            if(table.count) tables.push_back(std::move(table));
        }
    }
    std::vector<unsigned char> out{'F','T','S','Z'};
    put32(out,1);put32(out,std::uint32_t(tables.size()));
    for(const auto& t:tables) {
        std::string handle=t.handle;handle.resize(32,'\0');out.insert(out.end(),handle.begin(),handle.end());
        put32(out,t.slots);put32(out,t.count);out.insert(out.end(),t.rows.begin(),t.rows.end());
    }
    return out;
}

Outcome apply(const fs::path& root,const Plan& plan,const fs::path& runtime,tp::Progress* progress) {
    Outcome outcome;
    try {
        need(plan.error.empty() && !plan.projects.empty(),"Review the textures before importing.");
        need(!albion::backups::gameRunningIn(root),"Quit Fable before importing textures.");
        const auto dll=runtime/"TextureSizeFix.dll";
        need(fs::is_regular_file(dll),"TextureSizeFix.dll is missing next to the importer. Unzip the whole package.");
        auto hash=[](const fs::path& path) {
            if(!fs::exists(path))return std::string();
            need(fs::is_regular_file(path),"Import destination is not a file: "+path.string());
            return forge::modorder::sha256Of(path);
        };
        std::map<fs::path,std::string> before;
        auto remember=[&](const fs::path& path) {
            auto it=before.find(path);
            if(it==before.end())it=before.emplace(path,hash(path)).first;
            return it->second;
        };
        const bool archive4GB=std::any_of(plan.projects.begin(),plan.projects.end(),[](const auto& project){return project.review.afterBytes>=0x80000000ull;});
        const fs::path archiveRelative="Mods/Archive4GB/Archive4GB.dll";
        const auto archiveDll=runtime/"Archive4GB.dll";
        if(archive4GB) {
            need(hash(archiveDll)==archiveDllSha,"The verified Archive4GB.dll is missing or differs from this package. Unzip the whole package again.");
            const auto executable=remember(root/"Fable.exe");
            need(executable=="41dc91090ae853715ac06d2e9fc96e5d545381d197ed55d624c642f34509ac10" ||
                 executable=="94e361911348b0680175d145b8805c4732cd9fc12abc28ecf3016a16274ab8b1",
                 "Archives above 2 GiB require the audited Steam Fable TLC executable (original or its LAA-only variant).");
            need(!remember(root/"Mods.ini").empty(),"Archives above 2 GiB require Mods.ini from your mod loader so Archive4GB can be enabled before you launch.");
            remember(root/archiveRelative);
        }
        std::set<fs::path> selected;
        for(const auto& project:plan.projects) {
            const auto relative=tp::projectArchiveRelative(project.folder);
            need(project.archive.lexically_normal()==(root/relative).lexically_normal(),"Import destination changed. Review again.");
            need(!project.destinationHash.empty() && remember(project.archive)==project.destinationHash,
                 "Installed texture archive changed. Review again before importing.");
            need(selected.insert(relative).second,"Duplicate texture archive in import plan.");
        }
        // All validation and image encoding happen against private copies. A
        // stale second project must not reset or partially update the first.
        // Keep nested archive staging paths below the legacy Windows path limit.
        forge::TemporaryDirectory preparation(root,".forge-ti-");
        const auto& preparedRoot=preparation.path();
        for(const char* relative:sizeArchives) {
            const auto live=root/relative;
            if(remember(live).empty())continue;
            const fs::path original=live.string()+albion::backups::kOrigSuffix;
            const bool hasOriginal=!remember(original).empty();
            const auto staged=preparedRoot/relative;fs::create_directories(staged.parent_path());
            fs::copy_file(selected.contains(relative) && hasOriginal?original:live,staged);
            if(hasOriginal)fs::copy_file(original,staged.string()+albion::backups::kOrigSuffix);
        }
        const fs::path modRelative="Mods/TextureSizeFix";
        for(const auto& relative:{modRelative/"TextureSizes.dat",modRelative/"TextureSizeFix.dll",fs::path("Mods.ini")})
            remember(root/relative);
        if(!before.at(root/"Mods.ini").empty())fs::copy_file(root/"Mods.ini",preparedRoot/"Mods.ini");
        for(const auto& project:plan.projects) {
            // Even no-op/revert plans must revalidate their review token.
            const auto result=tp::apply(preparedRoot,project.folder,options(),project.review.token,progress);
            need(result.ok,result.error);
            need(result.afterBytes==project.review.afterBytes,"Reviewed archive size changed. Review again before importing.");
            outcome.imported+=result.changed;
        }
        const auto data=sizeData(preparedRoot,outcome.resized,outcome.largest);
        const auto mod=preparedRoot/modRelative;
        writeAtomically(mod/"TextureSizes.dat",data);
        fs::copy_file(dll,mod/"TextureSizeFix.dll",fs::copy_options::overwrite_existing);
        const bool registered=registerMod(preparedRoot,"TextureSizeFix\\TextureSizeFix.dll");
        if(archive4GB) {
            const auto target=preparedRoot/archiveRelative;fs::create_directories(target.parent_path());
            fs::copy_file(archiveDll,target,fs::copy_options::overwrite_existing);
            need(hash(target)==archiveDllSha,"Archive4GB.dll changed during import preparation.");
            need(registerMod(preparedRoot,"Archive4GB\\Archive4GB.dll"),"Could not enable Archive4GB in Mods.ini.");
        }
        if(!registered)
            outcome.notes.push_back("No Mods.ini found. Enable Mods\\TextureSizeFix\\TextureSizeFix.dll in your mod loader (FSE or EgoCore) so menus and the HUD keep their normal size.");
        const auto setup=tp::inspectSetup(root);
        const bool twoK=std::any_of(setup.begin(),setup.end(),[](const std::string& n){return n.rfind("2K DLL: correct EgoCore folder and enabled",0)==0;});
        // Retail allocates at most 512 per side; the EgoCore 2K DLL raises that to 2048.
        if(outcome.largest>512 && !twoK)
            outcome.notes.insert(outcome.notes.begin(),"Some imported textures are larger than 512 pixels. Install and enable the EgoCore 2K texture DLL (Mods\\Fable2KTextureLimit) or the game will not show them correctly.");
        for(const auto& note:setup) outcome.notes.push_back(note);
        albion::detail::PendingBanks pending(root,".forge-texture-app-");
        auto publish=[&](const fs::path& relative) {
            const auto source=preparedRoot/relative;
            if(hash(source)==before.at(root/relative))return;
            fs::copy_file(source,pending.prepare(relative));
        };
        for(const auto& relative:selected)publish(relative);
        publish(modRelative/"TextureSizes.dat");publish(modRelative/"TextureSizeFix.dll");
        if(archive4GB)publish(archiveRelative);
        if(registered)publish("Mods.ini");
        for(const auto& [path,expected]:before)
            need(hash(path)==expected,"Installed files changed during preparation. Review again before importing.");
        need(!albion::backups::gameRunningIn(root),"Fable started during preparation. Quit it before importing.");
        std::string error;need(pending.install(true,error),error);
        outcome.restoredArchives=plan.restoredArchives;
        outcome.archive4GBInstalled=archive4GB;
        outcome.ok=true;
    } catch(const std::exception& e) {outcome.imported=0;outcome.error=e.what();}
    return outcome;
}
}
