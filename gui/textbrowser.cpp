#include "app.hpp"
#include "theme.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "forge/big.hpp"
#include "forge/temporarydirectory.hpp"
#include "textwrite.hpp"
#include "textdisplay.hpp"
#include "dialogueaudio.hpp"
#include "modpack.hpp"
#include <nlohmann/json.hpp>

namespace albion::gui {
namespace fs=std::filesystem;
using theme::S;

fs::path App::textBigPath() const {
    const auto relative=fs::path("data")/"lang"/fs::u8path(textLanguage_)/"text.big";
    std::error_code ec;
    if(!packDest_.empty()) {
        const auto packed=fs::u8path(packDest_)/relative;
        if(fs::is_regular_file(packed,ec)) return packed;
    }
    const auto staged=fs::u8path(saveRoot())/relative;
    if(fs::is_regular_file(staged,ec)) return staged;
    return fs::u8path(installPath_)/relative;
}

std::vector<std::string> App::textLanguages() const {
    std::set<std::string> languages;
    for(const auto& root:{packDest_,saveRoot(),installPath_}) {
        if(root.empty()) continue;
        std::error_code ec;const auto folder=fs::u8path(root)/"data"/"lang";
        fs::directory_iterator iterator(folder,ec);
        if(ec) continue;
        const fs::directory_iterator end;
        for(;iterator!=end;iterator.increment(ec)) {
            if(ec) break;
            const auto& entry=*iterator;
            if(entry.is_directory(ec) && fs::is_regular_file(entry.path()/"text.big",ec)) {
                const auto name=entry.path().filename().u8string();
                languages.emplace(name.begin(),name.end());
            }
        }
    }
    languages.insert(textLanguage_);
    return {languages.begin(),languages.end()};
}

bool App::setTextLanguage(const std::string& language) {
    const auto languages=textLanguages();
    if(std::find(languages.begin(),languages.end(),language)==languages.end()) {
        textSelectionError_="No text bank for that language.";return false;
    }
    if(textLanguage_==language) return true;
    textLanguage_=language;textLoaded_=false;
    textRows_.clear();textById_.clear();textSelected_.reset();textFilteredRows_.clear();
    textError_.clear();textSelectionError_.clear();textFilterDirty_=true;
    return true;
}

void App::refreshTextBank() {
    if(textFuture_.valid() || modFilesBusy() || !installValid_) return;
    textRows_.clear();textById_.clear();textFilteredRows_.clear();textSelected_.reset();
    textError_.clear();textSelectionError_.clear();textFilterDirty_=true;
    textLoadedPath_=textBigPath();textLoaded_=true;
    const auto path=textLoadedPath_;
    textFuture_=std::async(std::launch::async,[path] {
        TextJob job;job.path=path;
        try {
            const auto file=forge::big::File::open(path);
            for(const auto& bank:file.banks()) for(const auto& entry:bank.entries) {
                TextRow row;row.bank=bank.name;row.name=entry.name;row.id=entry.id;row.type=entry.type;
                if(entry.type==0 || entry.type==1) {
                    if(entry.type==0) row.speakerHeader=entry.subHeader;
                    try {row.value=forge::textbig::decode(file.entryData(entry),int(entry.type));}
                    catch(const std::exception& error) {row.error=error.what();}
                }
                if(entry.type==2) {
                    try {
                        if(entry.length!=0) throw std::runtime_error("Narrator list has an unexpected payload.");
                        row.narrators=forge::textbig::decodeNarrators(entry.subHeader);
                    } catch(const std::exception& error) {row.error=error.what();}
                }
                job.rows.push_back(std::move(row));
            }
            std::stable_sort(job.rows.begin(),job.rows.end(),[](const auto& a,const auto& b) {
                return std::tie(a.name,a.bank,a.id)<std::tie(b.name,b.bank,b.id);
            });
        } catch(const std::exception& error) {job.error=error.what();}
        return job;
    });
}

void App::pollTextBank() {
    pollTextExport();
    if(!textFuture_.valid() || textFuture_.wait_for(std::chrono::seconds(0))!=std::future_status::ready) return;
    auto job=textFuture_.get();
    if(job.path!=textBigPath()) {textLoaded_=false;return;}
    textRows_=std::move(job.rows);textError_=std::move(job.error);textSelected_.reset();
    textById_.clear();textFilteredRows_.clear();textFilterDirty_=true;
    for(size_t index=0;index<textRows_.size();++index)
        textById_[{textRows_[index].bank,textRows_[index].id}].push_back(index);
    if(!textError_.empty()) pushLog("text: "+textError_,1);
}

bool App::exportTextTsv(const std::string& path) {
    textExportError_.clear();textExportSuccess_.clear();
    try {
        if(textExportBusy()) throw std::runtime_error("A text export is already running.");
        if(!textLoaded_ || textsBusy() || modFilesBusy() || !textError_.empty() || textLoadedPath_!=textBigPath())
            throw std::runtime_error("Wait for the current text bank to load before exporting.");
        if(path.empty()) throw std::runtime_error("Choose a new TSV file.");
        const auto output=fs::absolute(fs::u8path(path)).lexically_normal();
        auto extension=output.extension().string();
        std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
        if(extension!=".tsv") throw std::runtime_error("Choose a file ending in .tsv.");
        std::error_code ec;const auto status=fs::symlink_status(output,ec);
        if(ec && ec!=std::errc::no_such_file_or_directory) throw std::runtime_error(ec.message());
        if(status.type()!=fs::file_type::not_found) throw std::runtime_error("That path already exists. Choose a new TSV file.");
        if(!fs::is_directory(output.parent_path())) throw std::runtime_error("The output folder does not exist.");
        textExportFuture_=std::async(std::launch::async,[rows=textRows_,output,path] {
            TextExportJob job;job.path=path;
            try {
                forge::TemporaryDirectory work(output.parent_path(),"forge-text-export-");
                const auto temporary=work.path()/"text.tsv";
                std::ofstream stream(temporary,std::ios::binary);
                if(!stream) throw std::runtime_error("Cannot create the text export.");
                stream.write("\xEF\xBB\xBF",3);
                stream<<"bank\tid\tname\ttype\tcontent\tspeaker\tspeech_bank\tidentifier\ttags\tmembers\tnarrators\terror\r\n";
                auto cell=[&](const std::string& value) {
                    stream.put('"');for(char c:value) {if(c=='"') stream.put('"');stream.put(c);}stream.put('"');
                };
                for(const auto& row:rows) {
                    auto tags=nlohmann::json::array();
                    for(const auto& tag:row.value.tags) tags.push_back({{"position",tag.position},{"name",textdisplay::metadataUtf8(tag.name)}});
                    const std::vector<std::string> fields={row.bank,std::to_string(row.id),row.name,std::to_string(row.type),
                        row.value.content,textdisplay::metadataUtf8(row.value.speaker),textdisplay::metadataUtf8(row.value.speechBank),textdisplay::metadataUtf8(row.value.identifier),tags.dump(),
                        nlohmann::json(row.value.groupMembers).dump(),nlohmann::json(row.narrators).dump(),row.error};
                    for(size_t i=0;i<fields.size();++i) {if(i) stream.put('\t');cell(fields[i]);}
                    stream<<"\r\n";
                }
                stream.flush();if(!stream) throw std::runtime_error("Cannot finish the text export.");
                stream.close();if(!stream) throw std::runtime_error("Cannot close the text export.");
                // Publish only a new destination; a file appearing during export is preserved.
                if(!MoveFileExW(temporary.c_str(),output.c_str(),MOVEFILE_WRITE_THROUGH))
                    throw std::runtime_error("Cannot publish the export; choose a new writable TSV path.");
                job.rows=rows.size();
            } catch(const std::exception& error) {job.error=error.what();}
            return job;
        });
        return true;
    } catch(const std::exception& error) {textExportError_=error.what();return false;}
}

void App::pollTextExport() {
    if(!textExportFuture_.valid() || textExportFuture_.wait_for(std::chrono::seconds(0))!=std::future_status::ready) return;
    const auto job=textExportFuture_.get();textExportError_=job.error;
    textExportSuccess_=job.error.empty()?"Exported "+std::to_string(job.rows)+" entries to "+job.path:"";
    pushLog(job.error.empty()?textExportSuccess_:"text export: "+job.error,job.error.empty()?0:1);
}

void App::browseText(const std::string& nameOrId) {
    selectionInspectorOpen_=false;
    setModsMode(false);setTexturesMode(true);assetsTab_=5;
    textSearch_[0]=0;textFilterDirty_=true;
    textBrowseRequest_=nameOrId;
}

bool App::setTextEditContent(const std::string& content) {
    if(!textEditing_ || content.size()>=1024*1024 || content.find('\0')!=std::string::npos) return false;
    std::fill(textEditContent_.begin(),textEditContent_.end(),0);
    std::copy(content.begin(),content.end(),textEditContent_.begin());
    textEditConfirm_=false;return true;
}

bool App::beginTextEdit() {
    textEditError_.clear();textEditSuccess_.clear();
    textEditing_=false;textEditConfirm_=false;textEditAdding_=false;
    textEditChanges_={};textEditTagSelected_=-1;textEditSpeechBank_[0]=0;textEditIdentifier_[0]=0;
    textEditSpeakerNames_.clear();textEditSpeakerHeader_.clear();
    if(!textLoaded_ || textsBusy() || !textSelected_ || modFilesBusy() ||
       textLoadedPath_!=textBigPath()) {textEditError_="Wait for a current text entry to load.";return false;}
    const auto& row=textRows_[*textSelected_];
    if(row.type!=0 || !row.error.empty() || row.value.content.size()>=1024*1024) {
        textEditError_="Choose a decoded string smaller than 1 MiB.";return false;
    }
    if(!packDest_.empty() && !modpack::isPack(fs::u8path(packDest_))) {textEditError_="Choose a valid FableForge pack before editing text.";return false;}
    if(!packDest_.empty()) {
        try {(void)modpack::load(fs::u8path(packDest_));}
        catch(const std::exception& error) {textEditError_=error.what();return false;}
    }
    textEditName_=row.name;textEditBank_=row.bank;textEditId_=row.id;textEditExpected_=row.value;
    textEditSpeakerHeader_=row.speakerHeader;
    if(textwrite::editableMetadata(row.value.speechBank)) std::copy(row.value.speechBank.c_str(),row.value.speechBank.c_str()+row.value.speechBank.size()+1,textEditSpeechBank_);
    if(textwrite::editableMetadata(row.value.identifier)) std::copy(row.value.identifier.c_str(),row.value.identifier.c_str()+row.value.identifier.size()+1,textEditIdentifier_);
    const TextRow* narrators=nullptr;
    bool ambiguousNarrators=false;
    for(const auto& candidate:textRows_) if(candidate.bank==row.bank && candidate.type==2) {
        if(narrators) ambiguousNarrators=true;
        narrators=&candidate;
    }
    if(!ambiguousNarrators && narrators && narrators->name=="[NarratorList]" &&
       narrators->error.empty() && row.speakerHeader.size()==4)
        textEditSpeakerNames_=narrators->narrators;
    textEditSource_=textLoadedPath_;
    textEditDestination_=fs::u8path(packDest_.empty()?saveRoot():packDest_)/"data"/"lang"/fs::u8path(textLanguage_)/"text.big";
    std::error_code destinationError;
    if(fs::exists(textEditDestination_,destinationError) && !fs::is_regular_file(textEditDestination_,destinationError)) {
        textEditError_="The destination text bank is not a file.";return false;
    }
    if(destinationError) {
        textEditError_="Cannot inspect the destination text bank.";return false;
    }
    textEditContent_.assign(1024*1024,0);textEditing_=true;textEditConfirm_=false;
    return setTextEditContent(row.value.content);
}

bool App::beginTextAdd() {
    if(!beginTextEdit()) return false;
    textEditAdding_=true;textAddName_[0]=0;return true;
}

bool App::setTextEditSpeaker(const std::string& speaker) {
    if(!textEditing_ || std::count(textEditSpeakerNames_.begin(),textEditSpeakerNames_.end(),speaker)!=1) {
        textEditError_="Choose a unique speaker from this bank's narrator list.";return false;
    }
    if(speaker==textEditExpected_.speaker) textEditChanges_.speaker.reset();else textEditChanges_.speaker=speaker;
    textEditError_.clear();textEditConfirm_=false;return true;
}

bool App::setTextEditSpeechBank(const std::string& bank) {
    if(!textEditing_ || !textwrite::editableMetadata(textEditExpected_.speechBank) || !textwrite::editableMetadata(bank)) {
        textEditError_="Speech-bank metadata must be ASCII and smaller than 4096 bytes.";return false;
    }
    std::copy(bank.c_str(),bank.c_str()+bank.size()+1,textEditSpeechBank_);
    if(bank==textEditExpected_.speechBank) textEditChanges_.speechBank.reset();else textEditChanges_.speechBank=bank;
    textEditError_.clear();textEditConfirm_=false;return true;
}

bool App::setTextEditIdentifier(const std::string& identifier) {
    if(!textEditing_ || textEditAdding_ || !textwrite::editableMetadata(textEditExpected_.identifier) || !textwrite::editableMetadata(identifier)) {
        textEditError_="Choose an existing string and an ASCII identifier smaller than 4096 bytes.";return false;
    }
    std::copy(identifier.c_str(),identifier.c_str()+identifier.size()+1,textEditIdentifier_);
    if(identifier==textEditExpected_.identifier) textEditChanges_.identifier.reset();else textEditChanges_.identifier=identifier;
    textEditError_.clear();textEditConfirm_=false;return true;
}

bool App::setTextEditTag(size_t index,int32_t position,const std::string& name) {
    const auto& tags=textEditChanges_.tags?*textEditChanges_.tags:textEditExpected_.tags;
    if(!textEditing_ || index>=tags.size() || !textwrite::editableMetadata(tags[index].name) || !textwrite::editableMetadata(name)) {
        textEditError_="Choose a tag with an ASCII name smaller than 4096 bytes.";return false;
    }
    auto updated=tags;updated[index]={position,name};textEditChanges_.tags=std::move(updated);
    textEditError_.clear();textEditConfirm_=false;return true;
}
bool App::addTextEditTag() {
    const auto& tags=textEditChanges_.tags?*textEditChanges_.tags:textEditExpected_.tags;
    if(!textEditing_ || tags.size()>=4096) {textEditError_="A draft can add tags only below 4096 entries.";return false;}
    const auto newIndex=tags.size();auto updated=tags;updated.push_back({0,""});textEditChanges_.tags=std::move(updated);
    textEditTagSelected_=int(newIndex);textEditError_.clear();textEditConfirm_=false;return true;
}
bool App::removeTextEditTag(size_t index) {
    const auto& tags=textEditChanges_.tags?*textEditChanges_.tags:textEditExpected_.tags;
    if(!textEditing_ || index>=tags.size()) {textEditError_="Choose a tag to remove.";return false;}
    auto updated=tags;updated.erase(updated.begin()+index);textEditChanges_.tags=std::move(updated);
    textEditTagSelected_=-1;textEditError_.clear();textEditConfirm_=false;return true;
}

bool App::setTextAddName(const std::string& name) {
    if(!textEditing_ || !textEditAdding_ || name.size()>=sizeof textAddName_ || name.find('\0')!=std::string::npos) return false;
    std::copy(name.begin(),name.end(),textAddName_);textAddName_[name.size()]=0;
    textEditConfirm_=false;return true;
}

bool App::saveTextEdit() {
    textEditError_.clear();textEditSuccess_.clear();
    try {
        if(!textEditing_ || textEditContent_.empty()) throw std::runtime_error("Choose a string to edit first.");
        if(textsBusy() || modReadersBusy() || gameWriteBlocked("save text"))
            throw std::runtime_error("Wait for loads and writes to finish, and close the destination game.");
        const auto destination=fs::u8path(packDest_.empty()?saveRoot():packDest_)/"data"/"lang"/fs::u8path(textLanguage_)/"text.big";
        if((!packDest_.empty() && !modpack::isPack(fs::u8path(packDest_))) || textEditSource_!=textBigPath() || destination!=textEditDestination_)
            throw std::runtime_error("Text source or destination changed; start the edit again.");
        // A previously absent staged bank may have appeared since the edit began.
        if(textEditSource_!=destination && fs::exists(destination))
            throw std::runtime_error("The destination bank changed; refresh before editing.");
        if(!packDest_.empty()) (void)modpack::load(fs::u8path(packDest_));
        std::string selectedName=textEditName_;
        if(textEditAdding_) {
            selectedName=textAddName_;
            if(selectedName.empty()) throw std::runtime_error("Enter a new text name before saving.");
            textwrite::addStringFromDonor(textEditSource_,destination,textEditName_,textEditBank_,textEditId_,
                                          textEditExpected_,selectedName,std::string(textEditContent_.data()),
                                          textEditChanges_,textEditSpeakerHeader_);
        } else {
            textwrite::replaceContent(textEditSource_,destination,textEditName_,textEditBank_,textEditId_,
                                      textEditExpected_,std::string(textEditContent_.data()),
                                      textEditChanges_,textEditSpeakerHeader_);
        }
        textEditSuccess_="Saved "+selectedName+".";
        textEditing_=false;textEditConfirm_=false;
        textBrowseRequest_=selectedName;textLoaded_=false;
        dialogueTextIndex_.reset();dialogueTextRoot_.clear();dialogueTextError_.clear();
        dialogueSearchCacheKey_.clear();dialogueSearchResults_.clear();dialogueSearchGroups_.clear();
        dialogueSubtitles_.clear();rescanBackups();
        pushLog(textEditSuccess_,3);return true;
    } catch(const std::exception& error) {textEditError_=error.what();pushLog("save text: "+textEditError_,1);return false;}
}

bool App::selectText(const std::string& nameOrId) {
    if(textFuture_.valid() || !textLoaded_) return false;
    const bool exact=std::any_of(textRows_.begin(),textRows_.end(),[&](const auto& row){return row.name==nameOrId;});
    uint32_t id=0;
    const auto parsed=std::from_chars(nameOrId.data(),nameOrId.data()+nameOrId.size(),id);
    const bool numeric=parsed.ec==std::errc{} && parsed.ptr==nameOrId.data()+nameOrId.size();
    std::optional<size_t> selected;
    for(size_t index=0;index<textRows_.size();++index) {
        const auto& row=textRows_[index];
        if(exact ? row.name==nameOrId : numeric && row.id==id) {
            if(selected) {textSelectionError_="Text name or ID is ambiguous; choose a row from the list.";return false;}
            selected=index;
        }
    }
    if(!selected) {textSelectionError_="No matching text entry.";return false;}
    textSelected_=selected;textSelectionError_.clear();return true;
}

void App::drawTextBrowser(float pad,float inner,float cardInner) {
    if(textLoadedPath_!=textBigPath()) textLoaded_=false;
    if(!textLoaded_ && !textFuture_.valid()) refreshTextBank();
    if(textLoaded_ && !textFuture_.valid() && textBrowseRequest_) {
        selectText(*textBrowseRequest_);textBrowseRequest_.reset();
    }
    ImGui::SetCursorPosX(pad);theme::beginCard("##textbrowser",inner);theme::label("Text");
    ImGui::SetNextItemWidth(cardInner-S(76));
    if(ImGui::BeginCombo("##textlanguage",textLanguage_.c_str())) {
        for(const auto& language:textLanguages()) {
            if(ImGui::Selectable(language.c_str(),language==textLanguage_)) setTextLanguage(language);
            auto_.registerWidget(("text_language_"+language).c_str());
        }
        ImGui::EndCombo();
    }
    auto_.registerWidget("combo_text_language");ImGui::SameLine(0,S(6));
    if(theme::ghostButton("Refresh",ImVec2(S(70),0))) refreshTextBank();
    auto_.registerWidget("btn_text_refresh");
    ImGui::SetNextItemWidth(cardInner);
    if(focusFilter_) {ImGui::SetKeyboardFocusHere();focusFilter_=false;}
    if(ImGui::InputTextWithHint("##textsearch","Search name, ID or content",textSearch_,sizeof textSearch_)) textFilterDirty_=true;
    auto_.registerWidget("input_textsearch");
    if(textFilterDirty_) {
        textFilteredRows_.clear();const std::string needle=textSearch_;
        auto contains=[&](const std::string& value) {
            return std::search(value.begin(),value.end(),needle.begin(),needle.end(),[](char a,char b) {
                return std::tolower(static_cast<unsigned char>(a))==std::tolower(static_cast<unsigned char>(b));
            })!=value.end();
        };
        for(size_t index=0;index<textRows_.size();++index) {
            const auto& row=textRows_[index];
            if(needle.empty() || contains(row.name) || contains(row.value.content) || needle==std::to_string(row.id))
                textFilteredRows_.push_back(index);
        }
        textFilterDirty_=false;
    }
    ImGui::TextDisabled("%zu of %zu entries",textFilteredRows_.size(),textRows_.size());
    if(textFuture_.valid()) {theme::hint("Loading text...");auto_.registerWidget("text_loading");}
    if(!textError_.empty() || !textSelectionError_.empty()) {
        ImGui::TextWrapped("%s",(!textError_.empty()?textError_:textSelectionError_).c_str());
        auto_.registerWidget("text_error");
    }
    ImGui::BeginChild("##textlist",ImVec2(cardInner,S(230)));
    ImGuiListClipper clipper;clipper.Begin(int(textFilteredRows_.size()));
    while(clipper.Step()) for(int visible=clipper.DisplayStart;visible<clipper.DisplayEnd;++visible) {
        const auto index=textFilteredRows_[size_t(visible)];const auto& row=textRows_[index];
        ImGui::PushID(int(index));
        const std::string label=row.name.empty()?"(unnamed) "+std::to_string(row.id):row.name;
        if(ImGui::Selectable(label.c_str(),textSelected_==index)) {textSelected_=index;textSelectionError_.clear();}
        auto_.registerWidget(("text_row_"+std::to_string(index)).c_str());ImGui::PopID();
    }
    ImGui::EndChild();
    if(textSelected_) {
        const auto& row=textRows_[*textSelected_];
        ImGui::Separator();ImGui::TextWrapped("%s",row.name.c_str());
        ImGui::Text("ID %u",row.id);ImGui::TextWrapped("Bank: %s",row.bank.c_str());
        if(row.type==0 && row.error.empty()) {
            ImGui::TextWrapped("Speaker: %s",textdisplay::metadataUtf8(row.value.speaker).c_str());
            ImGui::TextWrapped("Speech bank: %s",textdisplay::metadataUtf8(row.value.speechBank).c_str());
            ImGui::TextWrapped("Identifier: %s",textdisplay::metadataUtf8(row.value.identifier).c_str());
            for(const auto& tag:row.value.tags) ImGui::TextWrapped("Tag at %d: %s",tag.position,textdisplay::metadataUtf8(tag.name).c_str());
        }
    }
    theme::endCard();
    ImGui::SetCursorPosX(pad);theme::beginCard("##textexport",inner);theme::label("Export text bank");
    if(drawPathInput("textexportpath","New .tsv file",textExportPath_,sizeof textExportPath_,cardInner,
                     PathField::TextExport,"input_text_export")) {textExportError_.clear();textExportSuccess_.clear();}
    ImGui::BeginDisabled(textExportBusy() || !textLoaded_ || textsBusy() || !textError_.empty() || modFilesBusy());
    if(theme::ghostButton("Export all entries",ImVec2(cardInner,0))) exportTextTsv(textExportPath_);
    auto_.registerWidget("btn_text_export");ImGui::EndDisabled();
    if(textExportBusy()) theme::hint("Exporting text...");
    if(!textExportError_.empty()) ImGui::TextWrapped("%s",textExportError_.c_str());
    if(!textExportSuccess_.empty()) ImGui::TextWrapped("%s",textExportSuccess_.c_str());
    theme::hint("Exports the loaded bank, including entries outside the search. Choose a new file; existing files are preserved.");
    theme::endCard();
    ImGui::SetCursorPosX(pad);theme::beginCard("##textedit",inner,true);theme::label("Edit string");
    drawPackDestination(cardInner,true);
    if(!textEditing_) {
        ImGui::BeginDisabled(textsBusy() || !textSelected_ || modFilesBusy());
        if(theme::ghostButton("Edit selected string",ImVec2(cardInner,0))) beginTextEdit();
        auto_.registerWidget("btn_text_edit");
        if(theme::ghostButton("New string from donor",ImVec2(cardInner,0))) beginTextAdd();
        auto_.registerWidget("btn_text_add");ImGui::EndDisabled();
    } else {
        ImGui::TextWrapped(textEditAdding_?"Donor: %s":"Editing: %s",textEditName_.c_str());
        if(textEditAdding_) {
            ImGui::SetNextItemWidth(cardInner);
            if(ImGui::InputTextWithHint("##textaddname","New name, e.g. TXT_MY_STRING",textAddName_,sizeof textAddName_)) textEditConfirm_=false;
            auto_.registerWidget("input_text_add_name");
            theme::hint("The donor stays intact. A free ID is allocated, and the new identifier matches the new name.");
        }
        ImGui::TextWrapped("Destination: %s",textEditDestination_.string().c_str());
        theme::label("Speaker");ImGui::SetNextItemWidth(cardInner);
        const auto speaker=textdisplay::metadataUtf8(textEditChanges_.speaker?*textEditChanges_.speaker:textEditExpected_.speaker);
        ImGui::BeginDisabled(textEditSpeakerNames_.empty());
        const bool speakerOpen=ImGui::BeginCombo("##texteditspeaker",speaker.empty()?"(empty)":speaker.c_str());
        auto_.registerWidget("combo_text_edit_speaker");
        if(speakerOpen) {
            for(size_t i=0;i<textEditSpeakerNames_.size();++i) {
                const auto& option=textEditSpeakerNames_[i];ImGui::PushID(int(i));
                ImGui::BeginDisabled(std::count(textEditSpeakerNames_.begin(),textEditSpeakerNames_.end(),option)!=1);
                if(ImGui::Selectable(option.empty()?"(empty)":option.c_str(),option==speaker)) setTextEditSpeaker(option);
                auto_.registerWidget(("text_speaker_option_"+std::to_string(i)).c_str());
                ImGui::EndDisabled();ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        if(!textEditSpeakerNames_.empty()) theme::hint("Choose an existing narrator. Saving updates its speaker index too.");
        const auto asciiFilter=[](ImGuiInputTextCallbackData* data)->int {return data->EventChar>=128?1:0;};
        theme::label("Speech bank");ImGui::SetNextItemWidth(cardInner);
        if(textwrite::editableMetadata(textEditExpected_.speechBank)) {
            if(ImGui::InputText("##texteditspeechbank",textEditSpeechBank_,sizeof textEditSpeechBank_,ImGuiInputTextFlags_CallbackCharFilter,asciiFilter))
                setTextEditSpeechBank(std::string(textEditSpeechBank_));
            auto_.registerWidget("input_text_edit_speechbank");
        } else theme::hint("This bank reference has unsupported encoding or length; it stays unchanged.");
        theme::label("Identifier");ImGui::SetNextItemWidth(cardInner);
        if(textEditAdding_) ImGui::TextWrapped("%s",textAddName_[0]?textAddName_:"Uses the new string name.");
        else if(textwrite::editableMetadata(textEditExpected_.identifier)) {
            if(ImGui::InputText("##texteditidentifier",textEditIdentifier_,sizeof textEditIdentifier_,ImGuiInputTextFlags_CallbackCharFilter,asciiFilter))
                setTextEditIdentifier(std::string(textEditIdentifier_));
            auto_.registerWidget("input_text_edit_identifier");
        } else theme::hint("This identifier has unsupported encoding or length; it stays unchanged.");
        theme::label("Dialogue tags");
        const auto& tags=textEditChanges_.tags?*textEditChanges_.tags:textEditExpected_.tags;
        const bool tagSelected=textEditTagSelected_>=0 && size_t(textEditTagSelected_)<tags.size();
        const std::string tagLabel=tagSelected?std::to_string(textEditTagSelected_+1)+": "+textdisplay::metadataUtf8(tags[size_t(textEditTagSelected_)].name):"Choose a tag";
        ImGui::SetNextItemWidth(cardInner);
        const bool tagsOpen=ImGui::BeginCombo("##textedittags",tagLabel.c_str());auto_.registerWidget("combo_text_edit_tags");
        if(tagsOpen) {
            for(size_t i=0;i<tags.size();++i) {
                const auto label=std::to_string(i+1)+": "+textdisplay::metadataUtf8(tags[i].name);
                if(ImGui::Selectable(label.c_str(),int(i)==textEditTagSelected_)) textEditTagSelected_=int(i);
                auto_.registerWidget(("text_tag_option_"+std::to_string(i)).c_str());
            }
            ImGui::EndCombo();
        }
        if(tagSelected) {
            const auto index=size_t(textEditTagSelected_);const auto tag=tags[index];
            if(textwrite::editableMetadata(tag.name)) {
                int position=tag.position;char name[4096]={};std::copy(tag.name.begin(),tag.name.end(),name);
                theme::label("Tag position");ImGui::SetNextItemWidth(cardInner);
                if(ImGui::InputInt("##textedittagposition",&position,0,0)) setTextEditTag(index,position,tag.name);
                auto_.registerWidget("input_text_edit_tag_position");
                theme::label("Tag name");ImGui::SetNextItemWidth(cardInner);
                if(ImGui::InputText("##textedittagname",name,sizeof name,ImGuiInputTextFlags_CallbackCharFilter,asciiFilter))
                    setTextEditTag(index,position,std::string(name));
                auto_.registerWidget("input_text_edit_tag_name");
            } else theme::hint("This tag name has unsupported bytes; it stays unchanged.");
            if(theme::ghostButton("Remove selected tag",ImVec2(cardInner,0))) removeTextEditTag(index);
            auto_.registerWidget("btn_text_tag_remove");
        }
        if(theme::ghostButton("Add tag",ImVec2(cardInner,0))) addTextEditTag();auto_.registerWidget("btn_text_tag_add");
        theme::hint("Positions are stored character indices. Order is preserved; content edits do not move tags automatically.");
        if(!packDest_.empty())
            theme::hint("Saves the bank into this pack. Mods > Deploy merges its changed strings with other mods.");
        else if(textEditSource_!=textEditDestination_)
            theme::hint("Creates a copy of the loaded bank in this folder. Restore removes the created bank.");
        theme::hint(textEditAdding_?
            "Copies donor metadata, with your chosen fields and tags. The original bank is backed up once.":
            "Keeps archive identity and any tags you leave unchanged. The original bank is backed up once.");
        if(!textEditConfirm_) {
            if(theme::ghostButton("Save text",ImVec2(cardInner,0))) textEditConfirm_=true;
            auto_.registerWidget("btn_text_save");
            if(theme::ghostButton("Cancel edit",ImVec2(cardInner,0))) textEditing_=false;
            auto_.registerWidget("btn_text_edit_cancel");
        } else {
            const int answer=confirmRow("Save this string to the destination bank?","Save",cardInner,S(28),"btn_text_save_confirm");
            if(answer>0) saveTextEdit();
            if(answer) textEditConfirm_=false;
        }
    }
    if(!textEditError_.empty()) {ImGui::TextWrapped("%s",textEditError_.c_str());auto_.registerWidget("text_edit_error");}
    if(!textEditSuccess_.empty()) ImGui::TextWrapped("%s",textEditSuccess_.c_str());
    theme::endCard();
}

void App::drawTextViewport(const ImVec2&,const ImVec2&) {
    ImGui::SetCursorPos(ImVec2(S(20),S(20)));
    if(textEditing_) {
        theme::label(textEditAdding_?"New string content":textEditName_.c_str());
        if(ImGui::InputTextMultiline("##texteditcontent",textEditContent_.data(),textEditContent_.size(),
                                    ImGui::GetContentRegionAvail(),ImGuiInputTextFlags_WordWrap)) textEditConfirm_=false;
        auto_.registerWidget("input_text_edit_content");return;
    }
    if(!textSelected_) {theme::hint("Choose a text entry to read its content.");return;}
    auto& row=textRows_[*textSelected_];theme::label(row.name.c_str());
    if(!row.error.empty()) {ImGui::TextWrapped("%s",row.error.c_str());auto_.registerWidget("text_entry_error");return;}
    if(row.type==0) {
        ImGui::InputTextMultiline("##textcontent",row.value.content.data(),row.value.content.size()+1,
                                 ImGui::GetContentRegionAvail(),ImGuiInputTextFlags_ReadOnly|ImGuiInputTextFlags_WordWrap);
        auto_.registerWidget("text_content");
    } else if(row.type==1) {
        ImGui::Text("%zu group member(s)",row.value.groupMembers.size());
        ImGui::BeginChild("##textmembers",ImGui::GetContentRegionAvail());
        ImGuiListClipper clipper;clipper.Begin(int(row.value.groupMembers.size()));
        while(clipper.Step()) for(int member=clipper.DisplayStart;member<clipper.DisplayEnd;++member) {
            const auto id=row.value.groupMembers[size_t(member)];
            const auto found=textById_.find({row.bank,id});
            const bool resolved=found!=textById_.end() && found->second.size()==1;
            const std::string label=resolved?textRows_[found->second.front()].name:"Missing or ambiguous member "+std::to_string(id);
            ImGui::PushID(member);ImGui::BeginDisabled(!resolved);
            if(ImGui::Selectable(label.c_str())) {textSelected_=found->second.front();textSelectionError_.clear();}
            auto_.registerWidget(("text_member_"+std::to_string(member)).c_str());
            ImGui::EndDisabled();ImGui::PopID();
        }
        ImGui::EndChild();
    } else if(row.type==2) {
        ImGui::Text("%zu narrator(s)",row.narrators.size());
        ImGui::BeginChild("##textnarrators",ImGui::GetContentRegionAvail());
        auto_.registerWidget("text_narrators");
        ImGuiListClipper clipper;clipper.Begin(int(row.narrators.size()));
        while(clipper.Step()) for(int i=clipper.DisplayStart;i<clipper.DisplayEnd;++i)
            ImGui::Text("%d: %s",i,row.narrators[size_t(i)].c_str());
        ImGui::EndChild();
    } else {
        theme::hint("This entry type has no text preview.");
        auto_.registerWidget("text_unsupported_entry");
    }
}
} // namespace albion::gui
