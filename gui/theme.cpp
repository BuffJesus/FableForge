#include "theme.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include "imgui_internal.h"

namespace albion::gui::theme {

namespace {

ImVec4 rgb(int r, int g, int b, float a = 1.0f) { return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a); }

const ImVec4 kPalette[Count] = {
    rgb(0x0f, 0x0e, 0x14),         // Bg0  window / viewport ground
    rgb(0x16, 0x15, 0x1d),         // Bg1  panels
    rgb(0x1e, 0x1c, 0x27),         // Bg2  cards, inputs
    rgb(0x27, 0x25, 0x33),         // Bg3  hovered
    rgb(0x2a, 0x28, 0x36),         // Border
    rgb(0xe6, 0xe3, 0xf0),         // Text
    rgb(0x9b, 0x97, 0xad),         // Muted
    rgb(0x6a, 0x66, 0x7c),         // Faint
    rgb(0x8b, 0x5c, 0xf6),         // Accent
    rgb(0xa7, 0x8b, 0xfa),         // AccentHover
    rgb(0x7c, 0x3a, 0xed),         // AccentActive
    rgb(0x8b, 0x5c, 0xf6, 0.18f),  // AccentSoft
    rgb(0xc4, 0xb5, 0xfd),         // AccentText
    rgb(0x34, 0xd3, 0x99),         // Success
    rgb(0xfb, 0xbf, 0x24),         // Warn
    rgb(0xf8, 0x71, 0x71),         // Error
};

} // namespace

ImVec4 vec(Color c) { return kPalette[c]; }
ImU32 col(Color c) { return ImGui::ColorConvertFloat4ToU32(kPalette[c]); }

static float g_scale = 1.0f;
void setScale(float s) { g_scale = s; }
float scale() { return g_scale; }

void hint(const char* text) {
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    ImGui::TextColored(vec(Faint), "%s", text);
    ImGui::PopTextWrapPos();
}

void labelValue(const char* text, const char* value, float width) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::TextColored(vec(Muted), "%s", text);
    const ImVec2 vs = ImGui::CalcTextSize(value);
    ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + width - vs.x, p.y), col(Text), value);
}

void applyTheme() {
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();   // reset so re-applying at a new scale does not compound
    s.WindowRounding = 0; s.ChildRounding = 0; s.FrameRounding = 6; s.PopupRounding = 8;
    s.GrabRounding = 6; s.TabRounding = 6; s.ScrollbarRounding = 8;
    s.WindowBorderSize = 0; s.ChildBorderSize = 0; s.FrameBorderSize = 0; s.PopupBorderSize = 1;
    s.WindowPadding = ImVec2(12, 12); s.FramePadding = ImVec2(10, 6); s.ItemSpacing = ImVec2(8, 8);
    s.ItemInnerSpacing = ImVec2(6, 6); s.IndentSpacing = 14; s.ScrollbarSize = 10; s.GrabMinSize = 12;
    s.ScaleAllSizes(g_scale);
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = vec(Text);
    c[ImGuiCol_TextDisabled] = vec(Faint);
    c[ImGuiCol_WindowBg] = vec(Bg0);
    c[ImGuiCol_ChildBg] = vec(Bg1);
    c[ImGuiCol_PopupBg] = vec(Bg2);
    c[ImGuiCol_Border] = vec(Border);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = vec(Bg2);
    c[ImGuiCol_FrameBgHovered] = vec(Bg3);
    c[ImGuiCol_FrameBgActive] = vec(Bg3);
    c[ImGuiCol_TitleBg] = c[ImGuiCol_TitleBgActive] = c[ImGuiCol_TitleBgCollapsed] = vec(Bg1);
    c[ImGuiCol_MenuBarBg] = vec(Bg1);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = vec(Bg3);
    c[ImGuiCol_ScrollbarGrabHovered] = vec(Faint);
    c[ImGuiCol_ScrollbarGrabActive] = vec(Accent);
    c[ImGuiCol_CheckMark] = vec(Accent);
    c[ImGuiCol_SliderGrab] = vec(Accent);
    c[ImGuiCol_SliderGrabActive] = vec(AccentHover);
    c[ImGuiCol_Button] = vec(Bg2);
    c[ImGuiCol_ButtonHovered] = vec(Bg3);
    c[ImGuiCol_ButtonActive] = vec(AccentSoft);
    c[ImGuiCol_Header] = vec(AccentSoft);
    c[ImGuiCol_HeaderHovered] = vec(Bg3);
    c[ImGuiCol_HeaderActive] = vec(AccentSoft);
    c[ImGuiCol_Separator] = vec(Border);
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_Tab] = vec(Bg1);
    c[ImGuiCol_TabHovered] = vec(Bg3);
    c[ImGuiCol_TextSelectedBg] = vec(AccentSoft);
    c[ImGuiCol_NavCursor] = vec(Accent);
}

bool primaryButton(const char* label, const ImVec2& size, bool enabled) {
    ImGui::PushStyleColor(ImGuiCol_Button, enabled ? vec(Accent) : vec(Bg2));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, enabled ? vec(AccentHover) : vec(Bg2));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, enabled ? vec(AccentActive) : vec(Bg2));
    ImGui::PushStyleColor(ImGuiCol_Text, enabled ? ImVec4(1, 1, 1, 1) : vec(Faint));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(8.0f));
    if (!enabled) ImGui::BeginDisabled();
    const bool pressed = ImGui::Button(label, size);
    if (!enabled) ImGui::EndDisabled();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    return pressed && enabled;
}

bool ghostButton(const char* label, const ImVec2& size) {
    ImGui::PushStyleColor(ImGuiCol_Button, vec(Bg2));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, vec(Bg3));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, vec(AccentSoft));
    ImGui::PushStyleColor(ImGuiCol_Text, vec(Muted));
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return pressed;
}

bool chip(const char* label, bool active) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(12), S(5)));
    ImGui::PushStyleColor(ImGuiCol_Button, active ? vec(Accent) : ImVec4(0.12f, 0.11f, 0.16f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? vec(AccentHover) : vec(Bg3));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, vec(AccentActive));
    ImGui::PushStyleColor(ImGuiCol_Text, active ? ImVec4(1, 1, 1, 1) : vec(Muted));
    const bool pressed = ImGui::Button(label);
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);
    return pressed;
}

bool toggle(const char* label, bool* value) {
    ImGui::PushID(label);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = S(20.0f), w = S(36.0f);
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    const float rowH = std::max(h, textSize.y) + S(6);
    ImGui::InvisibleButton("##t", ImVec2(std::max(ImGui::GetContentRegionAvail().x, w + S(8) + textSize.x), rowH));
    const bool clicked = ImGui::IsItemClicked();
    if (clicked) *value = !*value;
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float y = p.y + (rowH - h) * 0.5f;
    const ImU32 track = *value ? col(Accent) : (hovered ? col(Bg3) : col(Border));
    dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + w, y + h), track, h * 0.5f);
    const float kx = *value ? p.x + w - h * 0.5f - S(2) : p.x + h * 0.5f + S(2);
    dl->AddCircleFilled(ImVec2(kx, y + h * 0.5f), h * 0.5f - S(3), IM_COL32(255, 255, 255, 255));
    dl->AddText(ImVec2(p.x + w + S(10), p.y + (rowH - textSize.y) * 0.5f), col(*value ? Text : Muted), label);
    ImGui::PopID();
    return clicked;
}

bool segmented(const char* id, int& value, std::initializer_list<const char*> options, float width) {
    ImGui::PushID(id);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const int n = int(options.size());
    const float h = S(30.0f), pad = S(2);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + width, p.y + h), col(Bg0), S(8.0f));
    bool changed = false;
    const float segW = (width - 2 * pad) / n;
    int i = 0;
    for (const char* opt : options) {
        const ImVec2 a(p.x + pad + segW * i, p.y + pad), b(a.x + segW, p.y + h - pad);
        ImGui::SetCursorScreenPos(a);
        ImGui::InvisibleButton(opt, ImVec2(segW, h - 2 * pad));
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked() && value != i) { value = i; changed = true; }
        if (value == i) dl->AddRectFilled(a, b, col(Accent), S(6.0f));
        else if (hovered) dl->AddRectFilled(a, b, col(Bg3), S(6.0f));
        const ImVec2 ts = ImGui::CalcTextSize(opt);
        dl->AddText(ImVec2(a.x + (segW - ts.x) * 0.5f, a.y + (h - 2 * pad - ts.y) * 0.5f),
                    value == i ? IM_COL32(255, 255, 255, 255) : col(Muted), opt);
        ++i;
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + S(4)));
    ImGui::Dummy(ImVec2(width, 0));
    ImGui::PopID();
    return changed;
}

void label(const char* text) {
    ImGui::TextColored(vec(Muted), "%s", text);
}

void beginCard(const char* id, float width) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, vec(Bg2));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(12), S(8)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(8), S(5)));
    ImGui::BeginChild(id, ImVec2(width, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
}

void endCard() {
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
}

} // namespace albion::gui::theme
