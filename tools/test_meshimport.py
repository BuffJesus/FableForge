#!/usr/bin/env python3
"""Custom static mesh import (ROADMAP 0.17): a unit cube as .obj and as .glb (written here, no
libraries) goes through `forge mesh-import` into a scratch root that carries the install's
graphics.big / textures.big / CompiledDefs. Checks: MESH_<NAME> appended to MBANK_ALLMESHES with
the next id and decodable (`forge-tools mesh-info`: 24 vertices, 12 triangles, the bounds in Fable
Z-up space), OBJECT_<NAME> in game.bin with Graphic.modelId = that id and MeshHeight from the
bounds, <NAME>_DIFFUSE in textures.big, one-time .forge-orig backups. Skips without the install.
Copies ~800 MB of banks under build/ (graphics.big + textures.big). Nothing touches the install.

  python tools/test_meshimport.py [--root <fable install>] [--keep]
"""
import argparse, json, os, shutil, struct, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def find_root(explicit: str) -> str:
    if explicit:
        return explicit
    for c in [r"C:\Programs\Steam\steamapps\common\Fable The Lost Chapters",
              r"C:\Program Files (x86)\Steam\steamapps\common\Fable The Lost Chapters"]:
        if os.path.exists(os.path.join(c, "data", "CompiledDefs", "game.bin")):
            return c
    return ""


def pristine(root, rel):
    for sfx in (".retail-bak", ".forge-orig", ".atlas-orig", ".forgebak", ""):
        p = os.path.join(root, rel + sfx)
        if os.path.exists(p): return p
    return ""


# a unit cube, Y-up (glTF / Blender), 24 vertices (per-face normals), 12 triangles, CCW outward
def cube():
    faces = [((0, 0, 1), [(-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1)]),
             ((0, 0, -1), [(1, -1, -1), (-1, -1, -1), (-1, 1, -1), (1, 1, -1)]),
             ((1, 0, 0), [(1, -1, 1), (1, -1, -1), (1, 1, -1), (1, 1, 1)]),
             ((-1, 0, 0), [(-1, -1, -1), (-1, -1, 1), (-1, 1, 1), (-1, 1, -1)]),
             ((0, 1, 0), [(-1, 1, 1), (1, 1, 1), (1, 1, -1), (-1, 1, -1)]),
             ((0, -1, 0), [(-1, -1, -1), (1, -1, -1), (1, -1, 1), (-1, -1, 1)])]
    pos, nrm, uv, idx = [], [], [], []
    for n, quad in faces:
        base = len(pos)
        for i, p in enumerate(quad):
            pos.append((p[0] * 0.5, p[1] * 0.5 + 0.5, p[2] * 0.5))   # 1 unit wide, standing on y=0
            nrm.append(n)
            uv.append([(0, 1), (1, 1), (1, 0), (0, 0)][i])
        idx += [base, base + 1, base + 2, base, base + 2, base + 3]
    return pos, nrm, uv, idx


def write_obj(path):
    pos, nrm, uv, idx = cube()
    with open(path, "w", newline="\n") as f:
        f.write("o cube\n")
        for p in pos: f.write("v %g %g %g\n" % p)
        for t in uv: f.write("vt %g %g\n" % (t[0], 1 - t[1]))
        for n in nrm: f.write("vn %g %g %g\n" % n)
        f.write("usemtl wood\n")
        for i in range(0, len(idx), 3):
            a, b, c = idx[i] + 1, idx[i + 1] + 1, idx[i + 2] + 1
            f.write("f %d/%d/%d %d/%d/%d %d/%d/%d\n" % (a, a, a, b, b, b, c, c, c))


def write_glb(path):
    pos, nrm, uv, idx = cube()
    bin_ = b"".join(struct.pack("<3f", *p) for p in pos) + b"".join(struct.pack("<3f", *n) for n in nrm) + \
           b"".join(struct.pack("<2f", *t) for t in uv) + b"".join(struct.pack("<H", i) for i in idx)
    bin_ += b"\0" * (-len(bin_) % 4)
    np_, nn, nu, ni = len(pos) * 12, len(nrm) * 12, len(uv) * 8, len(idx) * 2
    mn = [min(p[k] for p in pos) for k in range(3)]; mx = [max(p[k] for p in pos) for k in range(3)]
    doc = {"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0, "name": "cube"}],
           "materials": [{"name": "wood"}],
           "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3, "material": 0}]}],
           "buffers": [{"byteLength": len(bin_)}],
           "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": np_}, {"buffer": 0, "byteOffset": np_, "byteLength": nn},
                           {"buffer": 0, "byteOffset": np_ + nn, "byteLength": nu}, {"buffer": 0, "byteOffset": np_ + nn + nu, "byteLength": ni}],
           "accessors": [{"bufferView": 0, "componentType": 5126, "count": len(pos), "type": "VEC3", "min": mn, "max": mx},
                         {"bufferView": 1, "componentType": 5126, "count": len(nrm), "type": "VEC3"},
                         {"bufferView": 2, "componentType": 5126, "count": len(uv), "type": "VEC2"},
                         {"bufferView": 3, "componentType": 5123, "count": len(idx), "type": "SCALAR"}]}
    js = json.dumps(doc).encode(); js += b" " * (-len(js) % 4)
    body = struct.pack("<II", len(js), 0x4E4F534A) + js + struct.pack("<II", len(bin_), 0x004E4942) + bin_
    with open(path, "wb") as f:
        f.write(b"glTF" + struct.pack("<II", 2, 12 + len(body)) + body)


def write_png(path):
    # a 64x64 brown PNG through the GUI-free route: the CLI has no PNG writer here, so a minimal encoder
    import zlib
    w = h = 64
    rows = b"".join(b"\x00" + bytes([139, 90, 43] * w) for _ in range(h))
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default="")
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    root = find_root(a.root)
    if not root:
        print("mesh import test skipped (no install)"); return 0
    os.chdir(ROOT)
    forge = os.path.join(ROOT, "build", "forge.exe"); tools = os.path.join(ROOT, "build", "forge-tools.exe")
    scratch = os.path.join(ROOT, "build", "mesh_root"); shutil.rmtree(scratch, ignore_errors=True)
    for d in ("data/CompiledDefs", "data/graphics/pc", "data/Levels"):
        os.makedirs(os.path.join(scratch, d))
    for rel in ("data/CompiledDefs/game.bin", "data/CompiledDefs/names.bin", "data/graphics/pc/textures.big"):
        src = pristine(root, rel)
        if not src: print("install lacks", rel); return 1
        shutil.copyfile(src, os.path.join(scratch, rel))
    gfx = pristine(root, "data/graphics/graphics.big") or pristine(root, "data/graphics/pc/graphics.big")
    if not gfx: print("install lacks graphics.big"); return 1
    shutil.copyfile(gfx, os.path.join(scratch, "data", "graphics", "graphics.big"))
    write_obj(os.path.join(scratch, "cube.obj")); write_glb(os.path.join(scratch, "cube.glb")); write_png(os.path.join(scratch, "wood.png"))
    ok = True

    def run(exe, *args, expect=0):
        r = subprocess.run([exe, *args], capture_output=True, text=True)
        if (r.returncode == 0) != (expect == 0):
            nonlocal ok; ok = False
            print("rc", r.returncode, "for", " ".join(args)); print(r.stdout[-800:], r.stderr[-400:])
        return r

    before = json.loads(run(tools, "mesh-info", os.path.join(scratch, "data", "graphics", "graphics.big"), "--max-id", "--json").stdout)
    for name, model in (("FORGE_CUBE_OBJ", "cube.obj"), ("FORGE_CUBE_GLB", "cube.glb")):
        r = run(forge, "mesh-import", os.path.join(scratch, model), name, "--texture", os.path.join(scratch, "wood.png"), "--install", scratch)
        if "OBJECT_%s ready" % name not in r.stdout: print("import did not finish:", r.stdout[-400:], r.stderr[-300:]); ok = False; continue
        info = json.loads(run(tools, "mesh-info", os.path.join(scratch, "data", "graphics", "graphics.big"), "MESH_" + name, "--json").stdout)
        if info.get("vertices") != 24 or info.get("triangles") != 12: print(name, "decoded geometry wrong:", info); ok = False
        if info.get("type") != 1 or info.get("id", 0) <= before.get("id", 0): print(name, "entry id/type wrong:", info); ok = False
        if info.get("physics_index", 0) != info.get("id", 0) - 1: print(name, "PhysicsIndex should name the hull written just before:", info.get("physics_index"), info.get("id")); ok = False
        hull = json.loads(run(tools, "mesh-info", os.path.join(scratch, "data", "graphics", "graphics.big"), "MESH_" + name + "[PHYSICS]", "--json").stdout)
        tags = [c["tag"] for c in hull.get("chunks", [])]
        if hull.get("type") != 3 or hull.get("magic") != ">>>>3DMF" or tags != ["3DRT", "MTLS", "MTRL", "SUBM", "PRIM", "TRIS", "SMTH", "VERT", "UNIV"]: print(name, "hull chunk tree wrong:", hull.get("magic"), tags); ok = False
        if hull.get("uncompressed") != hull.get("decoded"): print(name, "hull did not decompress to its declared size"); ok = False
        bb = info.get("bbox_min", []) + info.get("bbox_max", [])
        # Y-up 1 m cube standing on y=0 -> Fable Z-up centimetres: x -50..50, y -50..50, z 0..100
        want = [-50.0, -50.0, 0.0, 50.0, 50.0, 100.0]
        if len(bb) != 6 or any(abs(bb[i] - want[i]) > 1e-4 for i in range(6)): print(name, "bounds not Z-up:", bb); ok = False
        if info.get("texture_ids") != [info.get("diffuse_of_material_0")]: print(name, "info texture ids:", info.get("texture_ids"), info.get("diffuse_of_material_0")); ok = False
        dec = run(tools, "defs", "decode", scratch, "docs/re_reference/def_schema.json", "OBJECT_" + name).stdout
        mid = info.get("id", 0)
        want_graphic = "05000000" + struct.pack("<I", mid).hex()
        if want_graphic not in dec.replace(" ", ""): print(name, "Graphic.modelId not repointed:", [l for l in dec.splitlines() if "Graphic" in l]); ok = False
        if not any(l.split()[0] == "MeshHeight" and abs(float(l.split()[-1]) - 1.0) < 1e-3 for l in dec.splitlines() if l.strip().startswith("MeshHeight ")): print(name, "MeshHeight not from the bounds:", [l for l in dec.splitlines() if "MeshHeight" in l]); ok = False
        tex = run(tools, "fmp", "list", os.path.join(scratch, "data", "graphics", "pc", "textures.big")).stdout
        if name + "_DIFFUSE" not in tex: print(name, "diffuse texture not appended"); ok = False
    for rel in ("data/graphics/graphics.big", "data/graphics/pc/textures.big", "data/CompiledDefs/game.bin", "data/CompiledDefs/names.bin"):
        if not os.path.exists(os.path.join(scratch, rel + ".forge-orig")): print("no backup for", rel); ok = False
    # the editor's Import model card over the same scratch root, then the new object placed
    gui = os.path.join(ROOT, "build", "FableForge.exe")
    if os.path.exists(gui):
        r = subprocess.run([gui, "--auto", "tests/ui/meshimport.txt"], capture_output=True, text=True)
        log = os.path.join(ROOT, "tests", "ui", "meshimport.txt.log")
        tail = open(log, encoding="utf-8", errors="replace").read().strip().splitlines() if os.path.exists(log) else []
        if r.returncode != 0 or not tail or "RESULT PASS" not in tail[-1]:
            print("ui meshimport failed:", " | ".join(tail[-8:])); ok = False
        info = json.loads(run(tools, "mesh-info", os.path.join(scratch, "data", "graphics", "graphics.big"), "MESH_FORGE_CUBE_UI", "--json").stdout)
        if info.get("vertices") != 24: print("GUI import did not land in graphics.big:", info); ok = False
    if not a.keep:
        shutil.rmtree(scratch, ignore_errors=True)
    print("mesh import test", "OK" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
