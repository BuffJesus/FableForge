#pragma once
// Dark theme with violet accents + the few custom widgets the app uses.

#include <initializer_list>
#include "imgui.h"

namespace albion::gui::theme {

enum Color {
    Bg0, Bg1, Bg2, Bg3, Border, Text, Muted, Faint,
    Accent, AccentHover, AccentActive, AccentSoft, AccentText,
    Success, Warn, Error, Count
};

ImU32 col(Color c);
ImVec4 vec(Color c);
void applyTheme();

// Widgets. All return true when activated / changed.
bool primaryButton(const char* label, const ImVec2& size, bool enabled = true);
bool ghostButton(const char* label, const ImVec2& size);
bool chip(const char* label, bool active);
bool toggle(const char* label, bool* value);
bool segmented(const char* id, int& value, std::initializer_list<const char*> options, float width);
void label(const char* text);
void beginCard(const char* id, float width);
void endCard();

} // namespace albion::gui::theme
