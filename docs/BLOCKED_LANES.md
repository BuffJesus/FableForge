# Blocked lanes: research and plans (2026-09-28)

Minimap, water, navigation and particle emitters: what exists, what the engine / vanilla editor does, what blocks it,
and an ordered plan. Each lane was researched from the code and data and then checked by a second agent; its
corrections are listed under each lane and override the text above them. Evidence files are in the session scratch
(lanes/<topic>/), summarised here.

## MINIMAP

**Current state.** WHAT EXISTS IN FABLEFORGE (verified from code and git):
- Bake: src/worldedit.cpp bakeMinimapImage (line 268) and bakeMinimapTexture (line 340). It uses top-down ground albedo from albion::terrainexport::buildScene, a LEV hillshade (clamped 0.55..1.35) and a linear alpha fade (opaque to r~112.6 px, 0 at 128 px). CLI src/cli/textures.cpp:126 (`forge minimap-bake <map> <out.png> [--region R | --framing s,x,y]`).
- Framing: libs/forgecore/include/forge/minimapframe.hpp + src/minimapframe.cpp. This is the inverse of CTCInventoryMap::GetRelativePosOnMiniMap, with centred(W,H) writing MiniMapScale 1.0 plus centring offsets. The commits (001f6b8 fix(minimap), 8774a33 in-game verified) exist ONLY on the local branch feat/editor-ui-shell. 001f6b8 is not on origin yet. 0872942 (docs) is on origin/feat/editor-ui-shell.
- Texture register and append: EDITOR.md "Own region + minimap". MINIMAP_<LEVEL> is appended to textures.big and registered in PLAYER_GUI_PC/DEFAULT.MiniMapGraphics. This superseded the old slot takeover.
- Docs: ROADMAP_1.0.md row 26 is Done, including the in-game check of 2026-09-28 (hero marker on the art for retail BanditCampPath1 and for a 256x64 takeover region). VANILLA_EDITOR_INVENTORY.md section 10 has "Region minimaps" and "Minimap framing". DEBUG_EDITOR_FEATURE_AUDIT.md line 52 marks engine-style generation "Partial".
- Not present: MinimapTheme decoding. libs/forgecore/src/terraintex.cpp ThemeLibrary decodes only the 6 texture ids plus WaterType and WaterHeight. The field IS in the schema: docs/re_reference/def_schema.json CEngineThemeDef lists 'MinimapTheme'. Also not present: any class-based or painted style, buildings on the minimap, and the retail alpha profile.
- The water, feat/nav-thing-lines and fix/overworld-neighbour-grow branches carry no minimap work (inferred from `git log --all --grep=minimap`: every minimap commit is on feat/editor-ui-shell or merged history).

MINIMAP ZOOM: there is nothing left to fix, and the engine has no zoom (verified from code, FableWin decomp below).
- The ForgeTest64 note "minimap overflows its frame (scale field)" was the framing bug. The retail HUD draws the WHOLE 256 texture into its box, and MiniMapScale/Offset move only the markers. Row 26 fixed this and it was verified in-game.
- One real residual (verified from code): the HUD and inventory marker formulas disagree when scale != 1. That is harmless while FableForge writes scale 1.0.

Scratch evidence is in C:\Users\Cornelio\AppData\Local\Temp\claude\D--Documents-FableTLC\130e9b68-87fa-44cd-90d1-1630b5eeb085\scratchpad\lanes\minimap\:
- draw.c: FableWin decomp of 8 functions
- tex/*.png: 24 retail MINIMAP_* exports
- sheet.png, compare.png: retail vs prototype
- clusters.py/.json: per-map colour classes
- overlay.py, parchment_T.npy, parchment_template.png: the shared parchment layer
- classmap.py, classmap2.py: engine class vs painted art
- paint.py: working painted-style prototype
- albedo_*.png: the current bake, for the score

**Vanilla / engine.** RUNTIME DRAW PATH (verified from code: FableWin headless-Ghidra decomp, draw.c):
- CDrawMiniMap::Draw 0x020ddc2c
  - Closed: the HUD box is anchored top-right with size PLAYER_GUI_DEF(+0xa9c,+0xaa0). By the Ego_r PDB this is inferred to be CPlayerGuiDef.MiniMapScreenSize (Ego_r offset 2552; the debug-build layout is shifted).
  - Open (the toggle, or region load via CDrawMiniMap::Update 0x020df9e3): it lerps by the fade alpha to a centred square of side renderTargetW*256/640.
- CMiniMapDisplay::DrawMap 0x027d3c00 emits ONE CEnginePrimitive2DSprite of the region texture (this+0x2c) over the whole box. There is no crop, zoom or scale.
  - Hard-coded special case: MINIMAP_PRISONCOURTYARD1/2 are swapped when flags this+0x5c..0x5f are set, on hero z<=51 or y>4740. So PRISONCOURTYARD2 is not truly unused (verified). The old slot takeover of it was risky; the append approach avoids it.
- CMiniMapDisplay::GetDrawPosition 0x027d3990 places markers with:
  - box' = box with its max scaled by CRegion::GetMiniMapScale (CRegion+0x4c, FableWin 0x01c7f7c0)
  - x = floor(minx + ext'x·relSize.x·u) + (g.x + offX)·W/256
  - y = floor(miny + ext'y − ext'y·relSize.y·v) + (g.y + offY)·H/256
  - The offsets are NOT multiplied by scale. g is a .bss C2DVector at 0x04abca88 that reads 0 in the file; that it stays 0 at runtime is inferred.
  - relSize comes from GetRelativeMapSize (retail 0x829c70) = (1, short/long). u,v come from GetRelativePosOnMap (retail 0x829ce0) = (pos−regionMin)/extent. Both are landed byte-exact in FableTLC rebuild/src/compiled/00/82/.
- CTCInventoryMap::GetRelativePosOnMiniMap 0x020931b0 (pause map) computes centred pixel = (s·u'−0.5)·texW + s·offX and (s·(1−v')−0.5)·texH + s·offY. Here the offsets ARE scaled.
  - A test on the art: GreatwoodLake (s 0.75, off 31,70) gives walkable IoU 0.56 with the inventory formula and 0.18 with the HUD formula. So the art was painted in the inventory frame; I inferred this from the fit. BarrowFields: 0.62 vs 0.58.

GENERATOR (from FableForge inventory section 10, not re-decompiled by me):
- CRegionMinimap::PopulateRegionMinimap 0x02186790, BuildRegionMinimapTGA 0x021886b0, illumination 0x021878d0, CMinimapData::BuildMinimapTGA 0x0218cb20. It is debug-only (GenerateMiniMaps).
- Output: a 9-colour MinimapThemeColours class map plus normal lighting. None of those colours appear in retail art.
- Ego_r PDB CPlayerGuiDef also holds an older passability palette (verified from struct_layouts_egor.tsv): MiniMapPassableColour@2576, Impassable@2580, Water@2584, Mapless@2588, ImpassableWater@2592, MiniMapWorldResolution@2572, MinimapThemeColours@2620, MiniMapScreenRadius@2560.
- The EMinimapThemeType enum is NONE, GRASS, WATER, SNOW, CLIFF, EARTH, FOLIAGE, WOOD, BUILDING. In build/defc_copy/engine_theme.def the per-theme counts are EARTH 125, GRASS 97, FOLIAGE 91, WATER 69, CLIFF 51, SNOW 24, WOOD 8.

RETAIL ART MEASURED (verified by measurement on 24 exported MINIMAP_* textures, 256x256 RGBA):
1. The alpha disc is byte-identical in all 24: 255 to r=105, 252@110, 212@115, 127@120, 42@125, 15@128, 1.7@130.
2. A SHARED PARCHMENT LAYER (scratches and stains) is multiplied over every map.
   - The high-pass grain correlates at a median of 0.70 between maps on the joint background (p10 0.27, p90 0.82).
   - The extracted log-luminance template explains 25–73% of background variance on outdoor maps. It fails on the Arena and the Hobbe cave.
3. The painted bright "path" class is the LEV WALKABLE mask (cell byte +15), NOT the EARTH theme class:
   - PicnicArea: walkable IoU 0.66, EARTH IoU 0.09
   - BarrowFields: 0.62 vs 0.21
   - GreatwoodLake: 0.56
   - HookCoast: 0.40 (buildings cut into it)
4. WATER-class cells are painted water 91–95% of the time. Painted water is wider than the class.
5. The path and water outline is about 1–2 px at 0.72–0.79 × background RGB.
6. There is no visible height shading (inferred from the images and the variance split).
7. Per-region tints (k-means centres):
   - olive Greatwood/Picnic/Orchard/Lookout: bg #AEB16B, path #FEFED8, water #B4D4BA, cliff #5C5B34
   - ochre Oakvale/Barrowfields/Oakbay: #CCB86F / #FCFCD9, buildings brown #705629–#88703F
   - sage Witchwood/Knothole/Gibbet: #99B078 / #F7FDE2
   - teal-grey Darkwood: #94A7A1 / #F3FBF5, water #95C3E6
   - slate Graveyard: #8F9E9E, buildings #48556A
   - neutral grey Snowspire/NorthernWastes/HookCoast/LostBay: #A2A6A2 / #EEEAE8, water #A7C4E9, buildings #575A73
   - rust BanditCamp: #B5906A
   - mauve Hobbe cave: #C7ADB2
8. Buildings are dark translucent silhouettes. Some shops are saturated blobs with black outlines; their meaning is unknown.

PROTOTYPE (scratch paint.py, verified by measurement): walkable + WATER class + presets + outline + parchment + retail alpha, framed per region. Blurred MAE vs retail:
- PicnicArea 7.4 (current albedo bake 59.8)
- BarrowFields 12.5 (86.2)
- GreatwoodLake 7.8 (47.5)
Caveat: this is in-sample, because the presets and parchment came from these same maps.

**Blockers.** - The retail art is hand-painted, so no engine-exact bake exists (verified in FableForge inventory section 10 and by my palette check). "Faithful" can only mean engine data for the shapes (walkable mask, MinimapTheme, building footprints, the engine frame) plus a measured painted style for the colours. The colours are measured, not engine constants, and must be labelled that way.
- The parchment layer and alpha disc come from retail bytes. They must be pulled from the user's install at bake time and cached outside the repo. They must never be committed or shipped.
- Buildings: the engine uses a physics-mesh segment test in building bounding boxes. FableForge would need TNG building things plus mesh bounding boxes (the nav-thing-lines branch has physics outline extraction, still unmerged). The coloured shop blobs have no known data source.
- Picking a preset for a custom level is a heuristic (dominant ENGINE_THEME GroupDef such as TG_SNOWSPIRE → preset). This is inferred.
- Multi-map regions need the union box, and minimapframe already handles it. My measurements used single-map regions only. The LEVs I used were D:\tmp\addsound_out copies; I assume their themes and walkable data match retail.
- The minimap-framing commits are local-only on feat/editor-ui-shell. Any painted-style work stacks on them.
- None of this was run in-game. Launches are user-coordinated.

**Plan.**

1. 1. Decode MinimapTheme in forge::terraintex::ThemeLibrary. In libs/forgecore/src/terraintex.cpp near line 375, add fieldInt32(decoded,"MinimapTheme") → ThemeEntry.minimapTheme using the EMinimapThemeType order. Add a unit test: a sample of retail game.bin themes should equal the engine_theme.def values (e.g. GROUND_PATH_DRYMUD_GREEN=EARTH, WATER_BWLAKE_0=WATER, CLIFF_RUGGED_02=CLIFF). *(effort 1-2 h, value The engine's own per-cell class source, the same field CRegionMinimap reads at +0x98. Needed by the painted and debug styles.)*
2. 2. New forgecore module forge/minimappaint. It rasterises classes through minimapframe::fromPixel:
- walkable = LEV cell +15
- water = MinimapTheme WATER or WaterType!=0
- impassable CLIFF = dark tint
- a buildings layer, filled later by step 5
Clean the masks: Gaussian σ≈1.5–3, threshold 0.5, drop islands under about 12 px, fill small holes. Palette: the preset table measured above, stored as data with provenance. Paint a 1–2 px outline at 0.75× background around walkable, water and buildings. Remove the hillshade in this style. *(effort 1-1.5 days, value Closes the colour gap. The prototype cut blurred MAE vs retail from 48–86 to 7–13.)*
3. 3. Install-derived style assets, extracted at bake time and cached under %TEMP%/FableForge/minimap:
(a) the alpha disc, copied from any retail MINIMAP_* (all 24 checked are byte-identical). This replaces the linear fade at worldedit.cpp:326.
(b) the parchment layer: the median over retail MINIMAP_* of log(L / per-map background median) on background pixels, as in overlay.py. It is multiplied into the painted image.
If the install lacks them, fall back to a flat disc with no grain. Never commit these outputs. *(effort 0.5 day, value The retail look (grain, stains, exact vignette) with no shipped retail bytes. The alpha becomes exact instead of within 2.6 levels.)*
4. 4. Style selection plumbing. Add `forge minimap-bake --style painted|albedo|engine` and `--palette olive|ochre|sage|teal|slate|grey|rust|mauve`. Default the palette from the dominant ENGINE_THEME GroupDef of the level's themes, via a small table built from which retail regions use which TG_* group. Add a style and palette dropdown to the New level / own-region card, with a live 256 px preview. *(effort 0.5-1 day, value Authors get retail-looking minimaps by default and can still pick the old photo style.)*
5. 5. Buildings layer. For each TNG thing whose def is a building (or has a physics mesh), rasterise the rotated footprint (mesh bbox, later the physics outline from feat/nav-thing-lines) as the preset's dark building colour at about 85% opacity with an outline. This follows the engine generator's class 8 (inventory: physics bbox segment test). Skip the saturated shop blobs, or offer them as a user tag. *(effort 1 day (+0.5 once nav-thing-lines lands), value The town maps (Oakvale, HookCoast, Bowerstone-like custom levels) read like retail. It also lifts walkable IoU where buildings cut the paths.)*
6. 6. Optional `--style engine` debug look: a faithful port of BuildRegionMinimapTGA. Use the 9-colour MinimapThemeColours from PLAYER_GUI_PC plus the lit-class lerp toward 0.8× by dot(face normal, illumination dir), both already decoded in inventory section 10. Re-decompile 0x02186790, 0x021886b0 and 0x021878d0 for the exact lighting constants before coding. *(effort 0.5-1 day, value An engine-exact reference and debugging aid (the vanilla dev generator). Low player value.)*
7. 7. Scoring harness tools/minimap_style_score.py. For 8+ single-map retail regions (PicnicArea, BarrowFields, GreatwoodLake/Greatwood_3, HookCoast, LookoutPoint, Darkwood, Witchwood_2, BanditCampPath1), bake painted with the retail region's own framing. Report blurred MAE and walkable IoU vs the retail texture. Use LEAVE-ONE-OUT parchment and presets so the score is out-of-sample. Gates: MAE < 15, IoU ≥ 0.55. Add a CTest on forgecore paint determinism and outline width. *(effort 0.5 day, value Guards against regressions and makes "looks like retail" measurable, not a matter of taste.)*
8. 8. Zoom close-out, no engine change needed. Document the facts in inventory section 10: the HUD draws the full texture (DrawMap 0x027d3c00, CDrawMiniMap::Draw 0x020ddc2c), there is no zoom, and scale/offset move only the markers. Also document the HUD-vs-inventory formula split when s≠1 (GetDrawPosition 0x027d3990 leaves the offset unscaled; GetRelativePosOnMiniMap 0x020931b0 scales it). Make region-props warn when a custom region gets MiniMapScale≠1. Keep centred() at s=1. Retire the ForgeTest64 memory item "minimap zoom". *(effort 1-2 h, value Stops a third blind attempt at a zoom that does not exist and prevents marker drift for anyone who sets a scale.)*
9. 9. In-game check with the existing harness, only when the user says the game is free. Bake painted for a 256x64 takeover region and a dedicated region, then screenshot the HUD closed and open next to retail PicnicArea. Then update ROADMAP row 26 with a new row for "painted style" and push feat/editor-ui-shell. *(effort 0.5 day (user-gated), value Proves the look in the real renderer (DXT3 compression of the grain, alpha).)*

**Confidence.** High for the draw path: the HUD draws the full texture and has no zoom; the scale sits at CRegion+0x4c; the HUD/inventory formula split; the PRISONCOURTYARD special case. All of this is verified from the FableWin decomp.
High for these measurements on 24 retail textures: the byte-identical alpha disc, the shared parchment layer, painted bright = LEV walkable (IoU 0.56–0.66 on 3 of 4 single-map regions), the outline ratio of about 0.75, and the per-region tints.
Medium for the painted-style design. The prototype scores well but in-sample. Preset choice is heuristic, and buildings and shop blobs are unsolved.
The generator palette and illumination details come from FableForge docs; I did not re-decompile them.
I used D:\tmp\addsound_out LEVs as stand-ins for retail.
Nothing was run in-game, and no repo or install file was modified.

**Verifier.** corrections needed: Most of the lane holds up. One central claim is wrong: the idea that the HUD and inventory marker formulas split when the scale is not 1. That error spreads into the "residual", the IoU evidence and plan step 8.

CONFIRMED (checked against code and data):
- FableForge is on feat/editor-ui-shell. bakeMinimapImage is at worldedit.cpp:268 and bakeMinimapTexture at :340. The hillshade clamp is 0.55..1.35 (:316). The CLI is src/cli/textures.cpp:96-126. minimapframe.hpp/.cpp exist.
- Commits 001f6b8 and 8774a33 are only on the local feat/editor-ui-shell. 0872942 is also on origin/feat/editor-ui-shell. `git log --all --grep=minimap` shows no minimap work on water, nav-thing-lines or overworld-neighbour-grow.
- terraintex.cpp decodes only the 6 texture fields, WaterType and WaterHeight (lines 360-378), not MinimapTheme. def_schema.json:1428 has MinimapTheme as enum EMinimapThemeType.
- ROADMAP_1.0 row 26 is Done and was verified in-game on 2026-09-28. DEBUG_EDITOR_FEATURE_AUDIT line 52 says "Partial".
- CPlayerGuiDef offsets from struct_layouts_egor.tsv are right: MiniMapScreenSize@2552, ScreenRadius@2560, WorldResolution@2572, Passable@2576, Impassable@2580, Water@2584.
- The engine_theme.def counts are exact: EARTH 125, GRASS 97, FOLIAGE 91, WATER 69, CLIFF 51, SNOW 24, WOOD 8. The file is D:\Code\FableForge\build\defc_copy\engine_theme.def, in the FableForge tree, not FableTLC.
- All 24 tex/*.png have byte-identical alpha. My ring means match the quoted profile: 255 to r=105, then 252.1, 212.2, 126.9, 42.0, 15.3 and 1.7.
- DrawMap 0x027d3c00 draws a single CEnginePrimitive2DSprite of this+0x2c over GFToScreen(box), with no crop or zoom. GetMiniMapScale reads CRegion+0x4c (0x01c7f7c0). CDrawMiniMap::Draw uses GUI_DEF +0xa9c/+0xaa0 and renderW*256/640.
- GetRelativeMapSize 0x829c70 and GetRelativePosOnMap 0x829ce0 are landed under rebuild/src/compiled/00/82/.

CORRECTIONS:
1. (Major, checked with capstone on FableWin 0x027d3990-0x027d3b54) GetDrawPosition DOES scale the offsets.
   - At 0x27d3a0f-0x27d3a40 it scales the local box copy [ebp-0x28] in place: max = min + s·(max−min).
   - The offset term calls GetWidth and GetHeight on that same scaled box: `lea ecx,[ebp-0x28]; call 0x17c0f1f` at 0x27d3af1 and `call 0x1841bf6` at 0x27d3b22.
   - So the term is (g+off)·(s·W)/256, not (g+off)·W/256. Ghidra's decomp hid the ECX receiver.
   - In 256-texel units the HUD gives x = s·(256·relx·u + offX) and y = s·(256·(1−rely·v) + offY), measured from the box min.
   - The inventory (0x020931b0) gives (s·u'−0.5)·256 + s·offX and (s·(1−v')−0.5)·256 + s·offY, with u' = relx·u and v' = rely·v.
   - These are the same transform except for the −128 centre origin.
   - Consequences:
     - There is NO HUD/inventory disagreement at s≠1.
     - The "harmless residual" does not exist.
     - Plan step 8 should drop the formula-split documentation and the region-props warning for MiniMapScale≠1. Keep only the "no zoom, the full texture is drawn" close-out.
     - The GreatwoodLake IoU of 0.56 vs 0.18 used an unscaled-offset "HUD" variant that is not the engine. It proves nothing about which frame the art was painted in. No script for that variant survives in the scratch folder; classmap/classmap2/paint.py all use the scaled inventory form.
     - minimapframe.cpp (px = s·size·u + s·offX) matches both formulas.
2. g at 0x04abca88/0x04abca8c is loaded with `fild dword`. It is an integer pair (a long vector), not a float C2DVector.
3. relSize is not always (1, short/long). The landed code returns (1, h/w) when w>h, (w/h, 1) when w<h, and (1,1) when square. So the long axis gets 1 and the short axis gets short/long.
4. The PRISONCOURTYARD logic is misstated (DrawMap, draw.c lines 75-190). The texture swap depends only on hero z against 51:
   - With flag 0x5c set and z>51, it loads MINIMAP_PRISONCOURTYARD2 and sets 5c=0, 5d=1.
   - With flag 0x5d set and z<=51, it loads COURTYARD1 and sets 5c=1, 5d=0.
   - The 0x5e/0x5f pair only toggles on y ≤/> 4740 and changes NO texture in DrawMap.
   The conclusion stands: COURTYARD2 is really used, so taking over its slot was unsafe.
5. The linear alpha fade is at worldedit.cpp:330-333: (0.5−r)/0.06, opaque to 0.44·256 = 112.6 px. It is not at line 326. The numbers themselves are right.
6. MinimapTheme is at CEngineThemeDef +0x94 in the retail/Ego_r layout (fable_types.h:51252). The +0x98 in the inventory is the FableWin debug-build layout. Step 1 decodes by field name, so this doesn't break it, but label the two offsets so they aren't mixed up.
7. The step-7 scoring harness should compare against the engine transform above, used as a single formula.

MISSED:
- The ForgeTest64 memory file already says "Do not guess the minimap zoom again". Step 8's close-out should update that memory line: the HUD draws the full texture, and scale and offset move the markers only.
- The HUD sprite is drawn with colour 0x80,0x80,0x80 and blend/interp 2 (DrawMap), and the marker layer is drawn by DrawMarkers after it. Check the 0x80 modulate before the in-game colour comparisons in step 9.
- The painted-style design itself (walkable mask, parchment, presets, 24-texture alpha) is unaffected by these corrections.

Checking script: C:\Users\Cornelio\AppData\Local\Temp\claude\D--Documents-FableTLC\130e9b68-87fa-44cd-90d1-1630b5eeb085\scratchpad\lanes\minimap_check\mmdis.py

## WATER

**Current state.** WHAT EXISTS IN FABLEFORGE (all verified from git and code)

On the current branch, feat/editor-ui-shell (HEAD 9dc777e):
- There is no water editing tool.
- The vanilla Land-tab height pens are ported in libs/forgecore/include/forge/heightpen.hpp: changeHeight, paintHeight, smear, noise and heightAddition, called from src/leveledit.cpp:538-543. The header says outright that the water re-fit done by EditSetGroundSizeZAtBlockUndoable is "not reproduced here".
- The terrain exporter already previews painted water: src/terrainexport.cpp:661-700 uses PeekWaterDepth, PeekInterpolatedWaterHeight and ice.
- libs/forgecore/src/terraintex.cpp:375 reads WaterType.
- Document::addGroundTheme(name, defIndex) and paletteSlotOf (src/leveledit.hpp:440,453) can put any ENGINE_THEME into the 256-slot LEV palette.
- The LEV stores slot-0 and slot-1 strengths only; slot 2 is the implicit remainder (libs/forgecore/include/forge/lev.hpp:18,106). Vanilla works the same way.
- docs/VANILLA_EDITOR_INVENTORY.md:189-192 lists the water rows at inventory level only; no behaviour is recovered there.

On branch `water` (11 commits on merge-base 001dcde, tip 81b64ff; the current branch is 128 commits past that base; the branch was never seen in-game):
- src/stbwater.{hpp,cpp}: writes CWaterPatchMesh (the foreground water mesh) and the background water sub-patch.
- stbheightbake: water providers plus foreground-frame growth (the bake can grow a patch's foreground frame).
- stbterrain.{hpp,cpp}, terrainexport buildWaterLevels, and `forge water-audit` in src/cli/export.cpp.
- rangecodec diagnostics, tests/ui/water.txt, and tools/test_water.py (in check_all; it paints a pond on BanditCampPath_1 into a scratch install).
- A Terrain-tab "Water" brush: TerrainBrush::Mode::Water with waterAltitude and waterRungs, in `git show water:src/leveledit.cpp`, applyBrush. It is a radius brush, not the vanilla flood fill.
- docs/EDITOR.md "## Water" and ROADMAP_1.0 section 0.18 describe the state: steps 1 and 2 are done offline; the background sub-patch is written only when the background frame's fixed slot has room; there is no sea body, foam or shore data.

Range-codec note (verified from the commit message of 81b64ff): 65% of retail water blocks re-encode byte-exact. Every miss is an older compressor's cost model splitting one 4-byte block differently. Our blocks always decode. This does not block anything.

Merge dry run (`git merge-tree --write-tree HEAD water`, read-only, verified): 10 conflicted files with 14 hunks.
- CMakeLists.txt (1)
- docs/AUTOMATION.md (1)
- gui/app.hpp (1)
- gui/editor.cpp (4)
- libs/forgecore/include/forge/stbheightbake.hpp (1)
- libs/forgecore/src/stbheightbake.cpp (1): the current branch's `retarget` lambda against water's background-water trailer block, around line 588 of the merged tree
- src/cli/export.cpp (1)
- src/leveledit.cpp (1)
- src/leveledit.hpp (2): the branch's TerrainBrush::Mode enum against the current one, which now has Noise and HeightKey from the heightpen port
- tools/check_all.py (1)

Everything else merges cleanly: stbwater, stbterrain, terrainexport, rangecodec and the docs.

Ghidra note: headless Ghidra could not be used because D:\Documents\FableTLC\ghidra_proj was locked by another running java process (LockException). Everything below comes from capstone disassembly of FableWin.exe, with names from ghidra_out/fablewin_pdb_names.tsv and incremental-link thunks resolved. The scratch tools are in C:\Users\Cornelio\AppData\Local\Temp\claude\D--Documents-FableTLC\130e9b68-87fa-44cd-90d1-1630b5eeb085\scratchpad\lanes\water\ (wdis.py, filt.sh, callers.py, and the f_*.txt dumps).

**Vanilla / engine.** Everything in this section is verified from FableWin disassembly unless it is marked (inferred). The debug build's CEngineThemeDef offsets are the retail ones plus 4: 0x74 WaterHeight, 0x78 WaterType, 0x7c NoWaterThemeDef, 0x84 GroupDef. They match the assert strings. DNZ_FOR_HEIGHTS is 1e-4 (its dynamic initializer at 0x03ff92f0 loads 0x44bda4c).

## Dialog: how clicks reach the water tools

PaintInputPaintMap 0x029916a0 checks, in this order:
1. RiverFlag: PenEditRiver.
2. DrawPaths.
3. (LakeFlag or SeaFlag) and a fresh press: PaintInputFloodFillStaticWaterAt(pen with pen.+4 = GetWaterFillAltitude, block = round(pos)).
4. WaterAutoremoveFlag and a fresh press: PaintInputFloodRemoveWaterAround.

A "fresh press" is `!argC && !arg10` (inferred to mean press, not held or released). There is no Ice branch: ICE_FLAG only filters the theme list.

Control names: LAKE_FLAG, SEA_FLAG, ICE_FLAG, RIVER_FLAG, WATER_AUTOREMOVE_FLAG, WATER_FILL_ALTITUDE_BOX (an ABSOLUTE altitude), RIVER_HEIGHT_BOX_NAME (height RELATIVE to the waypoint ground), and the WATER_TG listbox.

Themes combo: InitWaterThemeListBox 0x02906b80 lists every ENGINE_THEME_GROUP whose name StartsWith a prefix. Which prefix depends on the Lake, Ice, River or Sea flag; the prefixes are stored at dialog+0x15c, 0x160, 0x164 and 0x168. The strings in the binary are TG_WATER_LAKE, TG_WATER_ICE, TG_WATER_RIVER and TG_WATER_SEA; that these are the stored values is inferred (strong).
- GetWaterThemeGroupIndex 0x0290a6d0 = GetDefGlobalIndexFromName(the selected text).
- Retail data, verified with `forge def-groups ENGINE_THEME` and names.bin: the only water groups are TG_WATER_LAKE_{BRIGHTWOOD, DARKWOOD, ICE_HOOKCOAST, KRAKEN_CHAMBER, WITCHWOOD} and TG_WATER_SEA_{HOOKCOAST, OAKVALE}.
- So with retail data, vanilla's River and Ice lists are EMPTY. WATER_LAKE_* and WATER_RIVER_* belong to TG_DO_NOT_USE_1 (GroupDef 2322), and INVALID_THEME_STANDIN is in the same group.

Pickup on the Water tab (PaintInputPickupHeight 0x029940d0): the fill box becomes ground(pos) + GetRelWaterHeightAtBlock.

## Per-block primitives (CEditWorldMap)

InitWaterDefList(group) 0x02976700
- Clears the map<float, def> at this+0x160.
- For i = 1..count(ENGINE_THEME): if def.GroupDef == group, insert (WaterHeight → global index). std::map keeps the first entry for a duplicate height.
- The last group is cached at this+0x174.

GetRelWaterHeightAtBlock 0x02976cb0
- Loops over slots 0..2, skipping theme ≤ 0.
- The blend is GetThemeBlendAtBlock for slots 0 and 1; slot 2 uses 255 − b1 − b0.
- Returns Σ(WaterHeight · blend) / 255.0.

GetWaterTypeAtBlock 0x02977140 returns slot 0's WaterType.

GetWaterThemeGroupAtBlock 0x029771f0 returns 0 if slot 0's WaterType is 0, otherwise slot 0's GroupDef.

EditSetRelWaterHeightAtBlockUndoable(pos, rel, group, txn) 0x029772f0
1. If rel > 16: show "The highest that water can reach above ground level is currently 16" and return.
2. If rel < DNZ: return.
3. If group changed, call InitWaterDefList.
4. water = lower_bound(rel)->second, i.e. the smallest WaterHeight ≥ rel. Asserts that one exists and that it is > 0.
5. SetTheme(slot 0 = water).
6. SetTheme(slot 1 = water.NoWaterThemeDef).
7. b = GFFloatToLongNear(rel · 255.0 / water.WaterHeight), asserted 0..255.
8. SetBlend(slot 0 = b), SetBlend(slot 1 = 255 − b). Slot 2's theme is not touched.

Retail NoWaterThemeDef always points at the family's 0-rung (verified with `forge-tools defs decode`): WATER_LAKE_* → WATER_LAKE_0 (2298), WATER_RIVER_* → WATER_RIVER_0 (2304), SEA_OAKVALE_* → SEA_OAKVALE_0 (1945), WATER_BWLAKE_* → WATER_BWLAKE_0 (1933).

## Flood fill

MapAround8 at 0x4ac55a0 (dynamic initializer 0x3ff9420), as (dx, dy): (0,−1), (1,−1), (1,0), (1,1), (0,1), (−1,1), (−1,0), (−1,−1).

IsPosValidFloodFillStartSite 0x02978130
- IsPosInMap, and IsPosChangeable (vtable slot 0x64 = 0x2974f50; the script-brush / editable-map test).
- Inside the mask box, if there is one.
- absH − ground > DNZ.

IsPosValidFloodFillRecursionSite 0x029781e0: the same tests, but if the block's water group == group and |ground + rel − absH| ≤ 0.05, the block is invalid (already filled).

EditFloodFillWaterUndoable(absH, pos, group, mask, txn) 0x02978300 is a DFS on a std::list stack:
- cur = back().
- Grow the dirty box, which starts at pos ± 1.
- SetRelWater(cur, absH − ground(cur), group).
- Push the first valid neighbour in MapAround8 order, or pop if there is none.
- At the end: clip the box to GetDimensions, call HeightAreaChanged(box), return the box.

(inferred) A reachable block more than 16 deep is refused by SetRelWater but stays a valid recursion site, so two such blocks would re-push each other. Vanilla would loop or hang. The port must refuse or clamp that case.

PaintInputFloodFillStaticWaterAt 0x02992930
1. Group 0: show "Select a theme".
2. Start site invalid: show "The ground is too high, or this point is outside a script brush".
3. Ask "Fill with water of height " + round(absH, 2) + "?" (MB_YESNO 0x34, IDYES = 6).
4. Backup.
5. If the block is already wet (IsPosValidFloodRemoveSite): run EditFloodRemoveWater first, then the fill.
6. SetLevelsInAreaAsChangedSinceLoad for both boxes.

## Flood remove

IsPosValidFloodRemoveSite 0x02979200: in map, changeable, inside the mask, and water group > 0.

EditFloodRemoveWaterUndoable 0x029792a0: the same DFS with any water group (not only the clicked one). Each block gets slot 0 = INVALID_THEME_STANDIN (a GetDefGlobalIndexFromName lookup; it exists in retail names.bin with WaterType 0), slot 1 = 0, blends 255 / 0. The original ground theme is NOT restored.

PaintInputFloodRemoveWaterAround 0x02992dc0: confirm "Water Flood Remove", then Backup and remove.

## Height-setter re-fit

EditSetGroundSizeZAtBlockUndoable(pos, newZ, txn) 0x0297aa90. It is called by every height pen: EditPlacePen, ChangeHeight, DrawPath, Noise, PlaceHeightAlteration and Smooth. It returns if the block is not in the map or not IsPosChangeable.

A) The block is dry (slot-0 WaterType == 0):
- For each MapAround8 neighbour n that is in the map, has a map number, is IsMapEditable and is wet (type ≠ 0): s = ground(n) + rel(n) − newZ.
- If s > 0.0: sum += s, count++, and the group is taken from the first such n.
- Add CEditTransactionSetHeight(pos, newZ).
- If count > 0: assert the group is set, then SetRelWater(pos, sum / count, group).
- In short, a dry block that sinks below its neighbours' water floods at their mean surface.

B) The block is wet:
- g = old ground, r = rel. Add the SetHeight transaction.
- newRel = g − newZ + r, which keeps the surface constant.
- If newRel > DNZ: SetRelWater(pos, newRel, GetWaterThemeGroupAtBlock(pos)).
- Otherwise: slot 0 = INVALID_THEME_STANDIN, slot 1 = 0, blends 255 / 0.

## Rivers

Code offsets: CEditControlCentre+0xb24 holds the CEditRiver (scoped_ptr), +0xb30 the list of final points.

PenEditRiver 0x029920a0
- A click adds a waypoint at (x, y, ground).
- Dragging a control point within 0.5 moves it.
- Afterwards it calls ConfigureVerticalGradientOfRegion and GenerateRiverFinalPoints.
- (inferred) AddWaypointAtPos 0x0299e9e0 places the two control points ±1 unit along the normalised direction from the previous waypoint.

GenerateRiverFinalPoints 0x02993170: for each waypoint pair, CBezierCurve(prev.pos, prev.cp2, cur.cp1, cur.pos). It pushes the start point, then GetPointOnCurve(t) for t = 0.01; t ≤ 0.99; t += 0.01 (a float accumulator).

The "Generate" button calls PaintInputGenerateRiver(GetRiverRelativeHeight) 0x02992f10:
- Group 0: show "Select a theme".
- Backup.
- If IsRiverLikelyToOverflow returns TRUE, call EditGenerateRiverUndoable. If it returns FALSE, show "This river is likely to overflow". The function is misnamed: 1 means OK, 0 means more than 80 (0x50) widening steps on a side.
- "Clear" calls ClearRiverWaypoints 0x02035a60.

EditGenerateRiverUndoable 0x02978b20 (partly recovered). For each consecutive pair (a, b) of final points:
- fwd = normalise(xy(a − b)). (Which of the pair is "a" is to be pinned.)
- perp = InitialiseFromAngles(GET_ROUNDED_ANGLE(angle(fwd) + 0.25), 1). Angles are in turns wrapped to [0, 1), with GFSin/GFCos.
- surface = a.z + relHeight.
- Widen L to the left while ground(L − perp) < surface; widen R to the right while ground(R + perp) < surface.
- Grow the box.
- Scale fwd and perp by 0.7071.
- Starting from L, while (p.x − R.x) · GFGetSign(perp.x) < 0: from p, walk along fwd, and while the block is dry and ground < surface, SetRelWater(block, surface − ground); then p += perp.
- At the end: HeightAreaChanged(box).
- (inferred) GFGetSign(0) = +1, so a segment whose perpendicular has x exactly 0 fills nothing.
- Not pinned yet: ConfigureVerticalGradientOfRegion 0x0299eec0 / ConfigureAssociatedPointsVerticalGradient 0x0299e810 (cos-weighted control-point z) and the exact iterator roles.

## Bake side

Already in FableTLC docs/engine/WATER_RE.md: CWaterPatchMesh::Save layout, the background sub-patch, and BuildAndSaveSea for the sea disc.

**Blockers.** 1. Merging the water branch: 14 conflict hunks in 10 files, listed in the state section. The height pens were rewritten since the water branch's merge base (Mode enum, applyBrush), so its brush does not drop in as is.

2. The water branch's brush differs from vanilla (verified):
   - It is a radius brush, not the 8-connected flood from a click.
   - Depth deeper than the top rung is clamped (rung = max, weight capped); vanilla refuses anything over 16.
   - The weight is clamped to [1, 255]; vanilla allows 0 to 255.
   - Cells need depth > 0.01; vanilla needs > 1e-4.
   - It moves the old ground theme into slot 2; vanilla leaves slot 2 alone.
   - It picks the "0-rung" by WaterHeight ≤ 0. This matches vanilla's NoWaterThemeDef on retail data but is not the same rule.
   - It has no IsPosChangeable / mask test and no confirm dialog.

3. Retail data leaves vanilla's River and Ice theme lists empty: no TG_WATER_RIVER* or TG_WATER_ICE* groups exist. The generic WATER_LAKE_* and WATER_RIVER_* ladders share TG_DO_NOT_USE_1, so InitWaterDefList for that group would mix them, and first insert by class index wins (WATER_LAKE_* comes before WATER_RIVER_*). A river family therefore needs a documented FableForge extension beyond vanilla (for example, selecting by name family), or new theme-group defs.

4. The river generator is not fully pinned: the exact iterator roles, ConfigureVerticalGradient* and waypoint control-point placement. The river functions need a Ghidra decompile once ghidra_proj is not locked (another java process held the lock), or a pybag trace. Launching FableWin needs the user's go-ahead: it shares the install and a single-instance mutex.

5. Nothing is verified in-game: the visible surface, the distant water, and wading. The user runs in-game tests, and launches need coordination.

6. Ocean needs a sea body (__ENGINE_SEA_STATIC_MAP_BANK_FILE__, BuildAndSaveSea / CWaterSeaGenerator) for the far disc. It is not written yet.

7. Other bake gaps: big lakes lose their distant water because background frames cannot grow yet (that needs rebasing the file-block tuples), and shore/foam data is zero.

8. Vanilla's likely hang on reachable cells deeper than 16 (inferred) has to be defined away in the port, and that choice should be documented.

**Plan.**

1. Write the recovered editor behaviour into FableTLC docs/engine/WATER_RE.md (a new '## Editor water ops' section): EditSetRelWater, InitWaterDefList, GetRelWater, flood fill/remove with MapAround8, the re-fit, dialog wiring and prefixes, the retail group facts, and the river outline. Record the Ghidra lock in FINDINGS_LOG and point to the scratch dumps. Mark each claim verified or inferred. *(effort S (1-2 h), value Leaves one citable spec so the port does not re-derive anything. This is the evidence-first rule from CLAUDE.md.)*
2. Add a forgecore module forge::water (pure, with no GUI) on feat/editor-ui-shell. waterDefList(defs, group) returns map<float, defIndex> with first insert winning in class order. relWaterAt(lev, x, y) uses 3 slots, with slot 2 = 255 - b0 - b1. typeAt/groupAt read slot 0. setRelWater(doc, x, y, rel, group): refuse rel > 16, no-op below 1e-4, lower_bound, slot0 = water, slot1 = NoWaterThemeDef, b = round-half-away(rel*255.0/H) in double, slot 2 untouched. Palette slots come from Document::addGroundTheme. It needs the theme fields WaterHeight, WaterType, NoWaterThemeDef and GroupDef, which terraintex already decodes. *(effort M (1 day), value The exact vanilla primitive that every water tool and the re-fit call.)*
3. Port floodFill(absH, start, group, mask) and floodRemove(start, mask) as the exact DFS: MapAround8 order, the start and recursion validity rules (DNZ, the 0.05 same-group tolerance), IsPosChangeable mapped to FableForge's editable-map check, and the dirty box as start ±1 clipped to the map. Removal writes INVALID_THEME_STANDIN / 0 / 255 / 0. For cells deeper than 16, stop the fill and report the count (a documented deviation from vanilla's likely hang). *(effort M (1 day), value Lakes and Ocean exactly as vanilla, as undoable single-transaction edits.)*
4. Add the height-setter re-fit. Give forge::heightpen an optional per-block setter hook so that changeHeight, paintHeight, smear, noise and heightAddition (and draw-paths, if ported) call water::refitAfterGroundSet(block, newZ) inside their loops, in vanilla's order. Dry blocks flood from the mean of their wet neighbours with surface above newZ. Wet blocks keep the surface, or turn into the standin when newRel ≤ 1e-4. Remove the 'not reproduced' note from heightpen.hpp. *(effort M (0.5-1 day), value Sculpting next to water behaves as in vanilla: surfaces stay put, and banks raised above water dry out.)*
5. Add a Water tab to the Height toolbox in gui/editor.cpp. Lake / Ocean / Ice / River radio buttons; an absolute altitude field (reusing Cursor+1 and pickup = ground + relWater); a Themes combo filtered by the TG_WATER_LAKE / SEA / ICE / RIVER prefix; an Auto-remove click mode; vanilla's confirm text and error messages ('Select a theme', 'The ground is too high, or this point is outside a script brush', 'Fill with water of height X?', 'Water Flood Remove'). When the River or Ice list is empty with retail data, say so, and offer an explicitly labelled 'FableForge extra' picker for the TG_DO_NOT_USE ladders by name family. Preview uses the existing terrainexport water surface. *(effort M (1-2 days), value The vanilla Water tab without the vanilla UI friction.)*
6. Merge the water branch's bake side into the current branch. Resolve the 14 hunks: keep the current brush modes, and replace the water branch's Mode::Water radius brush with the new flood tools, or keep it as a labelled extra. Keep stbwater, the stbheightbake water providers and foreground growth, water-audit, and test_water.py (retarget it to the flood tool). Run check_all and the full suite. Do it as one PR on its own branch; the lane never switched branches in the repo. *(effort M (1 day), value Painted water becomes visible after a deploy, near and far for small bodies.)*
7. Recover rivers fully. Once ghidra_proj is free, run DecompFuncs on 0x02978b20, 0x02978780, 0x0299e9e0, 0x0299eec0, 0x0299e810, 0x029920a0 and 0x02993170, pinning the iterator roles, the control-point placement and the vertical gradient. Then port CEditRiver (waypoints, draggable control points, 100-sample Bezier), the overflow check (80 steps) and the cross-section sweep, including the sign(0) quirk. Add a River pen with Generate and Clear. *(effort L (2-3 days), value Completes the vanilla Water tab (Rivers).)*
8. Tests. (a) Unit: a bowl heightfield; fill at altitude A gives exactly the connected cells with ground < A - 1e-4; slot and blend bytes match the formula; the refill tolerance is 0.05; remove gives the standin; the re-fit keeps the surface and dries banks; cells over 16 m deep are refused. (b) Retail oracle: for every wet cell of every retail LEV, check that setRelWater(relWaterAt(cell)) reproduces the same slot0/slot1/blend bytes. This validates lower_bound and rounding; report any maps painted another way. (c) Retail flood oracle: take a retail lake, record its wet set, floodRemove it in memory, floodFill from one of its cells at ground + rel, and diff the cell sets. (d) Optional live oracle, with user approval: pybag breakpoints on 0x029772f0 and 0x0297aa90 in the FableWin editor during a scripted fill, comparing the args. (e) In-game with the user: a pond via the flood tool, deploy, then wade and look near and far. *(effort M (1-2 days), value Shows the port is functionally identical to vanilla without guessing.)*
9. Later, after in-game confirmation: the Ocean far disc (the sea body static-map bank entry via BuildAndSaveSea / CWaterSeaGenerator RE), background-frame growth so big lakes keep their distant water, and the shore/foam generator (FindShorePointsInMap, SortShorePointsIntoWaterBodies, GenerateShoreMapUCoords). *(effort L (several days each), value Full visual parity for seas and large lakes.)*

**Confidence.** High for the verified parts, all read from FableWin disassembly with names from the PDB name table: EditSetRelWaterHeightAtBlockUndoable, InitWaterDefList, GetRelWater/Type/Group, the flood fill and remove with their validators and MapAround8 order, the re-fit in 0x0297aa90, the dialog wiring and messages, and the DNZ and 16 constants. The theme-def facts (NoWaterThemeDef pointing at the 0-rung, the group list, INVALID_THEME_STANDIN) were checked against retail game.bin and names.bin. The merge picture comes from git merge-tree.

Medium for rivers: the widening loop, the sweep, the constants 0.7071, 80 and 0.01, and the misnamed IsRiverLikelyToOverflow are read from code, but the iterator roles and the vertical-gradient maths still need a Ghidra decompile.

Inferred and flagged in the text: the >16 m flood hang, the prefix-to-slot mapping (dialog+0x15c..0x168), the press-only condition, and the sign(0) river quirk.

**Verifier.** accurate: I re-checked the research against FableWin disassembly, using the scratch wdis.py and capstone plus fablewin_pdb_names.tsv, and against git and code in D:/Code/FableForge. The research is accurate overall. The corrections and omissions below are ordered by importance.

VERIFIED
- Addresses: all 26 cited addresses resolve to the claimed PDB names in ghidra_out/fablewin_pdb_names.tsv. EditSetRelWaterHeightAtBlockUndoable is at 0x029772f0 and has the signature (C2DCoordI const&, float, unsigned long, txn).
- EditSetRelWater constant and comparisons: the compare against 16.0 (at 0x434985c) errors only when rel > 16. It returns when rel < DNZ.
- DNZ and InitWaterDefList: DNZ_FOR_HEIGHTS = 1e-4, from its dynamic initializer at 0x03ff92f0 (0x44bda4c → 0x4ac5548). InitWaterDefList is called only when the group differs from the cached this+0x174.
- EditSetRelWater body:
  - The lookup is lower_bound on the map at this+0x160.
  - Slot 0 is set to the water theme, and slot 1 to def+0x7c (NoWaterThemeDef).
  - Blends are b and 255−b on slots 0 and 1.
- MapAround8 order: confirmed from 0x03ff9420 as (0,−1), (1,−1), (1,0), (1,1), (0,1), (−1,1), (−1,0), (−1,−1). The pushes are y then x.
- Flood-fill recursion tolerance: 0.05 (at 0x4349860), with fabs.
- Flood-fill DFS: it is a std::list with back / push_back / pop_back, scans 8 neighbours, and ends with ClipTo.
- Re-fit at 0x0297aa90:
  - Dry branch: the neighbour test is s > 0.0, the result is sum/count, and there is an assert "surrounding_water_tg != 0".
  - Wet branch: newRel = g − newZ + r compared against DNZ, otherwise INVALID_THEME_STANDIN with blends 255/0, with an assert "tg > 0".
- Flood remove at 0x029792a0: writes the standin with blends 255/0.
- Dispatcher order in PaintInputPaintMap: River → DrawPaths → Lake/Sea (GetWaterFillAltitude → FloodFill) → Autoremove → SprayCan…
- River constants:
  - 0.7071 (at 0x434bb90) and GFGetSign in EditGenerateRiverUndoable.
  - An 0x50 limit in IsRiverLikelyToOverflow, which also adds 0.25.
  - Bezier samples: t starts at 0.01, steps by 0.01 in a float, and continues while t ≤ 1−0.01.
  - PaintInputGenerateRiver: Backup, then IsRiverLikelyToOverflow, then EditGenerateRiver, then SetLevelsInAreaAsChangedSinceLoad.
- Retail groups: `forge def-groups ENGINE_THEME` lists exactly TG_WATER_LAKE_{BRIGHTWOOD, DARKWOOD, ICE_HOOKCOAST, KRAKEN_CHAMBER, WITCHWOOD} and TG_WATER_SEA_{HOOKCOAST, OAKVALE}, plus TG_DO_NOT_USE_1 (198 defs) and TG_DO_NOT_USE_2.
- Merge dry run: `git merge-tree` confirms the same 10 conflicted files. Counting conflict markers per file gives CMakeLists 1, AUTOMATION 1, app.hpp 1, editor.cpp 4, stbheightbake.hpp 1, stbheightbake.cpp 1, export.cpp 1, leveledit.cpp 1, leveledit.hpp 2, check_all 1, so 14 in total.
- Water branch: merge-base 001dcde, tip 81b64ff, 11 commits.
- Water brush (`git show water:src/leveledit.cpp`, lines 397-433): all of the following are as described.
  - radius brush
  - depth <= 0.01f skip
  - 0-rung by h <= 0
  - rung clamped to the max rung
  - weight clamped to [1,255] with lround in float
  - slot 2 = first non-rung theme at strength 0
- Current branch facts:
  - heightpen.hpp:14 has the "not reproduced here" note.
  - leveledit.cpp:538-543 has the pen switch.
  - leveledit.hpp:440/453 has addGroundTheme and paletteSlotOf.
  - terrainexport.cpp:661+ has PeekWaterDepth and PeekInterpolatedWaterHeight.
  - terraintex.cpp:375 reads WaterType.

CORRECTIONS
1. Blend rounding (plan step 2). The code does not "round-half-away in double". It computes rel*255.0 in double, divides by WaterHeight, and stores the result to a FLOAT (fstp dword [ebp-0x198]). It then calls GFFloatToLongNear(float) at 0x018c4020, which is just `fld; fistp`, so it uses the FPU default mode: round-half-to-EVEN. Exact .5 cases therefore round to even, and the port should use nearbyint/lrint on the float-truncated quotient.
2. The flood-fill hang is broader than described. The research says two deep blocks are needed; one is enough.
   - A single reachable block deeper than 16 hangs vanilla. SetRelWater refuses it, so it stays a valid recursion site. It gets popped when it has no valid neighbours, but its predecessor stays on the stack, rescans in MapAround8 order and re-pushes it.
   - Each refusal also raises the modal "highest that water can reach… 16" DoErrorMessage, so in practice it is an endless dialog loop.
   - The same applies when the start block itself is deeper than 16, because the start-site check has no 16 limit.
   - This is still inferred from control flow, not run, but the reasoning is tighter.
3. HEAD has moved. feat/editor-ui-shell is now 2b135b6, one commit after 9dc777e ("feat(world): every map's ground on the World map…"), so it is 129 commits past the merge base, not 128. This does not affect the conflict set.

MISSED OR WORTH ADDING
- The >16 refusal also reaches the re-fit and rivers, and nothing clamps it:
  - Re-fit, wet branch: when newRel > 16 (a bank lowered by more than 16 under water), SetRelWater shows the dialog and leaves the old theme and blend in place. The ground changes but the rel stays the same, so the surface moves with the ground.
  - Re-fit, dry branch: a mean above 16 does the same.
  - Rivers: surface − ground can exceed 16, with the same result.
  - The port must choose and document the behaviour for all three cases, not only for the flood fill.
- Extra asserts to mirror as preconditions:
  - In SetRelWater: "pwater_theme->WaterType != WATER_TYPE_NULL".
  - In SetRelWater: a re-read of slot 0 via GetThemeAtBlock with "theme_0->WaterHeight > DNZ_FOR_HEIGHTS", which fires if the chosen rung has height ≤ DNZ.
  - In EditFloodFillWaterUndoable: "rel_water_height > DNZ_FOR_HEIGHTS" before each SetRelWater.
- Prefix consequence: TG_WATER_LAKE_ICE_HOOKCOAST starts with "TG_WATER_LAKE", so with StartsWith filtering the Hookcoast ice lake appears in the LAKE list. The Ice radio button filters for TG_WATER_ICE, which matches nothing, so its list is empty. The UI plan should say that ice lakes are reachable from Lake.
- The river dirty box starts at the first final point ±1, via fadd/fsub 1.0 and __ftol2. The sweep converts positions to blocks with GFFloatToLongNear, which also rounds half to even.
- PaintInputPaintMap calls Backup at 0x029917a9 before the pen dispatch. This may be conditional on the press state and was not pinned. The flood paths also call Backup themselves.

UNVERIFIED BY ME (not contradicted)
- The InitWaterThemeListBox prefix-to-slot mapping (dialog+0x15c..0x168).
- The retail NoWaterThemeDef → 0-rung indices (2298/2304/1945/1933).
- The range-codec 65% figure.
- The exact line (~588) of the stbheightbake conflict.
- The river iterator roles, which the research itself flags as open.

## Navigation editor features

**Current state.** Both nav branches are already merged. feat/nav-thing-lines (8673b8c, same commit as local main) and fix/overworld-neighbour-grow (1a60f09) are 0 commits ahead of feat/editor-ui-shell, which is 85 and 94 commits ahead of them. [verified: git rev-list] The memory note calling them "unmerged" is out of date. The worktrees D:/Code/FableForge-nav, D:/Code/FableForge-ow and D:/Code/FableForge-verify-ow still exist.

What FableForge has on feat/editor-ui-shell (9dc777e) [all verified from code]:
- LEV cell API in libs/forgecore/src/lev.cpp:390-406: walkableAt/setWalkableAt (+15), cameraPassableAt (+16), preferredPathAt/setPreferredPathAt (+20). The layout is documented in libs/forgecore/include/forge/lev.hpp:1-30.
- CLI: `forge lev paint-preferred <src> <out> x y r 0|1` (tools/forge-cli/main.cpp:147, 2803) via terrain::applyPreferredPathBrush (libs/forgecore/src/terrain.cpp:440).
- GUI brush modes Raise/Lower/Flatten/Smooth/Walkable/Blocked/Theme/ReplaceTheme/Environment/Sound/CameraPass/CameraBlock/Noise/HeightKey (src/leveledit.hpp:84). The Passability tool row in gui/editor.cpp:2550 is {Walkable, Blocked, Camera ok, Camera no}. There is no pref-nav brush and no pref-nav or nav overlay in the GUI.
- TerrainState stores walkable and cameraPassable per cell (src/leveledit.hpp:63-64) but has no preferred array. writeTerrainToLevel (src/leveledit.cpp:399-436) writes only heights, walkable, camera (ORed with walkable, as the vanilla saver does), themes, atmos and sound.
- Brush footprint: dx=x+0.5-bx, dy=y+0.5-by, included when dx²+dy² <= r² (src/leveledit.cpp:451-470).
- View modes: Textured/Wireframe/Walkable/Height (gui/renderer.hpp:43). The Walkable view comes from a per-vertex walk attribute in the shader (gui/renderer.cpp:90-100): teal, or orange with stripes. There is no per-cell RGBA overlay texture.
- Nav on save: saveTerrainLoose (src/leveledit.cpp ~1041-1066) diffs walkable against navWalkable_ and calls navmesh::patchWalkability, which splits or removes leaves, adds full-cell leaves (preference 0x00/0x80 from byte +20 only when a cell is opened; navpatch.cpp:427-454, 573), recomputes neighbours and regions, then calls emitNavigation. A preference-only change is never propagated into the tree today.
- Nav parse/emit: navpatch.hpp/.cpp (RetailNav: sections by name, every layer, switchable leaves, and the preference byte per leaf). Byte-identical round trip on retail files.
- Generators:
  - navmesh::generateTerrain (terrain only, one layer) is used for brand-new maps (src/worldedit.cpp:571).
  - The experimental navmesh::generateGround (libs/forgecore/src/navgenerate.cpp; navmesh.hpp:38-61) takes explicit GroundGeometry: blocking lines, detail areas, switchable lines and regionSeeds. It is reached only through the read-only `forge nav-compare` / `nav-lines` (src/cli/nav.cpp:33, 365). The seed categories in native order include CTCDNavigationSeed (src/cli/nav.cpp:241; navmesh.cpp:106 matches the NAVIGATION_SEED definition).
  - Parity per docs/HANDOFF_NAV.md: four maps exact in nodes, neighbours and regions (LookoutPoint, PicnicArea, Greatwood_1, GuildExterior). BarrowFields matches all ground nodes but has one stacked-layer region conflict. OrchardFarm is off by 26 generated-only and 38 retail-only nodes. The native box-intersection oracle agrees on 10005/10005 cases.
  - Not promoted to the writer. No in-game nav test has been done.
- Docs: docs/ROADMAP_1.0.md rows 15 and 24 (pref-nav brush "waits on the nav lane's cost propagation"), docs/DEBUG_EDITOR_FEATURE_AUDIT.md:48 and 83-94, docs/VANILLA_EDITOR_INVENTORY.md:308 and 328-356.

**Vanilla / engine.** All addresses are in the debug FableWin.exe (D:/Documents/FableTLC/debug_build/FableWin.exe, 55,571,968 bytes, ImageBase 0x400000; same size as the D:/tmp editor copy). Decompiled headless with DecompAt.java. Exports are in scratchpad/lanes/nav/navedit{,2,3}.c. Constants were read with pefile and capstone.

1. Pref-nav in-memory flag [verified]. CHeightMapCell byte +9 bits:
   - 0x02 = passable (IsPassableAt 0x022389b0, SetPassableAt 0x022389f0: `&0xfd | v<<1`)
   - 0x04 = shore
   - 0x08 = camera passable (IsCameraPassableAt 0x02238a50, SetCameraPassableAt 0x02238cc0)
   - 0x10 = pref-nav (IsPrefNavAt 0x02238c80 returns `(b9>>4)&1`; EditSetPrefNav@CEditMap 0x029a96f0 writes `b9 = b9&0xef | (v&1)<<4`)

2. Pref-nav on disk = serialized cell byte +20 [verified from code]. CMap::SaveToFile 0x02234c60 builds CFileFormatHeightMapCell per cell, including extra cells, then calls CReplaceSerialise<...>::SaveToFile 0x0223aee0. That writes the 5-byte header (u32 21, u8 7) and then:
   - f32 height/2048 at +5
   - 0 at +9
   - themes at +10..12
   - blends at +13..14
   - passable (bit1) at +15
   - camera = bit3 OR bit1 at +16
   - +17 (old sound)
   - 0 at +18
   - bit2 (shore) at +19
   - bit4 (pref-nav) at +20
   This matches FableForge lev.hpp. FableTLC docs/engine/NAVIGATION.md:325-342 separately pins byte +20 corpus-wide: 0 violations over 149 LEVs. That makes two sources.

3. Cost the nav builder uses [verified]. CWorldMap::GetMapNavigationAreaUpdateInfo 0x01c8f390 builds TopologyWeights per cell: !IsPassableAt → 0xFF, else IsPrefNavAt → 0x00, else 0x80. IsAreaAllSamePreferability 0x0328d400 subdivides any area with mixed weights, and the leaf stores the weight as its preference byte (0x40 on raised layers; NAVIGATION.md). So a pref-nav edit changes both leaf cost and quadtree shape.

4. Brush [verified]. The input chain:
   - CEditInputProcessSurveyPassability::ProcessInput 0x0295c3b0: mouse event types 4..6, GetCurrentPointedAtPos, Shift (key 0x2a or 0x36) → erase.
   - CEditControlCentre::PassabilityInput 0x02051d50 asserts EditMode==13, uses CSurveyDialog::GetPassabilityBrushRadius, and rounds the pen centre to an integer with GFFloatToLongNear. It then dispatches in priority order IsEditingPassability → IsEditingCameraPassability → IsEditingPrefNav, with value = !shift, then calls SetLevelsInAreaAsChangedSinceLoad(GetPenArea).
   - CEditWorldMap::EditSetPrefNav 0x02970360 (same body as EditSetPassability 0x02970060 and EditSetCameraPassability 0x02970630) loops x in floor(cx-r)..ceil(cx+r), same for y. It includes a cell when `hypot(x-cx, y-cy) - 0.5 < r` (float), plus world-box, IsPosInMap and IsMapEditable checks. It converts to local coordinates and calls CEditMap::EditSetPrefNav.
   - CEditMap::EditSetPrefNav writes only when the value changes, and updates that one cell's overlay (UpdateOverlayAt 0x029a9a10 = engine vfunc +0x100 on a 1×1 box).
   - Differences from FableForge: FableForge uses a float centre and a +0.5 cell-centre test with `<=` for the camera and walkable brushes. The vanilla walkable brush calls CMap::SetPassableAt, so it goes through the same pen code.

5. Selecting the pref-nav tool [verified]. CSurveyDialog::OnGuiEditPrefNavSelected 0x028d16c0 closes other surveys, sets survey state 4 (1 = off), and calls SetPrefNavEditingMode 0x02051700 → EditShowPrefNav (0x029716f0 over all editable maps; per map 0x029a9b60). That calls engine vfunc +0x104 (clear overlay) and then PassabilityOverlayInfo 0x029aa3a0 with IsPrefNavAt. Result: every cell is drawn with colour 0x04ac5664 when the flag is set and 0x04ac5660 when it is not.

6. Colour constants [verified]. They are .bss values set by static initializers at 0x3ff9310-0x3ff9360 and 0x3ff96c0-0x3ff96e0 through CRGBColour(R,G,B,A) (Initialise 0x18a0c60 stores B,G,R,A):
   - 0x04ac5660 overlay-false = (255,0,0,64) translucent red
   - 0x04ac5664 overlay-true = (53,120,24,64) green
   - 0x04ac554c nav non-navigable = (255,0,0,64)
   - 0x04ac5550 nav low cost = (53,120,24,64)
   - 0x04ac5554 nav normal cost = (180,120,30,64) brown/orange

7. Show navigability [verified]. CEditControlCentre::SetNavigabilityDisplayMode(layer, on) 0x02051bb0 → EditShowNavigability(layer) 0x029722e0. For each editable map it clears the overlay, calls CNavigatorManager::RemoveNavigationMap, then CWorldMap::ActivateNavMap 0x01c8ded0. That runs GetMapNavigationAreaInit + AddNavigationMap, which **regenerates nav from the current edit state**, and then OverlayNav(map, layer) 0x029723b0. OverlayNav details:
   - It walks the map box in steps of GetLargestNodeSize, descending with GetNextNavNodePos 0x029726d0 (corner order 0 = none, 1 = +x, 2 = +x+y, 3 = +y; half-size steps).
   - It calls CNavigatorManager::GetNodeAt(pos, layer).
   - If any probe returns NULL, it fills the **whole map opaque green (0,255,0,255)** and returns. This is the likely cause of the "flat unshaded green" seen live [inferred].
   - Each leaf of size >= 1 goes to OverlayNavNode 0x02973010, which uses the non-navigable colour when !IsNavigable. Otherwise it reads the node's cost (vfunc +0x20): cost < 0x65 → green 0x5550 (preferred 0 and raised-layer 64), else brown 0x5554 (0x80). It paints a size×size block (OverlayNavNodeWithColour 0x029736a0).
   - Half-size leaves are gathered four per cell by OverlayTinyNavNodes 0x029732a0 (asserts size%4==0): 0 navigable → red, 1-3 → (255,255,255,64) white, 4 → 0x5550 green regardless of cost.
   - EditShowNavigability@CEditMap 0x029a9e70 is an empty stub.

8. Nav regen on save [verified]. GenerateNavigationInformation 0x0204b700 runs RemoveNavigationMap + ActivateNavMap for every editable map. Its only callers are SaveAllLevels 0x0204b5d0 (call at 0x204b5f2) and SaveLevelIfChanged 0x0204b990 (call at 0x204b9cd, only when its 4th bool argument is set). DisableEditSurveyIfNecessary runs first.

9. Seeds [verified in the FableForge handoff; not re-derived here]. GetMapNavigationAreaInit 0x01c905d0 gathers seeds in this order: villages, AI creatures, creature generators, CTCDNavigationSeed, region entrances, exits. The world map gathers NAVIGATION_SEED things with GetAllTCDsInRoughArea<CTCDNavigationSeed, IsInQuestOrNullQuest> 0x01ccbd00 (inventory).

**Blockers.** 1. Nothing blocks the pref-nav brush data path. Byte +20 is verified, and the lev.cpp and terrain.cpp APIs exist. The only true dependency is a nav-cost update on save. Without it, painted paths are ignored until the nav is rebuilt, because retail leaves keep their old preference bytes (verified: patchWalkability only diffs walkable, navpatch.cpp:573). A preference patch (splitting a leaf down to the cell, then setting the byte) is a small, self-contained extension of navpatch. It does not need the full regenerator.
2. Navigability overlay: vanilla draws from a nav freshly regenerated from edit state. FableForge can't regenerate fully yet, so v1 has to draw the stored or patched RetailNav tree (level_ after the last save plus pending patches). That is faithful for unedited and patched maps but not for maps with unsaved object moves [inferred]. The renderer also lacks a per-cell RGBA overlay texture. The Walkable view is a per-vertex attribute, which is too coarse for half-unit leaves and 64-alpha blending.
3. Full nav regeneration, still experimental per HANDOFF_NAV.md:
   - upper and stacked layers are not modelled (GetNavigationLayerAt), which causes the BarrowFields region conflict;
   - OrchardFarm is still off by 26/38 nodes;
   - quest-variant sections aren't handled;
   - native action-point reachability filtering isn't re-run;
   - physics-only things with no render instance are unverified;
   - oversized objects from distant maps are missed;
   - automatic child seed components are incomplete;
   - there is no in-game proof.
   Promoting it to the save path needs a corpus gate over all 149 nav LEVs, not six maps.
4. Brush footprint parity: FableForge's walkable and camera brushes use a float centre with a +0.5 cell-centre `<=` test. Vanilla uses a round-to-nearest integer centre with `hypot - 0.5 < r`. Minor, but it should be ported so the footprint matches.
5. Unverified or inferred: the default brush radius and the nav-layer spinner range in CSurveyDialog; which nav section vanilla overlays when quest sections exist (GetNodeAt uses the active nav map, likely NULL); whether the full-green fill is what the user saw live.
6. Scope note: the relayed user request asked about particle emitters. This lane covered only the navigation topic assigned by the workflow script. Particle emitters are not researched here.

**Plan.**

1. 1. Port the vanilla pen footprint. Add a shared forge::terrain::penCells(cx=round(bx), cy=round(by), r) that yields cells where hypot(x-cx,y-cy)-0.5<r over floor/ceil bounds, clipped to the map. Use it for the Walkable/Blocked, CameraPass/Block and new PrefNav brushes (src/leveledit.cpp:451-470). Test: table-driven unit test against the formula decompiled from 0x02970360 at r=0,0.5,1,2.7 and at map edges. *(effort S (0.5 day), value Brush footprints match vanilla exactly for all three passability brushes, and the pref-nav brush gets them too)*
2. 2. Pref-nav brush in the editor. Add TerrainState::preferred (per cell, byte +20), load it in the terrain snapshot (leveledit.cpp ~219-240), and write it in writeTerrainToLevel. Add Mode::PrefOn/PrefOff (Shift erases, as in vanilla ProcessInput 0x0295c3b0), include it in the dirty and undo comparisons (leveledit.cpp:573, 940), and add a 'Villager path' / 'Villager path erase' pair to the Passability row (gui/editor.cpp:2550) with a hint. Tests: lev round-trip keeps byte 20 and the other 20 bytes identical; undo/redo; test_export case. *(effort S-M (1 day), value Ships the missing vanilla Survey > Passability 'Villager Preferability' tool (roadmap row 24))*
3. 3. Nav preference patch on save. Extend navpatch with patchPreference(nav, file, cells), or generalise patchWalkability to take a CellChange{walkable, preferred}. For each changed walkable cell on layer 0 of every section: split any covering leaf larger than one cell down to level 5 (reuse the existing split path, recompute neighbours), then set preference = byte20 ? 0x00 : 0x80. Level-6 half leaves in that cell get the same byte. Regions stay unchanged, since cost doesn't affect connectivity. Keep navPreferred_ next to navWalkable_ in Document and diff both. Tests: synthetic 32×32 root, paint 1 cell → nodesSplit>0, exactly one leaf has pref 0, neighbour sets symmetric, regions unchanged, emit→parse round trip. Corpus: toggle-and-revert on every retail LEV is byte-identical. Invariant check: after patching, every layer-0 leaf pref equals f(byte20) (the NAVIGATION.md invariant). *(effort M (1.5-2 days), value Painted paths actually change villager A* cost in-game without the full regenerator. Unblocks roadmap row 15's pref-nav part)*
4. 4. Per-cell overlay infrastructure in the renderer: an RGBA8 texture of cellsX×cellsY (plus a 2× variant for half-unit leaves), point-sampled over the terrain and alpha-blended over the textured view, keeping shading so the terrain shape still reads (the live criticism of vanilla). Expose Renderer::setCellOverlay(rgba, w, h, scale). *(effort M (1 day), value A shared base for the pref-nav view, navigability view and future survey overlays (sounds, minimap zones))*
5. 5. Pref-nav overlay, shown automatically while the pref brush is active (as vanilla EditShowPrefNav 0x029a9b60 does): per cell (53,120,24,64) when set, (255,0,0,64) when not. Offer a colour-blind-safe palette as an option, keeping vanilla colours as the default. Test: screenshot automation on a retail map with known roads (Bowerstone or Greatwood paths) and a unit test of the colour-array builder. *(effort S (0.5 day), value Shows authors exactly where villagers prefer to walk, matching vanilla semantics)*
6. 6. Show navigability view with a nav-layer spinner. From RetailNav (parseNavigation of the current level_ plus pending patches), take the NULL section and walk leaves of the chosen layer:
- size >= 1: red (255,0,0,64) if blocked or closed-switchable, green (53,120,24,64) if pref < 0x65, brown (180,120,30,64) otherwise;
- half leaves aggregated 4 per cell: 0 → red, 1-3 → white (255,255,255,64), 4 → green;
- a layer with no nodes → a 'no nav on this layer' banner instead of vanilla's opaque-green flood.
Also add an optional outline mode for leaf boundaries. Layer range = section.layerCount-1. Tests: the colour-builder unit test reproduces the OverlayNav rules on a synthetic tree; visual check against vanilla FableWin (runs from D:/tmp/fablewin_editor) on LookoutPoint layer 0 and BarrowFields layer 1, user-driven. *(effort M (1-1.5 days), value Vanilla navigability view parity, better readability, and immediate visual feedback for the walkable and pref patches)*
7. 7. NAVIGATION_SEED authoring: make sure the Things palette places NAVIGATION_SEED (CTCDNavigationSeed) markers with the NULL-quest filter, and show a small seed glyph plus 'reachable region' highlight in the navigability view, reusing the nav-compare seed gatherer (src/cli/nav.cpp:241). Test: place a seed on an island in a scratch map; generateTerrain keeps that island. *(effort S-M (1 day), value Authors can control which walkable islands survive nav generation, matching the vanilla seed semantics)*
8. 8. Nav regeneration next, behind an opt-in 'Rebuild navigation on save' toggle mirroring GenerateNavigationInformation 0x0204b700:
(a) run generateGround with the in-editor geometry, including unsaved thing moves;
(b) implement GetNavigationLayerAt and upper layers to clear the BarrowFields region conflict;
(c) close the OrchardFarm 26/38 gap;
(d) handle quest sections and action-point reachability filtering (Initialise 0x03290030);
(e) add a corpus gate: nav-compare over all 149 nav-bearing LEVs, exact node, neighbour and region counts reported, promote only when there are 0 diffs on 90% or more of maps and the rest are explained;
(f) user-driven in-game check: villagers path over a painted route, and the hero on a regenerated map.
Keep per-cell patching as the default until (e) passes. *(effort L (1-2 weeks; (b)-(c) are the unknowns), value The Show navigability view can then regenerate the way vanilla does. Full vanilla save-time rebuild; engine-quality nav for moved objects and new terrain (the 'nav frontier'))*
9. 9. Docs and bookkeeping: update ROADMAP_1.0 rows 15 and 24, VANILLA_EDITOR_INVENTORY §5.10 (add colours, footprint, EditShowPrefNav, the full-green failure mode), HANDOFF_NAV (pref patch), and fix the stale memory note: nav-thing-lines and overworld-neighbour-grow are merged into feat/editor-ui-shell. *(effort S (0.5 day), value Keeps the evidence trail accurate and saves a future session from re-deriving it)*

**Confidence.** High that the vanilla behaviour below is correct:
- pref-nav bit 0x10 ↔ byte +20 (SaveToFile decompile plus the corpus invariant);
- TopologyWeights 0/0x80/0xFF;
- pen formula and Shift-erases;
- overlay colours (static initializers disassembled, RGBA order checked against CRGBColour::Initialise);
- OverlayNav leaf and tiny-leaf rules;
- regen-before-overlay and regen-on-save call chains.
High on FableForge state (code read, git ancestry checked).
Medium on the effort estimates.
Inferred, not verified: the live "flat green" being the GetNodeAt==NULL whole-map fill, which nav section vanilla overlays under quests, the brush radius and layer-spinner defaults, and whether the per-cell pref patch behaves in-game the same as a full rebuild (leaves won't re-merge up to the largest node size; path cost should be equivalent).

**Verifier.** accurate: The key claims check out against the code. I found no errors in the load-bearing facts, only omissions and small refinements.

VERIFIED
- Git state:
  - feat/editor-ui-shell = 9dc777e.
  - feat/nav-thing-lines = 8673b8c = local main.
  - fix/overworld-neighbour-grow = 1a60f09.
  - Both branches are 0 commits ahead of feat/editor-ui-shell, which is 85 and 94 ahead of them. The memory note calling them "unmerged" is stale.
- FableForge code refs are all accurate:
  - lev.cpp cell API: bytes +15, +16, +20.
  - leveledit.hpp TerrainState has no preferred array. The Mode enum matches.
  - editor.cpp:2550 Passability row is {Walkable, Blocked, Camera ok, Camera no}.
  - renderer.hpp ViewMode is {Textured, Wireframe, Walkable, Height}.
  - writeTerrainToLevel ORs camera with walkable and never writes +20.
  - GUI brush test is +0.5 cell centre with <=.
  - navpatch openCell sets pref 0x00/0x80 only on newly opened level-5 leaves. patchWalkability (~l.573) branches only on walkableAt.
  - The save path (leveledit.cpp ~1041-1066) diffs only navWalkable_.
- Every FableWin address named in the research resolves to the claimed symbol in fablewin_editor_symbols.tsv or fablewin_pdb_names.tsv. That includes 0x028d16c0 OnGuiEditPrefNavSelected, 0x028d2090 GetPassabilityBrushRadius and 0x0328d400 CNavQuadTree::IsAreaAllSamePreferability.
- The decompiles in lanes/nav/navedit*.c confirm:
  - the pen formula: floor/ceil bounds, then GFHypoteneuse(x-cx, y-cy) - 0.5 < r;
  - the centre is an integer C2DCoordI;
  - the OverlayNavNode threshold: cost < 0x65 selects 0x04ac5550, otherwise 0x04ac5554, and !IsNavigable selects 0x04ac554c;
  - EditShowNavigability runs RemoveNavigationMap, then ActivateNavMap, then OverlayNav;
  - OverlayNav's GetNodeAt==NULL path fills the whole map with (0,255,0,255) and returns.

CORRECTIONS / MISSED
1. There is a third brush footprint, not two. The CLI `forge lev paint-preferred` / paint-walkable go through terrain::applyBooleanBrush (libs/forgecore/src/terrain.cpp:407-429). That brush tests integer corners with no +0.5, uses distance <= r plus a falloff `threshold` (an optional 7th CLI argument), and its bounds are inclusive up to file.width()/height(). The research lists the CLI brush as existing but never compares its footprint. Step 1's shared penCells should replace this one too, or the CLI and GUI will still disagree with vanilla and with each other. [verified from code]
2. The CLI paint-preferred never patches the nav, and neither does paint-walkable. They only rewrite LEV bytes, so a preference painted from the CLI is ignored in-game until the nav is rebuilt, the same as the GUI gap. The save-path patch also runs only when `!level_->navSections().empty()`. [verified from code]
3. CEditWorldMap::EditSetPrefNav 0x02970360 does more after the loop than the research says:
   - It builds a C2DBoxI(floor/ceil bounds), clips it to vfunc +0x34's box, and calls vfunc +0x70 on it. That is probably a changed-area or redraw notification, but its semantics are not verified.
   - The per-cell checks also call vfunc +0x34, vfunc +100 (0x64) and vfunc +0x44 before IsMapEditable. The research's summary "world-box/IsPosInMap/IsMapEditable" is fine, but the trailing +0x70 call is missing.
4. EditShowNavigability@CEditWorldMap 0x029722e0 loops map index from 1 to GetNoMaps()-1, so map 0 is skipped. It filters with vfunc +0x28(map), not an explicit IsMapEditable. The overlay-clear call is vfunc +0x104(map,1,1). [verified from decompile]
5. A worktree is missing from the inventory. D:/Code/FableForge-verify-ow is on integration/nav-overworld-20260927 (8b5e587), which is also fully merged: 0 commits ahead of feat/editor-ui-shell, and feat/editor-ui-shell is 87 ahead of it. [verified: git rev-list]
6. src/worldedit.cpp:554 explicitly clears preferredPathAt when it creates a brand-new map. So new maps start with no preferred paths, and generateTerrain's leaves all get 0x80. This is worth noting for step 7/8 tests. [verified from code]
7. Two claims were accepted from the lane's own exports and not re-derived in this check:
   - the colour constant RGBA values (static-initialiser disassembly);
   - OverlayTinyNavNodes' 0 / 1-3 / 4 rules.
   The OverlayNavNode size>=1 versus <1 split into the tiny-node vector is confirmed.
8. Scope: the relayed user request was about particle emitters, and this lane is nav. The research already flags this, correctly. The orchestrator should make sure a separate lane covers particle emitters.

## Particle emitters and effects in FableForge

**Current state.** What FableForge has now. All of this is on feat/editor-ui-shell. None of the other branches (water, feat/nav-thing-lines, fix/overworld-neighbour-grow) has any particle work: `git log --all --grep` finds only ade2a3b (the effect picker), 2c8e11f (particles made opt-in) and 8e5c39a (the parser and proxies). [verified-from-code]

1. **effects.big reader: src/effects.cpp (274 lines) and src/effects.hpp.** It reads data/Misc/pc/effects.big (bank PARTICLE_MAIN_PC) through forge::big, parses each entry only when it is first asked for, and caches it. The grammar port covers all 10 component classes: CPSCRenderSprite, UpdateNormal, EmitterGeneric, Spline, SingleSprite, RenderMesh, Light, Attractor, Orbit and DecalRenderer. It checks the 0x7B/0x26 terminators and applies the EgoCore quantisation scales. It keeps only a summary per effect:
   - sprite systems: sprite id, start colour, start/end size, blend mode, particles per second, particle life, offset;
   - mesh systems: mesh id and size;
   - lights: colour and radius.
   Every other field is read and thrown away: mid/end colours, the flags, emitter shape, velocities, spline data and so on. It has no writer. The API is openBank, byName, entryNames and entryCount. [verified-from-code]
2. **Static proxies: src/thingsexport.cpp:150-268.** `placeParticle` builds two crossed 1x1 quads per sprite system, tinted with the start colour (additive gets lum*2 as alpha). It scales them to max(start, end)*2, with z*1.5 on a guess. It also places CPSCRenderMesh meshes as static props and emits CPSCLight as glTF point lights. It is used for:
   - PARTICLE_EMITTER_PLACEABLE things (CTCDParticleEmitter.ParticleTypeName);
   - mesh `CREATEPARTICLE <fx>` dummies (spawnChildren).
   It only runs when `Options::particles` is true. That defaults to false (thingsexport.hpp:54), and only the CLI `forge export --particles` (src/cli/export.cpp:167/380) sets it. [verified-from-code]
3. **The GUI viewport never shows emitters.** gui/app.cpp:754-759 and :944-951 build `thingsexport::Options` without setting `particles`. So placed emitters and CREATEPARTICLE effects are invisible in the editor. [verified-from-code]
   - The log line at gui/editor.cpp:3295 ("the preview shows a tinted proxy when Objects are on") is therefore wrong. [verified-from-code]
   - Viewport clicks select rendered meshes and an emitter has none, so an emitter can only be reached through the Things list. [inferred]
4. **Placement.** The Actors tab has a "Particle effect" card (gui/editor.cpp:3300-3337) with a searchable list of all effects.big names. `App::placeEmitter` (editor.cpp:3282) calls `Document::placeEmitter` (src/leveledit.cpp:2076). That writes the retail PARTICLE_EMITTER_PLACEABLE block: CTCPhysicsStandard, an empty CTCEditor, then CTCDParticleEmitter with `IndependantObject TRUE` and `ParticleTypeName "<FX>"`. There is also an automation verb, `place_emitter <FX> [script]` (gui/automation.cpp:425). [verified-from-code]
5. **Thing Properties.** src/vanilla_props.inc:115 maps CTCDParticleEmitter.ParticleTypeName to the vanilla caption "ParticleID" (tab ParticleEmitter, kind enum). Its enumPairs is empty, though. The enum branch at editor.cpp:488 only handles K::Int with pairs, so the effect name falls through to a plain text box, with no picker and no validation. [verified-from-code]
6. **Renderer: gui/renderer.cpp:394-403.** It creates only an opaque blend state and SRCALPHA/INVSRCALPHA. There is no ONE/ONE or ONE/INVSRCCOLOR state, no billboards and no per-frame particle path. [verified-from-code]
7. **Live link: src/livelink.hpp.** Through ForgeFSE it can already teleport, spawn a def, ping and reload a region. The FSE script API has `CreateEffectAtPos` and `CreateEffectOnThing` (refs/fse_api_manifest.json), but the live link does not wire them yet. [verified-from-code]

**Aeon / EgoCore overlap.** EgoCore (C:\Users\Cornelio\Documents\EgoCoreInspect\EgoCore-master, MIT, © AeoN) already has:
- EgoCore/Particles/ParticleParser.h (720 lines), a byte-exact reader;
- ParticleCompiler.h (444), the exact writer;
- ParticleProperties.h (871), a full ImGui property editor for every component field;
- a "new particle entry" action (Banks/BankTabUI.h:463).
Its preview covers only the sprite texture (g_ParticlePreviewTexID) and the RenderMesh mesh (ParticleProperties.h:18-29, 718). It has no particle simulation. [verified-from-code] Aeon's announced work is presumably a simulated preview in EgoCore. [inferred]

Ground truth in the FableTLC repo: docs/formats/EFFECTS_FORMAT.md, tools/parse_effects.py (1165/1165 exact), tools/report_particle_rendering.py, tools/dump_shader_asm.py, and docs/journal/2026-08/PARTICLE_LIGHTING_VIEWER_HANDOFF.md. The handoff covers blend tuples, crossed-plane geometry, UV scale and the SM1.1 shaders. [verified-from-code]

**Vanilla / engine.** **What vanilla FableWin offers for particles**

1. **No particle editor dialog.** The NEditGui dialog classes in fablewin_editor_symbols.tsv are PaintMap, Theme2Map, ThingProperty, Region, MapsAndRegions, WorldMapPlacement(+Popup), Thing, Survey, MapVis, AttachingThings, Animation, CopyPaste, MapResize, Bone, Scene, Resolve, Tracks, ScriptBrush, Fractal, BrushLibrary, AnimationEvents, Quest and InitialQuests. None of them is about particles. [verified-from-code]
   - `CPSCBase::TransferUpdateParametersToDialog` @0x02f10250 (FableWin) and `CPSC*::GetGUIName` / `InterpolateTo` / `DatabaseSerialise` in ego_r look like hooks for Lionhead's separate particle tool. That tool did not ship. [inferred]
2. **Thing Properties shows one emitter field.** It is "ParticleID" on the ParticleEmitter tab:
   - Get: 0x01d68d36 through CGuiVarTransferSymbolVal on this+0x14. Set: `SetParticleID` 0x01d68e40.
   - The options come from `CParticleEmitterDatabase::GetParticleSymbols`, so it is a dropdown of every effects.big name.
   - FableWin strings "ParticleEmitter" @0x01d68cda and "ParticleID" @0x01d68cf9. [verified-from-code]
3. **CTCDParticleEmitter reads and writes only two .tng keys:** "IndependantObject" (0x01d64da3/0x01d64e13) and "ParticleTypeName" (0x01d64d7a/0x01d64ea7). The name is resolved through `NParticleEngine::GetParticleEmitterDatabase()`, with an assert on failure (0x01d64f4f). [verified-from-code]
4. **Editor drawing: `CTCDParticleEmitter::EditorDrawPreparePrimitivesForRendering` @0x01d668b0.** When bit 2 of flags byte this+0x4C is set, it draws the parent thing's CTCGraphicAppearance. The call chain is thunk 0x178fd48 → GetParentThing, 0x18576ef → GetTCGraphicAppearance, 0x17a6c2d → CTCGraphicAppearance::Draw. [verified-from-code]
   - `SetAsDrawable` @0x01d68390 toggles drawability using bit 3 of +0x4C. [verified-from-code]
   - The actual particles are simulated and drawn by the live engine running inside the editor, so vanilla shows the real effect in place. [inferred]
5. **Debug console commands:** NGlobalConsole::ConsoleCreateParticle @0x018bfe30, ConsoleCreateParticleOnCamera @0x018c0150, ConsoleCreate2DParticle @0x018c0450, ConsoleDumpParticleInfo @0x018bf490. [verified-from-code]
6. **FableWin also contains the effect writer:** `CParticleEmitter::WriteBinary` 0x02f0dbb0, `CParticleSystem::WriteBinary` 0x02f17d40, and a per-component WriteBinary for each of the 10 components (0x02f2d730 … 0x02f5c270). [verified-from-code]

**How the engine defines particles**

1. **Data.** effects.big / PARTICLE_MAIN_PC holds 1165 uncompressed entries, each one serialized CParticleEmitter:
   - header: magic 0x64, name, 7 bools, 5 floats, i32 Priority, 5 bools;
   - then the systems, each with its components; components end with 0x7B and systems with 0x26;
   - colours are stored BGRA; quantized fields are u32.
   The entry id is the `EParticleEmitter` value in RetailHeaders/pc/particles.h; both are present in the install. [verified-from-code, EFFECTS_FORMAT.md + the install listing]
2. **References.**
   - By int id: about 40 def fields, e.g. CChestDef.OpenParticleEffect or CThingShotDef.PrimaryEffect.
   - By name: .tng ParticleTypeName, mesh CREATEPARTICLE dummies, and CParticleAttacherDef records (13 bytes: id, names.bin helper offset, f32, u8).
   [verified-from-code per EFFECTS_FORMAT.md]
3. **Symbols.** `ghidra_out/egor_pdb_names.tsv` addresses belong to debug_build/ego_r.exe, NOT retail Fable.exe. Decompiling 0x00ace770 in the retail DB landed inside COptimisedMesh; in ego_r it resolves to `CPSCUpdateNormal::Update`. The 0x00ABE360 PreparePrimitives address in the handoff doc is therefore an ego_r address. [verified-from-code]
4. **Key ego_r addresses:**
   - CParticleEmitterDatabase: OpenBank 0x00aadfe0, GetEmitterTemplateHandleFromName 0x00aadfa0, CreateEmitterTemplate 0x00aae5f0, Save 0x00aae250, GenerateHeaderFile 0x00aae0d0.
   - CPSCEmitterGeneric: Update 0x00ad2d60, GenerateParticle 0x00ad1ac0, ReadBinary 0x00ad5cb0.
   - CPSCUpdateNormal: Update 0x00ace770, ReadBinary 0x00acc4c0.
   - CPSCRenderSprite: ReadBinary 0x00abb920, PreparePrimitives 0x00abe360.
   - CPSCSingleSprite::Update 0x00ac9c30; CPSCLight::Update 0x00ad9e50 and PreparePrimitives 0x00adae80; CPSCAttractor::Update 0x00ad90d0; CPSCOrbit::StartUpdate 0x00ad7c90.
   - DatabaseSerialise (a CPersistContext Transfer, so field names and defaults are recoverable), e.g. CPSCRenderSprite 0x00abd350 and CPSCEmitterGeneric 0x00ad4610.
   [verified-from-code, symbols; Update, EmitterGeneric::Update and GenerateParticle were decompiled headless, output in scratchpad lanes/particles/egor_sim.c]
5. **Simulation facts from the EmitterGeneric::Update decompilation:**
   - The simulation is fixed-tick. `NParticleEngine::GetTickRate` @0x00aaee80 returns the global at 0x121b7d0, written only by `SetTickRate` @0xaaee67. The callers and the actual Hz value are not yet traced.
   - Parameters are held as packed bitfields (e.g. (x>>15)&0x7fff) and dequantised as raw/32767*300 and raw/16383*100. Constants read from ego_r: 32767.0, 300.0, 16383.0 and 100.0. The result is rounded to 2 decimals (×10² round ÷10²).
   - This independently confirms EgoCore's scales for life/start (/32767*300) and particles per second (/16383*100).
   - A burst is generated as GFRandom plus ((packed & 0x3ff) - ((packed>>10) & 0x3ff)), with the seed taken from `GetRandomSeed` @0x00aaef60.
   - For continuous emission, fractional counts accumulate at this+0x2c.
   - The delay counter at this+0x28 is initialised as -round(tickRate * start).
   [verified-from-code]
6. **Rendering.** These come from PARTICLE_LIGHTING_VIEWER_HANDOFF.md (checked there against the code, not re-derived here):
   - Blend: ADDITIVE(3) = ONE/ONE; ADDSMOOTH(4) = ONE/INVSRCCOLOR; everything else SRCALPHA/INVSRCALPHA. BlendOp ADD/SUB/REVSUB.
   - Quad height = size * frameH/frameW.
   - N crossed planes at baseAngle + (i+0.5)*0.5turn/N.
   - UV scale = real/allocated texture size.
   - PSHADER_SPRITE_GROUP: rgb = 2*tex*v, a = tex*v.
   - Colour runs Start→Mid→End over the particle's life.
   - SpriteFlags bits: 0x01 3D face-me, 0x02-0x10 align, 0x20 rotate-centre, 0x40 rotate-Z, 0x80 lit, 0x100 mod2x, 0x200 no-Z.
7. **In-game hooks.** The FSE CreateEffectAtPos / CreateEffectOnThing bindings wrap `CGameScriptInterface::CreateEffect` (two overloads in the ego_r PDB). [verified-from-code, manifest + symbols]
   - effects.big is opened once through CParticleEmitterDatabase::OpenBank at boot, so edited effects need a game restart. [inferred]

**Blockers.** 1. **The simulation is not yet decompiled into a portable form.** EmitterGeneric::Update, GenerateParticle, UpdateNormal::Update and SingleSprite/Light/Attractor/Orbit/Spline Update are about 1,400 lines of Ghidra output in ego_r. It is readable because ego_r has symbols, but the bitfield layouts of the components (this+0x34/0x3e/0x40/0x4c, ...) still need mapping to EgoCore field names. The egor struct_layouts TSV has no concrete CPSC* layouts, only CPSCBase at 12 bytes. The FableWin.pdb / Ego_d.pdb type info may have them. [verified-from-code for the gap]
2. **Tick rate.** The Hz value set by SetTickRate (@0xaaee67) and GFRandom's algorithm are not yet traced. Both are needed for a frame-faithful preview; a visually faithful one can live without them. [verified-from-code that these are the dependencies]
3. **Wrong addresses in docs.** Any doc or plan that uses egor addresses as retail Fable.exe addresses is wrong. Retail addresses must come from BSim/RTTI or string xrefs (e.g. the component class-name strings). This matters for retail breakpoints or in-game verification, not for the editor. [verified-from-code]
4. **Renderer work.** The GUI renderer needs new blend states (ONE/ONE, ONE/INVSRCCOLOR), a dynamic camera-facing quad buffer, and a per-frame update loop for the viewport. It also has to cope with DXT1/DXT3 alpha and allocated-vs-real texture sizes, which the texture path already decodes.
5. **Parser coverage.** effects.cpp throws away most fields. Editing needs a lossless model and a writer. Options: port EgoCore's ParticleParser/ParticleCompiler (both MIT), or extend effects.cpp to keep every field and add an exact inverse.
6. **Writing effects.big.** forge::big::File::serialize() needs openFully() and entry.data overrides. Adding a new entry id also needs a matching particles.h enum entry if defs are to reference it by id; placed emitters use the name only. A new entry has not been verified in-game (EFFECTS_FORMAT.md §6). Writes go through the mod/staging path with .forge-orig backups; the retail install is never written directly.
7. **Coordination with Aeon.** EgoCore already owns bank-level effect editing (property editor and compiler). Duplicating a full property editor in FableForge would conflict. The simulation core is the piece both tools need, and the natural thing to share (MIT/MIT).

**Plan.**

1. P0 quick wins (visibility): set thingsexport::Options.particles=true in gui/app.cpp:754 and :944, behind an 'Effects' layer chip next to Objects/Foliage (app.cpp:1904). Make proxies pickable: tag the proxy instances with the thing index so a click selects the emitter; give meshless emitters a small diamond glyph + selection box. Fix the misleading log at editor.cpp:3295. Test: open a map with retail emitters (e.g. OakValeWest BUTTERFLY_BLUE, a torch CREATEPARTICLE BRAZIERFIREFINAL) and check the thingsexport stats line 'N of M particle emitters proxied' (thingsexport.cpp:400); UI automation: place_emitter SMOKE, then select it by viewport click; screenshot. *(effort S (0.5-1 day), value Emitters become visible and selectable in the editor at all; today they are invisible.)*
2. P0 ParticleID dropdown: in the property panel (editor.cpp ~488), special-case CTCDParticleEmitter.ParticleTypeName (kind string) with a searchable combo from effects::entryNames(), the way vanilla's CGuiVarTransferSymbolVal dropdown works (0x01d68d36/0x01d68e40). Keep the quoted spelling and flag unknown names in red, matching vanilla's assert at 0x01d64f4f. Changing it should re-proxy the thing. Also expose IndependantObject as a bool. Test: change BUTTERFLY_BLUE to FIREFLIES, save, reload the .tng and diff: only the ParticleTypeName line changes. *(effort S (0.5 day), value Vanilla parity for the only emitter field vanilla exposes.)*
3. P1 lossless model: extend src/effects.cpp to keep every field (mid/end colours, flags, emitter shape/velocity/spread, UpdateNormal physics, SpriteFlags, NoCrossedSprites, anim frames, spline points, light timeline), named after EgoCore ParticleParser.h / ParticleProperties.h. Alternatively vendor EgoCore's parser+compiler (MIT, credit AeoN) into libs/forgecore as forge/particles. Add an exact writer, the inverse quantisation from ParticleCompiler.h. Test: a ctest that round-trips all 1165 entries byte-exact against the install's effects.big (read-only), mirroring parse_effects.py --validate, and cross-checks the dequantised values against tools/parse_effects.py JSON for a sample (ids 137, 313, 314, 130, 926). *(effort M (1-2 days), value The foundation for simulating and editing; confirms the decoding is lossless.)*
4. P1 decompile the engine simulation from ego_r (symbols): headless DecompFuncs on EmitterGeneric::Update 0x00ad2d60, GenerateParticle 0x00ad1ac0, GetRandomRadius 0x00ad0ff0, ChangeOrientation 0x00ad10b0, UpdateNormal::Update 0x00ace770, SingleSprite::Update 0x00ac9c30, Light::Update 0x00ad9e50, Attractor::Update 0x00ad90d0, Orbit::StartUpdate/COrbitState::Update 0x00ad7c90/0x00ad73e0, Spline::StartUpdate 0x00adc560, RenderSprite::PreparePrimitives 0x00abe360, and ReadBinary for each component (field to bitfield offset map). Trace the SetTickRate(0xaaee67) callers for the Hz value and GFRandom. Recover field names and defaults from DatabaseSerialise with tools/transfer_extract (the same Transfer method as defs). Write docs/formats/PARTICLE_RUNTIME.md in FableTLC. Mark every constant as verified. *(effort M-L (2-4 days), value Engine-faithful behaviour (spawn, velocity, gravity/drag, colour/size ramps, timelines, bursts) taken from code instead of invented.)*
5. P2 in-viewport preview: a small CPU simulator in libs/forgecore (forge/particlesim) that ports those functions (fixed tick at the engine rate, the engine's seeded RNG) and a renderer path in gui/renderer.cpp. Renderer: dynamic quad VB batched by (texture, blend); blend states ADDITIVE ONE/ONE, ADDSMOOTH ONE/INVSRCCOLOR, default SRCALPHA/INVSRCALPHA, plus BlendOp SUB/REVSUB; crossed planes at baseAngle+(i+.5)*0.5turn/N; height = size*frameH/frameW; UV scale real/allocated; pixel rgb = 2*tex*col, a = tex*col; depth test on, no depth write. Simulate only emitters within MaxDrawDistance of the camera, honouring FadeIn/FadeOut, with a global on/off and a 'pause/step' control. Lights feed the existing point-light path. Tests: a headless deterministic snapshot (particle count/positions after N ticks for CANDLE_FLAME 137, BRAZIERFIREFINAL 926, LARGEWATERFALL 130); golden screenshots through the UI automation; a side-by-side against an in-game capture via the live link (next step) for the candle and brazier. *(effort L (4-7 days), value The real effect in the editor, which vanilla had through its embedded engine; the core feature.)*
6. P2 in-game preview via the live link: add sendCreateEffect(fxName, x, y, z) to src/livelink (Lua: CreateEffectAtPos) plus a 'Play in game here' button on the effect card and on the selected emitter. Coordinate launches with the user (shared install; never launch Fable.exe ourselves). *(effort S (0.5-1 day), value Ground-truth check of the preview and of placed emitters without a region reload.)*
7. P3 effect editing (complement EgoCore, don't clone it): an 'Effect' inspector for the selected emitter showing the key visual fields (colours start/mid/end, sizes, rate, life, spread, blend, sprite picker from the textures bank) with the live viewport preview updating as you edit. Edits make a mod-local clone (new name, e.g. MYMOD_BRAZIER_GREEN) and never touch retail entries. The deep, all-fields editor stays in EgoCore: add 'Open in EgoCore' plus import/export of EgoCore's particle JSON (EgoCore main.cpp --extract-particles) so both tools read and write the same data. Test: clone 926 as green fire, round-trip, place it, verify in-game via the live link after a restart. *(effort M (2-3 days), value Mods can make new looks in context (see it on the torch in the level), a workflow EgoCore's standalone preview can't give.)*
8. P3 creating and packaging new effects: write new entries into a staged effects.big (forge::big openFully + entry.data + serialize), append new names/ids to a staged particles.h, and include them in mod packs (FMP) and the merge path with .forge-orig backups. Validate that name references resolve (TNG ParticleTypeName, CREATEPARTICLE dummies, CParticleAttacherDef ids) before deploy. The new-entry path is unverified in-game (EFFECTS_FORMAT.md §6), so test from a New Game with a placed emitter first, then with a def id reference. *(effort M (2 days), value Fully new effects shippable in mods, closing the ROADMAP_1.0 0.17 'Effect editor' item.)*
9. Coordination with Aeon (do early): share this plan; propose that FableForge own in-world placement, preview-in-context, the live-link check and the sim core as an MIT header-only lib (forge/particlesim), and that EgoCore own bank-level editing/compiling. Offer the decompiled runtime notes (P1) so EgoCore's standalone preview can use the same sim. *(effort S, value No duplicate property editors; one faithful simulator shared by both tools.)*

**Confidence.** High on the current FableForge state; every file and line was read. High on vanilla FableWin's particle surface: the dialog list, the two .tng keys, the ParticleID dropdown, EditorDraw drawing the graphic appearance, and the console commands all come from symbols and disassembly. High on the format and reference chain, since the FableTLC docs are byte-validated at 1165/1165.

Medium on the simulation details. Only EmitterGeneric::Update was read closely; the other update functions are decompiled or listed but not yet mapped to fields. The tick rate and RNG algorithm are open.

Also confirmed: egor_pdb_names addresses are ego_r.exe, not retail. Inferred and unverified: that effects.big loads only at boot, and that Aeon's pending work is a simulated preview in EgoCore.

Scratch outputs are in C:\Users\Cornelio\AppData\Local\Temp\claude\D--Documents-FableTLC\130e9b68-87fa-44cd-90d1-1630b5eeb085\scratchpad\lanes\particles\: egor_sim.c (ego_r decompilation of UpdateNormal::Update, EmitterGeneric::Update and GenerateParticle), fwdis.py and thunk.py (FableWin capstone helpers), and retail_sim.c (the retail-address decompilation that showed the address mismatch).

**Verifier.** accurate: The research is largely accurate. I checked the FableForge files, lines, symbols, docs and the install. Corrections and additions, most important first:

1. **Branch attribution is wrong.** The research says all particle work is on feat/editor-ui-shell and that none of the other branches has any. In fact 8e5c39a (the parser and proxies), 2c8e11f (particles opt-in) and ade2a3b (the effect picker) are all ancestors of **main**, and also of water, feat/nav-thing-lines and fix/overworld-neighbour-grow. Checked with `git merge-base --is-ancestor`: exit code 0 for all of them. src/effects.cpp and src/effects.hpp are identical on those branches. The water and overworld branches differ only in thingsexport.cpp: water uses its own tng loading (WAD instead of levelstore), a change not related to particles. So the particle base is already in main, and none of the branches adds anything beyond it. [verified-from-code]

2. **SetTickRate address.** The ego_r symbol is `?SetTickRate@NParticleEngine@@YIXJ@Z` at **0x00aaee60**, not 0xaaee67. 0xaaee67 is at most the store instruction inside the function. GetTickRate at 0x00aaee80 and GetRandomSeed at 0x00aaef60 are correct. [verified from egor_pdb_names.tsv]

3. **FableWin "string" and "SetParticleID" addresses are push sites, not strings or function entries.**
   - 0x01d68cda is `push 0x40d7c64` ("ParticleEmitter") and 0x01d68cf9 is `push 0x40d7c74` ("ParticleID"). The strings themselves are at 0x040d7c64/0x040d7c74.
   - 0x01d68e40 is `push 0x40d7c80` ("ParticleID"). No `SetParticleID` symbol exists in fablewin_editor_symbols.tsv, so that name is inferred.
   - GetParticleSymbols does exist in FableWin at 0x01d65210.
   - Treat 0x01d64d7a, 0x01d64e13, 0x01d64ea7 and 0x01d64f4f as instruction sites inside functions too, not function starts. My capstone pass from 0x01d64d70 was misaligned, so I did not re-confirm those.

4. **Test map name.** OakValeWest_v2.tng contains no BUTTERFLY_BLUE. The files that do are StartOakValeWest, GuildWoods, BowerstoneSlums_v2, DesertedFarm, BanditCampPathEntrance and the two DemonDoor maps. SUNBEAMS (122 placements, e.g. ArenaExterior.tng:668) and OAKVALE_BURNING_* are the most common retail emitters and make better P0 test cases. [verified from install]

5. **Missed: the proxy treats only blend mode 3 as additive.** thingsexport.cpp:166-170 and the proxy cache key at :154 check `sp.blendMode == 3` only. ADDSMOOTH (4, ONE/INVSRCCOLOR), which the handoff doc uses for the candle flame, therefore gets the plain alpha path. Also, the additive alpha is lum*2 of the **untinted** texel. This can be fixed cheaply in P0.
   - Other blend modes exist too: the handoff lists 5 CONST_COLOUR. The research's "everything else SRCALPHA/INVSRCALPHA" matches the handoff table.

6. **Missed: EgoCore's JSON is export-only.** EgoCore's `--extract-particles <effects.big> <out.json>` (main.cpp:435/813) only exports. The P3 idea of "import/export of EgoCore's particle JSON so both tools read and write the same data" assumes an import path that EgoCore does not have. It would need Aeon to add one, or FableForge would have to write effects.big itself.

7. **Minor: the live-link host is still the Party Mode sidecar.** The livelink.hpp commands go through the host `PartyMode/PartyMode.lua` (install/isInstalled default). Project memory says Party Mode was removed from the New Oakvale bundle. The P2 live-link step should confirm which FSE host is actually installed before adding sendCreateEffect.

8. **Minor: EFFECTS_FORMAT.md §6 names the add path.** It says a new entry is untested in-game and names the add path as big_write `adds=`. It also lists Ghidra follow-ups that the research partly answers: loader quantisation constants and terminator handling. P1 should update that doc.

**Confirmed as stated** [verified]:
- effects.cpp is 274 lines, with the API openBank, byName, entryNames and entryCount, and no writer.
- `Options::particles=false` (thingsexport.hpp:54). The CLI sets it at export.cpp:167/380.
- gui/app.cpp:754 and :944 never set particles.
- The misleading log at editor.cpp:3295.
- placeEmitter at editor.cpp:3282, leveledit.cpp:2076 and automation.cpp:425.
- vanilla_props.inc:115 has kind enum with empty pairs, and the enum branch at editor.cpp:488 requires K::Int.
- renderer.cpp:394-403 has only opaque and SRCALPHA/INVSRCALPHA blend states.
- livelink has teleport, spawn, ping and reload, but no effect command.
- EgoCore ParticleParser.h is 720 lines, ParticleCompiler.h 444 and ParticleProperties.h 871. BankTabUI.h:463 is CreateNewParticleEntry, and g_ParticlePreviewTexID is at ParticleProperties.h:18.
- FableWin addresses match the symbol table: EditorDrawPreparePrimitivesForRendering 0x01d668b0, SetAsDrawable 0x01d68390, the console commands 0x018bf490, 0x018bfe30, 0x018c0150 and 0x018c0450, and TransferUpdateParametersToDialog 0x02f10250.
- The FableWin WriteBinary addresses are correct: 0x02f0dbb0 and 0x02f17d40, plus the components 0x02f2d730 to 0x02f5c270. There is also an extra CPSCSplineBase WriteBinary at 0x02f60500.
- The ego_r addresses match egor_pdb_names.tsv: OpenBank 0x00aadfe0, PreparePrimitives 0x00abe360, UpdateNormal::Update 0x00ace770, GenerateParticle 0x00ad1ac0, EmitterGeneric::Update 0x00ad2d60, and the two CreateEffect overloads at 0x008d1930 and 0x008ddc30.
- The effects.big header layout matches EFFECTS_FORMAT.md: magic 0x64, 7 bools, 5 floats, i32 Priority, 5 bools. The doc validates 1165 of 1165 entries exactly.
- The ADDITIVE and ADDSMOOTH blend tuples match the handoff table.
- egor_sim.c shows `-ROUND(tickRate*start)` at +0x28, the fractional accumulator at +0x2c, and the burst `(x&0x3ff)-((x>>10)&0x3ff)`.
- The install has particles.h under Defs/RetailHeaders/pc.
- No FableTLC doc gives the tick rate, so that gap is real.
