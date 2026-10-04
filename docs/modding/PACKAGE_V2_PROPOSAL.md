# Forge package v2 proposal

Status: design proposal, not an implemented or released package format. This
builds on `forge_pack.json` v1, the named historic FMP corpus, the Aeon 5.03
installation, and EgoCore's existing DLL loader. It should be tested against
the corpus before its schema is frozen.

## Goal and boundary

A Forge package should say **what** it changes and **what it needs**. Forge
should assemble all enabled packages against a selected baseline, explain the
winner for each contested change, assign final numeric IDs, and stage one
reversible output. The package is a portable ZIP with a proposed `.ffmod`
extension, a versioned `manifest.json`, and content addressed by relative
paths. The extension and schema remain provisional.

Aeon-style full `Data` distributions are **baseline profiles**. Forge installs
one such complete profile over a clean TLC copy, preserves all its banks and
unnamed records, and then applies compatible package layers. A profile can
declare its own version and file fingerprints. Forge must not infer that a
complete bank is a safe record delta merely because most named records parse.
The Aeon baseline can coexist with named FMPs and an EgoCore DLL, as the
Windows Aeon + Controller Support route demonstrates.

EgoCore remains responsible for loading runtime DLLs. Forge copies the
`Mods/<Name>/` folder, writes EgoCore's supported `Mods.ini` registration,
and composes the DLL's declared game-data resources with other layers. Forge
does not introduce a competing DLL ABI or ask EgoCore to decompile Aeon's WAD.

## Package structure

```text
ControllerSupport.ffmod
  manifest.json
  legacy/ControllerSupport.fmp
  runtime/FableControllerSupport/FableControllerSupport.dll
  hashes.json
```

The initial format may wrap an original FMP and EgoCore folder without
discarding either. Forge's importer converts the FMP to typed in-memory edits
at build time. Future authors can store native typed edits directly. The
original legacy file remains available for inspection and round-trip checks.

Illustrative manifest, subject to validation against real packages:

```json
{
  "format": "fableforge.package/2",
  "id": "aeon.controller-support",
  "name": "Controller Support for Aeon",
  "version": "1.0.0",
  "game": "fable-tlc",
  "requires": [
    {"id": "aeon-edition", "version": ">=5.03", "role": "baseline"},
    {"id": "egocore", "role": "runtime"}
  ],
  "loadAfter": ["aeon-edition"],
  "components": [
    {"kind": "legacy-fmp", "path": "legacy/ControllerSupport.fmp"},
    {"kind": "egocore-dll", "path": "runtime/FableControllerSupport",
     "entry": "FableControllerSupport.dll"}
  ]
}
```

`id` is a stable author-chosen identifier; display names can change. Versions,
game target, required baseline, runtime requirement, and component paths are
explicit. Package hashes identify exact input bytes. A required mod must be
enabled and precede its dependent; `loadAfter` is advisory ordering. A cycle
or missing required baseline is a build error shown in the Mods tab.

## Native edit model

Each operation has a typed target, expected base identity when needed, and an
explicit action (`set`, `add`, `remove`, or `replace`). Numeric runtime IDs are
output artifacts, not package identities.

| Family | Stable target | Merge unit |
|---|---|---|
| Compiled definitions | bank + record name/type + schema field | field when its layout is proved; otherwise whole record |
| Text | language + string symbol | named string; assign final TextID after load-order and picks |
| Meshes, textures, effects, audio | bank + entry name/type | named entry plus referenced metadata |
| Level things | map + UID | thing/field; a full level replacement must say so |
| Terrain, navigation, WAD files | map/chunk/file + baseline fingerprint | explicit patch or whole replacement |
| FSE scripts and quest registry | script path or quest key | script/file or registry entry |
| EgoCore runtime | mod folder + DLL name | one registered runtime component |

References use symbols such as `{ "bank": "text", "name": "TEXT_GUI_..." }`
or `{ "bank": "game.bin", "name": "OBJECT_..." }`. Forge allocates or
preserves the final ID **after** choosing winners, then rewrites only verified
reference slots. A missing target, incompatible field layout, changed unnamed
identity, or unresolved binary reference stops the build with the owning mod
and field named. Unknown bytes remain intact.

Conflicts are reported at the narrowest proven unit. Disjoint edits combine;
same-target differing edits use load order unless a saved pick names a winner.
Whole-file replacements visibly mask earlier entry edits. The report records
the contributing package, winning package, source hash, selected baseline,
and any unresolved or skipped operation. The viewport and native editors read
the same composed banks and provenance report, so users see installed changes
and their source rather than a separate preview state.

## Build and migration

1. Identify the clean TLC install and optional complete-data baseline profile.
2. Import each enabled FMP, Forge pack, loose tree, or EgoCore folder into
   typed layers without modifying the install.
3. Validate hashes, dependencies, baseline compatibility, unsupported banks,
   paths and cross-bank references. Build the conflict graph and apply picks.
4. Allocate IDs and compose all banks in scratch; independently reopen the
   outputs and check references. Preserve opaque data or refuse when a safe
   edit cannot be established.
5. Stage through one backup manifest while Fable is closed. Restore returns
   the exact prior files, including Aeon's installed baseline when that was
   selected as the source profile.

Start with import-and-wrap support for original FMPs and EgoCore folders, then
export native operations only for format families with proven writers. The
historic corpus becomes a compatibility suite: acceptance, refusal reason,
byte preservation, load-order precedence, and selected runtime checks. Full
overhaul conversion, arbitrary DLL dependency resolution, and unverified WAD
or anonymous-record relocation remain explicit open work.
