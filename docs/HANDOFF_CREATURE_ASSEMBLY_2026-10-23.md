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
