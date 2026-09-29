# World rendering performance research - 2026-09-28

The user's target is responsive, high-quality flight with continuous terrain and
water. Optimize frame-time spikes and visible transitions as well as throughput.
The current work is in `feat/editor-ui-shell`; implementation history and UI
requirements are in [HANDOFF_WORLD_UI.md](HANDOFF_WORLD_UI.md).

## Adaptive inactive GPU cache

The renderer queries DXGI's local process budget/usage once per second while
world detail is enabled. The inactive-map allowance is capped at the lesser of
128 MiB, one sixteenth of the process budget, and estimated room after reserving
headroom (10% or 64 MiB, bounded to half the budget). Estimated non-cache usage
subtracts tracked inactive payload from process usage with saturating arithmetic.
Pressure shrinks the allowance immediately and evicts oldest inactive maps;
five consecutive healthy samples allow at most 16 MiB growth. Query failure
retains the last cap and resets the recovery count. The initial fallback is
128 MiB. Active maps, overview terrain and overview water are never evicted by
this policy. It cannot guarantee the entire active working set fits in VRAM.

Tracy exposes query/trim zones and budget/usage/allowance plots. Automation can
inject telemetry without consuming memory; see `world_memory_sample` in
[AUTOMATION.md](AUTOMATION.md).

Measured in `build/profiles/world-memory-stream-overbudget`: eight real queries,
median 0.00598 ms / max 0.00791 ms; trimming four inactive maps totaling 117.1 MiB
tracked payload took 0.05691 ms. No GPU profiler diagnostics. This verifies low
overhead on the tested adapter, not a universal latency bound or FPS speedup.
Normal and zero-cache full-world routes each recorded 473 continuous frames with
no large transient empty-patch detection. Visible detail colour transitions remain,
including about 1,067 changed pixels near the Guild in the zero-cache run.

## Findings in Forge's code

Ground publication now blends the retained overview texture into detailed albedo
alongside the existing height morph. It reuses the overview allocation through
shared SRV references; no additional upload or draw pass. Fixed-camera A/B checks
at Guild/Oakvale reduce >40/channel publication changes from 8,709/12,695 pixels
to 8/1,550 respectively, with pixel-identical fully loaded endpoints. Lighting and
geometry transitions remain. See `tools/test_world_material_transition.py` and
`build/world-material-fixed-{before,after}`; unrestricted-size earlier captures
are not comparable. The four-region profile `world-material-blend` has no GPU
diagnostics, viewport p99 0.614 ms and editor update p99 3.246 ms. These timings
are a sanity check, not evidence of an FPS gain.

These are code observations, not measured speedup predictions:

| Current behavior | Consequence / next experiment |
| --- | --- |
| `Renderer::prepareLayer` now retains triangle indices and transforms each referenced vertex once per instance/material. Water also retains indices. | Less duplicated CPU/GPU geometry; true sharing between instances is still next. The source `foliageexport::Scene` already separates meshes from instances. |
| `makeTexture` creates one RGBA8 mip; the separate editable-terrain upload already generates mips. | World textures cannot select cheaper filtered mips. Add a full mip chain, preserve foliage alpha coverage, then share texture resources across maps. |
| Texture decode converts authored BC1/BC2 to RGBA for export/baking. | Preserve native compressed mip chains for eligible viewport textures. Keep RGBA available where editing/baking requires it. Do not recompress every frame. |
| Detailed terrain, plants and objects share one load/publication unit per LEV. Water now belongs to the persistent overview. | A small sea/filler LEV previously appeared as a square of new water. Independent overview water removes that dependency; terrain and object transitions still need intermediate LODs. |
| Terrain rows now share one persistent pool, capped at eight workers and leaving two reported CPU threads where possible. The pending task queue is bounded at twice the pool size; tiny/nested calls run inline. | Removes per-map thread creation and bounds row-bake concurrency across overview/detail/export. Outer decode workers still exist. Profile queue/join time before extending scheduling to the rest of the pipeline. |
| Mesh and texture caches serialize decoding under mutexes. | Increasing worker count alone may add contention. Profile before changing cache ownership; returned geometry pointers also need stable lifetimes during imports. |
| Overview texture and geometry uploads now have separate four-item / soft 2 ms frame slices; detailed uploads are similarly paced by batch. | Combine budgets and add visibility priority next. A batch can require multiple driver allocations and exceed the soft limit. CPU payload destruction now runs on a worker. |
| Overview tiles persist to disk; prepared full-detail maps do not. | Re-entering a discarded map repeats construction. Add a byte-budgeted CPU/GPU cache and a versioned disk cache of derived assets. |
| Detail budget counts maps, regardless of complexity. | Replace the approximation with measured CPU/GPU cost and memory pressure. One grass-heavy map can cost more than several sea maps. |
| Foliage preview decodes LOD0; distant z-sprites use full geometry. | Recover/use authored LODs where supported, otherwise generate preview-only LODs. Preserve the original editable/exported data. |

The test view at world `(3515, 815)` covers StartOakValeWest, East, adjacent
fillers and separate StartOakVale_Sea files. It is not one indivisible level.
This supports the streaming explanation for the user's square-by-square report;
it does not rule out every depth/overlap artifact elsewhere.

Implemented this pass: worker-side geometry expansion, staged hidden uploads,
complete-map publication, 250 ms detail/coarse fades, load/unload hysteresis,
generation checks and bounded overview uploads. Profiling also removed a second
quadratic map-name search from per-frame overview maintenance (settled Oakvale
median 6.584 ms to 0.165 ms). Persistent overview water is now implemented too;
shared instance geometry and real mesh LODs remain. An inactive GPU map cache
now retains up to 128 MiB / six maps, evicting least-recently-used entries. It
counts owned vertex/index and RGBA texture payloads, not driver overhead or
active working-set memory; it is not yet a device-memory-pressure controller.

For the user's report of whole patches flashing while still visible, residency
now follows eye translation rather than a horizon-sensitive ground ray. Terrain
transitions stay opaque and morph between overview-triangle and full heights;
objects/plants retain a separate dither fade. Bounds include both height ranges.
World near clipping follows ground clearance instead of a stale orbit distance
after descending from the overview. These address concrete disappearance
mechanisms; authored map seams and finer object LOD transitions remain open.

Full-world, varying-height tests subsequently reproduced a separate far-clipping
bug: free flight retained `orbitDistance * 30 + 1000` as its far plane. At the same
world camera pose, orbit distance 20 lost 127,024 covered pixels compared with
distance 8000; disabling frustum culling did not restore them. World rendering now
encloses the loaded overview/detail bounds, including hidden transition geometry,
independently of orbit distance. The per-frame bounds scan is linear in batches;
single-map views retain their previous projection. Tests cover all world-box
corners with elevated and outside-world eyes. See `build/world-far-before` and
`build/world-far-after` for paired hidden captures. This fixes a reproduced cause
of disappearance, not every possible streaming or visual-quality issue.
The same comparison exposed orbit-dependent haze: geometry restored by the clip
fix was still excessively dark at short focus distances. World haze now scales
from the enclosing world span, independently of orbit distance; map-view haze
keeps its prior behaviour. Final paired captures are in `build/world-far-fog-final`.

Further primary-source and GitHub evaluation is in
[the rendering research follow-up](WORLD_RENDERING_RESEARCH.md), including shared
geometry, object LODs, DXGI budgets and the archived Intel occlusion implementation.

### Completed-work dispatch and time-budgeted uploads

The next profile exposed finished CPU maps waiting behind the 400 ms demand timer.
Readiness now bypasses that timer; current camera demand/generation checks still
run before accepting a result. Detail uploads also use the existing 2 ms soft
budget more fully: the independent count guard is 32 instead of four. In the
four-batch capture, 246/338 upload frames stopped at four while below 2 ms. The
time-budgeted run used 1..16 batches, so the guard was not the usual limiter.
Individual driver allocations cannot be preempted; 2 ms is not a hard ceiling.

Hidden, screenshot-free, 33 ms-paced captures of the same 24 map builds:

| Measurement | Baseline | Ready dispatch | Time-budgeted uploads |
|---|---:|---:|---:|
| Median prepare-end to first upload | 393.75 ms | 48.17 ms | 49.48 ms |
| Detail upload frames | 337 | 338 | 238 |
| Start Oakvale load phase | 5.97 s | 4.07 s | 3.66 s |
| Adult Oakvale load phase | 6.54 s | 4.62 s | 3.76 s |
| Guild load phase | 6.61 s | 4.96 s | 3.78 s |
| Greatwood load phase | 5.54 s | 3.56 s | 3.00 s |
| Upload CPU p99 / maximum | 2.92 / 3.64 ms | 3.22 / 4.16 ms | 3.51 / 4.63 ms |

Total measured load-phase time fell 24.65 -> 14.20 seconds (42.4%). Prepared
geometry totals and upload counts matched; no profiler diagnostics were emitted.
These are paced loading comparisons under a competing gaming workload, NOT FPS
benchmarks. More work per upload frame is an explicit tradeoff: editor-update
p99 was 4.88 -> 5.74 ms, while its maximum was 8.74 -> 6.43 ms; do not interpret
single-run tails as a stable improvement. Evidence: `build/profiles/world-stream-`
`before`, `after`, `budget`, and `world-stream-dispatch.json`.

Overview water reuses the engine-derived mesh and merges only flat quads with
equal fade and ice values; no approximation of shores, holes or water heights.
The 400-map cold capture reduced water payload from 26.65 to 9.81 MiB (63.2%),
with water in 178 maps and about 14.70 MiB of indexed GPU buffers. Full-detail
loads no longer build/upload duplicate water. FWT2 tiles cache this mesh; keys
include LEV/WAD, STB, definitions, textures and gain. A hidden Oakvale comparison
confirms water remains visible with detail disabled. This does not eliminate
coarse terrain silhouette changes along shores when terrain detail arrives.

The 2D canvas also used repeated all-name lookups per box. Direct box placement
with pending-move overlays reduced median overview-phase editor update from
14.651 to 0.942 ms in successive cold captures. Terrain baking now shares its
STB chunk and decompressed frames between foreground/background readers;
six representative maps retained all 18 diagnostic PNG files byte-for-byte.
This reuse is scoped to one bake, without adding a persistent archive cache.

The next CPU-only pass replaced per-bake thread creation with `RowExecutor`.
Twelve concurrent producers exercise the queue bound and exact row coverage;
nesting cannot deadlock the pool, and errors drain outstanding callbacks before
escaping. OakVale_Sea_02 and StartOakValeWest's three diagnostic PNGs each remained
byte-identical against the saved pre-pool CLI. This verifies output preservation,
not a measured flight speedup. Later hidden cold routes passed, but competing
game/GPU activity prevents controlled frame-rate comparisons.

Those traces exposed up to 19.281 ms of CPU payload destruction on the main
thread. Retirement now transfers ownership to one worker (maximum measured
handoff 0.0174 ms). Backpressure prevents a growing queue of discarded meshes.

Indexed geometry reduced cumulative prepared vertex/index bytes by 50.39% across
the 15-map profiling route (204.7 MiB versus 412.6 MiB expanded equivalent).
This is not total/peak memory; texture sharing and true instancing are still open.
World-tab layout loading now bulk-reads STB records and indexes placement/owner
lookups. Its scripted step fell from 229.754 ms to 8.193 ms in follow-up traces;
all 401 map-list and 341 region-list lines match the old CLI byte-for-byte.
Tile keys use Windows file timestamps consistently across the normal/profiling
toolchains so switching builds no longer invalidates otherwise reusable tiles.

Black sea/filler ground had a separate cause: uncovered STB background pixels
were sampled as black. They now use the existing LEV theme fallback while
preserving covered pixels; the tile cache revision invalidates old black tiles.
The read-only actual-data regression checks 192,512 uncovered pixels against
the LEV fallback. Empty space outside map geometry is still the dark backdrop.

## Useful GitHub projects

Inspected upstream sources on 2026-09-28. Tracy is integrated; the other projects
below are evaluated candidates or references, not dependencies already added.

| Project | Fit for Forge | Decision |
| --- | --- | --- |
| [Tracy](https://github.com/wolfpld/tracy) (BSD-3-Clause) | CPU zones, worker timelines, D3D11 GPU timing and plots. | Integrated as an opt-in local, on-demand profiling build. See [PROFILING.md](PROFILING.md). |
| [meshoptimizer](https://github.com/zeux/meshoptimizer) (MIT) | Indexed geometry optimization, attribute-aware simplification and compact derived geometry. C++/CMake integration. | Strong candidate after replacing expanded triangle batches with shared indexed meshes. Start with conventional LODs; mesh shaders are not available in the current D3D11 renderer. |
| [DirectXTex](https://github.com/microsoft/DirectXTex) (MIT) | Mipmap generation, DDS and BC texture processing; upstream documents MinGW support. | Strong candidate for the derived texture cache. Preserve source BC data when possible; expensive compression belongs in preprocessing. |
| [enkiTS](https://github.com/dougbinks/enkiTS) (zlib) | Small C++ task scheduler with priorities, dependencies and pinned tasks; used by Avoyd's voxel engine. | Terrain rows now use a small internal bounded executor. Consider enkiTS if the broader asset pipeline needs priorities/dependencies; reserve render/input headroom and bound queued bytes. |
| [MaskedOcclusionCulling](https://github.com/GameTechDev/MaskedOcclusionCulling) (Apache-2.0) | CPU occlusion implementation and research reference. | Evaluate only after frustum/LOD/instancing. Upstream was archived September 7, 2026; adopting it entails maintaining the integration. |
| [Niagara](https://github.com/zeux/niagara) | Vulkan renderer reference for GPU-driven visibility. | Study architecture; not a drop-in D3D11 library or a reason to rewrite Forge's UI/renderer now. |
| [Godot Voxel](https://github.com/Zylann/godot_voxel) | Real voxel terrain streaming/LOD implementation. | Useful comparison for scheduling and continuous detail, not a legacy Fable asset loader. |

## Recommended implementation order

1. **Measure first.** Capture cold derived-cache, warm process start, settled
   view, coastal movement, rapid reversal, region jump and edit invalidation.
   Record CPU self time, GPU time, p95/p99/max frame duration, >33/50 ms frames,
   resident bytes, upload bytes, cache hits and time to useful terrain/water.
   Keep screenshot readbacks out of timing routes. Current automated runs include
   an explicit 4 ms sleep and Present(0); interactive runs use Present(1).
2. **Remove main-thread bursts.** Worker-side geometry preparation, bounded
   allocation/upload batches, cancellation and visibility only after complete
   publication. Apply budgets to overview uploads, cleanup and texture uploads,
   not just detail creation. A single D3D allocation can exceed a soft time budget.
3. **Share geometry and textures.** A mesh cache keyed by source revision, mesh
   id, LOD and material; instance transforms grouped by mesh/material/LOD and
   spatial cluster. A texture cache keyed by source revision/texture/mip/colour
   interpretation, with refcounts and byte accounting. Avoid one giant material
   batch spanning the entire world, which prevents useful culling.
4. **Keep the scene continuous.** Persistent terrain and water are implemented;
   water preserves shoreline attributes, water levels, ice and map placement.
   Add intermediate terrain LODs with
   geometric error bounds, edge stitching/morphing, and smoothly filtered
   textures. Use object LOD transitions and gradual foliage density fades.
5. **Schedule what the camera will need.** Prioritize visible projected area,
   proximity and motion prediction; prefetch ahead and retain a small trailing
   cache to handle reversals. Use separate load/unload thresholds. Teleports
   publish a complete coarse view immediately, then refine in priority order.
6. **Adapt to the actual machine.** Use CPU/GPU timing windows and the OS video
   memory budget, not GPU name or installed VRAM alone. Reduce distant grass,
   object detail and mip residency before removing nearby terrain/water. Adjust
   slowly with hysteresis; keep selected/edited objects precise. If needed,
   viewport-only resolution scaling leaves UI text native and must preserve
   picking/gizmo coordinates.
7. **Occlusion only where it pays.** Start with spatial frustum tests and
   screen-size LOD; add conservative hierarchical occlusion if dense-town/hill
   traces justify its cost. Never wait synchronously for a per-object query.
   On uncertain visibility or fast camera motion, render the object.

[D3D11 instancing](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-drawindexedinstanced)
supports shared geometry with per-instance data. Device resource creation can be
concurrent, but driver support and memory bandwidth determine whether it helps;
the immediate context stays on its owning thread.
[Microsoft's threading guidance](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-create)
is the basis for that restriction, not a blanket prohibition on worker uploads.

[DXGI memory budgets](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/nf-dxgi1_4-idxgiadapter3-queryvideomemoryinfo)
are relevant because exceeding the process budget can cause paging stalls.
[Geometry clipmaps](https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-2-terrain-rendering-using-gpu-based-geometry)
provide a reference for continuous regular-grid terrain and smooth LOD boundaries;
Forge's discontinuous/movable LEV layout makes tiled geomipmapping a simpler first
experiment than committing immediately to a global clipmap.
[Occlusion research](https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-6-hardware-occlusion-queries-made-useful)
explains the CPU/GPU stalls caused by naive stop-and-wait queries.

## Quality, caches and acceptance

Keep the product UI aligned with the existing philosophy: automatic quality by
default, a small manual preference, and optional diagnostics. Do not expose
worker counts, upload bytes, cache hashes or query internals in the normal flow.
The native debug editor and EgoCore inform format/default/LOD semantics; their
UI is not a template. Profile asset previews and eventual particles with the
same infrastructure rather than starting separate diagnostic systems.

Derived caches must include source revisions for LEV, STB, textures, meshes,
definitions, gain and cache format/quality settings. Moves should update
transforms where possible, not invalidate every world mesh. Edits invalidate
affected entries. Never bake preview simplification into the user's game assets.

Initial targets to test, not achieved guarantees: a useful warm overview within
one second after the data context is ready; a 16.7 ms frame budget at 60 Hz,
with p99 and worst-frame spikes reported separately; no recurring >50 ms
streaming hitches during ordinary flight. Test weaker/integrated hardware and
high-DPI viewports as well as the current Ryzen 7 5700X3D / RX 9060 XT machine.

A voxel demo's world-generation time alone is not an equivalent benchmark for
decoding and rendering a fixed set of authored legacy assets. Compare the time
until a navigable, coherent scene appears, frame-time distribution during motion,
visible detail and memory use. Forge should amortize its fixed conversion work
through caching and use progressive refinement to reach that responsive feel.
