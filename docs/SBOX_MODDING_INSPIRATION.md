# s&box inspiration for FableForge and the recreated engine

Reviewed 2026-09-27. This design comparison proposes future work. Only this
report was written by the background reviewer; no s&box code was copied,
no engine was installed or launched, and no game or implementation files changed.

**Recommendation:** use s&box to improve the authoring loop around Fable's
existing formats: richer property metadata, dependency-aware bakes, reusable
presets with stable references, and one transaction interface shared by editor,
CLI and eventual engine tools. Keep runtime play/reload work in the modernization
fork. These ideas reinforce the existing architecture rather than require a new
engine framework.

## What was actually reviewed

The public repository currently contains the managed C# engine, editor, tools
and game content. The native Source 2 core comes as downloaded prebuilt binaries;
this is not the complete native engine implementation. The README distinguishes
MIT source from native binaries governed by the s&box EULA and separately licensed
third-party components. This report uses architecture and behavior as inspiration,
not those binaries or a proposal to move Fable onto Source 2.
[Repository README](https://github.com/Facepunch/sbox-public/blob/372c601f8332149851410d88f36c0184bcb988f1/README.md).

GitHub API resolved `master` to commit
`372c601f8332149851410d88f36c0184bcb988f1`, dated 2026-09-26 22:52:15 UTC.
Source links below are pinned to that revision. Facepunch documentation was read
live on the review date. Evidence categories are **source inspected**, **documented**,
and **proposed for Fable**; neither reading source nor seeing a test proves the
feature was run successfully here.

Local comparison points:

- FableTLC `docs/engine/IN_ENGINE_MODDING_ENVIRONMENT.md`, especially sections
  5, 7 and 9; it already covers the broad s&box comparison.
- FableTLC `docs/engine/SCRIPTING_REDESIGN.md`: Lua host, declarative state,
  reload with thread restart, generated API and headless host.
- FableForge `docs/ROADMAP_1.0.md`: annotation schema, manifest, model recipe,
  per-map forgecore bake are already the next architectural steps.
- `src/leveledit.hpp`, `libs/forgecore/include/forge/defschema.hpp`,
  `libs/forgecore/include/forge/modorder.hpp`, `src/worldedit.cpp`,
  `gui/automation.cpp`, and [the debug audit](DEBUG_EDITOR_FEATURE_AUDIT.md).

The value added here is narrower contracts and acceptance examples for those
existing directions, rather than another broad feature wishlist.

## Highest-value adaptations

### 1. Inspector metadata with explanations and validation

**Documented:** s&box property attributes control grouping, titles, conditional
visibility, ranges, read-only/advanced fields, asset pickers and inline validation.
These are author-facing metadata, separate from a field's raw storage type.
[Property attributes](https://sbox.game/dev/doc/editor/property-attributes).

**Fable today:** `forge::defschema::Field` records name, type, donor offset and
optional retail offset. It deliberately does not provide display groups, ranges,
reference categories or semantic validation. The roadmap already proposes an
entity annotation layer over this schema.

**Proposed first slice:** annotate one practical class, such as a creature
generator or activation trigger. Give referenced definitions and UIDs typed
pickers; explain defaults; group rarely changed properties as advanced. Keep
unknown fields editable through the existing preservation path. A recovered
field type does not establish a valid range: annotations should carry their
evidence/confidence and avoid invented clamps.

**Acceptance:** selecting a broken reference identifies the owning thing and
field, offers the appropriate picker, and leaves unrelated serialized bytes
unchanged. The same validator returns structured results to CLI and GUI.

### 2. Bake dependencies are part of the data model

**Source inspected:** `ResourceCompileContext` distinguishes runtime references,
compile-only inputs and arbitrary packaged game files. An optional compile input
may be absent yet still cause recompilation if later created; `ReadMeta()` uses
that mechanism for `.meta` sidecars. The abstract interface establishes the
contract; this review did not inspect the complete native dependency scheduler.
[ResourceCompileContext](https://github.com/Facepunch/sbox-public/blob/372c601f8332149851410d88f36c0184bcb988f1/engine/Sandbox.Engine/Resources/Compiling/ResourceCompileContext.cs).

**Source inspected:** the editor asset abstraction exposes references,
dependants, input dependencies, unresolved references, source/compiled presence,
compile failure and freshness, plus temporary in-memory replacements for preview.
These are useful distinct states, not one generic dirty boolean.
[Asset](https://github.com/Facepunch/sbox-public/blob/372c601f8332149851410d88f36c0184bcb988f1/engine/Sandbox.Tools/Assets/Asset.cs).

**Fable today:** major bakers exist, but the roadmap still places orchestration
across `src/lodbake`, `src/stbterrain`, compaction and forgecore. The debug audit
shows why dependencies matter: nav detail may depend on a neighboring map's
building; an engine-style minimap depends on building physics as well as terrain;
world-coordinate fractal terrain can affect several editable maps.

**Proposed first slice:** make a per-map bake request return its input identities,
derived outputs, reasons for invalidation, diagnostics and source revision.
Classify dependencies explicitly. Cache keys should include relevant input
content, compiler version and options. A background result should be accepted
only for the revision it compiled. Compile into a scratch output first, validate,
then expose the new result; engine resource swapping belongs to the later fork.

**Acceptance:** changing one terrain theme rebuilds the appropriate derived
outputs; moving an overlapping building invalidates affected nav/minimap work;
unrelated maps remain untouched. A failed bake preserves the last usable output
while clearly marking it stale. This is a proposed Fable contract, not a claim
about s&box's full scheduler.

### 3. Separate package identity, installation order and authoring dependencies

**Source inspected:** `ProjectConfig` has organization/identifier, schema,
runtime package references, editor references and mounts. Editor references
represent authoring dependencies that need not be installed for the final
compiled package. Its schema version is distinct from content identity.
[ProjectConfig](https://github.com/Facepunch/sbox-public/blob/372c601f8332149851410d88f36c0184bcb988f1/engine/Sandbox.Engine/Systems/Project/ProjectConfig.cs).

**Fable today:** `forge_mods.json` is an install's ordered list of sources, hashes,
enabled flags and notes (`modorder.hpp`), with a versioned order container. A
content hash answers which bytes were installed; a package ID answers which mod
they belong to. Those should not be conflated.

**Proposed first slice:** agree on a per-mod manifest consumed by both the
offline composer and future VFS: stable ID, content version, schema version,
runtime dependencies, authoring-only dependencies, required base data and script
entry points. Keep `forge_mods.json` as the user's installation selection/order
unless an explicit migration is chosen. Reuse existing composition/conflict
logic; no hosted workshop or account service is needed.

**Acceptance:** the same local mod folder builds a retail-compatible staged pack
and describes a future VFS mount; absent authoring sources do not make a valid
compiled-only package fail runtime dependency checks. Cycles/missing runtime
dependencies produce an actionable diagnostic.

### 4. Reusable presets should preserve identities and overrides

**Source inspected:** prefab instances keep source identity, patches and GUID
mappings; the object model has explicit update and break-from-prefab operations.
[Prefab object integration](https://github.com/Facepunch/sbox-public/blob/372c601f8332149851410d88f36c0184bcb988f1/engine/Sandbox.Engine/Scene/GameObject/GameObject.Prefab.cs).

**Source test inspected, not executed:** refresh tests expect a changed base
property to propagate while a local override and added child's identity survive.
Other cases exercise prefab identity replacement and nested dependency order.
[Prefab refresh tests](https://github.com/Facepunch/sbox-public/blob/372c601f8332149851410d88f36c0184bcb988f1/engine/Tests/Sandbox.Test.Engine/Scene/GameObjects/PrefabInstance.Refresh.cs).

**Fable today:** `Document::Fragment` is a group of TNG blocks/frames; paste
assigns fresh UIDs and resets ScriptName. Existing copy/paste and presets are a
good base, but do not by themselves establish persistent template ancestry or
reference-preserving updates.

**Proposed first slice:** give a preset a stable identity, map template-local IDs
to placed TNG UIDs, and record per-instance property/transform overrides in an
editor sidecar. Flatten to ordinary TNG for retail output. First solve internal
UID references for a small trigger/door group; defer nested prefab inheritance.

**Acceptance:** place two copies of a wired camp/door preset; each points to its
own objects. Update the template's default appearance without overwriting one
instance's deliberately changed transform. Undo restores both data and links.
This connects directly to the debug audit's track/route and level-merge leads.

### 5. Tools, automation and UI should share transactions

**Documented:** s&box undo scopes capture before/after changes, but callers must
identify the objects being modified. Merely opening a scope captures nothing.
[Undo system](https://sbox.game/dev/doc/editor/undo-system).

**Documented:** s&box exposes scene inspection, object edits, assets, play mode,
console and screenshots through an editor MCP service bound to the local machine.
This review read documentation; it did not connect to or test that service.
[Editor MCP server](https://sbox.game/dev/doc/editor/mcp-server).

**Fable today:** document commands already create undo steps, nested batches
group actions, and GUI automation drives tests. The engine design already calls
for serializable transactions shared by tools, console and dev IPC.

**Proposed sequence:** first normalize document operations into typed requests
with stable target IDs, revision checks and structured results. The GUI and
automation harness call the same operations. An eventual IPC/MCP adapter can
expose those operations without implementing a second editing engine. An adapter
is optional; it should not delay useful editor features.

**Acceptance:** a multi-object move through automation and the GUI yields the
same document change and a single undo step. A stale request reports the changed
revision instead of acting on a different object at a recycled index.

## Later, in the recreated engine

**Edit and play are separate sessions.** Inspected s&box code creates a game
session, maps selection into it, and destroys that session on stop before
reactivating the parent edit session. These files establish session ownership;
they alone do not prove isolation of every resource or absence of a compile step.
[Scene session play controls](https://github.com/Facepunch/sbox-public/blob/372c601f8332149851410d88f36c0184bcb988f1/engine/Sandbox.Tools/Scene/Session/SceneEditorSession.Game.cs),
[GameEditorSession](https://github.com/Facepunch/sbox-public/blob/372c601f8332149851410d88f36c0184bcb988f1/engine/Sandbox.Tools/Scene/Session/GameEditorSession.cs).

For Fable, retain the proposed edit/play boundary, and make its proof concrete:
start from a known document revision, enter play, spawn/move/destroy things,
stop, and compare the edit document and reference graph to the pre-play snapshot.
Runtime changes should only become authoring changes through an explicit apply
operation. Retail's existing live link is not this feature.

**Reload needs a result model and migration rules.** s&box's managed hotloader
has per-type upgrade hooks, cached instance replacement and deferred processing.
Its result model distinguishes errors from warnings, includes contextual paths
and timings, and recognizes an instance upgrade can require restarting the game.
[InstanceUpgrader](https://github.com/Facepunch/sbox-public/blob/372c601f8332149851410d88f36c0184bcb988f1/engine/Sandbox.Hotload/InstanceUpgrader.cs),
[HotloadResult](https://github.com/Facepunch/sbox-public/blob/372c601f8332149851410d88f36c0184bcb988f1/engine/Sandbox.Hotload/HotloadResult.cs).

For Fable, use the existing Lua plan: explicit declared state, versioned
migrations, thread cancellation/restart and resource cleanup. Report whether a
reload succeeded, preserved state, restarted threads or requires a restart.
Do not imitate arbitrary managed-object upgrades for C++ pointers or promise
that an in-progress quest coroutine can resume at the same instruction.

**Visual logic should use the same APIs as scripts.** Facepunch documents
ActionGraph as reusable graph resources with event inputs, control flow and
custom code-backed nodes.
[ActionGraph](https://sbox.game/dev/doc/actiongraph/).
Fable already has quest-node tooling and the scripting proposal has a shared
generated API. Build on those rather than adding another graph runtime. A
trigger-to-door example with readable generated Lua and matching diagnostics
would be a better first result than a general-purpose visual scripting system.

## Relationship to the debug audit and current roadmap

| Existing finding/work | Useful s&box-inspired contract | Boundary |
|---|---|---|
| Preferred-path brush and camera-passability research | Typed property/brush metadata, inline validation, one undo transaction | Actual Fable persistence and navigation semantics still need native/retail proof. |
| Fractal terrain across maps | Preview compiled from a draft revision, apply as one grouped action | Preserve world-coordinate sampling and map eligibility recovered from FableWin. |
| Object collision, nav detail, minimap building silhouettes | Cross-map input dependencies and visible stale-bake diagnostics | s&box does not provide Fable's generator algorithms. |
| Linked tracks/route targets and UID merge behavior | Stable preset IDs, internal UID remapping, local overrides | Tracks, camera tracks and precalculated navigation routes remain distinct systems. |
| Native minimap-zone setter is empty | Expose only verified capabilities | A surviving debug GUI label does not justify implementing a speculative feature. |
| Current standalone editor + future tools component | Shared forgecore operations and dev IPC | Live resource reload belongs to the modernization fork. |

The existing in-engine design's broad direction remains sound, but its claims
about fully exact nav and useful minimap-zone tools should be read alongside the
newer [debug audit](DEBUG_EDITOR_FEATURE_AUDIT.md) and [nav handoff](HANDOFF_NAV.md).
Those documents have more recent behavior-level evidence. This report does not
modify the upstream proposal or expand the active navigation task.

Suggested delivery order: annotation/validation slice; per-map bake contract and
dependency reporting; manifest identity/dependency contract; reference-safe
presets; common transaction API; engine play/reload only when the recreated
world and resource manager support them. Continue already-authorized navigation
work before taking on these new implementation slices.

Do not import .NET, Source 2 formats, a general scene-graph replacement, hosted
package services or multiplayer merely to resemble s&box. Fable's regions,
LEV/TNG/defs, Lua ecosystem and forgecore remain the compatibility anchors.

## Review limits

This was a targeted online source/documentation review plus local source reads,
not a complete repository audit, engine benchmark or license opinion. Referenced
tests were read but not executed. No claims are made about native Source 2
compiler internals, s&box's end-to-end security boundary or exact reload timing.
Proposed acceptance cases describe future Fable work; none were implemented in
this task.
