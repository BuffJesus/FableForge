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

} // namespace

void App::frameDialoguePlayback() {
    if(!texturesMode_ || assetsTab_!=4) {
        if(dialogueAudio_ && (dialogueAudio_->playing() || dialogueAudio_->paused()))
            dialogueAudio_->stop();
        dialogueMotionPlaying_=false;
        return;
    }
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
    theme::label("Dialogue lip sync");
    ImGui::TextWrapped("Preview and edit a voiced line in its exact language and dialogue bank.");
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
    bool loadRequested=ImGui::Button("Load line##dialogue");
    auto_.registerWidget("button_dialogue_load");
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
    ImGui::TextUnformatted("Find by subtitle");
    ImGui::SetNextItemWidth(cardInner);
    if(ImGui::InputTextWithHint("##dialogue_search","Subtitle, speaker, or name",
                               dialogueSearchQuery_.data(),dialogueSearchQuery_.size()))
        dialogueError_.clear();
    auto_.registerWidget("input_dialogue_search");
    if(!dialogueSearchQuery_[0]) {
        dialogueSearchCacheKey_.clear();dialogueSearchResults_.clear();
    }
    if(dialogueSearchQuery_[0]) {
        const auto pairing=forge::lut::dialoguePair(bankNames[size_t(dialogueBank_)],
                                                     dialogueLanguage_);
        const fs::path folder=languageRoot/dialogueLanguage_;
        if(pairing && ensureTextIndex(folder)) {
            const std::string cacheKey=folder.string()+"\n"+pairing->lipsyncBank+
                "\n"+dialogueSearchQuery_.data();
            if(cacheKey!=dialogueSearchCacheKey_) {
                dialogueSearchResults_=dialogueTextIndex_->search(
                    pairing->lipsyncBank,dialogueSearchQuery_.data());
                dialogueSearchCacheKey_=cacheKey;
            }
            if(dialogueSearchResults_.empty()) theme::hint("No linked subtitles in this bank match.");
            else {
                ImGui::Text("%zu result%s%s",dialogueSearchResults_.size(),
                    dialogueSearchResults_.size()==1?"":"s",
                    dialogueSearchResults_.size()==100?" shown; refine to narrow":"");
                const float resultsHeight=theme::S(float(std::min<size_t>(155,
                    8+26*dialogueSearchResults_.size())));
                ImGui::BeginChild("##dialogue_search_results",
                                  ImVec2(cardInner,resultsHeight),true);
                for(size_t i=0;i<dialogueSearchResults_.size();++i) {
                    const auto& hit=dialogueSearchResults_[i];
                    const std::string label=std::to_string(hit.soundId)+"  "+
                        hit.line.speaker+"  "+hit.line.content+"##dialogue_result_"+
                        std::to_string(i);
                    const bool selectable=hit.soundId<=uint32_t(std::numeric_limits<int>::max());
                    ImGui::BeginDisabled(!selectable);
                    if(ImGui::Selectable(label.c_str())) {
                        dialogueId_=int(hit.soundId);loadRequested=true;
                    }
                    ImGui::EndDisabled();
                    auto_.registerWidget(("dialogue_search_result_"+
                                          std::to_string(i)).c_str());
                    if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s",
                        hit.line.name.c_str(),hit.line.content.c_str());
                }
                ImGui::EndChild();
            }
        } else if(!dialogueTextError_.empty()) theme::hint(dialogueTextError_.c_str());
    }
    if(loadRequested) {
        dialogueAudio_.reset();
        dialogueMotionPlaying_=false;
        dialogueLoaded_=false;
        dialogueError_.clear();
        dialogueSubtitles_.clear();
        dialogueTextError_.clear();
        dialogueTime_=0;
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
    if(dialogueLoaded_) {
        const double lipDuration=dialogueEntry_.duration();
        const float duration=float(std::max(dialogueAudioDuration_,lipDuration));
        ImGui::Separator();
        ImGui::Text("%zu frames at %u fps",dialogueEntry_.frames.size(),dialogueEntry_.fps);
        if(dialogueAudioDuration_>0)
            ImGui::Text("Lip sync %.3f s   Audio %.3f s",lipDuration,dialogueAudioDuration_);
        else ImGui::Text("Lip sync %.3f s   Audio unavailable",lipDuration);
        if(!dialogueTextError_.empty()) theme::hint(dialogueTextError_.c_str());
        else if(dialogueSubtitles_.empty())
            theme::hint("No linked subtitle for this Sound ID.");
        else for(const auto& line:dialogueSubtitles_) {
            if(!line.speaker.empty() && line.speaker!="NONE")
                ImGui::TextColored(theme::vec(theme::Muted),"%s",line.speaker.c_str());
            ImGui::TextWrapped("%s",line.content.c_str());
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",line.name.c_str());
        }
        {
            const bool playing=dialogueMotionPlaying_ ||
                (dialogueAudio_ && dialogueAudio_->playing());
            if(ImGui::Button(playing ? "Pause##dialogue" : "Play##dialogue")) {
                if(playing) {
                    if(dialogueAudio_ && dialogueAudio_->playing()) {
                        dialogueAudio_->pause();
                        dialogueTime_=float(dialogueAudio_->position());
                    }
                    dialogueMotionPlaying_=false;
                } else if(dialogueAudio_ && !dialogueAudioMuted_) {
                    dialogueAudio_->seek(dialogueTime_,dialogueError_);
                    if(!dialogueAudio_->play(dialogueError_)) dialogueMotionPlaying_=true;
                } else if(duration>0) dialogueMotionPlaying_=true;
            }
            auto_.registerWidget("button_dialogue_play_pause");
            ImGui::SameLine();
            if(ImGui::Button("Stop##dialogue")) {
                if(dialogueAudio_) dialogueAudio_->stop();
                dialogueMotionPlaying_=false;dialogueTime_=0;
            }
            auto_.registerWidget("button_dialogue_stop");
            ImGui::SameLine();
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
        }
        if(duration>0) {
            ImGui::SetNextItemWidth(cardInner);
            if(ImGui::SliderFloat("##dialogue_time",&dialogueTime_,0,duration,"%.3f s") &&
               dialogueAudio_) dialogueAudio_->seek(dialogueTime_,dialogueError_);
            auto_.registerWidget("slider_dialogue_time");
            const ImVec2 size(cardInner,theme::S(76));
            ImGui::InvisibleButton("##dialogue_timeline",size);
            auto_.registerWidget("timeline_dialogue");
            const ImVec2 p=ImGui::GetItemRectMin();
            ImDrawList* draw=ImGui::GetWindowDrawList();
            draw->AddRectFilled(p,ImVec2(p.x+size.x,p.y+size.y),IM_COL32(24,29,37,255));
            const size_t n=dialogueEntry_.frames.size();
            const int columns=std::max(1,int(size.x));
            for(int x=0;x<columns && n;++x) {
                const size_t frame=std::min(n-1,size_t(double(x)/columns*duration*dialogueEntry_.fps));
                const auto& keys=dialogueEntry_.frames[frame];
                const auto it=std::max_element(keys.begin(),keys.end(),
                    [](const auto& a,const auto& b){return a.weight<b.weight;});
                if(it==keys.end()) continue;
                const float height=(size.y-theme::S(12))*it->weight/255.0f;
                const ImU32 color=ImColor::HSV(float(it->id%17)/17.0f,0.65f,0.88f);
                draw->AddLine(ImVec2(p.x+x,p.y+size.y-theme::S(4)),
                              ImVec2(p.x+x,p.y+size.y-theme::S(4)-height),color);
            }
            const float marker=p.x+size.x*std::clamp(dialogueTime_/duration,0.0f,1.0f);
            draw->AddLine(ImVec2(marker,p.y),ImVec2(marker,p.y+size.y),IM_COL32(255,255,255,255),2);
            if(ImGui::IsItemActive() && ImGui::IsMouseDown(0))
                dialogueTime_=duration*std::clamp((ImGui::GetIO().MousePos.x-p.x)/size.x,0.0f,1.0f);
            if(ImGui::IsItemActive() && ImGui::IsMouseDown(0) && dialogueAudio_)
                dialogueAudio_->seek(dialogueTime_,dialogueError_);
        }
        const auto pose=forge::lipsync::sample(dialogueEntry_,dialogueTime_);
        ImGui::Text("Frame %zu   Closed mouth %.0f%%",pose.frame,pose.restWeight*100.0f);
        for(const auto& viseme:pose.visemes) if(viseme.weight>0.005f)
            ImGui::Text("%s  %.0f%%",viseme.symbol.c_str(),viseme.weight*100.0f);
    }
    theme::endCard();

    if(dialogueLoaded_) {
        const auto pairing=forge::lut::dialoguePair(bankNames[size_t(dialogueBank_)],
                                                    dialogueLanguage_);
        const auto key=std::make_tuple(archivePath().string(),pairing->lipsyncBank,
                                       uint32_t(dialogueId_));
        const auto stage=[&] {
            dialogueStaged_[key]=dialogueEntry_;
            dialogueExportMessage_.clear();
        };
        ImGui::Dummy(ImVec2(0,theme::S(8)));
        ImGui::SetCursorPosX(pad);
        theme::beginCard("##dialogueeditor",inner);
        theme::label("Edit lip sync frames");
        const bool playing=dialogueMotionPlaying_ ||
            (dialogueAudio_ && dialogueAudio_->playing());
        if(playing) theme::hint("Pause playback to edit a frame.");
        ImGui::BeginDisabled(playing);
        if(!dialogueEntry_.frames.empty() && dialogueEntry_.fps) {
            const size_t frame=std::min(dialogueEntry_.frames.size()-1,
                size_t(std::max(0.0f,dialogueTime_)*dialogueEntry_.fps));
            ImGui::Text("Frame %zu / %zu",frame,dialogueEntry_.frames.size()-1);
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
            ImGui::SameLine();
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
            // Re-read the frame after insert/delete changed the vector.
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
                int value=keyWeight.weight;
                ImGui::SetNextItemWidth(cardInner-theme::S(55));
                if(ImGui::SliderInt(symbol.c_str(),&value,0,255)) {
                    dialogueEntry_.frames[current][i].weight=uint8_t(value);
                    stage();
                }
                auto_.registerWidget(("slider_dialogue_key_"+std::to_string(i)).c_str());
                ImGui::SameLine();
                if(ImGui::SmallButton("x##remove")) {
                    dialogueEntry_.frames[current].erase(
                        dialogueEntry_.frames[current].begin()+i);
                    stage();
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            if(dialogueEntry_.frames[current].size()<4) {
              if(ImGui::BeginCombo("##dialogue_add_phoneme","Add phoneme...")) {
                static constexpr std::array<const char*,6> symbols={
                    "AH","EE","MM","OH","SZ","WW"};
                for(const char* symbol:symbols) {
                    bool present=false;
                    for(const auto& viseme:dialogueEntry_.dictionary)
                        if(viseme.symbol==symbol)
                            for(const auto& keyWeight:dialogueEntry_.frames[current])
                                present|=keyWeight.id==viseme.id;
                    if(present) continue;
                    if(ImGui::Selectable(symbol)) {
                        forge::lipsync::setWeight(dialogueEntry_,current,symbol,255);
                        stage();
                    }
                }
                ImGui::EndCombo();
              }
              auto_.registerWidget("combo_dialogue_add_phoneme");
            }
        }
        if(dialogueStaged_.contains(key)) {
            if(theme::ghostButton("Reset this line",ImVec2(cardInner,theme::S(26)))) {
                dialogueEntry_=dialogueOriginalEntry_;
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
    theme::label("Export edited dialogue");
    size_t stagedCount=0;
    for(const auto& [key,_]:dialogueStaged_)
        stagedCount+=std::get<0>(key)==archivePath().string();
    ImGui::Text("%zu staged line%s in %s",stagedCount,stagedCount==1?"":"s",
                dialogueLanguage_.c_str());
    theme::hint("Choose a new file path; existing archives are never overwritten.");
    ImGui::SetNextItemWidth(cardInner);
    if(ImGui::InputText("##dialogue_scratch_path",dialogueScratchPath_.data(),
                        dialogueScratchPath_.size())) dialogueExportMessage_.clear();
    auto_.registerWidget("input_dialogue_scratch_path");
    ImGui::BeginDisabled(!stagedCount);
    if(ImGui::Button("Export scratch dialogue.big##dialogue")) {
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
    if(ImGui::Button("Add staged lines to pack##dialogue")) {
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
            }
        } catch(const std::exception& ex) {dialoguePresetError_=ex.what();}
    }
    ImGui::Dummy(ImVec2(0,theme::S(8)));
    ImGui::SetCursorPosX(pad);
    theme::beginCard("##dialoguehead",inner);
    theme::label("Head preset assets");
    ImGui::SetNextItemWidth(cardInner);
    if(ImGui::BeginCombo("##dialogue_preset",presets[size_t(dialoguePreset_)].name.c_str())) {
        for(size_t i=0;i<presets.size();++i)
            if(ImGui::Selectable(presets[i].name.c_str(),int(i)==dialoguePreset_)) {
                dialoguePreset_=int(i);dialoguePresetChecked_=false;dialoguePresetAssets_={};
            }
        ImGui::EndCombo();
    }
    auto_.registerWidget("combo_dialogue_preset");
    if(!dialoguePresetError_.empty()) ImGui::TextWrapped("%s",dialoguePresetError_.c_str());
    else if(dialoguePresetAssets_.complete())
        ImGui::Text("Mesh %u  |  %zu phoneme poses",dialoguePresetAssets_.meshId,
                    dialoguePresetAssets_.animationIds.size());
    else for(const auto& name:dialoguePresetAssets_.missing)
        ImGui::TextWrapped("Missing: %s",name.c_str());
    theme::endCard();
}

void App::drawDialogueViewport(const ImVec2& origin, const ImVec2& size) {
    ImDrawList* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin,ImVec2(origin.x+size.x,origin.y+size.y),
                        IM_COL32(17,18,26,255));
    draw->PushClipRect(origin,ImVec2(origin.x+size.x,origin.y+size.y),true);
    const float scale=theme::S(1);
    draw->AddText(ImVec2(origin.x+24*scale,origin.y+22*scale),
                  IM_COL32(235,230,250,255),"Dialogue lip sync");
    if(!dialogueLoaded_) {
        draw->AddText(ImVec2(origin.x+24*scale,origin.y+60*scale),
                      IM_COL32(151,145,171,255),
                      "Choose a language and bank, then search or enter a Sound ID.");
        draw->PopClipRect();
        return;
    }
    const float duration=float(std::max(dialogueAudioDuration_,
                                        double(dialogueEntry_.duration())));
    char subtitle[160];
    const bool narrow=size.x<theme::S(420);
    if(narrow) std::snprintf(subtitle,sizeof subtitle,"Sound ID %d  |  %.2f s",dialogueId_,duration);
    else std::snprintf(subtitle,sizeof subtitle,"%s  |  Sound ID %d  |  %.3f s",
                       bankNames[size_t(dialogueBank_)],dialogueId_,duration);
    draw->AddText(ImVec2(origin.x+24*scale,origin.y+55*scale),
                  IM_COL32(170,166,185,255),subtitle);
    float headHeight=0;
    if(dialogueHeadReady_) {
        const ImVec2 buttonPos(origin.x+size.x-theme::S(narrow?70:109),
                               origin.y+theme::S(narrow?18:54));
        ImGui::SetCursorScreenPos(buttonPos);
        if(ImGui::SmallButton(narrow?"Reset##dialogue_head":"Reset view##dialogue_head")) {
            dialogueHeadYaw_=0;dialogueHeadPitch_=0.15f;dialogueHeadZoom_=0.8f;
        }
        auto_.registerWidget("button_dialogue_reset_view");
        if(narrow && ImGui::IsItemHovered()) ImGui::SetTooltip("Reset head view");
        if(dialogueHeadLastTime_!=dialogueTime_) {
            const auto mouth=forge::lipsync::sample(dialogueEntry_,dialogueTime_);
            forge::headpose::AnimationMap animations;
            for(const auto& [symbol,animation]:dialogueHeadAnimations_)
                animations.emplace(symbol,&animation);
            const auto pose=forge::headpose::evaluate(dialogueHeadGeometry_,mouth,animations);
            dialogueHeadMesh_.geometry=forge::headpose::skin(dialogueHeadGeometry_,pose);
            if(renderer_.updateHeadPreview(dialogueHeadMesh_)) dialogueHeadLastTime_=dialogueTime_;
        }
        const float maxSide=std::max(theme::S(8),
                                    std::min(size.x-theme::S(40),theme::S(400)));
        const float rowHeight=27*scale;
        const float reservedRows=rowHeight*float(std::min<size_t>(4,dialogueEntry_.dictionary.size()));
        const float fitSide=size.y-theme::S(172)-reservedRows;
        const float side=std::clamp(std::min(size.y*0.43f,fitSide),theme::S(8),maxSide);
        const ImVec2 pos(origin.x+(size.x-side)*0.5f,origin.y+theme::S(90));
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
        if(auto* image=renderer_.headPreview(uint32_t(side),dialogueHeadYaw_,
                    dialogueHeadPitch_,dialogueHeadZoom_,dialogueHeadWire_))
            draw->AddImage((ImTextureID)(intptr_t)image,pos,
                           ImVec2(pos.x+side,pos.y+side));
        headHeight=side+theme::S(100);
    }
    const float left=origin.x+85*scale,top=origin.y+std::max(108*scale,headHeight);
    const float width=std::max(1.0f,size.x-117*scale);
    const float rowHeight=27*scale;
    const size_t rows=std::min(dialogueEntry_.dictionary.size(),
        size_t(std::max(0,int((size.y-(top-origin.y)-72*scale)/rowHeight))));
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
        draw->AddText(ImVec2(origin.x+13*scale,y+4*scale),IM_COL32(204,200,219,255),label);
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
        if(ImGui::IsItemActive() && ImGui::IsMouseDown(0))
            dialogueTime_=duration*std::clamp((ImGui::GetIO().MousePos.x-left)/width,0.0f,1.0f);
        if(ImGui::IsItemActive() && ImGui::IsMouseDown(0) && dialogueAudio_)
            dialogueAudio_->seek(dialogueTime_,dialogueError_);
    }
    draw->AddText(ImVec2(narrow?origin.x+13*scale:left,
                         top+rows*rowHeight+12*scale),
                  IM_COL32(163,157,183,255),narrow?"Drag timeline to scrub":"Drag across the timeline to scrub viseme weights.");
    const auto& preset=forge::lipsync::headPresets()[size_t(dialoguePreset_)];
    std::string status=narrow?preset.name:"Head preset: "+preset.name;
    if(dialoguePresetAssets_.complete())
        status+=narrow?"  |  "+std::to_string(dialoguePresetAssets_.animationIds.size())+" poses"
                      :"  |  mesh "+std::to_string(dialoguePresetAssets_.meshId)+
                       ", "+std::to_string(dialoguePresetAssets_.animationIds.size())+" poses ready";
    else status+="  |  assets missing";
    draw->AddText(ImVec2(origin.x+24*scale,top+rows*rowHeight+52*scale),
                  IM_COL32(177,170,197,255),status.c_str());
    draw->PopClipRect();
}

} // namespace albion::gui
