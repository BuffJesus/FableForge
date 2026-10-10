#!/usr/bin/env python3
"""Follow confirmed group/member indices and verify edit/restore refresh with exact recovery."""
import argparse,hashlib,json,os,struct,subprocess,tempfile,zlib
from pathlib import Path
from test_mod_field_details import tag
from test_texture_lookup import texture_bank

def write_fixture(root,repo,extra_members=0,group_names=("3","GROUP_NORMAL")):
    folder=root/'data/CompiledDefs';folder.mkdir(parents=True)
    schema=json.loads((repo/'docs/re_reference/def_schema.json').read_text(encoding='utf-8'))['OBJECT']['fields']
    def object_payload(group):
        data=b'\x01\0\x01'
        for f in schema:
            value=group if f['name']=='GroupDef' else bytes(4)
            if isinstance(value,int):value=struct.pack('<i',value)
            data+=struct.pack('<I',tag(f['name']))+value
        return data
    records=[('OBJECT','OBJ_A',object_payload(2)),('OBJECT','OBJ_B',object_payload(3)),
        ('THING_GROUP',group_names[0],b'\x01\0\x01\0\0'),('THING_GROUP',group_names[1],b'\x01\0\x01\0\0'),
        ('OBJECT','WRONG_TYPE',object_payload(0)),('OBJECT','ABSENT',object_payload(-1)),
        ('OBJECT','OUT_OF_RANGE',object_payload(999)),('OBJECT','BAD_WIDTH',object_payload(struct.pack('<i',2)+b'EXTRA'))]
    records += [('OBJECT',f'OBJ_EXTRA_{i}',object_payload(2)) for i in range(extra_members)]
    names=bytearray();refs={}
    for t,n,_ in records:
        for name in (t,n):
            if name in refs:continue
            refs[name]=len(names)+4;names+=struct.pack('<I',tag(name))+name.encode()+b'\0'
    (folder/'names.bin').write_bytes(struct.pack('<5I',0,0xA8E36C34,len(refs),len(names)+4,0)+names)
    offsets=[];body=bytearray()
    for _,_,payload in records:offsets.append(2*len(records)+len(body));body+=payload
    chunk=zlib.compress(struct.pack('<'+'H'*len(records),*offsets)+body)
    class_counts={};identities=bytearray()
    for t,n,_ in records:
        identities+=struct.pack('<iii',refs[t],refs[n],class_counts.get(t,0));class_counts[t]=class_counts.get(t,0)+1
    data=bytes(9)+struct.pack('<I',len(records))+identities
    (folder/'game.bin').write_bytes(data+struct.pack('<I5I',2,0,0,len(records),len(chunk),len(chunk))+chunk)
    (folder/'frontend.bin').write_bytes((folder/'game.bin').read_bytes())
    levels=root/'data/Levels/FinalAlbion';levels.mkdir(parents=True);(levels/'unused.lev').write_bytes(b'')
    texture_bank(root/'data/graphics/pc/textures.big',[(1,'Existing',20)])
    return folder

def main():
    repo=Path(__file__).resolve().parents[1];parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gui',type=Path,default=repo/'build/FableForge.exe')
    parser.add_argument('--cwd',type=Path,default=repo);parser.add_argument('--work-root',type=Path,default=repo/'build');a=parser.parse_args()
    work=Path(tempfile.mkdtemp(prefix='definition-groups-ui-',dir=a.work_root.resolve()));root=work/'install'
    folder=write_fixture(root,repo)
    original={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.rglob('*') if p.is_file()}
    print('Evidence retained at',work,flush=True)
    for size,scale in [('1440x900',1),('800x600',1.5)]:
        case=work/size;case.mkdir();script=case/'groups.txt'
        lines=['wait_maps','wait_ready',f'set uiscale {scale}','assets_tab 6','wait_definitions','frames 3',
            'definition_select OBJ_A','frames 3','assert_state definition_group 2','reveal btn_definition_group','frames 3',
            'click btn_definition_group','frames 3','assert_state definition_selected 2','assert_state definition_group -',
            'reveal definition_member_0','frames 3','assert_widget definition_member_0','click definition_member_0','frames 3','assert_state definition_selected 0',
            'definition_select OBJ_B','frames 3','assert_state definition_group 3','reveal btn_definition_group','frames 3',
            'click btn_definition_group','frames 3','assert_state definition_selected 3',
            'reveal definition_member_1','frames 3','assert_widget definition_member_1','click definition_member_1','frames 3','assert_state definition_selected 1','definition_select OBJ_A','frames 3',
            'definition_edit 1','click btn_definition_group','frames 3','assert_state definition_selected 0','assert_state definition_editing 1','definition_cancel']
        lines+=['definition_edit 1','definition_value 3','definition_save','wait_ready','wait_file_job','wait_definitions','frames 3',
            'assert_state definition_group 3','reveal btn_definition_group','frames 3','click btn_definition_group','frames 3',
            'assert_state definition_selected 3','reveal definition_member_0','frames 3','assert_widget definition_member_0',
            'click definition_member_0','frames 3','assert_state definition_selected 0','assert_state definition_group 3',
            'restore_all','wait_ready','wait_definitions','frames 3','definition_select OBJ_A','frames 3',
            'assert_state definition_group 2','reveal btn_definition_group','frames 3','click btn_definition_group','frames 3',
            'assert_state definition_selected 2','reveal definition_member_0','frames 3','click definition_member_0','frames 3',
            'assert_state definition_selected 0','assert_state definition_group 2']
        for name in ('WRONG_TYPE','ABSENT','OUT_OF_RANGE','BAD_WIDTH'):
            lines+=['definition_select '+name,'frames 3','assert_state definition_group -']
        lines+=['definition_edit_refused 1','definition_bank frontend.bin','wait_definitions','frames 3','definition_select OBJ_A','frames 3','assert_state definition_group -','quit'];script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
        r=subprocess.run([str(a.gui.resolve()),'--install',str(root),'--size',size,'--auto',str(script)],cwd=a.cwd.resolve(),
            env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(case/'profile'),LOCALAPPDATA=str(case/'local')),
            capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=90)
        (case/'output.log').write_text(r.stdout+r.stderr,encoding='utf-8')
        trace=Path(str(script)+'.log').read_text(encoding='utf-8');assert r.returncode==0 and 'RESULT PASS' in trace and 'FAIL ' not in trace,trace[-5500:]
        assert 'click definition_member_0' in trace and 'click definition_member_1' in trace,trace[-5500:]
        assert all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in original.items())
        backups=list(root.rglob('*.forge-orig'));assert backups==[folder/'game.bin.forge-orig']
        assert hashlib.sha256(backups[0].read_bytes()).hexdigest()==original[folder/'game.bin']
        print(size,'group indices, member navigation, numeric names, draft protection, edit/restore refresh and invalid-reference refusal: PASS',flush=True)
    (work/'result.json').write_text(json.dumps({'passed':True,'source_hashes_unchanged':True})+'\n',encoding='utf-8')

if __name__=='__main__':main()
