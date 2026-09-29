#pragma once

// Compile away in normal builds. Profile builds accept a local Tracy connection
// on demand; no recorder is required to use the editor.
#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#define FORGE_ZONE(name) ZoneScopedN(name)
#define FORGE_ZONE_TEXT(text) ZoneText((text).data(), (text).size())
#define FORGE_THREAD(name) tracy::SetThreadName(name)
#define FORGE_PLOT(name, value) TracyPlot(name, double(value))
#define FORGE_FRAME() FrameMark
#define FORGE_MESSAGE(text) TracyMessage((text).data(), (text).size())
#else
#define FORGE_ZONE(name) ((void)0)
#define FORGE_ZONE_TEXT(text) ((void)0)
#define FORGE_THREAD(name) ((void)0)
#define FORGE_PLOT(name, value) ((void)0)
#define FORGE_FRAME() ((void)0)
#define FORGE_MESSAGE(text) ((void)0)
#endif
