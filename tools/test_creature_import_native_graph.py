#!/usr/bin/env python3
"""Native compiled graph + authored skin plans; tiny payload output, no bank writes."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from audit_creature_body_models import body_slots, mesh_directory
from audit_creature_visual_graph import analyze, listing
from test_fmp_links import read_bank


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream,'sha256').hexdigest()


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--fixtures', type=Path, required=True)
    parser.add_argument('--oracle-root', type=Path, required=True)
    parser.add_argument('--reader', type=Path, default=repo/'build/fableforge_skinnedinput_tests.exe')
    parser.add_argument('--payload-reader', type=Path, default=repo/'build/fableforge_defrelocate_tests.exe')
    parser.add_argument('--schema', type=Path, default=repo/'docs/re_reference/def_schema.json')
    parser.add_argument('--work-root', type=Path, required=True)
    args = parser.parse_args()
    native = args.install.resolve(strict=True)
    fixtures = args.fixtures.resolve(strict=True)
    assert json.loads((fixtures/'result.json').read_text())['passed']
    assert not args.work_root.resolve().is_relative_to(native)
    sources = [native/'data/CompiledDefs'/name for name in ('names.bin','game.bin')]
    sources.append(native/'data/graphics/graphics.big')
    before = {str(path):digest(path) for path in sources}
    originals = read_bank(native)
    shapes = {row['index']:row for row in analyze(originals)['records'] if not row['issues']}
    relocation = subprocess.run([str(args.payload_reader.resolve()),'--creature-visual-audit',
                                 str(native),str(args.schema.resolve())], capture_output=True,text=True,timeout=120)
    assert relocation.returncode == 0, relocation.stderr
    by_root = {row['root']:row['payloads'] for row in json.loads(relocation.stdout)['records']}
    max_mesh = max(entry['id'] for entry in mesh_directory(sources[2]))
    sys.path.insert(0,str(args.oracle_root.resolve()/'tools/blender_addon/io_scene_fable'))
    import mesh_rw
    fc = mesh_rw.fc
    work = Path(tempfile.mkdtemp(prefix='creature-native-mesh-plan-',dir=args.work_root.resolve()))
    print('Evidence retained at',work,flush=True)
    checks = []
    excluded = []
    counts = {}
    keys = ('indices_raw','names_raw','bones_raw','keyframes_raw','ibms_raw')
    for donor in (4521,5121,5150,3985,5110):
        folder = fixtures/str(donor)
        buf, entries = fc.open_graphics_big(str(folder/'source-install/data/graphics/graphics.big'))
        entry = entries[0]
        skeleton = mesh_rw.clone_skeleton(buf[entry['offset']:entry['offset']+entry['size']],5)
        count = 0
        for root,shape in shapes.items():
            visual = shape['visual_rows']
            body = visual[0]['component']
            try:
                _,slots = body_slots(originals[body]['data'])
            except ValueError:
                continue
            for slot in slots:
                if slot['id'] != donor:
                    continue
                if any(payload['unresolved'] for payload in by_root[root]):
                    label = f'refused-{donor}-{root}-{slot["body_part"]}-{slot["alternative"]}'
                    output = work/label
                    refusal = subprocess.run([str(args.reader.resolve()),'--native-plan',str(native),str(args.schema.resolve()),
                        str(folder/'source.gltf'),str(root),str(slot['body_part']),str(slot['alternative']),str(output)],
                        cwd=work,capture_output=True,text=True,timeout=120)
                    (work/(label+'.log')).write_text(refusal.stdout+refusal.stderr,encoding='utf-8')
                    assert refusal.returncode != 0 and 'unclassified possible reference' in refusal.stderr.lower(), refusal.stderr
                    assert not output.exists(), 'unresolved native graph created output'
                    excluded.append({'root':root,'donor':donor,**slot,'refusal_verified':True,
                                     'reason':'previously unresolved private graph'})
                    continue
                label = f'{donor}-{root}-{slot["body_part"]}-{slot["alternative"]}'
                output = work/label
                result = subprocess.run([str(args.reader.resolve()),'--native-plan',str(native),str(args.schema.resolve()),
                    str(folder/'source.gltf'),str(root),str(slot['body_part']),str(slot['alternative']),str(output)],
                    cwd=work,capture_output=True,text=True,timeout=120)
                (work/(label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
                assert result.returncode == 0,result.stderr
                report = json.loads(result.stdout)
                new_slots = [report['root_index'],report['body_index'],report['appearance_index']]
                assert new_slots == list(range(len(originals),len(originals)+3))
                assert report['source_entries'] == len(originals) and report['all_prior_records_preserved']
                assert report['donor_model_id'] == donor and report['mesh_id'] == max_mesh+1
                assert report['body_offset'] == slot['offset']
                nodes = [root]+[row['component'] for row in visual]
                maps = dict(zip(nodes,new_slots))
                for n,(node,payload,name) in enumerate(zip(nodes,by_root[root],('root','body','appearance'))):
                    expected = bytearray(originals[node]['data'])
                    for rewrite in payload['rewrites']:
                        assert rewrite['domain'] == 'game' and rewrite['before'] in maps
                        struct.pack_into('<I',expected,rewrite['offset'],maps[rewrite['before']])
                    if n == 0:
                        selected = {row['row']:i for i,row in enumerate(visual)}
                        for original in listing(originals[root]):
                            at = original['offset']
                            expected[at:at+12] = originals[root]['data'][at:at+12]
                            if original['row'] in selected:
                                struct.pack_into('<II',expected,at+4,new_slots[1+selected[original['row']]],new_slots[0])
                    if n == 1:
                        struct.pack_into('<I',expected,slot['offset'],max_mesh+1)
                    assert (output/(name+'.bin')).read_bytes() == expected,'unexpected native graph delta'
                mesh = (output/'mesh.bin').read_bytes()
                info = (output/'mesh.info').read_bytes()
                lod_size = struct.unpack_from('<I',info,48)[0]
                current = mesh_rw.clone_skeleton(mesh,5)
                ghost = mesh_rw.clone_skeleton(mesh[lod_size:],5)
                assert all(current[key] == ghost[key] == skeleton[key] for key in keys)
                _,prims,_ = fc._parse_mesh_primitives(mesh,5)
                vertices,faces,_ = fc._prim_geometry(prims[0],5)
                assert len(prims) == 1 and len(vertices) == 3 and faces == [(0,1,2)]
                checks.append({'root':root,'donor':donor,**slot,'mesh_id':max_mesh+1})
                count += 1
        counts[str(donor)] = count
        print(donor,count,'native graph/slot plans: '+('PASS' if count else 'NOT EXERCISED (no eligible root slot)'),flush=True)
    assert checks, 'no native graph plans exercised'
    assert {str(path):digest(path) for path in sources} == before,'native source changed'
    summary = {'passed':True,'checks':checks,'excluded':excluded,'counts':counts,'source_sha256':before,
               'publisher_verified':False,'gui_verified':False,'runtime_compatibility_verified':False}
    (work/'result.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
    print('PASS:',len(checks),'native graph plans;',len(excluded),'previously unresolved cases excluded',flush=True)


if __name__ == '__main__':
    main()
