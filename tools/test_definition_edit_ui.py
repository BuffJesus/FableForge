#!/usr/bin/env python3
"""Edit confirmed definition scalar drafts with guards, exact recovery and real controls."""
import argparse, hashlib, json, os, struct, subprocess, tempfile, time
from pathlib import Path
from test_mod_field_details import write_bank, read_values, FIELDS, tag
from test_texture_lookup import texture_bank
from test_fmp_links import read_bank, package_records
from test_mod_asset_choice_contributors import bank
from test_text_content_edit_ui import payload

def click(widget):return [f'reveal {widget}','frames 3',f'reveal {widget}','frames 3',f'click {widget}','frames 3']
def write_numeric_fixture(root):
    import zlib
    folder=root/'data/CompiledDefs';folder.mkdir(parents=True)
    names=bytearray();refs={}
    for name in ('CRegionScriptDef','1','NORMAL'):
        refs[name]=len(names)+4;names+=struct.pack('<I',tag(name))+name.encode()+b'\0'
    (folder/'names.bin').write_bytes(struct.pack('<5I',0,0xA8E36C34,len(refs),len(names)+4,0)+names)
    payloads=[b'\x01\0\x01'+b''.join(struct.pack('<Ii',tag(f),value) for f,value in zip(FIELDS,values)) for values in [(1,2,3,4),(5,6,7,8)]]
    chunk=zlib.compress(struct.pack('<2H',4,4+len(payloads[0]))+b''.join(payloads))
    data=bytes(9)+struct.pack('<I',2)
    data+=b''.join(struct.pack('<iii',refs['CRegionScriptDef'],refs[name],i) for i,name in enumerate(('1','NORMAL')))
    (folder/'game.bin').write_bytes(data+struct.pack('<I5I',2,0,0,2,len(chunk),len(chunk))+chunk)
MODES=('save-restore','invalid','noop','folder','pack','destination','stale','bool','float','uint','opaque','mixed-text','numeric-reselect','pack-guards','bool-noop','float-nan-noop','bool-checkbox')
def main():
    repo=Path(__file__).resolve().parents[1];parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gui',type=Path,default=repo/'build/FableForge.exe')
    parser.add_argument('--forge',type=Path,default=repo/'build/forge.exe')
    parser.add_argument('--cwd',type=Path,default=repo);parser.add_argument('--work-root',type=Path,default=repo/'build')
    parser.add_argument('--only',nargs='+',choices=MODES)
    a=parser.parse_args();work=Path(tempfile.mkdtemp(prefix='definition-edit-ui-',dir=a.work_root.resolve()))
    print('Evidence retained at',work,flush=True)
    for size,scale in [('1440x900',1),('800x600',1.5)]:
        for mode in (a.only or MODES):
            case=work/(size+'-'+mode);case.mkdir();root=case/'install \u03a9'
            if mode=='bool-noop':write_bank(root,[b'\2',b'\0',struct.pack('<f',2)],definition='CCombatAbilityAttackBase',record='SCALAR',fields=['Blockable','Knockdown','Damage'])
            elif mode=='float-nan-noop':write_bank(root,[struct.pack('<I',0x7fc01234)],definition='CSpecialAbilitiesDrainLifeDataDef',record='SCALAR',fields=['Damage'])
            elif mode=='numeric-reselect':write_numeric_fixture(root)
            elif mode in ('bool','bool-checkbox'):write_bank(root,[b'\0',b'\0',struct.pack('<f',2)],definition='CCombatAbilityAttackBase',record='SCALAR',fields=['Blockable','Knockdown','Damage'])
            elif mode=='float':write_bank(root,[struct.pack('<f',2)],definition='CSpecialAbilitiesDrainLifeDataDef',record='SCALAR',fields=['Damage'])
            elif mode=='uint':write_bank(root,[struct.pack('<I',2)],definition='COccupiableDef',record='SCALAR',fields=['TypeFlags'])
            elif mode=='opaque':write_bank(root,[1,2,3,struct.pack('<i',4)+b'OPAQUE_TRAILER'])
            else:write_bank(root,(1,2,3,4))
            levels=root/'data/Levels/FinalAlbion';levels.mkdir(parents=True);(levels/'unused.lev').write_bytes(b'')
            texture_bank(root/'data/graphics/pc/textures.big',[(1,'Existing',20)])
            target=root/'data/CompiledDefs/game.bin';names=target.parent/'names.bin';original=target.read_bytes();old_names=names.read_bytes();old_records=read_bank(root)
            destination=case/'output \u6f22';draft=case/'draft.png';external=None
            if mode=='pack-guards':
                bad_pack=case/'invalid-pack';bad_pack.mkdir();(bad_pack/'forge_pack.json').write_bytes(b'{invalid')
            if mode=='stale':write_bank(case/'external',(77,2,3,4))
            text=root/'data/lang/English/text.big';text_original=None
            if mode=='mixed-text':
                bank(text,'TEXT_MAIN',{'TXT_A':{'id':1,'type':0,'header':b'','payload':payload('Original')}})
                text_original=text.read_bytes()
            lines=['wait_maps','wait_ready',f'set uiscale {scale}']
            if mode=='folder':lines+=[f'set saveroot {destination.as_posix()}']
            if mode=='pack':
                destination.mkdir();(destination/'forge_pack.json').write_text(json.dumps({'version':1,'name':'Edited'}),encoding='utf-8')
                lines+=[f'pack_dest {destination.as_posix()}']
            if mode=='mixed-text':lines+=['assets_tab 5','wait_texts','frames 3','text_select TXT_A','text_edit','text_edit_content Text draft']
            lines+=['assets_tab 6','wait_definitions','frames 3',('definition_select NORMAL' if mode=='numeric-reselect' else 'definition_select 0'),'frames 3']
            if mode=='invalid':lines+=['definition_edit_refused 99']
            if mode=='opaque':lines+=['definition_edit_refused 3']
            lines+=click('btn_definition_edit_0')+['assert_state definition_editing 1']
            if mode=='save-restore':
                lines+=click('input_definition_edit_value')+['key_down Ctrl','key_down A','key_up A','key_up Ctrl','input_text 99',
                    'frames 3','close','frames 3','assert_state close_prompt 1','click btn_unsaved_cancel','frames 3',
                    'assert_state definition_editing 1','close','frames 3','assert_state close_prompt 1','click btn_unsaved_save','frames 3',
                    'assert_state assets_tab 6','assert_state close_prompt 0']
                lines+=click('btn_definition_save')+click('btn_definition_save_confirm')+['wait_ready','wait_file_job','wait_definitions','frames 3',
                    'assert_state definition_editing 0','assert_definition_field RandomVillagerMax 99',
                    'definition_edit 0','definition_value 88','definition_save','wait_ready','wait_file_job','wait_definitions','frames 3',
                    'assert_definition_field RandomVillagerMax 88','restore_all','wait_ready','wait_definitions','frames 3',
                    'definition_select 0','assert_definition_field RandomVillagerMax 1']
            elif mode=='bool-checkbox':
                lines+=click('input_definition_edit_value')+click('btn_definition_save')+click('btn_definition_save_confirm')
                lines+=['wait_ready','wait_file_job','wait_definitions','frames 3','assert_definition_field Blockable true']
            elif mode in ('bool-noop','float-nan-noop'):

                lines+=['definition_save','wait_ready','wait_file_job','wait_definitions','frames 3','assert_state definition_editing 0']
            elif mode=='numeric-reselect':
                lines+=['definition_value 99','definition_save','wait_ready','wait_file_job','wait_definitions','frames 3',
                    'assert_state definition_selected 1','assert_definition_field RandomVillagerMax 99']
            elif mode=='pack-guards':
                lines+=['definition_cancel',f'pack_dest {bad_pack.as_posix()}','frames 3','wait_definitions','definition_edit_refused 0',
                    f'pack_dest {(case/"missing-pack").as_posix()}','frames 3','wait_definitions','definition_edit_refused 0','assert_state definition_editing 0']
            elif mode=='mixed-text':
                lines+=['definition_value 99','close','frames 3','assert_state close_prompt 1','click btn_unsaved_save','frames 3',
                    'assert_state assets_tab 6','assert_state definition_editing 1','assert_text_edit TXT_A',
                    'definition_save','wait_ready','wait_file_job','wait_definitions','frames 3','assert_definition_field RandomVillagerMax 99',
                    'assert_text_edit TXT_A','close','frames 3','assert_state close_prompt 1','click btn_unsaved_save','frames 3',
                    'assert_state assets_tab 5','assert_text_edit TXT_A','text_save','wait_texts','frames 3','assert_text_content Text draft']
            elif mode in ('bool','float','uint','opaque'):

                field,value={'bool':('Blockable','true'),'float':('Damage','1.5'),'uint':('TypeFlags','4294967295'),'opaque':('RandomVillagerMax','99')}[mode]
                lines+=['definition_value '+value,'definition_save','wait_ready','wait_file_job','wait_definitions','frames 3',
                    'assert_state definition_editing 0','assert_definition_field '+field+' '+value]
            elif mode=='invalid':
                lines+=['definition_value 2147483648','definition_save_refused','assert_state definition_editing 1',
                    'assert_state definition_edit_failed 1','definition_value 0junk','definition_save_refused',
                    'definition_bank_refused frontend.bin','definition_select_refused 0','definition_value_refused '+('X'*128)]
                lines+=click('btn_definition_edit_cancel')+['assert_state definition_editing 0']
            elif mode=='destination':
                lines+=['definition_value 99',f'set saveroot {destination.as_posix()}','frames 3','wait_definitions','frames 3',
                    'definition_save_refused','assert_state definition_editing 1','assert_state definition_edit_failed 1','definition_cancel']
            elif mode=='stale':
                lines+=['definition_value 99',f'screenshot {draft.as_posix()}','frames 300','definition_save_refused',
                    'assert_state definition_editing 1','assert_state definition_edit_failed 1','definition_cancel']
            else:
                lines+=['definition_value '+('1' if mode=='noop' else '99'),'definition_save','wait_ready','wait_file_job','wait_definitions','frames 3',
                    'assert_state definition_editing 0','assert_definition_field RandomVillagerMax '+('1' if mode=='noop' else '99')]
            if mode=='folder':lines+=['assert_state context_ready 1']
            lines+=['quit'];script=case/'edit.txt';script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
            env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(case/'profile'),LOCALAPPDATA=str(case/'local'))
            with (case/'output.log').open('w',encoding='utf-8') as output:
                process=subprocess.Popen([str(a.gui.resolve()),'--install',str(root),'--size',size,'--auto',str(script)],
                    cwd=a.cwd.resolve(),env=env,stdout=output,stderr=subprocess.STDOUT)
                try:
                    if mode=='stale':
                        deadline=time.monotonic()+60
                        while not draft.exists() and process.poll() is None and time.monotonic()<deadline:time.sleep(.02)
                        assert draft.exists(),'draft checkpoint not reached'
                        target.write_bytes((case/'external/data/CompiledDefs/game.bin').read_bytes());external=target.read_bytes()
                    rc=process.wait(timeout=90)
                finally:
                    if process.poll() is None:process.terminate();process.wait(timeout=10)
            trace=Path(str(script)+'.log').read_text(encoding='utf-8')
            assert rc==0 and 'RESULT PASS' in trace and 'FAIL ' not in trace,trace[-6500:]
            if mode=='save-restore':
                assert 'ok   assert_state close_prompt 1' in trace and 'ok   assert_definition_field RandomVillagerMax 88' in trace
                assert target.read_bytes()==original and Path(str(target)+'.forge-orig').read_bytes()==original
            elif mode=='numeric-reselect':
                actual=read_bank(root);assert len(actual)==2
                assert actual[0]==old_records[0]
                old=old_records[1]['data'];assert actual[1]['data']==old[:7]+struct.pack('<i',99)+old[11:]
                assert Path(str(target)+'.forge-orig').read_bytes()==original
            elif mode=='mixed-text':
                assert read_values(root)==(99,2,3,4) and Path(str(target)+'.forge-orig').read_bytes()==original
                assert Path(str(text)+'.forge-orig').read_bytes()==text_original
                assert package_records(text,by_id=True)['TEXT_MAIN'][1][0]==payload('Text draft')
            elif mode in ('bool','bool-checkbox','float','uint','opaque'):
                assert Path(str(target)+'.forge-orig').read_bytes()==original
                actual=read_bank(root)[0]['data']
                if mode in ('bool','bool-checkbox'):expected=b'\1';width=1
                elif mode=='float':expected=struct.pack('<f',1.5);width=4
                elif mode=='uint':expected=struct.pack('<I',4294967295);width=4
                else:expected=struct.pack('<i',99);width=4
                old=old_records[0]['data']
                assert actual==old[:7]+expected+old[7+width:]
            elif mode in ('folder','pack'):
                assert target.read_bytes()==original and read_values(destination)==(99,2,3,4)
                assert (destination/'data/CompiledDefs/names.bin').read_bytes()==old_names
                assert Path(str(destination/'data/CompiledDefs/game.bin')+'.forge-created').exists()
                r=subprocess.run([str(a.forge.resolve()),'restore','--install',str(destination)],cwd=a.cwd.resolve(),capture_output=True,text=True)
                assert r.returncode==0 and not (destination/'data/CompiledDefs/game.bin').exists() and not (destination/'data/CompiledDefs/names.bin').exists(),(r.stdout,r.stderr)
            else:
                assert target.read_bytes()==(external if external is not None else original)
                assert not Path(str(target)+'.forge-orig').exists()
            assert names.read_bytes()==old_names and not list(case.rglob('forge-def-prepare-*'))
            print(size,mode,'scalar draft, guards and preserved bytes: PASS',flush=True)
    (work/'result.json').write_text(json.dumps({'passed':True,'sizes':2,'cases':2*len(a.only or MODES)})+'\n',encoding='utf-8')

if __name__=='__main__':main()
