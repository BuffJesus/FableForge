#!/usr/bin/env python3
"""Independent addon reader checks new type-5 payloads; no archive copies/writes."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


def digest(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()


def main():
    repo=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install',type=Path,required=True)
    parser.add_argument('--oracle-root',type=Path,required=True,help='FableTLC evidence checkout (read-only Python oracle)')
    parser.add_argument('--composer',type=Path,default=repo/'build/fableforge_skinnedcompose_tests.exe')
    parser.add_argument('--work-root',type=Path,default=repo/'build')
    args=parser.parse_args();native=args.install.resolve();graphics=native/'data/graphics/graphics.big'
    assert not args.work_root.resolve().is_relative_to(native)
    oracle=args.oracle_root.resolve()/'tools/blender_addon/io_scene_fable'
    sys.path.insert(0,str(oracle));import mesh_rw
    fc=mesh_rw.fc
    work=Path(tempfile.mkdtemp(prefix='skinned-composition-',dir=args.work_root.resolve()))
    print('Evidence retained at',work,flush=True);before=digest(graphics)
    result=subprocess.run([str(args.composer.resolve()),'--retail',str(graphics),str(work/'payloads')],
        cwd=repo,capture_output=True,text=True,timeout=120)
    (work/'composer.log').write_text(result.stdout+result.stderr,encoding='utf-8')
    assert result.returncode==0,result.stdout+result.stderr
    buf,entries=fc.open_graphics_big(str(graphics));keys=('indices_raw','names_raw','bones_raw','keyframes_raw','ibms_raw')
    checks=[]
    for path in sorted((work/'payloads').glob('*.mesh')):
        payload=path.read_bytes();info=path.with_suffix('.info').read_bytes();lod_size=struct.unpack_from('<I',info,48)[0]
        assert struct.unpack_from('<I',info,44)[0]==1 and 0<lod_size<len(payload)
        name,prims,bones=fc._parse_mesh_primitives(payload,5)
        skeleton=mesh_rw.clone_skeleton(payload,5)
        ghost_name,ghost_prims,ghost_bones=fc._parse_mesh_primitives(payload[lod_size:],5)
        ghost_skeleton=mesh_rw.clone_skeleton(payload[lod_size:],5)
        assert name==ghost_name and not ghost_prims and len(prims)==1
        assert all(skeleton[k]==ghost_skeleton[k] for k in keys),'ghost donor changed'
        p=prims[0];vertices,faces,uvs=fc._prim_geometry(p,5);skin=fc._prim_skin(p,skeleton['count'])
        assert p['stride']==28 and p['init']==0x14 and not p['sblocks'] and p['abc']>0
        assert all(len(ab[4])<=16 for ab in p['ablocks'])
        assert sum(ab[3] for ab in p['ablocks'])==len(vertices)
        for i in range(len(vertices)):
            joints=p['vb'][28*i+12:28*i+16];weights=p['vb'][28*i+16:28*i+20]
            assert sum(weights)==255 and sum(w>0 for w in weights)<=3
            assert all(joint%3==0 for joint in joints)
        if path.stem.startswith('fan-'):
            assert len(vertices)>21 and len(faces)==19 and p['abc']>=2
            for face_index,face in enumerate(faces):
                for k,index in enumerate(face):
                    expected_bone=(0,face_index+1,face_index+2)[k]
                    assert skin[index]==[(expected_bone,1.0)],'face corner palette remap changed'
                    if expected_bone==0:assert vertices[index]==(0.0,0.0,0.0)
                    else:assert abs(vertices[index][2]-expected_bone/20)<1e-6
        else:
            donor_id=int(path.stem.split('-')[0]);entry=next(e for e in entries if e['id']==donor_id)
            source=mesh_rw.clone_skeleton(buf[entry['offset']:entry['offset']+entry['size']],5)
            assert all(skeleton[k]==source[k] for k in keys),'raw donor changed'
            assert vertices==[(0.0,0.0,0.0),(1.0,0.0,0.0),(0.0,1.0,0.0)] and faces==[(0,1,2)]
            assert dict(skin[1])=={2:1.0} and dict(skin[2])=={1:1.0}
            assert set(dict(skin[0]))=={0,1,2}
            assert all(abs(dict(skin[0])[i]-w)<=1/255 for i,w in enumerate((.5,.3,.2)))
        checks.append(dict(payload=path.name,bones=skeleton['count'],vertices=len(vertices),faces=len(faces),blocks=p['abc'],sha256=digest(path)))
        print(path.name,'independent skeleton, geometry, skin and ghost: PASS',flush=True)
    assert len(checks)==12 and digest(graphics)==before,'missing cases or native graphics changed'
    (work/'result.json').write_text(json.dumps(dict(passed=True,graphics_sha256=before,checks=checks),indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
