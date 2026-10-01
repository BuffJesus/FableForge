# Arena Hall of Heroes content audit

2026-09-29. Read-only inspection of the installed TLC data and native executables;
exports, disassembly and screenshots are scratch artifacts under `build/`.
No install files or placement/geometry source code were changed by this audit.

## Result and unresolved issue

The six authored Hall structural meshes resolve, including all five automatic
mesh-dummy children. No failed structural mesh lookup or whole-room loss in the
last authored LOD was found. This does **not** establish that every asset class
is represented, or that Forge matches the running retail game's appearance.

Both full-detail editor and world screenshots show Hall geometry separated from
doors/statues. Disabling world LOD and culling does not remove that discrepancy.
The current object basis agrees with the inspected native functions; a global
180-degree basis change is therefore not justified by the evidence collected.
The remaining gap is reconciling the authored scene layout with the native
runtime placement/render result, including any scene-specific runtime behavior.

## Loaded and intentionally omitted content

`build/arena-default-world-content-audit.log` / `.glb` use objects, foliage and
creatures, matching the world content filters at the time of inspection:

- 211 TNG things; 40 placed objects; 27 meshes; 168,509 triangles.
- Zero unknown definitions and zero missing meshes; 175 things without a model.
- One unplaced thing is `NAVIGATION_SEED`, without a physics placement block.
- All five automatic mesh-dummy children resolve and are placed.
- Seven particle emitters are omitted by the world loader's particles=false
  option. The particles-enabled export resolves all seven and produces 30 meshes
  and 168,593 triangles (`build/arena-content-audit.log` / `.glb`).
- No baked foliage palette exists for this level in the inspected STB.
- Three `MARKER_LIGHT` and one `MARKER_SPOT_LIGHT` are not rendered lights.

`build/arena-world-content-audit.log` is the earlier **creatures-disabled**
diagnostic (36 placed, 24 meshes, 153,449 triangles), not the default-world result.

## Structural geometry and LODs

`build/arena_hall_geometry_audit.py` independently reads each primitive/static
block through the existing Python mesh parser. Results are in
`build/arena-hall-lod-material-geometry.json`.

| MESH_ARENA_HALL_OF_HEROES suffix | Kept LOD0 triangles | Kept last-LOD triangles |
| --- | ---: | ---: |
| 01 | 2,376 | 1,700 |
| 02 | 3,101 | 2,677 |
| 03 | 1,594 | 992 |
| 04 | 1,663 | 1,019 |
| 05 | 771 | 771 |
| 06 | 774 | 774 |

Last LODs retain the textured material sets and overall bounds. All 14,314
textureless-material triangles excluded from these six LOD0 meshes have zero
area; restoring them would not restore walls. The corresponding excluded
triangles in lower LODs are also degenerate. All six serialized RootMatrix
values (the 48 bytes following bone buffers) are identity. Each mesh has two
static primitives, flags20/stride20 and flags22/stride28, with float XYZ positions,
no animated blocks and no repeated copies.

## Placement fixture

WLD map59 has origin `(2240,3712)` and terrain size96x96. The Hall root placement
is `(29.268799,30.779541,99.029961)`, forward `(.707091,.707091,0)`,
up `(-.000345,.000345,.999994)`. Current assembled local bounds are approximately
X[-27.2,38.95], Y[-26.1,40.45], Z[96.56,120.92]. Several placed props instead lie
to the northeast: chandelier `(57.4641,58.9082,108.4815)`, doors around
`(41.70,74.66)`, `(73.00,43.35)` and `(73.35,74.85)`.

Screenshots `build/arena-content-editor.png` and `build/arena-world-full.png`
use local camera target `(48,48,100)`, yaw.8, pitch.65, distance105. Native game
screenshots from the same scene have **not** been captured for comparison.

## Input provenance checks

Loose `ArenaHallOfHeroes.tng` and its entries in active `FinalAlbion.wad`,
`.atlas-orig`, `.forgebak` and `.pre-secrethunt` are byte-identical: 204,431 bytes,
SHA256 prefix `05c83c40a8804ec4`. All contain the root placement above.

Hall mesh payloads are byte-identical across active `graphics.big`, `.retail-bak`,
`.forge-orig` and `.bak-20260721`. SHA256 prefixes:

| Suffix | Payload hash prefix |
| --- | --- |
| 01 | 15cb4df4e97bce24 |
| 02 | 7c00dbc7c82d8052 |
| 03 | ac3b05c469bec691 |
| 04 | 784b97e295cfa1ff |
| 05 | 1bfab6a6286a8692 |
| 06 | 0f3b1483850606b5 |

These checks exclude divergence among the inspected local copies; they are not
an independent pristine-retail download verification.

## Native transform evidence

Addresses below are `debug_build/FableWin.exe` unless marked retail. Scratch
disassembler: `build/arena_disasm.py`; symbol names from
`D:/Documents/FableTLC/ghidra_out/fablewin_pdb_names.tsv`.

- `CalcObjectMatrix`0x02ee3a00 and installed retail `Fable.exe`0x00bebaa0
  explicitly emit rows `{-right,-forward,up,position}`. `TransformPoint`0x01d129f0
  multiplies a local XYZ vector by those row-axis images, matching Forge.
- `CTCPhysicsStandard::OnSerialise`0x02527070 transfers the TNG forward/up values;
  `SetForward`0x022163a0 and `SetRHSet`0x02526d20 copy without sign conversion.
  `GetRHSet`0x02524bc0 returns that stored frame.
- `C3DMeshObject::SetPositionMatrix`0x031f67f0 independently uses the same signs.
  `CThingBuilding::OnCreate`0x01d814f0 sets graphic and scale without rotating it.
  Static primitive `SetPosition`0x02e976f0 writes CalcObjectMatrix directly to
  its object matrix at +0x80.
- `LoadVertexBuffer`0x032fe5b0 reads compatible buffers directly; other formats
  use `ConvertStaticVBFormat`0x032fb3f0 / `ConvertStream`0x03497390. Float3
  conversion copies XYZ unchanged. `ConvertToVBFormatStaticNonBump`0x032f9bc0,
  `GetPosition`0x032f9e50 and `SetPosition`0x02dc7cb0 also copy unchanged.
- Final static `RenderMeshPrimitive`0x02e9f5f0 obtains the parent's +0x80 matrix
  via `PeekObjectTransform`0x02d7b340 and passes it to
  `CEnginePrimitiveRenderUtil::SetWorldTransform`0x02e81290. Shader-manager
  `SetWorldTransform`0x03032b80 calls `InitialiseTransposed`0x03032be0, which only
  transposes/copies it; the fixed-function path0x030e9d60 copies its coefficients
  into a D3D world matrix. No additional axis flip was found at this boundary.

Evidence files: `arena-transform-disasm.txt`, `arena-transform-path.txt`,
`arena-matrix-upload.txt`, `arena-vb-copies.txt`, `arena-vb-getset.txt`,
`arena-load-vertices-audit.txt`, `arena-load-vertices-tail-audit.txt`,
`arena-stream-conversion-audit.txt`, `arena-building-create.txt`,
`arena-static-transform.txt`, `arena-final-render.txt`,
`arena-final-world-matrix.txt`, `arena-final-transpose.txt`, all under `build/`.

Existing Arena/Oakvale basis evidence is recorded in `docs/TODO.md`: Arena gates,
audience orientation and Oakvale fence joins motivated the current convention.
Those older visual comparisons were not rerun by this audit. Any future basis
change needs those fixtures plus an independently verified Hall runtime result.
