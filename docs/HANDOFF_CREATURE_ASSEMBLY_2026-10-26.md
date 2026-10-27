# Creature assembly, 2026-10-26

Continues the [Creature pose checkpoint](HANDOFF_CREATURE_POSE_2026-10-23.md).
Checkout and Git environment are unchanged (see that file).

## Pose gates executed

`fableforge_creaturepose_tests` (built 2026-10-23, unchanged source at 7b69640):

- No-argument unit cases: PASS.
- `--oracle` against `FableForgeEvidence/20261023/pi-f_r5r0vx/result.json`: 240/240
  global matrices and 120/120 transposed skin matrices equal native output exactly.
- `--bind-mesh` on all 12 sampled retail body meshes (`graphics.big` SHA256
  8b909c40...f617, unchanged after the run): neutral-bind skin matrices are within
  3.2e-5 of identity for 16-63 bones / 630-4,186 vertices. Stored bind keyframes,
  inverse binds and the raw native quaternion convention agree on retail data.

## Native draw structure

`CTCRandomAppearanceMorph::OnAppearanceDraw` (0252e720) creates one
`CEnginePrimitiveMorphedAnimatingMesh` per `CMeshMorphSetParts` entry, passing that
part's mesh ID and texture morphs and the SAME animation/position/orientation
interpolators (`DrawInfo+4/+8/+0xc`) to every part. There is no part-local
transform; selected parts are the drawn meshes. Bone scales come from
`GetBoneScalings(meshId)`, so the combined skeletal morph is mapped per part
through that part's own bone CRCs. The no-parts path (UpdateMorphSet adds globals
to the base morph set) is not assembled yet.

## Core assembler

`forge::creatureassembly::assemble(prefix, seed, decapitated, sources)` composes the
verified pieces: native selection, per-part type-3 texture composition, bone-config
read/prepare/combine, per-part CRC mapping, native bind pose with morph scaling,
then linear-blend skinning (`headpose::skin`). It refuses an appearance without
substituted parts, missing names/targets/meshes, and any mesh bone whose stored
CRC differs from `bin::nameCrc(name)`. Bind pose only; normals use the skin matrix,
not its inverse transpose.

`fableforge_creatureassembly_tests --retail <install> <schema> [seed] [obj-dir]`
(read-only) over all 330 CCreatureDef records, seeds 0-7 and 0x12345678:
188 assemble, 142 refuse as "no substituted body parts", zero other failures.
At 0x12345678 all 15 Creatures that selected a skeletal target scale 8-15 bones
on every part; none without a target is scaled.

Creature 9126 (meshes 3912/3914/3913), absolute extents (width/depth/height):
none 128.2/37.1/187.0; fat 131.4/54.5/189.4; short 128.2/37.1/176.7;
tall+fat 141.5/38.3/200.5; weak 127.1/35.3/186.9; strong changes limb
cross-sections only (max vertex delta 0.52). Renders show the three parts
joining seamlessly in the shared space: `build/creature-assembly-retail-20261026/`.

## GUI: Creature looks

`creatureassembly::merge` joins the posed parts into one preview geometry and
retargets each part's material diffuse/bump IDs through its composed map (from the
authored ID; no chasing). Retail sweep at 0x12345678: 1,105 materials, 446
retextured across 143 Creatures; the 22 Creatures with replacements but no change
carry identity rows (627->627) or IDs the mesh does not use (verified per part).

`Context::assembleCreature` resolves CREATURE -> CCreatureDef through
`defedit::resolveCreatureVisuals`, the names pool and `<gameRoot>/data/Bones`. It
needs the full `docs/re_reference/def_schema.json`: the embedded schema has no
CCreatureDef (first GUI run refused "no schema for CCreatureDef"). The GUI loads
it with the Definitions browser's lookup (bundled copy wins).

Assets > Models > **Creature looks** lists CREATUREs carrying
CTCRandomAppearanceMorph; pick one, type a Seed or press New look. Guide page:
`docs/guide/creature-looks.md`. `tests/ui/creature_looks.txt` (`creature_look`
command) passes against the retail install; screenshots of CREATURE_BANDIT_GRUNT
seeds 7 and 3 show different textured head/torso/arm sets joined seamlessly.

`--roots` over all 517 CREATURE roots (seed 7): 290 carry CTCRandomAppearanceMorph
and 282 of those assemble; 8 refuse with no substituted parts. No root without the
component has parts (212 base-mesh, 15 graph refusals), so those Creatures draw
their ordinary model. The base-mesh gap affects 8 entries in the GUI list.

Model export: Assets > Models > **Export .glb (Blender)** (and `model_export`)
writes the previewed model or Creature look to the Export tab's output folder via
`foliageexport::writeModelGlb` (terrain-less, same mesh/texture path as map GLBs).
The UI script exports MESH_OBJECT_BARREL and CREATURE_BANDIT_GRUNT seed 3; both
parse with all indices in range, embedded textures (1 and 5), Y-up.

Full suite after the assembler/GUI work: 62/62 CTest targets pass.

## Next

1. Base-mesh (no-parts) appearance path from the CAppearanceDef graphic (8 Creatures).
2. Animation sampling through the existing animation reader; keep bind-pose
   preview labelled as such.

## Rigged export and the import V flip

`skinnedexport` writes a single skinned retail mesh as a rigged glTF in metres:
bones as nodes at bind, inverseBindMatrices = C^-1 * M * C (so meshimport's
inverseBindInFable returns the mesh's own), JOINTS_0 local slots, WEIGHTS_0
normalized bytes summing to 255 (largest remainder). The GUI's Export .glb uses
it for selected models with bones; Creature looks stay static (posed). Review
(2026-10-26 workflow) found 15 retail creatures with repeated bone names (IGNORE /
EMITTER_ dummies: balverines, nymphs, trolls, dragon, scorpion king); those and any
mesh with unskinned vertices export static with a warning (`unsupportedReason`).

`fableforge_skinnedexport_tests --retail <graphics.big> <out> <ids>` runs retail ->
rigged .glb -> loadSkinnedModel -> mapSkinToDonor -> prepareSkinnedMesh (the real
composer) -> decode. All 12 sampled body meshes: every bone, identical triangle
counts, every composed vertex matched by position, weights within 0.02.

It also exposed a V flip in model import. The composer stored `1 - v` ("the decoder
yields 1-v"), but Forge's decoder yields stored v unchanged and the preview renders
retail correctly with it; glTF and D3D share a top-left origin; EgoCore (in-game
proven import) flips V on neither side. The round trip came back V-flipped versus
retail. The flip dates from the first static import (c9ad03e) and hides on
symmetric/uniform textures. Fixed in meshcompose (static writer, range check,
skinned writer) and the clipping diagnostic; after the fix all 12 meshes match UVs
modulo the packer's whole-tile offset. Pinned tests updated (test_export 3694,
meshinput edge fixture). 62/62 CTest, 91 geometry/URI import checks pass.
NOT yet seen in game: check an imported model with an asymmetric texture.

## Review and rc.3 status (end of 2026-10-26 session)

An adversarial review workflow (4 reviewers, one skeptic per finding) over 7b69640..02c3ef2
confirmed 6 defects, all fixed: rigged export now falls back to static for repeated bone
names/unskinned vertices; merge() makes out-of-range part material indices untextured;
save-root changes rebuild an active Creature look; stricter round-trip criteria (triangle
count, full vertex coverage); stale meshcompose.hpp comment; AUTOMATION.md model_export.
No reviewer found remaining code assuming the old stored 1-v convention.

rc.3 package gate: passes 57/57 selected workflows from the short evidence root
(C:/Users/Cornelio/AppData/Local/FableForgeEvidence/20261026). From the long checkout
build/ root, definition_vectors fails: a 260-char MAX_PATH limit on staged definition
writes (see ROADMAP known limitation). Not a regression in this work.

Resume: rebuild, `python tools/package.py --no-check --version 0.18.1-rc.3 --output-dir
dist/development/20261026-rc3`, run test_package.py with `--work-root` under FableForgeEvidence
and the RC2 selection (FableForgeRelease/rc2-package-selection.json), check CI on the head,
then tag v0.18.1-rc.3 on the branch head, attach the zip + Aeon guide zip, publish as
prerelease. Draft notes: session scratchpad rc3-notes.md (copy kept below if lost):
Creature looks; Export .glb (rigged); fixed flipped textures on model import (in-game check
pending); known long-path limitation.

**Published:** v0.18.1-rc.3 prerelease, tag on 24e817c,
https://github.com/BuffJesus/FableForge/releases/tag/v0.18.1-rc.3 . Archive SHA256
b40259144405cfdc37c2c502597a67a3553ad57ec89e292802cb3bab9d365d9a; gate 124 identities, 158 links,
57/57 workflows (FableForgeEvidence/20261026). Notes: docs/releases/0.18.1-rc.3.md. PR #2 stays
draft/unmerged. Next: in-game look at an imported asymmetric-texture model (V fix), then the
donor-guided character conversion lane (ROADMAP top).

## Donor-guided character conversion: core fitter

`forge::characterfit::fit(input primitives, donor part geometries, options)` aligns an
input humanoid to the donor (height scale, XY centre, feet on feet), assigns each triangle
to the part with the least summed corner+centroid distance to that part's surface (the
centroid-only rule misassigned 2-3% where layers overlap), duplicates seam vertices per
part, and blends each vertex's influences from the part's k nearest donor vertices
(exact hit copies unchanged). Every part keeps the donor skeleton blocks untouched.

`fableforge_characterfit_tests` (CTest: synthetic split/alignment/refusals) and
`--retail <graphics.big> <part ids>` self-fit oracle: the donor's own parts merged into one
input return to their own part on 100% (3912/3914/3913, 4543/4535/4542, 3909/3911/3910)
and 99.97% (5157/5163/5158) of triangles, with exact weights on every vertex.
Next: a CLI that loads a .glb/.obj, fits it to a Creature look's parts and writes one
rigged .glb per part (skinnedexport) for the existing donor import; then GUI.

## creature-fit CLI

`forge creature-fit <model.glb|.gltf|.obj> <CREATURE> --install <root> --out <dir> [--seed n]
[--schema docs/re_reference/def_schema.json] [--neighbours 1-64] [--no-align] [--json]` selects
the look's parts for the seed, fits the model, and writes `<CREATURE>_body<b>_alt<a>.glb` per
part with that part's own skeleton, ready for `creature-import --body b --alternative a`.

End to end: the static seed-3 CREATURE_BANDIT_GRUNT export fitted onto the seed-7 parts
(4543/4535/4542): 1149 + 2282 + 1352 = all 4783 input triangles. Each part passes
`fableforge_skinnedexport_tests --glb <graphics.big> <part.glb> <donor id>` (strict loader,
donor mapping, real composer; all triangles and 26/42/15 donor bones kept). Render:
docs/guide/img/creature_fit.png (head/torso/legs split cleanly). Not imported or seen in game.
Next: GUI entry (Creature looks card: "Fit a model to this look...") and an import-all step.
