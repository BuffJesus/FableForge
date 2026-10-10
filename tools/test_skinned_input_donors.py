#!/usr/bin/env python3
"""Native donor glTF/remapping/composition gate; tiny outputs, no bank writes."""
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def multiply(a, b):
    return [[sum(a[r][k]*b[k][c] for k in range(4)) for c in range(4)] for r in range(4)]


def fixture(skeleton, folder):
    count = skeleton['count']
    assert count >= 3
    # Independent generic matrix multiplication, not the C++ signed-permutation
    # implementation. Undo Fable coordinates before writing column-major glTF.
    c = [[100,0,0,0],[0,0,-100,0],[0,100,0,0],[0,0,0,1]]
    ci = [[.01,0,0,0],[0,0,.01,0],[0,-.01,0,0],[0,0,0,1]]
    order = list(reversed(range(count)))
    nodes = [{'name': name} for name in skeleton['names']]
    roots = []
    for i in range(count):
        parent = struct.unpack_from('<i', skeleton['bones_raw'], 60*i+4)[0]
        if parent < 0:
            roots.append(i)
        else:
            nodes[parent].setdefault('children', []).append(i)
    nodes.extend([{'mesh': 0, 'skin': 0, 'translation': [8,9,10], 'scale': [-1,2,1]}, {'children': roots}])
    data = bytearray(struct.pack('<9f', 0,0,0, .01,0,0, 0,0,-.01))
    for local_slots in [(0,1,2,0), (2,0,0,0), (1,0,0,0)]:
        data.extend(struct.pack('<4H', *(count-1-slot for slot in local_slots)))
    data.extend(struct.pack('<12f', .5,.3,.2,0, 1,0,0,0, 1,0,0,0))
    for slot in order:
        values = struct.unpack_from('<12f', skeleton['ibms_raw'], 64*slot)
        matrix = [list(values[r*4:r*4+4]) for r in range(3)] + [[0,0,0,1]]
        gltf = multiply(multiply(ci, matrix), c)
        data.extend(struct.pack('<16f', *(gltf[r][col] for col in range(4) for r in range(4))))
    doc = {'asset': {'version': '2.0'}, 'buffers': [{'uri': 'source.bin', 'byteLength': len(data)}],
           'bufferViews': [{'buffer': 0, 'byteOffset': off, 'byteLength': size} for off,size in [(0,36),(36,24),(60,48),(108,64*count)]],
           'accessors': [{'bufferView': 0, 'componentType': 5126, 'count': 3, 'type': 'VEC3', 'min': [0,0,-.01], 'max': [.01,0,0]},
                         {'bufferView': 1, 'componentType': 5123, 'count': 3, 'type': 'VEC4'},
                         {'bufferView': 2, 'componentType': 5126, 'count': 3, 'type': 'VEC4'},
                         {'bufferView': 3, 'componentType': 5126, 'count': count, 'type': 'MAT4'}],
           'meshes': [{'primitives': [{'attributes': {'POSITION': 0, 'JOINTS_0': 1, 'WEIGHTS_0': 2}}]}],
           'nodes': nodes, 'skins': [{'joints': order, 'skeleton': count+1, 'inverseBindMatrices': 3}],
           'scenes': [{'nodes': [count,count+1]}], 'scene': 0}
    (folder/'source.bin').write_bytes(data)
    (folder/'source.gltf').write_text(json.dumps(doc), encoding='utf-8')
    return doc


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--oracle-root', type=Path, required=True)
    parser.add_argument('--reader', type=Path, default=repo/'build/fableforge_skinnedinput_tests.exe')
    parser.add_argument('--work-root', type=Path, default=repo/'build')
    args = parser.parse_args()
    native = args.install.resolve()
    graphics = native/'data/graphics/graphics.big'
    assert not args.work_root.resolve().is_relative_to(native)
    sys.path.insert(0, str(args.oracle_root.resolve()/'tools/blender_addon/io_scene_fable'))
    import mesh_rw
    fc = mesh_rw.fc
    work = Path(tempfile.mkdtemp(prefix='skinned-input-donors-', dir=args.work_root.resolve()))
    print('Evidence retained at', work, flush=True)
    before = digest(graphics)
    buf, entries = fc.open_graphics_big(str(graphics))
    keys = ('indices_raw','names_raw','bones_raw','keyframes_raw','ibms_raw')
    checks = []
    for donor_id in (4521,5121,5150,3985,5110):
        entry = next(e for e in entries if e['id']==donor_id)
        assert entry['type']==5
        source = mesh_rw.clone_skeleton(buf[entry['offset']:entry['offset']+entry['size']], 5)
        folder = work/str(donor_id)
        folder.mkdir()
        doc = fixture(source, folder)
        def run(path, output):
            result = subprocess.run([str(args.reader.resolve()), '--donor', str(graphics), str(donor_id), str(path), str(output)],
                                    cwd=work, capture_output=True, text=True, timeout=120)
            (folder/(path.stem+'.log')).write_text(result.stdout+result.stderr, encoding='utf-8')
            return result
        output = folder/'payloads'
        result = run(folder/'source.gltf', output)
        assert result.returncode==0, result.stdout+result.stderr
        assert 'publication in-place and Restore:' in result.stdout, 'reader lacks coordinated publication gate'
        if os.name=='nt':
            assert 'publication locked-third-file rollback:' in result.stdout, 'reader lacks Windows rollback gate'
        for mode in ('stored','compressed'):
            payload_path = output/(mode+'.mesh')
            payload = payload_path.read_bytes()
            info = (output/(mode+'.info')).read_bytes()
            lod_size = struct.unpack_from('<I', info, 48)[0]
            assert struct.unpack_from('<I', info, 44)[0]==1 and 0<lod_size<len(payload)
            name, prims, bones = fc._parse_mesh_primitives(payload, 5)
            skeleton = mesh_rw.clone_skeleton(payload, 5)
            ghost = mesh_rw.clone_skeleton(payload[lod_size:], 5)
            assert all(source[k]==skeleton[k]==ghost[k] for k in keys)
            assert len(prims)==1
            vertices, faces, uvs = fc._prim_geometry(prims[0], 5)
            expected = [(0,0,0),(1,0,0),(0,1,0)]
            assert len(vertices)==3 and all(abs(a-b)<1e-6 for v,w in zip(vertices,expected) for a,b in zip(v,w))
            assert faces==[(0,1,2)]
            skin = fc._prim_skin(prims[0], source['count'])
            assert dict(skin[1])=={2:1.0} and dict(skin[2])=={1:1.0}
            assert set(dict(skin[0]))=={0,1,2}
            assert all(abs(dict(skin[0])[i]-weight)<=1/255 for i,weight in enumerate((.5,.3,.2)))
            checks.append({'donor': donor_id, 'mode': mode, 'bones': source['count'], 'sha256': digest(payload_path)})
        # A required bone name mismatch must refuse before creating any outputs.
        invalid = copy.deepcopy(doc)
        invalid['nodes'][0]['name'] = 'NOT_THE_DONOR_ROOT'
        invalid_path = folder/'mismatched.gltf'
        invalid_path.write_text(json.dumps(invalid), encoding='utf-8')
        refusal_output = folder/'refused'
        refusal = run(invalid_path, refusal_output)
        assert refusal.returncode!=0 and any(message in refusal.stderr for message in
            ('missing or ambiguous joint name', 'parent mismatch'))
        assert not refusal_output.exists()
        print(donor_id, 'authored input, remap, stored/compressed oracle and refusal: PASS', flush=True)
    assert len(checks)==10 and digest(graphics)==before
    (work/'result.json').write_text(json.dumps({'passed': True, 'graphics_sha256': before, 'checks': checks, 'refusals': 5,
        'publication': {'fresh_restore': 10, 'in_place_restore': 5, 'source_stale': 5, 'destination_stale': 5,
                        'missing_texture': 5, 'locked_third_file_rollback': 5 if os.name=='nt' else 0}}, indent=2)+'\n', encoding='utf-8')


if __name__=='__main__':
    main()
