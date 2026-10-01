# World rendering performance research - 2026-09-28

## Derived cutout cache (2026-09-29 ultra pass)

Matched 24-map captures with the same profile executable and 33 ms pacing show
cutout-mip worker time falling from 1512.38 ms without reuse to 909.23 ms with reuse;
total map-worker time falls from 3475.14 to 2887.20 ms. Median per-map cutout work
falls from 52.77 to 0.743 ms. The serial worker retains up to 32 MiB of source and
derived pixel payload, checks exact source identity, and evicts least-recently-used
entries. Upload payloads share immutable chains rather than copying them. This
adds bounded CPU residency; it does not enlarge the GPU texture working set.

The subsequent exact-coverage search shortcut preserves chosen alpha scales and
avoids scans that cannot improve zero error. Final capture: cutout work 905.65 ms,
map preparation 2804.03 ms; that small incremental cutout change is within normal
capture variation, so no separate timing claim. Captures are
`build/profiles/ultra-cache-{off,on,search}`. No profiler diagnostics. Pixel checks
are in `build/world-cutout-cache-search`; cache and uncached/saved-baseline output
are identical at all four poses, with matching GPU texture payloads.

These are paced worker-cost comparisons, not FPS benchmarks. Cold map work still
reaches roughly 555 ms; model/texture decoding and cache misses remain. See the
current [handoff](HANDOFF_WORLD_UI.md) for lifetime/budget details and validation.

The user's target is responsive, high-quality flight with continuous terrain and
water. Optimize frame-time spikes and visible transitions as well as throughput.
The current work is in `feat/editor-ui-shell`; implementation history and UI
requirements are in [HANDOFF_WORLD_UI.md](HANDOFF_WORLD_UI.md).

## Terrain normal transitions (2026-09-29)

Detail now interpolates from the actual overview triangle's smooth vertex normal
as well as its height and colour. The CPU sampler uses the overview's a-c-b / b-c-d
triangulation and shortened edge cells, retaining the interpolated normal length.
A packed R10G10B10A2 attribute carries that normal in render axes; the shader blends
before its usual per-pixel normalization. Full detail skips the blend. Vertex
stride grows from 36 to 40 bytes (11.1% in vertex buffers; index/texture bytes are
unchanged), and tracked resource accounting includes it. No extra texture or draw
pass is added. Normal packing is bounded to half a 10-bit step per component.

Matched hidden captures in `build/world-normals-{before,after}` use the same binary,
poses, dimensions, disabled animated water and eleven held transition fractions.
Only `world_normal_blend` differs. Coarse-to-publication mean max-channel pixel
change falls from 0.68794 to 0.17283 at Guild (74.9%) and 2.42430 to 0.23765 at
Oakvale (90.2%). Full-detail endpoints are pixel-identical at both poses. These
are cropped-viewport image differences, not FPS or proof of invisible transitions.
Intermediate steps still contain object dithering and moving silhouettes.

CPU regressions cover both triangle halves, planar slopes, shortened edge cells,
empty tiles, clamping, quantization and the GPU input layout. The diagnostic is
`tools/test_world_material_transition.py --legacy-normals` for the baseline, then
`--baseline <baseline-directory>` for the enabled comparison.

Validation also passes the full 494-frame flight and five UI/resource regressions
(model browser, extended range, inactive cache, memory pressure, water). The flight
has zero sampled culling mismatch, a two-pixel maximum transient candidate, and
remaining stationary colour changes up to 243 pixels. No broad-route A/B claim.

The clang capture `build/profiles/world-normal-blend` prepares 24 maps totaling
304.9 MiB geometry (not peak residency); CPU editor-update/viewport p99/max is
4.013/5.642 ms, GPU viewport 0.927/1.204 ms, and paced upload 2.615/2.912 ms.
No profiler diagnostics. This 33 ms paced run is an overhead sanity check, not an
FPS benchmark or a matched performance comparison.

## Active detail distance and memory (2026-09-29)

Automatic detail starts at six maps and may grow to 24 by default (user ceiling
1..32). Growth still requires five seconds of sustained frame time below 1/55 s,
with no pending streaming and a focused window; two seconds above 1/45 s reduce
it. DXGI telemetry further limits the ceiling once per second: reserve the larger
of 256 MiB or 20% of the process budget, subtract other usage (including inactive
cache), and estimate each map at the larger of 64 MiB or 1.5 times the largest
observed resident map. Missing telemetry limits expansion to six. Pressure clamps
the budget immediately; normal fade-out then retires excess maps. These estimates
are conservative heuristics, not hard VRAM bounds or cross-hardware benchmarks.

The base detail distance is adjustable from 100 to 1000 metres. Automatic mode
scales it by sqrt(current map budget / 6), bounded to 1..2; the default 250 metres
can therefore reach 500. Manual mode uses the specified distance and map limit.
Neither depends on orbit focus distance. Eye position still controls residency.
Overview terrain now targets 80 quads along the long side instead of 40; the cache
key changes so old coarse tiles are not reused. This improves distant terrain
sampling; it does not add geometry to authored low-poly object meshes.

`tests/ui/world_detail_range.txt` covers 18 resident maps at 500 metres, stable
residency when focus distance changes from 200 to 8000 at the same eye, and
synthetic ample/overbudget/unavailable telemetry. CPU tests cover expansion,
pressure, missing telemetry, large map costs and user ceilings. Full-detail
objects still use the existing LOD 0 decoder. Per-object LOD transitions remain open; normal interpolation is covered above.

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


## Shared textures and opaque mipmaps (2026-09-29 marathon)

Layer GPU textures are pooled by name/dimensions/policy with exact pixel comparison.
Weak ownership releases the allocation after the final batch/coarse-albedo owner.
The 18-map fixture drops texture payload from 443.6 MiB to 227.7 MiB with identical
pixels, or 291.0 MiB with opaque mip chains enabled. These are texture payloads,
not total DXGI usage. Pixel identity consumes a retained CPU copy (227.7 MiB in the
fixture); integrated/low-memory hardware still needs direct validation. Ordinary
uncorrected cutout mipmaps remain disabled. The subsequent pass below adds
coverage-preserving partial chains for world-detail cutouts.

The memory controller credits only textures exclusively owned by the active or
inactive map set. Sharing therefore cannot create fictitious free memory by
subtracting the same allocation more than once. Cache per-map charges deliberately
remain conservative. Resource lifetime, failed allocations, replacement pixels,
sampling policies, shape mismatches and mip-byte arithmetic have CPU/UI checks.

Object coverage now eases over 0.6 s while ground morphing keeps its 0.25 s duration.
This has no extra draw passes or geometry cost. Held-time arrival comparisons and
texture/filtering evidence are in `HANDOFF_WORLD_UI.md`; remaining screen-door
noise and map streaming boundaries are not claimed fixed.

CPU identity cleanup now uses the existing deferred-release worker abstraction,
with a 64 MiB pending limit and at most one 64 MiB worker payload. Busy/oversized
overflow releases synchronously; GPU objects never enter that worker. This avoids
an unbounded RAM backlog during memory-pressure eviction. The application polls
cleanup even outside the 3D viewport. The final paced capture's map-retirement max
is 1.24 ms and editor update/viewport CPU p99/max 3.612/4.556 ms; see the handoff for
capture paths, GPU timings, observed overflow and measurement limits.

## Cutout filtering and scaled-mesh normals (continued marathon)

World-detail cutouts now prepare partial mip chains on the streaming worker.
Alpha-weighted colour reduction rejects transparent RGB; RGBA8 alpha correction
preserves measured wrapped bilinear coverage. Levels that cannot meet both a
3-percentage-point and 10%-relative coverage tolerance are omitted, retaining the
last trustworthy level. Authored base images remain unchanged. See the handoff for
the independent implementation's [coverage-scaling reference](https://github.com/microsoft/DirectXTex/wiki/ScaleMipMapsAlphaForCoverage),
controlled captures, CPU tests and limitations.

The Guild/Oakvale capture adds 12.6/13.1 MiB of GPU texture payload; temporary CPU
mips retire with the existing world-detail worker payload. No persistent CPU mip
cache was added. The 24-map paced profile records worker preparation median/max
54.5/224.4 ms per map and cutout GPU upload max 0.865 ms. This is extra loading work
off the UI thread, not free processing or a claim of lower frame time.

World baked normals and editable object shader normals use inverse-transpose
bases for non-uniform scale/shear. Normalized cofactors avoid tiny-scale overflow;
reflection direction is preserved. The per-object constant buffer adds one 4x4
normal matrix. Terrain geometry, draw counts and asset topology remain unchanged.
## World antialiasing, 2026-09-29

World-only MSAA resolves into the existing UI texture. Auto chooses up to 4x;
supported 2x and 1x targets are allocation fallbacks. Sustained eligible frame
times above 1/45 s for 1.5 s lower one tier; below 1/55 s for 10 s restore one.
The recovery tolerance allows steady 60 Hz VSync presentation to recover quality.
Memory policy limits the target to 1/16 of DXGI budget and reserves at least
256 MiB or 20% of budget, with extra upgrade headroom. Unknown telemetry uses 1x.
Target payload is width * height * (8 * samples + 4) bytes for MSAA, or 8 bytes
per pixel at 1x. Transactional allocation temporarily retains the old target.
These are resource estimates, not measured whole-process VRAM or FPS claims.
Validation and the related EgoCore particle review are in `HANDOFF_WORLD_UI.md`.

## Obsolete preparation cancellation (2026-09-29)

The serial detail worker now checks a per-job cancellation flag between decoding,
geometry preparation and individual cutout chains. Camera demand changes request
cancellation at the existing demand refresh; invalidation/view exit requests it
immediately. This avoids finishing every remaining stage for a map no longer
wanted. Current decoder/mip calls still run to completion. Disabled/2D/other-tab
frames collect finished work and retire its payload on the existing CPU worker.

Deterministic checks use a held terrain checkpoint, covering camera movement,
filters, disable/view exit and native shutdown. They prove control flow and
unchanged restored pixels/resource accounting, not a measured latency gain.
The 494-frame one-map stress flight observed one cancellation without a test hold,
zero failures and zero sampled coverage gaps. Evidence and residual foliage
transitions: `HANDOFF_WORLD_UI.md`, `build/world-flight-cancel`.

## Cutout-edge multisampling (2026-09-29)

World 2x/4x MSAA now uses alpha-to-coverage for cutout textures. Previously
multisampling smoothed triangle boundaries but the texture's 0.5 alpha test was
still binary. A derivative-width ramp retains the 0.5 contour and supplies
fractional sample coverage. Texture/alpha derivatives are evaluated before the
spatial fade discard. 1x and blend-state allocation failure retain the hard test.
No extra render targets or texture payloads are introduced.

The blend state preserves the target's cleared alpha while replacing covered
colour/depth samples, so viewport compositing does not apply coverage a second
time. Water keeps its separate existing blend state. API semantics:
[Microsoft's alpha-to-coverage documentation](https://learn.microsoft.com/en-us/windows/win32/direct3d11/d3d10-graphics-programming-guide-blend-state#alpha-to-coverage).

Initial two-scene diagnostic (`build/world-cutout-aa`) retains the unsuccessful
mean-absolute-motion gate: Guild changes from 2.16758 to 2.17020 at 4x, while
squared motion drops from 17.23357 to 12.20244 and changes above 80 levels drop
from 46 to 1.82 per frame. Oakvale squared motion drops from 23.27652 to 13.96154,
large changes from 66.18 to 4.55. Smoothing redistributes smaller changes, so the
final gate measures squared motion and abrupt changes, and still reports the
absolute mean. These are image diagnostics on a tiny camera pan, not FPS or
general perceptual quality scores. GPU texture bytes are identical; weighted
cutout silhouette coverage changes by less than 1.3% at these poses.

## Draw distance beyond near-map slots (2026-09-29)

User feedback at 250-500 metres altitude exposed a residency limit: prior mesh LOD
selection worked only in loaded near maps. Increasing radius could leave the
same six maps resident and show little difference. Draw distance now requests
100-1000 metres independently of the adaptive near-detail map count.

A separate service prepares the last usable authored object LOD for other maps
within a padded view cone. Existing mesh classes, materials and cutouts are reused;
missing chains retain base geometry. Coarse scenery and near/full detail use
complementary dither coverage. True 3D box distance drives demand, with preload
radius `drawDistance * 1.15 + 32` and 32 metres of resident range hysteresis.
The near-map residency priority bonus is capped at 45 metres. Terrain refinement
retains its own budget. This supersedes the earlier 85%-of-load-radius policy.

Outer scenery is bounded to 64 maps and up to 1 GiB of accounted resources,
also constrained by one quarter of adapter budget and headroom after reserving
the larger of 256 MiB or one fifth of adapter budget. Unknown
telemetry permits 64 MiB. Shared textures and transient uploads mean this is not
a total process VRAM limit. More preparation, geometry and draws require profiling;
no frame-rate improvement is claimed. A fixed-camera 350-metre-altitude comparison
should show farther scenery when increasing distance from 250 to 1000 without
crossing a map boundary. The first actual-slider comparison with one full-detail
slot (`build/world-distance-first`) increased scenery maps 10 -> 37 and distant
drawn parts 0 -> 827 (445 lower-LOD parts), changing 11,680 pixels and restoring
identical pixels on return. Its gate failed assumptions of zero pending demand
and exact resident-name restoration: nine maps were budget-blocked and hysteresis
retained three extra residents. Final checks instead enforce memory bounds,
pending-versus-blocked demand and restored pixels under the revised allowance.
The final actual-slider run **passes** in `build/world-distance-final`. At fixed
350-metre camera height and one near GuildExterior slot, distance 250 -> 1000
increases scenery maps 10 -> 46. The high setting draws 4,709 scenery parts and
2,897 lower-LOD parts, with 723,205,836 accounted bytes under 1 GiB and zero pending,
blocked or failed demand. The images differ at 27,613 pixels; returning to 250
restores identical pixels while hysteresis retains 13 maps.

`build/world-scenery-lifecycle-final` also passes: pressure removes all residents,
including empty payload entries; recovery, disable/re-enable, in-flight
invalidation and filter restoration preserve settled pixels. The creatures-off
fixture has 46 scenery maps, 603,647,848 bytes and 4,484 drawn parts. An optional
fully hidden coarse copy of the near map is allowed in residency comparisons.
The handoff route observes 129 frames without an aggregate CPU coverage gap and
records 30 holds of outgoing detail while its fallback becomes available. This
checks map coverage state, not pixel-level GPU continuity. Extended range passes
with synthetic 16 GiB memory telemetry. Six CTest suites and the normal near-LOD
and focused UI regressions pass. `build/world-scenery-cancel-final.log` and its
artifact directory also pass: five deterministic cancellations, clean held-worker
shutdown, zero failures, identical restored pixels and 177,118,448 texture bytes
before/after. This verifies lifecycle behavior, not cancellation latency.

Far scenery uses its coarsest usable authored level, while nearby refinement has
its own budget. Serial preparation, the 64-map/GPU caps and conservative approximate
map bounds remain limitations. No FPS or universally seamless-flight claim follows.

Subsequent review fixed MeshCache lifetime across bank invalidation with immutable
shared mesh ownership. A deterministic concurrent cache-close/reopen regression
passes, as do all six normal CTest suites (13.74 seconds).

The first constrained-memory priority run (`build/world-scenery-priority-first`)
failed to settle: priority evictions rose from zero to 448 by 136 seconds. Its
owned hidden process was stopped with evidence retained. Admission now preflights
exact aggregate reclaimability, including textures shared by the victim group,
before admitting a ready higher-priority map. The policy suite passes 232 CPU
checks and the normal GUI is rebuilt. Native `build/world-scenery-priority-final`
passes in 33.68 seconds: 34 maps/265,581,812 bytes/zero evictions before movement,
39 maps/267,818,616 bytes/two evictions afterward, with 12 blocked requests at
both poses and a 268,435,456-byte limit. Over another 240 stationary frames,
resident names, bytes, drawn parts, eviction counter and pixels remain identical.
There are no failures. Final normal-build distance control also passes in
`build/world-distance-admission-final` (26.20 seconds): 46 scenery maps, 4,709 drawn
parts, 723,205,836 bytes, 27,613 changed pixels and exact restoration, matching the
prior comparison. Final handoff passes 129 watched frames with 29 fallback holds
(`build/world-scenery-handoff-admission-final.log`); final cancellation passes five
cancellations with identical restored pixels and 177,118,448 texture bytes
(`build/world-scenery-cancel-admission-final`). These test durations are not FPS
measurements. Latest interactive launch is verified responding with a FableForge
window. Serial coarse preparation, 64-map/GPU limits and available authored
levels remain the coverage/quality constraints.

## Population and cross-level mesh LOD continuation (2026-09-29)

World picking now intersects overview triangles instead of repeatedly marching
through map footprints. This fixes false hover hits when rays enter a map below
its ground from the void. CPU/native ray and cursor regressions pass; the
selectable surface remains an overview approximation.

Overview uploads retain a 2 ms soft budget and raise the count guard from 4 to 32.
Pending geometry sorts by camera-to-map-box distance. At 33 ms pacing,
`build/world-population-final` records thumbnail upload frames 100 versus 15 and
geometry frames 115 versus 92, with identical final pixels and 21,669,456 GPU
texture bytes. Both sides include camera priority, isolating the count guard.
These counts are not FPS measurements. Single driver calls may overrun the slice.

The new object path selects authored variants
by projected size with complementary coverage transitions, distance visibility
and per-object frustum culling inside material batches. The common world camera
applies across map tags to foliage and all placed mesh classes the scene loaders
produce. Retail research found multiple levels in 2,447 of 3,294 meshes. Missing
or unusable chains keep base geometry with size/distance visibility; this does
not synthesize missing levels or implement animation. Terrain uses 32-cell
patches with step 1/2/4/8 index levels, unchanged vertices and full-resolution
patch perimeters. A one-pixel projected vertical-error limit selects terrain
levels; conservative bounds/error include both morph endpoints. Water stays
on its existing independent geometry path.

Retained CPU ranges and 32 GPU metadata bytes per range support selection and
coverage. Adjacent visible ranges merge into draws. Geometry includes authored
alternatives, so resource bytes and draw calls may rise even when fewer triangles
reach the screen. Native visual/resource checks and profiles remain necessary.

Cross-map scheduling adds a bounded, feathered view-cone bonus, absolute underfoot
priority, resident hysteresis and an omnidirectional near ring. No map is rejected
solely for leaving the cone. Radius/altitude limits and adaptive map/memory caps
remain. Draw distance is 85% of loading radius, leaving a preload band. The
serial worker and whole-map publication still limit first appearance; this is
not spatial chunk streaming. Standalone demand tests pass 176 checks; native
cross-map native coverage includes the six-map LOD/recovery comparison below.

Detail upload requests 4 ms for a responsive current frame sample (at least
45 Hz), otherwise 2 ms. Fixed 2/4 ms overrides and a frame counter support
comparison. `build/world-lods-review` records 67 versus 36 upload frames for
the six-map scene, with identical final pixels at six poses/settings and
204,125,040 GPU texture bytes. This does not establish worst-case frame time.
The near pose draws 499 lower-LOD parts among 3,266 selected material parts
(691 merged draws). Frustum on/off and camera recovery produce identical pixels.
Far terrain submits 36,208 versus 38,912 full-resolution triangles; two patches
choose coarser levels. Near terrain remains full detail at the one-pixel limit.
