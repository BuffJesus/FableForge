# README screenshots (tools/readme_shots.py): three views of the editor on the retail install.
# Nothing is written to the install: the save root is a scratch folder and no deploy runs.
wait_maps
wait_ready
set saveroot build/readme_install
# 1. Oakvale in Edit mode, a barrel selected with the gizmo up (things + foliage + water)
select OakValeWest_v2
wait_loaded
wait_foliage
edit 1
frames 2
edit_tab 0
camera 96 108 8 0.75 1.0 62
frames 2
place OBJECT_BARREL_BREAKABLE
wait_foliage
frames 2
gizmo 0
frames 3
screenshot docs/screenshot_oakvale.png
# 2. Terrain tab: the sculpt brush over Greatwood's lake shore
select Greatwood_1
wait_loaded
wait_foliage
edit 1
frames 2
edit_tab 1
terrain_mode 0
brush 12 6
camera 48 96 45 0.9 0.85 95
frames 2
terrain_stroke 48 96 1
frames 3
screenshot docs/screenshot_greatwood.png
# 3. The Arena from the exporter side: textured preview, orbited
edit 0
frames 2
select Arena
wait_loaded
wait_foliage
frames 2
camera 32 38 91 2.4 0.92 58
frames 3
screenshot docs/screenshot_arena.png
quit
