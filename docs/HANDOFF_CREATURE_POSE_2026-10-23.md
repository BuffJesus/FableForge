# Paused Creature pose continuation, 2026-10-23

The user asked to stop and prepare the next session. The continuing FableForge
goal is paused, not complete. Resume here, then consult the detailed
[assembly evidence](HANDOFF_CREATURE_ASSEMBLY_2026-10-23.md).

## Checkout and accepted baseline

Use `C:/Users/Cornelio/AppData/Local/FableForgeDevelopment/definition-clone-gui-20261010`,
branch `feat/definition-clone-gui-20261010`. Preserve the separate dirty
`D:/Code/FableForge` checkout and Native work. For Git commands set:

```powershell
$env:GIT_OBJECT_DIRECTORY='C:/Users/Cornelio/AppData/Local/FableForgeDevelopment/definition-clone-git-objects'
$env:GIT_ALTERNATE_OBJECT_DIRECTORIES='D:/Code/FableForge/.git/objects'
```

Read root AGENTS and the project's dev/format/RE skills. No subagents were used.
Keep PR #2 draft; no merge, stable release or replacement of immutable ZIPs.
The retained package is still the profiles checkpoint built from 37c1ed1, not
the subsequent source work. Whole-Creature rendering/gameplay remain open.

Source through de80f2f is accepted for the shared appearance reader/selector,
ownership resolver, bone-config reader, positive scale combination and CRC slot
mapping. CI 39989492089 passed build, units, WARP GUI without an install and CLI;
package/upload skipped. Local receipts and exact boundaries are in the assembly
handoff. c81be32 adds the executed native pose-stage oracle, not renderer integration.

## Current checkpoint: builds, execution pending

New core files `forge/creaturepose.hpp` and `creaturepose.cpp` implement:

- Native quaternion/position frames, with parent-scaled child displacement.
- Rigid parent hierarchy followed by independent per-bone geometry scaling.
- `global * inverseBind` skin matrices and finite/count/parent/affine checks.
- A decoded-mesh bind adapter reading preserved 48-byte native keyframes;
  stored keyframe scaling is ignored by this path, as in the native 4x4 routines.

The new `fableforge_creaturepose_tests` target **builds but has not been run**.
Build log: `build/creature-pose-build-20261023.log`. Its modes are:

```text
fableforge_creaturepose_tests.exe
fableforge_creaturepose_tests.exe --oracle <native-pose-receipt.json>
fableforge_creaturepose_tests.exe --bind-mesh <graphics.big> <mesh-id>
```

No-argument cases cover displacement/independent scale, malformed inputs and raw
bind-record preservation. Oracle mode compares globals for 240 native cases and
skin matrices for 120 transposed cases. `--bind-mesh` reports bone/vertex counts
and neutral skin-matrix deviation; this retail diagnostic has not been executed.
`build/creature-pose-retail-samples-20261023.json` selects 12 spread body meshes
from the 139 distinct nonzero mesh IDs in all stored appearance alternatives.
No retail mesh result or C++ pose acceptance may be inferred from the build.

The C++ matrix/quaternion arithmetic has not been qualified on arbitrary retail
frames. Synthetic oracle comparisons are the next gate, not a general numerical
or animation-parity claim. The API is unused by scene/Assets rendering.

## Latest original-instruction evidence

`tools/audit_creature_pose_stages.py` was extended through actual native
AlignedPreMultiplyTransposed (02e8e0f0). All 240 stage cases pass; 120 transposed
cases also confirm `posedGlobal * inverseBind` order. Packet:
`C:/Users/Cornelio/AppData/Local/FableForgeEvidence/20261023/pi-f_r5r0vx/result.json`.
It retains the matching producer. Working copy:
`build/creature-assembly-native-20261023/pose-inverse-bind-native.json`.

Only mesh count, local-bone lookup and parent lookup are bridged; actual math
instructions execute in Unicorn. This is offline emulation, not a native game
launch. Quaternion signs differ from the common glTF formula. The earlier wrong
model and correction remain recorded in the prior pose packets. Both CPU math
paths, both matrix layouts, zero/non-unit quaternions, independent roots,
siblings and nonuniform scales are covered. Animation sampling and vertex
skinning are still outside this oracle.

## Coordination and first actions on resume

No Forge runtime/controller or owned lease remains. Native was actively taking
fresh per-capture V86 windows at the last inspection; never treat that historical
observation as a current hold or permission. Read the latest queue/lease/process
state. Do not close user-owned games or reclaim another owner's lease.

Shared lease: `D:/Documents/FableTLC/.agents/runtime-lease.json`.
Use the existing RuntimeLease helper and immediate conservative process guard
before every named executable launch. Neighboring TLC CPU tests also trip it.
Use short bounded windows and release only the owned token with a receipt.

Native thread `01a168a5-fbee-72f0-87fe-39450caddb64` reached 100 queued submissions;
release notification for the earlier bone-combination check failed despite the
actual successful lease release. Read-only count still showed 100 at the latest
inspection. Do not retry-spam or mutate the queue DB. Resolve current transport
availability before relying on notification delivery. This session took no new
runtime lease for the unexecuted Creature pose checks.

Next actions:

1. Reinspect the working tree and build the scoped pose target below normal,
   Ninja `-j2`, if source/binary identities require it.
2. In a fresh coordinated window, run the unit and `--oracle` against the packet
   above. Preserve the first failure; fix actual convention/layout differences
   instead of weakening the native expectation.
3. Run representative retail bind diagnostics. Steam inputs are under
   `C:/Programs/Steam/steamapps/common/Fable The Lost Chapters`; the bank is
   `data/graphics/graphics.big`. Pin source hashes and investigate deviations
   before claiming pose correctness on decoded meshes.
4. Reuse the existing animation reader and suitable skinning primitives, then
   connect selection/texture composition/morph resources through the shared
   ownership resolver and runtime profile into complete Creature presentation.
   Preserve pack asset context, use explicit preview seeds, and distinguish bind
   preview from animation. Missing parts or unsupported morphs must not silently
   become a partial NPC presented as complete.
