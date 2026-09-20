#!/bin/bash
# step.sh <label> [<action> <args...>] : run a gamewin action then capture to $ATLAS_SHOTS/<label>.png
W="$(dirname "$0")/gamewin.ps1"; S="${ATLAS_SHOTS:-${TEMP:-/tmp}/FableForge/shots}"; mkdir -p "$S"
label="$1"; shift
if [ $# -gt 0 ]; then powershell -NoProfile -ExecutionPolicy Bypass -File "$W" "$@" >/dev/null; fi
sleep "${ATLAS_STEP_WAIT:-3}"
powershell -NoProfile -ExecutionPolicy Bypass -File "$W" -Action capture -Output "$S/$label.png" >/dev/null
echo "$S/$label.png"
