#!/usr/bin/env python3
"""Exercise read-only definition browsing with private fixtures at two sizes."""
import argparse, hashlib, json, os, struct, subprocess, tempfile, zlib
from pathlib import Path
from test_mod_field_details import write_bank, FIELDS, tag
from test_texture_lookup import texture_bank
from test_setup_restore_ui import level

def main():
    repo=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gui',type=Path,default=repo/'build/FableForge.exe')
    parser.add_argument('--cwd',type=Path,default=repo)
    parser.add_argument('--work-root',type=Path,default=repo/'build')
    args=parser.parse_args()
    work=Path(tempfile.mkdtemp(prefix='definition-browser-ui-',dir=args.work_root.resolve()))
    root=work/'install';write_bank(root,(1,2,3,4))
    levels=root/'data/Levels/FinalAlbion';levels.mkdir(parents=True);(levels/'unused.lev').write_bytes(b'')
    texture_bank(root/'data/graphics/pc/textures.big',[(1,'Existing',20)])
    (levels/'DefinitionLinks.lev').write_bytes(level(4))
    (levels/'DefinitionLinks.tng').write_text('Version 2;\nXXXSectionStart NULL;\nNewThing Object;\nPlayer -1;\nUID 1;\nDefinitionType "REGION_SCRIPT_OVERRIDE_TEST";\nEndThing;\nXXXSectionEnd;\n',encoding='ascii')
    front=work/'frontend';write_bank(front,(5,6,7,8))
    folder=root/'data/CompiledDefs'
    (folder/'frontend.bin').write_bytes((front/'data/CompiledDefs/game.bin').read_bytes())
    opaque=bytearray((folder/'game.bin').read_bytes())
    payload=b'\xde\xad\xbe\xef'
    chunk=zlib.compress(struct.pack('<H',2)+payload)
    opaque=opaque[:41]+struct.pack('<II',len(chunk),len(chunk))+chunk
    (folder/'script.bin').write_bytes(opaque)
    hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.rglob('*') if p.is_file()}
    print('Evidence retained at',work,flush=True)
    for size,scale in [('1440x900',1),('800x600',1.5)]:
        case=work/size;case.mkdir()
        lines=['wait_maps','wait_ready',f'set uiscale {scale}','assets_tab 0','frames 3',
            'reveal combo_assets_tab','frames 2','click combo_assets_tab','frames 2',
            'click asset_page_6','wait_definitions','frames 3','assert_state assets_tab 6',
            'assert_state definitions_count 1','assert_state definitions_filtered 1',
            'reveal combo_definition_type','frames 3','reveal definition_row_0','frames 3','click definition_row_0','frames 3','assert_state definition_selected 0',
            'assert_state definition_ready 1','assert_state definition_fields 4']
        lines += [f'assert_definition_field {field} {i+1}' for i,field in enumerate(FIELDS)]
        lines += ['definition_select_refused MISSING','definition_select_refused 0junk',
            'definition_select 0','reveal input_definition_search','frames 3','click input_definition_search','input_text missing','frames 3',
            'assert_state definitions_filtered 0','key_down Escape','key_up Escape','frames 2','definition_search -','frames 4',
            'reveal combo_definition_type','frames 2','click combo_definition_type','frames 2',
            'click definition_type_0','frames 3','assert_state definitions_filtered 1',
            'reveal combo_definition_bank','frames 2','click combo_definition_bank','frames 2',
            'click definition_bank_frontend.bin','wait_definitions','frames 3',
            'definition_select REGION_SCRIPT_OVERRIDE_TEST','frames 3']
        lines += [f'assert_definition_field {field} {i+5}' for i,field in enumerate(FIELDS)]
        lines += ['definition_bank_refused ../game.bin','definition_bank script.bin','wait_definitions',
            'frames 3','definition_select 0','frames 3','assert_state definition_ready 0',
            'assert_widget definition_decode_error','assert_widget definition_raw_payload',
            'definition_bank game.bin','wait_definitions','frames 3','definition_select 0',
            'reveal btn_definition_refresh','frames 2','click btn_definition_refresh','wait_definitions',
            'frames 3','assert_state definition_selected -','assert_state definitions_count 1','textures_tab 0','select DefinitionLinks','wait_loaded',
            'edit 1','select_thing 0','frames 3','snapshot_document','click btn_selection_properties',
            'frames 3','reveal btn_object_definition','frames 2','click btn_object_definition',
            'wait_definitions','frames 3','assert_state assets_tab 6','assert_state definition_selected 0',
            'assert_definition_field RandomVillagerMax 1','assert_document_same','assert_state doc_changes 0','quit']
        if size=='800x600':
            expanded=[]
            for line in lines:
                expanded.extend(['click btn_selection_actions','frames 2','click menu_selection_properties']
                    if line=='click btn_selection_properties' else [line])
            lines=expanded
        script=case/'browser.txt';script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
        env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(case/'profile'),LOCALAPPDATA=str(case/'local'))
        result=subprocess.run([str(args.gui.resolve()),'--install',str(root),'--size',size,'--auto',str(script)],
            cwd=args.cwd.resolve(),env=env,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120)
        (case/'output.log').write_text(result.stdout+result.stderr,encoding='utf-8')
        trace=Path(str(script)+'.log').read_text(encoding='utf-8')
        assert result.returncode==0 and 'RESULT PASS' in trace and 'FAIL ' not in trace,trace[-7000:]
        assert all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in hashes.items())
        assert not list(root.rglob('*.forge-orig'))
        print(size,'controls, typed fields, raw fallback and source preservation: PASS',flush=True)
    # Independent multi-record fixture checks identity ambiguity and anonymous records.
    multi=work/'multi';write_bank(multi,(1,2,3,4))
    mf=multi/'data/CompiledDefs';names=bytearray();refs={}
    for name in ('CRegionScriptDef','123','DUP','OPAQUE_TYPE','OPAQUE','MALFORMED'):
        refs[name]=len(names)+4;names+=struct.pack('<I',tag(name))+name.encode('ascii')+b'\0'
    (mf/'names.bin').write_bytes(struct.pack('<5I',0,0xA8E36C34,len(refs),len(names)-4,0)+names)
    good=b'\x01\0\x01'+b''.join(struct.pack('<Ii',tag(f),i+11) for i,f in enumerate(FIELDS))
    records=[('CRegionScriptDef','123',good),('CRegionScriptDef','DUP',good),
        ('CRegionScriptDef','DUP',good),('CRegionScriptDef',None,good),
        ('OPAQUE_TYPE','OPAQUE',b'\xde\xad'),('CRegionScriptDef','MALFORMED',good[:-8])]
    offsets=[];payload=bytearray()
    for _,_,data in records:offsets.append(2*len(records)+len(payload));payload+=data
    chunk=zlib.compress(struct.pack('<'+'H'*len(records),*offsets)+payload)
    data=bytes(9)+struct.pack('<I',len(records))
    data+=b''.join(struct.pack('<iii',refs[t],-1 if n is None else refs[n],i) for i,(t,n,_) in enumerate(records))
    data+=struct.pack('<I5I',2,0,0,len(records),len(chunk),len(chunk))+chunk
    (mf/'game.bin').write_bytes(data)
    ml=multi/'data/Levels/FinalAlbion';ml.mkdir(parents=True);(ml/'unused.lev').write_bytes(b'')
    texture_bank(multi/'data/graphics/pc/textures.big',[(1,'Existing',20)])
    orphan=work/'orphan';(orphan/'data/CompiledDefs').mkdir(parents=True)
    (orphan/'data/CompiledDefs/game.bin').write_bytes(data)
    before={p:hashlib.sha256(p.read_bytes()).hexdigest() for r in (multi,orphan) for p in r.rglob('*') if p.is_file()}
    case=work/'identities';case.mkdir();script=case/'browse.txt'
    lines=['wait_maps','wait_ready','assets_tab 6','wait_definitions','frames 3',
        'assert_state definitions_count 6','definition_select 123','frames 3','assert_state definition_selected 0',
        'definition_select_refused DUP','definition_select_refused 123junk','definition_select 3','frames 3',
        'assert_state definition_selected 3','assert_definition_field RandomVillagerMax 11',
        'definition_select OPAQUE','frames 3','assert_state definition_ready 0','assert_widget definition_raw_payload',
        'definition_select MALFORMED','frames 3','assert_state definition_ready 0','assert_widget definition_decode_error',
        f'set saveroot {orphan.as_posix()}','frames 3','wait_definitions','frames 3',
        'assert_state definitions_count 0','assert_state definition_failed 1','assert_widget definition_bank_error',
        'set saveroot','frames 3','wait_definitions','frames 3','assert_state definitions_count 6','quit']
    script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
    result=subprocess.run([str(args.gui.resolve()),'--install',str(multi),'--auto',str(script)],cwd=args.cwd.resolve(),
        env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(case/'profile'),LOCALAPPDATA=str(case/'local')),
        capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120)
    trace=Path(str(script)+'.log').read_text(encoding='utf-8')
    assert result.returncode==0 and 'RESULT PASS' in trace and 'FAIL ' not in trace,trace[-7000:]
    assert all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in before.items())
    print('Numeric names, duplicates, unnamed records, unsupported records and paired-names refusal: PASS',flush=True)
    (work/'result.json').write_text(json.dumps({'passed':True,'source_hashes_unchanged':True})+'\n',encoding='utf-8')

if __name__=='__main__':main()
