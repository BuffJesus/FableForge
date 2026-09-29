#pragma once
#include "profile.hpp"

#ifdef TRACY_ENABLE
#include <ForgeTracyD3D11.hpp>
namespace albion::gui::profiling {
// Only the main thread's immediate context is timed. Worker zones are CPU only.
inline TracyD3D11Ctx gpu = nullptr;
}
#define FORGE_GPU_INIT(device, context) \
    albion::gui::profiling::gpu = TracyD3D11Context(device, context)
#define FORGE_GPU_ZONE(name) TracyD3D11Zone(albion::gui::profiling::gpu, name)
#define FORGE_GPU_COLLECT() TracyD3D11Collect(albion::gui::profiling::gpu)
#define FORGE_GPU_DESTROY() TracyD3D11Destroy(albion::gui::profiling::gpu)
#else
#define FORGE_GPU_INIT(device, context) ((void)0)
#define FORGE_GPU_ZONE(name) ((void)0)
#define FORGE_GPU_COLLECT() ((void)0)
#define FORGE_GPU_DESTROY() ((void)0)
#endif
