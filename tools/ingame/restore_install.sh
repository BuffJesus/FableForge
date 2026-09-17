#!/bin/bash
# Put back the .atlas-orig backups Albion Atlas made (FinalAlbion.wad/.stb/.bwd/.wld,
# loose .lev/.tng) and remove the loose files it wrote. Usage: restore_install.sh [game-root]
G="${1:-/c/Programs/Steam/steamapps/common/Fable The Lost Chapters}"
L="$G/data/Levels"
for f in "$L/FinalAlbion.wad" "$L/FinalAlbion_RT.stb" "$L/FinalAlbion.bwd" "$L/FinalAlbion.wld" "$G/data/graphics/pc/textures.big" "$G/FinalAlbion.bwd" "$L/FinalAlbion/FinalAlbion.bwd"; do
  if [ -f "$f.atlas-orig" ]; then cp "$f.atlas-orig" "$f" && echo "restored $(basename "$f")"; fi
done
for f in "$L/FinalAlbion"/*.atlas-orig; do
  [ -f "$f" ] || continue
  orig="${f%.atlas-orig}"; cp "$f" "$orig" && echo "restored $(basename "$orig")"
done
# loose files Atlas created where none existed before carry a .atlas-created marker: remove those only
for m in "$L/FinalAlbion"/*.atlas-created; do
  [ -f "$m" ] || continue
  f="${m%.atlas-created}"; rm -f "$f" "$m" && echo "removed loose $(basename "$f") (Atlas-created)"
done
