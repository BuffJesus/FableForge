#include "app.hpp"
#include "theme.hpp"
#include "effectlightpreview.hpp"
#include <cctype>

namespace albion::gui {
namespace {
constexpr float effectSpeeds[]={.25f,.5f,1.0f,2.0f};
bool containsInsensitive(const std::string& text, const std::string& needle) {
    return std::search(text.begin(),text.end(),needle.begin(),needle.end(),[](char a,char b) {
        return std::tolower(static_cast<unsigned char>(a))==std::tolower(static_cast<unsigned char>(b));
    })!=text.end();
}

void colourDetails(const uint8_t rgba[4]) {
    ImGui::ColorButton("##colour",ImVec4(rgba[0]/255.f,rgba[1]/255.f,rgba[2]/255.f,rgba[3]/255.f),
        ImGuiColorEditFlags_NoTooltip|ImGuiColorEditFlags_AlphaPreviewHalf,ImVec2(theme::S(28),theme::S(20)));
    ImGui::SameLine(); ImGui::Text("RGBA  %u, %u, %u, %u",rgba[0],rgba[1],rgba[2],rgba[3]);
}
terrainexport::Image thumbnail(const terrainexport::Image& source) {
    terrainexport::Image result;
    if (!source.width || !source.height || source.rgba.size()<size_t(source.width)*source.height*4) return result;
    const float scale=std::min(1.f,96.f/float(std::max(source.width,source.height)));
    result.width=std::max(1u,uint32_t(source.width*scale)); result.height=std::max(1u,uint32_t(source.height*scale));
    result.rgba.resize(size_t(result.width)*result.height*4);
    for (uint32_t y=0;y<result.height;++y) for (uint32_t x=0;x<result.width;++x) {
        const uint32_t sx=uint32_t(uint64_t(x)*source.width/result.width),sy=uint32_t(uint64_t(y)*source.height/result.height);
        std::copy_n(&source.rgba[(size_t(sy)*source.width+sx)*4],4,&result.rgba[(size_t(y)*result.width+x)*4]);
    }
    return result;
}
}

void App::advanceEffectPlayback(double seconds) {
    if(!effectPlaying_ || !std::isfinite(seconds) || seconds<=0) return;
    // Limit a stalled frame to 0.2 s of real time, then honour the selected
    // speed and carry the remainder through a loop boundary. Simulation::advance
    // caps work per call, so split faster playback into short chunks.
    double remaining=std::min(seconds,.2)*effectSpeeds[std::clamp(effectSpeedIndex_,0,3)];
    while(remaining>1e-9) {
        const double untilEnd=double(effectDuration_)-effectSimulation_.time();
        if(untilEnd<=1e-9) {
            if(!effectLoop_) { effectPlaying_=false; return; }
            effectSimulation_.reset(effectBrowserSelection_);
            ++effectLoopCount_;
            continue;
        }
        const double step=std::min({remaining,untilEnd,.1});
        effectSimulation_.advance(step);
        remaining-=step;
        if(effectSimulation_.time()+1e-9>=double(effectDuration_)) {
            if(!effectLoop_) { effectPlaying_=false; return; }
            effectSimulation_.reset(effectBrowserSelection_);
            ++effectLoopCount_;
        }
    }
}

void App::frameEffectPreview(bool currentOnly) {
    // Sample a separate deterministic simulation so framing never advances playback.
    particlepreview::Simulation sample; sample.reset(effectBrowserSelection_);
    float lo[3]={1e20f,1e20f,1e20f},hi[3]={-1e20f,-1e20f,-1e20f}; bool any=false;
    auto include=[&](const float* pos,float radius) {
        if (!std::isfinite(radius)) return;
        for (int axis=0;axis<3;++axis) if (!std::isfinite(pos[axis])) return;
        radius=std::clamp(radius,.025f,1000.f); any=true;
        for (int axis=0;axis<3;++axis) { lo[axis]=std::min(lo[axis],pos[axis]-radius); hi[axis]=std::max(hi[axis],pos[axis]+radius); }
    };
    std::map<int32_t,float> aspect;
    for (const auto& sprite:effectBrowserSelection_.sprites) {
        const auto row=std::find_if(texRows_.begin(),texRows_.end(),[&](const auto& texture) {
            return texture.bank=="GBANK_MAIN_PC" && texture.id==uint32_t(sprite.sprite);
        });
        aspect[sprite.sprite]=row!=texRows_.end() && row->width?float(row->height)/row->width:1.f;
    }
    auto includeSimulation=[&](const particlepreview::Simulation& state) {
        for (const auto& sprite:state.sprites())
            include(sprite.position,std::max(sprite.size[0],sprite.size[1]*aspect[sprite.texture])*.6f);
        for (const auto& mesh:state.meshes())
            include(mesh.position,std::max({std::abs(mesh.size[0]),std::abs(mesh.size[1]),std::abs(mesh.size[2])})*
                effectRenderer_.meshBoundsFactor(mesh.mesh,mesh.centredOnPosition));
        if (effectShowLightVolumes_) for (const auto& light:state.lights()) include(light.position,light.radius);
    };
    if(currentOnly) includeSimulation(effectSimulation_);
    else for (int tick=0;tick<=90;++tick) {
        includeSimulation(sample);
        sample.step();
    }
    if (!any) for (const auto& sprite:effectBrowserSelection_.sprites)
        include(sprite.offset,std::max(sprite.startSize,sprite.endSize)*.6f);
    if (!any) { effectCamera_.lookAt(0,0,0,.6f,.12f,4.f); return; }
    const float x=(lo[0]+hi[0])*.5f,y=(lo[1]+hi[1])*.5f,z=(lo[2]+hi[2])*.5f;
    const float radius=.5f*std::sqrt((hi[0]-lo[0])*(hi[0]-lo[0])+(hi[1]-lo[1])*(hi[1]-lo[1])+(hi[2]-lo[2])*(hi[2]-lo[2]));
    // Ground-facing mesh effects need an elevated view; the near-horizontal
    // sprite camera otherwise shows pools and ripples almost edge-on.
    const float pitch=effectBrowserSelection_.meshes.empty()?.12f:.55f;
    effectCamera_.lookAt(x,z,-y,.6f,pitch,std::clamp(radius/std::sin(effectCamera_.fovY*.5f)*1.1f,.15f,10000.f));
}

void App::refreshEffectBrowser() {
    const std::string previous=effectBrowserSelection_.name;
    effectBrowserRows_.clear(); effectBrowserError_.clear(); effectBrowserThumbnails_.clear();
    effectBrowserSelection_={}; effectBrowserReady_=false; effectBrowserLoaded_=true;
    effectSimulation_.reset({}); effectRenderer_.clear(); effectTexturesReady_=false; effectMeshesReady_=false;
    effectTextureWarnings_.clear();
    try {
        if (!effects::openBank(installPath_,effectBrowserError_,true)) return;
        for (const auto& name : effects::entryNames(installPath_)) {
            const auto effect=effects::byName(installPath_,name);
            if (effect) effectBrowserRows_.push_back({effect->id,effect->name,effect->displayName});
        }
        if (!previous.empty()) selectEffect(previous);
    } catch (const std::exception& e) { effectBrowserError_=e.what(); }
}

bool App::selectEffect(const std::string& nameOrId) {
    if (!effectBrowserLoaded_) refreshEffectBrowser();
    const auto found=std::find_if(effectBrowserRows_.begin(),effectBrowserRows_.end(),[&](const auto& row) {
        return std::to_string(row.id)==nameOrId ||
            (row.name.size()==nameOrId.size() && containsInsensitive(row.name,nameOrId));
    });
    if (found==effectBrowserRows_.end()) { pushLog("effects: no effect named "+nameOrId,1); return false; }
    if (effectBrowserReady_ && effectBrowserSelection_.id==found->id && effectBrowserSelection_.name==found->name) return true;
    try {
        const auto effect=effects::byName(installPath_,found->name);
        if (!effect) return false;
        effectBrowserSelection_=*effect; effectBrowserReady_=true; effectBrowserError_.clear();
        effectBrowserThumbnails_.assign(effect->sprites.size(),{});
        effectSimulation_.reset(*effect); effectLoopCount_=0;
        effectRenderer_.clearTextures(); effectRenderer_.clearMeshes(); effectTexturesReady_=false; effectMeshesReady_=false;
        effectTextureWarnings_.clear();
        frameEffectPreview();
        return true;
    } catch (const std::exception& e) { effectBrowserError_=e.what(); return false; }
}

void App::drawEffectBrowser(float pad,float inner,float cardInner) {
    using theme::S;
    if (!effectBrowserLoaded_) refreshEffectBrowser();
    ImGui::SetCursorPosX(pad); theme::beginCard("##effectbrowser",inner);
    theme::label("Effects");
    theme::hint("Inspect particle systems and open their textures or models.");
    if (theme::ghostButton("Refresh",ImVec2(cardInner,S(28)))) refreshEffectBrowser();
    auto_.registerWidget("btn_effect_refresh");
    ImGui::SetNextItemWidth(cardInner);
    ImGui::InputTextWithHint("##effectbrowsersearch","Search effects (name, id)",effectBrowserSearch_,sizeof effectBrowserSearch_);
    auto_.registerWidget("input_effect_browser_search");
    const std::string query=effectBrowserSearch_;
    std::vector<size_t> filtered;
    for (size_t i=0;i<effectBrowserRows_.size();++i) {
        const auto& row=effectBrowserRows_[i];
        if (query.empty() || containsInsensitive(row.name,query) || containsInsensitive(row.displayName,query) ||
            std::to_string(row.id)==query) filtered.push_back(i);
    }
    effectBrowserFiltered_=filtered.size();
    ImGui::TextColored(theme::vec(theme::Faint),"%zu of %zu effects",filtered.size(),effectBrowserRows_.size());
    ImGui::BeginChild("##effectbrowserlist",ImVec2(cardInner,S(260)));
    ImGuiListClipper clipper; clipper.Begin(int(filtered.size()));
    while (clipper.Step()) for (int i=clipper.DisplayStart;i<clipper.DisplayEnd;++i) {
        const auto& row=effectBrowserRows_[filtered[size_t(i)]];
        ImGui::PushID(int(row.id));
        if (ImGui::Selectable(row.name.c_str(),effectBrowserReady_ && row.id==effectBrowserSelection_.id)) selectEffect(row.name);
        auto_.registerWidget(("effect_row_"+std::to_string(row.id)).c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s\nid %u",row.name.c_str(),row.displayName.c_str(),row.id);
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (filtered.empty()) theme::hint("No matching effects.");
    if (!effectBrowserError_.empty()) ImGui::TextWrapped("%s",effectBrowserError_.c_str());
    if (effectBrowserReady_) {
        ImGui::Separator(); ImGui::TextWrapped("%s",effectBrowserSelection_.displayName.c_str());
        ImGui::Text("id %u | %d systems",effectBrowserSelection_.id,effectBrowserSelection_.systems);
        theme::hint("Preview particle animation in the centre panel. Drag to orbit; wheel to zoom.");
    }
    theme::endCard();
}

void App::drawEffectViewport(const ImVec2& origin,const ImVec2& size) {
    using theme::S;
    ImGui::SetCursorScreenPos(origin);
    ImGui::BeginChild("##effectinspector",size,ImGuiChildFlags_AlwaysUseWindowPadding);
    if (!effectBrowserReady_) {
        theme::label("Particle effects"); theme::hint("Choose an effect from the Assets list to inspect its systems.");
        ImGui::EndChild(); return;
    }
    if (!texturesLoaded_) refreshTextures();
    const auto& effect=effectBrowserSelection_;
    ImGui::TextWrapped("%s",effect.displayName.empty()?effect.name.c_str():effect.displayName.c_str());
    ImGui::TextWrapped("%s  |  id %u  |  %d systems",effect.name.c_str(),effect.id,effect.systems);
    ImGui::Text("%zu sprite systems  |  %zu mesh systems  |  %zu lights",effect.sprites.size(),effect.meshes.size(),effect.lights.size());
    if (effect.parsedFully) theme::hint("All components were decoded. Supported sprites, meshes and light volumes play below; other components remain inspectable.");
    else ImGui::TextWrapped("Partial decode: an unsupported or incomplete component stopped parsing. Some systems or values may be missing.");
    auto_.registerWidget("effect_decode_status");
    if (!effectRendererReady_) effectRendererReady_=effectRenderer_.init(device_,context_);
    if (effectRendererReady_ && !effectTexturesReady_) {
        effectTexturesReady_=true;
        std::set<int32_t> loaded;
        for (const auto& sprite : effect.sprites) {
            if (sprite.sprite<0 || !loaded.insert(sprite.sprite).second) continue;
            const auto texture=std::find_if(texRows_.begin(),texRows_.end(),[&](const auto& row) {
                return row.id==uint32_t(sprite.sprite) && row.bank=="GBANK_MAIN_PC";
            });
            texbrowse::SpriteTexture decoded; std::string error;
            if (texture==texRows_.end()) error="Texture is missing from GBANK_MAIN_PC";
            else if (texbrowse::decodeSpriteTexture(texturesBigPath(),texture->name,decoded,error)) {
                if (!effectRenderer_.setTexture(sprite.sprite,decoded.image,decoded.frames)) error=effectRenderer_.error();
            }
            if (!error.empty()) effectTextureWarnings_.push_back("Texture "+std::to_string(sprite.sprite)+": "+error);
        }
        frameEffectPreview(); // texture metadata supplies the authored frame aspect
    }
    if (effectRendererReady_ && !effectMeshesReady_) {
        effectMeshesReady_=true;
        std::set<int32_t> loadedMeshes,loadedTextures;
        for (const auto& sprite:effect.sprites) if (sprite.sprite>=0) loadedTextures.insert(sprite.sprite);
        auto graphics=std::filesystem::path(installPath_)/"data/graphics/graphics.big";
        if (!std::filesystem::exists(graphics)) graphics=std::filesystem::path(installPath_)/"data/graphics/pc/graphics.big";
        for (const auto& mesh:effect.meshes) {
            if (mesh.mesh<=0 || !loadedMeshes.insert(mesh.mesh).second) continue;
            if (loadedMeshes.size()>64) { effectTextureWarnings_.push_back("Mesh preview resource limit reached (64 models)."); break; }
            try {
                const auto geometry=forge::meshpreview::readLod0(graphics,uint32_t(mesh.mesh));
                if (!effectRenderer_.setMesh(mesh.mesh,geometry)) {
                    effectTextureWarnings_.push_back("Mesh "+std::to_string(mesh.mesh)+": "+effectRenderer_.error());
                    continue;
                }
                if (!effectRenderer_.meshUsesAuthoredBounds(mesh.mesh))
                    effectTextureWarnings_.push_back("Mesh "+std::to_string(mesh.mesh)+": authored bounds unavailable; size and centring are estimated from geometry.");
                if (geometry.boneCount>0) effectTextureWarnings_.push_back("Mesh "+std::to_string(mesh.mesh)+": skeletal animation is not previewed.");
                for (const auto& material:geometry.materials) {
                    const int32_t id=material.diffuseTexture;
                    if (id<=0 || !loadedTextures.insert(id).second) continue;
                    const auto texture=std::find_if(texRows_.begin(),texRows_.end(),[&](const auto& row) {
                        return row.id==uint32_t(id) && row.bank=="GBANK_MAIN_PC";
                    });
                    terrainexport::Image decoded; std::string error;
                    if (texture==texRows_.end()) error="Texture is missing from GBANK_MAIN_PC";
                    else if (texbrowse::decodeTexture(texturesBigPath(),texture->name,decoded,error)) {
                        if (!effectRenderer_.setTexture(id,decoded)) error=effectRenderer_.error();
                    }
                    if (!error.empty()) effectTextureWarnings_.push_back("Mesh texture "+std::to_string(id)+": "+error);
                }
            } catch (const std::exception& error) { effectTextureWarnings_.push_back("Mesh "+std::to_string(mesh.mesh)+": "+error.what()); }
        }
        frameEffectPreview();
    }
    if (ImGui::Button(effectPlaying_?"Pause":"Play")) effectPlaying_=!effectPlaying_;
    auto_.registerWidget("btn_effect_play"); ImGui::SameLine();
    if (ImGui::Button("Restart")) {effectSimulation_.reset(effect);effectLoopCount_=0;}
    auto_.registerWidget("btn_effect_restart"); ImGui::SameLine();
    if (ImGui::Button("Step")) { effectPlaying_=false; effectSimulation_.step(); }
    auto_.registerWidget("btn_effect_step"); ImGui::SameLine();
    if (ImGui::Button("Frame effect")) frameEffectPreview();
    auto_.registerWidget("btn_effect_frame");
    ImGui::SameLine();
    if (ImGui::Button("Frame current")) frameEffectPreview(true);
    auto_.registerWidget("btn_effect_frame_current");
    ImGui::Checkbox("Loop##effect",&effectLoop_);
    auto_.registerWidget("check_effect_loop");ImGui::SameLine();
    ImGui::SetNextItemWidth(S(80));
    const bool durationChanged=ImGui::DragFloat("Duration##effect",&effectDuration_,
                                                .1f,.5f,300.f,"%.1f s");
    effectDuration_=std::clamp(effectDuration_,.5f,300.f);
    if(durationChanged && effectSimulation_.time()>effectDuration_) {
        effectPlaying_=false;
        effectSimulation_.seek(effect,effectDuration_);
    }
    auto_.registerWidget("drag_effect_duration");ImGui::SameLine();
    ImGui::SetNextItemWidth(S(80));
    const char* speeds[]={"0.25x","0.5x","1x","2x"};
    ImGui::Combo("Speed##effect",&effectSpeedIndex_,speeds,4);
    auto_.registerWidget("combo_effect_speed");
    const ImVec2 timelineSize(std::max(1.0f,ImGui::GetContentRegionAvail().x),S(20));
    const ImVec2 timelineMin=ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##effect_timeline",timelineSize);
    auto_.registerWidget("effect_timeline");
    if(ImGui::IsItemHovered()) {
        const float fraction=std::clamp((ImGui::GetMousePos().x-timelineMin.x)/timelineSize.x,0.0f,1.0f);
        ImGui::SetTooltip("Seek to %.2f s",fraction*effectDuration_);
    }
    if(ImGui::IsItemDeactivated()) {
        const float fraction=std::clamp((ImGui::GetMousePos().x-timelineMin.x)/timelineSize.x,0.0f,1.0f);
        effectPlaying_=false;
        effectSimulation_.seek(effect,double(fraction*effectDuration_));
    }
    const ImVec2 timelineMax(timelineMin.x+timelineSize.x,timelineMin.y+timelineSize.y);
    const float progress=std::clamp(float(effectSimulation_.time())/effectDuration_,0.0f,1.0f);
    auto* timelineDraw=ImGui::GetWindowDrawList();
    timelineDraw->AddRectFilled(timelineMin,timelineMax,IM_COL32(34,37,50,255),S(3));
    timelineDraw->AddRectFilled(timelineMin,ImVec2(timelineMin.x+timelineSize.x*progress,timelineMax.y),
                                IM_COL32(116,83,220,255),S(3));
    timelineDraw->AddLine(ImVec2(timelineMin.x+timelineSize.x*progress,timelineMin.y),
                          ImVec2(timelineMin.x+timelineSize.x*progress,timelineMax.y),
                          IM_COL32(220,208,255,255),S(2));
    if (!effect.lights.empty()) {
        ImGui::Checkbox("Light volumes",&effectShowLightVolumes_);
        auto_.registerWidget("check_effect_light_volumes");
        ImGui::SameLine(); ImGui::Text("%zu active lights",effectSimulation_.lights().size());
    }
    ImGui::SetNextItemWidth(theme::S(220));
    ImGui::ColorEdit3("Background",effectBackground_,ImGuiColorEditFlags_NoAlpha);
    auto_.registerWidget("effect_background_color");
    if (ImGui::IsItemDeactivatedAfterEdit()) saveSettings();
    ImGui::SameLine();
    if(ImGui::Checkbox("Grid##effect",&effectShowGrid_)) saveSettings();
    auto_.registerWidget("check_effect_grid");
    const struct { const char* label; const char* widget; float value[3]; } presets[] = {
        {"Dark","btn_effect_bg_dark",{.025f,.035f,.05f}},
        {"Grey","btn_effect_bg_grey",{.35f,.35f,.35f}},
        {"Light","btn_effect_bg_light",{.9f,.9f,.9f}},
    };
    for (size_t i=0;i<3;++i) {
        const auto& preset=presets[i];
        if (ImGui::SmallButton(preset.label)) {
            std::copy_n(preset.value,3,effectBackground_);
            saveSettings();
        }
        auto_.registerWidget(preset.widget);
        if (i+1<3) ImGui::SameLine();
    }
    ImGui::Text("%.2f / %.1f s | %zu particles | %zu previewed systems",
        effectSimulation_.time(),effectDuration_,effectSimulation_.particleCount(),
        effectSimulation_.supportedSystems());
    const ImVec2 previewSize(std::max(1.f,ImGui::GetContentRegionAvail().x),S(270));
    effectLightVolumesDrawn_=0;
    if (effectRendererReady_) {
        auto* image=effectRenderer_.render(int(previewSize.x),int(previewSize.y),effectCamera_,
            effectSimulation_.sprites(),effectSimulation_.meshes(),effectBackground_,effectShowGrid_);
        if (image) {
            const ImVec2 imageOrigin=ImGui::GetCursorScreenPos();
            ImGui::Image((ImTextureID)(intptr_t)image,previewSize);
            if (effectShowLightVolumes_) effectLightVolumesDrawn_=drawEffectLightVolumes(*ImGui::GetWindowDrawList(),effectCamera_,imageOrigin,previewSize,effectSimulation_.lights());
            ImGui::SetCursorScreenPos(imageOrigin);
            ImGui::InvisibleButton("##effectpreviewinput",previewSize);
            ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
            auto_.registerWidget("effect_preview_image");
            if (ImGui::IsItemHovered()) {
                const auto& io=ImGui::GetIO();
                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) effectCamera_.orbit(-io.MouseDelta.x*.008f,-io.MouseDelta.y*.008f);
                if (io.MouseWheel!=0) {
                    float focus[3]; effectCamera_.focus(focus);
                    effectCamera_.lookAt(focus[0],focus[1],focus[2],effectCamera_.yaw,effectCamera_.pitch,
                        std::clamp(effectCamera_.distance*std::exp(-io.MouseWheel*.15f),.1f,1000.f));
                }
            }
        }
    }
    if (!effectRenderer_.error().empty()) ImGui::TextWrapped("Preview: %s",effectRenderer_.error().c_str());
    theme::hint("Drag to orbit; wheel to zoom. Unsupported behaviors remain approximate.");
    if (!effect.lights.empty()) theme::hint("Light volumes show animated colour and radius. They do not illuminate scene geometry.");
    if (!effect.meshes.empty()) theme::hint("Mesh playback supports authored XYZ orientation and fixed-axis rotation.");
    if (effectRenderer_.droppedMeshes()) ImGui::TextWrapped("%zu mesh particles omitted by preview resource limits.",effectRenderer_.droppedMeshes());
    for (const auto& warning : effectSimulation_.warnings()) ImGui::TextWrapped("%s",warning.c_str());
    for (const auto& warning : effectTextureWarnings_) ImGui::TextWrapped("%s",warning.c_str());
    ImGui::Separator();
    for (size_t i=0;i<effect.sprites.size();++i) {
        const auto& sprite=effect.sprites[i]; ImGui::PushID(int(i));
        const std::string heading="Sprite: "+sprite.system+"##sprite";
        if (ImGui::CollapsingHeader(heading.c_str(),ImGuiTreeNodeFlags_DefaultOpen)) {
            const auto texture=std::find_if(texRows_.begin(),texRows_.end(),[&](const auto& row) {
                return sprite.sprite>=0 && row.id==uint32_t(sprite.sprite) && row.bank=="GBANK_MAIN_PC";
            });
            auto& thumb=effectBrowserThumbnails_[i];
            if (!thumb.attempted) {
                thumb.attempted=true;
                if (texture!=texRows_.end()) {
                    terrainexport::Image decoded;
                    if (texbrowse::decodeTexture(texturesBigPath(),texture->name,decoded,thumb.error))
                        thumb.image=renderer_.uiTexture("effect_sprite_"+std::to_string(i),thumbnail(decoded));
                }
            }
            if (thumb.image) { ImGui::Image((ImTextureID)(intptr_t)thumb.image,ImVec2(S(64),S(64))); ImGui::SameLine(); }
            ImGui::BeginGroup();
            ImGui::Text("Texture id %d",sprite.sprite);
            ImGui::BeginDisabled(texture==texRows_.end());
            if (ImGui::SmallButton("Open texture") && texture!=texRows_.end() && selectTexture(texture->name)) assetsTab_=0;
            auto_.registerWidget(("effect_texture_"+std::to_string(i)).c_str()); ImGui::EndDisabled();
            if (texture==texRows_.end()) ImGui::TextUnformatted(sprite.sprite<0?"No sprite texture":"Texture is missing from GBANK_MAIN_PC");
            ImGui::EndGroup();
            if (!thumb.error.empty()) ImGui::TextWrapped("Thumbnail: %s",thumb.error.c_str());
            colourDetails(sprite.colour);
            ImGui::Text("Size  %.4g -> %.4g",sprite.startSize,sprite.endSize);
            ImGui::Text("Blend mode  %d%s",sprite.blendMode,sprite.blendMode==3?" (additive)":"");
            ImGui::Text("Rate  %.4g / second  |  Life  %.4g seconds",sprite.perSecond,sprite.lifeSecs);
            ImGui::Text("Offset  %.4g, %.4g, %.4g",sprite.offset[0],sprite.offset[1],sprite.offset[2]);
            ImGui::Text("Single sprite  %s",sprite.single?"Yes":"No");
            ImGui::Spacing();
        }
        ImGui::PopID();
    }
    for (size_t i=0;i<effect.meshes.size();++i) {
        const auto& mesh=effect.meshes[i]; ImGui::PushID("mesh"); ImGui::PushID(int(i));
        const std::string heading="Mesh: "+mesh.system+"##mesh";
        if (ImGui::CollapsingHeader(heading.c_str(),ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Model id %d",mesh.mesh);
            ImGui::BeginDisabled(mesh.mesh<0 || !ctx_.ready() || ctxFuture_.valid());
            if (ImGui::SmallButton("Open model") && selectModel(std::to_string(mesh.mesh))) assetsTab_=1;
            auto_.registerWidget(("effect_model_"+std::to_string(i)).c_str()); ImGui::EndDisabled();
            colourDetails(mesh.colour);
            ImGui::Text("Size  %.4g, %.4g, %.4g",mesh.size[0],mesh.size[1],mesh.size[2]);
            ImGui::Spacing();
        }
        ImGui::PopID(); ImGui::PopID();
    }
    for (size_t i=0;i<effect.lights.size();++i) {
        const auto& light=effect.lights[i]; ImGui::PushID("light"); ImGui::PushID(int(i));
        const std::string heading="Light: "+light.system+"##light";
        const bool open=ImGui::CollapsingHeader(heading.c_str(),ImGuiTreeNodeFlags_DefaultOpen);
        auto_.registerWidget(("effect_light_"+std::to_string(i)).c_str());
        if (open) {
            ImGui::Text("Enabled  %s  |  Position parameter  %u",light.enabled?"Yes":"No",light.positionParam);
            ImGui::Text("Radius  %.4g -> %.4g",light.radius,light.endRadius);
            if (light.useLife) ImGui::Text("Life  %.4g s  |  Start delay  %.4g s",light.lifeSecs,light.startTime);
            else ImGui::TextUnformatted("Continuous light (no lifetime)");
            if (light.respawns) ImGui::Text("Respawn delay  %.4g s",light.respawnDelaySecs);
            if (light.useTimeline) ImGui::Text("Timeline limit  %.4g s",light.timelineSecs);
            ImGui::Text("Radius fade  %s  |  Colour fade  %s",light.radiusFade?"Yes":"No",light.colourFade?"Yes":"No");
            if (light.useStartColour) { ImGui::PushID("start"); ImGui::TextUnformatted("Start"); colourDetails(light.colour); ImGui::PopID(); }
            if (light.useMidColour) { ImGui::PushID("mid"); ImGui::TextUnformatted("Middle"); colourDetails(light.midColour); ImGui::PopID(); }
            if (light.useEndColour) { ImGui::PushID("end"); ImGui::TextUnformatted("End"); colourDetails(light.endColour); ImGui::PopID(); }
            ImGui::Spacing();
        }
        ImGui::PopID(); ImGui::PopID();
    }
    if (effect.sprites.empty() && effect.meshes.empty() && effect.lights.empty())
        theme::hint("No active sprite, mesh or light systems were decoded for this effect.");
    ImGui::EndChild();
}
}
