#!/bin/bash
# step.sh <label> [<action> <args...>] : run a gamewin action then capture to $ATLAS_SHOTS/<label>.png
W="D:/Code/AlbionAtlas/tools/ingame/gamewin.ps1"; S="${ATLAS_SHOTS:-C:/Users/Cornelio/AppData/Local/Temp/claude/D--Documents-FableTLC/309ec680-458d-4ea1-9cba-2983e0c69c6b/scratchpad/game}"
label="$1"; shift
if [ $# -gt 0 ]; then powershell -NoProfile -ExecutionPolicy Bypass -File "$W" "$@" >/dev/null; fi
sleep "${ATLAS_STEP_WAIT:-3}"
powershell -NoProfile -ExecutionPolicy Bypass -File "$W" -Action capture -Output "$S/$label.png" >/dev/null
echo "$S/$label.png"
