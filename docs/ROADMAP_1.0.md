> Active continuation: [paired Creature clone CLI](HANDOFF_CREATURE_CLONE_2026-10-10.md). Thirty CLI cases, six retail-copy cases and six extracted workflows pass, including rollback/Restore and 44 existing GUI clone neighbors. Distinct Creature GUI action is next; own appearance/body cloning remains open. RC2 unchanged.

# Albion Atlas -> 1.0: what it should do, what it can't, and how we get there

Ordered dialogue-tag controls pass source checks at both window sizes and an
independent retail-copy preservation/Restore check. Signed stored positions remain
explicit; new tag names are bounded ASCII and original legacy names survive other
edits. Extracted-checkpoint validation passes for these controls.

Text editing now uses the existing pack chooser and writes the selected pack's
language-bank tree. Reads prefer that pack's bank, then the save folder, then base;
first save copies the loaded bank with transactional creation tracking. Existing
pack banks retain one-time originals. Drafts capture destination identity and
refuse switches or invalid/missing pack manifests. No manifest format extension.
An end-to-end scratch gate edits speaker/index, references and tags, adds a string,
then composes with a sibling's unrelated string edit; both window sizes preserve
source and manifest, validate destination/manifest refusal and restore the bank.
Restore exposed a CLI gap: explicitly selected tracked folders without game.bin
were refused. Backups/Restore now accept those folders with recognized records
or a stage manifest; empty unrecognized folders and implicit invalid installs still
refuse. Existing running-owner/alias and stage preflight remain unchanged.
Both-size pack gates, neighboring tags/folder gates and text-only physical/junction/
stage/offline recovery pass. Initial gate raced the reload before its destination
message; corrected barrier. Initial GUI Restore targeted the original install;
using explicit CLI pack root exposed and fixed the actual missing-game.bin gate.
Evidence: build/text-pack-{destination-final-checks,restore-text-only-checks,
tags-regression,folder-regression,composition-probe,destination-probe}-20261009.log.
Registered text_pack_destination and text_only_restore pass in the retained
pack checkpoint below. No live-game claim.

Single-byte text metadata display and JSON/TSV export now pass both-size all-byte
checks and a complete retail TSV comparison without source changes. Storage stays
raw; presentation maps bytes to Latin-1 code points in UTF-8. Existing metadata
editing and strict lookup checks pass, including the extracted checkpoint.

Open text drafts now participate in close protection. Both-size real modal/save
checks pass review/save, refusal retention, Cancel/Discard and mixed text/lip-sync
review order, including the extracted checkpoint.

Current retained checkpoint: dist/development/20261009-text-close,
ZIP SHA256 6d0b5c94b4231fad6620c0b2634e48f54426cde452558eb92d1825f931edc330.
Extracted identities (109 files), documentation links (127) and eighteen selected
workflows pass; 88 not run, zero skipped. Evidence: build/text-close-prompt-
{package,extracted-package}-20261009.log and C:/Users/Cornelio/AppData/Local/
FableForgeEvidence/20261009/package-smoke-3vqz1qzl. Superseded byte-export snapshot
removed after archive/executable checks; metadata retained under snapshot-cleanup/
text-byte-export-checkpoint-replaced. Only this snapshot remains. The broad gate
was not repeated; no release/live-game claim. Older checkpoints are historical.

Current retained checkpoint: dist/development/20261009-text-byte-export,
ZIP SHA256 86942c55ac92ec8bc23a5e4b22a2a63d7af7e06039f5d02d5a6690a01c1ff29e.
Extracted identities (109 files), documentation links (127) and seventeen selected
workflows pass; 88 not run, zero skipped. Evidence: build/text-byte-export-
{package,extracted-package}-20261009.log and C:/Users/Cornelio/AppData/Local/
FableForgeEvidence/20261009/package-smoke-8x_xffw9. Superseded pack snapshot removed
after archive/executable checks; metadata retained under snapshot-cleanup/
text-pack-checkpoint-replaced. Only this snapshot remains. The broad gate was not
repeated; no release/live-game claim. Older checkpoint statements are historical.

Current retained checkpoint: dist/development/20261009-text-pack,
ZIP SHA256 d9e59452367c2bc9c3f734fc823bf7cd5deb6d35d04a129d9ddf98f1f1c0a0f2.
Extracted identities (109 files), documentation links (127) and sixteen selected
workflows pass; 88 not run, zero skipped. Evidence: build/text-pack-
{package,extracted-package}-20261009.log and C:/Users/Cornelio/AppData/Local/
FableForgeEvidence/20261009/package-smoke-b82gakcd. Superseded tags snapshot removed
after archive/executable checks; metadata retained under snapshot-cleanup/
text-tags-checkpoint-replaced. Only this snapshot remains. The broad gate was
not repeated; no release/live-game claim. Older checkpoint statements are historical.

Current retained checkpoint: dist/development/20261009-text-tags,
ZIP SHA256 a188b7a2700750ffeb2ff18ae710b800dcca6cb518107fd3c75c58c1d032e816.
Extracted identities (109 files), documentation links (127) and fourteen selected
workflows pass; 88 not run, zero skipped. Evidence: build/text-tags-
{package,extracted-package}-20261009.log and C:/Users/Cornelio/AppData/Local/
FableForgeEvidence/20261009/package-smoke-qtgcc4pa. Superseded metadata snapshot
removed after archive/executable checks; metadata retained under snapshot-cleanup/
text-metadata-checkpoint-replaced. Only this snapshot remains. The broad gate was
not repeated; no release/runtime cue/in-game claim. Older checkpoints are historical.

Current retained checkpoint: dist/development/20261009-text-metadata,
ZIP SHA256 010d5675055aa3fc09521697cd27289035a475acb195dd817e811fcd4853f991.
Extracted identities (109 files), documentation links (127) and thirteen selected
workflows pass; 88 not run, zero skipped. Evidence: build/text-metadata-
{package,extracted-package}-20261009.log and C:/Users/Cornelio/AppData/Local/
FableForgeEvidence/20261009/package-smoke-zplpfd__. Superseded speaker-picker
snapshot removed after archive/executable checks; metadata retained under
snapshot-cleanup/speaker-picker-checkpoint-replaced. Only this snapshot remains.
The broad gate was not repeated; no release or in-game validation claim.
Older checkpoint statements below are historical.

Text drafts now edit speech-bank references and existing-string identifiers.
New values are ASCII and shorter than 4096 bytes; empty values are allowed.
Unsupported original byte values remain read-only and survive other edits.
Donor additions retain the new record name as their identifier. Tags, record IDs
and unrelated headers remain intact. Both-size real-input checks, stale/refusal/
Restore cases and neighboring content/donor/folder/speaker workflows pass.
An independent retail copy changes only ID 1's two references, preserves all
28,913 headers and other payloads, and restores exactly; actual source is unchanged.
Evidence: build/text-metadata-{corrected-checks,retail-copy,speaker-regression,
content-regression,donor-regression,folder-regression}-20261009.log; native copy
C:/Users/Cornelio/AppData/Local/FableForgeEvidence/20261009/retail-text-metadata-copy-irh6ytjv.
The initial new-record test incorrectly expected donor magic rather than the
required BIGB magic 42; corrected test passes. No audio playback or in-game claim.
These controls are included in the validated metadata checkpoint above.

Current retained checkpoint: dist/development/20261009-speaker-picker,
SHA256 1ef5215913ff81f509fa027f1fb6c77d4ec6bb68f22ee2a1ff038d00ecf7f320.
Twelve selected extracted workflows plus identities/documentation links pass;
88 not run, zero skipped. Superseded CLI-speaker snapshot removed after validation.
Older checkpoint statements below are historical.

Text drafts can choose an existing narrator,
updating speaker plus header index for edits and donor additions. Both-size real
dropdown/refusal/Restore/folder-copy checks and neighboring edit/add gates pass;
retail-copy speaker-only checks preserve all other fields and the source hash.
Extracted package validation passes; other metadata fields remain read-only.

Current retained checkpoint: dist/development/20261009-text-speaker,
SHA256 df3d02dc7739a7e2c83a6e3427f30c86707b973e304d28f788f9d5c877f69797.
Eleven selected extracted workflows plus identities/documentation links pass;
88 not run, zero skipped. Superseded text-folder snapshot removed after validation.
Older checkpoint statements below are historical.

CLI text set/import synchronize speaker text
with its narrator-list index and verify the full payload/header before publication.
Twenty speaker/refusal cases and existing publication/ID/import checks pass;
retail-copy replacement/addition checks preserve every unrelated record and the
actual source hash, including the extracted checkpoint.

Current retained checkpoint: dist/development/20261009-text-folder,
SHA256 b113225116b11069b74f0b5bdafa1e838888616c70938ff9f351f2249cae2a13.
Ten selected extracted workflows plus identities/documentation links pass;
88 not run, zero skipped. Superseded restore-alias snapshot removed after validation.
Older checkpoint statements below are historical.

Text edits/additions can create a bank copy in
a separate folder, track it as created and remove it with Restore. Both-size
preservation/recovery-record/external-destination checks and existing edit/add/
publication regressions pass, including the extracted package.

Current retained checkpoint: dist/development/20261009-restore-alias,
SHA256 2598a266c5487d8cd0c11647e07072f334339762a781fe7d42e206247ebd51d8.
Nine selected extracted workflows plus identities/documentation links pass;
88 not run, zero skipped. Superseded donor snapshot removed after validation.
Older checkpoint statements below are historical.

Restore protects text targets reached through
language-folder junctions, including stage-created files without original backups.
The shared running-game check resolves install and executable paths, protecting
whole-install aliases for other file types. Alias/refusal/offline-retry checks,
32 text publication checks, both-size multilingual Restore and staged recovery
regressions pass on owned scratch fixtures and the extracted checkpoint.

Current retained checkpoint: dist/development/20261009-text-donor,
SHA256 06f4e0e9ec61f796498f93b45be8a105d6e822c82e684c1fb743f596a66a59f7.
It includes donor addition, content editing and preceding text/restore fixes.
Eight selected extracted workflows plus identities/documentation links pass;
88 not run, zero skipped. Older package statements below are historical; the
superseded snapshot was removed after validation of this replacement.

Assets > Text can add a string from a donor, copying its metadata and allocating
a free ID. Both-size GUI/refusal/Restore gates and a retail-copy addition pass;
all 28,913 original records remain intact and the real bank hash is unchanged.
Metadata editing, pack output and missing-bank creation remain pending. Automation
Reveal now waits for an actually visible click centre; property/browser and partial
Restore regressions pass. This addition is newer than the text-edit checkpoint below.

Current retained checkpoint: dist/development/20261009-text-edit,
SHA256 785faa19a0d5878a04b0cda1c19a17bf40f8c5c2fcba772a174e67bec0b6690b.
It includes content editing and the preceding text fixes. Seven selected extracted
workflows plus identities/documentation links pass; 88 not run, zero skipped.
Older checkpoint paths/statements below are historical; their snapshots were
removed after validation of the replacement. A read-only independent layout
check accepts all 26,807 retail English string payloads, with the source hash
unchanged; in-game editing validation remains pending.

Assets > Text edits existing string content with confirmation, metadata/original
preservation and stale/noncanonical payload refusal. Desktop/small-window edit,
restore and browser checks plus 32 shared CLI publication cases pass. Missing-bank
creation, pack output, new strings and metadata edits remain pending. This feature
is newer than the retained text-recovery checkpoint below.

Current retained local checkpoint: dist/development/20261009-text-recovery,
SHA256 703ea90d0dca6b93f25111e8f62cf4243a305c360f86a83943f2f7c76c3c8b74.
It includes the CLI text and multilingual restore corrections below. Six extracted
workflows plus file identities/documentation links pass; 88 workflows were not
run, zero skipped. Older package paths/statements below are historical and their
snapshots were removed after validation of this replacement.

Restore discovers backups in all immediate localization-language directories,
waits for text-bank readers and refreshes the Text browser after restoring files.
Desktop/small-window multilingual and partial-failure retry checks pass. This
correction is newer than the retained local checkpoint.

CLI text set/import verify prepared banks before transactional publication,
retain in-place originals and refuse running standard text-bank destinations.
Thirty-two publication/backup/running-install/alias checks and existing text writer
regressions pass. This correction is newer than the local checkpoint.

CLI text show now rejects partial numeric tokens and ambiguous identities,
and displays narrator header names. Twenty-one lookup/header cases, four decode
boundary cases and retail name/ID lookups pass without changing bank bytes.
This CLI fix is newer than the checkpoint below.

Text workflows and preceding source fixes are in the validated local checkpoint
dist/development/20261009-text-browser (SHA256
7623f34eb57c34e431511f914ba104f3dfb081623e3a04c5626a91be97ede81d).
Extracted file identities/documentation links and defs/GUI/Text checks pass;
88 other package workflows were not run, and the historical full gate was not
repeated. Superseded development snapshots were cleaned up at the user's request.

Readable-object and information-display properties now link directly to the
read-only text browser, including first-load lookup and missing-reference
handling. Desktop/small-window checks preserve the map and source banks.

Assets > Text now browses localization strings/groups with language selection,
content search, read-only content/metadata, member links and per-language output
fallback. Desktop/small-window regressions pass alongside neighboring asset
workflows. Retail English indexes all 28,913 entries with no string/group decode
errors, and its bank hash is unchanged. Narrator preview matches all 379 original
header names; all 52 core groups and desktop/small-window workflows pass.
TSV export independently matches all 28,913 original entries and metadata while
preserving the source bank and existing output files. Editing/addition remain
pending. This browser is included in the local ZIP, newer than the last full gate.

Direct mod build/merge now refuses a running output installation before preparing
or committing files. Ten publication checks and 58 ownership checks in each
ordinary/complete-data mode pass. Included in the local checkpoint; no full-gate
rerun.

Static replacement now also requires a canonical root matrix, refusing donors
whose metadata the writer would discard. All 52 core groups, 16 replacement CLI
cases and three screen workflows pass. Included in the local checkpoint;
complex root metadata remains unsupported.

Static import refuses selected-scene skin instances before writes instead of
silently dropping their joint pose. Unused declarations remain accepted. All 52
core groups (732 model-input checks), 91 geometry CLI checks, 14 replacement
checks and three replacement screen workflows pass. Skin baking/import remains
pending; this guard is included in the local checkpoint.

glTF import retains distinct material-array identities even when display names
repeat, with a separate omitted-material default. All 52 core groups (729 model
input checks), 90 geometry CLI cases, 14 replacement cases and three replacement
screen workflows pass. This and save-folder model preview corrections are
included in the local checkpoint.

Existing simple static mesh geometry can now be replaced through CLI and Models,
retaining identity, materials, textures and collision. One-LOD canonical donors
without helpers, bones, cloth or radius overrides are supported; input material
slots must match. All 52 core groups, 10 replacement CLI cases and three GUI
workflows pass, including separate output-folder preservation. Complex donors,
new collision, recipe integration and in-game validation remain pending.

Editor matrix conversion retains large finite scales and direction vectors, and
rejects non-finite matrices/unrepresentable output without changing the caller's
frame. All 52 core groups plus owned-movement and selection workflows pass. This
correction is later than the finite-model-writers ZIP.

Direct and owned object transform writers reject non-finite frames before
mutation or undo, including preservation of existing redo. All 52 core groups
and placement/track-preview editor workflows pass. This guard is later than
the finite-model-writers packaged checkpoint.

Typed definition edits now retain representable subnormal floats instead of
rejecting them as underflow. Complete finite tokens remain required, including
refusal of underflow to zero. All 70 direct writer checks and 52 core groups pass;
this parser correction is later than the finite-model-writers ZIP.

Typed definition float/vector edits reject NaN and infinity before writes while
preserving unrelated legacy bytes. All 53 direct writer checks, 52 core groups
and 90 geometry CLI checks pass. The new test is registered in the full gate;
the historical full-gate result is not a run of this newer source.

Imported object height/radius calculations avoid overflowing intermediate float
subtractions. Both wide/tall successful imports independently verify finite saved
definition fields; 52 core groups and 90 geometry CLI checks pass. This source
change is later than the lock-and-polygon ZIP.

Active glTF morph shapes now refuse with bake-to-static guidance before any bank
write; zero effective weights and node-over-mesh defaults are handled explicitly.
All 52 core groups, 726 model-input and 88 geometry CLI checks pass. This change
is later than the lock-and-polygon ZIP; details and limits are in the handoff.

After the concave-obj package, OBJ faces with an attributed corner on a straight
edge now avoid collapsed fan triangles while retaining every corner. All 52 core
groups (25.23 seconds), 699 model-input and 85 geometry CLI checks pass. Source
also includes the validated CTCPhysicsLight position lock guard. Both changes
are included in the lock-and-polygon development ZIP, which passes packaged GUI,
109 identities, 127 links and extracted 85 geometry/88 storage checks. A further
18 concave straight-edge cases pass (717 model-input total). Retail and the full
workflow gate were not rerun for this package; the handoff records exact scope.

Concave OBJ polygon import now produces nonoverlapping triangles with winding
and corner attributes retained. Simple planar faces up to 1024 corners are supported;
unsupported polygons ask for exporter triangulation before any bank write.
All 52 core groups, 689 model-input and 85 geometry CLI checks pass. Latest
concave-obj local ZIP includes component float underflow prevention and passes
packaged GUI, identities/links and actual extracted 85 geometry/88 storage checks.
Retail smoke was not rerun for this ZIP; handoff records hashes and scope.

Confirmed native top-level float fields retain their type after integer-spelled
edits, and the property grid refuses float overflow before mutation/undo.
All 52 core groups and two editor workflows pass. Latest native-property-types
local ZIP includes preceding fixes and passes packaged retail/GUI plus all 88
extracted-tool CLI checks. Handoff records hashes and targeted validation scope.

Brain and switch name authoring now enforces the same native identifier rule
as storage review before mutation or undo, while allowing correction of quoted
legacy values. All 52 core groups, 88 CLI checks and two editor workflows pass.
The latest native-name-authoring development ZIP includes section whitespace,
numeric diagnostics and property/list guards; packaged retail/GUI and all 88
extracted-tool checks pass. Handoff records exact hashes and validation scope.

Property-grid edits now refuse CR/LF/NUL before mutation, and list entries
also refuse NUL. Combined changes pass 52 core groups and editor list/undo
checks. The latest property-list-lines local ZIP includes the preceding section
fix and passes packaged retail/GUI plus 84 extracted-tool CLI checks.

The later TNG section fix admits native day/night section suffixes and refuses
unsafe new headers before mutation. All 52 core groups, 84 storage CLI checks
and owner/daynight GUI pass. The tng-section-contract local ZIP passes packaged
retail/GUI and all 84 extracted-tool checks. Handoff records hashes and scope.

The corrected October 9 fixed-build broad gate now completes with all 190
workflows passing. Lipsync empty/non-type-1 entries and graphics-inclusive
overhaul overlap retain their stated limits. Two subsequently validated guards
are promoted: native ScriptName identifiers across review/authoring/track edits,
and finite complete minimap numbers before WLD mutation. Combined affected core
tests and original-file audits pass in isolation; all ordinary Release targets
build, 52 core groups and 76 public storage CLI checks pass after promotion.
Editor placement, track preview and property override workflows also pass.
The local native-writer-guards ZIP passes packaged retail/GUI, file identities/
documentation links and all 76 extracted-tool storage CLI checks; other packaged
workflows are not claimed as run.
See the current handoff for checkpoint
hashes and the separation between the full gate and these later changes.

WLD initial quest names now use the native top-level string-token contract:
numeric, quoted and digit-first names refuse at their source line. All 71 public
CLI checks, Release targets and 52 core groups pass; original WLD/BWD inventories,
region queries and protected source/plan hashes are unchanged. This validates
storage tokens, not quest registration or runtime activation. Both corrected
GB level/WAD fixtures pass with bulk graphics explicitly excluded; original
graphics-inclusive overlap remains unverified. Fresh Expanded Chapters complete
offline workflow now passes bank/text, input preservation and packed level checks.

Canonical retail DoorTriggerType 1 is now preserved as an inert CTCDoor
annotation, not a compiled-door trigger override. Native serializer evidence,
unit/67 public CLI checks, original retail file review and all Release builds
pass. Seasons loose storage accepts 322/449, Secrets 320/449; both original
WADs accept 308/398 with remaining opaque-script refusals. Input/namespace-plan
hashes are unchanged; all 52 core tests pass. Source admission is separate.
The broad run completes with 182 passes/eight original failures; focused
corrections and remaining limits are recorded in HANDOFF_WORLD_UI. Lost Content
level/asset diagnostics pass after explicitly excluding definition/language
banks. Expanded Chapters' original composed output passes exact bank/text and
798-source/583-built level audits against a pristine baseline.

Empty CTCAIScratchpad blocks now pass storage review, matching original retail
authoring and native serializer evidence. Nonempty gossip data remains refused;
this does not admit entire overhaul sources. Focused unit/public CLI checks,
unchanged-input Seasons/Secrets lookup inventories, all Release builds and
all 52 core tests pass. The latest packaged minimap snapshot predates this fix.
Broad regression triage also corrects obsolete Seasons/Secrets rejection
expectations to audit original bank/text preservation. Dragon Cliff passes
with pristine backups and a complete language baseline; other full-corpus
reruns are ongoing. See the current handoff for evidence and unresolved limits.

Region minimap setters now reject nonempty numeric/punctuation names before
mutation, following the verified native identifier consumer. Core worldinstall
and 16 CLI preservation cases pass; valid names/text and exact Restore pass.
Core empty minimap clearing removes its optional directive, preserving unrelated
bytes and a byte-identical repeated clear; fifty retail regions use omission.
Focused region recovery and all 52 core groups pass. CLI, tools, core tests
and the ordinary GUI have been built with the writer fix; all Release targets
now build after the owned new-level fixture released the CLI output.
A separately named GUI linked against the updated core passes a fresh, waited
smoke run including map loading, view modes, textured export and Setup controls.
The ordinary GUI/tools targets also build. The updated development ZIP passes
109 identities/127 links, retail/GUI workflows and extracted-tool region input
and recovery tests. The final complete-build ZIP repeats those extracted checks
successfully; the broader suite and release acceptance remain incomplete.

October 9 broad validation remains incomplete. Retail texture fixture setup
hit a 90s limit; phase timing shows 0.52s serialization and 80.34s output I/O.
The failure/retry UI gate now explicitly uses a bounded retail-derived bank,
with full-bank evidence kept separate. Stable unnamed whole-bank FMP fixtures
now check supported payload composition and actual identity-table refusal;
the focused reference/choice suite passes. The earlier development ZIP passes
three extracted-package workflows, with 87 not run. See the current handoff
for hashes, evidence paths and the remaining broad gate.

Model imports now refresh the texture list used by material links. Five editor
workflows preload the old list, import, follow the new link and verify exported
pixels at both normal and 125% scaling. Model name/ID ambiguity checks also pass;
see the 2026-10-09 continuation in HANDOFF_WORLD_UI.

glTF triangle indices now reject explicit buffer-view strides before import
staging. All 617 model-input checks, 78 CLI preservation checks and six compact
editor workflows pass; valid packed indices and interleaved attributes remain
supported. All Release targets build, all 52 core groups pass and six cached
Khronos sample models retain expected geometry and source hashes.

Dense normalized UV attributes now reject unaligned offsets/elements using
glTF's four-byte vertex alignment rule. All 620 model-input checks, 81 CLI checks,
ten sparse and sixteen topology checks pass, along with six cached sample models
and all 52 core groups. Details are in the October 9 handoff.

WLD MiniMapGraphic now rejects numeric/leading-digit tokens using its verified
native string consumer. All 57 public storage checks and 52 core groups pass;
both original packs' world-region inventories, namespace plans and hashes remain
unchanged. Source admission is still pending.

Texture replacement now keeps the slot's allocation, frame dimensions and
supported transparency mode when resizing an image. CLI replacements preserve
the original format unless a conversion is requested. The writer validates
before writing and refuses animated/volume layouts
that the single-image writer cannot preserve. Existing DXT1/DXT3 cutout slots
now retain their alpha and native cutout setting; other unsupported transparency
combinations and cutout format conversions still refuse. Thin texture mips average
along their remaining long axis, and DXT3 alpha rounds to its decoded grid.
Compressed mip chains now stop at the native shorter-side boundary; new DXT
images allocate at least 4x4, and undersized explicit allocations refuse.
New image imports use native static-image entry type 0; volume replacement
refusal also checks archive type and one-slice depth. Direct texture/model imports
use native marker 42 for new BIGB records while preserving existing donor records.
Collision-reference diagnostics retain unsigned IDs and honor the native no-hull
sentinel. See the 2026-10-08 handoff for regression and original-bank evidence.

Static glTF imports now support triangle strips/fans and sparse scalar/vector data with bounded source
reads and checked replacement indices. The Khronos sparse sample imports with
verified geometry/collision and unchanged source files. Mesh material texture IDs
retain all 32 unsigned bits through import, preview and links. Consumed glTF
attributes enforce core component encodings, and OBJ coordinate tokens reject
trailing garbage. Imports also report texture coordinates that exceed the writer's
range. All 52 core tests pass; see the 2026-10-08 continuation in HANDOFF_WORLD_UI
for focused/CLI/UI evidence.

Default-only scripted Hook review now accepts six more loose TNG files per source
and seven more WAD TNGs. Preserving the inert legacy light annotation
`Interted FALSE` adds one more Secrets file: Seasons 317/449, Secrets 316/449
loose text files, 303/398 WAD TNGs each. The annotation is not an alias for
`Inverted`. Nonzero replacements, opaque script data and source admission remain
refused. Both namespace plans preserve the expanded definition-name candidates.

Main particle BIG version 111 now has strict archive-envelope review. The nominated
baseline effects bank passes all 1,165 metadata/payload spans; particle payload
semantics remain explicitly refused. All 52 core and 32 storage CLI checks pass.

Ogg page-envelope review now complements codec decoding. All eight source Ogg
paths pass CRC/framing/termination checks, and all nine loose media pass both
checks. Scratch trailing/truncated Oggs still decode but fail storage review.
All 52 core and 27 storage CLI tests pass; source admission remains separate.

BIG storage diagnostics now validate the full supported archive envelope before
entry payloads. Both original frontend banks pass; nonstandard zero record markers
remain explicit refusals in the other source BIGs. All 51 core and 23 storage-CLI
checks pass. Existing payload-only evidence is not whole-archive acceptance.

BIGB serialization now rebuilds the native directory-length header and honors
each bank's payload alignment. Independent scratch rewrites of all eight source
BIGs preserve every entry's metadata/payload; FMP headers remain separate.
All 51 core tests pass (26.17s), plus FMP/bank refusal checks. See HANDOFF_WORLD_UI.

Strict lighting-table TGA storage review is available through the core and
`world review-storage`. Secrets' original image and all audited colour-table
coordinates pass; Seasons' source-local image remains unresolved. All 50 core
tests and 21 world-storage CLI checks pass. This does not admit either source.

World diagnostics now separate initial quest script references from definition
queries. LEV atmosphere auditing checks ENVIRONMENT_THEME_DAY and source/planned
class ordinals against the byte-sized cell index; a regression catches unchanged
name candidates that grow to ordinal 256. All 16 original atmosphere names fit
both plans, with zero new class/index findings. Source admission remains pending.

BIN saves refuse a record exceeding the 64-KiB decoded chunk before opening
outputs. The new boundary/preservation test brings the core gate to 50 passing
tests (39.48s); all 17 whole-bank scratch checks pass. Names loading also rejects
undeclared trailing records/bytes rather than dropping them on a subsequent save.

Native ReadTable/WriteTable evidence fixed names.bin's emitted stream length:
it includes the four-byte prefix at +16. Nine original-bank scratch saves now
preserve exact names bytes. Dedicated length-bounded tests cover no-op saves,
appended names and legacy-header normalization. All 49 core tests pass (28.01s)
and all 17 whole-bank checks pass; details in HANDOFF_WORLD_UI.

BIN framing now rejects skipped/overlapping record ranges, malformed payload
offsets and partially consumed zlib streams. All nine baseline/source banks
satisfy the checked framing. The 24 new boundary checks, both original namespace
audit orders, 17 scratch whole-bank checks and all 49 core tests pass (25.09s).
Empty banks/payloads remain valid; no source-admission or composition claim.

2026-10-08 complete STB ownership/storage review is now reusable core code.
It validates metadata, common-map bindings, all runtime structures and decoded
frames, sea entries, palette bounds and zero unowned padding. Both original
pack paths pass (one byte-identical bank): 473 entries / 446 maps / 140,025
frames / 26 seas. All Release targets build and all 46 core tests pass.
Compiled-world review also passes both original BWD paths (one identical
446-map / 141-region file), with independent exact round-trips and local-slot
bounds checks. The subsequent full 46-test gate passes.
Complete supported LEV storage now passes 448 loose maps and 398 WAD payloads
per original pack, including both map-header versions and native negative sound
sentinels. Strict navigation storage reuses the canonical reader. All Release
targets build and the full 48-test gate passes. WAD sparse-ID correction passes
the full 49-test gate. Compact WAD metadata/envelope review passes both original
796-entry archives with unchanged hashes and another full 49-test pass.
Strict WLD directive review also passes all four original world-file paths,
with a full 49-test pass. Strict TNG review now covers nine thing types,
reviewed scalar/physics/camera/light components, bounded spline keyframes,
ownership and container domains. Seasons accepts 311/449 loose text files,
Secrets 309/449; each accepts 296/398 WAD TNGs. GTG wrappers
are reviewed but their original bodies still refuse unsupported content.
Remaining files explicitly refuse, including opaque script data and overflowing
UIDs. All 49 core tests pass after hero/spline-event coverage (24.85s).
Read-only `forge-tools world review-storage <file> [--json]` now exposes these
checks, including per-entry WAD refusals and explicit no-source-admission scope.
Its 16 end-to-end CLI checks pass, as do all 49 core tests (17.94s); eight original
inputs across all seven formats retain their hashes after review.
Text refusals now include source lines and directives, with GTG map/body offsets;
the follow-up 49-test gate passes (24.64s) and original-file locations agree.
LEV exact-name inventory finds four missing palette names and one wrong-class
name in each loose source; WAD LEVs have three missing names. Original hashes
remain unchanged. The new report preserves these gaps; class/runtime and
composed-name resolution remain separate from storage acceptance.
Both namespace orders preserve all collected LEV CRC candidate sets, while the
global name audit exposes expansion of the duplicate silver-key chest name from
two source rows to four planned rows. Numeric relocation still has zero unresolved
BIN candidates (241/13 rewrites). Duplicate runtime selection remains unproven.
WLD/BWD region-definition lookup candidates are also unchanged in both plans;
all nonempty fields have unique REGION candidates. Original WLD/BWD differences
remain explicit. The public report has 17 passing CLI checks; lookup audits have
four regressions, including stale-report refusal without overwriting output.
Region asset inventory follows the GUI minimap table and native text fallback:
all populated minimap keys map to unique type-0 texture IDs under both PC/default
GUI definitions. Unmatched filler display labels follow the literal fallback.
Asset payload certification and active/composed GUI selection remain separate.
Fully reviewed TNGs now expose DefinitionType candidates: 684/519 loose
references and 955 per WAD retain unique mapped candidates in both plans.
Refused files and GTG bodies remain outside this field inventory. A shared core
extractor additionally covers proven atmosphere/family/hero/container names:
40/34 loose references and 43 per WAD, all nonempty candidates unchanged in
both plans. Conditional and other component references remain excluded.
All Release targets build and 49 core tests pass (24.06s).
The CLI has 18 passing checks, and the new diagnostics run in check_all.py.
Accepted GTG bodies now share that reference inventory, with map slots and
file line numbers. Any refused body suppresses the complete file inventory.
Original GTGs remain refused and original coverage/counts remain unchanged.
The subsequent full 49-test gate passes (24.81s).
Bounded original FableWin sort execution demonstrates table-dependent duplicate
ordering for the chest name; no stable duplicate winner is assumed. The lookup
audit now includes anonymous rows for nonempty CRC-zero queries, with a concrete
collision regression. Refreshed original reports preserve all earlier findings
and input hashes. These conditional debug-build results do not admit sources.
Core analyzeNameLookups now exposes these candidates and source changes after
CRC/pool/map consistency checks. Both original namespace audits independently
agree with the core output; allocation and composer policy remain unchanged.
All Release targets build and 49 core tests pass after this change (25.66s).
Read-only defs review-namespace now exposes that analysis publicly. Eleven
CLI checks pass; both original orders report the expected chest-name ambiguity
and candidate changes with unchanged input hashes. Payload relocation and source
admission remain explicitly unchecked by this diagnostic.
BIN reading now rejects unterminated names and invalid nonnegative name offsets
instead of losing their identities. Seventeen namespace CLI checks and seventeen
scratch whole-bank checks pass, both original audits preserve their inputs, and
the full 49-test gate passes (26.93s).
Remaining TNG/GTG and symbolic/source admission remain in progress.
Remaining world-file admission and divergent-namespace composer integration
are still pending; see HANDOFF_WORLD_UI.md for scope and reproduction.

2026-10-06 Forge continuation adds a read-only namespace allocator preserving
baseline identities and distinct additions. All 42 core tests pass; independent
original Seasons/Secrets identity audits pass both orders (15,386 planned slots).
Follow-up building/hero/brain/shop/creature/control recovery and primitive
boundaries, native expression lookup and nested sound/opinion/UI layouts now
leave zero unresolved BIN candidates in both orders (241/13 verified rewrites),
with independent exact payload-delta audits. All 42 core tests pass after the
final increment. Original FMP appearance regression
passes 204 references and 34 safe negative routes after shared-parser changes.
Loose/WAD inventory covers 1,696 payloads; observed matches are unrelated UID
or float substrings. BIG/STB raw inventory covers 44,569 entries; 306 candidate
groups have verified texture/text domains, 480 remain unclassified. Compressed
STB inventory decodes 140,025 frames; all 4,035 candidate-bearing frames now
have structural owners. Complete native-layout parsing places 4,096 occurrences
in 3,446 local-detail frames in float/normal-array domains. The 589 terrain
frames and 76 raw occurrences now have geometry/texture or file-offset domains;
native fixed-count directory evidence resolves the audit anomaly. All 140,025
owned frames now parse completely; 401 inner-codec matches are geometry fields.
Seasons graphics coverage now passes; opposite-order Secrets STB, physics and
compiled-mesh coverage also passes. Four original Secrets animations have
inconsistent chunk lengths; bounded diagnostic recovery locates their remaining
candidates but does not admit the malformed images. Secrets LEV/BWD and numeric
text candidates have verified domains. Native surface-length evidence resolves
both DXT5 sky textures, including their four-byte/one-byte final mips; core
storage validation now supports them. DXT5 preview remains unsupported.
The core foreground-directory
reader now supports native fixed counts, passed through height baking and bounds
edits; its metadata-free diagnostic stops before frames. All 446 original maps
pass the directory audit, with bounds-only byte preservation on the anomaly
fixture. All Release targets build and all 44 core tests pass.
All three compiled BIN banks are already covered by the identity/relocation
audits. Game contributor preparation now relocates versions before comparison
while retaining source ownership and real conflicts; independent audits check
all 30,564 contributors in both orders. External admission and composer integration
remain unfinished. Animation decoding now uses native child boundaries and a
separate strict storage validator rejects opaque/malformed layouts; complete
corpus checks retain exactly four original Secrets failures. Reusable external
BIG-entry review covers textures, text and animations; full-entry audits verify
39,312/39,456 entries while retaining unsupported layouts. Full texture review
also exposed V8U8, now validated in both packs, and three Secrets cutlass mip-size
disagreements. Independent decoding and native fetch-offset logic confirm those
headers must remain refused. Verified entry counts are now 39,313/39,457.
Strict physics review, including a verified subset of text cloth commands, now
accepts all 1,383 entries per pack, raising verified BIG entries to 40,696/40,840.
Compiled-mesh review now validates all 3,400/3,428 original entries, including
complete LOD/ghost bodies and supported cloth. BIG verification reaches
44,096/44,268; the only Secrets refusals are four malformed animations and three
texture headers. STB/world admission and composer integration remain pending.
Reusable STB terrain review now validates all 21,904 foreground and 95,895
background frames, including exact nested range storage and native shared-index
selection. Local-detail review adds all 22,226 bodies, bringing reusable decoded
frame validation to all 140,025 owned STB frames. The 26 separate sea payloads
also validate. All 446 common records and 3,597 numeric palette rows validate,
including frame-to-palette bounds checks. The complete common manifest now
validates its 446 unique map/chunk identities and exact contiguous record coverage.
Reusable bank metadata validation covers all 473 entries; scratch audit verifies
all map name/ID bindings, sea entries and zero alignment gaps. Complete chunk
ownership and remaining world admission are still pending.
Foliage palette read/write now uses native main/shadow/z-sprite order and retains
the complete runtime settings block; all 3,597 original rows rewrite exactly.
Appending maps now writes the actual maximum STB entry ID, preserving native
entry-table capacity for sparse banks; one/multiple append regressions pass.
All 46 tests pass.
Composer refusal remains. Work is active;
see HANDOFF_WORLD_UI.md for commands, evidence and next recovery targets.

2026-10-05 sleep checkpoint adds a tested definition-payload relocation core:
verified component and typed references change; opaque matching integers refuse.
All 41 core tests pass. Original Secrets' colliding sword reference passes an
independent exact-byte audit. Namespace allocation, external references and
composer integration remain unfinished; Seasons+Secrets still refuse together.
User requested a pause. Resume steps and evidence are in HANDOFF_WORLD_UI.md.

2026-10-05 namespace-preserving whole-bank composition admits original Seasons
and Albion Secrets with Controller first/last, including script/frontend banks,
text IDs/narrator metadata and world composition. UFP's 32-source lifecycle and
bank-scoped scalar choices in the GUI pass. Divergent overhaul namespaces still
need reference relocation; granular overrides remain central. See handoff and
[Controller stack coverage](CONTROLLER_STACK_COVERAGE.md) for offline evidence.

2026-10-05 broader Controller Support checks pass twelve mixed modular/overhaul
baseline combinations and a 31-source order/choice/disable/remove/re-add lifecycle.
That lifecycle exposed and fixed saved scalar choices being ignored after sources
disappear or agree. All forty core tests and both 45-case contributor modes pass.
Frozen 07caf52 now passes all 89 package workflows but predates this fix.
See [Controller stack coverage](CONTROLLER_STACK_COVERAGE.md) for exact combinations,
unsupported identity cases and offline-only scope; no gameplay claim.

2026-10-05 local 07caf52 development ZIP verifies the latest creature, script-feat
and component changes using the actual extracted tool. Four scoped package
workflows, 108 identities/122 links and all three focused corpus audits pass;
85 package workflows unrun, zero skips. No publication/runtime claim. See handoff.

2026-10-05 exact ChocolateBox component/village/creature aliases admit original
Knothole Glade Children (Anniversary) in TLC scratch import. Four original/derived
cases match prior-tool controls across all six banks; twelve safe refusals,
the classic Knothole regression and all forty core tests pass. No runtime claim.

2026-10-05 original Script Balance imports with 25 feat string references across
twelve native-layout rows. Six original/derived cases and twelve safe refusals
pass against prior-tool controls; all forty core tests pass. Contradictory
Pray/Puke expression enum metadata remains refused on native evidence. See handoff.

2026-10-05 creature optional references pass 18 source selections and four
safe unavailable-source refusals, covering new/existing records and vanilla
through merge/build with moved identities. Full original/control regression
also passes; no additional C++ change. See handoff for labelled fixture scope.

2026-10-05 native sheathed-weapon/leader recovery admits original Raise Dead
New, Spearanimsbeta3, Thunder Transform Mod and Winter Sky. Both routes match
prior-tool controls; four reordered-identity cases and eight safe refusals pass.
Sheathed zero remains a sentinel; leader identities follow moved records.
All 40 core tests pass. Frozen d25dde7 excludes this increment; see handoff.

2026-10-05 local d25dde7 material/environment ZIP passes four extracted workflows,
108 identities/122 links, plus actual packaged-tool material, environment and
Time-choice checks. Explicit 85 not run, zero skips. PACKAGE_INFO retains the
tracked audio-test diagnostics flag; no clean/full-suite claim. See handoff.

2026-10-05 environment Time choices retain the selected source's texture
metadata across eight-row/two-row labelled derivatives. Ten merge/build
selections and two safe unavailable refusals pass; all later fields and source
inputs remain unchanged. No additional C++ change; scoped packaging is next.

2026-10-05 original Enviroment Snowing relinks 214 sky/cloud texture references
across nine records, preserving every other byte. Both import routes pass
original and reassigned-ID scratch banks plus sixteen safe negative routes.
All Release targets build; scoped core 3/3, material and 18 record-refusal
regressions pass. Next are Time-field source choices and scoped package checks.
The earlier full forty-test run excludes this increment; see handoff.

2026-10-05 material particle links now preserve map keys/floats and use effect
identities for blended pairs and particle vectors. A labelled Gravity Hammer
material derivative passes nine links, both routes and fourteen safe refusals;
the full original still lacks dependencies. All 40 core tests pass and expanded
vector/domain tests pass separately. This checkpoint precedes the environment
binding above. See handoff for source evidence and acceptance scope.

2026-10-05 frozen 0b6be9c duplicate/creature ZIP has audited passes for all 89
workflows across 42 initial and 47 resumed successes. Both failed GUI attempts
are retained; the resumed script exercises the compact dropdown on its actual
1024x768 child desktop. Zero uncovered/fixture skips, 108 identities/122 links.
Newer 864e853 appearance ZIP passes four scoped workflows plus extracted-tool
original/derivative byte audits; 85 not run. See handoff for immutable hashes.

2026-10-05 original appearance choices retain metadata matching each prefix.
Thirteen named new-record/existing-record selections and two unavailable-choice
refusals pass for both original Dark Apprentices variants. Existing-record
coverage uses an explicitly derived distinct baseline prefix; the unchanged
source package is correctly unavailable against an identical baseline.

2026-10-05 original creature appearance links now retain model/texture/name
domains and complete group-count bounds. Dark Apprentices variants 1/2 and
Hero Pack pass 204 appearance references, all 282 creature links, exact bytes,
effective-bank parity and 34 safe negative routes. A labelled NoGenEXP77
derivative proves skeletal names offsets; its original remains refused by a
duplicate-owner guard. All 40 core tests pass; see handoff.

2026-10-05 FMP named records cannot occupy the reserved `@unnamed:` identity
namespace. A reproduced anonymous/named lookup collision now refuses both
routes before writes. Original duplicate-record parity and 24 safe negative
routes pass, alongside 18 focused record cases. Frozen 0b6be9c excludes this
guard; its package acceptance is still in progress.

2026-10-05 exact repeated FMP source/metadata identities now compose safely.
Original Assasain retexture/flame flourish matches an independent deduplicated
control (20 links) through both routes; 22 safe refusals and existing 16 record/
27 bank cases pass. Conflicting named identities and the independent augmentation
texture collision remain refused. Core 40 passes; frozen 997e065 excludes this
increment. See handoff for identity rules and preservation evidence.

2026-10-05 frozen f20e263 texture/float ZIP passes all 89 workflows across
42 initial and 47 resumed successes; the initial GUI layout failure is retained.
Zero uncovered/reported skips, 108 identities/122 links. Newer 997e065 creature
ZIP passes four scoped workflows plus actual extracted-tool prefix/choice gates;
85 not run. These scopes exclude later duplicate-record work; see handoff.

2026-10-05 counted creature prefixes now relink wound textures and facial
animation graphics entries. Original No Scar Fade and Bruised and Bloodied
(No Scars) pass 210 prefix links, all 258 creature-record links, exact other
bytes, bank parity and forty safe refusals; core 40 passes. Weapon4ID stays
refused. Frozen f20e263 excludes this increment; see handoff for layout scope.

2026-10-05 verified texture replacement pairs now bind old/new texture IDs.
Original Plate Helmet and Prophet 01 Upper Robe pass four links, exact other
bytes, direct/composed bank parity and twenty safe refusals; all 40 core tests
pass. Creature-prefix and Weapon4ID leads remain refused pending evidence.
Frozen b4cebe2 excludes this increment; see handoff for scope.

2026-10-05 finite definition floats now use shortest round-trip display text,
distinguishing adjacent values and signed zero. Exact merge/build choices and
both GUI sizes pass; all 40 core tests pass. Package harness adds this regression
(89 workflows). Frozen 9bc59e8 separately passes all its 88 workflows across
three retained attempts; later FMP and float increments are excluded. See handoff.

2026-10-05 verified opinion reaction vectors now relink animation names and
dialogue text while preserving attitudes and all other bytes. Original Getting
Away With Murder passes 306 links, bank parity and 14 safe refusals; all 40 core
tests pass. Frozen 9bc59e8/46d5c91 exclude this increment. See handoff for scope.

2026-10-05 original Augments.fmp now binds verified augmentation name/icon
scalars to text/texture IDs. Four links, exact surrounding bytes, direct/composed
bank parity and six safe refusals pass; existing 67 augmentation-effect links
and all 40 core tests pass. Frozen 9bc59e8 excludes this increment. See handoff.

2026-10-05 Windows Unicode loose-file identities now preserve contributor order,
reviewed choices and output ownership. Native TNG/QST I/O retains Unicode names;
57 ordinary/GUI and 55 complete-data checks pass, including exact stage restore.
The package harness now includes 88 workflows; older frozen ZIPs exclude this
increment. See HANDOFF_WORLD_UI for evidence and remaining scope.

2026-10-05 distinct-label a87b34a ZIP passes seven selected extracted workflows
under the child-skip guard: labels 43, fields, level casing and identical TNG
copies. Explicit 79 not run, zero reported skips; 108 identities/122 links pass.
Coverage guard distinguishes missing fixtures from normal recipe arbitration.
Independent full 85-workflow 087467d acceptance remains; see HANDOFF_WORLD_UI.

2026-10-05 frozen 087467d local ZIP passes all 85 extracted workflows in one run,
zero omitted/reported skips; retained child logs contain no SKIP markers. All
108 identities and 120 links pass. It includes DeleteUIDs and output ownership/
fresh-only staging, excludes later label changes, and remains independent of
60bafcf's seven-workflow scope. See HANDOFF_WORLD_UI for SHA and audit evidence.

2026-10-05 enabled labels now remain distinct under order case comparison,
including inferred bare-merge names. Ambiguous TNG selections are reproduced
and refused before composition/reversion; uniquely named registered sources
retain source/baseline selection. Label checks 43, core 40 and mixed corpus/UI
pass. Both frozen 087467d/60bafcf ZIPs exclude this later change; see handoff.

2026-10-05 clean 60bafcf local ZIP passes seven selected extracted workflows,
covering label refusal, reviewed file choices, ownership and DeleteUIDs. Explicit
79 omissions, zero fixture skips; 108 identities and 122 links pass. This is
scoped acceptance, independent of the 087467d archive's full 85-workflow run.
See HANDOFF_WORLD_UI for archive hashes and evidence.

2026-10-05 mod labels now distinguish sources from the reserved vanilla choice
and survive the choice-file format. Invalid names refuse registration and enabled
legacy entries refuse composition before deploy reverts its old stage. Both GUI
sizes and all 40 core tests pass; disabled legacy entries remain removable. The
frozen 087467d package excludes this later increment. See HANDOFF_WORLD_UI.

2026-10-05 reusable mod outputs now record generated file identities and remove
unchanged obsolete files transactionally. Edited obsolete files refuse cleanup;
unlisted files remain. Public --stage uses fresh composition, excludes output
bookkeeping and unrelated files, and refuses existing deployments before writes.
Both 58-check layouts pass; package harness now includes 85 workflows. Frozen
b7ec186 ZIP excludes this increment. See HANDOFF_WORLD_UI for evidence and scope.

2026-10-05 frozen b7ec186 local ZIP covers all 81 workflows across 44 retained
initial passes and 37 resumed passes after one GUI timeout. Zero uncovered/skips;
108 packaged identities and 120 doc links verified. Later DeleteUIDs and build
ownership changes are excluded; see HANDOFF_WORLD_UI for hashes and evidence.

2026-10-05 DeleteUIDs now agrees with effective winners, counts and provenance.
Blocked choices refuse builds and remain reviewable/clearable; default TNG bytes
are unchanged. Both 26/24-check layouts, existing TNG/casing checks and mixed
50-command/Mods UI checks pass. Frozen b7ec186 ZIP excludes this increment;
see HANDOFF_WORLD_UI for deletion priority and package acceptance boundaries.

2026-10-05 duplicate named source text entries now refuse ambiguous composition,
including repeats across sub-banks. Both 60-check layouts, both GUI sizes and
real TextID checks pass. Unchanged-bank and case-sensitive-name behavior remain;
fresh local package acceptance is next. See HANDOFF_WORLD_UI.

2026-10-05 selected text payloads now refuse if the codec cannot preserve their
raw bytes. Both 48-check ordinary/complete-Data suites, both GUI sizes and real
13-case TextID checks pass; untouched opaque baseline payloads are retained.
Duplicate source symbols remain separate. Frozen ZIP unchanged; see HANDOFF_WORLD_UI.

2026-10-05 identical TNG copies retain all actual contributor names without
changing copy/removal strategy. Earlier saved choices and provenance now agree;
26 ordinary/native/UI, 24 complete-Data, existing contributor/casing and mixed
50-command/Mods UI checks pass. Frozen ZIP unchanged; see HANDOFF_WORLD_UI.

2026-10-05 level casing: TNG/QST filenames now coalesce across casing and uppercase
extensions are composed. Saved path aliases share CLI/GUI identity while entry
names keep their case. Ordinary/native/UI 126 and complete-Data 122 checks,
compact WAD/stage restoration, all 40 core tests and mixed 50-command/Mods UI
checks pass. The frozen ZIP excludes this increment; see HANDOFF_WORLD_UI.

2026-10-05 local package at 2147246 passes all 71 extracted workflows, zero
omitted/skipped, plus 108 file identities and 120 doc links. It includes text
refusals and whole-file winner previews. New probes show case-only TNG/QST names
overwrite disjoint edits and uppercase extensions silently drop changes; these
are next, followed by trailing text bytes and duplicate named entries.
See HANDOFF_WORLD_UI for frozen artifact hashes and acceptance boundaries.

2026-10-05 text TOC metadata: speaker/NarratorList header deltas now count as
contributions and selected unsupported metadata refuses instead of disappearing.
Both 33-check ordinary/complete-Data gates, both-size GUI recovery, TextID 13 and
mixed 50-command/Mods UI checks pass. Fresh local package acceptance is next;
see HANDOFF_WORLD_UI for donor preservation and unsupported remapping boundaries.

2026-10-05 unnamed text changes: new/changed/duplicate unnamed records now refuse
ordinary composition while unchanged payloads/metadata survive. Ordinary 45-check,
complete-Data 46-check, both-size GUI recovery and 13-case TextID gates pass.
Named TOC-only deltas remain separate; see HANDOFF_WORLD_UI. Frozen ZIP unchanged.

2026-10-05 whole-file winner previews: clearing saved TNG/QST choices now shows
the last whole-file owner, including its removals. Four family/window cases and
existing 38 TNG / 40 QST checks pass; the frozen ZIP excludes this GUI fix.
See HANDOFF_WORLD_UI for cached-report and publication evidence.

2026-10-05 text composition: malformed selected strings, unsupported changed
types and missing language baselines now refuse instead of silently dropping
changes. Ordinary 36-check, complete-Data 37-check, both-size GUI recovery and
13-case TextID gates pass. Valid choices and unchanged groups remain supported.
The frozen ZIP excludes this fix; see HANDOFF_WORLD_UI for remaining boundaries.

2026-10-05 consolidated development ZIP at 928e38a passes all 61 extracted
workflows, zero omitted/skipped checks, plus 108 identities and 120 doc links.
It includes the subsequent contributor/staged-copy fixes. Three independent
text-entry probes still demonstrate silently dropped malformed strings, changed
groups and missing-baseline banks; these are next. See HANDOFF_WORLD_UI.

2026-10-05 QST contributors: unavailable active registration winners now refuse
publication, including single-editor/new-registry routes; single-contributor
picks are honored. Original flags/absence and unrelated whole-file removals
remain intact. Ordinary 40-check native/UI, complete-Data 38-check, standalone/
staged checks and all 40 core tests pass. See HANDOFF_WORLD_UI for boundaries.

2026-10-05 TNG contributors: active saved thing winners now require actual
contributions, including partial/replacement/single-editor/new-level routes.
Vanilla restores original/absent things while unrelated deletions stay intact.
Ordinary 38-check native/UI and complete-Data 36-check gates, standalone checks
and the mixed 50-command corpus/Mods UI pass. QST remains next; see HANDOFF_WORLD_UI.

2026-10-05 staged baseline copying: ordinary folder/FMP snapshots now retain
composer dependencies and authored paths, reducing the local copy from roughly
7.7 GB to 1.57 GB. Original restoration/absence, unrelated-file retention rules,
recipe fallback and byte-identical real-donor output pass. Timings vary; no
speedup claim. Broader source types keep full snapshots. See HANDOFF_WORLD_UI.

2026-10-05 language/string contributors: unavailable active per-language winners
now refuse publication; conflict checks and both-size dropdown recovery remain
available. Ordinary 26-check and complete-Data 24-check gates plus the 13-case
TextID regression on recorded originals pass. Existing-stage reconstruction
copies roughly 8 GB per merge on the local install; reducing that cost is next.
See HANDOFF_WORLD_UI for evidence and fixture boundaries. ZIPs remain frozen.

2026-10-05 FSE contributors: unavailable active quest-entry winners now refuse
publication while conflict checks remain reviewable. Ordinary 26-check native/UI
and complete-Data 24-check gates pass, including inactive/absent entries and
both-size real dropdown clearing. The mixed 50-command corpus/Mods UI gate also
passes. This increment is not in the earlier frozen ZIPs; see HANDOFF_WORLD_UI.

2026-10-05 local package checkpoints: frozen d1ad2a4 passes its original 48
extracted workflows across retained runs, zero skips. The later 1930ebb archive
passes ten affected workflows including scalar/record refusals; its other 42
workflows are not claimed. Both verify 108 identities and 120 documentation
links. Two retained D: scratch timeouts have same-binary passing command retries
and a passing C: gate; their cause remains unconfirmed. See HANDOFF_WORLD_UI.

2026-10-05 record contributor choices: absent/unchanged saved record winners now
refuse publication, including one remaining contributor; inactive records remain
supported. Both 48-case ordinary/complete-Data gates, both-size record-choice UI
checks and named FMP reference/relocation regressions pass. The earlier ZIP is
frozen and excludes this fix; see HANDOFF_WORLD_UI.

2026-10-05 scalar contributor choices: unavailable contributors for current
contested scalar fields now refuse builds while conflict checks remain reviewable.
Both 33-case ordinary/complete-Data gates and both-size field-choice UI checks
pass. Core code is unchanged from the 39-test gate. Whole-record fallback remains
a separate follow-up; see HANDOFF_WORLD_UI.

2026-10-05 file review key casing: review paths now use the same case-insensitive
identity as CLI file choices; mixed-case annotations cannot bypass changed-byte
refusal, and case-alias duplicates refuse. Both 28-case ordinary/complete-Data
gates and all 39 core tests pass; all Release targets build. The earlier d1ad2a4 development ZIP
is a separate frozen checkpoint; see HANDOFF_WORLD_UI.

2026-10-05 whole-file review fingerprints: new GUI choices preserve the raw
baseline/contributor snapshots from the last check. Changed contents refuse
builds and remain reviewable; legacy choices, stable reordering and unrelated
files remain supported. Ordinary staged review/build now use scratch originals.
Both 26-case ordinary/complete-Data gates, both-size comparison/reselection,
existing choice/field/asset regressions and all 39 core tests pass. Scope and
retained failed attempts are in HANDOFF_WORLD_UI; no package/gameplay claim.

2026-10-05 contributor review follow-up: conflict checks report unavailable
whole-file/texture/mesh choices without permitting publication. Builds remain
strict. Both-size real dropdown recovery, clipping-aware warning captures,
ordinary/complete-Data contributor checks and existing saved-choice recovery
gates pass. See HANDOFF_WORLD_UI for retained evidence and fixture boundaries.

2026-10-05 saved texture/mesh choices now refuse unavailable per-entry
contributors instead of falling back to load order. Twenty focused direct/build
checks, both original asset-layer suites, the 24-case whole-file gate and all
38 core tests pass. Inactive choices and valid source/vanilla choices remain
supported. No full-corpus, packaging or gameplay claim; see HANDOFF_WORLD_UI.

2026-10-04 expanded full scan completed at frozen bcfa118: 800/1102 originals
imported, 301 refused and one timed out. The same-build timeout retry passed,
giving 801 individually successful offline imports. No acceptance regression
remains in the old 821-package cohort after that retry. Later material-slot
validation and four original suit-map recoveries have separate targeted gates;
the full scan does not cover them. See HANDOFF_WORLD_UI for hashes and evidence.

2026-10-04 named-corpus follow-ups: exact expression, shop and region reference
domains raise separately demonstrated offline imports to 653 in the original
821-package scope. Original lowercase text banks add two checked packages and
expand bank eligibility to 1102; a fresh full sweep is pending. Twelve text-bank
refusal cases and thirteen existing text relocation/order/pick/language checks
pass. The provider index covers 1133 readable originals and offers candidate
dependencies without treating symbol matches as verified compatibility.

2026-10-04 named GLOBAL continuation: Aging Slowdown, Fire Teleport and Lightning
Teleport pass 42 independently checked model/texture/effect words, byte
preservation, direct/composed parity and safe missing-model refusal; the complete
small-bank scan reaches 439/577 without regressions. Counted mesh texture lists
now account for multi-LOD switch distances, with one/three-LOD asset fixtures and
safe malformed-list refusal. Inventory icon/title/detail references use their
texture/text domains. Interrupted large-bank candidates are retained; the fixed
full sweep is complete in the owned C: verification checkout: 635/821 accepted,
185 refused and one timeout. The timeout passed a separate same-build retry,
giving 636 successful offline imports; gameplay is a separate check. Non-staged
mod-build publication now rolls back reported replacement failures. See
HANDOFF_WORLD_UI for exact evidence and scope boundaries.

2026-10-04 release checkpoint: the Forge-managed Aeon/controller route is
published as prerelease `v0.18.1-rc.1` at tagged source `f5c763a` with green
Windows CI, a versioned application ZIP, updated illustrated guide and four
direct screenshot assets. The extracted candidate passed 46/46 workflows with
zero skips before a final screenshot-only repack; archive identities and links
were checked after repacking. The Discord message is drafted but unsent. Live
gameplay/Retroid remained untested at the release checkpoint and direct `mods merge` late-read output
atomicity remains open. See HANDOFF_WORLD_UI.

2026-10-04 external Windows check after release: Perodis installed the
three-source Aeon 5.03 + Controller Support FMP/DLL order through the released
Forge build and launched through EgoCore on a second Windows PC. Aeon signs
appeared in-game, an Xbox controller worked, and target lock did not crash.
The wired Nintendo Switch Pro Controller did not respond in Fable; Steam's
controller test worked, while Windows' raw `joy.cpl` properties showed many
buttons lit. This is evidence for the PC install/runtime path with one XInput
controller, not for the Switch Pro input path or Retroid. GameNative's source
has per-game Steam Input/XInput/DirectInput switches and forwards Android
controller state to the Wine process; the Fable/EgoCore route there is still
untested. The transfer guide now names the deployed game folder and
`FSE_Launcher.exe` explicitly.

2026-10-04 complete-Data installer integration: the isolated primary-based
merge preserves all product/test bytes from the 46-workflow-tested review branch
and passes an all-target Release build, 38 core cases and the focused
installer/refusal gates. The local development ZIP and illustrated guide are
validated but unpublished. At that checkpoint, direct `mods merge` late-read
output atomicity and live PC/Retroid gameplay remained open. See HANDOFF_WORLD_UI.

2026-10-03 primary scalar/Aeon checkpoint: review 9bc1f1d is integrated through
801e733; all Release targets build, 38 core tests and rebuilt-primary named-link /
existing-record preservation checks pass. Its frozen archive passes all 42 extracted workflows
across the original and resumed runs, zero skips; the initial texture CLI timeout
is retained. Intact Aeon baseline plus Controller FMP/DLL preserves all existing
non-imported records, including 6625 unnamed components. The corrected guide
and unnamed-source refusal are included. Separate sibling/FMP-bank guard package
validation remains in progress. No gameplay or complete-overhaul relocation claim;
see HANDOFF_WORLD_UI for evidence and the unchanged full-source-gate boundary.

2026-10-04 Forge-managed complete Aeon/controller route in the owning review
branch: real three-source normal GUI Add/Check/Deploy/redeploy/Undeploy and compact
check/deploy/restore pass; original five bank/WAD files restored, staged banks,
DLL and Mods.ini match the independent CLI build. The illustrated guide has
four new captures from the passing run. All Release targets, 38 core tests,
14 install, 29 bank and 16 FMP cases pass. An extracted local development ZIP
passes all 46 workflows with zero skips, 103 identities and 117 links. The
old rc.3 package is unchanged; game/Retroid behavior is untested. At that
checkpoint, `mods merge` late-read failure could leave prepared files in its requested output;
managed Deploy prepares in scratch. Exact evidence, hashes and continuation are
in HANDOFF_WORLD_UI.

2026-10-03 isolated unsupported-bank follow-up: changed script/frontend sources,
unsupported nonempty FMP banks and incomplete unnamed/sibling FMP exports refuse
before writes. Unchanged sibling copies remain supported. All 38 core checks,
29 native preservation cases and both-size persistent GUI refusal/retry pass.
This checkout is not yet packaged or promoted. The frozen 9bc1f1d package's initial
run passed 36 workflows then timed out seeding the texture-destination fixture;
six remaining checks are being resumed while retaining that failure. Lost Content's
content-only diagnostic remains incomplete after a composition timeout. Exact
scope and evidence are recorded in HANDOFF_WORLD_UI.

2026-10-03 isolated preservation correction: changed/new unnamed definition
sources now refuse instead of silently losing component edits. Controller FMP
composition over intact Aeon preserves 16362 existing records (6625 unnamed),
14 named links and baseline levels/assets. Both-size GUI refusal/retry and the
corrected two-layer guide deployment pass; all 38 core cases pass. Full overhaul
identity/relocation remains unsupported. Historical Aeon/UFP/other-overhaul
container counts did not establish complete preservation. Package acceptance
and promotion are pending; exact boundaries are in HANDOFF_WORLD_UI.

2026-10-04 named FMP exception: a blank-name `GameBINEntries` record can now
compose by numeric index when its ID, blank name, and definition type match the
selected baseline. Its fields participate in load order, conflict choices,
and saved picks. Loose overhaul directories remain guarded. The full named
archive scratch scan accepts 222 of 577 attempted small-bank FMPs, up from
197 before this exception; 244 large texture/graphics packages remain deferred
from apply testing. Original Bandit Beard and two conflicting lamp FMPs pass
byte, precedence, and GUI deploy/restore checks. See `docs/modding/FMP_FORMAT.md`.
The verified chest particle-effect link now resolves through `effects.big` in
both direct FMP apply and final composition. Two original Silver Keys packages
raise the small-bank accepted count to 224 of 577 attempted; missing effect
targets still refuse before writing.
The guarded nested `CREATURE.DeadBodyDecayEffect` link resolves through
`effects.big` as well. Original archived `Alwaysthere001.fmp` passes direct
apply and composition with matching definition banks; the full eligible
small-bank scan rises to 320/577, an additional 96 accepted packages. The
remaining 238 link refusals and 244 deferred large-bank packages are ongoing
corpus work, not gameplay compatibility claims.
The guarded `CREATURE.Weapon3ID` link now resolves through the existing
`RangedWeaponDef` layout. The archived 510-record `No body Rots.fmp` passes
direct/composed byte comparison and crossbow target validation. The eligible
small-bank scan rises to 338/577; 220 remaining link refusals need distinct
format work.
The bounded `HERO_ABILITY` and `HERO_STATS` text-reference paths now resolve
existing `text.big` names, with hero HUD icons resolved through `textures.big`.
The archived `EXP And Hero Mod.fmp` applies directly and through the one-mod
composer with byte-identical definition banks and 217 resolved named links.
The current full small-bank scan accepts 436/577, five more than the prior
431/577 scan after potion/SHOT particle controls resolve their effect IDs;
122 link refusals remain. New FMP Text IDs now resolve after
text-only sources, load order and picks are composed; cross-language ID
validation and a 13-case synthetic archived-hero regression pass offline.
Runtime behavior still requires separate validation. The proposed successor
package shape, including complete-data baseline profiles and EgoCore DLL
components, is in `docs/modding/PACKAGE_V2_PROPOSAL.md`.
The direct non-staging `mods merge` path now composes in scratch before copying
to its requested output, so late source-read failures leave existing output
alone. The final copy is not a transactional directory swap and can still
partially fail on destination I/O errors.

2026-10-03 choice feedback checkpoint: b428ec1 is integrated and rebuilt. Failed
choice reads/writes retain compact-safe feedback, preserve winners and recover
on same-process retry. Both-size primary locked/unreadable checks pass, including
destination reset. The 34b29bf archive passes THREE focused choice/field/file
workflows; the preceding 7b1c043 archive passes all 39. See HANDOFF_WORLD_UI for
the hashes, preservation evidence and unchanged full source-gate boundary.

2026-10-03 primary file comparison checkpoint: 55e20dc is integrated and all
Release targets build. Rebuilt-primary normal/compact file snapshots and the
retail/Aeon/merged named-link audit pass. The 7b1c043 package passes ALL 39
extracted runtime workflows without skips and verifies 103 shipped identities.
The last full source gate remains b1eb3b9 136 PASS / 1 identified/fixed dialogue
FAIL. Raw-file snapshots do not detect stale choices automatically. Exact
evidence and local archive identity are in HANDOFF_WORLD_UI.

2026-10-03 primary linker/Add/dialogue checkpoint: reviewed changes are integrated
at 0a7887e and all Release targets build. The frozen b1eb3b9 source gate finishes
136 PASS / 1 dialogue playback FAIL with no skipped integration checks. The
exact slider-end rounding defect is reproduced and fixed; focused rebuilt-primary
dialogue, Add recovery and expanded named-link audits pass. The 7f771b7 package
passes SIX focused extracted runtime workflows; the packaging-only follow-up
verifies 103 shipped identities. Neither is a complete 38-workflow package-suite
claim. Local archives/hashes and precise boundaries are in HANDOFF_WORLD_UI.

2026-10-03 primary texture checkpoint: 39bdf32 is promoted and rebuilt. Its
stripped development ZIP passes four focused texture workflows sequentially,
including new locked/invalid replacement recovery and existing Unicode inputs /
locked exports. The full 33-workflow package suite was not rerun; the preceding
e4a2807 package passes all 31. See HANDOFF_WORLD_UI for the exact boundaries.

2026-10-03 primary Mods checkpoint: e4a2807 is promoted and rebuilt; its stripped
development ZIP passes all 31 runtime workflows, 115 Markdown links and four
guide images. Mods failure feedback and same-process recovery pass at both sizes.
The isolated texture replacement follow-up below is excluded from this archive;
see HANDOFF_WORLD_UI for the retained hash and exact validation boundaries.

2026-10-03 isolated texture follow-up: invalid and sharing-locked replacements
retain wrapped errors beside Replace. Actual form input, complete failure
preservation, same-process retry, exact exported blue pixels and Restore pass
at both sizes. Promotion and extracted-package checks remain pending;
see HANDOFF_WORLD_UI for source and evidence boundaries.

2026-10-03 isolated Mods recovery follow-up: failed redeploy, invalid reports and
missing tools retain actionable errors beside compact-safe buttons. Same-process
retry, destination changes, same-install rescans and exact inventory recovery
pass at both sizes; existing field-details checks pass. Promoted e4a2807 passes
all 31 extracted workflows. See HANDOFF_WORLD_UI.

2026-10-03 primary checkpoint: the e7cb37b gate completes with 126 passing
checks, zero skipped integration checks and one prose-triggered documentation
failure. That wording is corrected and the checker passes after promotion to
6387b06. Primary Release targets build; Unicode process, image and compiler
changes have focused, real-compile and 37-core-test coverage. The d5347ea
extracted package passes 27 workflows; the 6387b06 compiler package passes all
28 and its bundled compiler passes real Unicode Controller Support compiles.
See HANDOFF_WORLD_UI for exact source and test boundaries.

2026-10-03: all 27 extracted runtime checks pass for the d5347ea Unicode
process/image package. A further isolated EgoCore review preserves native
compiler/environment/temp paths, verifies startup refusal and retry, and produces
identical real Controller Support output hashes across ASCII/Unicode concurrent
compiles. It awaits promotion/package validation; the primary gate remains
frozen at e7cb37b. See HANDOFF_WORLD_UI for exact evidence.

2026-10-03 isolated image follow-up: MinGW texture imports open Unicode PNG
paths natively before using the existing decoder. CLI add/export and GUI
replace/export verify exact red/blue pixels and complete Restore at both sizes;
37 core tests pass. Promotion/package validation remains pending, outside the
frozen e7cb37b full gate. See HANDOFF_WORLD_UI for evidence and boundaries.

2026-10-03 isolated follow-up: Windows CLI arguments, GUI helper launch and
default schema lookup preserve Unicode paths. ASCII/Unicode controls and actual
normal/compact file-pack deploy/undeploy recover exact scratch inventories.
This review change awaits promotion/package validation; the primary full gate
remains frozen at e7cb37b. See HANDOFF_WORLD_UI for evidence and boundaries.

2026-10-03: loaded asset views refresh across Mods writes at either destination,
including failed redeploy. Texture browsing follows the effective save-folder
bank; same-name preview/export pixels and persistent locked-output errors are
verified. All 25 runtime workflows pass in the extracted a3b030b development ZIP.
The last completed full gate is still afe9e55; see HANDOFF_WORLD_UI for boundaries.

2026-10-03: Setup restores tracked backups even when game.bin or all level
containers are missing. Partial failures remain visible through map reload,
ambiguous originals refuse before writes, orphaned stage backups stay preserved
with an explanation, and closing Setup or changing installs cancels Restore.
All 19 runtime workflows pass in the extracted 7336194 development package;
the whole-gate boundary is still afe9e55. See HANDOFF_WORLD_UI for exact evidence.

2026-10-03: the frozen afe9e55 checkpoint passes all 110 integration checks,
zero failures or skipped integration checks, including the corrected smoke,
37 core tests, current mod comparisons and every available corpus. Saved choices
outside the latest report are now reviewable/searchable and clear individually;
even an empty order returns a dry-run report. CDefString displays signed
name-table references. Focused review and a nine-workflow extracted development
package pass; exact source/validation boundaries are in HANDOFF_WORLD_UI.

2026-10-03: Mods now compares residual field baseline/contributor/winner values
and searches conflicts before the display cap. The frozen 330d4b5 gate passed
105 checks with one foliage-occluded smoke fixture failure; corrected smoke and
focused integrated workflows pass, alongside 37/37 CTest targets. Exact source
and validation boundaries are in HANDOFF_WORLD_UI. Per-field choices/profiles
remain in the linked content-override backlog.

Ground now offers Smear: frozen-neighbour blending into three distinct themes,
one application per click and optional Repeat. Theme-only saves preserve
passability bytes. Core and stock/loose normal/compact workflows pass; native
compatibility boundaries and evidence are in HANDOFF_WORLD_UI.

The pin/camera checkpoint at frozen source b132d50 exercised all 105 integration
checks: 104 passed and one off-screen whole-map button fixture failed. Scrolling
to that button fixes the workflow; its focused rerun passes stock/loose and
normal/compact. No checks were skipped. See HANDOFF_WORLD_UI for precise scope.


Map revisits now restore each map's camera, FOV and rendering mode, including
after restart. New maps use normal framing; views are scoped to the current
install. Stock/loose normal/compact round-trip and malformed-data checks pass.
See HANDOFF_WORLD_UI.


View > Map camera now adjusts vertical FOV in degrees, with typed input and reset.
Stock/loose normal/compact controls and restart restoration pass; session metadata
stores the view without editing the map. See HANDOFF_WORLD_UI.


Terrain brushes now pin their last valid ground point while Space is held, with
an explicit cursor marker and existing Shift inversion retained. Stock/loose,
normal/compact movement, painting, cancellation and undo checks pass. Native
last-point evidence and the deliberate binding difference are in HANDOFF_WORLD_UI.


The dated-snapshot/session checkpoint at frozen source `a987019` passes **104
integration checks, zero failures or skipped checks**. Core, UI, scratch installs,
imports/recipes, world/level packs and every available mod corpus were exercised.
Evidence and native brush-pin follow-up findings are in HANDOFF_WORLD_UI.


The editor now reopens the last map and camera in the same install, with its
View/Edit mode, editing tab and terrain tool. Invalid camera data or missing
maps fall back safely; stock/loose process tests pass at both sizes. Session
metadata does not save edits. See HANDOFF_WORLD_UI.


Dated current-map restore points now have Ctrl+B / File-menu capture and a Saved
snapshots library with undoable Restore and confirmed Delete. They survive
Save draft and restart; stock/loose and normal/compact checks pass. World-tab,
dialogue and asset state remain outside these captures. See HANDOFF_WORLD_UI.


Recovery snapshots can now be restored inside the open map from the File menu,
with a capture time and undo/redo back to later edits. Actual menu workflows pass
on stock/loose copies at normal/compact sizes; see HANDOFF_WORLD_UI.

Recovery publication now uses one same-volume replacement, retains the prior
archive on a locked-file failure, and preserves untouched camera bytes exactly.
Focused core and stock/loose UI evidence follows the 102-check checkpoint in
HANDOFF_WORLD_UI.

The Budget/recovery checkpoint at frozen source `22cbbd6` passes 102 integration
checks with zero failures or skips. Evidence and the scoped publication /
byte-preservation follow-ups are recorded in HANDOFF_WORLD_UI.

Timed crash recovery now snapshots unsaved objects and terrain into an
install-scoped app-data archive every five minutes, with configurable intervals
and manual capture. Recover/Discard works on reopen; recovery is undoable, and
failed saves keep the archive. Stock/loose and normal/compact checks are tracked
in HANDOFF_WORLD_UI. Whole-world snapshots remain separate work.

Budget > Areas now captures dragged boxes and clicked objects, with Shift
accumulation, visible picks, Clear and Escape. Normal/compact input and overlay
checks pass, alongside existing Budget/controls/selection suites. Native
half-open bounds and captured UID membership are recorded in HANDOFF_WORLD_UI;
the report retains its existing bank-cost estimates.

The full draft/survey checkpoint at source `3351548` passes 100 integration
checks with zero failures or skipped checks, plus focused stock/loose async and
terrain-only Save-on-close checks. Evidence and limits are in HANDOFF_WORLD_UI.

Save draft, Ctrl+S and F6 now save terrain and objects together; F7 saves and
reloads. Terrain authoring uses a game-ignored `.lev.forge-draft` file and keeps
the game's terrain/foliage bake baseline. Write terrain remains available after
saving. Failure and stock/loose validation are tracked in HANDOFF_WORLD_UI.

Walkable/Blocked and Camera pens now share the verified rounded survey disc,
with one-vertex brushes, small vertex cursors, regular-cell clipping and exact
undo. Navigation/save behavior stays on the existing path. Native evidence
and focused checks are in HANDOFF_WORLD_UI.

Terrain > Passability > Shore now paints independent shore-point flags, with
On/Off, Shift inversion, red overlays and stroke undo. Native serialization and
byte-preservation evidence are in HANDOFF_WORLD_UI. Visible water shoreline
generation remains separate.

Environ. now offers in-place Smear with the native counter footprint, tie rules
and truncated blend weights. Small cursors show reached cells and prompt for a
larger Radius when empty. It operates within the current map; native cross-map
neighbour reads remain outside this scope. Evidence and checks are in HANDOFF_WORLD_UI.

Ground now offers a full native stamp alongside its existing soft brush. It
shares Replace/Environment's strict truncated vertex disc, shows small cursor
dots and ignores Strength/time. Native evidence and checks are in HANDOFF_WORLD_UI.

Sound/environment region overlays now use decoded editor colours, sharp 4x4-cell
boundaries, all/selected/off sound controls and an environment toggle. They update
during paint and undo; these identify regions rather than previewing runtime audio
or lighting. Native colour arithmetic and validation are in HANDOFF_WORLD_UI.

2026-10-02 continuation: saved cell-centre placement now covers new/pasted things
and ground carry, preserving ground/fixed heights, group spacing and exact undo.
Normal/compact UI and all 36 core tests pass; native evidence and boundaries are
recorded in HANDOFF_WORLD_UI. Gizmo snap remains a separate control.
Whole-map Raise now includes camera coordinate bases and spline keys, with
native flag/serialization evidence and independent scratch TNG comparisons.
Ctrl-drag height sampling, fixed-point height keys and shared Speed controls now
cover all six sculpt tools. Exact-step/Flatten/Smooth and height-key cursors show
small vertex footprints; empty height-key footprints ask for a larger Radius.
Sound now uses its rounded vertex pen and outlines affected 4x4 cells at small
sizes; scratch output, normal/compact clicks, sampling and undo are checked.
Replace's strict disc now uses the verified truncated centre, with considered
vertex dots and normal/compact real-click/undo coverage.
Environment stamps its strict vertex pen's touched 4x4 cells fully and can
clear them, with scratch values, real clicks, sampling and exact undo checked.
The grid/camera broad gate passes 88 checks; later sculpt changes pass focused
normal/compact checks. See HANDOFF_WORLD_UI for exact build/evidence boundaries.

2026-10-01 workflow depth: new-level creation preserves edits made while the job
runs, compact origins remain readable, and Restore clears removed-map previews.
Scratch creation/restore, extracted layouts, world-pack composition and new-level
pack deploy/open/byte-exact undeploy pass. Terrain workers use isolated snapshots;
later strokes and map switches preserve consistent output and draft edits.
See HANDOFF_WORLD_UI for the evidence and remaining in-game checks.

2026-10-01 input/failure depth: OBJ/GLB container, accessor, scene and index checks pass 384
synthetic cases in normal and AddressSanitizer builds. Custom theme imports now
stage/roll back banks like model imports; invalid cliff images and late file-lock
failures preserve scratch files. Full map palettes are refused before creation.
Model and recipe-pack regressions pass; see HANDOFF_WORLD_UI for boundaries.

2026-10-01 import depth: model imports prepare their banks before replacement
and restore prior bank files after a reported commit failure. Invalid-input,
late file-lock, ordinary import and recipe-pack checks pass. Redirected-root
previews now read the imported mesh bank; the cube is visible in Arena and
placement undoes exactly. Core/texture/eight-map export checks pass; see HANDOFF_WORLD_UI.

2026-10-01 install depth: changing folders now refreshes mods and conflict picks
and clears the prior pack destination. Two-root switching, dirty-edit refusal,
byte-preservation and Setup restore checks pass; see HANDOFF_WORLD_UI.

2026-10-01 asset depth: tall texture previews preserve aspect ratio, narrow panels
wrap texture metadata and actions, and Assets pages use a full-name menu when
tabs cannot fit. Model Reset view stays visible. Compact UI, model browsing and
scratch texture round-trip checks pass; see HANDOFF_WORLD_UI.

2026-09-30 depth review: existing workflows are being audited feature by feature
in [FEATURE_DEPTH_AUDIT.md](FEATURE_DEPTH_AUDIT.md), starting with terrain,
objects and foliage. Grounded objects now follow terrain edits in the same undo
step, and baked foliage follows in the preview; focused core and retail UI checks
pass. The audit tracks unverified workflows explicitly.

AeoN's early editor feedback is reconciled in
[AEON_EDITOR_FEEDBACK.md](AEON_EDITOR_FEEDBACK.md). It distinguishes features now
present from workflow depth and retail-game checks that still need work.

2026-09-30 editor UX: active selection is above Quest sections; overview edit
points yield to nearby points while selected points remain visible. Floating
Terrain, Level and Properties tools close when their source section is left.
Six size/scale tours and the tool-window context test pass; see HANDOFF_WORLD_UI.

2026-09-30 area deletion: the Copy terrain rectangle now deletes enclosed
unlocked things by button or Del, with link/ownership cleanup and one undo.
Core and retail UI checks pass; see HANDOFF_WORLD_UI.

2026-09-30 radius view: selected radius-bearing things now show labelled,
terrain-following circles with an elevation line. The retail exit UI check
and viewport capture pass; see HANDOFF_WORLD_UI.

2026-09-30 meshless point clarity: viewport markers now have role letters,
distinct shapes, selected/hover labels and an in-view legend. The retail region
point UI workflow passes. Live FableWin comparison waits for the running game
to close; see HANDOFF_WORLD_UI.

2026-09-30 keyboard transforms: Edit now has selected-thing nudge, cardinal and
pointer facing, all three small-angle rotations, and fine/coarse height keys.
The retail UI script covers exact deltas and undo. See HANDOFF_WORLD_UI.

2026-09-30 parent validation: known Father/Mother sex mismatches now fail in
Document::setLink, using the editor's live definition lookup. Defs-only
sessions support that lookup. Core and retail male/female UI checks pass.
See HANDOFF_WORLD_UI.

2026-09-30 creature links: absent Home, Work, Father, Mother and Spouse fields
are now offered for AICreatures and saved in native order. Spouse edits are
reciprocal; clearing and deletion remove optional lines. The GUI checks
known parent sex from definitions, warning when unknown. Core and editor UI
tests pass; see HANDOFF_WORLD_UI.

2026-09-30 track naming: linking a new chain now assigns a unique
TrackTempName, preserving explicit names and avoiding numbers already loaded
in the map. Unlinking shares the allocator. Native decompilation and core/UI
tests verify the behavior; see HANDOFF_WORLD_UI.

2026-09-30 copy safety: native edit-brush copy exclusions now apply to
selection copy, terrain brushes, pasted old fragments and preset loading.
TrackNode duplication creates an unlinked standalone node; verbatim Village
duplication is refused before group edits. Core tests cover filter, clone,
paste and brush behavior. See HANDOFF_WORLD_UI.

2026-09-30 continuation: Assets > Effects now lets users choose Dark, Grey,
Light or a custom RGB preview background, saved in Forge settings. A paused
sprite preview shows the three clear colours in native UI captures; the Effects
browser regression passes. See HANDOFF_WORLD_UI.

2026-09-30 owner research: read-only retail decompilation confirms that
CTCOwnedEntity activation resolves its serialized OwnerUID and registers the
child in a live CTCThingOwner list, creating that component when needed. This
supports the existing strict UID graph. Valid owned descendants now follow
parent movement and rotation, including gizmo/carry preview, height and ground
commands, in one undo step. A saved checkbox disables transform propagation.
Quest-section and day/night changes now move valid owned descendants too, with
byte-preserving relocation and exact undo. Deleting an owner now offers Delete
all, Only selection (which detaches surviving children), or Cancel. Core and
native UI checks pass with one-step exact undo; details are in HANDOFF_WORLD_UI.
Single-object duplication now clears a copied `OwnerUID`; duplicating a selected
parent-child group remaps the copied links to the copied owners. Native clone
creates a fresh definition instance, while Forge still preserves the copied
block's other properties. Core/UI duplication and carry regressions pass.
The Selection card now displays a cached `+N owned` hint for valid descendants;
its selection changes and visible placement are UI checked.
Deleting a thing now clears unambiguous incoming UID links and repairs a
surviving track chain in the same undo step. Duplicate target IDs and ambiguous
fields are left untouched. Core and editor delete/track regressions pass.

2026-09-30 continuation: Shift+click now places the chosen thing on pointed
terrain. Left drag carries unlocked things and selected groups over terrain,
preserving ground offsets; Escape cancels and release gives one undo step.
Fixed-height carry and the viewport path pass `ground_place_carry.txt`.
Ctrl+Shift+drag clones and carries; Ctrl+D carries a new copy until ground click
or Escape. One undo restores the pre-clone document. See HANDOFF_WORLD_UI.

2026-09-30 continuation: window close and File > Exit now guard unsaved object,
terrain and pending World edits. Failed World writes retain pending edits/undo;
native close, map-switch and failed-write UI checks pass on in-memory or
nonexistent-root fixtures. See HANDOFF_WORLD_UI.

2026-09-30 continuation: meshless markers and region exits now have shared Edit
viewport glyph drawing/picking, a Markers chip, Objects-list filtering and focus.
MayorsHouseHallway native UI checks pass for visibility, Ctrl picking and context
selection and cross-document reload. CTCDRegionExit entrance links are now exposed
and synchronized with scripted-hook links; core and UI checks now cover both
same-marker rejection and valid entrance targeting with exact undo. See HANDOFF_WORLD_UI.

Bedtime checkpoint (2026-09-29): work paused at the user's request. Current build,
validation, remaining limitations and the researched next marker/picking feature
are recorded at the top of [HANDOFF_WORLD_UI.md](HANDOFF_WORLD_UI.md).

2026-09-29 continuation: **Assets > Models** now browses meshes with an orbitable
textured/wireframe preview, material links and definition references. World flight
now has denser overview terrain, stable distance selection, and memory-constrained
automatic expansion beyond six maps. Validation and remaining limitations are in
[the world/UI handoff](HANDOFF_WORLD_UI.md).

Later that day, performance work paused at the user's request. **Assets > Effects**
now inspects decoded sprite/mesh/light systems with asset links, and the editor
has contextual object actions plus a floating selection inspector; native UI
checks pass. Selected-effect sprite playback now adds Play/Pause, Restart, Step,
orbit and zoom. Bounded mesh playback now composites with sprites; fixed and
random initial/spin-axis mesh orientation are previewed. Direction/game-driven
mesh orientation, actual light illumination,
in-world playback and editing remain unfinished. The reported
ArenaHallOfHeroes appearance is reproducible, but missing-mesh checks are clean
and no speculative orientation fix was applied; see the
[content audit](ARENA_HALL_CONTENT_AUDIT.md).

Further continuation: existing container contents and creature-family lists now
support grouped/searchable add, replace and remove controls, including empty
blocks, with tested undo. Particle previews additionally simulate bounded orbit
and attraction, and correct size flags, scaling and alpha-test behavior against
the pinned EgoCore reference. Specialized orbit modifiers remain approximations.
Creature movement and cross-map paste now synchronize existing saved InitialPos
fields with the destination world origin; undo and rotation preservation are tested.
Locked in place now protects transforms, deletion and terrain reseating, with
checkbox/menu/Ctrl+L controls, mixed-selection handling and tested drag cancellation.
Known scalar overrides on seven existing component types can now be added when
omitted and reset, with strict validation and byte-exact undo. Effective defaults
and definition-declared missing components remain unresolved.
Particle meshes now use authored bounds and native-confirmed XYZ/fixed-axis
rotation, with deterministic native UI and pixel checks.
H now cycles visible object surfaces with locked-selection filtering and undo;
End remains direct terrain drop. Native physics-radius sweeps remain separate.
Ctrl+H sets absolute selection height with per-object terrain clamping, locks
and grouped undo; owned-child movement now follows the shared transform path.
Show in palette reveals the selected object/creature definition without editing.
Component lights now animate bounded colour/radius volumes using native timers;
scene illumination and runtime light-position parameters remain separate.

Latest continuation: [world view, UI validation and next asset/UX work](HANDOFF_WORLD_UI.md)
(2026-09-28). Covers visible streamed water, world-picking speed, culling,
automatic detail budgets and the user's design constraints.
See also [performance research](WORLD_PERFORMANCE.md) and [CPU/GPU profiling](PROFILING.md)
for measured bottlenecks, the optional Tracy build and remaining renderer work.

## Vanilla editor parity (Aeon's review, 2026-09-27)

Aeon (Discord, 2026-09-27) reviewed the editor against the vanilla Lionhead
editor (`FableWin.exe`, the dev build). The direction: **sift every vanilla editor function,
reimplement it here, refine it, and only then add new features.** The static
inventory of what vanilla has, with FableWin addresses and confidence, is
[VANILLA_EDITOR_INVENTORY.md](VANILLA_EDITOR_INVENTORY.md) (section 7 answers
each review point). FableWin runs from a scratch Anniversary dev tree, not the
install. The route in is the dev profile screen, then A (Debug Profile), then 4 (edit
FinalAlbion.wld). The on-screen walkthrough of its modes has not been done yet.

Work lands on branch `feat/editor-ui-shell` (not merged). **Done** = logic under a test; **UI unseen** =
the panel wiring builds but nobody has looked at it on screen.

| # | Item (review point -> vanilla evidence) | State |
|---|---|---|
| 1 | Scale gizmo too sensitive | **Done** `f440eaa`. It was a compounding bug: the cumulative ImGuizmo ratio was applied per frame. |
| 2 | Redundant top bar -> File / View / Help menu bar | **Done** `fdf48d8`. Screenshot seen. Button alignment fixed in `1a254d9` (screenshot seen 2026-09-28). |
| 3 | Collapsible left / right panels | **Done** `fdf48d8`: chevron strips, View menu, Ctrl+[ / Ctrl+], persisted. Screenshots seen. |
| 4 | Ground texture at the lowest mip | **Done** `1a254d9`: 4..16 texels per cell per map, GPU mips, anisotropic (screenshot seen 2026-09-28). |
| 5 | Replace theme (pen) / Flood Replace (click) / theme eyedropper (Ctrl+click) | **Done** `0f2c3f3` (vanilla `EditReplaceEngineThemeUndoable`, `EditFloodReplaceEngineThemeUndoable`, `PaintInputPickupTheme`). Screenshot seen 2026-09-28. |
| 6 | "Replace All" | **Done** `0f2c3f3`. Not in vanilla, which floods only the connected patch. |
| 7 | Heightmap path maker | **Done** `0f2c3f3`: Draw path, drag start -> end (vanilla `EditDrawPathPenUndoable`). Screenshot seen 2026-09-28. |
| 8 | Load other .wld files, not just FinalAlbion | **Done (stage 1)** `6c455b0`: File > Open world, maps grouped by region; the map's own .lev/.tng are read and saved. Later: STB bake for other worlds; world moves / regions for them. Screenshot seen 2026-09-28 with a Lionhead dev world (lake-n-shack.wld: Lake_and_Shack opens with terrain, water, objects; `tests/ui/open_world.txt`). |
| 9 | Paintable themes from ENGINE_THEME defs (grouped), local detail applied | **Grouping done** `ec002b4`: tree by ENGINE_THEME_GROUP (screenshot seen 2026-09-28). Local detail is open; it is not proven editor-side in vanilla. |
| 10 | Per-class property tabs (CTC editing) | **Done** `595dda5` + `6526402`: property grid per CTC block, vanilla captions/def pickers/enums/ranges from the recovered table (`docs/re_reference/vanilla_property_fields.tsv`), kind-checked edits (screenshot seen 2026-09-28). CreatureFamilies list controls and CTCLight colour picker have since landed. Open: CameraTrack name<->UID and adding a component a def lacks. |
| 11 | Actors from def entity groups by category, not Forge presets | **Done** `80aa11d`: type -> THING_GROUP -> def tree (3,972 defs / 93 groups vs retail), Actors tab leads with creatures by group, `forge def-groups`. Screenshot seen 2026-09-28. Presets card kept (user's call). Markers / villages / switches are not placeable yet. |
| 12 | Whole-world view, click a map to activate it, several maps at once | **Done (view) 2026-09-28**: the World tab draws every map's ground in its 2D box (vanilla world-map quads) and has a **Fly over (3D)** view of all 400 maps at their WLD origins (src/worldtiles: a ~40-quad height grid + a 1-texel/cell ground picture per map, built on worker threads, cached in %LOCALAPPDATA%/FableForge/worldtiles: 400 tiles in 0.1 s warm); hover names a map, click selects, double-click edits it with the camera kept (`tests/ui/world_view.txt`, screenshots seen). Open: several maps editable at once (vanilla "Locked for editing" per region); placed objects in the 3D world view. |
| 13 | Quest sections: toggle, choose current, add | **Done** `72037f6`: show/hide, current section for new things, add (free names, beyond vanilla), move selection (screenshot seen 2026-09-28). Later: FSE quest registration of a new section. |
| 14 | Paint environment themes and sound themes | **Done** `ea682b2` + `9fa5a2c`: the .lev game-map grid (4x4 cells; atmos palette + sound list, all 399 retail maps parse), environment + sound brushes, environment defs added to free slots (screenshot seen 2026-09-28). Adding a sound name the map lacks: **done** (the .lev after the list is re-laid; 403/404 dev levels verified). |
| 15 | Nav from NAVIGATION_SEED + paint navigability | Nav lane (`HANDOFF_NAV.md`). Vanilla rebuilds nav on save and has passability, camera-passability and pref-nav brushes. **Camera passability brush done** (+16; screenshot seen 2026-09-28). Walkable paint existed. The pref-nav brush waits on the nav lane's cost propagation. |
| 16 | Fractals | **Done** `1564660`: the vanilla generator ported from its code (inventory 10), vector-tested; Fractal terrain window applies it to the open map; compared live with the vanilla Fractals dialog 2026-09-28 (same fields and defaults; falloff start/end shown when ticked). Preview now in the vanilla colours (CEditFractal::HeightColours 0x03ff9550 bands, DrawFractal 0x029a76b0 slope shade + 256-unit checker; top-down, where vanilla draws it oblique). Open: applying across several maps at once. |
| 17 | Linking: parent / village / entrance<->exit ... | **Done (stage 1)**: owner, village, home, work, wife, exit->entrance, trigger->receptor, route; pick in the view, fit check, lines (UI unseen). Open: spouse/parent (not seen as .tng fields yet), links to another map's entrance. |
| 18 | Skybox / colour LUT follow ENVIRONMENT_THEME | Later (optional; vanilla does not do it either). |
| 19 | UI overhaul modelled on the vanilla editor's UI | Ongoing. Toolbar of modes, dialogs per mode, hotkeys 1-9. |
| 21 | Tracks (vanilla Tracks dialog; not in the review) | **Done** `7040517`: place / link / flip / rename / unlink TrackNode chains, lines in the view; **Preview** (vanilla Play Track, `PreviewCameraTrack` `0x0202f090` / `UpdatePreviewTrack` `0x0202f380`: the camera rides one track looking at a point riding another, straight segments by arc length over 1-100 s, then snaps back; `forge/trackpath`, `testTrackPath`, `tests/ui/track_preview.txt`, screenshot seen; Esc stops it, which vanilla cannot) (screenshot seen 2026-09-28). Script brushes: researched, parked (no retail use; inventory 10). |
| 22 | Fit Neighbours (vanilla world-map popup; not in the review) | **Done**: `EditFitFillerMap` ported from its code (`forge/fillerfit`, unit-tested: RNG, spline, exact seams, determinism); Fit to neighbours card with side map + Now/Fitted preview (screenshots seen). Open: the pack/World-tab entry point, several fillers at once. |
| 23 | Placement options (vanilla Things dialog; not in the review) | **Done**: random angle (`GFFloatRandom` off a seed), fixed angle, fixed height (absolute Z, never under the ground), ported from `PaintInputPlaceThingAt` (inventory 11b); Facing + Fixed height controls in Add an object, "Sample here" for the ground height. Unit-tested; `tests/ui/placement.txt` (screenshot seen). |
| 25 | Budget survey (vanilla Surveys > Engine; not in the review) | **Done**: `forge/budget` (vanilla's include flags, one count per mesh unless "Count every copy", texture memory per distinct texture = its full mip chain), a floating tool window from the Level tab: Whole map / Selected / Around the view, kind chips incl. Plants (the baked local detail), four totals, breakdown by definition / model / texture, Save report. Unit-tested; `tests/ui/budget.txt` (screenshots seen). StartOakValeWest: 19,344 things (18,709 plants), 141k triangles (638k every copy), 26.7 MB textures. |
| 26 | Minimap vs the engine (Aeon: "check our minimap bake against the editor's own generator") | **Done**: the generator decode re-verified; retail art is hand-painted but in the engine's frame (`GetRelativePosOnMiniMap`); our bake stretched the map, which put the hero marker off the art on non-square maps. Now baked through the engine's inverse transform with a centred `MiniMapScale` / `MiniMapOffsetX/Y` written to the region (takeover + dedicated). `forge/minimapframe` unit-tested; `test_newlevel.py` bakes a 128x64 own region and checks the WLD. `forge minimap-bake`. Inventory section 10 ("Minimap framing"). **In-game verified 2026-09-28** (adult hero, Lua package v28, retail transition): the HUD hero marker sits on the painted path on retail BanditCampPath1 (offset Y -83) and on the baked path on a 256x64 FableForge takeover region (centred, offset Y -96), same spot for the same local point. The child prologue draws no markers at all. |
| 27 | Brush library + Copy things (vanilla Copy and paste / Brush library dialogs) | **Done**: Region copy takes the objects inside the rectangle too (vanilla GetCopyThings); paste turns them with the ground and sets them at their old height above it, one undo step with the ground; saved copies as brushes (`<output folder>/brushes/*.brush.json`, themes by name like vanilla CEditMapBrush) listed in the Region card, click to load and paste. Pasting objects is now ~10x faster (each block is prepared in a one-thing file; editing in place re-indexed the whole .tng ~12 times per thing), which also speeds up Ctrl+V. Unit test + `tests/ui/brushes.txt` (62 objects of Oakvale, screenshots seen). |
| 29 | Height Toolbox pens (vanilla Paint Map dialog, Land tab; seen live 2026-09-28) | **Done**: `forge/heightpen` ported from FableWin (Change Height `0x02975710`, Paint Height `0x029753d0`, Smear `0x0297b3c0` + `GetFilteredHeight` `0x02974c50`, Noise `0x02975f00` with the engine LCG), unit-tested (`testHeightPens`). Raise / Lower gain "Exact step" (vanilla Change Height: +-step once per block per stroke, a terrace); Flatten = vanilla Paint Height (target box or the stroke start, Ctrl+drag samples in any sculpt tool with a visible Paint height, Speed = 1 - cos(pi/2 s)); Smooth = vanilla Smear (Smoothness, Spikes allowed: straight slopes stay); new Noise tool (Amount 1-50); "Repeat while held" = Spray can. Hard discs round the rounded pointer, clamp [0, 2048), no dt. Kept ours: the linear radius slider (vanilla 2^s, 0.25-32) and the smooth Raise / Lower. The -/= keys (`EditPlaceHeightAlterationUndoable` `0x029796b0`) raise / lower one latched ground point by the Speed opacity each frame held in sculpt tools (including Paths), one undo step per press. Not ported: the water re-fit in vanilla's height setter. `tests/ui/height_pens.txt` (heights asserted, screenshots seen). |
| 31 | Full comparison with the vanilla editor (2026-09-28) | 91 verified gaps / weaknesses (63 editor, 28 assets + animation) with vanilla evidence and fix plans: [PARITY_BACKLOG.md](PARITY_BACKLOG.md). Worked through by value. |
| 30 | Scene Browser (vanilla `CSceneDialog`; seen live 2026-09-28) | **Done**: the Objects list gains "Script-named only" and "Nearest first" (distance from the camera); the text filter and double-click-to-fly already covered Refresh / GO / Center Object. `tests/ui/scene_browser.txt` (camera over a cupboard: it heads the list; script-named only drops it), screenshot seen. |
| 24 | Seen live 2026-09-28, not ported yet | Water generators (lakes / ocean / rivers; with the `water` branch), villager-preferability brush (waits on the nav lane). Inventory 11b. |
| 28 | Owner combo + day/night creatures (vanilla PLAYER_LIST_BOX, O key, Quests dialog Day only / Night only) | **Done**: Owner combo in Placement options (Auto / Player 0-3 / Neutral) writes the .tng `Player` of owner-taking things; Auto = Neutral for non-creatures and the def's `DefaultOwner` for creatures (read by its CCRC tag; 274 retail creatures 0, 232 2, 2 3), as `CThingAICreature::ConstructFromParams` does; O gives the selection the combo's owner (Auto = Neutral, one undo). Day/night lives only in the section name (`<quest>%DayOnly` / `%NightOnly`, exact case): a Day and night / Day only / Night only switch for creatures and Day-only / Night-only filters in Sections (on by default; vanilla starts them off). `tests/ui/owner_daynight.txt`, unit test. Also fixed the stale `tests/ui/editor.txt` drop step (a PNG drop opens Ground themes since `ba62199`). UI unseen. |
| 20 | Remove direct object import and new-texture import from the world editor | **User's call.** Aeon advises it (complexity; an EgoCore cross-workflow later). Not done. |

## Navigation and editor research (2026-09-27)

The navigation/overworld integration is on local main. The experimental,
read-only `nav-compare` path compares object hulls, detailed leaves, switchable
doors, neighbours and regions against shipped navigation; it is not yet the
production writer. Current results and remaining input/layer gaps are in
[the navigation handoff](HANDOFF_NAV.md).

Two research guides support the integrated modding environment work below:

- [Debug editor feature audit](DEBUG_EDITOR_FEATURE_AUDIT.md): recovered native
  evidence for missing/partial tools, unknown behavior and confirmed stubs.
- [s&box modding inspiration](SBOX_MODDING_INSPIRATION.md): pinned source examples
  and proposed acceptance criteria for inspector metadata, asset dependencies,
  presets, shared transactions and eventual edit/play sessions.

## Resume here (2026-09-28, end of day)

**Branch `feat/editor-ui-shell`** (unmerged, unpushed) now also carries, all committed and tested:
- **Mod packs, steps a-e**: recipe packs; the editor writes objects / terrain / world edits / new
  levels INTO a pack ("Writes go into" picker in the Edit and World footers; world edits run on a
  shadow copy and `mods capture` the difference); the composer lays pack static-map chunks into the
  STB (appending new levels), merges FinalAlbion.bwd/.wld per map and region (`forge/worldmerge`,
  byte-identical to sequential edits), and re-points theme palettes by name. Tests:
  `tools/test_pack_levels.py`, `tools/test_pack_world.py`, `testWorldMerge` / `testFillerFit`.
- **Mods tab as a plugin list**: drag to reorder, wins/loses badges from Check conflicts, pack
  masters (`requires` in forge_pack.json, right-click a row) flagged in the list and by `mods build`
  (`tools/test_mods_masters.py`).
- **Fit to neighbours**: the vanilla world map's Fit Neighbours (`EditFitFillerMap`) ported from
  its code (`forge/fillerfit`), in a floating **tool window** (Now / Fitted relief, sides lit).
- **Tool windows** (`App::beginToolWindow`): big occasional tools pop out beside the view instead
  of stretching the sidebar. Fit to neighbours and Fractal terrain use it (screenshots seen).
  Candidates next: the selected thing's property grid (an inspector), New level, Check the map
  (results list), Tracks, Region properties, Assets imports, Mods conflicts, the brush library.
- Design philosophy (the user's): non-intimidating, intuitive, beautiful, modern, functional,
  powerful. Port vanilla BEHAVIOUR (its algorithm, its parameters), never its UI.

**Next (user, 2026-09-28): drive the vanilla debug editor autonomously**, note where it sucks and
where it rocks (`docs/VANILLA_EDITOR_INVENTORY.md` section 11 has the live notes so far), and
improve FableForge with that, per the philosophy. How to drive it: FableWin runs from
`D:/tmp/fablewin_editor/Fable_Anniversary-2013-02-25/Fable` (dev profile -> A -> 4; skip the `edit_component.cpp 228`
assert with M); the scratchpad driver's window-relative `wmove/wclick/dblclick/wheel/wdrag`
actions work (the old relative-walk ones miss the window); zoom far out to see the map grid;
panels drag by their title bars; arrow keys and letters are mode hotkeys; ask before launching
(it takes the screen) and never press Save all. Still to see live: getting into a level for
Thing mode / CTC editing, ScriptBrush, Survey, Engine themes, the brush library.
**Done 2026-09-28 (morning):** the vanilla editor driven hands-free (inventory 11b: the way into a level is the map card's "Locked for editing"; 3D works once its graphic-bank assert is skipped; things never render in the dev tree; every survey tab seen). Placement options ported (parity 23). Row 24 lists the rest seen live; the budget survey is done (parity 25); minimap-zone painting was dropped: its writer is an empty stub in FableWin and retail (inventory 11b). The real generator to check our bake against is `CRegionMinimap::BuildRegionMinimapTGA`.
**Documentation** (user, 2026-09-28): start a proper user guide before things get too complex -- task-first pages (make a level, paint, fit fillers, mod packs, load order), screenshots from the UI scripts, kept next to the features they describe.
Parity rows 22 (Fit Neighbours) + the vanilla wins noted so far: track **preview playback**, the
thing list's **filters** (script-named only, sort by distance, area range).

## Resume here (2026-09-26)

**Branches.** `modpacks` (23 commits: the whole 0.20 mod-pack composer + the 0.17 static mesh
import, in-game verified 2026-09-20 evening) is **fast-forwarded into `main`** (user decision
2026-09-26: release from main with mod packs in it, instead of tagging 0.16.0 first). `water`
(11 commits, 0.18: water brush, foreground `CWaterPatchMesh` writer, background sub-patch,
`forge water-audit`) stays a **separate branch**: never seen in-game, and its last commit records
a range-codec mismatch against the shipped bake (an older compressor's cost model; our blocks
decode, retail terrain blocks re-encode exactly). Merge it only after an in-game look.
`tools/ingame/gamewin.ps1` now sends scan codes (DirectInput ignores bare virtual keys once the
hero is in the world) and has `hold` / `lmb`.

**Suite on the merged main:** 21 checks; 19 passed in the full run. `overworld moves` and `textures tab` failed because they hard-coded the next texture id as 6294, which only holds for a retail `textures.big` (the install carries three `FORGE_PROBE_CUBE*_DIFFUSE` entries from the mesh probes). Both now take the id from their scratch bank and pass.

**Install caveat:** the 2026-09-20 mesh probes left 4 cubes + a barrel in StartOakValeWest and
their mesh/def/texture entries; `release_probes.py --stage mesh_undo` puts them back. Check
`forge backups` -> 0 differ before any release probe.

**Next, in order:**
1. ~~Release~~ DONE 2026-09-26: **v0.17.0** tagged and released (https://github.com/BuffJesus/FableForge/releases/tag/v0.17.0, CI green, zip attached; UI polish pass in the same release). The stranger's test is deferred by the user; the `modpacks` branch is now fully in `main`.
2. FableTLC's `docs/engine/IN_ENGINE_MODDING_ENVIRONMENT.md` Â§9 makes FableForge the offline half of
   the modding plan. Its "now" items that land here: (a) an **entity annotation file** (the FGD) off
   `defschema` for the property panel; (b) finalize the **mod manifest** (`forge_mods.json`) with the
   script-registration block of FableTLC `docs/engine/SCRIPTING_REDESIGN.md` Â§6 and a folder layout
   an engine VFS can mount; (c) a **model recipe** (the QC role) over `forge mesh-import`; (d)
   **per-map bake as a forgecore call** (`lodbake`, `stbterrain`, `stbcompact` still live in `src/`;
   nav is per-cell patch only).
3. `water`: in-game look, then merge.

## Resume here (2026-09-20, afternoon)

**Today:** suite ALL PASS 16/16 at `c5000b5` (the two synthetic-click suites passed under the full
run). Public repo is live: https://github.com/BuffJesus/FableForge (`main`, CI on push + tags; the
first run failed in "GUI without an install" with a silent exit 1 -- `47d0522` makes the GUI say the
D3D11 HRESULTs in `--auto` mode and the workflow prints the run log; run 2 showed WARP works and only the screenshot's missing `build/ui/` folder failed; `6d32101` creates it -- **CI GREEN** from run 3).
README got a *Get it* + safety block for strangers. `tools/ingame/release_probes.py` is the
unattended step-3 probe pass (RELEASE.md), dry-run clean, **not yet run against the game** (the
user asked to wait). `place_fishing_spot [reward|-] [scriptname]` so the harness can find one.
**In-game probes DONE (2026-09-20 afternoon, `release_probes.py`, all four stages PASS):** retextured barrel
seen red in-game; barrel/emitter/fishing spot found at their exact .tng positions on a fresh game; a full
session on the compacted bank (121/121 heights); `ProbeOwnRegion` installed as new slot 400, fresh game,
retail transition `StartOakVale -> ProbeOwnRegion`; restore -> 0 differ. Two bugs it found are fixed
(`6cd6036` texture-export/replace by listed name; `4888f12` `place OBJECT_ <name>` dropped the ScriptName).
Human-only leftovers: the preset in `build/ingame/release/things/04_after_probe.png`, the map-screen click.
**Later (afternoon):** regressions for both probe bugs (`tools/test_textures.py` by listed name + id + CLI
replace; `tests/ui/editor.txt` named barrel -> `assert_file_contains`), a 17th check `tools/check_docs_commands.py`
(every `forge`/`forge-tools` command the shipped docs name must exist: it surfaced nine working diagnostics
hidden from `forge --help` and 35 `forge <family>` mentions in docs/modding that are `forge-tools`), the
walkthrough's 28 UI labels verified against the GUI source. Suite ALL PASS 17/17 at `9213b50`.
Next: (1) ~~CI green~~ done, (2) ~~`python tools/ingame/release_probes.py`~~ done, (2b) `python tools/ingame/release_probes.py` with the game closed, then judge
the two screenshots, (3) `tools/package.py` -> stranger's test -> `git tag v0.16.0`.

## Resume here (2026-09-20 morning; written at the end of 2026-09-19)

This repo IS FableForge (`D:\Code\FableForge`, formerly Albion Atlas; the old toolchain repo is
`D:\Code\FableForge-legacy`). Exes: `build\FableForge.exe` (GUI), `build\forge.exe` (CLI,
`src/cli/*.cpp`), `build\forge-tools.exe` (the legacy CLI, 133 commands, `tools/forge-cli`).
`libs/forgecore` is canonical. Version 0.16.0. `tools/check_all.py` = 16 checks, **ALL PASS** at
`d1f54ac` (18 commits on 2026-09-19, tree clean). Fable.exe must be closed for the suite and
for any write to the install (every writer now refuses while the game runs from that install).

### State of 1.0
Every "Must" row in section 1 is done except the two that need a second machine and a public
remote. Everything on the code side of 1.0-rc is done (docs 3/3, perf, CLI split, bank
compaction, `RELEASE.md`). Nothing new is in-game verified since 2026-09-17.

### Next, in order (all the user's, per `docs/RELEASE.md`)
1. Game closed -> `python tools/check_all.py` -> ALL PASS.
2. In-game probes on a **fresh** game: map-screen travel to an own-region level through its
   entrance; a retextured barrel; a placed preset; a placed particle emitter; a placed
   **fishing spot** (Actors card); one session on a **compacted bank** (`forge compact-stb`).
   Then `forge restore`, `forge backups` -> 0 differ.
3. Public GitHub repo `FableForge` -> push -> CI green.
4. `python tools/package.py` -> `dist/FableForge-0.16.0-win64.zip` -> stranger's test on a
   second machine/VM (`docs/FIRST_LEVEL.md` end to end; the zip already dry-runs from a clean
   folder on this machine) -> `git tag v0.16.0` -> release + Discord post.

### After 1.0 (planned only, nothing started; do not queue ahead of the tag)
- **0.18 Water** -- RE complete (`FableTLC docs/engine/WATER_RE.md`): water is depth-theme paint
  (works today, gameplay water) + a baked STB `CWaterPatchMesh` retail only loads (layout and
  constants known). Order: water brush -> foreground writer (retail round-trip gate) ->
  background/shore/sea.
- **0.19 World in 3D** -- renderer multi-terrain over the existing world-placed bakes.
- **0.20 Mod packs v1** -- the legacy `mods`/`fmp`/`stage` family (`docs/modding/`), one
  composer for EgoCore mods and every older mod shape, mod things with provenance in the
  editor; corpus on disk (`work/nexus_mods/CATALOGUE.md`, GB packs in `D:\Downloads`).
- Chunk-level compaction (scoped in 1.0-rc #4), foliage brush (needs the harness + user).

### Watch list
- The synthetic-click suites: scripted runs now drop real mouse input (hardening, not a proven
  fix). If `ui paths` / `ui foliage` still flake in a full run, that theory is wrong.
- `ENGINE_RULES` says retail loads TNGs from the WAD only; Connected Regions ships 397 loose
  TNGs. Verify in-game before 0.20 relies on either.

Install state: retail per `forge backups` (10 backed-up files, 0 differ; the extra one is a loose
`OakValeWest_v2.tng` of identical content from tonight's guard test). The STB's `.atlas-orig`
baseline carries a `__ENGINE_SEA_STATIC_MAP_BANK_FILE__ForgeTest64` entry from the FableTLC
era (the `.forgebak` is true retail); harmless, 3.2 MB reclaimable. Saves: `0atlas` and
`1234234` carry childhood autosaves, `Cornelio` is the adult save (`--save-from Cornelio`).
FSE has no live-link hook installed.

---

## History: the night of 2026-09-19 (18 commits)

**2026-09-19 (last):** the release zip was dry-run from a clean folder (all three exes + GUI
against the Steam install), CI re-checked against the current targets, scripted runs now drop
real mouse input (flake hardening), the Nexus corpus is on disk for 0.20 (`work/nexus_mods/`).

**2026-09-19 (late):** the 1.0 "Must" table closed two rows -- *refuses to damage a running
game* (one guard for every writer, matched to the target install, proven with the game up) and
*runs on a clean machine* (static exes verified) -- plus `docs/RELEASE.md`. Open for 1.0 now:
the public repo/tag (user), the in-game probes (user), the stranger's test (user).

**2026-09-19 (night, later):** fishing spots (Actors card, `place_fishing_spot`), STB bank
compaction (`forge compact-stb`, Setup button, unit test), and four milestones *planned only*
-- 0.18 Water (RE'd from the debug build), 0.19 World in 3D, 0.20 Mod packs v1 (legacy
family researched, design docs ported), chunk-compact scoped. Suite ALL PASS 16/16.
Nothing new is in-game tested: the probe list in *Next in order* #2 grew by a fishing spot and
a compacted-bank session.

**2026-09-19 (evening):** three commits on top of the rename: the cutscene/title tooling from
the Oakvale Reborn session (`forge-tools script cutscene-*`, `title add`); a **foliage fix**
(the frame probes read a 4-byte lattice and missed ~75% of every map's baked foliage, incl.
Oakvale's town-square oak -- `docs/TODO.md` foliage block); and a **perf fix** in the shared
frame walker (23 GB of memset per map; boot + open Oakvale 3.65 s -> 0.79 s, the suite's
deploy checks ~35% faster). Suite ALL PASS 16/16 after each. Known flake: the synthetic-click
suites (`ui paths` textures checkbox, `ui foliage`) occasionally missed a click under the full
run and passed alone; since 2026-09-19 (late) `--auto` drops all real mouse messages before
ImGui sees them (the only path by which the user's cursor could reach a scripted click) --
a hardening, not a proven fix: a cursor-wiggle control run did not reproduce the flake on the
old binary either. Watch the next few full runs.

State at the end of 2026-09-19: 0.14 done except the public repo (user), 0.15 4/4, 0.15b 8/8 +
tour, 0.16 3/4, 0.17 2/6 (Textures tab, effect picker), 1.0-rc: docs 3/3, perf, main.cpp split +
automation.cpp, ENGINE_RULES.md, object-palette mesh thumbnails, bank compaction, RELEASE.md.
Scoped-not-started: foliage brush (harness), mesh/creature import (L), Blender addon bundle
(vendor its Python first), chunk-level compaction, texture-tab thumbnails, PiP minimap, brush
falloff ring.

## Context

Albion Atlas (`D:\Code\AlbionAtlas`, v0.13.1, no public remote, no CI) started as a map
exporter and grew in three days into a working replacement for the leaked Lionhead debug
editor: things, terrain, textures, new levels, regions, overworld, creatures, spawners and a
live link to the running game â€” every feature verified in the real engine by the unattended
harness. The user wants it to reach a **1.0 for the Fable modding community first**, then
keep growing as a power tool. "Improve" therefore means three things at once: make it
*safe and self-contained for strangers*, *deeper as a level design tool*, and *healthy as a
codebase* â€” and know exactly where the engine draws the line.

Everything below was checked against the code and the in-game evidence gathered in this
session; nothing is speculative unless marked *(untested)*.

---

## 1. What 1.0 should be (definition of done)

A zip someone on the Fable Discord downloads, unpacks, points at their Steam install, and
uses to **build a playable custom level with its own region, terrain, textures, objects,
NPCs and enemies â€” and walk into it â€” without touching a command line or breaking their
game**. Concretely:

| Must (1.0)                                                                   | Status today |
|-------------------------------------------------------------------------------|--------------|
| Runs on a clean machine: no Python, no FableTLC/FableForge checkout           | **Done** (native texture importer 2026-09-17; verified 2026-09-19: all three exes link `-static`, imports are Windows system DLLs + the UCRT `api-ms-win-crt-*` + `D3DCOMPILER_47` only = any Windows 10/11; `tools/package.py` zip = exes + docs + presets + `docs/re_reference` + `docs/modding`, 8.8 MB). The stranger's test (1.0-rc #5) is the final proof |
| One-click *restore my install to retail* and visible backup state             | **Done 2026-09-17**: `src/backups` registry, Setup panel *Restore the retail files* (with confirm) + per-file state, CLI `forge backups` / `forge restore`; refuses while the game runs |
| Refuses to damage a running game                                              | **Done 2026-09-19**: every writer (deploy, terrain, new level, world moves, entrance, textures, restore, compact) goes through one guard -- the live-link heartbeat when the link is installed, and always a `Fable.exe` process scan matched to the *target install* (`backups::gameRunningIn`, so a game running from another copy or a scratch tree is not blocked); proven with the game up (`deploy: Fable.exe is running from this install ...`) |
| New level end-to-end from the GUI (blank/copy, own region, minimap, textures, objects, NPCs, spawner) | Done, verified in-game |
| Tells the user the engine's rules where they bite (new region needs a new game; saves cache entities; spawners need an adult hero) | **Done 2026-09-18**: `raiseRule`/`drawRuleNotice` under the Place / spawner / new-level buttons (new region -> new game; saves cache entities; spawners need an adult hero), "Got it" per session; `docs/ENGINE_RULES.md` is the FAQ |
| Export still works as before (GLB/OBJ)                                        | Done |
| Documented for a newcomer (install, first level, FAQ of engine rules)         | **Done 2026-09-18/20**: `docs/FIRST_LEVEL.md` (first level in ten minutes, harness screenshots), `docs/ENGINE_RULES.md`, README *Get it* + safety block; the zip ships them flat next to README with working links |
| Public repo, tagged releases, CI that builds + runs the offline suite         | Missing (no remote, no workflow) |

| Could (post-1.0, "power")                                | Notes |
|-----------------------------------------------------------|-------|
| **3D world view** (the World tab in 3D, not only the 2D map-box grid) | Planned 2026-09-19, post-1.0 (see *0.19 â€” World in 3D*). Not an RE problem: the exporter already builds any map's terrain at its world origin (`export --world`, `world` lists every box/origin from WLD/BWD); the gap is the renderer holding ONE terrain (`gui/renderer.hpp` `upload(scene)` / `indexCount_`) |
| Foliage brush (grass/trees) into the STB local-detail tree | `reseatFoliageZ` already walks every primitive; a writer for new instances = the "environment brush" memory item; no RE blocker |
| Water editing (lakes/rivers via theme WaterHeight)        | RE'd from the debug build 2026-09-19 (`FableTLC docs/engine/WATER_RE.md`): water = depth-theme paint (`WATER_LAKE_{0..16}` etc., surface = ground + Î£ blendÂ·WaterHeight) â€” the paint side works today and gives gameplay water; the *visible* surface is a baked STB mesh retail only loads, and our deploy writes `hasWater=0`. Plan: water brush (altitude â†’ depth mix) â†’ `CWaterPatchMesh` writer (layout known, zeroed shore) â†’ background sub-patch/shore/sea |
| Prefab / preset library (village, bandit camp, shop)      | Village + spawner + creature blocks exist; a preset = a TNG fragment with relative positions |
| Multi-select, copy/paste across maps, align/snap tools    | Editor is single-selection today |
| Fishing spots                                              | Done 2026-09-19 (Actors card + `place_fishing_spot`; retail `MARKER_FISHING_SPOT`, optional first catch); untested in-game |
| Region entrance points (`FinalAlbion.gtg`) for map-screen travel | Format is plain TNG text (REGION_ENTRANCE_POINT things) â€” parseable with `forge::tng` |
| Custom ENGINE_THEME editing (cliff PNG, bump map, water) | `theme-add` covers base+cliff; the rest are `defedit::setField` calls |
| STB compaction after many edits                            | Chunks grow by the old foliage section per re-layout; the STB grows a chunk per replace |
| Custom meshes, creatures, object textures, particle effects, Blender round trip | All formats decoded and written from Python/Blender in FableTLC; native Atlas versions = milestone 0.17 below |

| Can't (engine facts â€” say so in the UI, don't promise)   | Evidence |
|-----------------------------------------------------------|----------|
| Live reload of an edited region                            | Game dies the moment WAD/STB are rewritten under it (2026-09-17 test) |
| New regions visible in an existing save                    | Saves cache the region table (region 146 test) |
| Spawners in the childhood prologue                         | Disabling quest until the Guild (adult save test) |
| Loose `.tng` as a modding delivery                          | Engine loads the WAD; loose files are only the editor's working copies |
| Engine-quality nav for freshly sculpted terrain             | Walkable paint patches the retail tree per cell; wholesale regeneration is the open "nav frontier" |

---

## 1b. Decision (2026-09-17): Atlas becomes FableForge -- LANDED 2026-09-18

The user's call: *Albion Atlas absorbs FableForge* â€” the Atlas repo is the product and takes
the FableForge name; the 1.0 is the **level/world/content editor** described here; the
quest/script tooling (FQT byte-exact compilers, quest cards, node editor, ForgeFSE Lua)
joins as a 1.x tab, not in 1.0. Consequences, folded into the milestones below:

- **0.14 also does the merge groundwork**: un-vendor `forgecore` â€” it moves in as
  `libs/forgecore` (its own CMake target, history imported with `git subtree` or a squash
  commit that names the source commit); `tools/sync_forgecore.py` and `vendor/VENDORED.md`
  retire; the one local override (`src/lzo.cpp` -> clean-room `lzo1x`) becomes the library's
  default (drops the GPL minilzo path entirely, which the public repo needs anyway).
  FableForge's 100 uncommitted working-tree files must be triaged *before* the import
  (commit what the user wants kept there, on a branch, in the FableForge repo).
- The `forge` CLI (`apps/forge`) is not merged into the GUI now; its level-side commands are
  already superseded by `AlbionAtlas` CLI commands. It moves over as `tools/forge-cli` so
  the quest commands keep working from one checkout; the half-built `apps/forge-gui`
  (ax::NodeEditor canvas) is archived on a branch for the 1.x quest tab.
- **Naming**: product "FableForge", the app executable `FableForge.exe`, the exporter tab
  keeps the name *Atlas* (people know the zip by it); CLI `forge` = today's `AlbionAtlas`
  commands + the quest commands. Rename lands at 0.15 so 0.14's portability work is not
  entangled with a rename diff.
- Memory/docs to update when this executes: the *Albion Atlas* and *FableForge Creation-Kit
  toolchain* memory notes, FableForge's `docs/`, and the CLAUDE.md sibling-repo line.

**Landed 2026-09-18 (2):** the legacy `forge` CLI (10.7k lines, 133 commands: quests, mods, saves,
defs, scripts, UI, STB tooling) builds here as `forge-tools.exe` from `tools/forge-cli/main.cpp`
against `libs/forgecore` unchanged, with `docs/re_reference/` (schemas, manifests) copied over;
its families fold into `forge` as they get a GUI. Not carried: the legacy `apps/forge-gui`
(node canvas), its tests, the 13 MB `fse_native_overlay.json` (stays in the legacy repo).

**Landed 2026-09-18:** product, exe and settings rename (`FableForge.exe` GUI, `forge.exe`
CLI, `%APPDATA%\FableForge` with a one-time copy of the old `AlbionAtlas` folder, presets
read both header tags, `.atlas-orig` / `.atlas-created` / the live-link hook tag kept for
existing installs); forgecore un-vendored into `libs/forgecore` (canonical; `tools/sync_forgecore.py`
retired); the repo folder is `D:\Code\FableForge`, the old repo `D:\Code\FableForge-legacy`
(its GUI canvas / quest tooling / ~100 uncommitted files are merged from there piece by piece).

## 2. Process: milestones to 1.0

Each milestone = a tagged release with the zip in `dist/`, `tools/check_all.py` green, and
the in-game probes it touches re-run (documented in the release notes). Order chosen so the
**portability blocker lands first** (nothing else matters to a stranger if textures need
Python), then safety, then depth.

### 0.14 â€” "Runs anywhere" (portability)
1. ~~**Native texture importer** replacing the Python shell-out.~~ DONE 2026-09-17
   (forgecore `texturewrite`, upstream `b7ea3c8`; validated against the Python
   reference and in-game). Remaining text kept for the record. Pieces already in tree:
   `src/dxt1.hpp` (DXT1 encode), `src/lzo1x.cpp` (LZO1X encoder incl. the engine-safe
   M2/M3/M4 rule), `forge::big` (reader with `Entry::data` writer field). To write:
   - DXT3 encoder (minimaps use DXT3; ~80 lines next to dxt1),
   - Fable texture payload = 34-byte Info + chunked-LZO mip 0 (with the `[0xFFFF][u32]` escape
     when a raw chunk >= 64 KiB â€” the rule found 2026-09-17, port of `lionhead_lz_compress.py`)
     + raw mips (`texture_build.py:311-464` is the spec),
   - GBANK_MAIN_PC append/replace writer (port of `bigb_write.py` entry rewrite, 700 lines of
     Python but the TOC logic is small).
   Upstream it to FableForge's forgecore first (`terraintex.cpp` `importPng`), then sync the
   vendor copy â€” same rule as every forgecore fix today.
   Verification: byte-compare our payload against `texture_build.py` output for the same PNG
   (PSNR=inf on the DXT blocks is not required; the Info header, chunk framing and TOC must be
   identical), then the existing `custom_theme_deploy.txt` + `atlas_check.png` in-game probe.
2. ~~**Install detection & first-run**~~ DONE 2026-09-17 (Setup panel, GOG/Steam candidates on C..H).
3. **CI + public repo** (workflow committed 2026-09-17; the public remote is the user's call): GitHub Actions building with the same MinGW/CMake profile and
   running `check_all.py --no-install` subset (unit, LZO cross-check, UI scripts that don't
   need an install â€” `noinstall.txt` exists). Retail-dependent tests stay local.

### 0.15 â€” "Can't hurt you" (safety + UX honesty)
1. ~~**Backup manager**~~ DONE 2026-09-17 (`src/backups`, CLI `backups`/`restore`, Setup panel). Was: one place listing every `.atlas-orig` (Levels, CompiledDefs,
   textures.big, FSE/PartyMode.lua, loose LEV/TNG), with *Restore everything to retail* and
   per-file restore, refusing while the game runs. Reuse `backupOnce` sites (`src/worldedit.cpp:44`,
   `src/leveledit.cpp:452`) by routing them through one registry (`src/backups.{hpp,cpp}`)
   that records what was backed up and when.
2. ~~**Engine-rule prompts at the point of action**~~ DONE 2026-09-18 (`raiseRule`/`drawRuleNotice`
   under the Place / spawner / new-level buttons, "Got it" per session). Was: (not the log): new region -> "start a new
   game"; placing a creature/spawner -> "existing saves won't show it; spawners need an
   adult hero"; deploy while game runs -> already refused.
3. ~~**Undo coverage**~~ DONE 2026-09-18 (palette in `TerrainState`; World tab undo/redo stack).
   Was: palette add (`Document::addGroundTheme`) and region edits are outside
   the undo stack today; put them in (snapshot the LEV palette; World tab already has Revert).
4. ~~**Loose-file honesty**~~ DONE 2026-09-18. Was: rename "Save .tng (loose file)" to "Save draft" and make *Write
   into FinalAlbion.wad* the primary action, since the engine never reads the loose copy.

### 0.15b â€” "Feels like a tool" (UI/UX polish; runs alongside 0.15 and 1.0-rc)

The bones are right (dark theme, purple accent, 3-pane, `theme::S` DPI scaling, Unreal
camera + QWER). What a stranger hits is *density and feedback*:

1. ~~**Right panel structure**~~ DONE 2026-09-18: sub-tabs *Objects | Terrain | Actors | Level*
   under the Tool card (`editTab_`, remembered in settings.json; Terrain <-> terrain tool
   follow each other; actions open the tab that owns their card). Paint lives inside Terrain
   (the brush card), so no separate Paint tab. Was: one long scroll of cards.
2. ~~**Feedback**~~ DONE 2026-09-18: toasts (every warn/error/success log line, 6 s, viewport
   top-right); the long jobs report their stage through `editor::ProgressFn`
   (`NewLevelRequest::progress`, `BlankLevelRequest::progress`, `applyWorldEdits(...,
   progress)`, `Document::deployTerrain(..., progress)`) and the busy button reads
   `Installing: baking the minimap  (12 s)`; the header status turns amber `writes -> <root>`
   when the save root is redirected. Not done: a real progress *bar* (the stages have no
   fixed weights; the label + elapsed time is the honest version).
3. **Viewport overlays**: DONE 2026-09-18 -- *Grid* chip (LEV cell lines, heavier every 8 =
   one terrain patch, screen-space width, per-cell lines fade out when a cell is under ~6 px),
   map-local `x y h` under the cursor (bottom-left pill; state `cursor_ground`), compass
   (Fable north) bottom-right; brush radius ring existed. Still open: falloff ring, selection
   bounds, the picture-in-picture minimap with the camera frustum.
4. **Pickers with pictures**: theme picker swatches DONE 2026-09-18 (the palette combo, the
   game-theme search list and a "painting with this theme" row show the base albedo, 64x64
   box-filtered from the texture cache, `Renderer::swatch`); *Add an object* mesh thumbnails DONE
   2026-09-18 (`Renderer::thumbnail`: the def's mesh rendered once into a 96 px target from a
   three-quarter view, one decode per frame for the visible rows, cached by def). Texture-tab
   thumbnails DONE 2026-10-02: visible rows decode from one open textures.big within ~4 ms a
   frame, cached by entry name (32 px, aspect kept, 768 at most), dropped and redone when
   the file changes (`tests/ui/texture_thumbs.txt`).
5. **Drag & drop**: PNG/JPG/TGA onto the window = custom ground texture (DONE 2026-09-18:
   lands in the Terrain tab's custom-texture input with a `GROUND_<file>` name, auto `drop
   <file>`); `.lev` already opened as a loose map. GLB onto the viewport = import model: DONE
   2026-10-02 (`.glb`/`.gltf`/`.obj` fill the Models import form with an `OBJECT_<FILE>` name;
   the user still checks the destination and imports; `tests/ui/drop_model.txt`).
6. **Discoverability**: ~~a `?` shortcut cheat-sheet overlay~~ DONE 2026-09-18 (`?` / F1 /
   header button; camera, objects, terrain, World, everywhere), empty-state hints exist
   ("Pick a map on the left", "Nothing selected", "This map has no .lev"), ~~first-run
   tour~~ DONE 2026-09-18 (three callouts after the first Setup: maps, tabs, viewport).
7. ~~**Consistency pass**~~ DONE 2026-09-18: one `App::confirmRow` (amber question, *Yes, ...* /
   *Cancel*) for the WAD write, terrain write, world apply and the backup restore; every
   button that touches the game reads "... into the game" / "Write into FinalAlbion.wad"
   and is the primary of its card; each confirm names the files and the backup.
8. ~~**Accessibility/perf of the UI**~~ DONE 2026-09-18: *Interface > Text size* slider
   (80..150 %, on top of DPI + window size, `settings.uiScale`), blocked cells in the
   walkable view are orange AND diagonally striped (reads without the hue), the objects
   list was already `ImGuiListClipper`-virtualised.

Verification: each item gets a `tests/ui/*.txt` script asserting widgets + a screenshot the
user reviews (the existing `screenshot` command), plus the DPI sweep 1024x700..2560x1440
that the theme pass already used.

### 0.16 â€” "Depth" (level design)
1. **Foliage brush**: scatter grass/trees into the local-detail tree. Writer for type-1
   repeated-mesh instances and type-0 meshes using the grammar in `src/stbrelocate.cpp`
   (`groupBody`) and forgecore's `encodeGroupContents` (`stbbake.cpp:1149`); bounds/spheres
   via the same helpers `zBox/zSphere` use. Preview via `foliageexport`.
   *Scoped 2026-09-18, not started:* forgecore already authors a whole local-detail section
   from placements (`buildType0LocalDetailSection`, type-1 grass proven in-game on
   ForgeTest64; type-0 trees are the open "type-0 strategy"), but only for authored 64x64
   chunks. A brush on a retail map needs (a) decode the chunk's existing local-detail
   groups into placements (`stbrelocate.cpp` walks them; a full decode is the gap),
   (b) add the painted ones, (c) re-encode the section for the map's real size and splice
   it into the chunk on deploy (`Document::deployTerrain` re-bakes heights/layers but keeps
   the section verbatim). Each step is byte-checkable offline (re-encode retail = retail),
   but the result needs the in-game harness before it ships -- do it with the user present.
2. ~~**Presets**~~ DONE 2026-09-18: `src/presets` (a preset is a valid loose .tng with a
   two-line header; shipped in `presets/` next to the exe, yours in `%APPDATA%\AlbionAtlas\presets`),
   Actors-tab card (click to place at the view centre as a selected group; *Save N selected
   objects as a preset*), 4 retail-derived presets built by `tools/build_presets.py` (bandit
   camp fire, Oakvale fence + gate, cottage furniture, graveyard corner; Objects only --
   creatures/villagers need the placer's world-space InitialPos, so a camp's spawner is added
   from the spawner card).
3. **Region entrances / map travel**: `src/gtg` DONE 2026-09-18 -- `FinalAlbion.gtg` is one
   TNG-shaped section per WLD map slot (`NEWMAP n` ... `ENDMAP`, CRLF, 151 of 399 slots
   present in retail; parsed and re-serialised byte-exact). An own-region level now gets a
   REGION_ENTRANCE_POINT + `<Level>HSP` start at its centre on install; the Level tab's
   *Region entrance* card and `AlbionAtlas entrance <map> [x y [z]]` show / set / move it.
   *(still untested in-game: whether the map screen needs anything beyond the entrance thing
   + the region's minimap -- verify with the harness on a fresh game.)*
4. ~~**Multi-select + copy/paste**~~ DONE 2026-09-18: Ctrl+click (viewport / objects list)
   toggles; the gizmo and nudges move the set as a rigid group about the primary; Del /
   Ctrl+D / Ctrl+C / Ctrl+V act on the set, each one undo step (`Document::beginBatch`,
   `extract` / `paste` fragments with positions relative to their centroid, pasted at the
   view centre on the ground). The fragment is also the preset format for #2.

### 0.17 â€” "Content" (meshes, creatures, textures, particles, Blender)

2026-09-29 particle follow-up: reviewed EgoCore `55bdc10`'s new simulator and
renderer. Forge now respects disabled systems/components in static proxies;
all 1165 retail effects still parse. Animated preview remains open. See
[upstream review and integration boundary](EGOCORE_PARTICLES_20260929.md).

Everything here already exists as **decoded formats and Python/Blender tooling in the FableTLC
repo**; the work is bringing it into Atlas so a modder never sees Python. Sizing is honest:
this is the biggest milestone and the one with real unknowns.

| Piece | What exists (verified) | What Atlas needs | Size |
|-------|------------------------|------------------|------|
| **Custom static meshes** (props, buildings) | Compiled-mesh grammar round-trips; `compose_mesh` builds type-1 (static) meshes from arbitrary geometry; `big_write.rebuild(adds=)` appends to graphics.big (`FableTLC docs/formats/MESH.md`, `tools/blender_addon/io_scene_fable/mesh_rw.py`). EgoCore `MeshCompiler.h`/`GltfMeshImporter.h` is the C++ oracle. Atlas already *reads* meshes (`thingsexport`, vendored `meshpreview`). | **CLI DONE 2026-09-20:** `forge mesh-import <model.glb\|.gltf\|.obj> <NAME> [--texture png \| --texture-id n] [--donor OBJECT_...]` -- `forge::meshcompose::composeStatic` (the `compose_mesh` static grammar in C++: one CStaticBlock per primitive, float layout 0x14/20, chunked LZO, the sentinel material, EgoCore's Info blob) + loaders for glTF 2.0 (glb / gltf, node transforms, core position/normal/UV component encodings) and OBJ (usemtl groups) in `src/meshimport` + the append to `MBANK_ALLMESHES` (next id) + `<NAME>_DIFFUSE` into textures.big + an `OBJECT_<NAME>` def (donor copy, `Graphic.modelId` repointed, MeshHeight/Radius from the bounds). Every composed payload is decoded back through `meshpreview` before it is written; `forge-tools mesh-info` inspects an entry. Unit test (compose -> decode) + `tools/test_meshimport.py` (cube .obj and .glb into scratch banks). The editor card *Import model* (Objects sub-tab, off the UI thread, reloads the def/texture context and the thumbnail bank; `tests/ui/meshimport.txt`) DONE the same day. The physics hull DONE the same day (`composePhysics`: EgoCore's `3DMF` type-3 entry from the model's triangles, `PhysicsIndex` wired; retail's `MESH_X[PHYSICS]` pairing confirmed with `mesh-info`); mesh space is centimetres (retail barrel z 0..132), the importer scales metres x100. The hull carries retail's chunk set (TRIS, SMTH, VERT, UNIV, MTLE; formats decoded from `MESH_OBJECT_BARREL[PHYSICS]` with `mesh-info --raw`) with the triangles in the engine's winding (straight order = inside-out hull: the hero walked in and was held at the centre -- three runs, positions logged). **IN-GAME VERIFIED 2026-09-20 evening:** the cube renders lit with its texture where placed, the retail barrel control is solid, cube 4 (flipped winding) stops the hero (user-confirmed). Left on the install: 4 probe cubes + a barrel in StartOakValeWest and their mesh/def/texture entries (`release_probes.py --stage mesh_undo` puts the files back). | L |
| **Custom creatures / NPCs** | The whole chain was proven in July: skinned type-5 mesh (`clone_skeleton` + `encode_skin` against a donor skeleton), DXT texture, `CCreatureDef.Graphic.modelId`, crc0 names, self back-ref retargeting (`docs/DEF_LOAD_CONTRACT.md`, memory *custom-npc-pipeline*). ElevenLabs dialogue + lipsync (`tools/anim_build.py`, `lipsync_build.py`). | "New creature from a donor" card: clone `CREATURE_*` def (+ appearance/body refs retargeted), optional custom body mesh (skinned import via the mesh composer, donor skeleton), optional texture; then `place CREATURE_MY_NPC` already works. Voiced dialogue stays Blender/CLI-side for 1.x. | L |
| **Texture editing** (objects, creatures, UI) | 0.14's native importer covers PNG -> DXT1/DXT3 + Info + chunked LZO + GBANK append/replace; `texture_extract.py` covers the reverse (DXT decode already in Atlas `dxt1.hpp` + vendored `parse_texture` logic). | ~~A **Textures** tab~~ DONE 2026-09-18: `src/texturebrowse` + `gui/textures.cpp` (4th panel tab: browse all banks, search, preview, *Export PNG*, *Replace from image* (slot size + format kept, validated), *Add a texture*; the selected object's textures from its mesh parts on top); CLI `textures` / `texture-export` / `texture-replace` / `texture-add`; `tools/test_textures.py` round-trips a barrel (mean diff 0.02). In-game: not yet (a retextured barrel screenshot is the user's probe). | M |
| **Particles / effects** | `effects.big` grammar decoded; Atlas `src/effects.cpp` *reads* it (1165/1165); EgoCore `ParticleCompiler.h` is the exact writer; `ParticleTypeName` in the TNG is plain text. | Placement already works (`PARTICLE_EMITTER_PLACEABLE`, opt-in preview). ~~Add a picker of effect names when placing an emitter~~ DONE 2026-09-18 (Actors tab *Particle effect* card: search the 1,165 effects.big names, `Document::placeEmitter` writes the retail CTCDParticleEmitter block; auto `place_emitter <FX> [script]`), and later an **Effect editor** (edit colour/size/rate fields of a cloned effect, append to effects.big via the shared big writer). | S (picker) / M (editor) |
| **Blender integration** | `tools/blender_addon/io_scene_fable` (import/export meshes, skin, bones; headless tests pass). Atlas exports GLB/OBJ of whole maps. | Ship the addon in the Atlas zip (it is MIT, same author); Atlas's "Import model" accepts the GLB the addon exports; document the loop *Atlas export -> Blender -> addon export -> Atlas import*. Later: a "Send to Blender / Bring back" pair that shells to the user's Blender for a selected thing. *Checked 2026-09-18: not a plain copy -- `io_scene_fable` imports FableTLC's `tools/` Python (parse_bigb, the LZ decoders...) via `sys.path`; it has to be made self-contained (vendor those modules into the addon package) before it can ship.* | M (bundle, after the untangling) / M (round trip UX) |
| **Custom animations** | `ANIM.md` Â§10: 3DAF writer + lipsync writer proven. | 1.x: attach a custom idle/anim to a placed creature through its def (`AnimationSet`); needs an in-game probe first. | L, *(untested in Atlas)* |

Order inside the milestone: textures tab (reuses 0.14) -> static mesh import -> creature clone
card -> effect picker. Each step gets the same treatment as everything else: scratch-install
script + one in-game probe (a custom barrel mesh standing in Greatwood, a custom NPC found by
`--things`, a retextured object visible in a screenshot).

### 0.18 â€” "Water" (post-1.0; RE complete 2026-09-19, see `FableTLC docs/engine/WATER_RE.md`)

How retail does it (from the debug build): water is **painted as depth themes** (`WaterType`
1 lake / 2 river / 3-5 sea / 8 ice, `WaterHeight` ladder 0..16; surface = ground + Î£ blend Â·
WaterHeight, `CEngineMap::PeekWaterHeight`) and the **visible surface is a baked STB mesh**
(`CWaterPatchMesh::Save` in the foreground frame after `hasWater`, a background sub-patch in
the patch trailer) that retail only loads. Gameplay water follows the paint alone; our deploy
writes `hasWater = 0` for regenerated patches, so a painted lake is invisible until (2).

1. **Water brush** (Terrain tab): surface altitude + body family (lake / river / sea / ice);
   each cell with ground < altitude gets a two-theme mix from the family's depth ladder so
   Î£ blend Â· WaterHeight = altitude âˆ’ ground, the ground theme kept in the third slot.
   Preview = the exporter's existing water surface (same formula). Ships gameplay water.
2. **Foreground writer**: `CWaterPatchMesh::Save` per touched patch (289 x 66 B, layout and
   every constant known; shore arrays zeroed, `distToShore` large). Gate: retail patches must
   round-trip byte-exact through the same encoder first. Expected in-game: the surface within
   the foreground radius, no foam.
3. **Background sub-patch + edge strips** (RE `CWaterGenerator::BuildStaticMapBackgroundBuffers`,
   the 0x38 `CTVertexWaterBackground`, `TesselateEdge`), then the shore generator (foam),
   then sea bodies (`__ENGINE_SEA_STATIC_MAP_BANK_FILE__*`; a lake never needs one).

Fishing spots are done (2026-09-19, Actors card + `place_fishing_spot`): a `MARKER_FISHING_SPOT`
thing with an optional `CTCContainerRewardHero` first catch -- no water dependency.

### 0.19 â€” "World in 3D" (post-1.0; planned 2026-09-19, not started, not the focus)

The World tab keeps its 2D grid as the precise editing surface (snap, overlap refusal,
pending moves); this adds a **3D view of a region and its neighbours** in the same viewport
the map editor uses (fly/orbit camera, gizmos, chips), so a modder sees how their level
sits against the world before moving it.

1. Renderer: many terrains instead of one -- a `std::vector<TerrainDraw>` (VB/IB/albedo per
   map, world offset from `forge world`) drawn with the existing shaders; `upload(scene)`
   becomes `uploadTerrain(mapKey, scene, worldXY)` / `clearTerrains()`. Foliage/things layers
   stay per selected map at first. *S-M.*
2. Scope by the engine's own rule: the selected map's region + every map it *sees*
   (`WLD SeesMap`, `world --regions`), baked with `texelsPerCell = 2` (a 128x224 map bakes
   in ~0.1 s at 4; the region set is 10-20 maps, so a few seconds, async like the preview;
   height-only meshes for the rest of the world as a grey horizon, optional). *M.*
3. World tab toggle *2D | 3D*; in 3D the selected map is outlined, neighbours faded, the
   engine's 8192 grid drawn on the ground plane; click selects a map, wheel/drag as today.
   Moving a map in 3D reuses the 2D move queue (drag on the ground plane = same snap/refusal
   path), so nothing new is written. *M.*
4. Later: seam-stitch preview across the touching edge in 3D, water/sea disc, and the
   overworld minimap tiles as the ground texture at far zoom. *Untested/unsized.*

Evidence needed before shipping: memory at 20 textured terrains (a 2-texel albedo is
~0.5 MB per map, fine), and that the world offsets match the in-game placement (the
`retail smoke` exports with `--world` already line maps up in Blender, so this is proven).

### 0.20 â€” "Mod packs v1: stack, order, install, uninstall" (post-1.0; researched 2026-09-19, M)

Corpus for testing: `work/nexus_mods/CATALOGUE.md` (2026-09-19; 19 small mods here, the GB-size
ones in `D:/Downloads`: Aeon Edition, Lost Content, Project Seasons, AlbionSecrets Modpack, Expanded
Chapters, Dragon Cliff). What a Fable mod *is*, by archive contents: `.fmp` (dominant), loose
game-root trees, bsdiff `.patch`, `.qst`, EgoCore `Mods/<Name>/` folders, whole-file GB packs.

Investigation backlog (2026-10-02): [fine-grained content overrides](modding/CONTENT_OVERRIDES.md)
for large mod stacks: compare baseline/contributors/result, select components,
records/assets and supported fields, persist compatibility choices, and validate
dependencies across FMPs and overhauls. Includes the historic FMP corpus as fixtures;
design proposal, not shipped functionality.

2026-10-03 reference correction: Controller Support's named FMP game links now
resolve against final composed identities and follow the selected field source.
Independent retail/Aeon/composed audits check all 14 references with zero
mismatches; the old raw importer had 12 wrong. Unsupported nested/multi-target
or asset metadata refuses before definition output is written. F2 Melee's
definition package remains unsupported; its earlier added-record counts did
not prove correct linking. See [FMP format and limits](modding/FMP_FORMAT.md).
Runtime compatibility remains unverified.

2026-10-04 named historic FMP audit: the original 1,142-package archive has
746 packages with nonempty banks on the current importer allowlist. An offline
apply sweep against the installed TLC data accepts 117 non-texture packages,
refuses 450, and reserves 169 texture-capable packages for a large-bank check;
387 need other bank types, and nine have no recognized entries. A verified
`Vector_VCDefString__` names-offset linker added 16 accepted spell packages;
a verified `CThingComponentSet` name-offset linker added No Quiver.
The real Berserk Scale/Small Berserk pair composes in both orders with one
reported `BerserkScale` conflict and different output banks. The Oak Vale bed
package composes a WAD TNG entry with five provenance UIDs. See
`docs/modding/FMP_FORMAT.md` for the commands and limits. In-game checks remain
open, as do nested creature/weapon links and remaining asset bank
integration. The `maintextures` entry composer now replaces/adds texture entries
by stable id, preserves disjoint FMP edits, reports same-entry conflicts and
whole-file masking, and surfaces conflicts in the Mods UI. The named texture
metadata audit finds 165/169 identity-compatible packages (838 replacement and
34 added entries); four collide with retail ids. Direct, disjoint, overlap in
both orders, whole-file ordering and refusal scratch tests pass. Runtime and
combined EgoCore resource ordering remain unverified/open; the overlapping
earlier-EgoCore/later-FMP case now refuses before stage rather than reversing
the requested winner. Per-entry FMP texture winners can now be saved in the Mods
conflict list (or supplied to `mods merge --picks`); an existing install-baseline
entry or absence of a newly added entry can be selected with `vanilla`. Picks
survive reordering, with an unavailable contributor reported and load order used.
The graphics entry composer now provides the same guarded replacement/addition,
whole-file masking, conflict report and saved per-mesh winner behavior for
`graphics.big/MBANK_ALLMESHES`. The named archive audit found 261/314 packages
with compatible mesh identities, 186 also having available texture names and
compatible texture identities. Direct and layered scratch tests pass; texture/mesh
reference remapping for collided ids, broader corpus builds, and in-game
viewport checks remain open. See `docs/modding/FMP_FORMAT.md`.

2026-10-04 named link progress: the read-only inspector now retains multi-target
groups; all 897 archive packages carrying game/object link metadata decode, but
the writer still refuses fields whose multi-target byte layout is unknown.
`CEngineGraphic.Model ID` now resolves to a named mesh in the planned graphics
bank. A scratch merge of the real Huge Mushroom FMP independently verified seven
new objects against seven exact output meshes with no introduced missing assets.
The Huge Mushroom editor probe then deployed the archived FMP through the Mods
panel, found its textured mesh in Models, placed its object in a scratch map,
and showed the selected geometry in the viewport. Undeploy removed the model
and restored original banks byte for byte. Gameplay and the other archived
packages remain unverified. See `tools/test_fmp_named_mesh_refs.py`,
`tools/test_fmp_named_object_ui.py` and the format notes for audit limits.
The Knothole Glade Children FMP now preserves its two ordered `MaleTeacher`
targets and resolves its nested creature weapon, brain, torso texture, body/eye
mesh and combat links. It composes offline with four applied definitions and
three additions; independent output checks match 18 creature references plus
the village pair, child entries and marker mesh. The scratch GUI deploy probe
places both new children and sees their selected skinned LOD0 meshes in the
viewport; undeploy restores the baseline banks. This exposed and fixed the
editor's previous `creatures=false` preview setting. Retail creature placement
and the editor workflow still pass. Gameplay, animation, other creature layouts
and asset winner picks need separate coverage.

What already exists (verified): the legacy CLI family in `forge-tools` -- `forge mods merge
<base> <out> --with <src>... [--fields] [--stage]` (sources = game-root dirs, `.fmp`, bsdiff
`.patch`, in argument order = load order; field-level `game.bin` merge, loose-TNG thing merge by
UID, `.qst` statement union; `tools/forge-cli/main.cpp:8007-8130`), `forge mods analyze`
(cross-mod def conflicts, :4637), `forge fmp list/apply/extract/export`, `forge tng
conflicts/merge`, `forge qst merge --picks`, and `forge stage/unstage` (`libs/forgecore/src/stage.cpp`:
copies an overlay in, `.forgebak` originals, `forge_stage_manifest.json`). The design is in
`docs/modding/MOD_PACKS.md` / `LOAD_ORDER.md` / `FMP_FORMAT.md` (ported from the legacy repo).
FableTLC's Oakvale Reborn installer is a *second* layer on top of a stage (`.ovrbak`, hashed
receipt), and the New Oakvale playtest is a sidecar-DLL bundle that replaces nothing.

The gaps: **one pack at a time** (`stage.cpp:35` refuses a second stage); no pack identity
(no name/version/hashes in the manifest, no persisted load order); three backup conventions
(`.forgebak`, `.atlas-orig`, `.ovrbak`) that don't know each other; merge does not cover
WAD-resident TNG/LEV, `text.big`, `names.bin` link fixups for `.fmp`, `FSE/quests.lua`
unions, textures/STB; conflict reports are per-family, not over a whole order; ForgeFSE loads
one `quests.lua` per DLL. None of the family is in `docs/CLI.md`.

1. ~~`modpack.json`~~ folded into 2: each order entry carries name, kind, source, sha256, note. *S*
2. ~~`forge mods list/add/remove/order` persisting `<root>/forge_mods.json`; `forge mods build`~~
   DONE 2026-09-20 (branch `modpacks`): `libs/forgecore/modorder` + `forge-tools mods
   list/add/remove/move/enable/disable/build`, `tools/test_mods.py` in check_all. The corpus
   needed three merge fixes first: the Fable Explorer `.fmp` dialect (contentType 459), partial
   game-root trees as TNG sources, and bsdiff against the pristine bytes (this install's game.bin
   is a record-identical re-save; `<file>.retail-bak` is tried when the result does not parse).
   Unofficial Patch + Special Melee + F2 Melee build into one game.bin + 8 TNGs. *S*
3. Deploy = rebuild from the order onto the retail baseline (`.forge-orig`), staged once;
   uninstall = drop from the order and rebuild; the three backup suffixes unified behind
   `albion::backups`. *M* -- `mods deploy` (revert the previous stage, rebuild, stage) and
   `mods undeploy` DONE 2026-09-20 on the `.forgebak` stage (round-trips byte-identical; empty
   folders pruned). Suffixes DONE 2026-09-20: new writes keep `.forge-orig` / `.forge-created`
   (`backups::backupOnce` / `markCreated`, one helper instead of six copies); the scanner reads
   the legacy `.atlas-*`, the stage's `.forgebak` and the overlay installer's `.ovrbak` with a kind
   per row; `restore` reverts a stage through its manifest first and rebases an original that
   was taken on top of a stage (this install's WAD/WLD/BWD/STB originals are that case).
   `tools/test_backups.py` in check_all.
4. `forge mods conflicts`: `modsAnalyze` + `tngConflicts` + `.qst` intersection over the whole
   order as one JSON; GUI per-row winner picker feeding `--picks`. *M* -- DONE 2026-09-20:
   `mods conflicts --json` is ONE report (sources, defs, tng, qst, text, files, wad, summary) with
   every stage's rows labelled by the order's mod names; agreeing edits are not conflicts; picks
   (`forge_mods_picks.txt`, namespaced keys, mod name or `vanilla`) drive every stage of
   build / deploy; the Mods tab's Conflicts card is the picker (`tests/ui/mods.txt`).
5. Merge coverage: `text.big` key union, WAD-resident TNG (extract -> thing-merge -> repack).
   *M* -- whole-file layers of tree sources (LEV / WLD / BWD / STB / INI / banks: last source
   wins, contested paths counted) DONE 2026-09-20; `text.big` key union DONE 2026-09-20; the merged
   loose FinalAlbion/*.lev|*.tng and root Levels/*.lev|*.tng are REPACKED into FinalAlbion.wad (replaced entries, new levels
   appended as native entries) DONE 2026-09-20 -- the GB packs (Project Seasons, AlbionSecrets) ship
   loose levels and rename the WAD to `_FinalAlbion.wad`, i.e. modders treat the WAD as winning, so
   the composer makes the WAD carry the merge whatever the precedence (`ENGINE_RULES` supported over
   `stage.hpp`'s comment; still to be seen in-game).
6. FSE Lua packs: union `quests.lua` / `FSE_Master.lua` per pack via `forge::questdeploy`,
   id-collision check; sidecar DLL stays the fallback. *S* -- `quests.lua` DONE 2026-09-20: a tree
   (or FSE-only folder, now a source) whose `FSE/quests.lua` differs from the install's contributes
   its entries by key with its own bytes (`QuestsLua::withEntry`); a key several mods define is a
   conflict (`fse:<key>` picks, the Conflicts card); ids shared across quests / entity scripts are
   reported as clashes (informational rows -- the mod needs a new id); the quests' Lua files ride
   as whole-file layers. `FSE_Master.lua` stays a whole-file layer (one Master per install).
7. Docs: the family into `docs/CLI.md`; a Mods tab in the GUI last. *S* -- the Mods tab DONE
   2026-09-20 (`gui/mods.cpp`: load order with add/remove/move/enable, Check conflicts / Build and
   deploy / Undeploy through `forge-tools.exe`; `tests/ui/mods.txt` runs inside `test_mods.py`).
8. **EgoCore mods and the older mods in one load order** -- **first cut DONE 2026-09-20** (branch
   `modpacks`, `libs/forgecore/egocore`): an `Mods/<Name>/` folder is a pack; `mods build` copies it
   under `<out>/Mods/`, writes EgoCore's own `[Mods]` line into `Mods.ini` (FSE_Launcher loads it;
   EgoCore's UI still lists it), and turns its `Data/Defs/*.def` text into a record layer: the
   blocks are merged into the retail text tree (EgoCore's MergeDefFile rule), the tree is compiled
   twice with `defc` (jamen/fable-defs, byte-deterministic; `FORGE_DEFC` / `FORGE_DEFS_TEXT`, the
   text tree is `FableTLC unified_build/UnifiedFable/Data/Defs`) -- base and mod, BOTH with the
   mod's added definitions so the two compiles share one index space -- and the fields that
   differ are applied onto retail's records (def-index fields compared by resolved name, added
   records re-pointed). Controller Support: 3 files, 4 blocks -> 14 records / 43 fields / 1 new,
   the merged control scheme has the mod's 71st binding; Water Wader: DLL only. In `test_mods.py`.
   TNG section-merge (`TngMerger.h`) DONE 2026-09-20: an EgoCore-style partial TNG mod (a
   `[Settings]` header with `Replace=true` / `DeleteUIDs:`, then only the things to add or replace --
   or simply far fewer things than the level) merges into the level even when it is the only editor
   instead of replacing it; DeleteUIDs are dropped after the merge; Replace=true copies whole.
   Still open: a shipped text tree (the text Data/Defs is this machine's path; `defc.exe` ships in
   the zip). `.resource` bank overrides DONE
   2026-09-20 (`egocore::applyResourceOverrides`, ModBankPatcher's layout incl. the graphics
   MipSize0 zeroing; replaced / appended entries; `test_mods.py` over frontend.big); the `.tmp`
   refusal DONE 2026-09-20. (user goal, 2026-09-19: *every* older
   mod, whatever its delivery shape -- a single `.fmp`, a loose-TNG/LEV mod, a bsdiff `.patch`, a
   Fable Explorer / ChocolateBox edit, a `.qst`, a whole-file GB pack). Evidence: the 2026 wave
   (Water Wader, Enable Sprint, Trample Vegetation, Controller Support ...) ships as
   `Mods/<Name>/<Name>.dll` [+ `Data/Defs/*.def` text defs, `<Name>_Info.txt`], and EgoCore
   (`C:/Users/Cornelio/Documents/EgoCoreInspect`, `EgoCore/Mods/ModManagerBackend.h`) is a
   *second mod manager*: `mods.ini` `[Mods]` lines `Name\Name.dll=1` with FSE as the "core mod",
   four kinds it detects (DLL / asset `.resource` bank overrides + loose assets / text `.def`
   merged then recompiled by its own compiler / TNG section-merged, `TngMerger.h`), and a deploy
   that restores "vanilla" from its own `.tmp` backups, patches banks, compiles defs, merges
   TNGs, launches. Two deployers on one install fight (its "vanilla" restore undoes a FableForge
   stage and vice versa), so the rule is **one composer, record-level layers**:
   - Every mod is normalised to the same intermediate form before ordering: layers of
     game.bin/names.bin records (field-level), TNG things by UID, `.qst` statements, text.big
     keys, bank entries, plus whole-file assets (LEV, WAD/STB payloads) that only load-order
     can resolve. `.fmp` and bsdiff `.patch` are applied to a retail copy and diffed back to
     layers (`modsMerge` already does the first half), so the Unofficial Patch's
     `game.bin.patch` (bsdiff over retail bytes) still lands underneath Aeon Edition's whole
     `game.bin` instead of failing on it; a Fable Explorer edit is just a game-root tree.
   - An EgoCore mod folder is one more pack type: the DLL stays in `Mods/<Name>/` and is
     registered by writing only the `[Mods]` lines of `mods.ini` (EgoCore's exact format, so
     EgoCore's own UI still shows it); its `.def` text is compiled with our def pipeline
     (EgoCore's `FableDefCompiler` is the oracle; the names.bin crc0 is already verified
     13593/13593) into the same record layer an `.fmp` becomes, so an EgoCore def mod and a
     2020 `.fmp` conflict-check and field-merge in one report; its TNGs go through our thing
     merge; `.resource` bank entries through our big writers.
   - EgoCore's own deploy is then never run (its dirty flags stay clean because we never touch
     its `.tmp` backups); the game is launched from Steam / FableForge. A documented rule, and
     a check that refuses to stage when EgoCore's backups show it has deployed.
   - GB packs DONE 2026-09-20 (`tools/test_gbpack.py`, Project Seasons under the UFP): the parked
     `_FinalAlbion.wad` and `userst.ini` are never layers, identical whole files are skipped
     (streamed compare), changed FinalAlbion/root levels are repacked into the real WAD (423 entries in the 2026-09-30 Project Seasons retest), ProjectAutumn/ +
     the whole STB / banks ride as whole-file layers; 24 s. AlbionSecrets Modpack has the same
     shape (zip peeked, not extracted).
   - Test matrix from the corpus (`work/nexus_mods/CATALOGUE.md`): Unofficial Patch + Aeon
     Edition (bsdiff vs whole game.bin); Ultimate Spell Pack + mfvicli Better* (18 + 35 fmps on
     the same spell defs -- the field-merge case); Connected Regions (397 loose LEV/TNG + WLD)
     + Project Seasons (whole WAD/STB) -- the whole-file load-order case; Water Wader +
     Controller Support (DLL + text defs + fmp) on top of all of it. *M*
9. **Mod content in the editor.** After `mods build` + stage, a map opens with the merged TNG,
   so things a mod placed are ordinary things: selectable, movable, deployable through the WAD
   writer. Provenance DONE 2026-09-20: `mods build` writes `forge_mods_provenance.json`
   (level -> `uid:<n>` -> mod; the thing merge's winner, the first mod for agreeing edits, every
   added/changed thing of a single-editor copy) which the stage carries next to `forge_mods.json`;
   the editor badges those things in *Objects in this map*, filters by *Placed by* (everyone /
   retail only / one mod), and *Back to retail* on a selected thing writes a vanilla pick for the
   next deploy (`tests/ui/mods.txt`). Provenance is by bytes: a ChocolateBox re-save (F2 Melee)
   touches every thing of its levels (float formatting, defaulted properties), so every one is
   badged -- a semantic thing signature needs the engine's property defaults (open). Still open:
   thumbnails for a mod's new `OBJECT_*` defs (needs its `graphics.big` entries), and the in-game
   check whether retail reads loose `Data/Levels/*.tng` at all (`ENGINE_RULES` says the WAD wins;
   Connected Regions ships 397 loose TNGs). *M*
Later (*L*): LOOT-style masterlist + topo sort, LEV grid merge.

### 1.0-rc â€” "Polish, docs, and a stranger's test"
1. Docs: ~~a 10-minute "first level" walkthrough with screenshots~~ DONE 2026-09-18
   (`docs/FIRST_LEVEL.md` + `docs/walkthrough/*.jpg`, harness-captured, shipped in the zip;
   ends with the *cannot yet* table), ~~the CLI reference generated from `main.cpp` usage text~~
   DONE (`tools/gen_cli_reference.py` -> `docs/CLI.md`, regenerated by `package.py`), ~~an
   "engine rules" FAQ page~~ DONE (`docs/ENGINE_RULES.md`, each rule with its evidence; shipped).
2. Performance: the albedo bakes (`terrainexport::buildScene`, both the STB-pass and the
   LEV-blend paths) run row-parallel since 2026-09-18 (byte-identical output; 128x224 map at
   32 texels/cell 6.9 s -> 3.5 s, at 16: 1.8 -> 1.1 s). Measured: the paint preview bakes
   at 4 texels/cell and was already under a second, so a dirty-patch re-bake is not worth
   its complexity now. `chunk-audit --all` audits the 399 maps on every core (report byte-identical,
   88 s -> 70 s only: the audit is allocation-bound on the CRT heap, so more threads buy little).
3. ~~Split `src/main.cpp` into `src/cli/*.cpp`~~ DONE 2026-09-18 (`common` / `levels` /
   `textures` / `install` / `world` / `chunks` / `export`, each family a
   `std::optional<int> runX(cmd, args)`; the duplicated backups block went; outputs diffed
   identical against the pre-split binary). The automation dispatcher is `gui/automation.cpp` too.
4. STB compaction: ~~an STB-level compaction that drops superseded payloads~~ DONE 2026-09-19
   (`forge compact-stb`, Setup panel button, `src/stbcompact`, unit test; 574.4 -> 571.2 MB
   on the install, payloads verified; needs one in-game run on a compacted bank). Still open,
   scoped 2026-09-19 (M, not started): a `chunk-compact` pass. `stbrelocate::run` already
   classifies every frame (foreground run, patch LOD file blocks, the local-detail section,
   and the "unreferenced" patches retail itself carries) and rewrites each family's absolute
   pointers for a *translation*; compaction is the same walk with a global re-lay: (1) measure
   = slot gaps + superseded foliage sections (`chunk-compact --dry-run <map>` first, there is
   no grown chunk in the retail install to measure), (2) re-lay fg frames in 2048-aligned
   slots, LOD blocks, then the LD section in `SaveFileBlock` order, patching the record's
   directory pointers exactly as the relocation does, (3) gate = `chunk-audit` clean +
   relocate-and-back digest identical + `compact(compact(x)) == compact(x)`; in-game with
   the user before it ships.
5. A "stranger's test": fresh Windows VM or a second PC, retail Steam install, the zip only â€”
   follow the walkthrough; every step that needs a workaround becomes a bug.

### 1.0
Follow `docs/RELEASE.md` (tag, zip, Discord post); `docs/PLAN.md` becomes `docs/ROADMAP.md`
(post-1.0 items) and the current PLAN's history moves to `docs/journal/`.

---

## 3. Verification (how each milestone is proven)

- **Offline**: `python tools/check_all.py` (unit, LZO vs minilzo and vs the engine's asm
  decoder, retail smoke, 8 UI scripts, new-level + overworld scratch suites). New features
  add a scratch-tree script under `tests/ui/` the way `theme_deploy.txt`, `custom_theme_deploy.txt`
  and `world.txt` do.
- **In-game** (user-driven or via `tools/ingame/ingame_terrain_test.py`): each milestone
  re-runs the live probes it touches (`*_live.txt` scripts) â€” childhood save for terrain/things,
  `--save-from Cornelio` for spawners, `--new-game` for new regions. Screenshots are the
  evidence; the user reviews them critically.
- **Portability**: the 0.14 importer is byte-compared against the Python reference on the
  same PNGs before the Python path is removed; the "stranger's test" in 1.0-rc is the final gate.
- **Restore discipline**: every probe ends with the backup manager's restore (or today's manual
  `.atlas-orig` copy-back) and a `cmp` against the backups â€” the mistake from this session
  (deleting the backups instead of moving them back) becomes impossible once restore is a
  button that only moves.

## 4. Critical files

- Texture import: `vendor/forgecore/src/terraintex.cpp` (`importPng`, `findTextureBuilder`),
  `src/dxt1.hpp`, `src/lzo1x.cpp`, `vendor/forgecore/include/forge/big.hpp`; reference
  `D:\Documents\FableTLC\tools\texture_build.py`, `lionhead_lz_compress.py`, `bigb_write.py`.
- Backups: `src/worldedit.cpp:44` and `src/leveledit.cpp:452` (`backupOnce`), `src/livelink.cpp:154`.
- Editor/UI: `gui/editor.cpp` (cards), `gui/app.cpp` (automation + state dump), `gui/world.cpp`.
- Foliage: `src/stbrelocate.cpp` (`groupBody`, `ldLayout`), `vendor/forgecore/src/stbbake.cpp:1149`.
- CLI split: `src/main.cpp`.
- Docs: `README.md`, `docs/EDITOR.md`, `docs/PLAN.md`, `docs/AUTOMATION.md`.
- Content pipeline references (FableTLC): `docs/formats/MESH.md`, `MESH_COMPOSE.md`,
  `BIG_WRITER.md`, `TEXTURE_WRITER.md`, `EFFECTS_FORMAT.md`, `DEF_LOAD_CONTRACT.md`,
  `ANIM.md`; tools `tools/blender_addon/io_scene_fable/{mesh_rw,fable_core}.py`,
  `tools/big_write.py`, `tools/texture_build.py`; C++ oracle EgoCore
  (`MeshCompiler.h`, `GltfMeshImporter.h`, `ParticleCompiler.h`, `BankEditor.h`).

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

## GUI definition clone continuation, 2026-10-10

The isolated development branch adds Clone selected definition, a bounded new-name
draft and Create confirmation around the proven paired publisher. Prior records,
identity slots and name offsets remain intact; references retain their original
targets. Opaque, numeric-named, unnamed and indexed duplicate-name donors are supported.
Whole captured source bank/name bytes, destination identity, pack validity and
compatible outputs are checked before publication. Shared definition draft state
protects Close and Install; the created record is reselected by explicit index.

Thirty-eight real-control GUI cases pass at both sizes across game/frontend/script
banks, folder/pack output, refusals, cancellation, stale sources and Restore.
All 34 neighboring scalar-draft cases and both core clone/BIN-writer targets pass.
Actual retail inputs through the new GUI preserve all 14,764 prior records and raw
identity rows; independent donor/key/ordinal checks and explicit-output Restore pass,
with native compiled-bank hashes unchanged. The development-version rebuild and
extracted package pass: 112 identities, 132 links, seven selected workflows,
120 not run and zero skips. Archive SHA256:
`73fb4dae506f4a387bc26bcf64e464a1276c29253e77f0aae6ad6cb92c5a9349`.
The packaged GUI also passes the independent retail clone/Restore check.
This is separate from RC2's unchanged archive; no full-suite or live-game claim.
Evidence and resume: [GUI clone handoff](HANDOFF_GUI_CLONE_2026-10-10.md).

### Partial-output texture context follow-up

A folder-clone regression reproduced `context_ready=0` after writing only compiled
definitions. Context loading now combines authored definitions with native textures
when output textures are absent. Existing corrupt banks, directories or status
errors do not fall back. Eight focused clone cases at both sizes, two scalar-folder
cases and the 14,764-record retail clone/Restore check pass, including a ready-context
assertion and unchanged input hashes. No texture files are copied or authored.
Exact extracted follow-up acceptance passes 112 identities, 132 links and four
selected workflows (including 44 clone/34 scalar cases), 123 not run, zero skips.
Archive SHA256: `9be422f1097e0cee629a5450de6e4a8e230217b93da711438b5d66d668d26e3b`.
The packaged retail GUI check also passes the ready-context requirement and Restore.
The initial clone checkpoint and RC2 archive remain intact. See the GUI clone
handoff for evidence; Unicode GUI wide-string authoring is next.

### GUI Unicode wide-string authoring, 2026-10-10

The development GUI edits complete valid CWideString originals and drafts up to
127 UTF-8 bytes using the proven strict native UTF-16LE converter. Supplementary
input is preserved through a shared 32-bit ImGui character ABI. C0/C1 controls,
malformed surrogates/UTF-8, overlong originals and incomplete/opaque-tailed strings
refuse. Narrow strings and clone names remain ASCII-only. Exact no-ops keep original
bytes; transactional publication and all existing draft/write guards remain.
Five targeted CTests, 42 GUI wide cases, 24 narrow neighbors and the both-size
14,764-record retail Unicode delta/Restore check pass. The final control tightening
passes targeted reruns. Exact extracted acceptance passes 113 identities, 132 links,
nine selected workflows, 119 not run and zero skips. Archive SHA256:
`ed71aa9bf5ee94ad2d155b24de6fdc71c7af4e457531a4950ec92c53294cd74a`.
The packaged retail GUI check preserves all identities, unrelated payloads and names,
and passes Restore with native hashes unchanged. Broader font
coverage remains open: Latin/Greek render, but inspected CJK/emoji use replacement
glyphs despite correct stored bytes. See [Unicode handoff](HANDOFF_GUI_UNICODE_2026-10-10.md).

### System glyph fallback, 2026-10-10

Windows Fonts are located through the native directory API. Body/bold/title/small
keep their primary Segoe faces and merge available CJK/emoji fallback glyphs.
Immutable file bytes are shared across roles and scale rebuilds; no font is bundled
or installed. Six system/no-font/primary-only cases pass at both sizes across three
scale passes and all roles. CJK and monochrome emoji are now visible in inspected
native/synthetic drafts, superseding the prior replacement-glyph limitation when
the relevant system fonts are installed. Optional absence stays usable and explicit.
Unicode head `2d8abff` passes Windows CI run 38062819381.

Exact extracted archive passes 114 identities, 134 links and eight selected
workflows, 121 not run and zero skips; the font report has zero skipped glyph groups
on this machine. Archive SHA256:
`581cc3201f1382c21bf4c0895b09ae0c8df16d6c4e2217f04d2bbc77517bfe3c`.
This does not prove universal font coverage, text shaping, colour emoji, gameplay or
another-machine behavior. See [font handoff](HANDOFF_GUI_FONTS_2026-10-10.md).

### Confirmed GroupDef target picker, 2026-10-10

Decoded game-bank int32 GroupDef drafts now offer searched native-index choices
for confirmed Thing/terrain-theme families. Wrong-family, missing and stale target
identities refuse before publication; unresolved originals retain exact no-op bytes.
Other integers/banks remain uninterpreted and raw CLI editing is unchanged.
The current retail villager schema does not decode its GroupDef type, so it remains
read-only; no speculative schema change. All 32 synthetic cases and six retail-copy
Object/Building/theme cases pass, independently preserving 14,764 records except
the selected four-byte delta, exact identities/names and Restore; native inputs stay
unchanged. The dev skill selected affected tests; the format-I/O skill required the
independent raw-delta and untouched-byte checks.

Exact extracted archive passes 115 identities, 136 documentation links and eight
selected workflows (including 32 picker, six retail, 34 scalar and name/tree/group
neighbors), 120 not run and two unrelated missing-corpus skips. SHA256:
`cf6d013ef97fd8fe3fcf128f3dcebf8c156a3d272a32d852bed4750b8cc733dd`.
No full-suite, clean-machine or gameplay claim. See
[GroupDef handoff](HANDOFF_GROUP_PICKER_2026-10-10.md).

### Creature GroupDef schema recovery, 2026-10-10

The prior villager read-only limitation is now resolved for GroupDef only. Original
FableWin bytes, derived/base transfer bodies and PDB names confirm the inherited
signed int32 type. All 517 retail Creature records independently match unique
four-byte boundaries and decoded values. No neighboring unknown types change.
Eight real Object/Building/Creature/theme picker cases pass at both sizes with
exact unrelated records/identities/names, Restore and unchanged native hashes.
Both affected CTests pass; compact Creature picker inspected.

Separate archive SHA256:
`495961b1ab015cc6c6fab6c1508050184be1b9144fa715ad4c93eaad176effb7`.
Exact extracted acceptance passes 116 identities, 138 links and five selected
workflows (including the extracted-schema 517-record check, eight retail edits,
32 synthetic cases and tree/group neighbors), 124 not run and two unrelated
missing-corpus FMP skips. No full-suite, clean-machine or gameplay claim.
The debug-evidence skill required native/PDB confirmation before promotion;
format-I/O required independent boundaries and unchanged-byte/Restore checks.
See [Creature schema handoff](HANDOFF_CREATURE_GROUP_2026-10-10.md).

### Creature donor self-reference foundation, 2026-10-10

Canonical defrelocate now recognizes the verified Creature CDefs prefix, retaining
the same strict clean/encode checks and unresolved-candidate refusal. Independent
raw audit covers all 517 retail donors and 1,521 self slots: 509 relocate using only
those slots, eight remain refused for outside-prefix matches. Header/CRC, shared
references and every other payload byte stay exact; native compiled hashes unchanged.
Three affected relocation/namespace/FMP CTests pass. No clone publisher or GUI
feature is claimed for this foundation; generic clone still copies exact bytes.
Distinct self-retargeted cloning and own appearance/body references remain next.
Picker head b49dcf2 passes Windows CI 38065042651. See
[creature donor handoff](HANDOFF_CREATURE_CLONE_2026-10-10.md).

Distinct defedit::cloneCreature now precomputes strict self-only relocation before
canonical append. Shared entries, loaded pool and all prior slots remain exact;
unknown matches/empty listings refuse before mutation. Generic clone remains an
exact byte copy. Five affected CTests pass (inventory 56, not full suite), and the
independent retail container gate passes 508 appended donors plus nine refusals,
including exact identities, name crc0/ordinals, original raw pool and per-clone
deltas with unchanged native hashes. Paired publication/Restore and GUI promotion
remain next; own appearance/body cloning remains unfinished. No new package or
gameplay claim. Schema head d2678ed CI 38065569483 passes.

Paired `defs clone-creature` now requires an explicit schema and game-bank paths.
The publisher recomputes the expected self-only clone from fresh source before
using existing namespace, offline, transaction and recovery guards. Generic exact
cloning retains its donor-byte requirement. Thirty synthetic CLI cases, six native
input/owned output cases, five affected CTests and 35 generic CLI neighbors pass.
Native hashes stay unchanged; fresh/existing outputs independently verify all
14,764 prior records, raw identity rows/pool, appended crc0/ordinal and Restore.
Exact extracted checkpoint passes 117 identities, 141 links and six workflows,
including 44 unchanged GUI clone cases; 125 not run and two unrelated missing-corpus
skips. ZIP SHA256 `732db7d64d142e7f7f9138d26345aaf25c1a36d651269074bba8c43b4908fc92`.
Distinct Creature GUI action is next; own appearance/body cloning remains open.
No full-suite, clean-machine or gameplay claim. See the creature donor handoff.
