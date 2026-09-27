# Navigation lane, 2026-09-27

Checkout: `D:/Code/FableForge-nav`, branch `feat/nav-thing-lines`, starting commit
`8178e9b`. Workflow procedures are committed at `a9b21a3`; the experimental
navigation implementation is committed at `91498be`. Game-install files were
not changed. Both branches were validated and merged at `7def801` in the owned,
isolated checkout `D:/Code/FableForge-verify-ow` (overworld source `1a60f09`).
Local `main` and this lane were fast-forwarded to validation commit `8b5e587`.

## Setup

Used the installed Codex `skill-creator` procedure and its validator. Inspected
built-in, user/Claude, plugin and project skill discovery information. There was
no existing project build, format or Ghidra skill to reuse. Compression skills
rewrite prose and are inappropriate for preserving investigation evidence here.

Added `.agents/skills/fableforge-dev`, `fableforge-format-io` and
`fableforge-debug-editor-re` with minimal AGENTS routing. These replace repeated
build/check selection, format/evidence discovery and debug-editor investigation
setup. All three pass `quick_validate.py`. Development and editor-RE procedures
were applied to the continuation below. No scripts or global settings added.

No separate release skill: `docs/RELEASE.md` already supplies the procedure.
No per-format skills or duplicate native-tooling skill. Native VC7.1 traps belong
to the native lane; shared FableTLC `docs/pipeline/GOTCHAS.md` already has native
tooling history. Basic worktree/shell/CRLF/lock precautions are in the dev skill.
Keep these skills on this branch until normal integration; do not make divergent
copies in the main/overworld worktrees.

## First continuation milestone: unexplained detailed leaves

All 17 LookoutPoint and 325 BarrowFields layer-0 half-unit leaves now have a
qualifying parent cell. The prior unexplained counts were 17 and 14 respectively.

- LookoutPoint: the high-detail `BUILDING_GUILD_LO_POLY_01` in neighbouring
  GuildExterior overlaps the map. Local TNG-only enumeration missed it.
- BarrowFields: eight missing leaves came from an undersized chief-tent sphere.
  The engine uses half the AABB diagonal, not the farthest vertex from its centre.
- The other six belong to unit parent cells with corners inside the home-tent
  detail area. Refinement tests the parent, so a resulting child can lie outside
  the original box. The old diagnostic tested each child for box overlap.
- Mesh-created children now use their own `child:<definition>` tag when checking
  the detail flag. Children did not account for these two fixtures' missing leaves.

`buildHull` now uses the engine sphere calculation. `requestsHigherDetail` checks
node corners against half-open detail boxes. The read-only `nav-lines` command
includes touching-map candidates for detail areas and offers `--details` for
object boxes and any unexplained leaf coordinates. Collision-line scoring is
still local-map-only; no generator or install writer was changed.

## Evidence and confidence

FableTLC evidence checkout: `D:/Documents/FableTLC`. FableWin.exe SHA256:
`A9D6D0977D9D7FC8242DA4292AE7ED92E926CA80364A95C003D9B9F689E41845`.

- `CWorldMap::GetMapNavigationAreaInit` at `0x01c905d0`: searches overlapping
  Buildings/Objects, applies quest-or-null-quest filtering, checks the virtual
  high-detail flag and physics component, then clamps bounding-info boxes.
  Existing export: prior session scratch `navinit.c` under
  `C:/Users/Cornelio/AppData/Local/Temp/claude/D--Documents-FableTLC/6bcb0168-d4c1-4763-81d4-5255088b0e5f/scratchpad`.
- `CPhysicsMesh::CalculateBoundingInfo`, `0x030f8770`: min/max centre and
  half-diagonal magnitude. Fresh decompilation: `build/nav-bounds-evidence.c`;
  magnitude-to-sphere setter disassembly in `build/nav-radius-disasm.log`.
- `CTCPhysicsBase::GetBoundingInfo`, `0x0246e470`: shape bounds when available,
  otherwise radius/position fallback. `build/nav-detail-evidence.c`.
- `CNavQuadTreeNode::GoToHigherDetail`, `0x03286830`: node TL/TR/BL/BR corners
  passed to the detail box's ContainsPoint; receiver direction verified in
  disassembly at `0x032868e5..0x03286945`, `build/nav-bounds-headless.log`.
- `C2DBoxF::ContainsPoint`, `0x017dcb98`: lower-inclusive/upper-exclusive comparisons,
  `build/nav-contains-evidence.c`. This boundary interpretation is from pseudocode.

Fresh queries used the existing `tools/ghidra_scripts/DecompAt.java` and
`DisasmRange.java` via `analyzeHeadless ... -process FableWin.exe -readOnly
-noanalysis`. Logs/exports are local build artifacts, not committed source.
The existing source comments also identify the navigation-line slicer addresses.

## Validation and remaining work

Built `forge`, `fableforge_tests`, `fableforge_lzo_tests`; both CTest tests pass.
New regressions distinguish a half-diagonal sphere from a farthest-vertex sphere,
parent versus child refinement, corner containment versus overlap, and boundary cases.

Reproduce retail diagnostics from this checkout:
`build/forge.exe nav-lines <LookoutPoint|BarrowFields|PicnicArea> --raw-verts --details`.
The first two have zero unexplained detail leaves and line precision 1.000;
PicnicArea has no half-unit leaves and retains line precision 0.984.
These are installed-data comparisons, not a pristine-install or in-game proof.

## Second continuation milestone: ground regeneration

Added experimental `forge::navmesh::generateGround` with explicit static lines,
detail boxes and switchable UID/line groups. It refines the tree, removes islands
unreachable from navigation anchors, builds neighbour links and assigns regions.
The existing production terrain generator remains the default. `forge nav-compare
<map>` collects geometry and compares the regenerated NULL-section ground layer;
it does not write the game install. Its raw-vertex default follows the native
getter; the older `nav-lines` command retains its explicit `--raw-verts` option.

Current installed-data comparisons (node keys include shape, type, blocked flag,
preference and switchable UIDs; neighbour comparisons ignore numeric node IDs):

| Map | Matched nodes | Generated-only / retail-only | Equal neighbour sets | Region partition conflicts |
|---|---:|---:|---:|---:|
| LookoutPoint | 1349 | 0 / 0 | 861 / 861 | 0 |
| PicnicArea | 855 | 0 / 0 | 536 / 536 | 0 |
| BarrowFields (ground projection) | 2224 | 0 / 0 | 1327 / 1327 | 1 |
| Greatwood_1 | 1231 | 0 / 0 | 723 / 723 | 0 |
| OrchardFarm (ground projection) | 1761 | 26 / 38 | 1052 / 1093 retail (1083 generated) | 0 on shared leaves |
| GuildExterior | 515 | 0 / 0 | 322 / 322 | 0 |

BarrowFields has two source layers; the new generator accepts one only. All
retail ground nodes now exist with matching type/cost/UID/blocked state and
neighbour sets. The former four extra nodes near the trader table/market stall
came from mistaking saved action point (40.197021,87.783203) for a region seed;
the separate seed input below resolves them. Region counts are 5 generated
versus 3 retail (including
region zero); retail region1 splits into generated regions1 and3. Upper-layer
connections are not modeled. Do not promote this path to the editor writer yet.

The Demon Door revealed a separate collision input: render mesh 3981 has no
physics hull, but CDoorDef12438 names closed collision mesh3992. Native
`CTCDoor::OnInitialActivate` at `0x0257d5c0` installs that closed mesh in editor
mode (`build/nav-door-evidence.c`). The collector now resolves it, reducing the
BarrowFields discrepancy from 8/14 to 4/6. Hull decoding now rejects malformed
chunks and oversized vertex/triangle counts. BIN exposes the original names
offset so component names can be resolved without a second names-file parser.

Further native evidence: `Initialise` `0x032851b0`, `IsAreaClear` `0x0328c7e0`,
`SetUpRegions` `0x0328f270` in `build/nav-generator-evidence.c`; static blockers
take precedence over switchable lines. Map-thing UIDs use the low 40 bits.
Current region/layer integration still needs wider parity checks. Segment
boundary behavior was subsequently recovered and verified below.
Touching-map selection does not cover oversized objects from distant maps;
generated switchable children without explicit UIDs are reported unresolved.
The collector follows exported render instances, so physics-only things that
never produce an instance remain unverified. Raw definition-tag scanning,
quest variants and upper layers remain limitations.

Validation: full build succeeds. The broad `tools/check_all.py --no-build` run
completed ALL PASS in `build/full-gate.log`; mod-corpus and Project Seasons
checks skipped for missing fixtures. That run preceded the final closed-door
lookup and hull validation changes. After those changes, rebuilt all targets,
reran CTest and the initial three comparisons. Synthetic tests cover blocker/detail
splits, anchored islands, switchable links, symmetric neighbours, serialization
round trips, invalid input and malformed hull counts. No in-game navigation
test has been performed. Integration was completed subsequently as recorded below.

The user-requested background audit is complete:
[Debug editor feature audit](DEBUG_EDITOR_FEATURE_AUDIT.md). It records five
promising gaps, partial implementations, uncertain leads and a confirmed empty
minimap-zone setter. It does not authorize implementing all those features.

## Third continuation: closed doors, region assignment and s&box review

Native `CNavSwitchableLeafNode::RemoveRegionZero` (`0x03282290`) always returns
false. The builder now retains unvisited door leaves as blocked, region-zero
nodes, including links to other surviving door leaves. This recovers the three
Demon Door leaves at y28.5 and all six previously missing BarrowFields nodes.
`MakeRegion` (`0x03282570`) propagates only to switchable neighbours; ordinary
`MakeRegion` (`0x032818b0`) propagates to every neighbour. `SetUpRegions` first
visits anchors in order, then repeatedly seeds unassigned normal nodes beside
assigned doors, root by root. Ported this sequence instead of assigning doors
to an arbitrarily numbered adjacent component. Tests cover no-anchor closed
doors, retained closed-door links and seeding from either side of a doorway.
Evidence: `build/nav-anchor-evidence.c`, and the existing native bodies in
FableTLC `ghidra_out/decomp_navmesh2.c`.

Broader read-only comparisons exposed the gaps in the table above. Greatwood_1's
former differences clustered around x37..47/y119..124 (resolved below);
GuildExterior's former missing half-unit leaves near (2.5,101.25) were resolved
by the seed correction below. BarrowFields' loose TNG is byte-identical to the WAD entry (SHA256
`FF0F5C0B18F4D3DE6F368C2A1E8634945BD7BC0022C6BCC81BE061DB96A4E73E`);
its LEV comes from the WAD. Graphics/defs and installed data still are not a
certified pristine corpus.

Inspected native segment/box intersection: endpoint containment plus four
edge tests with epsilon0.0001, rejecting parallel segment intersections.
The initial clipper's degeneracies were a known difference, subsequently fixed
using native execution as described below. Evidence: `build/nav-intersection-
evidence.c`, `nav-line-intersection-evidence.c`, `nav-segment-evidence.c`.

`nav-compare --details` now shows both sides of node differences, anchors,
neighbour mismatches and region splits. Temporary LEV extraction checks write
failures and rejects map arguments containing paths.

The requested background s&box review is complete:
[s&box inspiration](SBOX_MODDING_INSPIRATION.md). It extends the existing
FableTLC `docs/engine/IN_ENGINE_MODDING_ENVIRONMENT.md` with pinned source
evidence and concrete acceptance examples for annotations, bake dependencies,
package identities, presets, transactions, edit/play sessions and reload.

## Native geometry oracle and integration checks

Replaced generic clipping in the experimental generator with the native
endpoint/edge predicate. `C2DLineF::IntersectsWith` (`0x0324c6d0`) normalizes
directions, computes a float dot product/denominator and checks a computed
intersection against both segments with epsilon0.0001. Collinear overlap alone
does not count. Float rounding matters near parallel lines: `GFSqr`, square
root, inverse length, normalized components and dot products each round to
float; the numerator retains intermediate precision. Disassembly confirmed the
rounding boundaries, including `Dot` (`0x021e3ae0`) storing to a float local
before returning. The internal kernel is `libs/forgecore/src/navgeometry.hpp`.

Added reproducible optional verification:
`python tools/verify_debug_nav.py --debug-exe D:/Documents/FableTLC/debug_build/FableWin.exe`.
It compiles the actual C++ kernel with the owning worktree's compiler and runs
the debug executable's box-intersection routine under Unicorn. Only CRT fabs
and sqrt imports are replaced by equivalent x87 instructions. The executable
hash must match the recorded binary. This does not launch or mutate the game.
Result: **10005/10005** edge-intersection cases agree, including random translated
boxes, near-boundary lines, collinear edges and degenerate points. This proves
those sampled primitive results, not full generator parity. New synthetic
generator tests cover half-open points, collinear top edges and epsilon slack.
All six installed-map comparisons above remain unchanged after the correction.

Full suites completed ALL PASS on the navigation implementation through
`efc02db` (`build/full-gate-current.log`) and on isolated overworld commit
`1a60f09` (`D:/Code/FableForge-verify-ow/build/full-gate-overworld.log`). Both
lacked the optional mod/Project Seasons corpora. The later intersection change
passed its native oracle, core unit tests and all six map comparisons.

Combined merge `7def801` builds successfully. Both CTest targets pass; the
native geometry oracle again matches 10005/10005 when launched from a different
working directory (all scratch writes remain in the script's owning checkout).
The combined overworld scratch test passes, including GUI moves, terrain deploy,
theme deploy and custom-theme deploy (`build/combined-overworld.log` in the
integration checkout). LookoutPoint/PicnicArea retain exact comparison results;
BarrowFields retains the documented discrepancies. Docs-command checks pass.
The validated integration was fast-forwarded into local main at `8b5e587`; no push
or release was performed. Production navigation remains the existing writer.

## Graphic definition scale and automatic children

Greatwood_1's two `OBJECT_LOOKOUT_POINT_STATUE` instances have Graphic scale 2.
The scene loader previously used only ObjectScale, leaving their rendered mesh
and collected physics hulls half-sized. `Context::graphicModelId` now optionally
returns the cached Graphic scale; root instances use `0.01 * (ObjectScale *
GraphicScale)`. Missing, nonfinite or nonpositive graphic scales fall back to 1.
Explicit named GraphicOverride retains its existing unit graphic scale; its
native semantics were not established by this investigation.

Evidence from the same hash-pinned FableWin:

- `CEngineGraphic` layout: BankIndex +0, AnimStep +4, RenderSizeX +8,
  AdditiveAlpha +12, Type +13. Serialized Graphic stores type first, so
  RenderSizeX is the float at byte 12. `CPersistTraits<CEngineGraphic>::TransferIn`
  (`0x0200af80`) confirms the field sequence.
- `SetMainGraphic` (`0x01d643d0`) copies RenderSizeX to appearance +0x74.
  `GetFullScale` (`0x01d0a3c0`) returns appearance +0x44 times +0x74.
  `CTCPhysicsStandard::SetPhysicsMeshBankIndex` (`0x02525db0`) uses that full scale.
- `CTCMeshAutomaticEntityCreator::CreateObject` (`0x02570e20`) and
  `CreateBuilding` (`0x02571d30`) pass the parent's **GetScale** to the child,
  preserving the child's own graphic scale. `GetDummyObjectPositionStatic`
  (`0x031f7a90`) transforms the dummy translation through the full parent matrix
  and normalizes its composed orientation axes. Child transforms now follow
  that distinction, including nested children.

Evidence exports are under `build/nav-{fullscale,child-create,graphic-layout,
physics-create,graphic-transfer}-evidence.c`. Tests cover a rotated, nonuniform
dummy under a parent with ObjectScale 2 / GraphicScale 3, a child GraphicScale 5,
and a unit-graphic grandchild. A read-only real-scene probe also checks cold and
cached statue lookup and ObjectScale 3 / GraphicScale 2 -> mesh scale 0.06.

All **1231 Greatwood nodes and 723 leaf-neighbour sets now match retail**, with
matching region partitions. The other five comparison results are unchanged.
The full shared-scene regression gate completed **ALL PASS** in
`build/full-gate-scale.log`; the optional mod and Project Seasons corpus checks
skipped because their fixtures are absent. Includes build, core tests, eight-map
retail export smoke, UI/editor, scratch overworld/deploy and mesh-import checks.
No in-game visual check has been performed; generation remains experimental.

Further input check: GuildExterior's missing half-leaves (retail indices 16/17)
form region 2 and link only to one another. All three serialized navigation
positions resolve to region 1. There is no switchable leaf in that tiny region.
This led to a newly confirmed input error: the serialized positions are **action
points**, not region seeds. `SaveToFile` accesses this+0x70, while `SetUpRegions`
iterates this+0x98 (debug layout; disassembly in `build/nav-seed-offsets.log`).
`GetMapNavigationAreaInit` gathers separate seeds from villages, AI creatures,
creature generators, navigation seeds, region entrances and exits. Interface 99
is CTCDRegionEntrance (`0x01ba918f`). A scratch comparison using local TNG seed
candidates recovers Guild completely, removes Barrow's four extra nodes and
recovers 115 Orchard nodes; the three previous exact comparisons stay exact.
The explicit seed input described below implements this distinction. Scratch evidence:
`build/nav-region-probe.cpp`, `build/nav-scale-guild-details.log`.

## Separate region seeds from serialized action points

`GroundGeometry::regionSeeds` now supplies the ground builder's reachability
inputs. `RetailSection::positions` is preserved as action-point metadata and
never seeds a component. Regression tests put an action point on an otherwise
unseeded island and a region seed on a map with no action points; only the
explicit seed affects retention. Closed-door behavior and serialization still
pass. Earlier references to saved positions as "anchors" were an incorrect
input assumption, corrected here.

The read-only CLI gathers NULL-quest seed candidates from local and touching
map TNGs, in the native category order: villages, AI creatures, creature
generators, navigation seeds, entrances, exits. Component presence comes from
TNG blocks or definition Components, so custom definitions can qualify without
hard-coded definition names. Seed positions are kept separate from serialized
action points in both the data model and diagnostic output.

Evidence: `CNavQuadTree::SaveToFile` (`0x03289b50`) reads debug this+0x70;
`SetUpRegions` (`0x0328f270`) iterates this+0x98. `Initialise` (`0x03290030`)
sets up regions and then removes unreachable action points, confirming their
different roles. Exports: `build/nav-seed-storage-evidence.c`,
`build/nav-seed-offsets.log`, `build/nav-entrance-interface.c`, plus the existing
`GetMapNavigationAreaInit` export referenced above.

Four maps now match nodes, neighbours and region partitions exactly:
LookoutPoint, PicnicArea, Greatwood_1 and GuildExterior. BarrowFields matches all
ground nodes and neighbour sets, with its stacked-layer region discrepancy
remaining. OrchardFarm recovers 115 nodes and now differs by 26 generated-only /
38 retail-only nodes. Logs: `build/nav-region-seeds-<map>.log`.

Limitations remain explicit: seed layers currently assume ground rather than
implementing `GetNavigationLayerAt`; native leaf ownership/search ordering and
automatic child seed components are not fully reconstructed. The core preserves
existing action-point metadata rather than re-running native action-point
reachability filtering. These comparisons do not establish full bake parity or
justify promotion to the production writer.

Validation after the seed correction: all targets build, CTest **2/2** passes,
all six comparison logs above reproduce, and docs-command checks pass. The full
suite already passed on the preceding shared-scene scale fix (`a537d7f`); this
follow-up changes only the experimental generator/diagnostic, its tests and
metadata documentation. Latest binaries are in this lane's `build` directory.
