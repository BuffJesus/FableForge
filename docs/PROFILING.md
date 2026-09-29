# CPU and GPU profiling

Forge has an opt-in Tracy 0.13.1 integration. Normal builds compile all markers
away and do not fetch/link Tracy. Profile builds use local connections on demand;
closing the profiler does not require closing the editor. System-wide sampling
is disabled by default; explicit zones and D3D11 GPU timing remain available.

## Build and tools

For streaming latency comparisons, use `tests/ui/world_stream_profile.txt` with
`tools/profile_world.py --frame-ms 33 --script tests/ui/world_stream_profile.txt`.
It traverses four regions plus a high-altitude overview without screenshot
readbacks. `--frame-ms` defaults to 4; 33 reduces GPU contention while gaming.
The report includes prepare-end-to-first-upload delays. Matching is bounded by
the next serial prepare, so discarded results are counted as unmatched instead
of being assigned a later map's upload. These intervals include normal frame
scheduling and are not GPU execution times. See WORLD_PERFORMANCE.md for the
baseline/ready-dispatch/time-budgeted-upload comparison.

```powershell
powershell -File tools/build_profile.ps1
```

This uses installed LLVM `clang-cl`, Visual Studio C++ build tools and Ninja,
builds into `build-profile-clang`, and defaults to two jobs at below-normal
priority. The normal Release build remains independent. MinGW profiling builds
are rejected: GCC 15.2 reproduced a cold-load crash in Tracy's thread-local
`ProducerToken` destructor (`build/cold-connected-gdb.log`). Warm captures alone
did not expose it. This is evidence for this configuration, not proof that every
MinGW/Tracy combination is affected. Do not work around it by leaking TLS state.

CMake pins Tracy to `05cceee0df3b8d7c6fa87e9638af311dbabc63cb` (v0.13.1).
For offline builds, supply CMake's `FETCHCONTENT_SOURCE_DIR_TRACY` with that
source checkout. The fetched source and BSD-3-Clause license stay under the
build's `_deps/tracy-src` directory. The license is also copied beside the
profile editor as `TRACY-LICENSE.txt`.

Use the matching upstream
[Windows 0.13.1 tools](https://github.com/wolfpld/tracy/releases/tag/v0.13.1).
Extract `windows-0.13.1.zip` to `build/tools/tracy`. The local development machine
already has `tracy-profiler.exe`, `tracy-capture.exe` and `tracy-csvexport.exe`
there. These tools are not added to the shipping package.

For interactive investigation, start the profile editor and Tracy's viewer,
connect to `127.0.0.1:8086`, then fly around. Zoom into a slow frame and inspect
CPU zones and GPU tracks. The viewer can also open saved `.tracy` captures.

## Repeatable world capture

```powershell
python tools/profile_world.py
# First-use derived tiles, without deleting the regular cache:
python tools/profile_world.py --cold --output build/profiles/world-cold
```

This launches a local recorder and a hidden editor, runs `tests/ui/world_profile.txt`, requires a
clean test exit, and exports:

- `build/profiles/world.tracy`: full timeline, worker threads and plots.
- `world-cpu.csv`, `world-cpu-self.csv`: individual CPU events in nanoseconds.
- `world-gpu.csv`: individual GPU events in nanoseconds.
- `world-messages.csv`: route phase markers.
- `world-plots.csv`: CPU events plus plot samples (plot rows have empty source/time fields).
- `world-summary.json`: per-zone sample counts and median/p95/p99/max milliseconds.

Use `--output build/profiles/run2`, `--exe`, `--tracy-dir` or `--script` to
change the capture. `--show` makes the editor visible. Hidden captures still
consume GPU resources: avoid benchmarking while another GPU application is
running. `--cold` retains its isolated cache directory for inspection. It means
cold derived tiles, not flushed OS disk caches. Summaries include named route
phases so settled views can be compared separately from streaming.

The route stays read-only, fixes the detail limit and covers
initial overview, Start Oakvale loading, settled coast, camera movement,
a Greatwood jump and another settled view. A `wait_profiler` command prevents
the measured route starting without a connected recorder.

## What is instrumented

| CPU track / zone | What it separates |
| --- | --- |
| Editor main: Frame | Whole loop, including Present and labelled automation pacing. |
| Editor update and viewport | Application work and viewport command submission. |
| World overview poll / upload | Consuming tile workers and creating overview GPU resources. |
| World detail demand / upload; World ground ray | Streaming selection, picking and upload work. |
| World paced upload; D3D batch upload; D3D texture create | Per-frame upload slice and individual driver creation calls. |
| World overview/detail worker | Map preparation with the map name attached to its zone. |
| Terrain build / texture bake; Terrain grid mesh; Water build | Terrain/water construction. |
| STB terrain layers/background read | Legacy terrain source extraction. |
| Terrain bake rows / join; Terrain bake queue wait; Terrain bake join | Overall bake, bounded-queue admission and waiting for completion. |
| Terrain bake pool: Terrain bake rows | Persistent workers shared across map bakes; row computation separated from caller waits. |
| Foliage/Placed objects decode; Mesh/Texture cache lookup / decode | Asset conversion and cache access. |
| Indexed geometry prepare / bounds; Water geometry prepare | CPU preparation of indexed upload data. |
| World payload handoff; World prepared payload release (worker) | Main-thread ownership transfer versus worker-side destruction of uploaded/discarded CPU data. |
| Viewport CPU submit; Terrain/world; Water; UI submit | CPU draw submission. |
| Present / display wait | Swapchain/driver/display waiting. |

GPU tracks separately time Viewport, Terrain/world batches, Water and UI. Plots
show submitted/culled batch counts, resident detail maps and the active budget.
GPU zones use the main immediate context and Tracy's asynchronous collection.
The D3D11 header overlay retries `S_FALSE` timestamps on a later collection,
retaining the next unread query. It does not spin or wait during frame collection;
shutdown collection may wait. This follows the
[GetData availability contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-getdata).
`cmake/TracyD3D11.cmake` checks the pinned source before generating the overlay;
upstream/fetched sources are unchanged. Retry events appear in the
`GPU timestamp retry` plot. Real query errors still produce diagnostics.
Startup calibration and final collection may wait; exclude these from flight
measurements. The initial integration does not instrument every allocation or
provide a VRAM-residency counter yet.

CPU zone duration is elapsed time, including any waits inside that scope; it is
not automatically active CPU time. Parent and child inclusive totals overlap,
as do parallel workers: never add them to claim frame duration. Use self-time
exports and the timeline to attribute stalls. GPU elapsed time is separately
measured; CPU submission duration is not a GPU-cost proxy.

Automation sleeps 4 ms per frame and uses Present(0); interactive operation uses
Present(1). Keep the pacing zone separate and do not report the scripted route
as maximum achievable FPS. Captures themselves have overhead. Compare matching
build/configuration/hardware/routes, distinguish cold from warm caches, and
confirm any claimed improvement in the ordinary Release build.

See [WORLD_PERFORMANCE.md](WORLD_PERFORMANCE.md) for the optimization priorities.

## Captured findings and outstanding validation

The initial warm traces (`build/profiles/world.tracy` and `world-paced.tracy`)
identified repeated map-name searches in overview maintenance. In the matching
302-frame settled Oakvale phase, median overview maintenance fell from 6.584 ms
to 0.165 ms; editor update from 6.915 ms to 0.606 ms. The first route's jump wait
was later corrected, so whole-route totals are not a controlled comparison.
These traces used the earlier GCC profiling build and are diagnostic evidence,
not a claim that its cold path was stable or a cross-machine FPS guarantee.

`fableforge_profile_threads` is an optional CPU-only target: start Tracy capture,
then run it to exercise 4,096 short-lived profiled workers. It requires a local
recorder and returns nonzero if connection or completion fails. It does not
replace the full cold-world CPU/GPU route. LLVM clang-cl 22.1.8 / MSVC ABI passed:
all 4,096 worker zones were exported and both processes exited zero
(`build/profiles/clang-thread-lifetime.tracy`). The editor build and the
`tools/build_profile.ps1` helper also passed.

The user subsequently permitted hidden launches that do not steal focus. Full
cold-world routes passed with clang-cl and the row pool (`world-pool-cold.tracy`)
and again after moving payload destruction to a worker (`world-cleanup-cold.tracy`).
The latter was sampled 172 times: Forge's window was never visible or foreground.
These ran alongside a game; do not treat whole-frame/GPU timings as a controlled
hardware benchmark. Tracy emitted D3D11 timestamp-readiness messages, which the
report now retains as diagnostics rather than confusing them with route phases.
Treat GPU timing completeness as suspect in those captures.

CPU attribution was useful: the original main-thread payload destruction peaked
at 19.281 ms. With worker retirement, ownership handoff peaked at 0.0174 ms and
the worker handled destruction (up to 18.591 ms). The queue holds only one
payload; streaming preparation waits for that slot without blocking rendering.

`world-indexed-cold.tracy` passed with the retry overlay: five delayed reads were
retried, no profiler diagnostics, and viewport/UI GPU event counts matched their
CPU submissions (2,268 / 2,444). The 15 prepared maps used 214,650,840 bytes of
indexed geometry versus 432,692,388 bytes for the previous expanded equivalent:
50.39% less. This is cumulative prepared vertex/index data, not peak residency or
total application memory. The capture helper now reports this distinction.

The same trace isolated the 229.754 ms World-tab automation step. World layout
loading repeatedly read the entire STB common header for each map and rescanned
WLD placements/owners. A bulk read plus first-match lookup tables reduced the
follow-up step to 8.193 ms (`world-layout-bulk.tracy`, layout itself 7.988 ms).
Complete retail CLI map/region listings remained byte-identical to the saved
baseline. These are observed operation times, not controlled whole-app FPS tests.

Cache keys now use raw Windows file metadata, avoiding incompatible C++ file-clock
epochs between MinGW Release and clang-cl profiling builds. This invalidates old
older tiles once. Current v4 keys/FWT2 payloads also include persistent water and
the bake's definitions, textures, STB and gain. Keep `--cold` for an explicitly isolated cache; merely omitting it
does not prove the cache is warm (check tile preparation zones and cache reuse).

`world-overview-water.tracy` passed with no diagnostics. Across 400 freshly baked
tiles, flat-quad merging reduced water data from 27,946,802 to 10,288,259 bytes
(63.2%); cache hits do not emit these plots. The helper now reports both totals
separately from full-detail geometry. The water GPU median was 0.010 ms, p99
0.028 ms, maximum 0.281 ms; concurrent gaming prevents controlled FPS conclusions.
The same capture exposed 2D overview CPU time (14.651 ms median editor update in
the overview phase), motivating removal of repeated map-name searches there.
The follow-up `world-water-canvas.tracy` measured 0.942 ms median editor update
in that phase, with the new `World 2D canvas` zone at 0.245 ms. Its diagnostics
were empty. Frame maxima can still be dominated by display waits while gaming.

`world-stb-shared.tracy` then passed without diagnostics after sharing one STB
chunk/frame decode between foreground and background. All 400 derived tile
files match the previous capture byte-for-byte, in addition to 18 diagnostic
PNGs across six maps. Median `Terrain build / texture bake` fell from 20.690 to
15.098 ms (415 samples each). The previous separate STB zones summed to 8.041 s;
the new combined zone sums to 4.879 s, including its nested background decode.
Do not add that nested zone again or interpret worker sums as wall-clock savings.

`world-detail-cache.tracy` validates the inactive-GPU-cache return path and the
opaque ground morph / ground-clearance clipping changes. It passed without
diagnostics. The scripted return produced no extra `World detail prepare` event;
only initial loading and loading after explicit invalidation built the map
(two events total). `World inactive detail bytes` and `World detail cache hits`
plots report the cache, separate from the active scene. A six-map ordinary
Release route additionally recorded five cache hits and one rebuild on return,
with 47,344,936 bytes retained; the oversized sixth map was not cached.
