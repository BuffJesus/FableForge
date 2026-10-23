#include "app.hpp"
#include "theme.hpp"
#include "windowsprocess.hpp"
#include "definitionfield.hpp"
#include "definitionclone.hpp"
#include "definitionbody.hpp"
#include "definitionread.hpp"
#include "creaturepublish.hpp"
#include "forge/fmplinks.hpp"
#include "texturebrowse.hpp"
#include "modpack.hpp"
#include "../vendor/embedded_schema.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <unordered_map>
namespace albion::gui {
namespace fs=std::filesystem;
namespace {
int definitionASCIIChar(ImGuiInputTextCallbackData* data) {return data->EventChar<32 || data->EventChar>126;}
int definitionUnicodeChar(ImGuiInputTextCallbackData* data) {return data->EventChar<32 || (data->EventChar>=127 && data->EventChar<=159);}
std::string definitionLower(std::string value) {
    for(auto& c:value) c=char(std::tolower(static_cast<unsigned char>(c)));return value;
}
}
struct App::CreatureImportDraft {
    creatureimport::Sources sources;
    creatureimport::SourceProof proof;
    creatureimport::MeshRequest request;
    meshimport::Model model;
    std::vector<forge::meshcompose::Material> materials;
    std::optional<creatureimport::TextureAppend> texture;
    forge::defschema::Schema schema;
    fs::path root,graphicsRelative,resolvedRoot;
    std::array<std::string,10> inputs;
    struct Target {fs::path path,resolved;std::string hash;bool present;};
    std::vector<Target> targets;
    void capture(const fs::path& path) {
        const bool present=fs::exists(path);
        if(present && !fs::is_regular_file(path)) throw std::runtime_error("Import review target is not a file.");
        targets.push_back({path,paths::resolvedPath(path),present?forge::modorder::sha256Of(path):std::string(),present});
    }
    void validate() const {
        if(paths::resolvedPath(root)!=resolvedRoot) throw std::runtime_error("Import destination identity changed; review again.");
        for(const auto& target:targets)
            if(paths::resolvedPath(target.path)!=target.resolved || fs::exists(target.path)!=target.present ||
               (target.present && (!fs::is_regular_file(target.path) || forge::modorder::sha256Of(target.path)!=target.hash)))
                throw std::runtime_error("Import destination or pack manifest changed; review again.");
        for(const auto& source:model.sourceFiles)
            if(!fs::is_regular_file(source.path) || paths::resolvedPath(source.path)!=source.resolved ||
               forge::modorder::sha256Of(source.path)!=source.sha256)
                throw std::runtime_error("Authored model or sidecar changed; review again.");
        creatureimport::requireSourceProof(sources,proof);
    }
};
std::array<std::string,10> App::creatureImportInputs() const {
    return {definitionEditValue_,definitionCloneBodyName_,definitionCloneAppearanceName_,definitionCreatureModel_,
        definitionCreatureTexture_,definitionCreatureMeshName_,definitionCreatureTextureName_,definitionCreatureTextureId_,
        std::to_string(definitionCreatureFormat_)+":"+std::to_string(definitionCreatureStored_)+":"+
            (definitionCreatureSlot_?std::to_string(*definitionCreatureSlot_):"none"),
        definitionEditDestination_.string()+":"+packDest_+":"+saveRoot()};
}
bool App::prepareCreatureImport() {
    definitionEditError_.clear();creatureImportDraft_.reset();definitionEditConfirm_=false;
    try {
        if(!definitionEditing_ || definitionEditType_!="clone-creature-private" || !definitionCreatureMesh_)
            throw std::runtime_error("Choose a private Creature draft with custom mesh enabled.");
        if(definitionsBusy() || modReadersBusy() || gameWriteBlocked("review creature import"))
            throw std::runtime_error("Wait for loads/writes and close the destination game.");
        if(!definitionCreatureSlot_ || *definitionCreatureSlot_>=definitionCreatureSlots_.size())
            throw std::runtime_error("Choose the body part and alternative explicitly.");
        const fs::path destination=fs::u8path(packDest_.empty()?saveRoot():packDest_)/"data/CompiledDefs/game.bin";
        if(definitionPaths()!=std::pair{definitionEditNames_,definitionEditBank_} || destination!=definitionEditDestination_)
            throw std::runtime_error("Definition source or destination changed; start the draft again.");
        if(!packDest_.empty()) {(void)modpack::load(fs::u8path(packDest_));}
        const auto read=[&](const std::vector<fs::path>& relatives) {
            for(const auto& root:{packDest_,saveRoot(),installPath_}) if(!root.empty())
                for(const auto& relative:relatives) {const auto path=fs::u8path(root)/relative;if(fs::exists(path)) return path;}
            return fs::u8path(installPath_)/relatives.front();
        };
        auto draft=std::make_shared<CreatureImportDraft>();draft->inputs=creatureImportInputs();
        draft->sources={definitionEditNames_,definitionEditBank_,
            read({"data/graphics/graphics.big","data/graphics/pc/graphics.big"}),read({"data/graphics/pc/textures.big"})};
        draft->root=destination.parent_path().parent_path().parent_path();draft->resolvedRoot=paths::resolvedPath(draft->root);
        draft->graphicsRelative=draft->sources.graphics.parent_path().filename()=="pc"?"data/graphics/pc/graphics.big":"data/graphics/graphics.big";
        draft->schema=definitionSchema_;
        const auto slot=definitionCreatureSlots_[*definitionCreatureSlot_];
        draft->request={definitionEditIndex_,slot.bodyPart,slot.alternative,definitionEditValue_,definitionCloneBodyName_,
            definitionCloneAppearanceName_,definitionCreatureMeshName_,!definitionCreatureStored_};
        const auto namesBytes=definitionCloneNamesBytes_,bankBytes=definitionCloneBankBytes_;
        const auto pack=packDest_;
        creatureImportFuture_=std::async(std::launch::async,[draft,namesBytes,bankBytes,pack] {
            CreatureImportJob job;job.draft=draft;
            try {
                draft->proof=creatureimport::captureSources(draft->sources);
                if(definitionwrite::cloneBytes(draft->sources.names)!=namesBytes || definitionwrite::cloneBytes(draft->sources.definitions)!=bankBytes)
                    throw std::runtime_error("The source bank or name pool changed; start the draft again.");
                draft->capture(draft->root/"data/CompiledDefs/names.bin");draft->capture(draft->root/"data/CompiledDefs/game.bin");
                draft->capture(draft->root/draft->graphicsRelative);draft->capture(draft->root/"data/graphics/pc/textures.big");
                if(!pack.empty()) {const fs::path root=fs::u8path(pack);(void)modpack::load(root);draft->capture(root/modpack::kFileName);}
                draft->model=meshimport::loadSkinnedModel(fs::u8path(draft->inputs[3]));
                uint32_t textureId=0;const auto& text=draft->inputs[7];
                const auto parsed=std::from_chars(text.data(),text.data()+text.size(),textureId);
                if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size()) throw std::runtime_error("Choose a complete unsigned decimal texture ID.");
                if(!draft->inputs[4].empty() && textureId) throw std::runtime_error("Choose a new texture or an existing texture ID, not both.");
                for(size_t i=0;i<draft->model.materialNames.size();++i) {
                    forge::meshcompose::Material material;material.name=draft->request.meshName+"_MAT"+std::to_string(i);
                    material.diffuseId=textureId;draft->materials.push_back(std::move(material));
                }
                if(!draft->inputs[4].empty()) {
                    const int format=std::stoi(draft->inputs[8]);
                    if(format<0 || format>2) throw std::runtime_error("Invalid texture format.");
                    draft->texture=creatureimport::captureTexture(fs::u8path(draft->inputs[4]),draft->inputs[6],
                        std::array<const char*,3>{"dxt1","dxt3","argb8888"}[size_t(format)]);
                }
                (void)creatureimport::prepareMesh(forge::bin::File::open(draft->sources.names,draft->sources.definitions),
                    forge::big::File::open(draft->sources.graphics),draft->schema,draft->request,draft->model,draft->materials);
                draft->validate();
            } catch(const std::exception& error) {job.error=error.what();}
            return job;
        });return true;
    } catch(const std::exception& error) {definitionEditError_=error.what();return false;}
}
bool App::saveCreatureImport() {
    definitionEditError_.clear();
    try {
        if(!creatureImportDraft_ || creatureImportDraft_->inputs!=creatureImportInputs()) throw std::runtime_error("Custom import fields changed; review again.");
        if(definitionsBusy() || modReadersBusy() || gameWriteBlocked("create creature import")) throw std::runtime_error("Wait for loads/writes and close the destination game.");
        if(definitionPaths()!=std::pair{definitionEditNames_,definitionEditBank_}) throw std::runtime_error("Definition source changed; start the draft again.");
        const auto draft=creatureImportDraft_;
        creatureImportFuture_=std::async(std::launch::async,[draft] {
            CreatureImportJob job;job.draft=draft;
            try {
                draft->validate();
                const auto result=creatureimport::publishMesh(draft->sources,draft->proof,draft->root,draft->graphicsRelative,
                    draft->schema,draft->request,draft->model,draft->materials,true,[draft]{draft->validate();},draft->texture?&*draft->texture:nullptr);
                job.published=true;job.rootIndex=result.clones.rootIndex;
            } catch(const std::exception& error) {job.error=error.what();}
            return job;
        });definitionEditConfirm_=false;return true;
    } catch(const std::exception& error) {definitionEditError_=error.what();return false;}
}
void App::pollCreatureImport() {
    if(!creatureImportFuture_.valid() || creatureImportFuture_.wait_for(std::chrono::seconds(0))!=std::future_status::ready) return;
    auto job=creatureImportFuture_.get();
    if(!job.error.empty()) {definitionEditError_=job.error;creatureImportDraft_.reset();definitionEditConfirm_=false;pushLog("creature import: "+job.error,1);return;}
    if(!job.published) {
        if(!definitionEditing_ || !definitionCreatureMesh_ || job.draft->inputs!=creatureImportInputs()) {
            definitionEditError_="Draft changed during review; review again.";return;
        }
        creatureImportDraft_=std::move(job.draft);definitionEditConfirm_=true;return;
    }
    definitionEditSuccess_="Created "+job.draft->request.rootName+" with a custom body alternative. Other slots/animations remain shared; runtime compatibility unverified.";
    for(const auto& note:job.draft->model.notes) pushLog("creature import: "+note,0);
    definitionEditing_=false;definitionCloning_=false;definitionEditConfirm_=false;definitionCreatureMesh_=false;creatureImportDraft_.reset();
    definitionCloneFile_={};definitionCloneNamesBytes_.clear();definitionCloneBankBytes_.clear();
    definitionLoaded_=false;definitionBrowseRequest_.reset();definitionBrowseIndex_=job.rootIndex;
    foliageexport::closeMeshBank();modelRoot_=job.draft->root.string();
    defThumbs_.clear();defList_.clear();themeGroupOf_.clear();envDefs_.clear();soundDefs_.clear();familyNames_.clear();issuesRev_=~0ull;
    if(packDest_.empty() || (!ctx_.gameRoot().empty() && paths::resolvedPath(job.draft->root)==paths::resolvedPath(ctx_.gameRoot()))) {
        ctxError_.clear();reloadWhenContextReady_=true;startContextLoad(job.draft->root.string());
    }
    refreshTextures();rescanBackups();pushLog(definitionEditSuccess_,3);
}
std::pair<fs::path,fs::path> App::definitionPaths() const {
    for(const auto& root:{packDest_,saveRoot(),installPath_}) {
        if(root.empty()) continue;
        const auto folder=fs::u8path(root)/"data"/"CompiledDefs";
        std::error_code error;
        if(fs::exists(folder/definitionBank_,error)) return {folder/"names.bin",folder/definitionBank_};
    }
    const auto folder=fs::u8path(installPath_)/"data"/"CompiledDefs";
    return {folder/"names.bin",folder/definitionBank_};
}
bool App::setDefinitionBank(const std::string& bank) {
    if(bank!="game.bin" && bank!="frontend.bin" && bank!="script.bin") {
        definitionSelectionError_="Choose game.bin, frontend.bin or script.bin.";return false;
    }
    if(definitionBank_==bank) return true;
    if(definitionEditing_) {definitionSelectionError_="Save or cancel the definition draft before changing banks.";return false;}
    definitionBrowseRequest_.reset();definitionBrowseIndex_.reset();
    definitionBank_=bank;definitionLoaded_=false;definitionSelected_.reset();definitionDecoded_.reset();
    definitionType_.clear();definitionSelectionError_.clear();return true;
}
void App::refreshDefinitions() {
    if(definitionsBusy() || activeFileJob() || modFilesBusy() || !installValid_) return;
    const auto [names,bank]=definitionPaths();
    definitionLoadedNames_=names;definitionLoadedBank_=bank;definitionLoaded_=true;
    definitionFile_={};definitionSelected_.reset();definitionDecoded_.reset();definitionFiltered_.clear();definitionTypes_.clear();definitionMembers_.clear();definitionGroupOf_.clear();definitionNameIndices_.clear();definitionTree_.clear();definitionReveal_=false;
    definitionError_.clear();definitionDecodeError_.clear();definitionSelectionError_.clear();definitionFilterDirty_=true;
    definitionFuture_=std::async(std::launch::async,[names,bank] {
        DefinitionJob job;job.names=names;job.bank=bank;
        try {job.file=forge::bin::File::open(names,bank);auto schemaPath=fs::current_path()/"docs"/"re_reference"/"def_schema.json";
#ifdef _WIN32
            const auto bundled=process::executableDirectory()/"docs"/"re_reference"/"def_schema.json";
            if(fs::is_regular_file(bundled)) schemaPath=bundled;
#endif
            job.schema=fs::is_regular_file(schemaPath)?forge::defschema::Schema::load(schemaPath):forge::defschema::Schema::loadText(kEmbeddedDefSchema,"embedded");
            if(bank.filename()=="game.bin") {
                job.members.resize(job.file.entries().size());
                for(size_t i=0;i<job.file.entries().size();++i) {
                    try {
                        const auto decoded=forge::defdecode::decode(job.file.entries()[i],job.schema);
                        if(const auto group=definitionread::groupTarget(job.file,decoded)) job.members[*group].push_back(i);
                    } catch(const std::exception&) {} // Unsupported records have no confirmed group.
                }
            }
        }
        catch(const std::exception& error) {job.error=error.what();}
        return job;
    });
}
void App::pollDefinitions() {
    pollCreatureImport();
    if(!definitionFuture_.valid() || definitionFuture_.wait_for(std::chrono::seconds(0))!=std::future_status::ready) return;
    auto job=definitionFuture_.get();
    const auto [names,bank]=definitionPaths();
    if(job.names!=names || job.bank!=bank) {definitionLoaded_=false;return;}
    definitionFile_=std::move(job.file);definitionSchema_=std::move(job.schema);definitionMembers_=std::move(job.members);definitionError_=std::move(job.error);
    uint64_t nameOffset=4;const auto& originalNames=definitionFile_.originalNames();
    for(size_t i=0;i<originalNames.size();++i) {
        if(nameOffset<=0x7fffffffu) definitionNameIndices_.emplace(uint32_t(nameOffset),i);
        nameOffset+=originalNames[i].second.size()+5;
    }
    definitionGroupOf_.assign(definitionFile_.entries().size(),std::nullopt);
    for(size_t group=0;group<definitionMembers_.size();++group)
        for(const auto index:definitionMembers_[group]) definitionGroupOf_[index]=group;
    std::set<std::string> types;for(const auto& entry:definitionFile_.entries()) types.insert(entry.definition);
    definitionTypes_={types.begin(),types.end()};definitionFilterDirty_=true;
    if(!definitionError_.empty()) pushLog("definitions: "+definitionError_,1);
}
bool App::selectDefinitionIndex(size_t index,bool reveal) {
    definitionSelectionError_.clear();
    if(definitionEditing_) {definitionSelectionError_="Save or cancel the definition draft before selecting another record.";return false;}
    const auto [names,bank]=definitionPaths();
    if(!definitionLoaded_ || definitionsBusy() || names!=definitionLoadedNames_ || bank!=definitionLoadedBank_ || index>=definitionFile_.entries().size()) {
        definitionSelectionError_="Choose an entry from the current loaded bank.";return false;
    }
    definitionSelected_=index;definitionReveal_=reveal;definitionDecoded_.reset();definitionDecodeError_.clear();
    try {definitionDecoded_=forge::defdecode::decode(definitionFile_.entries()[index],definitionSchema_);}
    catch(const std::exception& error) {definitionDecodeError_=error.what();}
    return true;
}
bool App::selectDefinition(const std::string& nameOrIndex) {
    if(definitionsBusy() || !definitionLoaded_) {definitionSelectionError_="Wait for the definition bank to load.";return false;}
    std::optional<size_t> exact;
    if(!nameOrIndex.empty()) for(size_t i=0;i<definitionFile_.entries().size();++i) {
        if(definitionFile_.entries()[i].name!=nameOrIndex) continue;
        if(exact) {definitionSelectionError_="Definition name is ambiguous; choose its indexed row.";return false;}
        exact=i;
    }
    if(exact) return selectDefinitionIndex(*exact);
    size_t index=0;const auto parsed=std::from_chars(nameOrIndex.data(),nameOrIndex.data()+nameOrIndex.size(),index);
    if(parsed.ec==std::errc{} && parsed.ptr==nameOrIndex.data()+nameOrIndex.size()) return selectDefinitionIndex(index);
    definitionSelectionError_="Definition not found in this bank.";return false;
}
void App::browseDefinition(const std::string& name) {
    if(definitionEditing_) {definitionEditError_="Save or cancel the definition draft before opening another record.";return;}
    definitionBrowseIndex_.reset();
    selectionInspectorOpen_=false;setModsMode(false);setTexturesMode(true);setAssetsTab(6);
    setDefinitionBank("game.bin");definitionSearch_[0]=0;definitionType_.clear();definitionFilterDirty_=true;definitionBrowseRequest_=name;
}
bool App::setDefinitionEditValue(const std::string& value) {
    if(!definitionEditing_ || definitionsBusy() || value.size()>=sizeof definitionEditValue_) return false;
    if(definitionEditType_=="CWideString") {
        try {std::vector<uint8_t> validated;forge::unicode::appendUtf16z(validated,value);}
        catch(const std::exception&) {return false;}
    } else if(std::any_of(value.begin(),value.end(),[](unsigned char c){return c==0 || c>=128;})) return false;
    std::snprintf(definitionEditValue_,sizeof definitionEditValue_,"%s",value.c_str());definitionEditConfirm_=false;return true;
}
bool App::beginDefinitionEdit(size_t field) {
    definitionEditError_.clear();definitionEditSuccess_.clear();
    try {
        if(definitionEditing_) throw std::runtime_error("Save or cancel the current definition draft first.");
        if(definitionsBusy() || modFilesBusy() || !definitionSelected_ || !definitionDecoded_ ||
           definitionPaths()!=std::pair{definitionLoadedNames_,definitionLoadedBank_} || field>=definitionDecoded_->fields.size())
            throw std::runtime_error("Choose a field from the current loaded definition.");
        const auto& selected=definitionDecoded_->fields[field];
        if(!definitionwrite::fieldEditable(selected)) throw std::runtime_error("This field has no supported field edit layout.");
        if(!packDest_.empty()) {if(!modpack::isPack(fs::u8path(packDest_))) throw std::runtime_error("Choose a valid FableForge pack.");(void)modpack::load(fs::u8path(packDest_));}
        definitionEditIndex_=*definitionSelected_;definitionEditExpected_=definitionFile_.entries()[definitionEditIndex_];
        definitionEditExpectedNames_=definitionFile_.originalNames();definitionEditNames_=definitionLoadedNames_;definitionEditBank_=definitionLoadedBank_;
        definitionEditExpectedGroups_.clear();
        if(definitionBank_=="game.bin" && selected.name=="GroupDef" && selected.type=="int32" && definitionread::groupFamily(definitionEditExpected_))
            for(size_t i=0;i<definitionFile_.entries().size();++i) if(definitionread::groupChoice(definitionFile_,definitionEditExpected_,i)) {
                const auto& group=definitionFile_.entries()[i];
                forge::bin::Entry identity{group.definition,group.name,group.indexInDefinition,{}};
                definitionEditExpectedGroups_.emplace(i,std::move(identity));
            }
        definitionEditDestination_=fs::u8path(packDest_.empty()?saveRoot():packDest_)/"data"/"CompiledDefs"/definitionBank_;
        definitionEditField_=selected.name;definitionEditType_=selected.type;definitionEditing_=true;definitionCloning_=false;
        definitionNameSearch_[0]=0;
        if(!setDefinitionEditValue(definitionwrite::fieldValue(selected))) {
            definitionEditing_=false;throw std::runtime_error("The field value does not fit a supported definition draft.");
        }
        return true;
    } catch(const std::exception& error) {definitionEditError_=error.what();return false;}
}
bool App::beginDefinitionBodyModel(size_t bodyPart,size_t alternative) {
    definitionEditError_.clear();definitionEditSuccess_.clear();
    try {
        if(definitionEditing_)throw std::runtime_error("Save or cancel the current definition draft first.");
        if(definitionsBusy() || modFilesBusy() || !definitionSelected_ || definitionBank_!="game.bin" ||
           definitionPaths()!=std::pair{definitionLoadedNames_,definitionLoadedBank_})
            throw std::runtime_error("Choose a body component in the current game bank.");
        if(!packDest_.empty()) {if(!modpack::isPack(fs::u8path(packDest_)))throw std::runtime_error("Choose a valid FableForge pack.");(void)modpack::load(fs::u8path(packDest_));}
        auto current=forge::bin::File::open(definitionLoadedNames_,definitionLoadedBank_);
        definitionwrite::verifyEntries(definitionFile_,current);
        const auto decoded=forge::defdecode::decode(current.entries()[*definitionSelected_],definitionSchema_);
        const auto layout=forge::fmplinks::creaturePrefixLayout(decoded.prefix);
        const auto slot=std::find_if(layout.bodyModels.begin(),layout.bodyModels.end(),[&](const auto& value){return value.bodyPart==bodyPart && value.alternative==alternative;});
        if(slot==layout.bodyModels.end())throw std::runtime_error("This body model alternative is absent.");
        uint32_t id=0;for(size_t n=0;n<4;++n)id|=uint32_t(current.entries()[*definitionSelected_].data.at(slot->offset+n))<<(8*n);
        const auto graphicsPath=graphicsBigPath();auto graphics=forge::big::File::open(graphicsPath);
        (void)forge::defedit::setCreatureBodyModel(current,definitionSchema_,*definitionSelected_,bodyPart,alternative,id,graphics);
        definitionCloneNamesBytes_=definitionwrite::cloneBytes(definitionLoadedNames_);
        definitionCloneBankBytes_=definitionwrite::cloneBytes(definitionLoadedBank_);
        definitionCloneFile_=std::move(current);definitionBodyGraphics_=std::move(graphics);definitionBodyGraphicsPath_=graphicsPath;
        definitionBodyPart_=bodyPart;definitionBodyAlternative_=alternative;definitionEditIndex_=*definitionSelected_;
        definitionEditNames_=definitionLoadedNames_;definitionEditBank_=definitionLoadedBank_;
        definitionEditDestination_=fs::u8path(packDest_.empty()?saveRoot():packDest_)/"data"/"CompiledDefs"/definitionBank_;
        definitionEditField_="Body "+std::to_string(bodyPart)+" / alternative "+std::to_string(alternative);
        definitionEditType_="body-model";definitionEditing_=true;definitionCloning_=false;definitionEditConfirm_=false;
        definitionNameSearch_[0]=0;
        std::snprintf(definitionEditValue_,sizeof definitionEditValue_,"%u",id);return true;
    }catch(const std::exception& error){definitionEditError_=error.what();return false;}
}
bool App::beginDefinitionClone(bool creature,bool privateVisual) {
    definitionEditError_.clear();definitionEditSuccess_.clear();
    try {
        if(definitionEditing_) throw std::runtime_error("Save or cancel the current definition draft first.");
        if(definitionsBusy() || modFilesBusy() || !definitionSelected_ ||
           definitionPaths()!=std::pair{definitionLoadedNames_,definitionLoadedBank_})
            throw std::runtime_error("Choose a record from the current loaded definition bank.");
        if(!packDest_.empty()) {if(!modpack::isPack(fs::u8path(packDest_))) throw std::runtime_error("Choose a valid FableForge pack.");(void)modpack::load(fs::u8path(packDest_));}
        auto current=forge::bin::File::open(definitionLoadedNames_,definitionLoadedBank_);
        definitionwrite::verifyEntries(definitionFile_,current);
        if(privateVisual && !creature) throw std::runtime_error("Private visuals require a Creature donor.");
        if(creature) {
            if(definitionBank_!="game.bin" || current.entries()[*definitionSelected_].definition!="CREATURE")
                throw std::runtime_error("Choose a Creature donor in the game bank.");
            // Preflight the strict layout/refusal path without changing the
            // captured donor bank or choosing the user's eventual new name.
            auto probe=current;size_t suffix=0;std::string probeName;
            do {probeName="FORGE_CREATURE_DRAFT_"+std::to_string(suffix++);}
            while(std::any_of(current.entries().begin(),current.entries().end(),[&](const auto& entry){return entry.name==probeName;}));
            if(privateVisual) {
                std::array<std::string,3> probeNames;
                std::set<uint32_t> keys;
                for(const auto& entry:current.entries()) keys.insert(forge::bin::nameCrc(entry.name));
                for(auto& name:probeNames) {
                    do{name="FORGE_PRIVATE_DRAFT_"+std::to_string(suffix++);}
                    while(keys.contains(forge::bin::nameCrc(name)));
                    keys.insert(forge::bin::nameCrc(name));
                }
                const auto cloned=forge::defedit::cloneCreatureVisuals(probe,definitionSchema_,*definitionSelected_,probeNames[0],probeNames[1],probeNames[2]);
                const auto body=forge::defdecode::decode(probe.entries()[cloned.bodyIndex],definitionSchema_);
                const auto layout=forge::fmplinks::creaturePrefixLayout(body.prefix);
                definitionCreatureSlots_.clear();
                for(const auto& slot:layout.bodyModels) {
                    uint32_t id=0;
                    for(size_t n=0;n<4;++n) id|=uint32_t(probe.entries()[cloned.bodyIndex].data.at(slot.offset+n))<<(8*n);
                    definitionCreatureSlots_.push_back({slot.bodyPart,slot.alternative,id});
                }
            } else (void)forge::defedit::cloneCreature(probe,definitionSchema_,*definitionSelected_,probeName);
        }
        definitionCloneNamesBytes_=definitionwrite::cloneBytes(definitionLoadedNames_);
        definitionCloneBankBytes_=definitionwrite::cloneBytes(definitionLoadedBank_);
        definitionCloneFile_=std::move(current);
        definitionEditIndex_=*definitionSelected_;
        definitionEditNames_=definitionLoadedNames_;definitionEditBank_=definitionLoadedBank_;
        definitionEditDestination_=fs::u8path(packDest_.empty()?saveRoot():packDest_)/"data"/"CompiledDefs"/definitionBank_;
        const auto& donor=definitionFile_.entries()[definitionEditIndex_];
        definitionEditField_=donor.name.empty()?"(unnamed record)":donor.name;
        definitionEditType_=privateVisual?"clone-creature-private":creature?"clone-creature":"clone";definitionEditing_=true;definitionCloning_=true;
        definitionEditConfirm_=false;definitionEditValue_[0]=0;
        definitionCloneBodyName_[0]=0;definitionCloneAppearanceName_[0]=0;
        definitionCreatureMesh_=false;definitionCreatureStored_=false;definitionCreatureSlot_.reset();creatureImportDraft_.reset();
        definitionCreatureModel_[0]=0;definitionCreatureTexture_[0]=0;
        definitionCreatureMeshName_[0]=0;definitionCreatureTextureName_[0]=0;
        std::snprintf(definitionCreatureTextureId_,sizeof definitionCreatureTextureId_,"0");definitionCreatureFormat_=0;
        if(!privateVisual) definitionCreatureSlots_.clear();return true;
    } catch(const std::exception& error) {definitionEditError_=error.what();return false;}
}
bool App::saveDefinitionClone() {
    if(definitionEditType_=="clone-creature-private" && definitionCreatureMesh_) return saveCreatureImport();
    definitionEditError_.clear();definitionEditSuccess_.clear();
    try {
        if(!definitionEditing_ || !definitionCloning_) throw std::runtime_error("Choose a record to clone first.");
        if(definitionsBusy() || modReadersBusy() || gameWriteBlocked("clone definition")) throw std::runtime_error("Wait for loads and writes, and close the destination game.");
        const auto destination=fs::u8path(packDest_.empty()?saveRoot():packDest_)/"data"/"CompiledDefs"/definitionBank_;
        if(definitionPaths()!=std::pair{definitionEditNames_,definitionEditBank_} || destination!=definitionEditDestination_)
            throw std::runtime_error("Definition source or destination changed; start the clone again.");
        if(definitionEditBank_!=destination && fs::exists(destination)) throw std::runtime_error("The destination bank appeared; refresh before cloning.");
        if(!packDest_.empty()) {if(!modpack::isPack(fs::u8path(packDest_))) throw std::runtime_error("Choose a valid FableForge pack.");(void)modpack::load(fs::u8path(packDest_));}
        if(definitionwrite::cloneBytes(definitionEditNames_)!=definitionCloneNamesBytes_ ||
           definitionwrite::cloneBytes(definitionEditBank_)!=definitionCloneBankBytes_)
            throw std::runtime_error("The source bank or name pool changed; start the clone again.");
        auto cloned=definitionCloneFile_;
        const bool creature=definitionEditType_=="clone-creature";
        const bool privateVisual=definitionEditType_=="clone-creature-private";
        size_t added;
        if(privateVisual) {
            const auto result=forge::defedit::cloneCreatureVisuals(cloned,definitionSchema_,definitionEditIndex_,
                definitionEditValue_,definitionCloneBodyName_,definitionCloneAppearanceName_);
            added=result.rootIndex;
            const definitionwrite::CreatureVisualCloneContract contract{definitionSchema_,definitionEditIndex_,
                definitionEditValue_,definitionCloneBodyName_,definitionCloneAppearanceName_};
            definitionwrite::publishClone(cloned,definitionEditNames_,definitionEditBank_,destination,true,nullptr,{},&contract);
        } else {
            added=creature?forge::defedit::cloneCreature(cloned,definitionSchema_,definitionEditIndex_,definitionEditValue_):
                forge::defedit::cloneEntry(cloned,definitionEditIndex_,definitionEditValue_);
            definitionwrite::publishClone(cloned,definitionEditNames_,definitionEditBank_,destination,true,
                creature?&definitionSchema_:nullptr,creature?std::optional<size_t>(definitionEditIndex_):std::nullopt);
        }
        definitionEditSuccess_="Created "+std::string(definitionEditValue_)+(privateVisual?" with private body and appearance definitions (assets shared).":".");
        definitionEditing_=false;definitionCloning_=false;definitionEditConfirm_=false;
        definitionCloneFile_={};definitionCloneNamesBytes_.clear();definitionCloneBankBytes_.clear();
        definitionLoaded_=false;definitionBrowseRequest_.reset();definitionBrowseIndex_=added;
        if(definitionBank_=="game.bin") {
            const auto targetRoot=destination.parent_path().parent_path().parent_path();
            if(packDest_.empty() || (!ctx_.gameRoot().empty() && paths::resolvedPath(targetRoot)==paths::resolvedPath(ctx_.gameRoot()))) {
                defThumbs_.clear();defList_.clear();themeGroupOf_.clear();envDefs_.clear();soundDefs_.clear();familyNames_.clear();issuesRev_=~0ull;
                ctxError_.clear();reloadWhenContextReady_=true;
                startContextLoad(packDest_.empty()?saveRoot():packDest_);
            }
        }
        rescanBackups();pushLog(definitionEditSuccess_,3);return true;
    } catch(const std::exception& error) {definitionEditError_=error.what();pushLog("clone definition: "+definitionEditError_,1);return false;}
}
bool App::saveDefinitionEdit() {
    if(definitionCloning_) return saveDefinitionClone();
    definitionEditError_.clear();definitionEditSuccess_.clear();
    try {
        if(!definitionEditing_) throw std::runtime_error("Choose a field to edit first.");
        if(definitionsBusy() || modReadersBusy() || gameWriteBlocked("save definition")) throw std::runtime_error("Wait for loads and writes, and close the destination game.");
        const auto destination=fs::u8path(packDest_.empty()?saveRoot():packDest_)/"data"/"CompiledDefs"/definitionBank_;
        if(definitionPaths()!=std::pair{definitionEditNames_,definitionEditBank_} || destination!=definitionEditDestination_)
            throw std::runtime_error("Definition source or destination changed; start the edit again.");
        if(definitionEditBank_!=destination && fs::exists(destination)) throw std::runtime_error("The destination bank appeared; refresh before editing.");
        if(!packDest_.empty()) {if(!modpack::isPack(fs::u8path(packDest_))) throw std::runtime_error("Choose a valid FableForge pack.");(void)modpack::load(fs::u8path(packDest_));}
        bool changed;
        if(definitionEditType_=="body-model") {
            if(graphicsBigPath()!=definitionBodyGraphicsPath_ ||
               !definitionwrite::sameBodyGraphics(definitionBodyGraphics_,forge::big::File::open(definitionBodyGraphicsPath_)))
                throw std::runtime_error("The graphics source changed; start the body edit again.");
            if(definitionwrite::cloneBytes(definitionEditBank_)!=definitionCloneBankBytes_ ||
               definitionwrite::cloneBytes(definitionEditNames_)!=definitionCloneNamesBytes_)
                throw std::runtime_error("The source bank or name pool changed; start the body edit again.");
            uint32_t id=0;const std::string_view value=definitionEditValue_;
            const auto parsed=std::from_chars(value.data(),value.data()+value.size(),id);
            if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size())throw std::runtime_error("Choose a complete unsigned decimal model ID.");
            auto plan=definitionCloneFile_;
            const auto result=forge::defedit::setCreatureBodyModel(plan,definitionSchema_,definitionEditIndex_,definitionBodyPart_,definitionBodyAlternative_,id,definitionBodyGraphics_);
            changed=result.oldModelId!=result.newModelId;
            if(changed) {
                const definitionwrite::CreatureBodyContract contract{definitionSchema_,definitionBodyGraphics_,definitionCloneBankBytes_,definitionCloneNamesBytes_,
                    definitionEditIndex_,definitionBodyPart_,definitionBodyAlternative_,id};
                definitionwrite::publishCreatureBody(plan,definitionEditNames_,definitionEditBank_,definitionBodyGraphicsPath_,destination,contract);
            }
        }else changed=definitionwrite::replaceField(definitionEditNames_,definitionEditBank_,destination,definitionEditIndex_,
            definitionEditExpected_,definitionEditExpectedNames_,definitionSchema_,definitionEditField_,definitionEditValue_,&definitionEditExpectedGroups_);
        definitionEditSuccess_=changed?"Saved "+definitionEditField_+".":"The field is unchanged.";
        definitionEditing_=false;definitionEditConfirm_=false;definitionLoaded_=false;definitionBrowseRequest_.reset();definitionBrowseIndex_=definitionEditIndex_;
        if(changed && definitionBank_=="game.bin") {
            const auto targetRoot=destination.parent_path().parent_path().parent_path();
            if(packDest_.empty() || (!ctx_.gameRoot().empty() && paths::resolvedPath(targetRoot)==paths::resolvedPath(ctx_.gameRoot()))) {
                defThumbs_.clear();defList_.clear();themeGroupOf_.clear();envDefs_.clear();soundDefs_.clear();familyNames_.clear();issuesRev_=~0ull;
                ctxError_.clear();reloadWhenContextReady_=true;
                startContextLoad(packDest_.empty()?saveRoot():packDest_);
            }
        }
        rescanBackups();pushLog(definitionEditSuccess_,3);return true;
    } catch(const std::exception& error) {definitionEditError_=error.what();pushLog("save definition: "+definitionEditError_,1);return false;}
}
void App::drawDefinitionBrowser(float pad,float inner,float cardInner) {
    using theme::S;
    const auto [names,bank]=definitionPaths();
    if(names!=definitionLoadedNames_ || bank!=definitionLoadedBank_) definitionLoaded_=false;
    if(!definitionLoaded_ && !definitionsBusy()) refreshDefinitions();
    if(definitionLoaded_ && !definitionsBusy() && definitionBrowseRequest_) {
        selectDefinition(*definitionBrowseRequest_);definitionBrowseRequest_.reset();
    }
    if(definitionLoaded_ && !definitionsBusy() && definitionBrowseIndex_) {
        selectDefinitionIndex(*definitionBrowseIndex_);definitionBrowseIndex_.reset();
    }
    ImGui::SetCursorPosX(pad);theme::beginCard("##definitionbrowser",inner,true);theme::label("Definitions");
    drawPackDestination(cardInner,true);
    if(!definitionEditing_) {
        ImGui::BeginDisabled(definitionsBusy() || modFilesBusy() || !definitionSelected_);
        if(theme::ghostButton("Clone selected definition",ImVec2(cardInner,0))) beginDefinitionClone();
        auto_.registerWidget("btn_definition_clone");ImGui::EndDisabled();
        if(definitionSelected_ && *definitionSelected_<definitionFile_.entries().size() && definitionBank_=="game.bin" &&
           definitionFile_.entries()[*definitionSelected_].definition=="CREATURE") {
            ImGui::BeginDisabled(definitionEditing_ || definitionsBusy() || modFilesBusy());
            if(theme::ghostButton("Clone creature",ImVec2(cardInner,0))) beginDefinitionClone(true);
            auto_.registerWidget("btn_definition_clone_creature");ImGui::EndDisabled();
            ImGui::BeginDisabled(definitionsBusy() || modFilesBusy());
            if(theme::ghostButton("Clone private creature visuals",ImVec2(cardInner,0))) beginDefinitionClone(true,true);
            auto_.registerWidget("btn_definition_clone_creature_private");ImGui::EndDisabled();
        }
    }
    if(definitionEditing_) {
        ImGui::BeginDisabled(definitionsBusy());
        if(definitionCloning_) {
            ImGui::TextWrapped("Clone %s",definitionEditField_.c_str());
            theme::hint(definitionEditType_=="clone-creature-private"?
                "New root, body and appearance definitions. Other gameplay components and all assets stay shared.":definitionEditType_=="clone-creature"?
                "Retargets verified self references. Components, appearance and body stay shared.":
                "Copies this record unchanged. References keep their existing targets.");
        } else ImGui::TextWrapped("Edit %s (%s)",definitionEditField_.c_str(),definitionEditType_.c_str());
        if(definitionEditType_=="body-model")theme::hint("Model ID: unique skinned type-5 mesh. All users of this component change; assets stay shared. Skeleton/runtime compatibility is not verified.");
        ImGui::TextWrapped("Destination: %s",definitionEditDestination_.string().c_str());
        if(definitionEditType_=="CDefString") {
            std::string preview=definitionEditValue_;
            uint32_t selected=0;const std::string_view value=definitionEditValue_;
            const auto parsed=std::from_chars(value.data(),value.data()+value.size(),selected);
            if(value=="-1") preview="No name";
            else if(parsed.ec==std::errc{} && parsed.ptr==value.data()+value.size()) {
                const auto found=definitionNameIndices_.find(selected);
                if(found!=definitionNameIndices_.end()) {
                    const auto& name=definitionFile_.originalNames()[found->second].second;
                    preview=name.empty()?"(empty name)":name;
                }
            }
            ImGui::SetNextItemWidth(cardInner);
            const bool open=ImGui::BeginCombo("##definitionname",preview.c_str());
            auto_.registerWidget("combo_definition_edit_name");
            if(open) {
                ImGui::InputText("Search names",definitionNameSearch_,sizeof definitionNameSearch_);auto_.registerWidget("input_definition_name_search");
                if(ImGui::Selectable("No name",value=="-1")) setDefinitionEditValue("-1");
                auto_.registerWidget("definition_name_option_none");
                std::vector<std::pair<uint32_t,size_t>> options;const auto search=definitionLower(definitionNameSearch_);
                for(const auto& option:definitionNameIndices_) {
                    const auto& name=definitionFile_.originalNames()[option.second].second;
                    if(search.empty() || definitionLower(name).find(search)!=std::string::npos || std::to_string(option.first).find(search)!=std::string::npos) options.push_back(option);
                }
                ImGuiListClipper clipper;clipper.Begin(int(options.size()));
                while(clipper.Step()) {
                    for(int i=clipper.DisplayStart;i<clipper.DisplayEnd;++i) {
                        const auto offset=options[size_t(i)].first;const auto& name=definitionFile_.originalNames()[options[size_t(i)].second].second;
                        const auto label=std::to_string(offset)+": "+(name.empty()?"(empty name)":name);
                        ImGui::PushID(i);
                        if(ImGui::Selectable(label.c_str(),value==std::to_string(offset))) setDefinitionEditValue(std::to_string(offset));
                        auto_.registerWidget(("definition_name_option_"+std::to_string(offset)).c_str());ImGui::PopID();
                    }
                }
                ImGui::EndCombo();
            }
        } else if(!definitionCloning_ && definitionBank_=="game.bin" && definitionEditField_=="GroupDef" &&
                  definitionEditType_=="int32" && definitionread::groupFamily(definitionEditExpected_)) {
            const std::string value=definitionEditValue_;
            std::string preview="Unresolved original: "+value;
            uint32_t selected=0;const auto parsed=std::from_chars(value.data(),value.data()+value.size(),selected);
            if(parsed.ec==std::errc{} && parsed.ptr==value.data()+value.size() && definitionread::groupChoice(definitionFile_,definitionEditExpected_,selected))
                preview=std::to_string(selected)+": "+(definitionFile_.entries()[selected].name.empty()?"(unnamed group)":definitionFile_.entries()[selected].name);
            ImGui::SetNextItemWidth(cardInner);const bool open=ImGui::BeginCombo("##definitiongroup",preview.c_str());
            auto_.registerWidget("combo_definition_edit_group");
            if(open) {
                ImGui::InputText("Search groups",definitionNameSearch_,sizeof definitionNameSearch_);auto_.registerWidget("input_definition_group_search");
                std::vector<size_t> options;const auto search=definitionLower(definitionNameSearch_);
                for(size_t i=0;i<definitionFile_.entries().size();++i) {
                    const auto& group=definitionFile_.entries()[i];
                    if(definitionread::groupChoice(definitionFile_,definitionEditExpected_,i) &&
                       (search.empty() || definitionLower(group.name).find(search)!=std::string::npos || std::to_string(i).find(search)!=std::string::npos)) options.push_back(i);
                }
                ImGuiListClipper clipper;clipper.Begin(int(options.size()));
                while(clipper.Step()) for(int i=clipper.DisplayStart;i<clipper.DisplayEnd;++i) {
                    const auto index=options[size_t(i)];const auto& group=definitionFile_.entries()[index];
                    const auto label=std::to_string(index)+": "+(group.name.empty()?"(unnamed group)":group.name);
                    if(ImGui::Selectable(label.c_str(),value==std::to_string(index))) setDefinitionEditValue(std::to_string(index));
                    auto_.registerWidget(("definition_group_option_"+std::to_string(index)).c_str());
                }
                ImGui::EndCombo();
            }
        } else if(definitionEditType_=="bool") {
            const std::string_view value=definitionEditValue_;bool checked=value=="true" || value=="1";
            if(ImGui::Checkbox("Value",&checked)) setDefinitionEditValue(checked?"true":"false");
        } else if(definitionEditType_=="CRGBColour") {
            int values[4]{};std::string_view remaining=definitionEditValue_;bool valid=true;
            for(size_t i=0;i<4;++i) {
                const auto comma=remaining.find(',');const auto part=remaining.substr(0,comma);
                const auto parsed=std::from_chars(part.data(),part.data()+part.size(),values[i]);
                if(parsed.ec!=std::errc{} || parsed.ptr!=part.data()+part.size() || values[i]<0 || values[i]>255 || ((i<3)!=(comma!=std::string_view::npos))) {valid=false;break;}
                remaining=comma==std::string_view::npos?std::string_view{}:remaining.substr(comma+1);
            }
            if(valid) {
                float rgba[4];for(size_t i=0;i<4;++i) rgba[i]=float(values[i])/255.0f;
                bool changed=ImGui::ColorEdit4("##colour",rgba,ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_Uint8|ImGuiColorEditFlags_AlphaBar|ImGuiColorEditFlags_AlphaPreviewHalf);
                auto_.registerWidget("input_definition_colour_picker");
                if(ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel)) auto_.registerWidget("definition_colour_popup");
                if(changed) for(size_t i=0;i<4;++i) values[i]=std::clamp(int(std::lround(rgba[i]*255.0f)),0,255);
                ImGui::TextDisabled("R / G / B / A (0-255)");ImGui::BeginGroup();
                const float width=(cardInner-3*ImGui::GetStyle().ItemSpacing.x)/4;
                for(size_t i=0;i<4;++i) {
                    if(i) ImGui::SameLine();ImGui::SetNextItemWidth(width);ImGui::PushID(int(i));
                    if(ImGui::InputInt("##channel",&values[i],0,0)) {values[i]=std::clamp(values[i],0,255);changed=true;}
                    auto_.registerWidget(("input_definition_colour_"+std::to_string(i)).c_str());ImGui::PopID();
                }
                ImGui::EndGroup();if(changed) setDefinitionEditValue(definitionwrite::colourValue(values));
            } else {ImGui::SetNextItemWidth(cardInner);ImGui::InputText("##definitionvalue",definitionEditValue_,sizeof definitionEditValue_);}
        } else if(definitionEditType_=="C2DVector" || definitionEditType_=="C3DVector") {
            const size_t count=definitionEditType_=="C2DVector"?2:3;float values[3]{};
            std::string_view remaining=definitionEditValue_;bool valid=true;
            for(size_t i=0;i<count;++i) {
                const auto comma=remaining.find(',');const auto part=remaining.substr(0,comma);
                const auto parsed=std::from_chars(part.data(),part.data()+part.size(),values[i]);
                if(parsed.ec!=std::errc{} || parsed.ptr!=part.data()+part.size() || ((i+1<count)!=(comma!=std::string_view::npos))) {valid=false;break;}
                remaining=comma==std::string_view::npos?std::string_view{}:remaining.substr(comma+1);
            }
            ImGui::TextDisabled("%s",count==2?"X / Y":"X / Y / Z");
            ImGui::SetNextItemWidth(cardInner);
            if(valid) {
                bool changed=false;ImGui::BeginGroup();
                const float width=(cardInner-ImGui::GetStyle().ItemSpacing.x*float(count-1))/float(count);
                for(size_t i=0;i<count;++i) {
                    if(i) ImGui::SameLine();ImGui::SetNextItemWidth(width);ImGui::PushID(int(i));
                    changed=ImGui::InputFloat("##coordinate",&values[i],0,0,"%.9g") || changed;
                    auto_.registerWidget(("input_definition_vector_"+std::to_string(i)).c_str());ImGui::PopID();
                }
                ImGui::EndGroup();
                if(changed) setDefinitionEditValue(definitionwrite::vectorValue(values,count));
            } else ImGui::InputText("##definitionvalue",definitionEditValue_,sizeof definitionEditValue_);
        } else {
            const bool text=definitionCloning_ || definitionEditType_=="CCharString" || definitionEditType_=="CWideString";
            const bool wide=definitionEditType_=="CWideString";
            if(text) ImGui::TextDisabled("%s",definitionEditType_=="clone-creature-private"?"Root name (127 ASCII characters max)":definitionCloning_?"New name (up to 127 ASCII characters)":wide?"Unicode text (up to 127 UTF-8 bytes)":"Text (up to 127 ASCII characters)");
            ImGui::SetNextItemWidth(cardInner);ImGui::InputText("##definitionvalue",definitionEditValue_,sizeof definitionEditValue_,
                text?ImGuiInputTextFlags_CallbackCharFilter:0,text?(wide?definitionUnicodeChar:definitionASCIIChar):nullptr);
        }
        auto_.registerWidget("input_definition_edit_value");
        if(definitionEditType_=="body-model") {
            // Use the captured directory, never a freshly loaded bank while a
            // draft is open. Save still revalidates the directory and sources.
            const auto* meshes=definitionBodyGraphics_.findBank("MBANK_ALLMESHES");
            if(meshes) {
                std::unordered_map<uint32_t,size_t> counts;
                for(const auto& mesh:meshes->entries) ++counts[mesh.id];
                const std::string_view value=definitionEditValue_;uint32_t selected=0;
                const auto parsed=std::from_chars(value.data(),value.data()+value.size(),selected);
                const bool valid=parsed.ec==std::errc{} && parsed.ptr==value.data()+value.size();
                std::string preview="Choose a named model";
                for(const auto& mesh:meshes->entries) if(valid && mesh.id==selected && mesh.type==5 && counts[mesh.id]==1) {
                    preview=std::to_string(mesh.id)+": "+(mesh.name.empty()?"(unnamed model)":mesh.name);break;
                }
                ImGui::SetNextItemWidth(cardInner);
                const bool open=ImGui::BeginCombo("##definitionbodymodel",preview.c_str());
                auto_.registerWidget("combo_definition_body_model");
                if(open) {
                    ImGui::InputText("Search models",definitionNameSearch_,sizeof definitionNameSearch_);
                    auto_.registerWidget("input_definition_body_model_search");
                    const auto search=definitionLower(definitionNameSearch_);
                    std::vector<size_t> options;
                    for(size_t i=0;i<meshes->entries.size();++i) {
                        const auto& mesh=meshes->entries[i];
                        if(mesh.type==5 && counts[mesh.id]==1 && (search.empty() ||
                           definitionLower(mesh.name).find(search)!=std::string::npos ||
                           std::to_string(mesh.id).find(search)!=std::string::npos)) options.push_back(i);
                    }
                    ImGuiListClipper clipper;clipper.Begin(int(options.size()));
                    while(clipper.Step()) for(int i=clipper.DisplayStart;i<clipper.DisplayEnd;++i) {
                        const auto& mesh=meshes->entries[options[size_t(i)]];
                        const auto label=std::to_string(mesh.id)+": "+(mesh.name.empty()?"(unnamed model)":mesh.name);
                        if(ImGui::Selectable(label.c_str(),valid && selected==mesh.id)) setDefinitionEditValue(std::to_string(mesh.id));
                        auto_.registerWidget(("definition_body_model_option_"+std::to_string(mesh.id)).c_str());
                    }
                    ImGui::EndCombo();
                }
            }
        }
        if(definitionCloning_ && definitionEditType_=="clone-creature-private") {
            ImGui::TextDisabled("Body name (127 ASCII characters max)");ImGui::SetNextItemWidth(cardInner);
            ImGui::InputText("##privatebody",definitionCloneBodyName_,sizeof definitionCloneBodyName_,ImGuiInputTextFlags_CallbackCharFilter,definitionASCIIChar);
            auto_.registerWidget("input_definition_clone_body");
            ImGui::TextDisabled("Appearance name (127 ASCII characters max)");ImGui::SetNextItemWidth(cardInner);
            ImGui::InputText("##privateappearance",definitionCloneAppearanceName_,sizeof definitionCloneAppearanceName_,ImGuiInputTextFlags_CallbackCharFilter,definitionASCIIChar);
            auto_.registerWidget("input_definition_clone_appearance");
            if(ImGui::Checkbox("Import a custom body model",&definitionCreatureMesh_)) {definitionEditConfirm_=false;creatureImportDraft_.reset();}
            auto_.registerWidget("check_definition_creature_mesh");
            if(definitionCreatureMesh_) {
                theme::hint("Replaces one body alternative. Other slots and animations remain shared. Use a glTF skin matching the donor skeleton.");
                std::string selected="Choose body / alternative";
                if(definitionCreatureSlot_ && *definitionCreatureSlot_<definitionCreatureSlots_.size()) {
                    const auto& slot=definitionCreatureSlots_[*definitionCreatureSlot_];
                    selected="Body "+std::to_string(slot.bodyPart)+" / alternative "+std::to_string(slot.alternative)+" / model "+std::to_string(slot.modelId);
                }
                ImGui::SetNextItemWidth(cardInner);
                const bool open=ImGui::BeginCombo("##creatureslot",selected.c_str());auto_.registerWidget("combo_definition_creature_slot");
                if(open) {
                    for(size_t i=0;i<definitionCreatureSlots_.size();++i) {
                        const auto& slot=definitionCreatureSlots_[i];
                        const auto label="Body "+std::to_string(slot.bodyPart)+" / alternative "+std::to_string(slot.alternative)+" / model "+std::to_string(slot.modelId);
                        if(ImGui::Selectable(label.c_str(),definitionCreatureSlot_==i)) definitionCreatureSlot_=i;
                        auto_.registerWidget(("definition_creature_slot_"+std::to_string(i)).c_str());
                    }
                    ImGui::EndCombo();
                }
                ImGui::TextDisabled("Skinned glTF model (.glb / .gltf)");
                drawPathInput("creaturemodel","Model file",definitionCreatureModel_,sizeof definitionCreatureModel_,cardInner,PathField::Model,"input_definition_creature_model");
                ImGui::TextDisabled("New model name (MESH_ prefix, A-Z / 0-9 / underscore)");ImGui::SetNextItemWidth(cardInner);
                ImGui::InputText("##creaturemeshname",definitionCreatureMeshName_,sizeof definitionCreatureMeshName_,ImGuiInputTextFlags_CallbackCharFilter,definitionASCIIChar);
                auto_.registerWidget("input_definition_creature_mesh_name");
                ImGui::TextDisabled("Optional new texture image");
                drawPathInput("creaturetexture","Image file (optional)",definitionCreatureTexture_,sizeof definitionCreatureTexture_,cardInner,PathField::Image,"input_definition_creature_texture");
                ImGui::TextDisabled("Existing texture ID (0 for none or new image)");ImGui::SetNextItemWidth(cardInner);
                ImGui::InputText("##creaturetextureid",definitionCreatureTextureId_,sizeof definitionCreatureTextureId_,ImGuiInputTextFlags_CharsDecimal);
                auto_.registerWidget("input_definition_creature_texture_id");
                if(definitionCreatureTexture_[0]) {
                    ImGui::TextDisabled("New texture name (A-Z, 0-9, underscore)");ImGui::SetNextItemWidth(cardInner);
                    ImGui::InputText("##creaturetexturename",definitionCreatureTextureName_,sizeof definitionCreatureTextureName_,ImGuiInputTextFlags_CallbackCharFilter,definitionASCIIChar);
                    auto_.registerWidget("input_definition_creature_texture_name");
                    static constexpr const char* formats[]={"DXT1","DXT3","ARGB8888"};
                    ImGui::SetNextItemWidth(cardInner);
                    const bool formatOpen=ImGui::BeginCombo("##creatureformat",formats[definitionCreatureFormat_]);auto_.registerWidget("combo_definition_creature_format");
                    if(formatOpen) {
                        for(int i=0;i<3;++i) {
                            if(ImGui::Selectable(formats[i],definitionCreatureFormat_==i)) definitionCreatureFormat_=i;
                            auto_.registerWidget(("definition_creature_format_"+std::to_string(i)).c_str());
                        }
                        ImGui::EndCombo();
                    }
                }
                ImGui::Checkbox("Store model without compression",&definitionCreatureStored_);auto_.registerWidget("check_definition_creature_stored");
                if(creatureImportDraft_ && creatureImportDraft_->inputs!=creatureImportInputs()) {creatureImportDraft_.reset();definitionEditConfirm_=false;}
            }
        }
        if(!definitionEditConfirm_) {
            if(theme::ghostButton(definitionCreatureMesh_?"Review custom import":definitionCloning_?"Create definition":"Save field",ImVec2(cardInner,0))) {
                if(definitionCreatureMesh_) prepareCreatureImport();else definitionEditConfirm_=true;
            }
            auto_.registerWidget("btn_definition_save");
            if(theme::ghostButton("Cancel edit",ImVec2(cardInner,0))) {definitionEditing_=false;definitionCloning_=false;definitionEditConfirm_=false;definitionCreatureMesh_=false;creatureImportDraft_.reset();definitionCreatureSlots_.clear();definitionCreatureSlot_.reset();definitionCloneFile_={};definitionCloneNamesBytes_.clear();definitionCloneBankBytes_.clear();}
            auto_.registerWidget("btn_definition_edit_cancel");
        } else {
            const char* question=definitionCreatureMesh_?"Create private definitions and import this body model?":definitionCloning_?(definitionEditType_=="clone-creature-private"?
                "Create three private definitions (assets shared)?":definitionEditType_=="clone-creature"?
                "Create creature with shared components?":"Create this definition in the destination bank?"):
                definitionEditType_=="body-model"?"Rebind this body model (shared users; compatibility unverified)?":"Write this field to the destination bank?";
            const int answer=confirmRow(question,definitionCloning_?"Create":"Save",cardInner,S(28),"btn_definition_save_confirm");
            if(answer>0) saveDefinitionEdit();if(answer) definitionEditConfirm_=false;
        }
        ImGui::EndDisabled();
        if(creatureImportFuture_.valid()) ImGui::TextWrapped("Preparing or publishing custom creature import...");
    }
    if(!definitionEditError_.empty()) {ImGui::TextWrapped("%s",definitionEditError_.c_str());auto_.registerWidget("definition_edit_error");}
    if(!definitionEditSuccess_.empty()) ImGui::TextWrapped("%s",definitionEditSuccess_.c_str());
    ImGui::BeginDisabled(definitionEditing_);
    ImGui::SetNextItemWidth(cardInner);
    const bool bankOpen=ImGui::BeginCombo("##definitionbank",definitionBank_.c_str());auto_.registerWidget("combo_definition_bank");
    if(bankOpen) {
        for(const auto* choice:{"game.bin","frontend.bin","script.bin"}) {
            if(ImGui::Selectable(choice,definitionBank_==choice)) setDefinitionBank(choice);
            auto_.registerWidget(("definition_bank_"+std::string(choice)).c_str());
        }
        ImGui::EndCombo();
    }
    ImGui::SetNextItemWidth(cardInner);
    if(ImGui::InputTextWithHint("##definitionsearch","Search name, type, index or group",definitionSearch_,sizeof definitionSearch_)) definitionFilterDirty_=true;
    auto_.registerWidget("input_definition_search");
    ImGui::SetNextItemWidth(cardInner);
    const bool typeOpen=ImGui::BeginCombo("##definitiontype",definitionType_.empty()?"All types":definitionType_.c_str());auto_.registerWidget("combo_definition_type");
    if(typeOpen) {
        if(ImGui::Selectable("All types",definitionType_.empty())) {definitionType_.clear();definitionFilterDirty_=true;}
        auto_.registerWidget("definition_type_all");
        for(size_t i=0;i<definitionTypes_.size();++i) {
            const auto& type=definitionTypes_[i];
            if(ImGui::Selectable(type.c_str(),type==definitionType_)) {definitionType_=type;definitionFilterDirty_=true;}
            auto_.registerWidget(("definition_type_"+std::to_string(i)).c_str());
        }
        ImGui::EndCombo();
    }
    if(definitionFilterDirty_) {
        definitionFiltered_.clear();definitionTree_.clear();const auto needle=definitionLower(definitionSearch_);
        const auto& entries=definitionFile_.entries();
        for(size_t i=0;i<entries.size();++i) {
            const auto& entry=entries[i];
            if(!definitionType_.empty() && entry.definition!=definitionType_) continue;
            std::string groupLabel;
            if(i<definitionGroupOf_.size() && definitionGroupOf_[i]) {
                const auto group=*definitionGroupOf_[i];groupLabel=entries[group].name+" "+std::to_string(group);
            }
            if(!needle.empty() && definitionLower(entry.name+" "+entry.definition+" "+std::to_string(i)+" "+groupLabel).find(needle)==std::string::npos) continue;
            definitionFiltered_.push_back(i);
        }
        std::stable_sort(definitionFiltered_.begin(),definitionFiltered_.end(),[&](size_t a,size_t b) {
            return std::tie(entries[a].definition,entries[a].name,a)<std::tie(entries[b].definition,entries[b].name,b);
        });
        for(const auto index:definitionFiltered_)
            definitionTree_[entries[index].definition][definitionGroupOf_[index]].push_back(index);
        definitionFilterDirty_=false;
    }
    ImGui::TextDisabled("%zu of %zu entries",definitionFiltered_.size(),definitionFile_.entries().size());
    if(definitionsBusy()) theme::hint("Loading definitions...");
    if(!definitionError_.empty()) {ImGui::TextWrapped("%s",definitionError_.c_str());auto_.registerWidget("definition_bank_error");}
    if(!definitionSelectionError_.empty()) ImGui::TextWrapped("%s",definitionSelectionError_.c_str());
    ImGui::BeginChild("##definitionlist",ImVec2(cardInner,S(300)));
    const auto& entries=definitionFile_.entries();
    auto drawRows=[&](const std::vector<size_t>& indices,bool showGroup) {
        ImGuiListClipper clipper;clipper.Begin(int(indices.size()));
        if(definitionReveal_ && definitionSelected_) {
            const auto found=std::find(indices.begin(),indices.end(),*definitionSelected_);
            if(found!=indices.end()) clipper.IncludeItemByIndex(int(found-indices.begin()));
        }
        while(clipper.Step()) for(int visible=clipper.DisplayStart;visible<clipper.DisplayEnd;++visible) {
            const auto index=indices[size_t(visible)];const auto& entry=entries[index];
            auto label=std::to_string(index)+": "+(entry.name.empty()?"(unnamed)":entry.name);
            if(showGroup && definitionGroupOf_[index]) {
                const auto group=*definitionGroupOf_[index];
                label+=" | "+std::to_string(group)+": "+(entries[group].name.empty()?"(unnamed group)":entries[group].name);
            }
            ImGui::PushID(int(index));if(ImGui::Selectable(label.c_str(),definitionSelected_==index)) selectDefinitionIndex(index,false);
            auto_.registerWidget(("definition_row_"+std::to_string(index)).c_str());
            if(definitionReveal_ && definitionSelected_==index) {
                const bool visible=ImGui::IsItemVisible();ImGui::SetScrollHereY(.5f);
                if(visible) definitionReveal_=false;
            }
            ImGui::PopID();
        }
    };
    if(definitionSearch_[0]) drawRows(definitionFiltered_,true);
    else for(const auto& [type,groups]:definitionTree_) {
        const size_t typeIndex=size_t(std::lower_bound(definitionTypes_.begin(),definitionTypes_.end(),type)-definitionTypes_.begin());
        size_t count=0;for(const auto& [group,indices]:groups) count+=indices.size();
        const bool selectedType=definitionSelected_ && entries[*definitionSelected_].definition==type;
        const bool oneType=definitionTree_.size()==1;ImGui::PushID(type.c_str());
        if(!oneType && definitionReveal_ && selectedType) ImGui::SetNextItemOpen(true);
        const auto typeLabel=(type.empty()?"(unnamed type)":type)+" ("+std::to_string(count)+")##type";
        const auto flags=ImGuiTreeNodeFlags_SpanAvailWidth | (definitionFiltered_.size()<=64?ImGuiTreeNodeFlags_DefaultOpen:0);
        const bool typeOpen=oneType || ImGui::TreeNodeEx(typeLabel.c_str(),flags);
        if(!oneType) auto_.registerWidget(("definition_type_node_"+std::to_string(typeIndex)).c_str());
        if(typeOpen) {
            const bool onlyUngrouped=groups.size()==1 && !groups.begin()->first;
            std::vector<std::optional<size_t>> groupOrder;groupOrder.reserve(groups.size());
            for(const auto& [group,indices]:groups) groupOrder.push_back(group);
            std::sort(groupOrder.begin(),groupOrder.end(),[&](const auto& a,const auto& b) {
                if(!a || !b) return !a && bool(b);
                return std::tie(entries[*a].name,*a)<std::tie(entries[*b].name,*b);
            });
            for(const auto& group:groupOrder) {
                const auto& indices=groups.at(group);
                if(onlyUngrouped) {drawRows(indices,false);continue;}
                const auto groupId=group?std::to_string(*group):"none";
                ImGui::PushID(groupId.c_str());
                if(definitionReveal_ && selectedType && definitionGroupOf_[*definitionSelected_]==group) ImGui::SetNextItemOpen(true);
                const auto name=group?std::to_string(*group)+": "+(entries[*group].name.empty()?"(unnamed group)":entries[*group].name):"No confirmed group";
                const auto label=std::string(name)+" ("+std::to_string(indices.size())+")##group";
                const bool open=ImGui::TreeNodeEx(label.c_str(),flags);
                auto_.registerWidget(("definition_group_node_"+std::to_string(typeIndex)+"_"+groupId).c_str());
                if(open) {drawRows(indices,false);ImGui::TreePop();}
                ImGui::PopID();
            }
            if(!oneType) ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::BeginDisabled(definitionsBusy() || modFilesBusy());
    if(theme::ghostButton("Refresh definitions",ImVec2(cardInner,0))) refreshDefinitions();
    auto_.registerWidget("btn_definition_refresh");ImGui::EndDisabled();
    ImGui::TextWrapped("Bank: %s",definitionLoadedBank_.string().c_str());
    ImGui::TextWrapped("Names: %s",definitionLoadedNames_.string().c_str());
    theme::hint("Entry indices are native record identities; unnamed records are included.");ImGui::EndDisabled();theme::endCard();
}
void App::drawDefinitionViewport(const ImVec2&,const ImVec2&) {
    using theme::S;ImGui::SetCursorPos(ImVec2(S(20),S(20)));
    if(!definitionSelected_) {theme::hint("Choose a definition to inspect its fields and raw bytes.");return;}
    const auto& entry=definitionFile_.entries()[*definitionSelected_];
    ImGui::TextWrapped("%s",entry.name.empty()?"(unnamed definition)":entry.name.c_str());
    ImGui::TextWrapped("%s | entry %zu | class index %d | %zu bytes",entry.definition.c_str(),*definitionSelected_,entry.indexInDefinition,entry.data.size());
    if(definitionBank_=="game.bin" && definitionDecoded_) if(const auto group=definitionread::groupTarget(definitionFile_,*definitionDecoded_)) {
        const auto& target=definitionFile_.entries()[*group];
        const auto label="Open group: "+(target.name.empty()?"(unnamed)":target.name)+" (entry "+std::to_string(*group)+")";
        ImGui::BeginDisabled(definitionEditing_);
        if(ImGui::SmallButton(label.c_str())) definitionBrowseIndex_=*group;
        auto_.registerWidget("btn_definition_group");ImGui::EndDisabled();
    }
    if(!definitionDecodeError_.empty()) {ImGui::TextWrapped("%s",definitionDecodeError_.c_str());auto_.registerWidget("definition_decode_error");}
    ImGui::BeginChild("##definitiondetail",ImGui::GetContentRegionAvail());
    if(definitionBank_=="game.bin" && definitionDecoded_ && entry.definition=="CCreatureDef" &&
       definitionDecoded_->clean() && forge::defdecode::encode(*definitionDecoded_)==entry.data) {
        try {
            const auto layout=forge::fmplinks::creaturePrefixLayout(definitionDecoded_->prefix);
            if(!layout.bodyModels.empty()) {
                theme::hint("Body-part model alternatives. Binding changes shared component users; assets remain shared.");
                for(const auto& slot:layout.bodyModels) {
                    uint32_t id=0;
                    for(size_t n=0;n<4;++n)id|=uint32_t(entry.data.at(slot.offset+n))<<(8*n);
                    const auto key=std::to_string(slot.bodyPart)+"_"+std::to_string(slot.alternative);
                    const auto label="Open body "+std::to_string(slot.bodyPart)+" / alternative "+
                        std::to_string(slot.alternative)+" model "+std::to_string(id);
                    ImGui::BeginDisabled(definitionEditing_ || modFilesBusy() || meshReplaceFuture_.valid());
                    if(ImGui::SmallButton(label.c_str()) && selectModelById(id)) assetsTab_=1;
                    auto_.registerWidget(("definition_body_model_"+key).c_str());ImGui::EndDisabled();
                    ImGui::BeginDisabled(definitionEditing_ || definitionsBusy() || modFilesBusy());
                    if(ImGui::SmallButton(("Bind body "+std::to_string(slot.bodyPart)+" / alternative "+std::to_string(slot.alternative)).c_str()))
                        beginDefinitionBodyModel(slot.bodyPart,slot.alternative);
                    auto_.registerWidget(("definition_bind_body_"+key).c_str());ImGui::EndDisabled();
                }
            }
        } catch(const std::exception&) {
            // Unsupported counted prefixes stay inspectable, with no guessed links.
        }
    }
    if(definitionBank_=="game.bin" && definitionDecoded_ && entry.definition=="CREATURE") {
        for(const auto& [kind,label,widget]:std::array<std::tuple<const char*,const char*,const char*>,2>{
                std::tuple{"CCreatureDef","Open body definition","btn_definition_body"},
                std::tuple{"CAppearanceDef","Open appearance definition","btn_definition_appearance"}}) {
            if(const auto target=definitionread::creatureVisualTarget(definitionFile_,entry,*definitionDecoded_,kind)) {
                const auto text=std::string(label)+" (entry "+std::to_string(*target)+")";
                ImGui::BeginDisabled(definitionEditing_);
                if(ImGui::SmallButton(text.c_str())) definitionBrowseIndex_=*target;
                auto_.registerWidget(widget);ImGui::EndDisabled();
            }
        }
        theme::hint("Component edits affect every creature that shares that definition.");
    }
    if(*definitionSelected_<definitionMembers_.size() && !definitionMembers_[*definitionSelected_].empty()) {
        const auto& members=definitionMembers_[*definitionSelected_];
        if(ImGui::CollapsingHeader(("Group members ("+std::to_string(members.size())+")").c_str(),ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BeginDisabled(definitionEditing_);
            ImGui::BeginChild("##definitionmembers",ImVec2(0,S(150)));
            ImGuiListClipper memberClipper;memberClipper.Begin(int(members.size()));
            while(memberClipper.Step()) for(int row=memberClipper.DisplayStart;row<memberClipper.DisplayEnd;++row) {
                const auto index=members[size_t(row)];const auto& member=definitionFile_.entries()[index];
                const auto label=std::to_string(index)+": "+(member.name.empty()?"(unnamed)":member.name)+" | "+member.definition;
                if(ImGui::Selectable(label.c_str())) definitionBrowseIndex_=index;
                auto_.registerWidget(("definition_member_"+std::to_string(index)).c_str());
            }
            ImGui::EndChild();ImGui::EndDisabled();
        }
    }
    if(definitionDecoded_) {
        ImGui::Text("%zu fields; %zu prefix bytes",definitionDecoded_->fields.size(),definitionDecoded_->prefixLen);
        if(ImGui::BeginTable("##definitionfields",3,ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg|ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Field");ImGui::TableSetupColumn("Type");ImGui::TableSetupColumn("Value");ImGui::TableHeadersRow();
            for(size_t i=0;i<definitionDecoded_->fields.size();++i) {
                const auto& field=definitionDecoded_->fields[i];const auto value=definitionwrite::fieldValue(field);ImGui::PushID(int(i));
                ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);ImGui::TextWrapped("%s",field.name.c_str());
                ImGui::TableSetColumnIndex(1);ImGui::TextWrapped("%s",field.type.c_str());
                ImGui::TableSetColumnIndex(2);size_t previewSize=std::min<size_t>(2048,value.size());
                while(previewSize>0 && previewSize<value.size() && (static_cast<unsigned char>(value[previewSize])&0xc0)==0x80) --previewSize;
                const auto preview=value.size()>previewSize?value.substr(0,previewSize)+" ... (full value: right-click to copy)":value;
                ImGui::TextWrapped("%s",preview.c_str());auto_.registerWidget(("definition_field_"+std::to_string(i)).c_str());
                if(ImGui::BeginPopupContextItem("##definitionvalue")) {if(ImGui::MenuItem("Copy full value")) ImGui::SetClipboardText(value.c_str());ImGui::EndPopup();}
                if(const auto offset=definitionread::nameOffset(*definitionDecoded_,i)) {
                    const auto found=definitionNameIndices_.find(*offset);
                    if(found!=definitionNameIndices_.end()) {
                        const auto& name=definitionFile_.originalNames()[found->second].second;
                        ImGui::TextWrapped("Name: %s",name.empty()?"(empty name)":name.c_str());
                        auto_.registerWidget(("definition_name_reference_"+std::to_string(i)).c_str());
                    }
                }
                if(definitionBank_=="game.bin") if(const auto model=definitionread::objectModelId(entry,*definitionDecoded_,i)) {
                    ImGui::BeginDisabled(definitionEditing_ || definitionsBusy() || modFilesBusy() || !ctx_.ready() || ctxFuture_.valid());
                    if(ImGui::SmallButton(("Open model "+std::to_string(*model)).c_str()) && selectModelById(*model)) assetsTab_=1;
                    auto_.registerWidget(("definition_model_"+std::to_string(i)).c_str());ImGui::EndDisabled();
                }
                if(definitionBank_=="game.bin") if(const auto texture=definitionread::themeTextureId(entry,*definitionDecoded_,i)) {
                    ImGui::BeginDisabled(definitionEditing_ || definitionsBusy() || modFilesBusy() || !ctx_.ready() || ctxFuture_.valid());
                    if(ImGui::SmallButton(("Open texture "+std::to_string(*texture)).c_str())) {
                        if(!texturesLoaded_) refreshTextures();
                        std::string error;const auto* row=texbrowse::findTextureRowById(texRows_,"GBANK_MAIN_PC",*texture,error);
                        if(row) {if(selectTexture(row->name)) assetsTab_=0;}
                        else pushLog("textures: "+error,1);
                    }
                    auto_.registerWidget(("definition_texture_"+std::to_string(i)).c_str());ImGui::EndDisabled();
                }
                if(definitionwrite::fieldEditable(field)) {
                    ImGui::BeginDisabled(definitionEditing_ || definitionsBusy() || modFilesBusy());
                    if(ImGui::SmallButton("Edit field")) beginDefinitionEdit(i);
                    auto_.registerWidget(("btn_definition_edit_"+std::to_string(i)).c_str());ImGui::EndDisabled();
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    } else theme::hint("This record has no confirmed field decode. Its complete payload remains available below.");
    ImGui::Separator();theme::label("Raw payload");auto_.registerWidget("definition_raw_payload");
    ImGui::BeginChild("##definitionraw",ImVec2(0,S(230)));
    ImGuiListClipper clipper;clipper.Begin(int((entry.data.size()+15)/16),ImGui::GetTextLineHeightWithSpacing());
    while(clipper.Step()) for(int line=clipper.DisplayStart;line<clipper.DisplayEnd;++line) {
        const size_t start=size_t(line)*16;char buffer[96];int written=std::snprintf(buffer,sizeof buffer,"%06zx  ",start);
        for(size_t i=start;i<std::min(start+16,entry.data.size());++i) written+=std::snprintf(buffer+written,sizeof buffer-size_t(written),"%02x ",entry.data[i]);
        ImGui::TextUnformatted(buffer);
    }
    ImGui::EndChild();ImGui::EndChild();
}
} // namespace albion::gui
