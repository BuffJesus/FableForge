> Latest session checkpoint: [2026-10-10 continuation](HANDOFF_2026-10-10.md). RC2 passes all 54 CTests and 57 selected extracted workflows; GitHub release integration is next.

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

## 2026-10-05 reserved FMP record identity guard

A crafted Assasain package kept anonymous slot 14336, named its identical
second record @unnamed:14336 and retained one metadata record. Both routes
accepted it before the change (C scratch fmp-reserved-record-probe-yoxggtus).
The named record could replace the anonymous imported-index lookup. Named
GameBINEntries and Objects records now reject that reserved namespace before
output writes. Original duplicate parity remains identical to the independent
control (20 links); fmp-identical-records-7ioahexr passes 24 negative routes,
including the new collision, with all prior outputs and source hashes intact.
Focused fmp-record-refusal-ihg4j1ye passes 18 cases; its source file invariance
now uses SHA-256. CLI target rebuilt; this guard changes no forgecore code.
Frozen 0b6be9c excludes it. No game launch or live-install write occurred.

## 2026-10-05 compact desktop package smoke

Frozen 0b6be9c ZIP SHA-256
95e4e15734a405d3b4a77ce7593a49c70adcbedcc7e457ee9c2861375ee005e7
passed 42 workflows in package-smoke-lwbtweya before its wide-chip GUI script
failed on a 1028x749 compact window. A diagnostic-only retry in
package-smoke-4odpvi9m logged the child desktop as 1024x768, requested
1440x900 and captured 1028x749. Both failures are retained; neither has a
successful result. Earlier direct retry ui-smoke-s1d4ejkq passed wide chips.
The desktop differs between executions; its external cause is not established.

The smoke launcher now logs desktop/requested/captured dimensions and uses
real compact dropdown clicks with explicit mode assertions on narrow desktops.
It can also request that path with --compact. Existing pixel difference,
terrain color, accent and GLB assertions remain. Compact ui-smoke-ntrzs7af
and ui-smoke-nas91wm4 pass. Package child settings/cache roots are private.
The same frozen ZIP tail is running in package-smoke-wxqrejsb; full acceptance
is pending. No code in the frozen archive was changed by the harness repair.

Actual extracted forge-tools identity matches PACKAGE_INFO in the original
identical-record gate fmp-identical-records-s1qnzaxe/package-identity-audit.json:
20 links, four control-equivalent outputs and 22 safe negative routes. This
scoped audit excludes the later reserved namespace guard and is not a full
package acceptance claim.

## 2026-10-05 original nonempty creature appearance recovery

ChocolateBox 237DCBE3 and native CCreatureDef counted-pointer evidence describe
body-part model alternatives, paired texture rows and skeletal names offsets.
The older 4000099073 metadata transposes alternatives across body-part lists.
Independent source offset/name audits in fmp-appearance-probe-tm70hutl match
9,935 words across 186 records, with no missing named IDs or order mismatches.
The original source bytes and bank IDs, not list labels alone, establish order.

The existing bounded prefix parser now records model/texture/skeletal offsets.
Metadata carries occurrence plus complete group count; native locate validates
those counts against all applicable body parts and optional skeletal slots.
The lowercase skeletal-only member refuses when a mesh alternative exists.
All links belong to prefix part zero. Only selected four-byte words change;
mesh/texture IDs and skeletal names.bin offsets resolve in distinct domains.
Native tests cover mixed domain exact bytes, count/index disagreement and the
skeletal-only case. Final all-target build and package follow-up are in progress.

Original Dark Apprentices variants 1/2 and Hero Pack pass in
fmp-creature-appearance-a_49pg00: 204 appearance words, all 282 creature links,
exact whole records and effective six-bank parity through both routes; 34
negative routes preserve prior output and every original input hash. Unchanged
banks omitted from output are compared against the owned baseline copy.
The earlier y2d4g8ci checker failed by requiring an unchanged text bank in output;
v13ygrz5 then exposed the original No General Experiance duplicate-owner guard.
These diagnostic attempts are retained.

The full No General Experiance original remains refused. Its explicitly
labelled one-record NoGenEXP77 derivative in fmp-creature-appearance-vw0a3y6t
passes 70 appearance words (9 mesh, 56 texture, 5 names offsets), all 86 links,
byte preservation, bank parity and 14 safe negative routes. The derivative
retains the original record/metadata bytes; its package hash and original hash
are separate in the report. ubq84ok1 first exposed skeletal-only lowercase
members, now handled with explicit native boundaries. No full historic counts
were recomputed and no gameplay or live-install writes occurred.

The 40-case core run passed 39 and timed out in the unrelated audio test;
a focused unchanged audio rerun passed in 0.18 seconds. Later prefix tests
pass after the skeletal-only addition. Retain both results; do not claim a
single uninterrupted forty-pass run yet.

Final all-target Release build is complete. All 40 core tests pass together in
44.95 seconds after the final parser change; the earlier audio timeout and
focused recovery remain part of the diagnostic history. The existing original
wound/expression gate is being checked against this build separately.

## 2026-10-05 completed frozen duplicate/creature acceptance

Frozen 0b6be9c SHA-256
95e4e15734a405d3b4a77ce7593a49c70adcbedcc7e457ee9c2861375ee005e7
now has 89 distinct successful workflows across 42 initial successes in
package-smoke-lwbtweya and 47 continued successes in package-smoke-wxqrejsb.
build/audit_package_acceptance_0b6be9c.py passes and the tail retains
combined-acceptance.json. There are zero uncovered workflows or reported
fixture skips, with 108 package identities and 122 documentation links.
This is not an uninterrupted run: the initial GUI failure and diagnostic-only
package-smoke-4odpvi9m failure are both retained. The resumed GUI exercises
real compact dropdown selections with unchanged pixel and GLB assertions.
This immutable archive excludes the later reserved namespace guard and
appearance recovery; no current-head/full-historic-corpus claim is implied.

Frozen a0cb5ab SHA-256
37dce53af5b061092d8324b3406ef162b04c5c3324732b050f10a825395217a2
passes two scoped workflows (18 FMP record cases and compact GUI) in
package-smoke-ct3gpf67; 87 not run, zero skips. It includes the reserved
namespace guard but excludes appearance recovery.

Frozen 864e853 SHA-256
9d04692d4a8c6af68c533b3cbb09ba6fe083642ed3300ab3f6b6d6fe3f9bb6cb
passes four scoped workflows in package-smoke-81ymnck2: both record contributor
modes, FMP record refusal and GUI; 85 not run, zero skips, 108 identities/122
links. Actual extracted-tool appearance checks ftndda7b and hyvg0fe5 pass
204 original appearance words/282 total creature links/34 negative routes
and the separately labelled 70-word skeletal derivative/86 links/14 negative
routes. appearance-acceptance-audit.json verifies the actual tool path/hash
against PACKAGE_INFO (d53982c6f0e01198c2c471b47a3078bcff2efe4dfaf8bc037352353e445ebde6).
No General Experiance original remains refused. The old wound/expression gate
ntcmmgbd passes 210 prefix/258 total links plus forty safe negative routes
against this final parser build. All archives/guide ZIPs remain local.

## 2026-10-05 original appearance prefix choices

The retained regression gate test_fmp_creature_appearance_choices.py passes
13 selections and two safe unavailable refusals in fmp-appearance-choices-sryy8r4z.
The two original Dark Apprentices packages have different appearance prefix
sizes/texture group counts. A named new record follows whole-record load order
or either source choice (three merges). An existing named record follows
prefix part zero with no scalar choice key, for order/either source/baseline
through merge and recipe build (eight selections). Choosing a disabled/omitted
source refuses before changing output or picks; clearing it recovers the
remaining source (two selections). Selected prefixes have independently
resolved mesh/texture IDs and every later record byte equals baseline.

Existing-record coverage uses an explicitly labelled baseline derivative:
only the first MeshGroup word differs from original variant 1. Its original
record/metadata bytes otherwise remain, with source and derivative hashes
separate. Native source packages and live bank hashes remain unchanged.
Probes e4ogsupz (new record), gz7i64zj (source identical to baseline correctly
unavailable) and 3uhyj28c (distinct derived baseline) are retained. The prior
unavailable result was expected contributor semantics, not an import defect.

The appearance choice gate also passes with the actual extracted 864e853 tool
in fmp-appearance-choices-6crk3cc5. Its package-identity-audit.json matches the
recorded executable hash to PACKAGE_INFO, verifies 13 selections/two safe
refusals and confirms different source prefix sizes. This Python-only follow-up
adds no C++ behavior beyond frozen 864e853.

### 2026-10-09 Definitions browser foundation

Assets > Definitions now browses game/frontend/script banks with name/type/index filtering, confirmed typed fields, raw fallback, refresh and an object-property jump. This phase is read-only; definition field editing, grouping and cloning remain pending. Both window sizes pass the dedicated fixture check and source preservation; text browsing and text close-prompt neighbor checks pass. Retail game.bin exposes 14,764 records; OBJECT_QUEST_CARD_TEMPLATE shows 19 fields matching CLI decoding, with every compiled bank hash unchanged. Evidence: `build/definition-browser-ui-u6mgkrtz` and `C:/Users/Cornelio/AppData/Local/FableForgeEvidence/20261009/retail-definition-browser-kcj8_wl8`. The Definitions checkpoint is validated: `dist/development/20261009-definitions/FableForge-0.18.1-rc.1-win64.zip`, SHA256 `a76c7ac5b13dcf0f498a8681545cd5d4701b166309292c18c724ccf2374b64d5`. Extracted package evidence `package-smoke-_k74g4bl`: 109 file identities, 127 documentation links and 19 selected workflows pass; 88 not run, zero skipped. The superseded text-close snapshot was removed after retaining its metadata on C.

### 2026-10-09 Definition field selector correction

The CLI field editor previously accepted a numeric prefix such as `0junk` and edited record zero. `defs set-field` now requires an exact unique name or a complete unsigned record index; duplicate names refuse before creating output. Numeric names take priority, and unnamed records use explicit indices. The new test exercises ten refusals and four valid writes, independently checking every record payload and unchanged source hashes. Evidence: `build/definition-selector-before-qo7mn_5b`, `build/definition-selector-_ft2qwgt`; raw diagnostic reconstruction neighbor check passes. Validated checkpoint: `dist/development/20261009-def-selector/FableForge-0.18.1-rc.1-win64.zip`, SHA256 `6b256f1fa40684aeccab98b4aaab723e5c1c5eb7667c3243ef9e794b59a6ba5a`. Extracted evidence `package-smoke-fffj9jcr`: 109 identities, 127 links and four affected workflows pass; 104 not run, zero skipped. The earlier 19-workflow browser checkpoint remains recorded in C evidence; its superseded D snapshot was removed.

### 2026-10-09 Definition publication correction

`defs set-field` now prepares and verifies edited records before publication. Separate outputs preserve the source name table byte-for-byte, including historical headers. Existing destination pools must match all record identities; missing pools copy the original bytes. Standard bank outputs use original backups or creation markers and rollback. Destination-owner checks include physical folder aliases and occur before preparation and publication. Nineteen publication/recovery checks pass, plus selector and raw-decoding neighbors. Evidence: `build/definition-publication-before-y8obqfiz`, `build/definition-publication-q_gsptyc`. No native install writes. Validated checkpoint: `dist/development/20261009-def-publication/FableForge-0.18.1-rc.1-win64.zip`, SHA256 `c84778d82f650915a40b84c78d352f047759f72e75c440e18b6ecf95762942dd`. Extracted evidence `package-smoke-ed3rikn7`: 109 identities, 127 links and five affected workflows pass; 104 not run, zero skipped. Superseded selector snapshot metadata was retained on C before removing the old D folder.

### 2026-10-09 Fixed-width definition edit preservation

The generic field writer now refuses a scalar/vector/colour edit when its tag-delimited byte range has an unexpected size. This prevents discarding opaque derived-class trailers. The existing finite/subnormal float parser is preserved. Twenty-seven canonical/refusal cases, core tests, publication, selector and raw-decoding neighbors pass. A retail bank copied to C verifies one float delta across all 14,764 records, exact identity table and names preservation, unsupported DieOffTimer refusal, and byte-exact Restore. Real compiled banks remain unchanged. Evidence: `build/definition-layout-before-rcuh_xir`, `build/definition-layout-ivclrzsy`, `C:/Users/Cornelio/AppData/Local/FableForgeEvidence/20261009/retail-definition-edit-copy-neandq92`. Validated checkpoint: `dist/development/20261009-def-layout/FableForge-0.18.1-rc.1-win64.zip`, SHA256 `7bf0c3abbf11c71b843db2c149ab62a18a52cf7598ff1ec643eb90affc36437b`. Extracted evidence `package-smoke-9247y0a7`: 109 identities, 127 links and six affected workflows pass; 104 not run, zero skipped. Superseded publication snapshot metadata was retained on C before deleting the old D folder.

### 2026-10-09 GUI definition scalar drafts

Assets > Definitions now edits confirmed int32, uint32, float and bool fields through a one-field draft with explicit Save confirmation. Other fields remain inspectable. Stale payload/name-pool snapshots, invalid values, changed destinations and unsupported widths refuse while retaining the draft. Existing banks retain originals; new output-folder or pack banks retain creation markers. No-op edits write nothing. Definition drafts participate in close/install-switch protection and can coexist with text drafts. Game-bank edits refresh the current asset context without publishing map edits; context paths use UTF-8 conversion.

Twenty-four GUI cases pass across both window sizes, including exact Restore, stale payloads, all supported scalar kinds, retained opaque fields, pack/folder destinations and mixed text drafts. Browsing and text browser/close-prompt neighbors pass. A GUI float edit on a C-hosted retail copy independently preserves every other payload and all 14,764 identities, keeps names exact, and restores the original bank exactly. Evidence: `build/definition-edit-ui-ttq8_cdo`, `build/definition-edit-ui-95l3wgbx`, `build/definition-browser-ui-msaxu2x8`, `C:/Users/Cornelio/AppData/Local/FableForgeEvidence/20261009/retail-definition-edit-ui-copy-h1rswvxt`. Native install unchanged. Validated checkpoint: `dist/development/20261009-def-edit/FableForge-0.18.1-rc.1-win64.zip`, SHA256 `2f7a26bf1d2ac80b807eacde73bdd254b40f16c2b34ae8793638322c87cb5910`. Extracted evidence `package-smoke-07npuqzg`: 109 identities, 127 links and 23 selected workflows pass; 88 not run, zero skipped. Its GUI definition check includes 28 cases, including numeric-name re-selection and missing/malformed pack refusal. The superseded layout checkpoint metadata was retained on C before removal from D. Grouping, references, strings/vectors and cloning remain pending.

### 2026-10-09 Definition group navigation and exact no-op drafts

Game-bank `GroupDef` navigation now follows a unique four-byte scalar only when its target is a *_GROUP record in the same loaded bank. Numeric target names cannot redirect the jump. Navigation is deferred until the next frame and disabled while a definition draft is open. Frontend/script references remain uninterpreted. Both window sizes pass valid, wrong-type, absent, out-of-range, wrong-width, numeric-name and draft-protection cases. Retail OBJECT_QUEST_CARD_TEMPLATE resolves entry 7439, GO_GAMEPLAY, with native bank hashes unchanged.

Unchanged scalar drafts now bypass re-encoding after stale-source checks, retaining exact existing boolean/NaN representations. The prior noncanonical-boolean no-op normalized a byte; the new regression preserves it and creates no backups. All 32 definition editing cases and browsing neighbors pass. Evidence: `build/definition-groups-ui-6a9oucua`, `build/definition-edit-bool-noop-before-20261009.log`, `build/definition-group-exact-noop-edit-check-20261009.log`, `C:/Users/Cornelio/AppData/Local/FableForgeEvidence/20261009/retail-definition-group-read-od6qop7c`. Validated checkpoint: `dist/development/20261009-def-groups/FableForge-0.18.1-rc.1-win64.zip`, SHA256 `c9c23675db1e625cfac360e81afca721b9e773b3b06691f99a84e9e0cca20126`. Extracted evidence `package-smoke-4v8m8pvw`: 109 identities, 127 links and seven affected workflows pass; 105 not run, zero skipped. Only this validated snapshot remains on D; predecessor metadata is retained on C.

### 2026-10-09 Definition boolean checkbox

Boolean field drafts now use a checkbox; other supported scalars retain the value input. Actual checkbox clicks and confirmation update only the selected byte at both window sizes. Existing noncanonical true bytes and NaN payloads remain exact on unchanged drafts. Six focused checks pass in `build/definition-edit-ui-704x4hzk`. The full definition UI gate now includes 34 cases. Validated checkpoint: `dist/development/20261009-def-checkbox/FableForge-0.18.1-rc.1-win64.zip`, SHA256 `fd8259f0de4dc7f0474d9c369521e6c62e3bc06b2e8d3fc03678954480240d97`. Extracted evidence `package-smoke-93az6sxx`: 109 identities, 127 links and three affected workflows pass, including all 34 editing cases; 109 workflows not run, zero skipped. Superseded group snapshot metadata was retained on C before removing its D folder.

### 2026-10-09 Definition group members

Selecting a game-bank group now shows a virtualized list of records with confirmed GroupDef references to it. Membership is decoded in the existing background bank job, with unsupported records omitted; member clicks queue explicit indices for the next frame and remain disabled during drafts. Both window sizes pass object-to-group-to-member navigation, numeric names, malformed references, group-reference editing and refreshed membership after byte-exact Restore. Existing original backups remain exact. Browser neighbors pass, and retail OBJECT_QUEST_CARD_TEMPLATE returns from GO_GAMEPLAY with all compiled bank hashes unchanged. Evidence: `definition-groups-ui-0esvvm0x`, `definition-groups-ui-dertrtju`, `definition-browser-ui-9cvznq8s`, `retail-definition-members-read-x0z97k56`, under `C:/Users/Cornelio/AppData/Local/FableForgeEvidence/20261009`. The first write fixture used invalid per-class identities; publication correctly refused before writes. The corrected fixture and backup-retention assertion pass. Validated checkpoint: `dist/development/20261009-def-members/FableForge-0.18.1-rc.1-win64.zip`, SHA256 `2e309d6df70ba1d9ec4bbb07a4f66a7c6c5f72a177e3c37328ab7afb636b71fd`. Extracted evidence `package-smoke-pl269e6u`: 109 identities, 127 links and three affected workflows pass, including 34 editing cases; 109 not run, zero skipped. Superseded checkbox snapshot metadata was retained on C before removal from D.

### 2026-10-09 Definition vector drafts

Confirmed C2DVector (8 bytes) and C3DVector (12 bytes) fields now display coordinates and offer X/Y/Z number inputs through the existing field draft, confirmation, stale-source and recovery path. The unchanged-draft comparison uses a round-trip float representation, retaining exact NaN payloads, signed zero and subnormals. Core writer encoding is unchanged. Unexpected widths stay read-only and invalid/nonfinite edits refuse before writes. Thirty-two cases pass at both sizes, including real coordinate clicks, one-component byte deltas, precision-sensitive neighboring values, exact no-ops and malformed/truncated values. Eight scalar neighbors pass. A C-hosted retail copy verifies DollCentreOffset.x on HERO_POSTCARD1, all 14,764 record identities and unrelated payloads, exact names, backup and Restore; native hashes stay unchanged. Evidence: `definition-vector-ui-2cmck173`, `definition-edit-ui-s_i99r6g`, `retail-definition-vector-copy-xq2603ui`, under `C:/Users/Cornelio/AppData/Local/FableForgeEvidence/20261009`. The definition_vectors workflow is registered for package and broad checks. Validated checkpoint: `dist/development/20261009-def-vectors/FableForge-0.18.1-rc.1-win64.zip`, SHA256 `72c01fe77fe02d31a4d2872dd80bc10fc62dbffd02a5d91a5370255c8897e397`. Extracted evidence `package-smoke-ok0zey4g`: 109 file identities, 127 documentation links and four affected workflows pass, including 32 vector and 34 scalar cases; 109 not run, zero skipped. Superseded member snapshot metadata was retained on C before removing its D folder.

### 2026-10-09 Native colour byte-order correction

The existing CRGBColour writer treated physical bytes as RGBA, swapping red/blue for the game. Native FableWin evidence establishes BGRA: Initialise 018A0C60 stores R at +2/G at +1/B at +0/A at +3; CPersistTraits binary output 019C5E28 calls WriteColour 03083120, which writes GetB/GetG/GetR/GetA to successive bytes. ReadColour 03082170 reconstructs the same order; binary input 019C5E02 copies that value. Debug executable SHA256 `a9d6d0977d9d7fc8242da4292ae7ed92e926ca80364a95c003d9b9f689e41845`; PDB names independently identify the getters and I/O functions. Public r,g,b,a input now writes B,G,R,A for CRGBColour; the explicitly named ColourRGBA layout stays RGBA. Existing fixed-width guards and float handling remain. No GUI colour editing yet.

The corrected layout regression fails on the prior tool (`build/definition-colour-before-20261009.log`) and passes after correction. Nine colour inputs/refusals, 27 fixed-width layout cases and core tests pass. A copied retail PLAYER_HERO edit validates BGRA red with alpha 128, every other payload and all 14,764 identities, exact names/original backup and Restore; actual native hashes stay unchanged. Evidence on C: `definition-colour-native-proof`, `definition-colour-cjauuk6w`, `definition-layout-82qsk7ng`, `retail-definition-colour-copy-mil4w1nw`, under `C:/Users/Cornelio/AppData/Local/FableForgeEvidence/20261009`. The definition_colour workflow is registered. Validated checkpoint: `dist/development/20261009-def-colour-order/FableForge-0.18.1-rc.1-win64.zip`, SHA256 `f7b189d9d2767dbae7bdd4784883bf31a733387e143b93498fdd0edc0f621869`. Extracted evidence `package-smoke-u95d_gex`: 109 identities, 127 links and five affected workflows pass; 109 not run, zero skipped. Superseded vector snapshot metadata was retained on C before removal from D.

### 2026-10-09 Definition colour drafts

Confirmed four-byte CRGBColour fields now display RGBA values and offer a swatch picker, alpha control and four labelled byte-channel inputs. Source BGRA bytes are converted to public RGBA and saved through the corrected native writer, existing stale/destination guards and recovery path. Unchanged drafts write nothing; unexpected widths stay read-only, and invalid direct drafts refuse. Twenty cases pass across both sizes, including actual R/B/A input, opening/closing the popup, a real saturation/value selection followed by Save, exact extreme-colour no-ops and malformed value/layout refusals. The 32 vector neighbors pass. Small-window draft/picker screenshots were inspected. A retail copy GUI edit changes exactly PLAYER_HERO's red byte, preserves every other payload and all 14,764 identities/names, and restores byte-for-byte; native hashes remain unchanged. C evidence: `definition-colour-ui-gag1xb2l`, `definition-vector-ui-f790czg7`, `retail-definition-colour-ui-copy-1xzmouo0`, under `C:/Users/Cornelio/AppData/Local/FableForgeEvidence/20261009`. The definition_colour_edit workflow is registered. Validated checkpoint: `dist/development/20261009-def-colour-ui/FableForge-0.18.1-rc.1-win64.zip`, SHA256 `c3c2ece76263e9390f19349825a5fa4b0d6bfb51aee306a4dce35753911b0c22`. Extracted evidence `package-smoke-2yb67txb`: 109 identities, 127 links and six affected workflows pass, including 20 colour, 32 vector and 34 scalar UI cases; 109 not run, zero skipped. Superseded colour-order snapshot metadata was retained on C before removal from D.

### 2026-10-09 Inline definition text and terminator preservation

Supported CCharString fields now offer printable ASCII drafts up to 127 characters through the existing confirmation, source/destination guards and recovery path. Old values must be one complete terminated printable string fitting the draft; non-ASCII, embedded terminators, missing terminators, trailers and longer strings stay read-only. Actual InputText filters Unicode and nonprintable characters, and the writer path independently validates new drafts. Canonical string encoding is established in FableTLC docs/formats/DEFS.md (native Transfer<CCharString> 01895D30 / writer 0189C530 and retail CFireheartMinigameDef entry 10516). Wide strings/name-table references remain unedited.

Review found the shared writer accepted OLD\0OPAQUE and discarded the trailer; baseline `definition-string-trailer-before-mcx_cgh3` confirms it. Core CCharString edits now require exactly one final NUL and reject embedded NUL input, while allowing complete legacy/high-byte originals in explicit CLI replacement. Ten CLI canonical/refusal cases, 24 GUI cases at both sizes and core tests pass. GUI checks cover real longer/shorter/empty/max-length saves, exact no-ops, Unicode filtering and protected legacy/opaque values. A retail copy GUI resize of MoonFX on unnamed entry 10516 preserves every other payload and all 14,764 identities/names, retains an exact original and restores byte-for-byte; real native hashes remain unchanged. C evidence: `definition-string-ui-pj_ub1vc`, `definition-string-layout-wa5xnstt`, `retail-definition-string-ui-copy-2umra_sy`, under `C:/Users/Cornelio/AppData/Local/FableForgeEvidence/20261009`. definition_strings and definition_string_layout workflows are registered. Validated checkpoint: `dist/development/20261009-def-strings/FableForge-0.18.1-rc.1-win64.zip`, SHA256 `5bf5295529bf7a018f6e148eceaed682757c233f1566ff3da49516f11a3fe9de`. Extracted evidence `package-smoke-30yl8rp0`: 109 identities, 127 links and ten affected workflows pass; 107 not run, zero skipped. Superseded colour UI snapshot metadata was retained on C before removing its D folder.

### 2026-10-09 Native wide-string correction and ASCII drafts

The old CWideString writer prepended a u16 character count and omitted a final zero unit. Direct FableWin evidence disproves that layout: CPersistTraits binary output 01895C50 calls WriteNullTerminatedWString 030830D0, which writes (GetLength+1)*2 bytes from the string pointer. Binary input 01895BC0 uses the stream's wide-string read method. PDB names identify the writer and GetLength; debug executable SHA256 remains `a9d6d0977d9d7fc8242da4292ae7ed92e926ca80364a95c003d9b9f689e41845`. Retail UI TextValue samples independently show literal UTF-16LE text followed by a zero unit, without a count. Entry 5983 PC_PHOTOJOURNAL_CAPTURE_DESCRIPTION decodes cleanly with zero leftover; TextValue is field 3.

Core CWideString now writes ASCII as native NUL-terminated UTF-16LE, requires one complete even-length original value and refuses missing/embedded terminators, trailers and non-ASCII new input. Complete Unicode originals may be explicitly replaced with ASCII through CLI; no-op/GUI browsing preserves their bytes. The public header grammar is corrected. GUI exposes only complete printable ASCII originals fitting 127 characters; Unicode, long and unsupported originals remain read-only. Actual input uses the same ASCII filter and independently validated draft path. The prior wrong encoding is captured in `definition-wide-before-l2fkyu21`.

Thirteen CLI cases, 24 GUI cases at both sizes and core tests pass. A retail copy GUI TextValue resize validates native UTF-16 termination, every other payload and all 14,764 identities/names, exact original and Restore; actual native hashes stay unchanged. C evidence: `definition-wide-native-proof`, `definition-wide-ui-3tmln7it`, `definition-wide-layout-nhra58sw`, `retail-definition-wide-ui-copy-03pi4qjo`, under `C:/Users/Cornelio/AppData/Local/FableForgeEvidence/20261009`. definition_wide and definition_wide_layout are registered. Unicode authoring remains pending; it is refused instead of guessing a conversion. Validated checkpoint: `dist/development/20261009-def-wide/FableForge-0.18.1-rc.1-win64.zip`, SHA256 `59504874daef972f3e0b7934393171a99195f1954dc6c9c74d0216956aa14ba8`. Extracted evidence `package-smoke-nrzmu1tx`: 109 identities, 127 links and thirteen affected workflows pass; 106 not run, zero skipped. Superseded inline-string snapshot metadata was retained on C before removing its D folder.

### 2026-10-09 Definition type/group tree

The Definitions list now groups cleanly decoded game-bank records by type and confirmed GroupDef target. Other banks retain ungrouped records. Search includes confirmed group names and indices. Large trees start collapsed; selecting a record opens its ancestors and brings the virtualized row into view. Drafts keep navigation disabled. Native entry indices remain the identities.

Four dedicated cases at 1440x900 and 800x600 pass, including 128-record selection, group-name search, bank switching and protected drafts. Existing group edit/Restore and browser checks pass. A first large-list check caught premature clearing of the scroll request; the request now remains pending until the row is visible. Evidence on C: definition-tree-ui-38i20mhg, definition-groups-ui-j___a6e7 and definition-browser-ui-jjmsrp1o. definition_tree is registered in package and regular validation. Package validation remains pending.

### 2026-10-09 Definition type/group tree

Definitions now group records by type and confirmed game-bank GroupDef target. Search includes group names and indices. Selecting a record opens its ancestors and reveals its virtualized row; drafts protect navigation. Four cases at both window sizes pass, including 128 records, search and bank switching. Group edit/Restore and browser neighbors pass. Evidence on C: definition-tree-ui-38i20mhg, definition-groups-ui-j___a6e7 and definition-browser-ui-jjmsrp1o. The first large-list check exposed premature clearing of the scroll request, now fixed. definition_tree is registered. Retail read-only navigation passes with all bank hashes unchanged (retail-definition-tree-read-q6u27wdm). Validated checkpoint: `dist/development/20261009-def-tree/FableForge-0.18.1-rc.1-win64.zip`, SHA256 `6e2389e2bb7300b4cc76ff88e523c5f7f820328d9451a8b39be84198691727e7`. Extracted package-smoke-wf7mwbop passes 109 file identities, 127 links and four selected workflows; 116 not run, zero skipped. Earlier package-smoke-dzlaq6jv passed six checks but its final selection audit failed because text_close was mistyped; it is retained as partial evidence, not a completed gate. Superseded wide snapshot metadata retained on C before removal.

### 2026-10-09 Object definition model link

Clean game-bank OBJECT Graphic fields with the confirmed 17-byte CEngineGraphic layout now open Models by their native bank ID at bytes 4..7. Other fields, banks and unsupported widths remain inspectable. The link shares the existing strict model-ID selector, preserving the current preview on missing/ambiguous IDs, and stays disabled during a definition draft or asset operation. Native binary output 0200af00 and input 0200ae90 confirm the kind/index/float/float/byte sequence; the stream writes four-byte kind and index. Existing uidef and FMP model links use the same slot.

Eight scratch GUI cases at both window sizes pass: actual numeric-name collision, missing/ambiguous targets, truncated graphics, draft protection and bank switching. The four tree neighbors pass. Retail OBJECT_BARREL_UNBREAKABLE opens model 4075 MESH_BARREL_UNBREAKABLE_01; compiled banks, graphics.big and textures.big hashes remain unchanged. C evidence: definition-graphic-native-proof, definition-model-ui-8i5k3my2 and retail-definition-model-read-1cjv2wcs. definition_model is registered. Validated checkpoint: `dist/development/20261009-def-model/FableForge-0.18.1-rc.1-win64.zip`, SHA256 `830cdfa971aff612a6961d6e2cc8f59e49dfeac7a20e54bd51dab7e7f1b9fc77`. Extracted package-smoke-fm_04lu1 passes 109 file identities, 127 links and four selected workflows; 117 not run, zero skipped. Superseded tree checkpoint metadata retained on C before removal.

### 2026-10-09 Wide-string UTF-8 authoring in CLI

CWideString CLI edits now accept valid UTF-8 and encode native NUL-terminated UTF-16LE, including supplementary characters. The strict converter already used by text.big is shared in forge/unicode.hpp; text.big delegates to it. Malformed UTF-8, embedded NULs and malformed original UTF-16 surrogates refuse. Original termination/trailer guards remain. GUI wide drafts retain their bounded printable ASCII contract. Native wide-string layout evidence is unchanged; this extends conversion, not the wire format.

Twenty CLI cases pass, including accents, mixed scripts, supplementary/maximal code points and malformed originals. The new Unicode unit target checks independently specified bytes, invalid/overlong UTF-8, surrogates, boundaries and an overflowing cursor. Four focused CTest targets (Unicode, BIG writer, dialogue text and core) pass; the complete suite was not rerun. All 24 existing wide GUI cases pass. A retail copy TextValue edit preserves all 14,764 identities, every other payload, the table header and exact names; native banks remain unchanged. C evidence: definition-wide-layout-eoebsew2 and retail-definition-wide-unicode-copy-cdfp0teg. Validated checkpoint: `dist/development/20261009-def-unicode/FableForge-0.18.1-rc.1-win64.zip`, SHA256 `5df203ad95f664ed273d712c2c0faede502b989f61d27113b365bc05abfcad1b`. Extracted package-smoke-zann_j8d passes 109 file identities, 127 links and six selected workflows; 115 not run, zero skipped. Superseded model-link checkpoint metadata retained on C before removing its D folder.

### 2026-10-09 Terrain-theme texture links

Clean game-bank ENGINE_THEME records now offer Open texture for the six positive, complete int32 texture fields used by terraintex::ThemeLibrary. Zero/negative IDs, other integers, malformed widths and other banks do not infer links. Navigation resolves the explicit GBANK_MAIN_PC ID and then the unique exact symbol through the existing texture browser. Missing IDs, duplicate IDs and duplicate symbols preserve the current selection. Definition drafts and active asset operations protect navigation.

Sixteen scratch cases at both sizes pass, covering numeric names, missing/ambiguous IDs, duplicate symbols, a different texture bank, truncation, sentinel IDs and draft protection. Eight model-link neighbors pass. Retail GROUND_BLEACHEDLONGGRASS opens independently resolved main-bank ID 4260 UNASSIGNED_SAND_FINE; native definition and texture hashes remain unchanged. C evidence: definition-texture-ui-xw0rw7hg, definition-model-ui-jc41ao7j and retail-definition-texture-read-i3ggftsr. Two initial launches failed (texture automation reported PASS but the process did not return success; model preview failed); exact fixture replays and fresh complete runs passed without source changes. Initial evidence qyz1um1k/bezygjxu remains retained; cause unisolated. Future checks retain process exit codes explicitly. definition_texture is registered. Validated checkpoint: `dist/development/20261009-def-texture/FableForge-0.18.1-rc.1-win64.zip`, SHA256 `7dd5618dd8e6cf7651b0446124eacb8e85acbfe2d23f9372f78efd2c54fb69b1`. Extracted package-smoke-9rgrm4e7 passes 109 identities, 127 links and five selected workflows; 117 not run, zero skipped. The initial failures did not recur in the packaged checks. Superseded Unicode checkpoint metadata retained on C before removing its D folder.

### 2026-10-09 Definition group ordering and identity labels

Definition groups now sort by their names with native index as the tie-breaker; the unconfirmed bucket stays first. Group headings and search rows show the native group index alongside its name, distinguishing numeric, duplicate and unnamed groups. Eight tree cases pass at both sizes, including widget-position ordering for reversed names and duplicate-name ties, native selection, group search, draft protection and virtualized records. Group edit/Restore neighbors pass. C evidence: definition-tree-ui-b0xxw1xt and definition-groups-ui-a7vn4lxb. Validated checkpoint: `dist/development/20261009-def-tree-sort/FableForge-0.18.1-rc.1-win64.zip`, SHA256 `d4155b940c9d155c48246057bd489487ff02e3d1366c7d5e481ef30e417848d0`. Extracted package-smoke-3wfnk8cd passes 109 identities, 127 links and three selected workflows; 119 not run, zero skipped. Superseded texture checkpoint metadata retained on C before removal.

### 2026-10-09 Definition integer bounds

The shared integer writer now checks explicit int32/uint32 bounds after parsing with 64-bit intermediates, and unsigned fields reject a negative sign. CDefString uses signed parsing for negative input while retaining unsigned/hex bit representations; its documentation now identifies a signed names-table offset rather than a definition-entry index. Existing -1 sentinel support was verified before the change; the defect was acceptance of negative unsigned values and out-of-range negative references that wrapped into valid-looking values. The prior extracted tool fails the new regression at uint32 -1, saving 4294967295. Baseline C evidence: definition-integer-before-hxlf81va and definition-integer-bounds-q3o8v0d8.

Twenty-two CLI bounds/compatibility cases, 27 layout cases, core tests and six scalar GUI neighbors pass. Retail input underflow refuses before creating output; valid -1 preserves all 14,764 records, the table and exact names, with native hashes unchanged. C evidence: definition-integer-bounds-ytt1tbne, definition-layout-kb65z92g, definition-edit-ui-2at7c7__ and retail-definition-integer-copy-acm0g8h3. definition_integer_bounds is registered. Validated checkpoint: `dist/development/20261009-def-int-bounds/FableForge-0.18.1-rc.1-win64.zip`, SHA256 `9be7ec0a17da1776d140577b475b4282dde33743170c70d20d63500a1e3f5027`. Extracted package-smoke-edqz1anu passes 109 identities, 127 links and six selected workflows, including all 34 scalar GUI cases; 117 not run, zero skipped. Superseded tree-sort snapshot metadata retained on C before removing its D folder.

### 2026-10-09 Readable wide-string display

The definition formatter now displays complete valid CWideString values as quoted, escaped UTF-8 instead of hex. Strict conversion requires the terminator to end the complete field range; malformed surrogates, missing terminators, odd widths and trailers retain every raw byte in hex. GUI field previews stop at a UTF-8 boundary at the existing 2048-byte cap, while Copy full value retains the whole formatted string. GUI authoring remains bounded printable ASCII; Latin-accent display was visually verified at both sizes. Broader font coverage and Unicode GUI authoring remain pending.

Thirteen formatter fixtures cover known ASCII/BMP/supplementary bytes, control escaping and raw fallback. Three focused CTest targets (definition decode, core and Unicode) pass. Twenty wide CLI cases validate saved bytes, human-readable new values and typed decoding; source pools/payload neighbors remain exact. Twenty-eight wide GUI cases pass, adding both sides of a UTF-8 truncation boundary; cafe screenshots were inspected. The retail Unicode-copy delta again preserves every other record and native hashes. An initial test log used the Windows default encoding and could not write Greek output; it now writes explicit UTF-8. C evidence: definition-wide-ui-3tlqwta2 and retail-definition-wide-unicode-copy-n8i404ad. Validated checkpoint: `dist/development/20261009-def-wide-display/FableForge-0.18.1-rc.1-win64.zip`, SHA256 `927618380cd3df21dc10a18a8cc4e51460b5c6e0837c4638a2ee1176e453c51b`. Extracted package-smoke-s1j4pbky passes 109 identities, 127 links and six selected workflows; 117 not run, zero skipped. Superseded integer checkpoint metadata retained on C before removing its D folder.


## Definition name-offset labels, 2026-10-09

CDefString fields now show the name at a confirmed byte offset in the owning bank's loaded names pool. Duplicate and unused pool records retain their native offsets; invalid, interior, sentinel and malformed references keep their raw value. Labels refresh with the bank and remain read-only. Native CDefString GetString uses buffer + offset + 4, rather than a record ordinal. Retail PC_PHOTOJOURNAL_CAPTURE_DESCRIPTION Font offset 224 resolves to ENG_ARIAL_16.

Twenty-eight scratch cases pass across both window sizes, covering duplicate/unused/empty/numeric names, invalid offsets, bank switching and name-pool refresh. Both-size native read checks, browser and text-close neighbors pass; source hashes remain unchanged. Evidence on C: definition-name-reference-native-proof, definition-name-labels-ui-p194dtte and retail-definition-name-label-read-xey72ggf. definition_name_labels is registered. Validated checkpoint: dist/development/20261009-def-name-labels/FableForge-0.18.1-rc.1-win64.zip, SHA256 bf5e9aa22fe5ed165ca5a072ac02dddf232a7a8e7216c73cffc0b2a233a76081. Extracted package-smoke-vlhl0cvj passes 109 identities, 127 links and five selected workflows; 119 not run, zero skipped. Initial selection used nonexistent text_close; corrected text_close_prompt completes the gate. Superseded wide-display checkpoint metadata retained on C before removing its D folder.


## Existing-name reference drafts, 2026-10-09

Four-byte CDefString fields now have an existing-name chooser with explicit byte offsets, duplicate-name choices and No name (-1). Search filters names or offsets and the choice list is virtualized. Saving a changed reference checks the encoded four bytes against exact starts in the captured owning pool; invalid/interior/negative references refuse. An unchanged legacy reference preserves its original bytes. Existing namespace, record, destination, running-game and recovery guards remain in use. No names are appended.

Twenty-six scratch GUI cases pass at both sizes, including real selections, duplicate names, search in a 500-plus-name pool, sentinel and invalid references, unchanged legacy offsets, changed destination/name pool and Restore. Twenty-eight label neighbors passed before adding search. The search popup was visually inspected at 800x600. Both-size retail-copy Font edits preserve all 14,764 record identities, every unrelated payload, bank identity tables, name-pool bytes and native hashes; Restore returns the exact original bank. C evidence: definition-name-picker-ui-jnj2prmr and retail-definition-name-picker-copy-ttovgl3s. Twelve scalar neighbors pass (Save/Restore, destination, stale record, bool, float and uint at both sizes). Validated checkpoint: dist/development/20261009-def-name-picker/FableForge-0.18.1-rc.1-win64.zip, SHA256 ed36368ec189b45b43150cdec2a8b59a424b9ce0b5d8b850cf64bf95870035e9. Extracted package-smoke-9uvd04as passes 109 identities, 127 links and seven selected workflows; 118 not run, zero skipped. Superseded name-label checkpoint metadata retained on C before removing its D folder.


## Generic donor-clone foundation, 2026-10-09 (in progress)

forgecore now exposes defedit::cloneEntry by exact unique donor name or native index. It appends an exact payload with a canonical per-type ordinal, leaving previous slots, payloads and loaded name-pool records intact. New names must be nonempty printable ASCII up to 127 bytes and unique among bank records. Ambiguous/missing donors, invalid names, oversized payloads and noncanonical existing ordinals refuse before mutation. Unnamed donors use an index; numeric donor names stay exact names.

The new clone CTest and existing BIN writer target pass. Clone checks include opaque bytes, numeric/unnamed donors, duplicate unused pool strings, rejected names/donors, independent save/reopen record equality and an unchanged name-pool body prefix. CTest inventory is now 54 targets; no full suite run. This is an in-memory API only. Paired publication and user-facing clone controls remain pending. Current validated checkpoint remains def-name-picker.


## Paired definition clone CLI, 2026-10-09

forge-tools defs clone <bin> <donor-name-or-index> <new-name> --out <bin> [--names <names.bin>] [--json] publishes one exact donor clone with its appended name pool. Explicit output is required. Existing destination records and raw name pool must match the loaded source; stale or incompatible destinations refuse. The pair uses the existing rollback-capable staging/recovery mechanism, with original backups or creation markers for both files. Clone publication is separate from unchanged-pool field saves.

Thirty-five CLI cases pass: exact/numeric/unnamed/duplicate-index donors, invalid selectors/names, fresh and in-place publication, existing outputs, incompatible/missing pools, orphan recovery, physical aliases, rollback after a locked name-file rename, Restore and running-owner/junction refusal using an owned dummy helper. Original source files remain exact. Both clone/BIN-writer CTest targets pass. A full retail-bank export preserves all 14,764 prior records and identity-table rows, appends the exact donor and one correctly keyed name, and restores creation outputs without changing native hashes. Nineteen field-publication and fourteen selector neighbors pass. C evidence: definition-clone-038ldrlw, retail-definition-clone-export-rdt8kq3r, definition-publication-lz8k6ia0 and definition-selector-a95ictqb. GUI cloning remains pending.

The user explicitly requested GitHub milestone releases/updates on 2026-10-09. Prepare 0.18.1-rc.2 as a prerelease in the isolated C release checkout, preserving the working lane and excluding the separately owned test_dialogueaudio.cpp edits. Existing GitHub rc.1 is retained; do not overwrite it. Source publication, CI and exact RC2 package validation are in progress.
