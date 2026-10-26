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
At 0x12345678 all 15 Creatures that selected a skeletal target scale 6-15 bones
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
