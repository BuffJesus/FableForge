# Debug editor functionality audit

2026-09-27. Read-only investigation of FableWin's editor versus the current
`D:/Code/FableForge-nav` checkout (`feat/nav-thing-lines`). This is a bounded
first audit, not a count of every debug feature or a promise of parity. No game
files or implementation were changed. The navigation lane was active during
the audit; its implementation status can advance independently of this report.

The strongest additional opportunities are **fractal terrain over multiple maps,
preferred-path painting, camera-passability painting, linked track authoring,
and minimaps containing building silhouettes**. Navigation and terrain baking
are substantial existing implementations with remaining integration/parity work.
Some apparently missing debug features are actually inactive: the minimap-zone
brush reaches an empty function in this executable.

## Evidence and confidence

All native addresses below are **FableWin debug virtual addresses**, not retail
addresses. Executable: `D:/Documents/FableTLC/debug_build/FableWin.exe`, SHA256
`A9D6D0977D9D7FC8242DA4292AE7ED92E926CA80364A95C003D9B9F689E41845`
(identity recorded by the navigation lane). Symbol discovery used
`ghidra_out/fablewin_pdb_names.tsv` and `fablewin_editor_symbols.tsv` in that
evidence checkout. Names alone are not evidence of functioning behavior.

Evidence abbreviations, all local:

- **A:** `build/debug-feature-audit-evidence.c`, fresh targeted native decompiles.
- **B:** `build/debug-feature-audit-extra.c`, fresh targeted native decompiles.
- **N:** `build/nav-generator-evidence.c`, current lane's native navigation exports.
- **T:** `D:/Documents/FableTLC/ghidra_out/terrain_bake_decomp.txt`.
- **L:** `D:/Documents/FableTLC/ghidra_out/leveleditor_decomp.c`.
- **S:** `C:/Users/Cornelio/AppData/Local/Temp/claude/D--Documents-FableTLC/6bcb0168-d4c1-4763-81d4-5255088b0e5f/scratchpad`;
  specifically `navinit.c`, `navlines.c`, `navlines2.c`, `navlines3.c`.

Fresh exports used the existing `DecompAt.java`, Ghidra project `FableTLC`,
program `FableWin.exe`, `-readOnly -noanalysis`; logs are alongside A/B. Build
artifacts are not durable source, so the addresses and observations are retained
here. High confidence means an inspected body establishes the named behavior;
it does not imply a completed serialization trace or in-game proof. Searches
covered `src`, `gui`, and `libs/forgecore`; a missing dedicated editor action does
not preclude hand editing a TNG or using older external tooling.

## Capability inventory

| Capability | FableForge status | Native evidence and practical value | Confidence / next dependency |
|---|---|---|---|
| Fractal landscape generation across editable maps | **Missing dedicated workflow** | A: `GenerateNewFractal` `0x0203ccd0`, `ApplyFractalToMaps` `0x0203cd70`, world-map application `0x02974020`; B: map application `0x029a8e20`, `NewFractal` `0x029a81c0`. Produces a shared height field sampled in world coordinates, useful for coherent terrain across map boundaries. | High for application behavior; recover generator/noise parameters and UI defaults before implementation. |
| Preferred-path brush | **Partial: format API exists, editor brush missing** | B: `CEditMap::EditSetPrefNav` `0x029a96f0` changes a cell flag and overlay. N: `GetMapNavigationAreaUpdateInfo` `0x01c8f390` assigns preferred cells cost 0 and ordinary passable cells `0x80` (blocked `0xff`). Useful for directing AI along authored paths. | High for native writer/cost input. Need brush undo, preview, regenerated cost propagation, and behavioral fixture. |
| Camera-passability brush | **Missing dedicated API/UI in audited paths** | B: `CEditMap::EditSetCameraPassability` `0x029a9940` changes a distinct cell flag and updates overlay. Allows authors to control camera traversal separately from creature walkability. | High for writer, medium for full authoring contract. Trace serialized cell field and camera consumer before choosing file byte. |
| Linked tracks and track editing | **Missing dedicated GUI workflow** | B: `FinishLinkingTracks` `0x020302d0` handles link state, validates/assigns track names, and updates displayed track length. Native symbols also expose start/continue/flip/delete links and camera preview. | High for finalization; other actions are leads until their bodies and TrackNode persistence are traced. |
| Precalculated navigation route targets / linked route markers | **Uncertain scope; no dedicated action found** | B: `CTCPreCalculatedNavigationRoute::SetPThingNavigationRouteIsTo` `0x02034270` assigns a thing reference. This is a separate system from `CThingTrackNode` tracks. | Medium for reference setter, low for complete editor workflow. Trace setter callers plus `OnSerialise` `0x026c1580`, `SaveRoute` `0x026c1760`, `LoadRoute` `0x026c19f0`. Do not conflate these with camera tracks or generic markers. |
| Engine-style minimap generation with building silhouettes | **Partial: current map image bake is an approximation** | B: `CRegionMinimap::PopulateRegionMinimap` `0x02186790` classifies terrain by strongest theme, stores lighting, and rasterizes buildings using physics intersections. Current `src/worldedit.cpp::bakeMinimapTexture` uses ground albedo, hillshade and a circular alpha fade. Buildings do not enter that function. | High. Need native theme palette/output builder, world-region extents and physics meshes; reusable collision work can supply inputs. |
| Whole-map navigation regeneration | **Partial; active navigation lane** | S: `GetMapNavigationAreaInit` `0x01c905d0`; N: update info `0x01c8f390`, node initialization `0x032851b0`, area-clear test `0x0328c7e0`, region setup `0x0328f270`. Includes object lines, layers, switchable blockers, detail areas and cost inputs. | High native evidence; follow `HANDOFF_NAV.md` for latest integration state. Existing leaf coverage is not exact node parity. |
| Collision outlines used by navigation | **Partial: real diagnostic/extraction implementation** | S: physical thing getter `0x01f21d20`; physics mesh-object getter seed `0x017cbe97` transforms stored 2D line endpoints through its matrix. `src/navlines.cpp` and `src/cli/nav.cpp` already implement extraction/comparison. | High for getters. Remaining value is generator/visualization integration and exact physics/quest/object selection, not a second extractor. |
| Terrain/static-map bake | **Implemented substantially; integration and parity remain** | T: `CEngineLandscapeMap::GenerateStaticMapEntry` `0x02cca940` builds LOD map and patches and writes compressed foreground/background data; local-detail entry `0x02e3bf00` is separate. Existing `stbbake`, `stbheightbake`, `src/stbterrain.cpp` and `src/lodbake.cpp` cover major pieces. | High. Per-map forgecore orchestration is explicitly on the roadmap. Do not label all terrain baking missing. Water/shore and foliage generation need separate scope. |
| Minimap zone paint/show/clear | **Inactive native write path; not a verified missing feature** | A: world brush `0x02970b20` iterates a circular footprint, checks editable maps, converts coordinates and calls `CEditMap::EditSetMinimapZone` `0x029a9930`. That target is empty. | High, confirmed against executable bytes below. Do not prioritize a replacement solely because the dialog/symbols exist. |
| Native semantic thing validation before saving | **Uncertain parity** | T: `FindInvalidThing` `0x0203ff10` searches for the first invalid thing, globally for map `-1`, otherwise filtered by map. `SaveLevel` `0x020377e0` invokes it. Could expose invalid object links/definitions before deployment. | High for search/call, unknown actual validity rules. Trace `CThingFilter_IsValid` and compare existing validators before defining a new checker. |
| Level merge with UID/reference replacement | **Partial/uncertain parity; mod merging already exists** | L: `CEditLevelMerger::MakeIDReplacementsForOurs` `0x02b291c0` walks text replacement pairs and selected thing records, performing string replacement. Useful lead for reference-preserving copy/merge semantics. | High for inspected text replacement, low for full conflict policy. `gui/mods.cpp`, `tools/test_mods.py` and roadmap document existing mod merging; audit its reference handling before calling this missing. |

## Important behavioral details

### Fractal generation is more than randomizing the selected map

`ApplyFractalToMaps` first calls `Backup(true)`. World-map application iterates
map indices beginning at 1, checks a virtual eligibility predicate and
`IsMapEditable`, applies the same fractal object, marks each changed, and invokes
a final virtual update. The eligibility virtual and final update have not been
independently identified here.

`CEditMap::EditGenerateFractal` visits the full height-map dimensions, converts
each local coordinate to world coordinates, samples `GetFractalHeightAt`,
multiplies by the supplied scalar, then **sets** ground height. The inspected
body does not add noise to the existing height. The shared world-coordinate
sampling is the key seam-preserving behavior to reproduce.

`NewFractal` accepts six scalar arguments, a bool and two more scalars. It stores
the bool/two conditional values and two other parameters, calls `NewFractalData`
with three doubles plus a scalar, and marks data dirty. Exact names, noise
algorithm, seed semantics, falloff curve and clamps remain unverified. Next
target bodies: dialog generation `0x028e0350`, `NewFractalData` `0x029a70e0`,
`GetFractalHeightAt` `0x029a7290`, `PostProcessRescale` `0x029a7140`.

### Preferred-path and camera flags are not serialized byte offsets

Native in-memory height-cell byte `+9` uses bit `0x10` for preferred navigation
and bit `0x08` for camera passability. Both setters compare old/new values before
updating the overlay and flag. These offsets must **not** be copied into LEV
files: native memory layout differs from the serialized 21-byte cell record.

Forge already reads/writes preferred paths at serialized byte 20 in
`libs/forgecore/src/lev.cpp:278`. `src/leveledit.hpp::Brush::Mode` and
`gui/editor.cpp` offer Raise/Lower/Flatten/Smooth/Walkable/Blocked/Theme, without
a preferred-path or camera-passability brush. Preferred-path authoring is thus
a promising small extension to existing infrastructure, provided navigation
costs are updated consistently.

### Native minimaps contain semantic terrain and building coverage

`PopulateRegionMinimap` samples the strongest engine theme at image-derived
world coordinates and maps a field at theme-def `+0x98` into eight output
classes plus default zero. The classes' friendly names and palette colors were
not recovered here. A class table controls whether terrain lighting is applied.

It then enumerates buildings, requires a physics mesh, obtains transformed
bounding-box edges/centre and bounding info, clips the candidate raster area,
and tests a vertical segment from ground Z to ground Z + 100 against the physics
mesh. A hit writes class 8. This establishes real building coverage rather than
merely an AABB rectangle fill; the precise bounds arithmetic still merits
disassembly/fixture confirmation. The current Forge bake takes LEV/theme input
only, so this is a concrete missing input and behavior.

Next native targets: `BuildRegionMinimapTGA` `0x021886b0`,
`CreateRegionMinimapTGAFile` `0x02188b80`, illumination `0x021878d0`, and region
extents. `docs/engine/MINIMAP_RE.md` in FableTLC primarily concerns the retail
runtime renderer and should not be mistaken for a completed authoring study.

### The minimap-zone brush is a false positive

The entire body at `0x029a9930` is a stack-frame prologue/epilogue and `ret 12`.
Read directly from FableWin, PE file offset `0x0121ad30`:

```text
55 8b ec 51 89 4d fc 8b e5 5d c2 0c 00
```

The surrounding world brush and GUI names survive, but this setter writes no
zone. This finding does not prove every historical version or alternate zone
path was inactive. It proves this inspected path cannot establish a missing
working tool in the available executable.

Similarly, the retail naming corpus's `GenerateNewFractal` call label occurs
inside a crime-persistence body (`naming_batches5/batch_53.txt`, function
`0x007b3760`, strings `PendingCrimes` etc.). That label was excluded. Authoritative
debug addresses plus actual bodies avoid treating an old propagated name as
functionality evidence.

## Suggested follow-up order

1. Finish the active navigation integration; preserve its fixture/node-parity
   gates. This unlocks reliable preferred-path behavior and better reuse of
   collision data.
2. Add a preferred-path brush using the existing LEV field, with undo/overlay
   and a fixture proving changed route preference. First confirm cost update
   behavior rather than saving a flag that existing cached nav ignores.
3. Recover the remaining fractal generator/dialog bodies, then prototype a
   two-map world-coordinate preview with reversible application and terrain bake.
4. Recover native minimap palette/output and add building silhouettes; compare
   the same region against debug output before claiming an engine-style match.
5. Trace camera-passability serialization/consumers and linked track/route
   persistence. These are separate authoring features, not navigation aliases.

Further symbol-backed leads (not body-proven findings in this audit): sound and
reflection brushes, scripted map brushes, noise pen generation, filler-map
fitting, track camera preview, and additional entity validity rules. Each needs
a targeted body/caller/persistence trace before promotion to a feature backlog.
Water already has a substantive separate investigation in FableTLC
`docs/engine/WATER_RE.md`; the roadmap records a separate unmerged water branch.
Avoid duplicating that work or calling its presence verified in this checkout.

No tests were run for this documentation-only audit. Static source comparison,
targeted native decompilation, and direct byte confirmation establish the
observations; no new retail/debug authoring fixture was generated or played.
