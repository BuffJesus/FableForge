# World rendering research follow-up — 2026-09-28

Research checked against Forge's D3D11 renderer and the latest local captures.
These are implementation candidates, not claims that the libraries are integrated.
The measured dispatch/upload fixes are in [WORLD_PERFORMANCE.md](WORLD_PERFORMANCE.md).

## What the current measurements support

The same 24-map route prepared 280.6 MiB of indexed geometry. Ready CPU results
previously waited a median 394 ms before uploading. Removing that timer dependency
and using the existing upload time budget reduced four load phases from 24.65 to
14.20 seconds at 33 ms automation pacing. GPU viewport medians in those captures
were about 0.15 ms; competing gaming and deliberate pacing make these unsuitable
for FPS claims. Loading latency and resource duplication deserve attention before
a wholesale GPU-driven renderer rewrite.

`gui/renderer.cpp::prepareLayer` currently transforms and copies source vertices
for each instance, then merges by image/cutout material. It already reduces draw
calls through material merging. A switch to per-mesh instancing could increase
draw calls while reducing CPU work, transfer size and GPU memory. Measure both.

## Ranked candidates

| Priority | Candidate | Proposed use in Forge | Integration decision |
|---|---|---|---|
| 1 | Shared meshes and D3D11 instancing | Repeated trees, grass and placed props retain one mesh plus per-instance transforms | Implement in the current renderer; study a small example, do not replace the engine |
| 2 | Persistent prepared-asset cache | Cache reusable mesh/material/texture preparation independently of map residency | Extend derived caches, preserve source installation and exporter output |
| 3 | meshoptimizer | Optimize index/fetch order and generate error-bounded object LODs | Strong library candidate; pin a reviewed revision and benchmark a prototype |
| 4 | DXGI process memory budgets | Adjust inactive cache and optional detail to available graphics memory | Add telemetry first, then controlled eviction with headroom |
| 5 | Visibility/motion-aware requests | Prioritize the view and upcoming travel path while retaining nearby fallback | Adapt scheduling ideas with hysteresis and cancellation |
| Later | Occlusion culling / meshlets | Dense occluded scenes after profiling shows a net benefit | Experimental; require missing-geometry regression tests |

## Shared geometry and LODs

D3D11 provides indexed instanced drawing directly. The
[Microsoft API documentation](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-drawindexedinstanced)
and [bgfx's instancing example](https://raw.githubusercontent.com/bkaradzic/bgfx/master/examples/05-instancing/instancing.cpp)
are useful references for geometry plus instance-data separation. Forge's terrain
morph stays on its existing path. Start with repeated foliage/props; retain map
tags, independent visibility/fades, picking identity and correct mirrored transforms.
Use stable source asset identity across maps, not display names alone.

[meshoptimizer](https://github.com/zeux/meshoptimizer) supplies cache/fetch ordering,
attribute-aware simplification, error-based LOD selection and geometry compression.
It is [MIT licensed](https://github.com/zeux/meshoptimizer/blob/master/LICENSE.md).
The README includes screen-space error selection and cautions that simplification
can stop above the requested triangle count when quality/topology constraints bind.
For Forge, generate LODs once per unique object on a worker or into a derived cache,
preserve UV/normal/material boundaries, and select by projected error with hysteresis.
Do not simplify world water/terrain borders indiscriminately. Leave skinning and
alpha foliage on conservative policies until specific assets pass visual checks.
Pin a reviewed release/commit at implementation time; research has not added a dependency.

[DirectXMesh](https://github.com/microsoft/DirectXMesh) is another MIT geometry
processing reference, including normals, adjacency, cache optimization and meshlets.
Its documented May 2026 release changed OptimizeFaces signatures. It is useful
for content validation, but adding two overlapping mesh-processing dependencies
should require a concrete missing capability.

Prepared-cache keys should include source identity/revision, parser and preparation
version, axis/normal conventions and LOD/material settings. Keep original geometry
for editing/export; derived LODs belong to preview/render data. Record unique versus
instanced bytes before promising a particular memory reduction.

## Adapting to the computer and camera

[DXGI QueryVideoMemoryInfo](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/nf-dxgi1_4-idxgiadapter3-queryvideomemoryinfo)
reports process usage and budget. Microsoft documents paging/stutter risks when
usage exceeds the assigned budget. Forge now samples DXGI once per second and
shrinks the inactive-map allowance immediately under pressure, recovering slowly.
The 128 MiB maximum still measures owned payload, not total device residency.
Overview and active maps remain resident; this is not a controller for the active
working set. Failed queries retain the last bounded allowance. GPU capacity and
frame-time pressure are separate signals; CPU core count alone is not a quality score.

[Cesium's tileset documentation](https://cesium.com/learn/cesiumjs/ref-doc/Cesium3DTileset.html)
provides useful streaming precedents: progressive resolution, view-center priority,
flight-destination preload and avoiding requests likely to be obsolete during motion.
These are scheduling references, not a proposal to import a browser/globe renderer.
For free flight, predict a short velocity-based corridor with a bounded horizon;
camera stops/reversals must cancel low-priority work. Never evict visible fallback
just because the predicted direction changed. Test 180-degree turns, teleports,
near-boundary oscillation and vertical flight, not only steady forward travel.

## Threads, culling and larger engines

[Microsoft's D3D11 threading guidance](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-intro)
distinguishes thread-safe device resource creation from single-threaded device
contexts. Upload workers are a possible experiment, but moving calls that use the
shared immediate context onto workers would be incorrect. Driver support affects
whether deferred contexts help. Audit `appendPreparedBatch`, texture creation and
mipmap generation first; compare total CPU time and upload tails before adopting it.

[Intel MaskedOcclusionCulling](https://github.com/GameTechDev/MaskedOcclusionCulling)
is a useful CPU occlusion implementation, but its owner archived it on September 7,
2026 and states it no longer provides maintenance. Treat it as a reference or a
deliberately owned dependency. Do not let coarse terrain, transparent water or
foliage become overly aggressive occluders. Camera cuts invalidate temporal
visibility assumptions; any uncertain result must remain visible.

[Wicked Engine](https://github.com/turanszkij/WickedEngine) is a broader architecture
reference. Native mesh/amplification shaders are not exposed by D3D11, as the
[DirectXTK documentation](https://github.com/microsoft/DirectXTK/wiki/) explains.
Meshlet demos therefore do not establish a drop-in speedup for this renderer.
Ordinary instancing and LODs can be evaluated without changing graphics APIs.

For UI diagnosis, [ImGui's Debug Tools](https://github.com/ocornut/imgui/wiki/Debug-Tools)
provide internal metrics and inspection facilities. Keep these developer-facing;
the product's normal UI remains compact and follows the documented design philosophy.
This is distinct from researching FableWin editor behaviour or EgoCore formats.

## Acceptance experiments

1. Count unique source meshes, repeated instances, texture reuse, submitted draws
   and actual GPU memory; compare with current material merging.
2. Prototype shared foliage geometry in a scratch render path. Require visual
   parity at full detail, reduced duplication and no material/picking regressions.
3. Prototype three object LODs with bounded projected error. Verify silhouettes,
   alpha edges and rapid boundary crossings; retain the original until the next LOD
   is completely uploaded.
4. Evaluate memory pressure with telemetry and a deliberately small test budget,
   then repeat long flights/reversals to detect cache churn.
5. Keep screenshot-based coverage checks separate from screenshot-free timing
   captures. Record p95/p99 and worst frames, readiness latency and resident bytes.
   Reproduce on weaker hardware before claiming broadly adaptive performance.

Voxel-world generation timings are not directly comparable to Forge's archive
decoding, authored mesh/texture preparation and GPU uploads. The measured timer
and batching wins show there is useful headroom here without discarding fidelity.
