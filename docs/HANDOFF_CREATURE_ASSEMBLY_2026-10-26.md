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

## Next

1. GUI: Assets/Definitions preview of an assembled Creature with a seed control,
   textures resolved per part through the composed replacements.
2. Base-mesh (no-parts) appearance path from the CAppearanceDef graphic.
3. Animation sampling through the existing animation reader; keep bind-pose
   preview labelled as such.
