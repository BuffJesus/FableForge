# Creature selection and assembly continuation

The checked core appearance reader now has a pure deterministic selection API,
`forge::creatureappearance::select`. All 2,640 corpus selections match execution
of the original native selection routines. It is not connected to scene/Models
rendering yet. Whole-creature assembly, animation and live compatibility remain
open. The retained checkpoint ZIP is unchanged.

Checkout/branch remain the definition-clone GUI lane. The preceding reader change
is aca1c11; [CI 39978691290](https://github.com/BuffJesus/FableForge/actions/runs/39978691290)
passed build, unit tests, WARP GUI smoke and CLI usage. Package/upload were skipped.
Earlier writer/GUI/checkpoint acceptance and original input identities are in
[the profiles handoff](HANDOFF_CREATURE_PROFILES_2026-10-23.md).

Selector CI 39982494520 subsequently passed build, units, WARP GUI smoke and
CLI usage. Package/upload were skipped. This does not add rendering acceptance.

## Selected data and boundaries

`select(prefix, storedSeed, decapitated)` returns chosen body/alternative indices,
mesh IDs, ordered texture replacements, skeletal name offsets/amplitudes, global
texture replacements and the final seed. Input/seed are not mutated. Asset/name
resolution, rendering and animation are separate responsibilities.

The order follows original FableWin routines, not independent per-part seeds:

1. Skeletal groups in their stored clean key order. Key zero considers each row:
   AllowVariation skips the coin, otherwise a draw of 50 or greater includes it.
   Nonzero keys select a candidate only when the coin is greater than 50.
   Selected varying rows consume a unit-range float draw.
2. Draw the body texture color group when the group count is nonzero.
3. For each nonempty body group, draw one alternative, then its texture choices,
   then that body group's texture choices. Decapitation omits group zero only
   after consuming all its normal draws.
4. Select global texture replacements with color group zero.

Texture rows are grouped by original texture ID. Rows with GroupID zero or the
chosen color group are eligible; one eligible row is selected per source key.
The API preserves model/body/global replacement order rather than guessing a
renderer flattening rule. A texture key with no compatible row throws before a
selection escapes. Dirty-map tie sorting and unordered clean maps also refuse;
no unverified sort order or partial fallback is presented as a native result.

GFRandom advances uint32 state by multiply 0x24a1, add 0x24df, rotate-right 13,
then modulo the bound. GFFloatRandom(1) advances the same state and yields exactly
`float(seed & 0xffff) / 65536`, including a possible zero amplitude.

## Original instruction oracle and source acceptance

`tools/audit_creature_body_selection.py` pins the original FableWin SHA256 and
executes bounded original x86 in Unicorn. `--full-morph-set` includes native
GetRandomMorphSet (0289ac90), skeletal selection (0289ad40), skeletal addition
(0289b010), body selection (0289b110), texture selection (0289b300), integer RNG
(018cf370), float RNG (018bac90) and their actual jump thunks. It retains explicit
bridges for containers/strings/output capture and a unit-range x87 remainder
bridge for the external CRT call. This is neither native allocation/ABI nor
engine process/rendering acceptance. Bridge addresses and original byte hashes
are recorded for review; clean maps are the supported oracle fixture contract.

Successive evidence stages under `build/creature-assembly-native-20261023` keep
the distinction between body-only execution with a texture helper bridge,
body+native texture execution, and full native selection. Do not use an earlier
bridged stage to claim coverage of a later native routine.

Final receipt:
`C:/Users/Cornelio/AppData/Local/FableForgeEvidence/20261023/cs-6zq12yea/selection-native-core.json`.
All 330 CCreatureDef records, four seeds and both head states pass 2,640 native
versus independent Python comparisons. The compiled C++ selector matches those
2,640 complete results exactly: final seeds, parts, ordered textures, skeletal
amplitudes and global textures. Original executable/game.bin/names.bin hashes
remain unchanged. The receipt pins C++ executable/schema and producer hashes.

An additional 56 original-x86 synthetic cases cover empty layouts, key-zero and
nonzero skeletal groups, first-coin values 49/50/51 (seeds 32/43/114), texture
layering, color choices and head removal. Core unit assertions pin native vectors
for these threshold/seed/amplitude behaviors and unsupported-table refusals.
The first key-zero probe exposed a missing neutral iterator-increment bridge;
the bridge was added, and the complete final edge matrix passed.

Both scoped executables (`fmplinks`, `defrelocate`) pass. Builds are logged in
`build/creature-appearance-selection-{build,final-build}-20261023.log`.
Combined source hashes/receipt: `build/creature-selection-source-acceptance-20261023.json`.
This adds an unused pure selector; the accepted reader/import GUI gates were not
repeated. Shipping GUI and checkpoint acceptance still refer to their pinned
prior binaries, not an assembled-NPC preview.

The short test window used a fresh process guard/exclusive RuntimeLease. All
owned tests ended and the lease released (`cs-6zq12yea/released-runtime-lease.json`);
Native availability queued `01a16974-9f79-7bd0-a3f6-a6578579e165`. No Forge gap is
reserved. Continue fresh claims for subsequent named test/GUI/game launches.

## Next required integration

### Shared ownership resolver in progress

`defedit::resolveCreatureVisuals` extracts the existing clone graph checks into
a read-only API: root/body/appearance indices, selected CDefs row numbers and
component count. The clone now consumes that proof. Original-parent and correctly
typed unique-row requirements stay intact. Component payload cleanliness,
runtime profile, selection, assets and relocation validity remain separate.

The affected target builds (`build/creature-visual-resolver-build-20261023.log`).
Direct unit assertions cover shared ownership, private-clone resolution and
malformed roots/indices/owners. An unrelated untyped relocation candidate may
pass graph resolution while still refusing clone, preserving that distinction.
After the neighboring CPU tests closed, the scoped unit passed and the compiled
`--visual-graph-audit` matched the independent reader on all 517 roots: 502 accepted
and 15 refused. All source bank hashes stayed unchanged. Receipt:
`C:/Users/Cornelio/AppData/Local/FableForgeEvidence/20261023/vg-g9shta90/result.json`;
executable SHA256 `1e0da7013b333f8b020be529103836a14199a8da414f650cc12e80f52ec5be44`.
`build/creature-visual-resolver-acceptance-20261023.json` pins source/schema hashes.
The fresh bounded lease released after both owned calls closed; availability
queued 01a16995-29f8-7b52-a87a-6db3ce8736ab. No Forge reservation remains. Earlier
refusals occurred before claim; guard hits belonged neighboring TLC CPU work,
not Native. Native's agreed three-capture gap was honored through explicit release.

`tools/audit_creature_visual_resolution.py` independently reads the serialized CDefs
triples and original-parent rows. Its offline run finds 502 accepted and 15
refused roots among all 517 retail Creatures, with input hashes unchanged:
`build/creature-visual-graph-independent-20261023.json`. The refused set contains
the null root, incomplete templates and cooperative spirit definitions; it must
not be turned into a guessed complete NPC. Optional `--reader` compares every
result with the compiled resolver and requires an already guarded launch window.
The original `audit_creature_visual_graph.py` observation workflow is preserved;
its synthetic checks pass.
The independent model checks graph structure; C++ also checks reflected decoding.

### Texture composition source continuation

`composeTextures(part, global)` now produces one type-3 replacement per original
texture ID, retaining first-source insertion order and the last replacement for
that source. It does not chase replacement IDs or mix parts. An empty part map
composes the globals for the ordinary base-mesh path. The selector's ordered
output remains intact; composition is a separate, currently unused API.

Original FableWin `UpdateMorphSet` (0252dbd0) adds global texture entries after
each selected part's entries; with no substituted parts it adds globals to the
base mesh morph set. `FindPEngineMorph` (01e5f4d0) compares the incoming original
ID to the first word of each existing engine morph. `AddTextureMorph` (01e5f6d0)
finds or creates that source entry, clears its blend list when the incoming blend
type is 3, then appends the descriptor. The consequential branch is confirmed at
01e5f743 (`cmp [eax+4],3`), 01e5f747 (skip clear for other types), 01e5f74f (clear)
and 01e5f75e (append). This helper only represents these selected type-3 lists;
it does not implement the general compositor's other blend types.

Read-only exports and PE-byte comparisons are under
`build/creature-assembly-native-20261023`: `composition-*`, `lookup-*`, individual
address `.c` files, `texture-composition-disassembly.txt`, and
`byte-verification.json` (37 verified function ranges). Executable identity is
unchanged from the selector oracle above. This is static instruction evidence,
not a newly executed native composition oracle or rendering acceptance.

Material follow-up: both `GetDiffuseTexture` (02e73b40) and `GetBumpTexture`
(02e810f0) consult `GetComposedTexture` (02ee4d10) using their original material
texture ID. That routine performs one source-key map lookup. For a single blend
entry it obtains `GetBaseTextureIndex` (02ee4f00), which returns the first blend
descriptor's texture ID, and the caller loads that texture. `IsMultipleTextures`
(02ee4ec0) tests whether the blend list has more than one entry. There is no
recursive lookup of the returned replacement in this path. Therefore do not
restrict the composition rule to diffuse IDs when adding material integration;
the bump path uses it too. Other texture channels remain untraced. These exports
are in `materials-*` and `single-texture-*` in the same evidence directory.

The affected unit target builds (`build/creature-texture-composition-build-20261023.log`).
Its added assertions distinguish duplicate-source override, identity override,
cyclic destinations, part isolation and the no-parts global path. The first launch attempt
refused active Native CPU tests before claiming a lease. After a clear guard,
the scoped executable passed in a fresh bounded lease. Receipt:
`C:/Users/Cornelio/AppData/Local/FableForgeEvidence/20261023/ct-m6nzxgb3/result.json`;
executable SHA256 `63f65b3912576a57ed47d2ac6c2fa40508f52d1beb8e81f506c1a1c29b9de9a3`.
The owned process closed and the lease released; availability queued
01a16987-9cc3-7351-8979-62d4326cef22. No Forge reservation remains.

The morphed animating-mesh constructor (01e0a8f0) forwards position/orientation,
animation and scale inputs to the standard animating-mesh constructor (01e0aa00),
which copies the interpolators. There is no added part-local transform in this
wrapper. Confirm the shared caller arguments and skeletal application before
using this observation to combine geometry. Material-channel lookup, skeletal
deformation, renderer integration and representative complete previews remain open.

### Skeletal application trace

Further read-only exports (`skeletal-*` in the native evidence directory) add
three original-byte-matched functions, bringing that verification list to 40.
`AddSkeletalMorph` (026625e0) stores a group/name/value entry and marks the cached
morph dirty. `GetPSkeletalMorph` (02662c80) recalculates only while dirty, clears
the flag and returns the cached counted pointer. `CalculateSkeletalMorph`
(02662960) resets the combined object, resolves each stored name through
`CSkeletalMorphResourceManager::GetMorphTarget`, constructs a parameter for that
target, adds it, sets its current value, then notifies parameter updates. The
random-appearance caller clears/adds only group `RAND` (0x52414e44), leaving
other producers relevant to the final shape.

This confirms that selected name/amplitude pairs need resolved target resources;
they are not an extra shared object scale. Target loading, combination and bone
application still need evidence and implementation. The next narrow native entry
points are GetMorphTarget (023e36e0), PreloadBoneConfigs (023e3f10), and
C3DSkeletalMorphTarget::CombineMorph (0321ce20). No rendering parity is claimed.

Resolve CCreatureDef through the existing checked CDefs ownership graph and use
the runtime component list to distinguish random-morph from ordinary animated
appearance. Do not assume every Creature uses body substitution. Read an existing
TNG `CTCRandomAppearanceMorph.Seed` with its signed decimal uint32 bit pattern.
Absent Seed comes from world construction state, not a universal zero; an Assets
preview needs an explicit preview seed rather than a false native-parity claim.

Trace how UpdateMorphSet/CEngineTextureMorphSet compose duplicate texture sources
before flattening ordered replacements. Recover the shared part transforms and
skeletal morph application from OnAppearanceDraw/engine primitives before joining
meshes in a scene. Preserve the tested pack asset context and distinguish a bind
pose preview from animation. Do not treat one imported triangle as a complete NPC.

Then validate complete representative NPCs, missing-part/refusal behavior, texture
identity and transforms in owned scratch/leased GUI windows. Keep gameplay checks
separate. Dirty-map tie order remains an explicit unsupported selection case until
native sorting behavior is established; reader/writer byte preservation is not a
substitute for that selection contract.

Resource follow-up exports (`skeletal-resources-*`, `bone-config-loader-*`) bring
the original-byte verification list to 44 functions. GetMorphTarget resolves a
name under the bone-config directory and caches the target; PreloadBoneConfigs
enumerates `*.bncfg`. The actual target loader (0287d630) skips to the
`#Start_Bone_data` section, reads bone names and three float components, and
initializes the target from those rows. Earlier creature/group-setting text is
not read as bone rows. Parser token/error behavior and scale combination still
need instruction checks before a new core reader or deformation implementation.

A read-only census of all 75 stored skeletal references finds 15 unique resource
names, all present under retail `data/Bones`; their file hashes are retained in
`build/creature-assembly-native-20261023/bone-resource-census.json`. This validates
resource availability, not parsing, pose or complete rendering.

## Bone scale continuation

Read-only `bone-combine-*` and `bone-keys-*` exports bring the original-byte
verification list to 51 functions. The vector methods invoked by CombineMorph
are explicit subtraction (019072b0), scalar multiplication (018c0af0) and
in-place addition (01ded8a0). Caller disassembly at 0321cf35 loads the incoming
bone's scale at +0xc, subtracts identity, multiplies by the supplied amount and
adds it to the accumulated bone scale. A newly inserted bone starts at identity.
The recovered rule is `accumulated += (incoming - 1) * amount`, componentwise;
it is not multiplication of the incoming scale vectors. Float storage/rounding
and complete original-machine combination probes remain to be checked.

Target Initialise (0321ccc0) excludes rows whose three components are all exactly
one, then sorts. Bone construction (0321cba0) stores CCharString::GetCRC at +8;
CompareCRCs (0321cc20) compares that field as unsigned. GetCRC (0186d2a0) passes
the raw string byte length/data and initial CRC zero to CCRC::Calc. This is a
name-keyed target, not a direct local skeleton index. Ties/collisions and actual
bone application are not yet supported by a new implementation.

`bone-config-shape-census.json` independently checks the 15 referenced retail
resources: 254 named XYZ rows, six exact identity rows, finite float32 values,
and no repeated bone CRC within a target. Each has the observed CRLF start marker
and semicolon-separated row shape. Hashes/source order remain recorded. This is
an offline grammar census, not execution of CStringParser or a core reader.

## Bone-config reader source acceptance

`forge::boneconfig::read` now reads the verified CRLF subset into source-ordered
names, CRCs, float32 XYZ scales and byte offsets. It locates exact start/end bone
markers and leaves unknown header/group-setting text and the input untouched.
The parser accepts one semicolon-terminated row per line, bounds input to 1 MiB,
rows to 4096 and printable ASCII bone names to 255 bytes, and refuses malformed
component counts, nonfinite/out-of-range decimals, NUL input and duplicate CRCs.
It does not claim general CStringParser grammar compatibility.

`prepare` separately validates the names/CRCs/scales, excludes exact identity
rows, and sorts by unsigned CRC. Source rows, including identities and their
offsets, stay intact. Duplicate keys/collisions refuse until native tie behavior
is established. This is an unused read-only core API, not skeleton deformation.

The new `fableforge_boneconfig_tests` target builds and passes, including malformed
rows, identity pruning, source preservation, unsigned ordering and CRC collision
refusals. All 15 referenced retail files match independent readback: 254 source
rows and 248 prepared target rows, exact offsets, names, CRCs and float32 bits.
Every resource hash stays unchanged. The acceptance packet is
`C:/Users/Cornelio/AppData/Local/FableForgeEvidence/20261023/bc-nubgz_lw/result.json`;
binary SHA256 `2e7817e06cc172fa5c377978f3f3acb70ce4525c15a6775d6c7d42e09e4b79fc`.
Source hashes/receipt: `build/bone-config-reader-acceptance-20261023.json`.
Build log: `build/bone-config-reader-build-20261023.log`. The test's read-only
`--read file` mode emits both row sets for further independent comparisons.

These checks exercised a fresh bounded guarded lease; all owned calls closed and
the lease released. Availability queued 01a1699d-5f81-7813-b2a9-0d948a0b80af;
receipt `bc-nubgz_lw/released-runtime-lease.json`. No Forge reservation remains.
No shipping GUI or package was rebuilt for this unused core reader.

Next: verify weighted combination with original instructions and implement the
mapping/application to mesh skeleton bones. Continue scene/Assets integration
through the shared ownership resolver and runtime-profile checks. Do not present
undeformed joined parts as the selected complete Creature.
