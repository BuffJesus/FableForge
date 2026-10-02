#include "app.hpp"
#include "dialogueaudio.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include "forge/big.hpp"
#include "forge/lut.hpp"
#include "modpack.hpp"
#include "theme.hpp"

namespace albion::gui {
namespace {

constexpr std::array<const char*,4> bankNames={
    "Dialogue.lut", "Dialogue2.lut", "ScriptDialogue.lut", "ScriptDialogue2.lut"};

const char* mouthShapeLabel(const std::string& symbol) {
    if(symbol=="AH") return "AH - open mouth";
    if(symbol=="EE") return "EE - wide mouth";
    if(symbol=="MM") return "MM - closed lips";
    if(symbol=="OH") return "OH - rounded mouth";
    if(symbol=="SZ") return "SZ - S / Z sounds";
    if(symbol=="WW") return "WW - W sound";
    return symbol.c_str();
}

} // namespace

void App::setDialogueEditing(bool editing) {
    dialogueToolsOpen_=editing;
    if(!editing) return;
    if(dialogueAudio_ && dialogueAudio_->playing()) {
        dialogueTime_=float(dialogueAudio_->position());
        dialogueAudio_->pause();
    }
    dialogueMotionPlaying_=false;
    dialogueTracksOpen_=true;
    settings_.showActions=true;
}

void App::frameDialoguePlayback() {
    if(!texturesMode_ || assetsTab_!=4) {
        if(dialogueAudio_ && (dialogueAudio_->playing() || dialogueAudio_->paused()))
            dialogueAudio_->stop();
        dialogueMotionPlaying_=false;
        return;
    }
    if(dialogueToolsOpen_ && !ImGui::IsAnyItemActive() && !ImGui::GetIO().WantTextInput &&
       !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
       ImGui::IsKeyPressed(ImGuiKey_Escape)) setDialogueEditing(false);
    if(!dialogueLoaded_) {dialogueMotionPlaying_=false;return;}
    const float duration=float(std::max(dialogueAudioDuration_,
                                        double(dialogueEntry_.duration())));
    if(dialogueMotionPlaying_) {
        dialogueTime_+=ImGui::GetIO().DeltaTime;
        if(dialogueTime_>=duration) {
            if(dialogueLoop_ && duration>0) dialogueTime_=std::fmod(dialogueTime_,duration);
            else {dialogueTime_=duration;dialogueMotionPlaying_=false;}
        }
        return;
    }
    if(!dialogueAudio_) return;
    if(dialogueAudio_->finished()) {
        dialogueAudio_->stop();
        if(dialogueLoop_) {
            if(!dialogueAudio_->play(dialogueError_)) dialogueLoop_=false;
        } else {
            std::string error;
            dialogueAudio_->seek(dialogueAudioDuration_,error);
            dialogueTime_=float(dialogueAudioDuration_);
        }
    }
    if(dialogueAudio_->playing()) dialogueTime_=float(dialogueAudio_->position());
}

void App::drawDialogueBrowser(float pad, float inner, float cardInner) {
    namespace fs=std::filesystem;
    const fs::path languageRoot=fs::path(installPath_)/"data"/"lang";
    const auto archivePath=[&] {
        return (languageRoot/dialogueLanguage_/"dialogue.big").lexically_normal();
    };
    if(dialogueToolsOpen_) drawDialogueTools(pad,inner,cardInner);
    else {
    std::vector<std::string> languages;
    std::error_code ec;
    for(fs::directory_iterator it(languageRoot,ec),end; !ec && it!=end; it.increment(ec)) {
        if(!it->is_directory(ec)) continue;
        const fs::path folder=it->path();
        if(fs::exists(folder/"dialogue.big",ec)) languages.push_back(folder.filename().string());
    }
    std::sort(languages.begin(),languages.end());

    ImGui::SetCursorPosX(pad);
    theme::beginCard("##dialoguebrowser",inner);
    theme::label("Find dialogue");
    if(languages.empty()) {
        theme::hint("No dialogue.big found under the install's data/lang folders.");
        theme::endCard();
        return;
    }
    if(std::find(languages.begin(),languages.end(),dialogueLanguage_)==languages.end()) {
        dialogueLanguage_=languages.front();
        dialogueLoaded_=false;
        dialogueSubtitles_.clear();
    }
    if(dialogueScratchLanguage_!=dialogueLanguage_) {
        dialogueScratchLanguage_=dialogueLanguage_;
        const fs::path path=fs::temp_directory_path()/"FableForge"/"exports"/
            dialogueLanguage_/"dialogue.big";
        std::snprintf(dialogueScratchPath_.data(),dialogueScratchPath_.size(),
                      "%s",path.string().c_str());
        dialogueExportMessage_.clear();
    }
    ImGui::TextUnformatted("Language");
    ImGui::SetNextItemWidth(cardInner);
    if(ImGui::BeginCombo("##dialogue_language",dialogueLanguage_.c_str())) {
        for(const auto& language:languages)
            if(ImGui::Selectable(language.c_str(),language==dialogueLanguage_)) {
                dialogueAudio_.reset();
                dialogueMotionPlaying_=false;
                dialogueLanguage_=language;
                dialogueLoaded_=false;
                dialogueError_.clear();
                dialogueSubtitles_.clear();
                dialogueScratchLanguage_.clear();
            }
        ImGui::EndCombo();
    }
    auto_.registerWidget("combo_dialogue_language");
    const auto ensureTextIndex=[&](const fs::path& folder) {
        if(dialogueTextRoot_!=folder.string()) {
            dialogueTextIndex_.reset();dialogueTextRoot_=folder.string();
            dialogueTextError_.clear();dialogueSearchCacheKey_.clear();
            dialogueSearchResults_.clear();
        }
        if(!dialogueTextIndex_ && dialogueTextError_.empty()) try {
            dialogueTextIndex_=std::make_unique<forge::dialoguetext::Index>(
                forge::dialoguetext::Index::open(folder/"text.big",
                    fs::path(installPath_)/"data"/"Defs",dialogueLanguage_));
            dialogueSearchCacheKey_.clear();
        } catch(const std::exception& ex) {
            dialogueTextError_=std::string("Subtitle lookup unavailable: ")+ex.what();
        }
        return bool(dialogueTextIndex_);
    };
    ImGui::Spacing();
    ImGui::TextUnformatted("Search all dialogue");
    ImGui::SetNextItemWidth(cardInner);
    if (focusFilter_) { ImGui::SetKeyboardFocusHere(); focusFilter_ = false; }
    if(ImGui::InputTextWithHint("##dialogue_search","Words, speaker, name or ID",
                               dialogueSearchQuery_.data(),dialogueSearchQuery_.size()))
        dialogueError_.clear();
    auto_.registerWidget("input_dialogue_search");
    bool loadRequested=false;
    const fs::path folder=languageRoot/dialogueLanguage_;
    if(ensureTextIndex(folder)) {
        const std::string cacheKey=folder.string()+"\n"+dialogueSearchQuery_.data();
        const bool searchChanged=cacheKey!=dialogueSearchCacheKey_;
        if(searchChanged) {
            dialogueSearchResults_=dialogueTextIndex_->search("",dialogueSearchQuery_.data(),
                std::numeric_limits<size_t>::max());
            dialogueSearchGroups_.clear();
            for(size_t i=0;i<dialogueSearchResults_.size();++i) {
                const auto& speaker=dialogueSearchResults_[i].line.speaker;
                dialogueSearchGroups_[speaker.empty() || speaker=="NONE" ? "Unknown speaker" : speaker].push_back(i);
            }
            dialogueSearchCacheKey_=cacheKey;
        }
        ImGui::TextWrapped("%zu line%s / %zu speaker%s",dialogueSearchResults_.size(),
            dialogueSearchResults_.size()==1?"":"s",dialogueSearchGroups_.size(),dialogueSearchGroups_.size()==1?"":"s");
        theme::hint("Expand a speaker, then choose a line.");
        ImGui::BeginChild("##dialogue_results",ImVec2(cardInner,theme::S(300)),false);
        auto_.registerWidget("tree_dialogue_lines");
            if(dialogueSearchResults_.empty()) theme::hint("No matches. Try fewer words or another speaker.");
            const float rowHeight=ImGui::GetTextLineHeightWithSpacing()*3+theme::S(10);
            size_t groupIndex=0;
            for(const auto& [speaker,indices]:dialogueSearchGroups_) {
                ImGui::PushID(speaker.c_str());
                if(searchChanged && dialogueSearchQuery_[0] && dialogueSearchResults_.size()<=100)
                    ImGui::SetNextItemOpen(true,ImGuiCond_Always);
                const bool open=ImGui::TreeNodeEx("##speaker",ImGuiTreeNodeFlags_SpanAvailWidth,
                    "%s (%zu)",speaker.c_str(),indices.size());
                auto_.registerWidget(("dialogue_group_"+std::to_string(groupIndex++)).c_str());
                if(!open) { ImGui::PopID();continue; }
            ImGuiListClipper clipper;
            clipper.Begin(int(indices.size()),rowHeight);
            while(clipper.Step()) for(int row=clipper.DisplayStart;row<clipper.DisplayEnd;++row) {
                const size_t i=indices[size_t(row)];
                const auto& hit=dialogueSearchResults_[i];
                ImGui::PushID(int(i));
                const auto pos=ImGui::GetCursorScreenPos();
                const float width=ImGui::GetContentRegionAvail().x;
                const auto current=forge::lut::dialoguePair(bankNames[size_t(dialogueBank_)],dialogueLanguage_);
                const bool selectedLine=dialogueLoaded_ && current && hit.lipsyncBank==current->lipsyncBank && hit.soundId==uint32_t(dialogueId_);
                if(ImGui::Selectable("##line",selectedLine,0,ImVec2(0,rowHeight-ImGui::GetStyle().ItemSpacing.y))) {
                    for(size_t bank=0;bank<bankNames.size();++bank) {
                        const auto pair=forge::lut::dialoguePair(bankNames[bank],dialogueLanguage_);
                        if(pair && pair->lipsyncBank==hit.lipsyncBank && hit.soundId<=uint32_t(std::numeric_limits<int>::max())) {
                            dialogueBank_=int(bank);dialogueId_=int(hit.soundId);loadRequested=true;
                            break;
                        }
                    }
                }
                auto_.registerWidget(("dialogue_search_result_"+std::to_string(i)).c_str());
                if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s\n%s | %u",hit.line.name.c_str(),hit.line.content.c_str(),hit.lipsyncBank.c_str(),hit.soundId);
                auto* draw=ImGui::GetWindowDrawList();
                draw->PushClipRect(pos,ImVec2(pos.x+width,pos.y+rowHeight-theme::S(5)),true);
                draw->AddText(ImGui::GetFont(),ImGui::GetFontSize(),pos,
                    theme::col(theme::Text),hit.line.content.c_str(),nullptr,width);
                draw->PopClipRect();
                ImGui::PopID();
            }
                ImGui::TreePop();ImGui::PopID();
            }
            ImGui::EndChild();
    } else theme::hint(dialogueTextError_.c_str());
    ImGui::Spacing();
    const bool advancedOpen=ImGui::CollapsingHeader("Look up by bank & ID");
    auto_.registerWidget("header_dialogue_lookup");
    if(advancedOpen) {
    ImGui::TextUnformatted("Bank");
    ImGui::SetNextItemWidth(cardInner);
    if(ImGui::BeginCombo("##dialogue_bank",bankNames[size_t(dialogueBank_)])) {
        for(size_t i=0;i<bankNames.size();++i)
            if(ImGui::Selectable(bankNames[i],int(i)==dialogueBank_)) {
                dialogueAudio_.reset();
                dialogueMotionPlaying_=false;
                dialogueBank_=int(i);
                dialogueLoaded_=false;
                dialogueError_.clear();
                dialogueSubtitles_.clear();
            }
        ImGui::EndCombo();
    }
    auto_.registerWidget("combo_dialogue_bank");
    ImGui::TextUnformatted("Sound ID");
    ImGui::SetNextItemWidth(cardInner);
    if(ImGui::InputInt("##dialogue_id",&dialogueId_)) {
        dialogueAudio_.reset();dialogueMotionPlaying_=false;
        dialogueLoaded_=false;dialogueSubtitles_.clear();dialogueError_.clear();
    }
    auto_.registerWidget("input_dialogue_id");
    if(ImGui::Button("Load line##dialogue")) loadRequested=true;
    auto_.registerWidget("button_dialogue_load");
    }
    if(loadRequested) {
        dialogueAudio_.reset();
        dialogueMotionPlaying_=false;
        dialogueLoaded_=false;
        dialogueError_.clear();
        dialogueSubtitles_.clear();
        dialogueTextError_.clear();
        dialogueTime_=0;
        dialogueHeadLastTime_=-1;
        dialogueAudioDuration_=0;
        try {
            if(dialogueId_<=0) throw std::runtime_error("Choose a positive Sound ID.");
            const auto pairing=forge::lut::dialoguePair(bankNames[size_t(dialogueBank_)],
                                                         dialogueLanguage_);
            if(!pairing) throw std::runtime_error("This language and bank cannot be paired.");
            const fs::path folder=languageRoot/dialogueLanguage_;
            const auto archive=forge::big::File::openFully(folder/"dialogue.big");
            const auto* bank=archive.findBank(pairing->lipsyncBank);
            if(!bank) throw std::runtime_error("Paired lip sync bank is missing.");
            const forge::big::Entry* match=nullptr;
            for(const auto& record:bank->entries) if(record.id==uint32_t(dialogueId_)) {
                if(match) throw std::runtime_error("Duplicate Sound ID in the paired bank.");
                match=&record;
            }
            if(!match || match->type!=1 || !match->length)
                throw std::runtime_error("No lip sync entry for this Sound ID in the chosen bank.");
            dialogueEntry_=forge::lipsync::decode(archive.entryData(*match),match->subHeader);
            dialogueExportMessage_.clear();
            dialogueOriginalEntry_=dialogueEntry_;
            const auto key=std::make_tuple(archivePath().string(),pairing->lipsyncBank,
                                           uint32_t(dialogueId_));
            if(const auto staged=dialogueStaged_.find(key);staged!=dialogueStaged_.end())
                dialogueEntry_=staged->second;
            if(ensureTextIndex(folder))
                if(const auto* lines=dialogueTextIndex_->find(pairing->lipsyncBank,
                                                               uint32_t(dialogueId_)))
                    dialogueSubtitles_=*lines;
            try {
                const auto audio=forge::lut::File::open(folder/pairing->lutFilename);
                if(const auto* clip=audio.find(uint32_t(dialogueId_))) {
                    auto player=std::make_unique<DialogueAudioPlayer>();
                    player->load(audio.pcm16(uint32_t(dialogueId_)),clip->sampleRate,
                                 clip->channels);
                    if(player->available()) {
                        dialogueAudioDuration_=player->duration();
                        dialogueAudio_=std::move(player);
                    } else dialogueError_="Audio clip is empty; lip sync animation is still playable.";
                } else dialogueError_="Audio unavailable for this Sound ID; lip sync animation is still playable.";
            } catch(const std::exception&) {
                dialogueError_="Audio bank unavailable; lip sync animation is still playable.";
            }
            dialogueLoaded_=true;
        } catch(const std::exception& ex) {
            dialogueError_=ex.what();
        }
    }
    if(!dialogueError_.empty()) ImGui::TextWrapped("%s",dialogueError_.c_str());
    ImGui::Dummy(ImVec2(0,theme::S(8)));
    theme::hint("Choose a line, then play it beside the character preview.");
    theme::endCard();

    }
    const auto& presets=forge::lipsync::headPresets();
    if(!dialoguePresetChecked_) {
        dialoguePresetChecked_=true;
        dialoguePresetAssets_={};
        dialoguePresetError_.clear();
        dialogueHeadReady_=false;
        dialogueHeadLastTime_=-1;
        dialogueHeadAnimations_.clear();
        renderer_.clearHeadPreview();
        try {
            fs::path graphics=fs::path(installPath_)/"data"/"graphics"/"graphics.big";
            if(!fs::exists(graphics))
                graphics=fs::path(installPath_)/"data"/"graphics"/"pc"/"graphics.big";
            const auto archive=forge::big::File::open(graphics);
            dialoguePresetAssets_=forge::lipsync::inspectHeadPreset(
                archive,presets[size_t(dialoguePreset_)]);
            if(dialoguePresetAssets_.complete()) {
                dialogueHeadGeometry_=forge::meshpreview::readLod0(
                    graphics,dialoguePresetAssets_.meshId);
                if(dialoguePresetAssets_.eyeMeshId) {
                    const auto eyes=forge::meshpreview::readLod0(
                        graphics,dialoguePresetAssets_.eyeMeshId);
                    forge::headpose::attachEyes(dialogueHeadGeometry_,eyes,
                        presets[size_t(dialoguePreset_)].eyeSides,
                        // EgoCore's approved head preview seats the retail eye
                        // mesh at native size. Applying the creature graphic's
                        // RenderSizeX here made the eyes visibly protrude.
                        1.0f);
                }
                const auto* bank=archive.findBank("MBANK_ALLMESHES");
                if(!bank) throw std::runtime_error("Head animation bank is missing.");
                const auto& preset=presets[size_t(dialoguePreset_)];
                for(size_t i=0;i<preset.tracks.size();++i) {
                    const forge::big::Entry* entry=nullptr;
                    for(const auto& candidate:bank->entries)
                        if(candidate.id==dialoguePresetAssets_.animationIds[i] &&
                           candidate.name==preset.tracks[i].animation) {
                            entry=&candidate;break;
                        }
                    if(!entry) throw std::runtime_error("Head animation disappeared from the bank.");
                    dialogueHeadAnimations_.try_emplace(preset.tracks[i].symbol,
                        forge::animation::decode(archive.entryData(*entry)));
                }
                std::vector<terrainexport::Image> images;
                std::map<uint32_t,int> textureIds;
                std::vector<std::string> warnings;
                dialogueHeadMesh_=foliageexport::makeMesh(
                    dialoguePresetAssets_.meshId,preset.mesh,preset.name,
                    dialogueHeadGeometry_,ctx_.ready() && !ctxFuture_.valid(),
                    ctx_,images,textureIds,warnings);
                dialogueHeadReady_=renderer_.setHeadPreview(dialogueHeadMesh_,images);
                if(!dialogueHeadReady_)
                    throw std::runtime_error("Head has no drawable geometry.");
                for(const auto& warning:warnings) pushLog("dialogue: "+warning,1);
            } else {
                dialoguePresetError_="Preview assets unavailable:";
                for(const auto& name:dialoguePresetAssets_.missing)
                    dialoguePresetError_+="\n"+name;
            }
        } catch(const std::exception& ex) {dialoguePresetError_=ex.what();}
    }
    if(!dialoguePresetError_.empty()) ImGui::TextWrapped("%s",dialoguePresetError_.c_str());
}

void App::drawDialogueTools(float pad,float inner,float cardInner) {
    namespace fs=std::filesystem;
    const fs::path languageRoot=fs::path(installPath_)/"data"/"lang";
    const auto archivePath=[&] { return (languageRoot/dialogueLanguage_/"dialogue.big").lexically_normal(); };
    if(dialogueLoaded_) {
        const auto pairing=forge::lut::dialoguePair(bankNames[size_t(dialogueBank_)],
                                                    dialogueLanguage_);
        const auto key=std::make_tuple(archivePath().string(),pairing->lipsyncBank,
                                       uint32_t(dialogueId_));
        const auto stage=[&] {
            dialogueStaged_[key]=dialogueEntry_;
            dialogueHeadLastTime_=-1;
            dialogueExportMessage_.clear();
        };
        ImGui::Dummy(ImVec2(0,theme::S(8)));
        ImGui::SetCursorPosX(pad);
        theme::beginCard("##dialogueeditor",inner);
        theme::label("Mouth shapes");
        theme::hint("Choose a frame, then adjust its mouth shapes.");
        const bool playing=dialogueMotionPlaying_ ||
            (dialogueAudio_ && dialogueAudio_->playing());
        if(playing) theme::hint("Pause playback to edit a frame.");
        ImGui::BeginDisabled(playing);
        if(!dialogueEntry_.frames.empty() && dialogueEntry_.fps) {
            const size_t frame=std::min(dialogueEntry_.frames.size()-1,
                size_t(std::max(0.0f,dialogueTime_)*dialogueEntry_.fps));
            ImGui::Text("Frame %zu of %zu",frame+1,dialogueEntry_.frames.size());
            const auto jump=[&](size_t target) {
                dialogueTime_=float((double(target)+0.01)/dialogueEntry_.fps);
                if(dialogueAudio_) dialogueAudio_->seek(dialogueTime_,dialogueError_);
            };
            if(ImGui::SmallButton("Previous##lipframe") && frame>0) jump(frame-1);
            auto_.registerWidget("button_dialogue_previous_frame");
            ImGui::SameLine();
            if(ImGui::SmallButton("Next##lipframe") && frame+1<dialogueEntry_.frames.size())
                jump(frame+1);
            auto_.registerWidget("button_dialogue_next_frame");
            // Frame navigation above may have changed the selected time.
            const size_t current=std::min(dialogueEntry_.frames.size()-1,
                size_t(std::max(0.0f,dialogueTime_)*dialogueEntry_.fps));
            const auto keys=dialogueEntry_.frames[current];
            for(size_t i=0;i<keys.size();++i) {
                const auto& keyWeight=keys[i];
                const auto viseme=std::find_if(dialogueEntry_.dictionary.begin(),
                    dialogueEntry_.dictionary.end(),[&](const auto& item) {
                        return item.id==keyWeight.id;
                    });
                const std::string symbol=viseme==dialogueEntry_.dictionary.end() ?
                    "Unknown" : viseme->symbol;
                ImGui::PushID(int(i));
                ImGui::TextWrapped("%s",mouthShapeLabel(symbol));
                float value=keyWeight.weight*100.f/255.f;
                const float removeWidth=ImGui::CalcTextSize("Remove").x+ImGui::GetStyle().FramePadding.x*2;
                ImGui::SetNextItemWidth(cardInner-removeWidth-ImGui::GetStyle().ItemSpacing.x);
                if(ImGui::SliderFloat("##weight",&value,0,100,"%.0f%%")) {
                    dialogueEntry_.frames[current][i].weight=uint8_t(std::lround(value*255.f/100.f));
                    stage();
                }
                auto_.registerWidget(("slider_dialogue_key_"+std::to_string(i)).c_str());
                if(ImGui::IsItemHovered()) ImGui::SetTooltip("Influence on this frame: 0%% off, 100%% full mouth shape.");
                ImGui::SameLine();
                if(ImGui::SmallButton("Remove##remove")) {
                    dialogueEntry_.frames[current].erase(
                        dialogueEntry_.frames[current].begin()+i);
                    stage();
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            if(dialogueEntry_.frames[current].size()<4) {
              if(ImGui::BeginCombo("##dialogue_add_phoneme","Add mouth shape...")) {
                static constexpr std::array<const char*,6> symbols={
                    "AH","EE","MM","OH","SZ","WW"};
                for(const char* symbol:symbols) {
                    bool present=false;
                    for(const auto& viseme:dialogueEntry_.dictionary)
                        if(viseme.symbol==symbol)
                            for(const auto& keyWeight:dialogueEntry_.frames[current])
                                present|=keyWeight.id==viseme.id;
                    if(present) continue;
                    if(ImGui::Selectable(mouthShapeLabel(symbol))) {
                        forge::lipsync::setWeight(dialogueEntry_,current,symbol,255);
                        stage();
                    }
                }
                ImGui::EndCombo();
              }
              auto_.registerWidget("combo_dialogue_add_phoneme");
            }
            if(ImGui::SmallButton("Insert after##lipframe")) {
                forge::lipsync::insertFrameAfter(dialogueEntry_,frame);
                stage();jump(frame+1);
            }
            auto_.registerWidget("button_dialogue_insert_frame");
            ImGui::SameLine();
            ImGui::BeginDisabled(dialogueEntry_.frames.size()<=1);
            if(ImGui::SmallButton("Delete##lipframe")) {
                forge::lipsync::eraseFrame(dialogueEntry_,frame);
                stage();jump(std::min(frame,dialogueEntry_.frames.size()-1));
            }
            auto_.registerWidget("button_dialogue_delete_frame");
            ImGui::EndDisabled();
        }
        if(dialogueStaged_.contains(key)) {
            if(theme::ghostButton("Reset this line",ImVec2(cardInner,theme::S(26)))) {
                dialogueEntry_=dialogueOriginalEntry_;
                dialogueHeadLastTime_=-1;
                dialogueStaged_.erase(key);
                dialogueExportMessage_.clear();
            }
            auto_.registerWidget("button_dialogue_reset_line");
        }
        ImGui::EndDisabled();
        theme::endCard();
    }

    ImGui::Dummy(ImVec2(0,theme::S(8)));
    ImGui::SetCursorPosX(pad);
    theme::beginCard("##dialogueexport",inner);
    theme::label("Save changes");
    size_t stagedCount=0;
    for(const auto& [key,_]:dialogueStaged_)
        stagedCount+=std::get<0>(key)==archivePath().string();
    ImGui::Text("%zu edited line%s in %s",stagedCount,stagedCount==1?"":"s",
                dialogueLanguage_.c_str());
    theme::hint("Choose a new file path; existing archives are never overwritten.");
    if(drawPathInput("dialogue_scratch_path", "New dialogue archive", dialogueScratchPath_.data(),
                     dialogueScratchPath_.size(),cardInner,PathField::DialogueExport,
                     "input_dialogue_scratch_path")) dialogueExportMessage_.clear();
    ImGui::BeginDisabled(!stagedCount);
    if(ImGui::Button("Export archive...##dialogue",ImVec2(cardInner,0))) {
        dialogueExportMessage_.clear();
        try {
            std::vector<forge::lipsync::ArchiveEdit> edits;
            for(const auto& [key,value]:dialogueStaged_)
                if(std::get<0>(key)==archivePath().string())
                    edits.push_back({std::get<1>(key),std::get<2>(key),value});
            const fs::path output=dialogueScratchPath_.data();
            forge::lipsync::writeScratchArchive(
                languageRoot/dialogueLanguage_/"dialogue.big",output,edits);
            dialogueExportMessage_="Wrote "+std::to_string(edits.size())+
                " edited lines to "+output.string();
        } catch(const std::exception& ex) {dialogueExportMessage_=ex.what();}
    }
    auto_.registerWidget("button_dialogue_export");
    ImGui::EndDisabled();
    if(!dialogueExportMessage_.empty())
        ImGui::TextWrapped("%s",dialogueExportMessage_.c_str());

    const auto packs=packChoices();
    const std::string packName=packDest_.empty() ? "Choose a mod pack" :
        "Mod pack: "+packLabel(packDest_);
    ImGui::SetNextItemWidth(cardInner);
    if(ImGui::BeginCombo("##dialogue_pack",packName.c_str())) {
        for(const auto& [label,folder]:packs)
            if(ImGui::Selectable(label.c_str(),folder==packDest_)) packDest_=folder;
        ImGui::EndCombo();
    }
    auto_.registerWidget("combo_dialogue_pack");
    ImGui::BeginDisabled(!stagedCount || packDest_.empty() ||
                         !modpack::isPack(packDest_));
    if(ImGui::Button("Save to pack##dialogue",ImVec2(cardInner,0)) && !fileWriteBlocked("lip sync pack")) {
        std::vector<forge::lipsync::ArchiveEdit> edits;
        for(const auto& [key,value]:dialogueStaged_)
            if(std::get<0>(key)==archivePath().string())
                edits.push_back({std::get<1>(key),std::get<2>(key),value});
        std::string error;
        if(modpack::addLipSync(packDest_,dialogueLanguage_,edits,error)) {
            std::erase_if(dialogueStaged_,[&](const auto& item) {
                return std::get<0>(item.first)==archivePath().string();
            });
            dialogueExportMessage_="Added "+std::to_string(edits.size())+
                " lip sync line(s) to pack "+packLabel(packDest_)+
                ". Mods > Deploy builds them into the game.";
        } else dialogueExportMessage_=error;
    }
    auto_.registerWidget("button_dialogue_add_pack");
    ImGui::EndDisabled();
    if(packs.empty()) theme::hint("Create a Forge pack on the Models or Ground themes page.");
    theme::endCard();

}

void App::drawDialogueViewport(const ImVec2& viewportOrigin, const ImVec2& size) {
    if(modFilesBusy()) return;
    ImGui::BeginChild("##dialogue_workspace",size,false);
    const ImVec2 origin(viewportOrigin.x,viewportOrigin.y-ImGui::GetScrollY());
    const float pad=theme::S(20),scale=theme::S(1);
    const float contentWidth=std::max(theme::S(40),ImGui::GetContentRegionAvail().x-pad*2);
    ImGui::Indent(pad);
    ImGui::SetCursorScreenPos(ImVec2(origin.x+pad,origin.y+theme::S(16)));
    ImGui::PushFont(fontBold_);
    ImGui::TextUnformatted("Dialogue");
    ImGui::PopFont();
    if(dialogueLoaded_) {
        ImGui::SameLine();
        if(ImGui::Button(dialogueToolsOpen_?"Browse dialogue":"Edit lip sync"))
            setDialogueEditing(!dialogueToolsOpen_);
        auto_.registerWidget("button_dialogue_edit");
    }
    ImGui::TextUnformatted("Preview character");
    const auto& presets=forge::lipsync::headPresets();
    ImGui::SetNextItemWidth(std::clamp(contentWidth-theme::S(82),theme::S(60),theme::S(320)));
    if(ImGui::BeginCombo("##dialogue_preset",presets[size_t(dialoguePreset_)].name.c_str())) {
        for(size_t i=0;i<presets.size();++i) {
            if(ImGui::Selectable(presets[i].name.c_str(),int(i)==dialoguePreset_)) {
                dialoguePreset_=int(i);dialoguePresetChecked_=false;dialoguePresetAssets_={};
            }
            auto_.registerWidget(("dialogue_character_"+std::to_string(i)).c_str());
        }
        ImGui::EndCombo();
    }
    auto_.registerWidget("combo_dialogue_preset");
    ImGui::SameLine();
    if(ImGui::Button("Reset view")) {
        dialogueHeadYaw_=0;dialogueHeadPitch_=0.15f;dialogueHeadZoom_=0.8f;
    }
    auto_.registerWidget("button_dialogue_reset_view");
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Drag the character to rotate. Scroll to zoom.");
    ImGui::Dummy(ImVec2(0,theme::S(5)));
    ImGui::BeginChild("##dialogue_caption",ImVec2(contentWidth,ImGui::GetTextLineHeightWithSpacing()*(size.y<theme::S(600)?2:3)),false);
    if(dialogueLoaded_ && !dialogueSubtitles_.empty()) {
        const auto& line=dialogueSubtitles_.front();
        if(!line.speaker.empty() && line.speaker!="NONE") ImGui::TextColored(theme::vec(theme::Muted),"%s",line.speaker.c_str());
        ImGui::TextWrapped("%s",line.content.c_str());
    } else if(dialogueLoaded_) ImGui::TextWrapped("Line %d - No linked subtitle",dialogueId_);
    else ImGui::TextWrapped("Choose dialogue in the panel on the right to hear a line and preview its lip sync.");
    ImGui::EndChild();
    const float duration=dialogueLoaded_ ? float(std::max(dialogueAudioDuration_,double(dialogueEntry_.duration()))) : 0;
    const float timelineHeight=dialogueLoaded_ && dialogueTracksOpen_ ? std::min(theme::S(150),size.y*.23f) : 0;
    const float controlsHeight=dialogueLoaded_ ? theme::S(contentWidth<theme::S(240)?144:112) : theme::S(36);
    const float available=origin.y+size.y-ImGui::GetCursorScreenPos().y-controlsHeight-timelineHeight-theme::S(16);
    const float side=std::min(contentWidth,std::max(theme::S(120),std::min(theme::S(400),available)));
    const ImVec2 pos(origin.x+(size.x-side)*.5f,ImGui::GetCursorScreenPos().y);
    ImDrawList* draw=ImGui::GetWindowDrawList();
    if(dialogueHeadReady_) {
        if(dialogueHeadLastTime_!=dialogueTime_) {
            const auto mouth=forge::lipsync::sample(dialogueEntry_,dialogueTime_);
            forge::headpose::AnimationMap animations;
            for(const auto& [symbol,animation]:dialogueHeadAnimations_)
                animations.emplace(symbol,&animation);
            const auto pose=forge::headpose::evaluate(dialogueHeadGeometry_,mouth,animations);
            dialogueHeadMesh_.geometry=forge::headpose::skin(dialogueHeadGeometry_,pose);
            if(renderer_.updateHeadPreview(dialogueHeadMesh_)) dialogueHeadLastTime_=dialogueTime_;
        }
        ImGui::SetCursorScreenPos(pos);
        ImGui::InvisibleButton("##dialogue_head_orbit",ImVec2(side,side),
                               ImGuiButtonFlags_MouseButtonLeft);
        auto_.registerWidget("dialogue_head_viewport");
        if(ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            dialogueHeadYaw_-=ImGui::GetIO().MouseDelta.x*0.01f;
            dialogueHeadPitch_=std::clamp(dialogueHeadPitch_+
                ImGui::GetIO().MouseDelta.y*0.01f,-1.5f,1.5f);
        }
        if(ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel!=0)
            dialogueHeadZoom_=std::clamp(dialogueHeadZoom_*
                std::exp(-ImGui::GetIO().MouseWheel*0.12f),0.5f,8.0f);
        if(ImGui::IsItemHovered() && !ImGui::IsMouseDown(0))
            ImGui::SetTooltip("Drag to rotate. Scroll to zoom.");
        if(auto* image=renderer_.headPreview(uint32_t(side),dialogueHeadYaw_,
                    dialogueHeadPitch_,dialogueHeadZoom_,dialogueHeadWire_))
            draw->AddImageRounded((ImTextureID)(intptr_t)image,pos,
                           ImVec2(pos.x+side,pos.y+side),ImVec2(0,0),ImVec2(1,1),IM_COL32_WHITE,theme::S(10));
    } else {
        ImGui::SetCursorScreenPos(pos);
        ImGui::Dummy(ImVec2(side,side));
        draw->AddText(ImVec2(pos.x,pos.y+side*.5f),theme::col(theme::Muted),"Preview unavailable");
    }
    ImGui::SetCursorScreenPos(ImVec2(origin.x+pad,pos.y+side+theme::S(6)));
    if(!dialogueLoaded_) {
        ImGui::TextColored(theme::vec(theme::Muted),"Drag to rotate / Scroll to zoom");
        ImGui::Unindent(pad);
        ImGui::EndChild();
        return;
    }
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(theme::S(6),theme::S(4)));
            const bool playing=dialogueMotionPlaying_ ||
                (dialogueAudio_ && dialogueAudio_->playing());
            if(theme::primaryButton(playing ? "Pause##dialogue" : "Play##dialogue", ImVec2(theme::S(62),0))) {
                if(playing) {
                    if(dialogueAudio_ && dialogueAudio_->playing()) {
                        dialogueAudio_->pause();
                        dialogueTime_=float(dialogueAudio_->position());
                    }
                    dialogueMotionPlaying_=false;
                } else if(dialogueAudio_ && !dialogueAudioMuted_) {
                    if(dialogueTime_>=duration) dialogueTime_=0;
                    dialogueAudio_->seek(dialogueTime_,dialogueError_);
                    if(!dialogueAudio_->play(dialogueError_)) dialogueMotionPlaying_=true;
                } else if(duration>0) { if(dialogueTime_>=duration) dialogueTime_=0; dialogueMotionPlaying_=true; }
            }
            auto_.registerWidget("button_dialogue_play_pause");
            ImGui::SameLine();
            if(ImGui::Button("Stop##dialogue")) {
                if(dialogueAudio_) dialogueAudio_->stop();
                dialogueMotionPlaying_=false;dialogueTime_=0;
            }
            auto_.registerWidget("button_dialogue_stop");
            if(contentWidth>=theme::S(240)) ImGui::SameLine();
            ImGui::Checkbox("Loop##dialogue",&dialogueLoop_);
            auto_.registerWidget("checkbox_dialogue_loop");
            if(dialogueAudio_) {
                ImGui::SameLine();
                if(ImGui::Checkbox("Mute##dialogue",&dialogueAudioMuted_)) {
                    if(dialogueAudio_->playing()) {
                        dialogueTime_=float(dialogueAudio_->position());
                        dialogueAudio_->pause();
                        dialogueMotionPlaying_=dialogueAudioMuted_;
                    } else if(!dialogueAudioMuted_ && dialogueMotionPlaying_) {
                        dialogueMotionPlaying_=false;
                        dialogueAudio_->seek(dialogueTime_,dialogueError_);
                        if(!dialogueAudio_->play(dialogueError_)) dialogueMotionPlaying_=true;
                    }
                }
                auto_.registerWidget("checkbox_dialogue_mute");
            }
            ImGui::PopStyleVar();
        }
    ImGui::SetNextItemWidth(contentWidth);
    if(ImGui::SliderFloat("##dialogue_time",&dialogueTime_,0,duration,"%.2f s") && dialogueAudio_)
        dialogueAudio_->seek(dialogueTime_,dialogueError_);
    auto_.registerWidget("slider_dialogue_time");
    ImGui::Checkbox("Show lip sync timeline",&dialogueTracksOpen_);
    auto_.registerWidget("checkbox_dialogue_timeline");
    if(dialogueTracksOpen_) {
    ImGui::BeginChild("##dialogue_tracks",ImVec2(contentWidth,timelineHeight),false);
    const ImVec2 trackOrigin=ImGui::GetCursorScreenPos();
    draw=ImGui::GetWindowDrawList();
    const float left=trackOrigin.x+theme::S(78),top=trackOrigin.y;
    const float width=std::max(1.0f,ImGui::GetContentRegionAvail().x-theme::S(84));
    const float rowHeight=theme::S(23);
    const size_t rows=dialogueEntry_.dictionary.size();
    std::array<int,256> rowForId;
    rowForId.fill(-1);
    const auto pose=forge::lipsync::sample(dialogueEntry_,dialogueTime_);
    for(size_t row=0;row<rows;++row) {
        const auto& viseme=dialogueEntry_.dictionary[row];
        rowForId[viseme.id]=int(row);
        const float y=top+row*rowHeight;
        draw->AddRectFilled(ImVec2(left,y),ImVec2(left+width,y+rowHeight-2*scale),
                            row%2 ? IM_COL32(27,29,39,255) : IM_COL32(23,25,34,255));
        char label[64];
        std::snprintf(label,sizeof label,"%.8s  %2.0f%%",viseme.symbol.c_str(),
                      row<pose.visemes.size() ? pose.visemes[row].weight*100.0f : 0.0f);
        draw->AddText(ImVec2(trackOrigin.x+theme::S(6),y+4*scale),IM_COL32(204,200,219,255),label);
    }
    if(duration>0 && rows && !dialogueEntry_.frames.empty()) {
        const int columns=std::max(1,int(width));
        for(int x=0;x<columns;++x) {
            const size_t frame=std::min(dialogueEntry_.frames.size()-1,
                size_t(double(x)/columns*duration*dialogueEntry_.fps));
            for(const auto& key:dialogueEntry_.frames[frame]) {
                const int row=rowForId[key.id];
                if(row<0 || !key.weight) continue;
                const float y=top+row*rowHeight;
                const float fill=(rowHeight-5*scale)*key.weight/255.0f;
                const ImU32 color=ImColor::HSV(float(key.id%17)/17.0f,0.65f,0.88f);
                draw->AddRectFilled(ImVec2(left+x,y+rowHeight-3*scale-fill),
                                    ImVec2(left+x+1,y+rowHeight-3*scale),color);
            }
        }
        const float marker=left+width*std::clamp(dialogueTime_/duration,0.0f,1.0f);
        draw->AddLine(ImVec2(marker,top-4*scale),
                      ImVec2(marker,top+rows*rowHeight),IM_COL32(255,255,255,255),2*scale);
        ImGui::SetCursorScreenPos(ImVec2(left,top));
        ImGui::InvisibleButton("##dialogue_large_timeline",ImVec2(width,rows*rowHeight));
        auto_.registerWidget("timeline_dialogue_large");
        auto_.registerWidget("timeline_dialogue");
        if(ImGui::IsItemActive() && ImGui::IsMouseDown(0))
            dialogueTime_=duration*std::clamp((ImGui::GetIO().MousePos.x-left)/width,0.0f,1.0f);
        if(ImGui::IsItemActive() && ImGui::IsMouseDown(0) && dialogueAudio_)
            dialogueAudio_->seek(dialogueTime_,dialogueError_);
    }
    ImGui::SetCursorScreenPos(ImVec2(trackOrigin.x,top+float(rows)*rowHeight));
    ImGui::Dummy(ImVec2(1,1));
    ImGui::EndChild();
    auto_.registerWidget("dialogue_timeline_panel");
    }
    ImGui::Unindent(pad);
    ImGui::EndChild();
}

} // namespace albion::gui
