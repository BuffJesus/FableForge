#!/usr/bin/env python3
"""Replace static geometry in tiny banks; independently audit entry preservation."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
from test_big_entry_markers import archive
from test_mod_field_details import write_bank
from test_meshimport import write_glb
from test_texture_lookup import texture_bank


def read_archive(path):
    data=path.read_bytes();word=lambda offset:struct.unpack_from('<I',data,offset)[0]
    cursor=word(8);count=word(cursor);cursor+=4;banks=[]
    for _ in range(count):
        end=data.index(0,cursor);name=data[cursor:end];cursor=end+1
        version,entries,start,size,alignment=struct.unpack_from('<5I',data,cursor);cursor+=20
        row=start+4+8*word(start);records=[]
        for _ in range(entries):
            row_start=row;magic,identity,kind,length,offset,dev_type,name_size=struct.unpack_from('<7I',data,row);row+=28
            symbol=data[row:row+name_size];row+=name_size
            crc,sources=struct.unpack_from('<2I',data,row);row+=8;source_names=[]
            for _ in range(sources):
                length_name=word(row);row+=4;source_names.append(data[row:row+length_name]);row+=length_name
            info_size=word(row);row+=4;info_offset=row;info=data[row:row+info_size];row+=info_size
            records.append({'id':identity,'type':kind,'name':symbol,'magic':magic,'dev_type':dev_type,
                            'crc':crc,'sources':source_names,'info':info,'payload':data[offset:offset+length],
                            'row':row_start,'offset':offset,'info_offset':info_offset})
        assert row==start+size
        banks.append({'name':name,'version':version,'alignment':alignment,'records':records})
    return banks


def signature(banks):
    return [(bank['name'],bank['version'],bank['alignment'],[
        {key:value for key,value in entry.items() if key not in ('row','offset','info_offset')}
        for entry in bank['records']]) for bank in banks]


def material_records(payload):
    """Read material byte ranges independently for the bounded no-helper donor."""
    header=payload.index(0)+1+1+40+10
    count=struct.unpack_from('<i',payload,header)[0]
    cursor=header+16+5+48;records=[]
    for _ in range(count):
        start=cursor;cursor+=4
        cursor=payload.index(0,cursor)+1
        cursor+=28
        flags=payload[cursor:cursor+5];cursor+=5
        if flags[4]:
            for _ in range(4): cursor=payload.index(0,cursor)+1
        records.append(payload[start:cursor])
    return records


def main():
    repo=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--forge',type=Path,default=repo/'build/forge.exe')
    parser.add_argument('--tool',type=Path,default=repo/'build/forge-tools.exe')
    args=parser.parse_args();forge=args.forge.resolve();tool=args.tool.resolve()
    work=Path(tempfile.mkdtemp(prefix='mesh-replace-',dir=repo/'build'));root=work/'install'
    print('Evidence retained at',work,flush=True)
    schema=json.loads((repo/'docs/re_reference/def_schema.json').read_text(encoding='utf-8'))
    fields=schema['OBJECT']['fields']
    values=[struct.pack('<IIIff',1,1,0,1,1) if field['name']=='Graphic' else 0 for field in fields]
    write_bank(root,values,definition='OBJECT',record='OBJECT_BARREL_UNBREAKABLE',fields=[field['name'] for field in fields])
    archive(root/'data/graphics/graphics.big',b'BIGB',[
        ('MBANK_ALLMESHES',205,[(1,1,'Donor',b'opaque donor')]),
        ('UnrelatedBank',17,[(93,99,'Opaque',b'untouched other bank payload')])])
    texture_bank(root/'data/graphics/pc/textures.big',[(1,'Existing',20)])
    model=work/'cube.glb';write_glb(str(model))
    def run(label,*arguments,success=True):
        result=subprocess.run([str(forge),*map(str,arguments)],capture_output=True,text=True,timeout=60)
        (work/(label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        assert (result.returncode==0)==success,(label,result.stdout,result.stderr)
        return result
    run('seed','mesh-import',model,'REPLACE_TEST','--texture-id','1','--install',root)
    graphics=root/'data/graphics/graphics.big'
    original=read_archive(graphics);target=next(entry for entry in original[0]['records'] if entry['name']==b'MESH_REPLACE_TEST')
    source_id=target['id'];checks=[]
    baseline={path.relative_to(root):hashlib.sha256(path.read_bytes()).hexdigest() for path in root.rglob('*') if path.is_file()}
    polygon=work/'replacement.obj'
    polygon.write_text('v 0 0 0\nv 3 0 0\nv 3 3 0\nv 2 3 0\nv 2 1 0\nv 1 1 0\nv 1 3 0\nv 0 3 0\nf 1 2 3 4 5 6 7 8\n',encoding='utf-8')
    polygon_hash=hashlib.sha256(polygon.read_bytes()).hexdigest()
    for selector,stored in [('MESH_REPLACE_TEST',False),(str(source_id),True)]:
        before=read_archive(graphics)
        run('replace-'+selector,'mesh-replace',polygon,selector,*(['--stored'] if stored else []),'--install',root)
        after=read_archive(graphics)
        for bank_before,bank_after in zip(before,after):
            assert (bank_before['name'],bank_before['version'],bank_before['alignment'])==(bank_after['name'],bank_after['version'],bank_after['alignment'])
            assert len(bank_before['records'])==len(bank_after['records'])
            for old,new in zip(bank_before['records'],bank_after['records']):
                if old['name']==b'MESH_REPLACE_TEST':
                    assert {key:value for key,value in old.items() if key not in ('payload','info','row','offset','info_offset')}=={
                        key:value for key,value in new.items() if key not in ('payload','info','row','offset','info_offset')}
                    assert old['info'][:4]==new['info'][:4] and old['info'][56:]==new['info'][56:]
                else: assert signature([{**bank_before,'records':[old]}])==signature([{**bank_after,'records':[new]}])
        info=subprocess.run([str(tool),'mesh-info',str(graphics),'MESH_REPLACE_TEST','--json'],capture_output=True,text=True,check=True,timeout=30)
        decoded=json.loads(info.stdout);assert decoded['vertices']==8 and decoded['triangles']==6 and decoded['id']==source_id,decoded
        for relative,digest in baseline.items():
            if relative!=graphics.relative_to(root): assert hashlib.sha256((root/relative).read_bytes()).hexdigest()==digest,relative
        assert hashlib.sha256(polygon.read_bytes()).hexdigest()==polygon_hash
        assert not list(root.glob('.forge-mesh-replace-*'))
        checks.append('replace-'+selector)
    # Noncanonical raw flag bytes and opaque material words must survive the
    # public CLI, not just the low-level composer round trip.
    donor=next(entry for entry in read_archive(graphics)[0]['records'] if entry['name']==b'MESH_REPLACE_TEST')
    first=material_records(donor['payload'])[0]
    material_offset=donor['payload'].index(0)+1+1+40+10+16+5+48
    fields_offset=material_offset+4+len(first[4:first.index(0,4)+1])
    raw=bytearray(graphics.read_bytes())
    struct.pack_into('<I',raw,donor['offset']+fields_offset+20,0x81234567)
    struct.pack_into('<I',raw,donor['offset']+fields_offset+24,0xFFFFFFFF)
    raw[donor['offset']+fields_offset+28:donor['offset']+fields_offset+31]=bytes([2,3,4])
    graphics.write_bytes(raw)
    expected_materials=material_records(next(entry for entry in read_archive(graphics)[0]['records'] if entry['name']==b'MESH_REPLACE_TEST')['payload'])
    run('raw-material-preservation','mesh-replace',polygon,'MESH_REPLACE_TEST','--stored','--install',root)
    actual_materials=material_records(next(entry for entry in read_archive(graphics)[0]['records'] if entry['name']==b'MESH_REPLACE_TEST')['payload'])
    assert actual_materials==expected_materials
    checks.append('raw-material-preservation')
    # Every refusal gets its own tiny install and must preserve all bytes.
    for label,mutation,selector in [
        ('missing',None,'MESH_MISSING'),('animated','type','MESH_REPLACE_TEST'),
        ('duplicate-id','duplicate-id','MESH_REPLACE_TEST'),('helpers','helpers','MESH_REPLACE_TEST'),
        ('animated-header','animated-header','MESH_REPLACE_TEST'),
        ('bone-header','bone-header','MESH_REPLACE_TEST'),
        ('helper-tail','helper-tail','MESH_REPLACE_TEST'),
        ('root-translation','root-translation','MESH_REPLACE_TEST'),
        ('root-nan','root-nan','MESH_REPLACE_TEST'),
        ('safe-radius','safe-radius','MESH_REPLACE_TEST'),('missing-physics','missing-physics','MESH_REPLACE_TEST'),
        ('multiple-lods','lod','MESH_REPLACE_TEST'),('slot-count','slots','MESH_REPLACE_TEST')]:
        install=work/label;shutil.copytree(root,install);bank=install/'data/graphics/graphics.big'
        raw=bytearray(bank.read_bytes());records=read_archive(bank)[0]['records']
        entry=next(row for row in records if row['name']==b'MESH_REPLACE_TEST')
        if mutation=='type': struct.pack_into('<I',raw,entry['row']+8,2)
        elif mutation=='duplicate-id': struct.pack_into('<I',raw,records[0]['row']+4,source_id)
        elif mutation=='helpers':
            header=entry['offset']+entry['payload'].index(0)+1+1+40
            struct.pack_into('<H',raw,header,1)
        elif mutation=='animated-header': raw[entry['offset']+entry['payload'].index(0)+1]=1
        elif mutation in ('bone-header','helper-tail'):
            header=entry['offset']+entry['payload'].index(0)+1+1+40
            struct.pack_into('<H',raw,header+(18 if mutation=='bone-header' else 8),1)
        elif mutation in ('root-translation','root-nan'):
            matrix=entry['offset']+entry['payload'].index(0)+1+1+40+10+16+5
            struct.pack_into('<f',raw,matrix+(36 if mutation=='root-translation' else 0),5 if mutation=='root-translation' else float('nan'))
        elif mutation=='safe-radius': struct.pack_into('<f',raw,entry['info_offset']+52,2)
        elif mutation=='missing-physics': struct.pack_into('<I',raw,entry['info_offset'],9999)
        elif mutation=='lod': struct.pack_into('<I',raw,entry['info_offset']+44,2)
        bank.write_bytes(raw)
        input_model=polygon
        if mutation=='slots':
            input_model=work/'two-slots.obj';input_model.write_text('v 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl A\nf 1 2 3\nusemtl B\nf 1 2 3\n',encoding='utf-8')
        hashes={path:hashlib.sha256(path.read_bytes()).hexdigest() for path in install.rglob('*') if path.is_file()}
        run(label,'mesh-replace',input_model,selector,'--install',install,success=False)
        assert hashes=={path:hashlib.sha256(path.read_bytes()).hexdigest() for path in hashes}
        assert not list(install.glob('.forge-mesh-replace-*'))
        checks.append(label)
    (work/'result.json').write_text(json.dumps({'passed':True,'checks':checks},indent=2)+'\n',encoding='utf-8')
    print(f'Static mesh replacement: {len(checks)} checks passed; unrelated entries and banks unchanged.')


if __name__=='__main__':
    main()
