# EgoCore particle update review, 2026-09-29

## Current status: sprites, bounded meshes and light volumes

Selected-effect playback now includes ordinary mesh particles alongside sprites.
Mesh renderers retain their system index and the completed shared emitter/update
configuration, so names can repeat and a mixed renderer does not double-spawn.
Mesh-only systems use the same deterministic bounded population. Legacy export
sizes/colours/counts remain unchanged. Mesh ID zero remains an absent-model sentinel.

The mesh viewport uses immutable geometry buffers, diffuse textures, per-axis size,
colour/alpha/size fades, optional centring and the reference (-Right,-Forward,Up)
basis. Authored payload sphere bounds take precedence over descriptor fallback;
missing/unusable bounds fall back to geometry with a warning. Native-confirmed XYZ
initial turns and fixed-axis quaternion increments are supported (evidence below).
Transparent mesh/sprite commands are sorted together; depth tests remain on
and depth writes off. Mesh additive blending includes sampled texture alpha,
unlike the sprite path. Frame-atlas sampling includes half-texel bounds to prevent
adjacent-frame bleed. Limits: 64 cached meshes/128 MiB geometry, 4096 submitted
mesh instances and two million triangles per frame, existing 256 MiB textures.

Random initial and spin-axis mesh orientation now use native-confirmed axis/turn
branches with a separate deterministic preview RNG. The retail bank contains
383 mesh renderers requesting random initial orientation and 390 requesting a
random spin axis. Direction/game orientation remains outside this pass.
The particle preview suite now passes 76 synthetic checks plus that retail
flag audit. Existing sprite RNG snapshots remain unchanged.
The browser now also exposes EgoCore-style 0.25x/0.5x/1x/2x playback speed,
Loop/Once, adjustable duration and a seek timeline. Seek resets and simulates
fixed ticks to the requested time, retaining the fractional tick for resumed
playback. Retail `BRAZIERFIREFINAL` GUI checks cover speed, loop and timeline;
the core test compares seek with explicit stepping.
The 3D ground grid from EgoCore's FX viewport is now available beside Forge's
background colour. It is drawn first, in the same render target as particles,
on the Fable XY plane at one-unit intervals. The toggle defaults off; a retail
`BLOOD_POOL` capture and pixel check cover on/off rendering and exact reset.
The FL10 WARP renderer check now passes simulation-produced random initial and
spin quaternions into a held asymmetric mesh: the initial image differs from
both clear and identity, a later tick changes visible pixels, and reset
reproduces the initial image exactly. The harness supplies the renderer's
required background RGB explicitly. A retail `CHEST_OPEN_01` automation run
confirms a submitted mesh changes orientation and resets at one tick, but its
captured viewport is too faint to prove pixel-level rotation; the WARP fixture
is the visual regression for this branch.
The Effects viewport also offers Frame current, which fits the currently
simulated particles instead of the default 90-tick path. A retail
`ACTIVATE_SKILL_01` capture at one tick increases visible FX pixels from 495
to 798 on the dark background; `tests/ui/effect_frame_current.txt` and
`tools/test_effect_frame_current_pixels.py` check this camera behavior.

Limitations: direction/game-driven mesh orientation; geometry-derived sphere
only when authored bounds are unavailable/unusable; external size parameters, trails, flicker, animated skeletons,
scene-dependent passes and actual particle-light illumination. Animated light volumes
now visualize native colour/radius independently of particle emission; see below.
Persistent-single-sprite/mesh combinations are skipped with a warning. No in-world
animated effect rendering, bank writing or native-fidelity claim.

Validation: 70 simulation checks, 21 authored-bounds checks including retail mesh435, exporter suite and all 1165 retail effects
(3220 sprites, 1189 mesh systems, 150 lights unchanged). Persistent FL10 WARP test
covers centring, independent axes, quaternion renderer transform, material indexing,
mesh/sprite alpha differences and composition, atlas boundaries, invalid data,
resource caps and cleanup. Native BLOOD_POOL renders one two-triangle mesh with
1215 visible pixels; 15-to-30 ticks changes 158 pixels, pause/restart exact. The
initial low camera angle made ground effects edge-on; mesh framing now starts at
0.55 radians. Native playback/force/browser regressions pass; original sprite
pixel check still changes 5488 pixels with exact pause/restart.

Logs: `build/build-mesh-particles-final.log`, `build/effect-meshes-native-final.log`,
`build/effect-mesh-pixels.log`, `build/effect-mesh-regressions.log`,
`build/effect-mesh-sprite-pixels.log`; core/parser audit under
`build-review/test-meshparticles.log`, `test-meshparser.log`,
`meshparticles-retail.log`. Screenshot: `walkthrough/w18_mesh_particles.png`.

Authored-bounds follow-up passes native BLOOD_POOL with its authored sphere and
unchanged pixel results (`build/authored-bounds-native.log`,
`build/authored-bounds-pixels.log`). Fixed-axis rotation passes DAZED01STAR's actual
quaternion-change assertion plus render/paused/restart checks: 280 visible pixels,
485 changed pixels, exact paused/reset frames. Pool and sprite replay regressions
remain unchanged. Logs: `build/build-mesh-orientation.log`,
`build/mesh-orientation-native.log`, `build/mesh-orientation-pixels.log`,
`build/mesh-orientation-pool-pixels.log`, `build/mesh-orientation-sprite-pixels.log`.

Light-volume follow-up retains all CPSCLight fields and uses separate native
component timers, linear colour/radius interpolation, cosine fade windows and
max-axis system scaling. A toggle shows three clipped wire rings and a centre
point, with a clearly stated distinction from scene illumination. Runtime position
parameters fall back to local origin and global alpha to 1. The inspector exposes
lifetime, start/respawn delay, colour stages, radius range and internal enablement.

41 dedicated checks pass; retail decoding remains 1165 complete effects and 150
lights. Native BURNING_HANDS_POWER_UP verifies active/expired states, hide/show,
pause and restart. Overlay toggle changes 1319 pixels; tick15-to30 changes4060,
with exact pause/reset. Sprite/pool/rotation regressions and pixel checks pass.
Logs: `build/build-light-volumes-final.log`, `build/effect-lights-native-first.log`,
`build/effect-light-pixels.log`, `build/effect-light-regressions.log`.
Screenshot: `walkthrough/w22_light_volumes.png`. Native formula evidence is in the
component-light section at the end of this document.

The sections below record the earlier stages in order.

Reviewed upstream `eeeeeAeoN/EgoCore` at `55bdc10` (tag `29.9.26`),
"Added a particle simulator and renderer." The inspection checkout was fetched;
its working tree was not updated. Source:
https://github.com/eeeeeAeoN/EgoCore/commit/55bdc10

The new `EgoCore/Particles/ParticleSimulator.h` uses deterministic seed 1337,
30 Hz fixed simulation ticks, sub-tick interpolation, bounded particle pools,
initial bursts and continuous emission. It handles normal updates, orbit,
attractor and persistent single-sprite components. System/component
Enabled flags gate simulation. `ParticleRenderer.h` adds sprite/mesh rendering,
particle blend modes and operations, with transparent depth writes disabled.
`ParticleProperties.h` integrates playback and preview controls.

This is an implementation reference, not proof of complete native-engine parity.
In particular, copying the full header into Forge would couple its own parser,
resource cache, DirectX types and UI to Forge's renderer. Forge currently retains
only static-export fields in `src/effects.hpp`; animated preview needs an explicit
simulation model, retained component parameters, lifetime/resource budgets, blend
passes and deterministic playback tests before it can replace static proxies.

Concrete correction adopted now: Forge previously consumed but ignored system
and component Enabled flags. It now parses disabled payloads to preserve stream
alignment while excluding their sprite/mesh/light proxies and preventing disabled
render/update/emitter components from overwriting active sprite properties.
No bank writer or installed data changed. Standalone `effects::decode` enables
synthetic fixtures without a globally cached installed bank.

Validation: synthetic disabled system, disabled renderer, active renderer followed
by disabled renderer, and truncated terminator checks pass. Retail `forge effects`
walk: 1165 fully parsed, zero partial; 3220 active sprite systems, 150 lights,
1189 mesh systems (`build/effects-enabled-audit.log`). Full animated preview remains
unimplemented; the user requested that this upstream update inform continued work.

## Follow-up source audit and implemented inspector

Source checkout: `C:/Users/Cornelio/Documents/EgoCoreInspect/EgoCore-git`, inspected
using `git show 55bdc10` without changing its working tree. The selected-effect
inspector is now implemented and UI-tested in Assets > Effects. It is not an
integration of the animated simulator.

Closer inspection corrects the earlier spline claim: the simulator finds
`CPSCSpline` but does not use it. `StayWithEmitter` is stored without attachment
updates. Ground collision uses Z=0 and friction 0.85. The inspected renderer has
no light, decal or trail rendering paths. These are reasons to report supported
components explicitly, not to claim complete native parity from this reference.

The practical next implementation is animated preview of the selected effect:
Play/Pause, Restart and one-tick Step, initially ordinary and persistent sprite
systems. The 690-line simulator is separable from the 2,176-line renderer, whose
bank/mesh/UI dependencies should not be copied into Forge. Retain the missing
emitter timing, burst, shape, velocity, physics/lifetime, sprite timelines/frame
data and blend operations in Forge's checked decoder. Add deterministic bounded
simulation and a Forge sprite pass with appropriate blending and no depth writes.
The upstream parser's unchecked EOF/unknown-component handling must not replace
Forge's checked decoding. Test fixed-seed snapshots, delayed emission/lifetimes,
disabled components, pause/step/reset and resource limits before enabling playback.

In-world attachment, scene collision, lights/trails/decals and effect editing or
writing remain separate unfinished work. No animated preview was implemented in
this inspector pass.

## Selected-effect playback continuation

Assets > Effects now has an isolated animated sprite preview with Play/Pause,
Restart, one 30 Hz tick Step, orbit and zoom. Forge retains checked emitter,
normal-update and sprite parameters, runs a deterministic bounded CPU simulation,
and draws camera-facing sprites through an independent D3D11 pass. This adapts
useful behavior from the pinned EgoCore reference; it is not a wholesale import
or a claim of native-engine parity. Adapted component layouts retain MIT notice.

Supported behavior includes bursts, continuous emission, delays, finite lifetimes,
normal velocity/acceleration/gravity/drag, colour and size evolution, sprite frame
animation and persistent single sprites. Sprite texture decoding crops allocation
padding and distinguishes sheets from repeated raw mip chains. Unreconstructable
animations are reported instead of displaying an entire sheet as one particle.

Budgets: 64 systems, 2048 particles per system, 4096 total, bounded elapsed catch-up,
and a 256 MiB preview texture cache. Each selection/reset restarts seed 1337.
Simulation pauses while another asset tab is active. Unsupported active behavior
is listed in the viewport: meshes/lights, orbit/attractors/splines, scene collision,
attachment, trails and other unsupported flags. There is no sub-tick interpolation,
in-world animated rendering, effect editing or bank writing in this pass.

Visual verification caught and corrected a shader mismatch: the reference sprite
pass uses MODULATE2X RGB and, for additive modes, premultiplies by particle alpha
only. Multiplying texture alpha into additive RGB made smoke/fire too dim. The
WARP regression includes a partially transparent additive texture, conventional
alpha, add-smooth, frame selection, aspect ratio, reuse and resource bounds.
Copyright and MIT notice are preserved in `vendor/EgoCore-LICENSE.txt`.

Validation: core parser tests, 25 CPU preview checks and sprite-frame tests pass;
all 1,165 retail effects decode fully with unchanged system totals. Native
`effect_playback.txt` and `effect_browser.txt` pass, including actual button
clicks. `tools/test_effect_playback_pixels.py` verifies zero changed pixels while
paused and after reset/replay; advancing 30 to 60 ticks changes 1,055 pixels.
Candle and waterfall screenshots additionally verify framing and animated retail
textures. Evidence: `build/effect-playback-final-native.log`,
`build/ui/effect_playback_*.png`, `build/particle-texture-retail-probe.txt`, and
`build/particle_renderer_warp_smoke.cpp`. These are isolated previews, not runtime
retail-scene appearance comparisons.

## Orbit, attraction and sprite correctness follow-up

The checked decoder now retains active `CPSCOrbit` and `CPSCAttractor` parameters,
with a count-before-allocation check for attractor points. Disabled components
and systems do not enter the preview. Separate authored start/end sizes preserve
legacy static export sizes while applying the reference renderer's maximum-axis
scale in the preview (including its nonpositive-scale fallback).

The simulator implements the reference's planar orbit force for emitted sprites,
analytic orbit radius/expansion for persistent single sprites, and attraction to
the first authored point with radius/falloff controls. It caps each force type at
16 components per system. This is the upstream preview approximation: specialized
orbit types, axis/cycle/squeeze/random modifiers, external centres and additional
attractor points are explicitly reported, not implemented as native equivalents.

Additional corrections from source review: ordinary sprites hold StartSize when
size fading is disabled, and use the authored interpolation/minimum when enabled;
single sprites retain their distinct cosine size fade and fixed-angle behavior.
Modes 0/1/2 alpha-test below16/255; additive modes retain their previous semantics.
WARP tests verify threshold pixels and zero-texture-alpha additive behavior.
Automatic framing now uses simulated sizes and texture aspect. Selecting the same
effect again no longer resets paused time/camera; Refresh and Restart still reset.

Validation: 50 CPU simulation checks, parser checks, and native
`effect_forces.txt` / `effect_playback.txt` pass. `BUTTERFLY_BLUE` moves from
(4.730039,0.504708,2) at30 ticks to(4.534177,0.951528,2) at60;
`EXP_WHISP_BIG` increases11 to23 particles with an active constant-force attractor.
Stripped-force control simulations prove motion changes due to force, not just
emission (`build/particle-force-retail-examples.txt`, `particle-force-retail-probe.txt`).
Viewport pixels change271 and2,549 respectively. Updated normal playback changes
5,488 pixels while pause/reset remain identical. Native regression log:
`build/continuation-native-regressions.log`. In-world particles, mesh/light playback,
interpolation and effect editing/writing remain unfinished.

## Native orientation evidence

Read-only disassembly of `D:/Documents/FableTLC/debug_build/FableWin.exe`
(SHA256 `a9d6d0977d9d7fc8242da4292ae7ed92e926ca80364a95c003d9b9f689e41845`)
corrects several approximations in pinned EgoCore `55bdc10`. Detailed addresses
and the bounded implementation policy are recorded in
`build/native-particle-orientation.txt`.

`CPSCUpdateNormal::ReadBinary` reads initial XYZ directly into offsets
`+0x34/+0x38/+0x3c` at `02f44fdb/02f44fe9/02f44ff7`; min/max angular increments
go directly to `+0x40/+0x44` at `02f45005/02f45013`. `UpdateAddParticle`
constructs X/Y/Z axis quaternions at `02f428d6/02f4290c/02f42942` and multiplies
`(Qx * Qy) * Qz` at `02f42969/02f42970`. Native `CQuaternion::operator*`
(`0339fa00`) uses the ordinary Hamilton product, independently checked from
its component operations.

Angles are **turns**: `SetFromAxisAngle` (`0339fd80`) multiplies the angle by
0.5, then calls `GFFastSin/GFFastCos` (`02dcfc30/02dcfcd0`). Their multiplier at
`040800a8` is 1024 and their cyclic table mask is 1023, giving one full cycle
per input unit. There is no degrees/radians conversion in the binary reader.
The sampled angular increment is passed directly to the delta quaternion at
`02f42a75/02f42ac1`, without a time division. `UpdateOrientation` (`02f410d0`)
applies `Q *= delta` once per particle update. A zero nonrandom axis produces
an identity increment (`02f42acb`); it does not select a random axis.

The selected preview now implements fixed authored XYZ initialization and
fixed-axis increments in those units at its existing 30 Hz tick. It normalizes
quaternions for numerical stability and reduces angles modulo a turn before
trigonometry. It reuses the existing sampled speed without adding RNG draws,
so existing sprite positions, angles and emission randomness remain unchanged.
This is not a claim of native RNG or full engine scheduling parity.

At the time of the fixed-axis pass, random initial orientation, random spin
axes and direction/game-driven orientation remained unsupported. The random
initial and spin-axis branches are now implemented as described above;
direction/game-driven orientation remains unsupported.
EgoCore instead uses only InitialRotationZ about the spin axis, assumes radians
and divides angular speed by 30, and randomizes a zero axis. Those choices are
not used as native evidence. Existing sprite angular behavior is intentionally
outside this mesh refinement.


## Native component-light evidence and isolated volume preview

`CPSCLight` is a component-level light, not a light attached to each emitted
particle. Pinned EgoCore `55bdc10` retains its binary fields but implements no
light playback in `ParticleSimulator.h` or `ParticleRenderer.h`. This extension
therefore follows the debug binary directly. It draws no scene illumination:
`Simulation::lights()` supplies light-volume parameters for the inspector.
Light states have independent timers and never consume sprite/mesh RNG or the
particle population budget. There are at most 64 retained active light states.

Native anchors (`debug_build/FableWin.exe`, PDB names in
`ghidra_out/fablewin_pdb_names.tsv`):

- `CPSCLight::ReadBinary` **02f54290**; `Update` **02f52620**;
  `PreparePrimitives` **02f52860**, continuing through **02f530b3**;
  `SmoothFadeValue` **02f532d0**.
- Component timer, delay and alive fields are `+0x0c`, `+0x10`, `+0x14`;
  one primitive handle at `+0x58`. Prepare constructs exactly one
  `CEnginePrimitiveLight` at **02f5308b**; there is no emitted-particle loop.
- `GetCurrentPosition` **02f104c0** queries the position parameter, then the
  component user-point fallback. Light uses base `GetUserPoint` **02f103a0**,
  which returns false; absent parameters consequently resolve to zero. System
  local/world transforms follow. Isolated preview has neither runtime parameter
  bindings nor attachment transforms and explicitly uses local origin.
- `GetGlobalAlpha` **02f0a550** reads system `+0x84`; its constructor
  **02f151c0** initializes it to one at **02f152db**. Isolated preview uses one.
  `ScaleParticleDimensions` **02f26dc0** applies the largest system scale axis
  only when the system's ScaleParticles flag is set.

Serialized body offsets, excluding the component envelope (71 bytes total):

| Offset | Field |
|---|---|
| 0 | u32 position parameter |
| 4, 8, 12, 16 | f32 life seconds, respawn delay, start time, timeline seconds |
| 20, 24 | f32 start/end world radius |
| 28, 32, 36 | i32 attenuation factor, fade-in end, fade-out begin |
| 40, 44 | f32 radius fade minimum; BGRA colour fade minimum |
| 48?58 | byte bools: radius fade, use life, internal enabled, respawns, colour fade, use start/mid/end colour, use fade colour, use timeline, initialized timer |
| 59, 63, 67 | BGRA start/mid/end colour |

Legacy `LightSystem::radius` and `colour` still mean the stored start values, so
static exports retain their existing data. Component/system Enabled gates stay
separate from the retained internal LightEnabled field.

With UseLife disabled, Update marks the component alive without advancing its
clock. Otherwise it initializes the clock to `-nearest(startSeconds*tickRate)`;
negative ticks delay activation. The life threshold is truncated
`lifeSeconds*tickRate`, and expiration uses **strictly greater than**, not equal.
Expired lights respawn only after the independent delay threshold, resetting the
life and delay clocks. Timeline support freezes the clock when its absolute time
reaches TimelineSeconds. The branch at **02f527a7** skips both `alive=true` and
`timer++`; a fresh timeline-zero light stays inactive, while a previously active
light can remain alive with frozen parameters. Serialized InitializedTime is
retained for inspection, but preview reset deliberately starts fresh runtime
state. UseFadeColour is retained but is not read by this Update/Prepare path.

Prepare uses normalized component age, or zero with UseLife disabled. Colour is
piecewise **linear** start?mid?end, not the sprite 511-step spline. Missing start
uses mid/end/white; missing end uses mid/start/white; missing mid averages the
endpoints. Radius interpolates start?end. Fade boundaries are integers divided
by 1000. Fade-in/out multiply system global alpha by the cosine S curve
`0.5 - 0.5*cos(pi*x)`. When this factor F is below one:

- ColourFade multiplies RGB by `minimumRGB + (1-minimumRGB)*F`, then clamps each
  result upward to minimumRGB.
- RadiusFade multiplies radius by `minimumRadius + (1-minimumRadius)*F`.

System dimension scale follows the radius fade. The second distance passed to
`CEnginePrimitiveLight` is `radius*(AttenuationFactor+1)/1000`; the actual scene
attenuation shader is not recovered here. Native emits clamped RGB, so stored
colour alpha is not treated as light intensity. Preview DrawLight alpha is one.
The volume overlay must be labelled as a diagnostic, not simulated lighting.

Preview safety: nonfinite components are omitted with a warning. Times are
bounded to 0?3600 seconds, radii to 0?100000, scale to 0?1000, fade boundaries to
0?1000, radius minimum to 0?1, and attenuation factor to -1?1000. Bounded authored
values are reported. Zero lifetimes avoid division by zero; native floating-point
edge cases and cosine lookup-table bit parity are not claimed.

`tests/test_lightpreview.cpp` checks field retention, outer/internal enabled gates,
emission independence, start delay/lifetime/respawn boundaries, timeline freezing,
linear colours, fades/minima, dimension scale, deterministic reset, hostile inputs,
component limits, and unchanged sprite RNG/populations when lights are added.
The retail probe `build/light-preview-retail-probe.txt` reports **1165 complete
records, zero partial records, 150 light components**, all internally enabled.
No retail effect contains only lights. A mixed-renderer fixture is
`BURNING_HANDS_POWER_UP`: tick15 radius6.25/RGB(1,1,.5); tick30 radius7.15341/
RGB(.953788,.953788,0). Pause and toggle the volume overlay to isolate its pixels.
