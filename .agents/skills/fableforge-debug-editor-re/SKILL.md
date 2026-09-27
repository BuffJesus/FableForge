---
name: fableforge-debug-editor-re
description: Recover Fable debug-build editor behavior before implementing FableForge world-editor algorithms such as navigation, terrain baking, collision outlines or map generation. Use targeted decompilation and retail fixtures; excludes symbol-only surveys and native engine rebuilding.
---

# Preflight

- Identify the feature and acceptance question from the current lane handoff.
- Resolve the FableForge worktree; inspect AGENTS, dirty paths and skill ownership.
- Check [roadmap](../../../docs/ROADMAP_1.0.md) and [engine rules](../../../docs/ENGINE_RULES.md).
- Locate the FableTLC evidence checkout separately and keep it read-only for this lane.
- Evidence there includes `debug_build/FableWin.exe` / `.pdb`, `symbols`, `ghidra_proj`,
  `ghidra_out/fablewin_editor_symbols.tsv`, and `ghidra_out/leveleditor_decomp.c`.
- Navigation exports include `ghidra_out/decomp_navmesh.c`, `decomp_navmesh2.c`
  and `decomp_navmesh2_key.c`; search for the target function before reading ranges.
- Check the previous lane's referenced export paths: targeted exports may be in session scratch space.
- Distinguish FableWin addresses/types from retail addresses before following cross-references.

# Investigate one behavior

1. Form a falsifiable question from a small fixture, unexplained output or missing editor action.
2. Search symbols for that feature: e.g. navigation area initialization or the relevant editor command.
   Do not dump the full symbol table or decompile every caller speculatively.
3. Read the implementation body, its immediate caller and data structures; names alone are insufficient.
4. Trace the UI command through editor state, engine calls and serialized output where applicable.
5. Inspect defaults, empty inputs, early returns, clamps, coordinate transforms and error paths.
6. Recover ownership and selection rules: which maps, objects, children, layers or seeds enter the algorithm?
7. Cross-check consequential branches, virtual dispatch, constants and types against disassembly/PDB data.
8. If exports are incomplete, use a targeted read-only Ghidra/headless query on the identified program.
   Inspect existing invocation scripts; never force-unlock or mutate another session's project.
9. If Ghidra cannot run, use existing exports, identify their provenance and state what is unverified.
10. Compare with a small retail/debug fixture; include coordinates, counts and counterexamples.

# Translate into FableForge

- Reuse `libs/forgecore` format abstractions and the existing scene/world model.
- Separate observations, disassembly-confirmed facts and hypotheses in the findings.
- Treat debug behavior as evidence; compare retail layout and behavior before calling it a contract.
- Express clean C++ types/algorithms rather than copying malformed decompiler temporaries or guessed ABI layouts.
- For nav, trace initialization inputs, tree refinement, switchable blockers, connectivity and serialization.
- For terrain, distinguish source heights, collision, foreground/distant bakes and minimap output.
- For route markers/fractal generation, inspect actual command parameters and persistence before inventing APIs.
- Preserve unexplained fixture differences; a good aggregate score does not prove node-level parity.

# Validation and stop condition

- First test the smallest case distinguishing the competing explanations.
- Then compare representative output with retail/debug evidence and run affected regressions.
- Record executable identity, source symbol/address, export path, confidence and remaining gaps in `docs`.
- Keep changing findings and feature status out of this skill.
- Reuse unchanged exports and reports instead of rerunning large queries.
- Stop the investigation when the acceptance question is answered with evidence sufficient for the next change.
- If evidence contradicts the proposed algorithm, narrow or defer that part; do not silently fill gaps.
