"""Level sweep metrics: export every map with objects (untextured, fast), then
measure how each placed object sits on the terrain.

For every instance under the Things root the terrain height under its origin
is sampled (bilinear on the exported heightmap) and the offset dz = origin - ground
is recorded. Objects whose origin is far above or below the ground are listed per
map; a map with many is worth a look. Also collects the exporter's own skip
counts and warnings. Output: out/sweep/metrics.json + a markdown summary.

    python tools/sweep_metrics.py [--maps a,b,c] [--exe build/AlbionAtlas.exe]
"""
import json, os, re, struct, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, 'build', 'AlbionAtlas.exe')
OUT = os.path.join(ROOT, 'out', 'sweep')


def load_glb(fn):
    d = open(fn, 'rb').read()
    ln = struct.unpack_from('<I', d, 12)[0]
    j = json.loads(d[20:20 + ln])
    bin0 = d[28 + ln:]

    def acc(i):
        a = j['accessors'][i]
        bv = j['bufferViews'][a['bufferView']]
        off = bv.get('byteOffset', 0) + a.get('byteOffset', 0)
        ct = {5126: ('f', 4), 5123: ('H', 2), 5125: ('I', 4), 5121: ('B', 1)}[a['componentType']]
        n = {'VEC3': 3, 'SCALAR': 1, 'VEC2': 2, 'VEC4': 4}[a['type']]
        stride = bv.get('byteStride', ct[1] * n)
        out = []
        for k in range(a['count']):
            out.append(struct.unpack_from('<' + ct[0] * n, bin0, off + k * stride))
        return out
    return j, acc


def analyse(fn):
    j, acc = load_glb(fn)
    terrain = j['meshes'][0]
    ex = terrain.get('extras', {})
    W, H = ex.get('map_width', 0), ex.get('map_height', 0)
    pos = acc(terrain['primitives'][0]['attributes']['POSITION'])
    cx = W + 1
    heights = [p[1] for p in pos]   # Y-up export: y = height, z = -fableY, index = fy * cx + fx

    def ground(x, fy):
        if x < 0 or fy < 0 or x > W or fy > H:
            return None
        ix, iy = min(int(x), W - 1), min(int(fy), H - 1)
        fx, fyy = x - ix, fy - iy
        h00 = heights[iy * cx + ix]; h10 = heights[iy * cx + ix + 1]
        h01 = heights[(iy + 1) * cx + ix]; h11 = heights[(iy + 1) * cx + ix + 1]
        return h00 * (1 - fx) * (1 - fyy) + h10 * fx * (1 - fyy) + h01 * (1 - fx) * fyy + h11 * fx * fyy

    things_root = next((n for n in j['nodes'] if n.get('name') == 'Things'), None)
    rows = []
    if things_root:
        for ci in things_root.get('children', []):
            n = j['nodes'][ci]
            if 'mesh' not in n or n.get('name', '').startswith('child:'):
                continue
            t = n['translation']
            g = ground(t[0], -t[2])
            if g is None:
                continue
            rows.append((j['meshes'][n['mesh']]['name'], round(t[1] - g, 2), [round(v, 1) for v in t]))
    high = [r for r in rows if r[1] > 4.0]
    low = [r for r in rows if r[1] < -4.0]
    return dict(objects=len(rows), high=len(high), low=len(low),
                high_examples=sorted(high, key=lambda r: -r[1])[:5],
                low_examples=sorted(low, key=lambda r: r[1])[:5])


def main():
    args = sys.argv[1:]
    maps = None
    exe = EXE
    if '--maps' in args:
        maps = args[args.index('--maps') + 1].split(',')
    if '--exe' in args:
        exe = args[args.index('--exe') + 1]
    if maps is None:
        maps = open(os.path.join(OUT, 'maps.txt')).read().split()
    os.makedirs(os.path.join(OUT, 'glb'), exist_ok=True)
    results = {}
    t0 = time.time()
    for i, m in enumerate(maps):
        fn = os.path.join(OUT, 'glb', m + '.glb')
        p = subprocess.run([exe, 'export', m, '--things', '--no-textures', '--out', fn],
                           capture_output=True, text=True, errors='replace')
        log = p.stdout + p.stderr
        stat = re.search(r'(\d+) placed objects from (\d+) things.*?(\d+) unknown defs, (\d+) missing meshes, (\d+) unplaced', log)
        warnings = [w.strip() for w in re.findall(r'warning: (.*)', log)]
        r = dict(placed=int(stat.group(1)) if stat else -1, things=int(stat.group(2)) if stat else -1,
                 unknownDefs=int(stat.group(3)) if stat else -1, missingMeshes=int(stat.group(4)) if stat else -1,
                 warnings=warnings, rc=p.returncode)
        try:
            r.update(analyse(fn))
        except Exception as e:  # noqa
            r['analyse_error'] = str(e)
        results[m] = r
        print(f'[{i + 1}/{len(maps)}] {m}: placed {r["placed"]}, high {r.get("high", "?")}, low {r.get("low", "?")}, warnings {len(warnings)}', flush=True)
    json.dump(results, open(os.path.join(OUT, 'metrics.json'), 'w'), indent=1)
    # summary
    lines = ['# Level sweep metrics', '', f'{len(maps)} maps, {time.time() - t0:.0f}s', '',
             '| map | placed | >4 above ground | >4 below | warnings |', '|---|---|---|---|---|']
    for m, r in sorted(results.items(), key=lambda kv: -(kv[1].get('high', 0) + kv[1].get('low', 0))):
        if r.get('high', 0) + r.get('low', 0) == 0 and not r['warnings']:
            continue
        lines.append(f'| {m} | {r["placed"]} | {r.get("high", "?")} | {r.get("low", "?")} | {"; ".join(r["warnings"][:2])} |')
    open(os.path.join(OUT, 'METRICS.md'), 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
    print('wrote', os.path.join(OUT, 'METRICS.md'))


if __name__ == '__main__':
    main()
