# World view and UI continuation — 2026-09-28

Checkout: `D:/Code/FableForge`, branch `feat/editor-ui-shell`, continued from
Desktop `forge.txt` and the pending edits after `f7d7be4`.

## Release preparation: 0.18.0-rc.1

The user explicitly requested GitHub publication, a version/release/docs update,
then a visible launch of the packaged build. This supersedes the earlier no-push
working-session constraint. Publishing as a prerelease preserves the outstanding
fresh-game, human visual and separate-machine gates in `docs/RELEASE.md`; 0.17.1
remains the latest final release. Notes: `docs/releases/0.18.0-rc.1.md`.

The full release rebuild exposed `1L << 40` in the distant-texture DXT1 encoder.
On Windows, long is 32-bit; a four-shade block decoded as 255/255/255/255. Replacing
the invalid sentinel with `numeric_limits<long>::max()` restores 0/85/170/255.
Only palette-index selection changes; endpoints, block framing and container
layout remain unchanged. An exact round-trip regression covers the four shades.

Packaging now derives the version/suffix from CMake, validates its staging path,
and includes release notes/README screenshots. CI runs all four CTest suites.
`FABLEFORGE_AUTOMATION_HIDDEN=1` hides scripted children throughout scratch suites,
without affecting an ordinary interactive launch. The CLI reference was regenerated;
the engine-rules document now reflects supported extracted installs.

Release checks uncovered test-harness maintenance: ui_smoke tried to unlink a
subdirectory; newlevel's off-grid subcase looked for a Level-tab button on the
Objects tab and ignored its failed assertion; overworld assumed stock's 399 maps
instead of independently counting the fixture WLD; mods used a brittle 110..112
upper bound on changed records (this customized base needs 113). Fixes preserve
the 95-new-record/minimum-edit expectation and the existing content, layout,
refusal, deployment and rollback checks. Changed checks must pass on rerun.

Validation completed: all 33 broad checks passed across the initial run and
targeted reruns (`build/release-checks.log`, `build/release-rerun-*.log`); no corpus
skips reported. The strengthened newlevel check also passed. README screenshots
were regenerated and inspected; temporary ready-toasts are cleared for publication.
The 18.0 MB ZIP was extracted to a separate scratch folder and used as the working
directory: CLI info, GLB export with foliage/objects, defs listing, and hidden world
water/detail-toggle GUI checks all passed. `build/release-package-smoke.json` records
the tested ZIP SHA-256 and extracted path. The normal binary has profiling disabled
and only Windows/UCRT/D3D imports. Package and source version are 0.18.0-rc.1.

## Previous: continuous overview-to-detail material transition

The next marathon pass traced a remaining terrain snap: height morphing was
already continuous, but publication immediately switched to the detailed albedo.
World ground now samples the existing overview SRV during its transition and
blends toward detail using the same fraction as height. The shared SRV is retained
with AddRef/released on tag removal and layer clearing, and connected in either
upload order. It allocates no duplicate texture and adds no draw pass. Resource
accounting continues to count owned allocations rather than shared references.
Full-detail shading skips the additional sample. Object/plant dithering remains.

Automation adds `world_transition_hold <fraction|auto>` and an A/B material switch.
`tools/test_world_material_transition.py` holds Guild/Oakvale poses, disables
animated water, and captures fractions 0..1 in eleven steps. Hidden launches use
a fixed outer size through `test_world_streaming.py --size`. Initial unrestricted
window-size comparisons were rejected because their viewport dimensions differed;
use only `build/world-material-fixed-{before,after}` for A/B results.

Both controlled runs pass. Cropped viewport publication (coarse -> held fraction 0):
- Guild: pixels changing by >40 in any channel 8,709 -> 8; mean max-channel delta
  7.9170 -> 1.2872.
- Oakvale: 12,695 -> 1,550; mean max-channel delta 12.1337 -> 4.6388.
- Full-detail endpoints are pixel-identical at both poses. Intermediate steps still
  include moving terrain silhouettes and object dithering, not just texture changes.
The Guild endpoints were visually inspected. These diagnostics show reduced
publication discontinuity, not elimination of every geometry/lighting seam.

Normal and clang profile builds pass. `build/profiles/world-material-blend` retains
the same 24 prepared maps / 280.6 MiB geometry across four regions. Editor update
p99/max 3.246/4.335 ms; GPU viewport p99/max 0.614/0.620 ms; no profiler diagnostics.
A 33.407 ms Present wait remains unexplained. This is an overhead sanity capture,
not a controlled FPS improvement claim.

The broad zero-cache route `build/world-flight-material` passes: 473 continuous
frames, 2,594 watchdog polls, no visible window/focus capture, zero sampled culling
coverage differences, focus-distance regression passed. Largest transient missing
candidate is two pixels; no stationary colour-change candidates above the existing
threshold. Contact sheet inspected. Actual capture dimensions differ from the prior
turn's flight, so do not treat these candidate counts as a matched A/B speed or
quality score; use the controlled material test for that comparison. Authored void
and geometry/normal seams remain. Rebuilt CPU geometry tests pass.
Return-trip cache/invalidation, memory-pressure eviction/recovery, and water-toggle
UI regressions all pass at 33 ms pacing after the shared-SRV change. No game-install
writes, visible launches, commits or pushes. Next candidates remain shared mesh/
texture resources, object LODs, and matching overview/detail normals at publication.

## Previous: GPU memory telemetry and adaptive inactive cache

The next marathon pass added `Renderer::queryVideoMemory()` using
`IDXGIAdapter3::QueryVideoMemoryInfo` (local segment, node 0). World detail samples
once per second. The inactive cache shrinks under pressure, retaining up to
128 MiB / one sixteenth of the reported budget / estimated room after headroom,
whichever is smallest. Recovery grows at most 16 MiB per five healthy samples;
failed queries retain the previous bounded allowance. Only inactive maps are
evicted. Active maps, overview terrain and 178 overview water batches are outside
this policy. This does not bound the entire working set or implement object LODs.

`src/detailcache.hpp` holds the tested pure policy and ordered trim operation.
Automation's `world_memory_sample` injects budget/usage, unavailable data, or
restores real queries with `auto`. It never allocates VRAM or changes OS budgets.
The existing return-trip cache test now injects ample room for hardware-independent
expectations. `test_world_flight.py --memory-pressure` extends visual comparisons
to an exhausted synthetic allowance. Tracy records query/trim zones and process
budget/usage/cache allowance plots; state dumps expose validity and injection.

Validation: normal MinGW and clang profile builds pass; all four CTest
suites pass. `world_memory_pressure.txt` passes in both builds, evicting one cached
map, retaining active terrain/water, reloading on revisit, and recovering gradually.
The existing return-trip/invalidation cache regression also passes at 33 ms pacing.
Actual DXGI reports valid data on this machine (budget 16,198,258,688 bytes at
capture time; dynamic, not a hardware-capacity claim). The single-map profile
`build/profiles/world-memory-pressure` reports a 0.00657 ms real query and maximum
0.00528 ms inactive trim, with no GPU profiler diagnostics.

The normal full-world flight `build/world-flight-memory` passed: 473 continuous
frames, 3,026 window polls with no visible/foreground window, sampled culling
coverage differences zero, focus-distance regression passed. Largest transient
candidate was nine pixels; one 316-pixel colour transition near Hook Coast remains.
The contact sheet was inspected. Several poses face authored empty world space;
this coverage test is not proof that every rendering transition is smooth.

The zero-cache flight `build/world-flight-memory-pressure` also passed: 473 frames,
2,887 hidden/focus polls, zero sampled culling differences and at most nine transient
missing pixels. It additionally exposed 1,067/1,035-pixel stationary colour changes
near GuildExterior during detail arrival, plus 317 near Hook Coast. An affected
Guild frame was inspected: detail transitions remain visible; do not describe this
as all pop-in fixed. The harness's new report flag was added while this run was
already executing, so the generated script/state logs are the injection evidence.

`build/profiles/world-memory-stream-overbudget` follows the existing four-region
24-map route, then climbs and trims four inactive maps (122,743,308 tracked bytes)
to zero. Eight real DXGI queries: median 0.00598 ms, max 0.00791 ms. Maximum trim:
0.05691 ms. No profiler diagnostics. Editor update p99 3.249 ms / max 5.327 ms;
whole-frame maximum includes a 9.8 s automation profiler-connect wait, not an
interactive editor stall. These runs are not FPS benchmarks. The first scratch
variant incorrectly expected zero retention at usage exactly equal to budget:
reclaiming cache left enough room for one map after headroom. Its assertion failed
correctly; the final forced-overbudget variant uses 2 GiB usage against 1 GiB budget.

Next renderer work: shared geometry/texture resources and intermediate detail LODs,
measured independently of this inactive-cache policy. UI design and other backlog
constraints below remain in effect. No game-install writes, commits or pushes.

## Previous: dispatch latency, upload budget and additional research

Continued the user's marathon request, then researched more GitHub/primary-source
options while the final hidden route ran. The user subsequently closed their game;
visible launches were not needed. Findings and ranked implementation candidates:
[WORLD_RENDERING_RESEARCH.md](WORLD_RENDERING_RESEARCH.md). No new third-party
dependency was installed. Meshoptimizer/shared geometry/DXGI memory budgets are
promising; Intel MaskedOcclusionCulling was verified archived September 7, 2026.

Implemented two measured streaming improvements in `updateWorldDetail`:
- A ready CPU future bypasses the 400 ms demand timer. Current eye/generation/
  wanted checks still run before accepting it; ordinary idle demand stays throttled.
- GPU detail uploads retain the 2 ms soft budget but raise the batch-count guard
  from 4 to 32. Profiling showed 246/338 upload frames stopped below budget at four;
  the new capture used 1..16 batches. Individual allocations can exceed the budget.

`tests/ui/world_stream_profile.txt` is a screenshot-free four-region/altitude route.
`profile_world.py --frame-ms 33` now supports reduced-contention captures and
reports serial prepare-to-first-upload latency (unmatched/discarded work separate).
`test_world_streaming.py` accepts script lists and frame pacing, and uses below-
normal child priority. Normal and clang profile builds are current.

Three comparable captures, each with 24 prepared maps and identical geometry totals,
had no profiler diagnostics. Median readiness latency: 393.75 -> 48.17 ms after
dispatch; 49.48 ms with time-budgeted uploads. Four load phases total: 24.65 ->
17.22 -> 14.20 seconds. Detail upload frames: 337 -> 338 -> 238. This is a 42.4%
loading reduction at fixed 33 ms pacing, not an FPS claim. Upload p99/max rose
2.92/3.64 -> 3.51/4.63 ms; the work is less spread out. Complete evidence and caveats
are in WORLD_PERFORMANCE.md and `build/profiles/world-stream-{before,after,budget}`.
The combined comparison JSON is `build/profiles/world-stream-dispatch.json`.

Validation: cache reuse/invalidation script PASS at 33 ms pacing. Full-world
continuous routes passed after dispatch alone and after both changes. Final
`build/world-flight-budget-final`: 473 frames, 3164 visibility watchdog polls,
zero visible/foreground samples, zero sampled culling coverage differences,
focus-coverage assertion PASS, only a five-pixel transient candidate. Largest
stationary colour change was 338 pixels at the known Hook Coast transition;
contact sheet inspected. No large transient empty patch reproduced in that route.

After the game closed, `world-stream-game-closed` captured the same route at 4 ms
pacing: CPU editor-update median/p99/max 0.732/3.313/6.171 ms; GPU viewport
0.121/0.818/0.935 ms. One Present call took 30.836 ms; cause is unproven. Frame
duration includes deliberate sleep and hidden-window presentation, so do not infer
FPS from it or directly compare it with the earlier competing-game capture.
No new rendering test processes remain. No commit, push or install writes.

## Earlier: full-world flight and reproduced far clipping

User clarified that diagnosis must cover the large 3D world at varying heights.
The harness now defaults to `--route world`: 89 poses across Albion, absolute eye
heights 50..7000, climbs/descents/turns. `--continuous` captures every live frame
and world state; `capture_begin/end` preserve explicit screenshots too. A rolling
three-frame analyzer ranks temporary missing coverage and large changes at fixed
poses. `world_pose` moves without forcing demand or changing orbit focus distance.

First full route saved 473 consecutive frames (`build/world-flight-global-continuous`).
Its reference phase timed out: old `wait_world_detail` demanded at least one map
even at heights where zero maps is correct. This was an automation bug, not a
60-second UI freeze. It now refreshes demand once and accepts a settled empty set.
Partial-run sequence analysis found only 1..3-pixel transient holes and a largest
stationary colour change of 345 pixels near Hook Coast; this is not proof of the
user's original flash. Its window watchdog saw no visible/foreground child windows.

A separate identical-pose test DID reproduce missing world geometry. With orbit
distance 20, the far plane was only 1600 despite flying far above the world. One
sample lost 127,024 covered pixels compared with orbit distance 8000. Turning
frustum culling off did not help. `Renderer::render` now derives world far clipping
from loaded overview/detail batch bounds instead of orbit distance; map views are
unchanged. CPU tests verify enclosure of every world-box corner from high/outside
eyes. GUI build and focused core CTest passed.

Paired before/after captures: `build/world-far-before` / `build/world-far-after`.
Maximum focus-dependent missing coverage fell from 127,024 to 16 isolated pixels;
positions reviewed, no connected patch remains in that comparison. Minor camera
matrix/silhouette differences mean exact RGB equality is not this test's oracle.
`--assert-focus-coverage` provides a 64-pixel tolerance regression gate; the saved
baseline fails and fixed captures pass. Both hidden runs exited 0/PASS, with zero
visible/foreground samples (739 and 766 watchdog polls respectively). A final
continuous full-detail route is recorded separately under `world-flight-global-fixed`.
This fixes a proven disappearance mechanism, not every visual/streaming issue.

The full-detail follow-up completed exit 0/PASS in about 157 seconds: 473 consecutive
live frames, nine same-pose reference samples, zero culling coverage differences,
one nine-pixel transient candidate and no >128-pixel stationary colour changes at
the 80/255 threshold. Its 3072 watchdog polls saw no visible/foreground windows.
Reviewing the images exposed a second orbit-distance dependency: haze darkened
the restored world after zooming close. World fog now uses enclosing world span
instead. That final shader-constant change is checked separately by the paired
`build/world-far-fog-final` run; the 473-frame run predates the haze adjustment.
Final haze/clip run passed its focus-coverage assertion and native automation;
815 watchdog polls saw zero visible/foreground child windows. At sample 84 the
>40/255 colour-change count fell from 48,399 before the haze adjustment to 111
(small raster/silhouette differences and animated water remain). Final before/
after image: `build/world-far-fog-final/before-after.png`, visually inspected.
No GUI test process remains. Normal build is current; profiling build has not
been rebuilt for these last projection/automation changes. No commit or push.

## Earlier: autonomous hidden flight diagnostics

The user still sees patches flash/empty inside the viewport and asked for testing
while gaming. Do not launch visible windows or steal focus. Added
`tools/test_world_flight.py`: deterministic Start/adult Oakvale routes, low flight,
optional one-map budget/altitude stress, same-pose coarse/settled references and
culling-disabled comparisons. Captures include state logs, PNGs, contact sheets,
GIFs and JSON. Missing-coverage checks have synthetic positive/negative controls;
they are diagnostic, not a complete visual-quality oracle. Automation adds
`world_eye_ground x y clearance [yaw pitch]` without forcing demand updates and
camera telemetry in `dump_state`. `--auto-frame-ms` defaults to 4, harness uses 33.

Five hidden runs completed (normal Start/adult, adult stress, two Start stress
routes), 69 sampled live poses plus references. All native exits were zero and
automation logged PASS. Across 4,892 watchdog polls, zero child windows were
visible or foreground. No test process remains. Normal GUI build and Python
syntax/coverage controls passed. No renderer fix was made in this latest step.

Artifacts: `build/world-flight-start`, `world-flight-oakvale`,
`world-flight-oakvale-stress`, `world-flight-start-stress`, and final
`world-flight-start-overhead`. Normal runs used four phases; the last two also
compared settled detail with culling disabled. No sampled transient missing
coverage or culling coverage differences were found. The first altitude stress
view looked past the terrain at some poses; final stress pitch 1.5 keeps the
ground in view. Contact sheets inspected. This does NOT reproduce or resolve the
user's reported flash: captures are sampled, not every rendered frame, adaptive
quality is disabled, and only clear-background holes are detected. Visible coarse
terrain/detail arrival and disconnected world edges still merit investigation.
Overview-relative clearance also cannot guarantee clearance from detailed terrain
or buildings. Next diagnosis should capture consecutive render frames and widen
routes/settings, rather than asserting this issue fixed from sparse clean frames.

## Design contract

The user's direction in ROADMAP_1.0.md: **non-intimidating, intuitive, beautiful,
modern, functional, powerful. Port vanilla behaviour, never its UI.**
Use visual browsing, contextual actions and compact inspectors. Put technical
details and uncommon controls behind tooltips or an intentional advanced view.
Selection must not open a large popup or interrupt camera movement.

## This continuation

- Finished the pending pinned panel/sub-tab headers, fitted labels and hover
  details, compact hints, Speed value placement, labelled new-level coordinates,
  View > Text size, bounded/scrolled Help and tool windows. Activity follows
  available height until explicitly toggled; starts folded on short windows.
  Fractal preview now leaves space for Apply and Defaults at 1366x768 / 150%.
- Fixed the world-picking bottleneck: each ray sample called `worldPlacement`
  for every box, repeatedly scanning/lowercasing the entire world list. Use the
  already-known box and pending moves instead; preserve smallest-overlap rules.
  The transcript's roughly 280-second detail tour now completes in roughly
  6 seconds on this machine with warm tiles. Instrumented uploads were only
  33–54 ms/map: renderer expansion was not the dominant cause of that delay.
- Water now lives in the persistent overview at WLD origins, using the existing
  ripple/shore fade/ice shader, alpha blending and depth testing after opaque
  geometry. It stays visible while detail loads, unloads or is disabled. The Water
  toggle is independent of detail; leaving 3D releases the overview GPU buffers.
- Frustum culling of world terrain, detail and water batches. Oakvale comparison:
  653 submitted batches without culling, 249 with culling (404 skipped), with
  pixel-identical static screenshots. This is not occlusion culling.
- Automatic detail adjusts the map count up to the user's limit (default six):
  reduce after two seconds below 45 fps, restore after five seconds above 55 fps.
  Ignore loading, focus loss and isolated >250 ms stalls. Manual mode fixes the
  map count. This is the existing coarse-tile/full-detail transition, not new
  per-object mesh LODs or dynamic resolution. Choices currently last the session.
- Completed loads are checked against current camera demand and a generation
  counter before upload, so obsolete work after moves/options changes is dropped.
- Scale-sweep success now requires a clean process exit as well as RESULT PASS.
- Moved detailed geometry expansion/bounds to workers, paced GPU uploads at
  four batches / soft 2 ms per frame, and publish a map only after its
  batches are ready. Coarse/detail terrain transitions use a complementary
  250 ms opaque terrain height morph and separate object/plant dither fade;
  persistent water is unaffected. Residency hysteresis reduces churn.
- Paced overview texture and geometry uploads too. Removed another quadratic
  map lookup in every-frame overview maintenance. In a matching settled Oakvale
  phase, Tracy measured median maintenance falling from 6.584 ms to 0.165 ms.
  See [PROFILING.md](PROFILING.md) for comparison limits and captures.
- Added optional Tracy CPU/worker/D3D11 GPU zones, counters, a read-only route,
  phase summaries and isolated cold-cache capture. Normal builds compile these
  markers away. GCC profiling exposed a separate thread-local destructor crash
  under cold loading; profiling now requires the clang-cl/MSVC ABI build helper.
- Fixed black terrain on partially baked sea/filler maps: uncovered background
  pixels now use the existing LEV theme sampler. Covered STB colours are retained.
  Revised the tile cache key so old black tiles are rebuilt. Actual-data check
  `tools/test_world_background.py` matched 192,512 uncovered pixels to the LEV
  fallback; StartOakValeWest's fully covered output stayed pixel-identical.
  Dark space outside actual geometry is still the viewport backdrop.
- Continued with CPU-only terrain scheduling while the user is gaming. Replaced
  fresh per-bake thread groups with one bounded persistent `RowExecutor`, shared
  by foreground and LEV fallback paths. At most eight row workers (fewer on
  smaller CPUs), a queue capped at twice that count, inline nesting to prevent
  deadlocks, and exception draining before caller-owned image memory can die.
  Tiny bakes remain inline. Outer overview/decode workers are unchanged.
  Added queue/join profiler zones and a standalone concurrent scheduler test.
  Before/after CLI outputs for OakVale_Sea_02 and StartOakValeWest were byte-
  identical across all three diagnostic PNGs per map (`build/row-pool-compare`).
  No flight speedup is claimed until the deferred graphics measurements run.
  Normal and clang-cl profiling builds passed; the focused row-pool tests passed
  in both. Normal CTest is now 3/3 passing (including the new scheduler suite).
- The next cold trace found main-thread destruction of uploaded CPU data taking
  up to 19.281 ms. `DeferredRelease` moves it to one worker; streaming waits for
  that slot without accumulating a backlog. A follow-up cold capture measured
  main-thread handoff at at most 0.0174 ms, with the actual freeing on the worker.
  Tests cover retained ownership while busy, worker-thread destruction and drain
  on shutdown. GPU resources still belong to the render thread.
- Streamed layers and water now use indexed geometry instead of repeating a full
  vertex for every triangle. Each source vertex is transformed once per
  instance/material; triangle order, materials, UVs, shore alpha and ice remain.
  This is not GPU instancing: copies still have separate transformed vertices.
  CPU geometry tests passed. The full world viewport matched the pre-indexing
  image pixel-for-pixel (only an Activity elapsed-time digit differed outside it),
  and full-window culling on/off images matched. Hidden water/editor routes passed.
- Profile report parsing now handles commas in Tracy messages and keeps internal
  diagnostics out of route phases. Plot CSVs expose geometry bytes versus the
  old expanded equivalent. A pinned D3D11 overlay retries delayed timestamps;
  actual query errors remain visible. See PROFILING.md and vendor/VENDORED.md.
- Indexed cold capture passed: 50.39% less prepared geometry (204.7 versus 412.6
  MiB across 15 maps; not peak residency), five delayed GPU reads successfully
  retried, no diagnostics, matching viewport/UI CPU and GPU submission counts.
- Applied the format-I/O skill to the read-only STB optimization. The World tab
  reread the whole common header per map; a new bulk API reads it once, retaining
  the single-record slice/validation contract. Map placement and region ownership
  now use first-match indexes. The scripted opening step fell from 229.754 ms to
  8.193 ms; 401 map-list and 341 region-list lines match the saved CLI byte-for-byte.
  Unit coverage includes unknown-byte preservation and bad record end pointers.
  No writer or game-install file was changed.
- Tile cache v3 uses raw Windows file metadata, avoiding compiler-specific
  file-clock epochs. This lets MinGW Release and clang-cl profiling share tiles.
  Normal/profile builds passed; the latest core suite passed 4/4, including
  scheduler/retirement and CPU indexed-geometry tests. Last traces and comparisons
  live under `build/profiles` and `build/world-layout-compare`.
  Cross-toolchain cache validation also passed: the profile build reused all
  400 Release-generated tiles without changing their bytes or modification times
  (`build/cache-abi-ia3_ss00`). All test launches in this pass were hidden.
- Persistent overview water uses conservative flat-quad merging: only equal
  height, fade and ice values merge, within one liquid/ice stream and at most
  32 cells per side. Shores, holes, varying heights and ice boundaries retain
  their triangles. Unsupported topology is returned unchanged. This changes
  derived preview geometry only, not game files or the engine-derived water bake.
  Synthetic coverage/interpolation checks and cache round-trip/truncation tests
  pass. Actual 400-map capture contains water in 178 maps: CPU payload fell from
  26.65 to 9.81 MiB (63.2%); compact GPU buffers total about 14.70 MiB. These byte
  counts exclude container/allocator overhead, textures and the rest of the world.
  Hidden overview/detail/off screenshots confirm the coast remains visible.
  `world-overview-water.tracy` completed without diagnostics; its water GPU median
  was 0.010 ms, but concurrent gaming means this is not a controlled FPS claim.
- Cache format FWT2 / key v4 adds water and includes game.bin, names.bin,
  textures.big, STB and gain alongside the LEV/WAD source revision. Old tiles
  rebuild once. Raw Win32 timestamps keep keys portable between our compilers.
  Keys are captured when overview loading starts; live external edits still need
  a reload. Prepared full-detail geometry does not yet have a persistent cache.
- The water capture exposed repeated all-name searches in the 2D canvas too.
  Known boxes now supply their positions directly, with pending moves applied;
  draw ordering, hover selection and drag semantics are unchanged. Added a
  `World 2D canvas` profiler zone for attribution. Follow-up cold capture
  `world-water-canvas.tracy` reduced the overview phase's median editor update
  from 14.651 to 0.942 ms; the canvas itself measured 0.245 ms. The existing
  pending-move world route passed, along with the updated water route (178
  resident water maps independent of detail, and checkbox clicks with detail off).
  The complete four-test suite passed after these changes. Water screenshots
  were inspected; launches stayed hidden and below-normal priority.
- Terrain baking now reads/decompresses each map's STB frames once for both
  foreground layers and background colour. The standalone diagnostics retain
  their independent reader APIs; no cross-map cache, owning TLS, format parser
  changes or writer changes were introduced. Frames are released before texture
  row work. Six before/after maps (OakVale_Sea_02, StartOakValeWest, HookCoast,
  Greatwood_1, PrisonIsland, HeroGuildComplexInside) produced byte-identical
  engine/background, baked and LEV-fallback PNGs (18 files total), recorded in
  `build/stb-read-share-compare/comparison.json`. Both builds and the affected
  core test passed. Final cold capture `world-stb-shared.tracy` passed without
  diagnostics; all 400 resulting tile-cache files are byte-identical to the
  preceding capture (`tile-comparison.json`). Median terrain-build time across
  415 bakes fell from 20.690 to 15.098 ms. Foreground and background STB zones
  previously totaled 8.041 seconds of worker time; the combined read/decode
  totaled 4.879 seconds. Those are summed, overlapping worker durations, not
  elapsed load time, and concurrent gaming limits controlled timing claims.

## Verification and limits

Latest user follow-up: whole patches briefly flash/disappear **while still in
the viewport**, not just distant buildings arriving. The subsequent changes are:

- Detail residency follows the eye's horizontal position instead of the centre
  ray's ground intersection, which could jump across the world when looking near
  the horizon. The 250-unit radius extends up to 500 for a long focus distance;
  existing map-count limits, height gates and retention hysteresis remain.
- Recently inactive, fully uploaded detail maps stay hidden in a 128 MiB / six-map
  LRU. Returning restores GPU resources without decoding or uploading again.
  Accounting includes vertex/index buffers and each owned RGBA mip-0 texture;
  it excludes driver overhead and does not cap active maps or total app memory.
  Wanted cached maps are rescued before outgoing maps can evict them. Oversized
  maps are dropped without flushing useful smaller residents. Disable detail,
  leave World, reload layout/install, or change placement/detail options to clear
  the cache and invalidate in-flight workers. This is not a disk detail cache.
- Ground no longer uses complementary screen-space cutouts against a differently
  shaped coarse mesh. Full ground remains opaque and moves from sampled overview
  triangle heights to its detailed heights. Its otherwise unused `walk` vertex
  attribute holds the coarse height; vertex stride/memory is unchanged. Bounds
  include both endpoints for culling. Coarse ground hides only after a complete
  ground-bearing map uploads; it returns when fade-out reaches the coarse shape.
  Water remains independent; object/plant fades still use dither. This does not
  add intermediate mesh resolutions, object LODs or GPU instancing, nor eliminate
  every possible seam between differently authored neighbouring maps.
- World near clipping uses clearance over terrain (0.1 to 20 units), not the old
  orbit focus distance retained during free flight. Descending from a distant
  overview can therefore no longer retain a focus-derived near plane tens of
  metres ahead. Other views retain their existing clipping policy.

Tests cover LRU byte/count limits, oversized rejection, replacement, reuse and
clear; triangle-vs-bilinear overview heights; and near-plane bounds. The hidden
`world_detail_cache.txt` route checks horizon turns without new builds, a return
trip without another build, and invalidation on detail disable / leaving World.
`world_transition.txt` records start/middle/end ground transitions and a descent
that deliberately keeps an 8,000-unit focus distance. These are targeted fixes
for verified mechanisms; the user's exact flight has not been reproduced.
The normal build and focused core/geometry tests pass. Five hidden routes passed
with clean native exits: world_view, world_water, world_detail_cache,
world_transition and world_culling. Culling on/off images remain pixel-identical.
Mid-transition and low-flight screenshots were inspected: no map-sized empty
patch, but a pixel comparison still found 33 newly clear pixels within shared
start/end coverage (thin seam/silhouette differences). Do not claim all cracks
or all pop-in are eliminated. The clang-cl profile build also passes.
`world-detail-cache.tracy` passed with no diagnostics: exactly two map builds
(initial and after explicit invalidation), with none on the cached return.
A separate six-map Oakvale return check retained five maps (47,344,936 bytes),
reused all five, and rebuilt only the oversized sixth: loads 6 -> 7 rather than
6 -> 12. The cache stayed below its 134,217,728-byte allowance. Evidence:
`build/world-cache-six-result.json`. This checks real textured/foliage maps as
well as the one-map deterministic cache test.

Core tests include six-plane culling, clipping boundaries, a containing map,
and adaptive-budget pressure/recovery/focus/stall cases. UI scripts:
`world_view.txt` (including pending-move picking), `world_water.txt` (on/off,
unload/reload), `world_culling.txt` (paired screenshots), `editor.txt`,
`owner_daynight.txt`, `height_pens.txt`. Scale coverage includes 1280x720 / 100%,
1366x768 / 150%, 1920x1080 / 100%, 2560x1440 / 100%. True 4K remains untested.
The 132-item UI review is a historical issue list, not 132 independently closed
defects. No game-install changes or in-game validation belong to this pass.

World streaming exposed intermittent Windows heap corruption (0xC0000374) in
MinGW thread-exit TLS destruction (`build/world-crash-gdb.log`, `run_dtor_list`).
`walkFramedBlocks` had a thread-local owning scratch buffer. It now owns the
same uninitialized/grow-only buffer within a scan, eliminating TLS destruction.
Ten consecutive release world/water runs passed after this change; the saved
pre-change binary reproduced the crash again in the comparison. This isolates
the lifetime change; it does not prove a specific compiler/runtime defect.
Clang AddressSanitizer world and water tours passed even before that change,
so an ASan pass alone was insufficient here. Reproduce with
`python tools/test_world_streaming.py` (or `--exe` for a saved baseline).
ASan and temporary guarded-allocation builds are local diagnostics, not shipping
targets. The normal build's temporary CMAKE_PROJECT_INCLUDE was cleared.

The user is gaming and now permits occasional hidden launches that do not take focus.
`tools/profile_world.py` and `tools/test_world_streaming.py` now default to hidden
windows (`--show` opts in); hidden rendering still consumes GPU resources.
Full cold routes passed under clang-cl with the row pool and background payload
retirement. The latter run sampled the window 172 times: never visible or
foreground. CPU-only TLS stress is separate from graphics validation.
The clang-cl 22.1.8 profile build/helper and CPU-only stress passed: 4,096 workers,
4,096 recorded zones, clean test/recorder process exits. Evidence is
`build/profiles/clang-thread-lifetime.tracy`; no graphics window was launched for
that check.
The final ordinary Release build (`FableForge`, `forge`, both test binaries) and
core CTest passed 2/2 after the terrain fallback changes. Python helper syntax
checks and `git diff --check` passed. Earlier UI/scale coverage is listed
separately from the new hidden profiling runs. No commit or push was made.

## What remained from Claude, and the user's follow-up requests

1. **World performance:** per-object mesh LODs, GPU instancing/shared meshes,
   mip/compressed-texture residency, bounded scheduling
   beyond terrain rows, prefetch/cache eviction by memory budget and weak-GPU profiling
   remain. Uploads now have soft slices, but individual driver allocations and
   GPU resource destruction can still hitch. CPU payload disposal is now on a
   worker with bounded backpressure. See [research and GitHub
   candidates](WORLD_PERFORMANCE.md). No guarantee of hitch-free flight or
   cross-hardware fps.
2. **Asset browser first:** Assets > Models currently opens the import card.
   Add search by mesh name/id, orbitable textured/wireframe preview, material
   texture links, counts, helper points and used-by references. Follow with
   collision views, skeleton/skin preservation, animations and attachments.
   See PARITY_BACKLOG.md, “Mesh browser with 3D preview”, and animation sections.
3. **Particle/effect editing and creation:** start with lossless parsing/writing
   and clones/templates, then colour, texture, size, emission, life and lights
   with a preview. Preserve unexposed fields. Current static proxies are not an
   engine-faithful simulation. Reuse/cross-check EgoCore ParticleParser/Compiler
   and recovered runtime behaviour. Deep bank editing can remain in EgoCore.
   BLOCKED_LANES.md's corrections matter: its particle JSON route was export-only;
   a working two-way interchange must be implemented and verified, not assumed.
4. **Contextual viewport UI:** useful right-click actions (Focus, Duplicate,
   Delete, Drop to ground, Properties, suitable links), a restrained selection
   toolbar, and an on-demand inspector window that follows selection. Reuse
   existing editor operations and undo rather than parallel action logic.
5. **Broader parity:** multiple editable maps/regions, animation/bone/event tools,
   water generators, navigation cost/layer gaps, non-mesh things and asset
   dependencies remain in PARITY_BACKLOG.md / BLOCKED_LANES.md / HANDOFF_NAV.md.
   The separate water-generation branch still needs its own validation. Visible
   water in this view does not implement those generators.

Debug-editor evidence should define algorithms, defaults, ownership/selection
rules and serialization; EgoCore supplies format implementations and cross-checks.
Neither is a UI template. These are next-work plans, not implemented claims or
authorization to message Aeon/Discord. Re-read current code before tackling each
historical backlog item, because some entries predate later fixes.
