#!/usr/bin/env python3
"""Retail smoke test: export N maps from the local Fable install with the built
CLI and validate every GLB structurally (container, accessor ranges, embedded
PNG, index bounds). Needs a Fable TLC install (auto-detected by the exe).

  python tools/retail_smoke.py [--exe build/AlbionAtlas.exe] [--count 12] [--all]
                               [--out build/smoke] [--no-textures]

Exit code 1 on any failure. Prints one line per map.
"""
import argparse, json, os, struct, subprocess, sys, time

def parse_glb(path):
    b = open(path, "rb").read()
    magic, ver, total = struct.unpack_from("<III", b, 0)
    assert magic == 0x46546C67 and ver == 2, "bad GLB magic/version"
    assert total == len(b), f"GLB length field {total} != file size {len(b)}"
    jl, jt = struct.unpack_from("<II", b, 12)
    assert jt == 0x4E4F534A and jl % 4 == 0, "bad JSON chunk"
    doc = json.loads(b[20:20 + jl])
    bo = 20 + jl
    bl, bt = struct.unpack_from("<II", b, bo)
    assert bt == 0x004E4942 and bo + 8 + bl == len(b), "bad BIN chunk"
    return doc, b[bo + 8:]

def validate(doc, bin_, textured):
    assert doc["asset"]["version"] == "2.0"
    assert doc["buffers"][0]["byteLength"] == len(bin_)
    for bv in doc["bufferViews"]:
        assert bv["byteOffset"] % 4 == 0
        assert bv["byteOffset"] + bv["byteLength"] <= len(bin_)
    prim = doc["meshes"][0]["primitives"][0]
    acc = doc["accessors"]
    pos = acc[prim["attributes"]["POSITION"]]
    n = pos["count"]
    ia = acc[prim["indices"]]
    ibv = doc["bufferViews"][ia["bufferView"]]
    idx = struct.unpack_from(f"<{ia['count']}I", bin_, ibv["byteOffset"])
    assert max(idx) < n and min(idx) >= 0, "index out of range"
    assert ia["count"] % 3 == 0
    ex = doc["meshes"][0]["extras"]
    assert n == (ex["map_width"] + 1) * (ex["map_height"] + 1), "vertex count != grid"
    assert ia["count"] == ex["map_width"] * ex["map_height"] * 6, "triangle count != cells*2"
    # min/max really bound the positions
    pbv = doc["bufferViews"][pos["bufferView"]]
    xyz = struct.unpack_from(f"<{n * 3}f", bin_, pbv["byteOffset"])
    for k in range(3):
        col = xyz[k::3]
        assert abs(min(col) - pos["min"][k]) < 1e-3 and abs(max(col) - pos["max"][k]) < 1e-3, "accessor min/max wrong"
    if textured:
        assert "images" in doc and doc["images"][0]["mimeType"] == "image/png"
        bv = doc["bufferViews"][doc["images"][0]["bufferView"]]
        png = bin_[bv["byteOffset"]:bv["byteOffset"] + bv["byteLength"]]
        assert png[:8] == b"\x89PNG\r\n\x1a\n", "embedded image is not PNG"
        w, h = struct.unpack(">II", png[16:24])
        assert w > 0 and h > 0
        assert doc["materials"][0]["pbrMetallicRoughness"]["baseColorTexture"]["index"] == 0
        return n, ia["count"] // 3, (w, h)
    return n, ia["count"] // 3, None

def trimesh_check(path, textured):
    """Independent loader check (optional): trimesh must open the GLB and see a textured terrain."""
    try:
        import trimesh
    except ImportError:
        return "trimesh n/a"
    s = trimesh.load(path, force="scene")
    assert "terrain" in s.geometry, "trimesh: no 'terrain' geometry"
    g = s.geometry["terrain"]
    assert len(g.faces) > 0 and len(g.vertices) > 0
    if textured:
        mat = g.visual.material
        img = getattr(mat, "baseColorTexture", None) or getattr(mat, "image", None)
        assert img is not None, "trimesh: terrain material has no base colour texture"
    return f"trimesh ok ({len(s.geometry)} geom, {len(s.graph.nodes_geometry)} nodes)"

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=os.path.join("build", "AlbionAtlas.exe"))
    ap.add_argument("--count", type=int, default=12)
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--out", default=os.path.join("build", "smoke"))
    ap.add_argument("--no-textures", action="store_true")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)

    lst = subprocess.run([a.exe, "list"], capture_output=True, text=True)
    if lst.returncode != 0:
        print(lst.stderr); return 1
    maps = [ln.split()[0] for ln in lst.stdout.splitlines()[1:] if ln.strip()]
    if not a.all:
        step = max(1, len(maps) // a.count)
        maps = maps[::step][:a.count]
    failures = 0
    t_all = time.time()
    for m in maps:
        out = os.path.join(a.out, m + ".glb")
        full = (maps.index(m) % 4 == 0) and not a.no_textures   # every 4th map with foliage + objects
        cmd = [a.exe, "export", m, "--out", out, "--quiet"] + (["--no-textures"] if a.no_textures else []) + (["--foliage", "--things"] if full else [])
        t0 = time.time()
        r = subprocess.run(cmd, capture_output=True, text=True)
        dt = time.time() - t0
        if r.returncode != 0:
            failures += 1
            print(f"FAIL {m:40s} exit {r.returncode}: {r.stderr.strip()[:200]}")
            continue
        try:
            doc, bin_ = parse_glb(out)
            n, tris, tex = validate(doc, bin_, not a.no_textures)
            tm = trimesh_check(out, not a.no_textures)
            print(f"ok   {m:40s} {n:7d} verts {tris:7d} tris {str(tex or '-'):12s} {os.path.getsize(out)/1e6:6.2f} MB {dt:5.2f}s  {tm}")
        except AssertionError as e:
            failures += 1
            print(f"FAIL {m:40s} {e}")
    print(f"{len(maps) - failures}/{len(maps)} maps ok in {time.time() - t_all:.1f}s")
    return 1 if failures else 0

if __name__ == "__main__":
    sys.exit(main())
