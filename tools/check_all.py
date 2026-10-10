#!/usr/bin/env python3
"""Run every check: build, unit tests, retail CLI smoke, GUI suites.

  python tools/check_all.py [--no-build] [--count 8]
"""
import argparse, json, os, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def retail_dialogue_big():
    root = os.environ.get("FABLE_TLC_ROOT", "")
    if not root:
        settings = os.path.join(os.environ.get("APPDATA", ""), "FableForge", "settings.json")
        try:
            with open(settings, encoding="utf-8") as f:
                root = json.load(f).get("install", "")
        except (OSError, ValueError, AttributeError):
            pass
    path = os.path.join(root, "data", "lang", "English", "dialogue.big") if root else ""
    return path if path and os.path.isfile(path) else ""

def run(name, cmd, **kw):
    t0 = time.time()
    r = subprocess.run(cmd, **kw)
    for output in (r.stdout, r.stderr):
        if output:
            text = output.decode(errors="replace") if isinstance(output, bytes) else output
            if r.returncode:
                print(text, end="" if text.endswith("\n") else "\n")
            else:
                for line in text.splitlines():
                    if "skip" in line.lower(): print(line)
    print(f"[{'PASS' if r.returncode == 0 else 'FAIL'}] {name} ({time.time() - t0:.1f}s)")
    return r.returncode == 0

def main():
    os.chdir(ROOT)  # the script's owning worktree, even when launched from another checkout
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-build", action="store_true")
    ap.add_argument("--count", type=int, default=8)
    a = ap.parse_args()
    ok = True
    if not a.no_build:
        ok &= run("build", ["cmake", "--build", "build"], capture_output=True)
    ok &= run("CTest (core, rows, geometry, LZO)", ["ctest", "--test-dir", "build", "--output-on-failure"], capture_output=True)
    ok &= run("UI system glyph fallback, missing fonts, roles and atlas rebuilds",
              [sys.executable, "tools/test_font_fallback_ui.py"], capture_output=True)
    for label, script in (
        ("read-only world storage CLI and per-entry refusals", "test_worldreview_cli.py"),
        ("LEV exact-name ambiguity and class findings", "test_levsymbols.py"),
        ("compiled name CRC candidates and stale-report refusal", "test_deflookups.py"),
        ("read-only namespace CLI, mappings and lookup hazards", "test_namespacereview_cli.py"),
        ("BIN chunk framing and malformed record boundaries", "test_binboundaries.py"),
        ("names writer native stream length and preservation", "test_nameswriter.py"),
        ("new retail BIG entry markers and donor preservation", "test_big_entry_markers.py"),
        ("definition diagnostic raw-field reconstruction", "test_defsdecode_raw.py"),
        ("music-array framing, local keys and source path bounds", "test_musicbindings.py"),
        ("text entry ID parsing, explicit allocation and refusal preservation", "test_text_entry_ids.py"),
        ("text show strict identity, ambiguity refusal and narrator headers", "test_text_show_identity.py"),
        ("text import encoding/ID refusal before writes", "test_text_import_validation.py"),
        ("text speaker header synchronization and unsafe narrator refusal", "test_text_speaker_header.py"),
        ("text payload truncation and string terminators", "test_text_decode_boundaries.py"),
        ("texture exact-name priority and ambiguous write refusal", "test_texture_lookup.py"),
        ("texture dimension validation and output preservation", "test_texture_dimensions.py"),
        ("texture import option refusal and preservation", "test_texture_import_options.py"),
        ("animated/volume texture replacement refusal", "test_texture_layout_refusal.py"),
        ("cutout texture replacement and alpha preservation", "test_texture_cutout.py"),
        ("new texture entry types and donor preservation", "test_texture_entry_types.py"),
        ("texture append ID exhaustion and output preservation", "test_texture_id_exhaustion.py"),
        ("mesh/collision ID exhaustion and install preservation", "test_mesh_id_exhaustion.py"),
        ("mesh-import strict texture IDs and refusal preservation", "test_mesh_texture_ids.py"),
        ("mesh donor identity and generated name refusal", "test_mesh_identity_refusal.py"),
        ("mesh non-finite geometry and bank preservation", "test_mesh_invalid_geometry.py"),
        ("static mesh replacement and archive preservation", "test_mesh_replace.py"),
        ("definition finite float/vector edits and byte preservation", "test_defs_finite_values.py"),
        ("sparse mesh imports and refusal preservation", "test_mesh_sparse.py"),
        ("triangle strip/fan imports and refusal preservation", "test_mesh_topology.py"),
        ("combined collision hull index limits and preservation", "test_mesh_collision_limit.py"),
        ("faction registry domains and literal relationship framing", "test_factionregistry.py"),
        ("particle literal symbol filtering and accepted field locations", "test_particlebindings.py"),
        ("bounded minimap reference table inventory", "test_worldassets.py"),
        ("reviewed TNG definition coverage and ambiguity", "test_thingsymbols.py"),
    ):
        ok &= run(label, [sys.executable, "tools/" + script], capture_output=True)
    ok &= run("unchanged bank carries, unsupported sibling/FMP bank refusal and export preservation",
              [sys.executable, "tools/test_mod_bank_refusal.py"], capture_output=True)
    ok &= run("whole-bank slots, sibling banks and granular conflict choices",
              [sys.executable, "tools/test_mod_whole_bank_layout.py"], capture_output=True)
    ok &= run("FMP record identity refusal and supported legacy replacements",
              [sys.executable, "tools/test_fmp_record_refusal.py"], capture_output=True)
    ok &= run("mod label choice identity, staged refusal and legacy recovery",
              [sys.executable, "tools/test_mod_label_refusal.py"], capture_output=True)
    ok &= run("finite float conflict text and exact selected bank bits",
              [sys.executable, "tools/test_mod_float_values.py"], capture_output=True)
    if os.name == "nt":
        ok &= run("restore language/root aliases and stage-created running ownership",
                  [sys.executable, "tools/test_text_restore_alias_guard.py"], capture_output=True)
        ok &= run("text bank prepared verification, locked publication, backups and running destination guards",
                  [sys.executable, "tools/test_text_write_publication.py"], capture_output=True)
        ok &= run("text browser property jumps, narrator lists, TSV preservation and language/root fallback",
                  [sys.executable, "tools/test_text_browser_ui.py"], capture_output=True)
        ok &= run("mod output publish rollback, running destination refusal and exit retry",
                  [sys.executable, "tools/test_mod_output_publish.py"], capture_output=True)
        ok &= run("cutout texture replacement in the editor",
                  [sys.executable, "tools/test_texture_cutout.py", "--ui"], capture_output=True)
        ok &= run("animated/volume texture visible replacement refusal",
                  [sys.executable, "tools/test_texture_layout_refusal.py", "--ui"], capture_output=True)
        ok &= run("model format UI imports, visible refusal and preserved preview",
                  [sys.executable, "tools/test_mesh_format_ui.py"], capture_output=True)
        ok &= run("static mesh replacement UI, visible refusal and refreshed preview",
                  [sys.executable, "tools/test_mesh_replace_ui.py"], capture_output=True)
        ok &= run("texture UI exact names, ambiguous shortcuts and max ID",
                  [sys.executable, "tools/test_texture_names_ui.py"], capture_output=True)
        ok &= run("model UI exact names and ambiguous ID/name preservation",
                  [sys.executable, "tools/test_model_names_ui.py"], capture_output=True)
        ok &= run("effect exact names, unique fallback and duplicate refusal",
                  [sys.executable, "tools/test_effect_names.py"], capture_output=True)
        for options in ([], ["--complete-data"]):
            ok &= run("Windows Unicode file identity, choices and exact restoration " + str(options),
                      [sys.executable, "tools/test_mod_unicode_file_identity.py", *options], capture_output=True)
            ok &= run("mod output ownership, rollback and fresh-only staging " + str(options),
                      [sys.executable, "tools/test_mod_build_ownership.py", *options], capture_output=True)
    dialogue = retail_dialogue_big()
    if dialogue:
        install_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(dialogue))))
        ok &= run("Read-only Creature visual ownership and relocation observations (not clone eligibility)",
                  [sys.executable, "tools/audit_creature_visual_graph.py", "--install", install_root,
                   "--payload-exe", "build/fableforge_defrelocate_tests.exe"], capture_output=True)
        ok &= run("Read-only Creature animation boundaries and numeric candidate observations",
                  [sys.executable, "tools/audit_creature_animation.py", "--install", install_root,
                   "--payload-exe", "build/fableforge_defrelocate_tests.exe"], capture_output=True)
        ok &= run("Private Creature visual API, localized ownership, atomic refusal and independent paired output",
                  [sys.executable, "tools/test_creature_visual_clone_api.py", "--install", install_root], capture_output=True)
        ok &= run("Private Creature GUI controls at both sizes, independent native deltas and Restore",
                  [sys.executable, "tools/test_creature_private_clone_ui.py", "--install", install_root], capture_output=True)
        for donor_policy in ("inherited", "missing", "unresolved"):
            ok &= run("Private Creature GUI donor policy " + donor_policy,
                      [sys.executable, "tools/test_creature_private_clone_ui.py", "--install", install_root,
                       "--donor-policy", donor_policy, "--only", "save"], capture_output=True)
        ok &= run("Creature donor self-reference layout and independent relocation audit",
                  [sys.executable, "tools/audit_creature_self_refs.py", "--install", install_root, "--clone-api"], capture_output=True)
        ok &= run("Retail paired Creature clone, self-only deltas and Restore",
                  [sys.executable, "tools/test_creature_clone_retail.py", "--install", install_root], capture_output=True)
        for window_size in ("1440x900", "800x600"):
            ok &= run("Retail Creature clone controls and Restore " + window_size,
                      [sys.executable, "tools/test_creature_clone_retail.py", "--install", install_root,
                       "--gui", "build/FableForge.exe", "--size", window_size], capture_output=True)
        ok &= run("Creature GroupDef schema, independent boundaries and all retail records",
                  [sys.executable, "tools/test_creature_group_schema.py", "--install", install_root], capture_output=True)
        ok &= run("retail group picker families, exact field deltas and Restore",
                  [sys.executable, "tools/test_definition_group_retail.py", "--install", install_root], capture_output=True)
        ok &= run("retail GUI Unicode definition delta, identity preservation and Restore",
                  [sys.executable, "tools/test_definition_wide_retail.py", "--install", install_root], capture_output=True)
        ok &= run("retail lipsync byte roundtrip", [os.path.join("build", "fableforge_lipsync_tests.exe"), dialogue])
        ok &= run("retail dialogue LUT/lipsync join", [os.path.join("build", "fableforge_lut_tests.exe"), os.path.dirname(dialogue)])
        ok &= run("retail dialogue subtitle join", [os.path.join("build", "fableforge_dialoguetext_tests.exe"), install_root])
        graphics = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(dialogue))), "graphics", "graphics.big")
        if os.path.isfile(graphics):
            ok &= run("retail lip sync head presets", [os.path.join("build", "fableforge_lipsync_preset_tests.exe"), graphics])
            ok &= run("retail head skeletons", [os.path.join("build", "fableforge_meshskeleton_tests.exe"), graphics])
            ok &= run("retail animation bank and phoneme poses", [os.path.join("build", "fableforge_animation_tests.exe"), graphics, "--all"])
            ok &= run("retail head phoneme skin poses", [os.path.join("build", "fableforge_headpose_tests.exe"), graphics])
        ok &= run("retail dialogue frame edits, scratch export and pack recipe",
                  [sys.executable, "tools/test_dialogue_edit.py", "--install", install_root], capture_output=True)
        ok &= run("dialogue gesture, frame/shape and line-history undo",
                  [sys.executable, "tools/test_dialogue_undo.py", "--install", install_root], capture_output=True)
        ok &= run("create and save a mod pack from Dialogue",
                  [sys.executable, "tools/test_dialogue_pack_create.py", "--install", install_root], capture_output=True)
        ok &= run("dialogue deployment and Setup restore refresh",
                  [sys.executable, "tools/test_dialogue_restore.py", "--install", install_root], capture_output=True)
        ok &= run("mod conflict winner reset and scratch deploy",
                  [sys.executable, "tools/test_mod_winner_refresh.py", "--root", install_root], capture_output=True)
        ok &= run("mod field details, actual contributors, choice scope and independent merged values",
                  [sys.executable, "tools/test_mod_field_details.py", "--install", install_root], capture_output=True)
        ok &= run("individual scalar field choices, precedence/reset, exact merged bytes and layout identity",
                  [sys.executable, "tools/test_mod_field_choices.py", "--install", install_root], capture_output=True)
        ok &= run("mod raw-file snapshots, same-size source refresh, clip visibility and composed picks",
                  [sys.executable, "tools/test_mod_file_content.py", "--install", install_root], capture_output=True)
        ok &= run("long mod field values, each baseline/contributor/result and exact merged payload",
                  [sys.executable, "tools/test_mod_long_values.py", "--install", install_root], capture_output=True)
        ok &= run("mod conflict search beyond 400 rows, saved choices and reset",
                  [sys.executable, "tools/test_mod_conflict_search.py", "--install", install_root], capture_output=True)
        ok &= run("unavailable saved mod choices, actual winner, preservation and recovery",
                  [sys.executable, "tools/test_mod_unavailable_choice.py", "--install", install_root], capture_output=True)
        ok &= run("saved choices outside the conflict report, preservation and individual clearing",
                  [sys.executable, "tools/test_mod_unreported_choices.py", "--install", install_root], capture_output=True)
        ok &= run("active asset search and hidden panel keyboard recovery",
                  [sys.executable, "tools/test_asset_search.py", "--install", install_root], capture_output=True)
        ok &= run("model import form, pack retry and next-draft preservation",
                  [sys.executable, "tools/test_model_import_form.py", "--install", install_root], capture_output=True)
        if os.name == "nt":
            for options in ([], ["--invalid"], ["--add"], ["--add", "--invalid"]):
                ok &= run("Texture image refusal, persistent error, retry and exact Restore (bounded retail-derived bank) " + str(options),
                          [sys.executable, "tools/test_texture_replace_failure_ui.py", "--install", install_root,
                           "--small-bank", *options], capture_output=True)
            for mode in ("redeploy", "report", "missing", "unnamed", "sibling", "record"):
                ok &= run(f"Mods {mode} persistent errors, preserved files and same-process retry",
                          [sys.executable, "tools/test_mod_action_error_ui.py", "--install", install_root,
                           "--mode", mode], capture_output=True)
            corpus = os.path.join(ROOT, "work", "nexus_mods", "_peek")
            controller_fmp = os.path.join(corpus, "ControllerSupportFMP", "ControllerSupport.fmp")
            aeon = os.path.join(corpus, "AeonEdition")
            f2_fmp = os.path.join(corpus, "F2MeleeWeapons", "F2MeleeWeaponPack.fmp")
            if os.path.isfile(controller_fmp) and os.path.isfile(os.path.join(aeon, "Data", "CompiledDefs", "game.bin")):
                ok &= run("FMP named references, payload preservation, choices and missing-target refusal",
                          [sys.executable, "tools/test_fmp_links.py", "--install", install_root,
                           "--fmp", controller_fmp, "--aeon", aeon], capture_output=True)
            else:
                print("[SKIP] named FMP reference audit (controller/Aeon corpus unavailable)")
            if os.path.isfile(f2_fmp):
                ok &= run("unsupported FMP refusal, persistent Mods error and exact redeploy recovery",
                          [sys.executable, "tools/test_mod_action_error_ui.py", "--install", install_root,
                           "--mode", "fmp", "--fmp", f2_fmp], capture_output=True)
            else:
                print("[SKIP] unsupported FMP UI refusal (F2 corpus unavailable)")
            ok &= run("EgoCore Unicode compiler paths, startup refusal, retry and failed-output preservation",
                      [sys.executable, "tools/test_egocore_unicode_paths.py", "--install", install_root], capture_output=True)
            ok &= run("Unicode PNG inputs, locked/invalid preservation, decoded pixels and GUI replacement",
                      [sys.executable, "tools/test_texture_unicode_ui.py", "--install", install_root], capture_output=True)
            ok &= run("Unicode CLI arguments, packaged helper/schema lookup and GUI deploy recovery",
                      [sys.executable, "tools/test_unicode_paths.py", "--install", install_root], capture_output=True)
            ok &= run("invalid mod inputs preserve the previous deployment",
                      [sys.executable, "tools/test_mod_picks_inputs.py", "--root", install_root], capture_output=True)
    else:
        print("[SKIP] retail lipsync byte roundtrip (English dialogue.big unavailable)")
    ok &= run("lzo1x vs the engine's asm decoder", [sys.executable, "tools/verify_engine_lzo.py"], capture_output=True)
    ok &= run(f"retail smoke ({a.count} maps)", [sys.executable, "tools/retail_smoke.py", "--count", str(a.count)], capture_output=True)
    ok &= run("ui smoke", [sys.executable, "tools/ui_smoke.py"], capture_output=True)
    gui = os.path.join("build", "FableForge.exe")
    if dialogue:
        ok &= run("map compass heading through pitch, travel, zoom and turns",
                  [sys.executable, "tools/test_compass.py", "--install", install_root], capture_output=True)
        ok &= run("terrain slope following, saved orientation and undo",
                  [sys.executable, "tools/test_terrain_slope.py", "--install", install_root], capture_output=True)
        ok &= run("cell-centre placement, paste and carry at normal/compact sizes",
                  [sys.executable, "tools/test_placement_grid.py", "--install", install_root], capture_output=True)
        ok &= run("whole-map Raise camera bases, spline keys, scratch output and undo",
                  [sys.executable, "tools/test_wholemap_cameras.py", "--install", install_root], capture_output=True)
        ok &= run("Ctrl-drag height picking across sculpt tools at normal/compact sizes",
                  [sys.executable, "tools/test_height_pick.py", "--install", install_root], capture_output=True)
        ok &= run("Held brush pin, moved rays, release, Shift, survey and exact undo",
                  [sys.executable, "tools/test_brush_pin.py", "--install", install_root], capture_output=True)
        ok &= run("fixed-point sculpt height keys, other tools unchanged and undo",
                  [sys.executable, "tools/test_height_keys.py", "--install", install_root], capture_output=True)
        ok &= run("sculpt and height-key cursor vertices at normal/compact sizes",
                  [sys.executable, "tools/test_height_cursor.py", "--install", install_root], capture_output=True)
        ok &= run("precise terrain brush controls",
                  [sys.executable, "tools/test_terrain_brush_controls.py", "--install", install_root], capture_output=True)
        ok &= run("Sound brush cells, cursor, sampling and undo at normal/compact sizes",
                  [sys.executable, "tools/test_sound_cursor.py", "--install", install_root], capture_output=True)
        ok &= run("Replace strict vertex pen, cursor and undo at normal/compact sizes",
                  [sys.executable, "tools/test_replace_cursor.py", "--install", install_root], capture_output=True)
        ok &= run("Ground full stamp, strict vertex cursor, real paint and exact undo",
                  [sys.executable, "tools/test_ground_stamp.py", "--install", install_root], capture_output=True)
        ok &= run("Ground Smear, frozen blends, repeat, draft reopen and stock/loose preservation",
                  [sys.executable, "tools/test_ground_smear.py", "--install", install_root], capture_output=True)
        ok &= run("Environment strict cell pen, cursor, clear, sample and undo at both sizes",
                  [sys.executable, "tools/test_environment_cursor.py", "--install", install_root], capture_output=True)
        ok &= run("Crash recovery reopen, captured workers, timer and normal/compact controls",
                  [sys.executable, "tools/test_recovery.py", "--install", install_root], capture_output=True)
        ok &= run("Map and camera session restore, invalid data and automation isolation",
                  [sys.executable, "tools/test_session.py", "--install", install_root], capture_output=True)
        ok &= run("Dated snapshot capture, reopen, undo and confirmed deletion at both sizes",
                  [sys.executable, "tools/test_snapshots.py", "--install", install_root], capture_output=True)
        ok &= run("Budget area boundaries, captured objects and normal/compact input",
                  [sys.executable, "tools/test_budget_areas.py", "--install", install_root], capture_output=True)
        ok &= run("Joint terrain drafts, reopen, bake baseline and Restore on stock/loose copies",
                  [sys.executable, "tools/test_terrain_drafts.py", "--root", install_root], capture_output=True)
        ok &= run("Walkable/Camera rounded survey pens, cursors and exact undo",
                  [sys.executable, "tools/test_passability_cursor.py", "--install", install_root], capture_output=True)
        ok &= run("Shore-point pen, On/Off/Shift, visible overlays and exact undo",
                  [sys.executable, "tools/test_shore_points.py", "--install", install_root], capture_output=True)
        ok &= run("Environment Smear sparse cursor, controls, sampling and exact undo",
                  [sys.executable, "tools/test_environment_smear.py", "--install", install_root], capture_output=True)
        ok &= run("Sound/environment survey colours, selected filtering, controls and undo",
                  [sys.executable, "tools/test_survey_overlay.py", "--install", install_root], capture_output=True)
        ok &= run("effect auto length, replay and live scrubbing",
                  [sys.executable, "tools/test_effect_timing.py", "--install", install_root], capture_output=True)
        ok &= run("ui dialogue lip sync browser", [gui, "--auto", "tests/ui/dialogue_browser.txt"])
        ok &= run("ui dialogue head layouts, eyes and posed pixels",
                  [sys.executable, "tools/test_dialogue_layout.py", "--install", install_root], capture_output=True)
        ok &= run("ui dialogue picker, playback and edit/export workspace",
                  [sys.executable, "tools/test_dialogue_workspace.py", "--install", install_root], capture_output=True)
        ok &= run("Dialogue retail eye defs", [sys.executable, "tools/test_dialogue_eye_defs.py",
                                                "--install", install_root], capture_output=True)
        effects = os.path.join(install_root, "data", "Misc", "pc", "effects.big")
        if os.path.isfile(effects):
            ok &= run("FX normal/compact controls, transport, grid/framing pixels and asset links",
                      [sys.executable, "tools/test_effect_workspace.py", "--install", install_root], capture_output=True)
    ok &= run("ui paths", [gui, "--auto", "tests/ui/paths.txt"])
    ok &= run("ui controls", [gui, "--auto", "tests/ui/controls.txt"])
    ok &= run("ui wheel", [gui, "--auto", "tests/ui/wheel.txt"])
    ok &= run("ui foliage", [gui, "--auto", "tests/ui/foliage.txt"])
    ok &= run("ui region export", [gui, "--auto", "tests/ui/region.txt"])
    ok &= run("ui editor", [gui, "--auto", "tests/ui/editor.txt"])
    ok &= run("ui compact map and viewport layout", [gui, "--size", "800x600", "--auto", "tests/ui/compact_layout.txt"])
    ok &= run("ui thing keyboard transforms", [gui, "--auto", "tests/ui/thing_keyboard_transforms.txt"])
    ok &= run("ui selected radius rings", [gui, "--auto", "tests/ui/radius_rings.txt"])
    ok &= run("ui attachment picker", [gui, "--auto", "tests/ui/attach_picker.txt"])
    ok &= run("ui region entrance attachment", [gui, "--auto", "tests/ui/attach_entrance.txt"])
    ok &= run("ui creature sex definitions", [gui, "--auto", "tests/ui/creature_sex.txt"])
    ok &= run("ui placement", [gui, "--auto", "tests/ui/placement.txt"])
    ok &= run("ui budget", [gui, "--auto", "tests/ui/budget.txt"])
    ok &= run("ui brushes", [gui, "--auto", "tests/ui/brushes.txt"])
    ok &= run("ui area object delete", [gui, "--auto", "tests/ui/clip_delete.txt"])
    ok &= run("ui Escape cancels a write confirmation (selection kept, Enter ignored)", [gui, "--auto", "tests/ui/confirm_escape.txt"])
    ok &= run("ui paint refuses slot 0 and the INVALID_THEME_STANDIN placeholder", [gui, "--auto", "tests/ui/paint_target_guard.txt"])
    ok &= run("ui dropped model fills the import form (nothing imported)", [gui, "--auto", "tests/ui/drop_model.txt"])
    ok &= run("ui texture list thumbnails", [gui, "--auto", "tests/ui/texture_thumbs.txt"])
    ok &= run("ui scale gizmo drag sensitivity, symmetry, Shift and undo", [gui, "--auto", "tests/ui/scale_drag.txt"])
    ok &= run("ui clear all sounds (vanilla EditFillSound), one undo step", [gui, "--auto", "tests/ui/clear_sounds.txt"])
    ok &= run("ui Replace all takes a whole water family (vanilla NoWaterThemeDef)", [gui, "--auto", "tests/ui/replace_water_family.txt"])
    ok &= run("ui whole-map heights: raise with objects, scale, set, undo", [gui, "--auto", "tests/ui/wholemap_heights.txt"])
    ok &= run("ui World tab opens a map and Fit to neighbours", [gui, "--auto", "tests/ui/world_fit.txt"])
    ok &= run("ui owner + day/night", [gui, "--auto", "tests/ui/owner_daynight.txt"])
    ok &= run("ui height pens (vanilla Height Toolbox)", [gui, "--auto", "tests/ui/height_pens.txt"])
    ok &= run("ui terrain follows placed things and foliage", [gui, "--auto", "tests/ui/terrain_follows_objects.txt"])
    ok &= run("ui floating tool section context", [gui, "--auto", "tests/ui/tool_window_context.txt"])
    ok &= run("ui scene browser switches (script-named only, nearest first)", [gui, "--auto", "tests/ui/scene_browser.txt"])
    ok &= run("ui track preview (vanilla Play Track)", [gui, "--auto", "tests/ui/track_preview.txt"])
    ok &= run("ui world view (2D ground tiles + 3D fly-over of every map)", [gui, "--auto", "tests/ui/world_view.txt"])
    ok &= run("ui no-install", [gui, "--auto", "tests/ui/noinstall.txt", "--install", "D:/definitely/not/fable"])
    ok &= run("new level from donor (scratch install)", [sys.executable, "tools/test_newlevel.py"], capture_output=True)
    ok &= run("overworld moves (scratch install + World tab)", [sys.executable, "tools/test_overworld.py"], capture_output=True)
    ok &= run("textures tab (scratch textures.big)", [sys.executable, "tools/test_textures.py"], capture_output=True)
    ok &= run("mod load order (scratch root; skips without the corpus)", [sys.executable, "tools/test_mods.py"], capture_output=True)
    ok &= run("locked same-size mod file refuses comparison and retries after unlock",
              [sys.executable, "tools/test_unreadable_file_compare.py"], capture_output=True)
    ok &= run("Aeon baseline preservation + Controller Support (scratch loose levels; skips without the corpus)",
              [sys.executable, "tools/test_aeon_controller.py"], capture_output=True)
    ok &= run("Freeroam FMP walkability (76 LEVs through scratch WAD; skips without corpus)",
              [sys.executable, "tools/test_freeroam.py"], capture_output=True)
    ok &= run("mod asset health (new missing mesh versus retail, Mods report)",
              [sys.executable, "tools/test_mod_asset_health.py"], capture_output=True)
    ok &= run("backup manager (every suffix, stage revert + rebase)", [sys.executable, "tools/test_backups.py"], capture_output=True)
    ok &= run("Setup restore (scratch loose level, GUI reload)", [sys.executable, "tools/test_setup_restore_ui.py"], capture_output=True)
    ok &= run("Localization backup discovery, text-reader barrier and refreshed restore preview",
              [sys.executable, "tools/test_text_backup_restore.py"], capture_output=True)
    ok &= run("Text content editing, metadata/original preservation and stale-edit refusal",
              [sys.executable, "tools/test_text_content_edit_ui.py"], capture_output=True)
    ok &= run("Donor-based text addition, free IDs and unsafe-addition refusal",
              [sys.executable, "tools/test_text_donor_add_ui.py"], capture_output=True)
    ok &= run("Text speaker picker, narrator indices, metadata preservation and stale-header refusal",
              [sys.executable, "tools/test_text_speaker_picker_ui.py"], capture_output=True)
    ok &= run("Text speech-bank/identifier inputs, ASCII/legacy preservation and stale metadata refusal",
              [sys.executable, "tools/test_text_metadata_ui.py"], capture_output=True)
    ok &= run("Text tag controls, ordering, signed positions and stale/legacy preservation",
              [sys.executable, "tools/test_text_tags_ui.py"], capture_output=True)
    ok &= run("Text metadata byte display/export, valid UTF-8 JSON/TSV and source preservation",
              [sys.executable, "tools/test_text_byte_export.py"], capture_output=True)
    ok &= run("Definition vector controls, exact component preservation and malformed-value refusal",
              [sys.executable, "tools/test_definition_vector_edit_ui.py"], capture_output=True)
    ok &= run("Definition donor cloning, paired publication, rollback and exact Restore",
              [sys.executable, "tools/test_definition_clone.py"], capture_output=True)
    ok &= run("Creature clone selector, verified self-only payloads, paired rollback and Restore",
              [sys.executable, "tools/test_creature_clone_cli.py"], capture_output=True)
    ok &= run("Private Creature CLI ownership, paired publication, rollback and Restore",
              [sys.executable, "tools/test_creature_private_clone_cli.py"], capture_output=True)
    ok &= run("Creature clone controls, draft protection, stale sources and paired Restore",
              [sys.executable, "tools/test_creature_clone_ui.py"], capture_output=True)
    ok &= run("Creature visual graph synthetic ownership observations",
              [sys.executable, "tools/audit_creature_visual_graph.py"], capture_output=True)
    ok &= run("Creature animation synthetic boundaries and malformed component refusal",
              [sys.executable, "tools/audit_creature_animation.py"], capture_output=True)
    ok &= run("Definition clone controls, exact donors, stale drafts and paired Restore",
              [sys.executable, "tools/test_definition_clone_ui.py"], capture_output=True)
    ok &= run("Definition existing-name picker, exact field deltas, invalid offsets and Restore",
              [sys.executable, "tools/test_definition_name_picker_ui.py"], capture_output=True)
    ok &= run("Definition owning names offsets, duplicate names, refresh and source preservation",
              [sys.executable, "tools/test_definition_name_labels_ui.py"], capture_output=True)
    ok &= run("Definition terrain texture IDs, bank scope, ambiguity and protected drafts",
              [sys.executable, "tools/test_definition_texture_ui.py"], capture_output=True)
    ok &= run("Definition object model IDs, numeric names, ambiguity and protected drafts",
              [sys.executable, "tools/test_definition_model_ui.py"], capture_output=True)
    ok &= run("Definition type/group trees, group search, virtualized selection and protected drafts",
              [sys.executable, "tools/test_definition_tree_ui.py"], capture_output=True)
    ok &= run("Definition group navigation, typed targets, numeric names and protected drafts",
              [sys.executable, "tools/test_definition_groups_ui.py"], capture_output=True)
    ok &= run("Definition scalar drafts, real inputs, destinations, stale guards, close review and Restore",
              [sys.executable, "tools/test_definition_edit_ui.py"], capture_output=True)
    ok &= run("Definition group picker, exact indices, target families and stale identity refusal",
              [sys.executable, "tools/test_definition_group_picker_ui.py"], capture_output=True)
    ok &= run("Definition wide Unicode drafts, native encoding, real inputs and protected malformed originals",
              [sys.executable, "tools/test_definition_wide_edit_ui.py"], capture_output=True)
    ok &= run("Definition integer bounds, signed name references and exact field deltas",
              [sys.executable, "tools/test_definition_integer_bounds.py"], capture_output=True)
    ok &= run("Definition native UTF16 terminators, malformed-layout guards and source preservation",
              [sys.executable, "tools/test_definition_wide_layout.py"], capture_output=True)
    ok &= run("Definition inline ASCII strings, real inputs, size boundaries and legacy-byte refusal",
              [sys.executable, "tools/test_definition_string_edit_ui.py"], capture_output=True)
    ok &= run("Definition string terminators, opaque-byte refusal and exact unrelated fields",
              [sys.executable, "tools/test_definition_string_layout.py"], capture_output=True)
    ok &= run("Definition colour picker, real RGBA channels, exact no-ops and invalid-layout refusal",
              [sys.executable, "tools/test_definition_colour_edit_ui.py"], capture_output=True)
    ok &= run("Definition native BGRA colours, RGBA input, component bounds and preserved neighbors",
              [sys.executable, "tools/test_definition_colour.py"], capture_output=True)
    ok &= run("Definition scalar widths, opaque-trailer refusal and preserved source",
              [sys.executable, "tools/test_definition_layout.py"], capture_output=True)
    ok &= run("Definition bank publication, exact name pools, locks, aliases and recovery",
              [sys.executable, "tools/test_definition_publication.py"], capture_output=True)
    ok &= run("Definition field selectors, ambiguous-name refusal and one-field preservation",
              [sys.executable, "tools/test_definition_selector.py"], capture_output=True)
    ok &= run("Definition browser controls, confirmed fields, raw fallback and unchanged source",
              [sys.executable, "tools/test_definition_browser_ui.py"], capture_output=True)
    ok &= run("Text draft close prompt, review/save/cancel/discard and failed-save retention",
              [sys.executable, "tools/test_text_close_prompt_ui.py"], capture_output=True)
    ok &= run("Text pack destination, composition, manifest guards and tracked-folder recovery",
              [sys.executable, "tools/test_text_pack_destination_ui.py"], capture_output=True)
    ok &= run("Text-only tracked-folder Restore, running-owner aliases and stage recovery",
              [sys.executable, "tools/test_text_restore_alias_guard.py", "--text-only"], capture_output=True)
    ok &= run("Text folder creation, recovery tracking and external-destination refusal",
              [sys.executable, "tools/test_text_folder_destination_ui.py"], capture_output=True)
    ok &= run("Setup locked-write error visibility, preserved bytes and same-process retry",
              [sys.executable, "tools/test_setup_restore_failure_ui.py"], capture_output=True)
    ok &= run("Setup partial restore, selected-map refresh and remaining locked target retry",
              [sys.executable, "tools/test_setup_restore_failure_ui.py", "--partial"], capture_output=True)
    ok &= run("Setup ambiguous recovery records, all-file preservation and resolved retry",
              [sys.executable, "tools/test_setup_restore_ambiguity_ui.py"], capture_output=True)
    ok &= run("Setup staged baseline recovery and editor-original rebase",
              [sys.executable, "tools/test_setup_stage_restore_ui.py"], capture_output=True)
    ok &= run("Setup orphaned stage backup visibility, preserved files and recovered-manifest retry",
              [sys.executable, "tools/test_setup_stage_restore_ui.py", "--orphan"], capture_output=True)
    ok &= run("Setup confirmation Enter/Escape, closing panel and install-switch cancellation",
              [sys.executable, "tools/test_setup_confirmation_ui.py"], capture_output=True)
    ok &= run("Setup missing-bank recovery, Data folder normalization and running-game guard",
              [sys.executable, "tools/test_setup_missing_bank_ui.py"], capture_output=True)
    ok &= run("Setup missing loose-level recovery and running-game guard",
              [sys.executable, "tools/test_setup_missing_bank_ui.py", "--missing-levels"], capture_output=True)
    if dialogue:
        ok &= run("Texture PNG export locked-output preservation, visible failure and same-process retry",
                  [sys.executable, "tools/test_texture_export_failure_ui.py", "--install", install_root], capture_output=True)
        ok &= run("Texture browser destination changes, same-bank retention and fallback",
                  [sys.executable, "tools/test_texture_destination_ui.py", "--install", install_root], capture_output=True)
        ok &= run("Mods loaded model/texture views across deploy and undeploy",
                  [sys.executable, "tools/test_mod_asset_refresh_ui.py", "--install", install_root], capture_output=True)
        ok &= run("Mods asset refresh at a separate save root with selected install preserved",
                  [sys.executable, "tools/test_mod_asset_refresh_ui.py", "--install", install_root,
                   "--separate-root"], capture_output=True)
        for options, label in [([], "selected install"), (["--separate-root"], "separate save root")]:
            ok &= run("Mods failed redeploy asset refresh at " + label,
                      [sys.executable, "tools/test_mod_asset_refresh_ui.py", "--install", install_root,
                       "--failed-redeploy", *options], capture_output=True)
        stock_wad = os.path.join(install_root, "data", "Levels", "FinalAlbion.wad")
        if os.path.isfile(stock_wad):
            ok &= run("Setup missing stock-WAD recovery and running-game guard",
                      [sys.executable, "tools/test_setup_missing_bank_ui.py", "--wad",
                       stock_wad], capture_output=True)
        else:
            print("Setup stock-WAD recovery skipped (no stock WAD)")
        ok &= run("Setup restore after model import, refreshed asset views and exact banks",
                  [sys.executable, "tools/test_setup_asset_restore.py", "--install", install_root], capture_output=True)
    ok &= run("Install switch (scratch roots, unsaved edit and invalid path)",
              [sys.executable, "tools/test_install_switch_ui.py"], capture_output=True)
    ok &= run("Project Seasons bank preservation + level/WAD fixture without graphics (skips without the pack)", [sys.executable, "tools/test_gbpack.py", "--skip-graphics"], capture_output=True)
    ok &= run("Seasons/AlbionSecrets bank preservation + overlapping level/WAD fixtures without graphics (skips without both)",
              [sys.executable, "tools/test_gbpack.py", "--pack", "both", "--skip-graphics"], capture_output=True)
    ok &= run("Dragon Cliff Restored (root WAD levels and asset references; skips without the archive)",
              [sys.executable, "tools/test_dragoncliff.py"], capture_output=True)
    ok &= run("Lost Content refusal + content-only WAD payloads (skips without the archive)",
              [sys.executable, "tools/test_lost_content.py"], capture_output=True)
    ok &= run("Expanded Chapters bank/text preservation and WAD payloads (skips without both archives)",
              [sys.executable, "tools/test_expanded_chapters.py"], capture_output=True)
    ok &= run("custom mesh import (.obj + .glb cube into scratch banks)", [sys.executable, "tools/test_meshimport.py"], capture_output=True)
    ok &= run("custom theme import (base/cliff textures, failure rollback)", [sys.executable, "tools/test_custom_theme.py"], capture_output=True)
    ok &= run("custom theme full-palette refusal (synthetic GUI fixture)", [sys.executable, "tools/test_custom_theme_ui.py"], capture_output=True)
    ok &= run("recipe packs (forge_pack.json models + themes composed at build, order swap, deploy/undeploy)", [sys.executable, "tools/test_recipe_packs.py"], capture_output=True)
    ok &= run("loose-level install (extracted levels, no FinalAlbion.wad)", [sys.executable, "tools/test_loose_install.py"], capture_output=True)
    ok &= run("tall terrain edit (grown patches re-laid, LOD patches re-sampled)", [sys.executable, "tools/test_tall_terrain.py"], capture_output=True)
    ok &= run("level edits into a mod pack (.lev/.tng + static-map chunk; mods build lays them in; install untouched)", [sys.executable, "tools/test_pack_levels.py"], capture_output=True)
    ok &= run("world edits in mod packs (captured shadows; per-record BWD/WLD merge equals the sequential edit; new level STB/WAD)", [sys.executable, "tools/test_pack_world.py"], capture_output=True)
    ok &= run("mods tab as a plugin list (masters set in the GUI and flagged, wins/loses badges, build reports masters)", [sys.executable, "tools/test_mods_masters.py"], capture_output=True)
    ok &= run("docs name real commands", [sys.executable, "tools/check_docs_commands.py"], capture_output=True)
    print("ALL PASS" if ok else "SOME CHECKS FAILED")
    return 0 if ok else 1

if __name__ == "__main__":
    sys.exit(main())
