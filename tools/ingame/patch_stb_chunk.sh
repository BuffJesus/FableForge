#!/bin/bash
# patch_stb_chunk.sh <chunk.bin> [map] : write a same-size chunk over the map's entry in the install's FinalAlbion_RT.stb
C="$1"; M="${2:-StartOakValeWest}"; G="/c/Programs/Steam/steamapps/common/Fable The Lost Chapters/data/Levels/FinalAlbion_RT.stb"
python - "$C" "$M" "$G" <<'PY'
import sys, subprocess, re, os
chunk, m, stb = sys.argv[1:4]
out = subprocess.run(["D:/Code/FableForge-legacy/build/forge.exe", "stb", "list", stb], capture_output=True, text=True).stdout
mt = re.search(r"\s+(\d+) bytes\s+@(\d+)\s+Data\\Levels\\FinalAlbion\\%s\.lev" % re.escape(m), out)
size, off = int(mt.group(1)), int(mt.group(2))
data = open(chunk, 'rb').read()
assert len(data) == size, (len(data), size)
with open(stb, 'r+b') as f:
    f.seek(off); f.write(data)
print("patched", m, "at", off, len(data), "bytes")
PY
