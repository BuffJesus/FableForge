#!/usr/bin/env python3
"""Browse tiny localization banks through actual controls without changing files."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
from test_mod_field_details import write_bank
from test_mod_asset_choice_contributors import bank
from test_texture_lookup import texture_bank
from test_setup_restore_ui import level


def string(value,identifier):
    payload=value.encode('utf-16-le')+b'\0\0'
    for field in ('ScriptDialogue.lug','Speaker',identifier):
        encoded=field.encode('utf-8');payload+=struct.pack('<I',len(encoded))+encoded
    return payload+struct.pack('<Ii',1,1)+b'emphasis\0'


def main():
    repo=Path(__file__).resolve().parents[1];parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gui',type=Path,default=repo/'build/FableForge.exe')
    parser.add_argument('--cwd',type=Path,default=repo)
    parser.add_argument('--work-root',type=Path,default=repo/'build')
    args=parser.parse_args();gui=args.gui.resolve()
    work=Path(tempfile.mkdtemp(prefix='text-browser-ui-',dir=args.work_root.resolve()));root=work/'install'
    print('Evidence retained at',work,flush=True)
    write_bank(root,(1,2,3,4))
    levels=root/'data/Levels/FinalAlbion';levels.mkdir(parents=True);(levels/'unused.lev').write_bytes(b'')
    (levels/'TextLinks.lev').write_bytes(level(4))
    (levels/'TextLinks.tng').write_text('Version 2;\nXXXSectionStart NULL;\nNewThing Object;\n'
        'Player -1;\nUID 1;\nDefinitionType "OBJECT_TEXT_TEST";\n'
        'StartCTCActionUseReadable;\nGameTextDefName "123";\nEndCTCActionUseReadable;\n'
        'StartCTCInfoDisplay;\nTextTag "TEXT_HELLO";\nTextTagBack "MISSING";\nEndCTCInfoDisplay;\n'
        'EndThing;\nXXXSectionEnd;\n',encoding='ascii')
    texture_bank(root/'data/graphics/pc/textures.big',[(1,'Existing',20)])
    records={}
    for name,identity,content in [('TEXT_HELLO',10,'Hello Ω 漢字'),('TEXT_OTHER',20,'Another "quoted"\tline\nSecond line Ω'),
                                ('123',70,'Numeric symbol'),('TEXT_ID123',123,'Numeric ID'),
                                ('TEXT_DUP_A',80,'First duplicate'),('TEXT_DUP_B',80,'Second duplicate')]:
        records[name]={'id':identity,'type':0,'header':b'','payload':string(content,name)}
    records.update({'TEXT_GROUP':{'id':30,'type':1,'header':b'','payload':struct.pack('<6I',5,10,20,999,40,80)},
                    'TEXT_NESTED':{'id':40,'type':1,'header':b'','payload':struct.pack('<2I',1,30)},
                    'TEXT_BAD':{'id':50,'type':0,'header':b'','payload':b'\x01'},
                    'TEXT_NARRATORS':{'id':60,'type':2,'header':struct.pack('<I',2)+b'HERO\0GUILDMASTER\0','payload':b''},
                    'TEXT_NARRATORS_BAD':{'id':61,'type':2,'header':struct.pack('<I',1)+b'UNTERMINATED','payload':b''}})
    bank(root/'data/lang/English/text.big','TEXT_ENGLISH_MAIN',records)
    def language(folder,language,content):
        bank(folder/'data/lang'/language/'text.big','TEXT_'+language.upper()+'_MAIN',
             {'TEXT_HELLO':{'id':10,'type':0,'header':b'','payload':string(content,'TEXT_HELLO')}})
    language(root,'French','Bonjour Ω');staged=work/'staged';language(staged,'English','Staged Ω')
    original={p:hashlib.sha256(p.read_bytes()).hexdigest() for folder in (root,staged) for p in folder.rglob('*') if p.is_file()}
    for size,scale in [('1440x900',1),('800x600',1.5)]:
        case=work/size;case.mkdir()
        exported=case/'all text Ω.tsv';existing=case/'existing.tsv';existing.write_bytes(b'original output')
        alias=case/'native-alias.tsv';os.link(root/'data/lang/English/text.big',alias)
        lines=['wait_maps','wait_ready',f'set uiscale {scale}',
               'select TextLinks','wait_loaded','edit 1','select_thing 0','frames 3','snapshot_document',
               'click btn_selection_properties','frames 3','reveal property_group_CTCActionUseReadable',
               'click property_group_CTCActionUseReadable','frames 3',
               'reveal property_text_CTCActionUseReadable_GameTextDefName',
               'click property_text_CTCActionUseReadable_GameTextDefName','wait_texts','frames 3',
               'assert_state assets_tab 5','assert_state text_selected 123','assert_text_content Numeric symbol',
               'assert_document_same','textures_tab 0','frames 3','click btn_selection_properties',
               'frames 3','reveal property_group_CTCInfoDisplay',
               'click property_group_CTCInfoDisplay','frames 3','reveal property_text_CTCInfoDisplay_TextTag',
               'click property_text_CTCInfoDisplay_TextTag','frames 3',
               'assert_state text_selected TEXT_HELLO','assert_text_content Hello Ω 漢字','assert_document_same',
               'textures_tab 0','frames 3','click btn_selection_properties','frames 3',
               'reveal property_text_CTCInfoDisplay_TextTagBack',
               'click property_text_CTCInfoDisplay_TextTagBack','frames 3','assert_state text_failed 1',
               'assert_state text_selected TEXT_HELLO','assert_document_same','assert_state doc_changes 0',
               'text_select TEXT_HELLO','assets_tab 0','frames 3',
               'reveal combo_assets_tab','frames 2','click combo_assets_tab','frames 2',
               'assert_widget asset_page_5','click asset_page_5','wait_texts','frames 3',
               'assert_state assets_tab 5','assert_state texts_count 11','assert_state texts_filtered 11',
               'assert_state text_failed 0','click input_textsearch','input_text hello','frames 3',
               'assert_state texts_filtered 1','click text_row_5','frames 3',
               'assert_state text_selected TEXT_HELLO','assert_state text_ready 1','assert_state text_tags 1',
               'assert_text_content Hello Ω 漢字','assert_widget text_content','click text_content',
               'key_down Ctrl','key_down A','key_up A','key_up Ctrl','input_text CHANGED','frames 3',
               'assert_text_content Hello Ω 漢字','text_search 漢字','frames 3','assert_state texts_filtered 1',
               f'text_export_path {exported.as_posix()}','reveal btn_text_export','frames 3',
               'click btn_text_export','wait_text_export','frames 3','assert_state text_export_succeeded 1',
               f'text_export_refused {existing.as_posix()}',f'text_export_refused {alias.as_posix()}',
               f'text_export_refused {(root/"data/lang/English/text.big").as_posix()}',
               f'text_export_refused {(case/"missing"/"out.tsv").as_posix()}',
               'text_search -','frames 3','text_select TEXT_GROUP','frames 3','assert_state text_members 5',
               'assert_widget text_member_2','assert_widget text_member_4','click text_member_0','frames 3',
               'assert_state text_selected TEXT_HELLO','text_select TEXT_NESTED','frames 3',
               'click text_member_0','frames 3','assert_state text_selected TEXT_GROUP',
               'text_select 123','frames 3','assert_state text_selected 123','assert_text_content Numeric symbol',
               'text_select_refused 80','frames 3','assert_state text_selected 123','assert_state text_failed 1',
               'text_select_refused MISSING','text_select TEXT_BAD','frames 3','assert_state text_ready 0',
               'assert_state text_failed 1','assert_widget text_entry_error','text_select TEXT_NARRATORS','frames 3',
               'assert_state text_type 2','assert_state text_ready 1','assert_state text_narrators 2',
               'assert_widget text_narrators','assert_text_narrator 0 HERO','assert_text_narrator 1 GUILDMASTER',
               'text_select TEXT_NARRATORS_BAD','frames 3',
               'assert_state text_ready 0','assert_state text_failed 1','assert_widget text_entry_error',
               'reveal combo_text_language','frames 2','click combo_text_language','frames 2','click text_language_French','wait_texts','frames 3',
               'assert_state texts_count 1','text_select TEXT_HELLO','frames 3','assert_text_content Bonjour Ω',
               'text_language_refused Missing','text_language English','wait_texts','frames 3',
               'text_select TEXT_HELLO','reveal btn_text_refresh','frames 2','click btn_text_refresh','wait_texts',
               'frames 3','assert_state text_selected -','assert_state texts_count 11',
               f'set saveroot {staged.as_posix()}','frames 3','wait_texts','frames 3','assert_state texts_count 1',
               'text_select TEXT_HELLO','frames 3','assert_text_content Staged Ω',
               'text_language French','wait_texts','frames 3','text_select TEXT_HELLO','assert_text_content Bonjour Ω',
               'text_language English','wait_texts','set saveroot','frames 3','wait_texts','frames 3',
               'assert_state texts_count 11','text_select TEXT_HELLO','assert_text_content Hello Ω 漢字',
               f'set saveroot {(work/"empty-output").as_posix()}','frames 3','wait_texts','frames 3',
               'text_select TEXT_HELLO','assert_text_content Hello Ω 漢字','quit']
        if size=='800x600':
            expanded=[]
            for line in lines:
                expanded.extend(['click btn_selection_actions','frames 2','click menu_selection_properties']
                                if line=='click btn_selection_properties' else [line])
            lines=expanded
        script=case/'browser.txt';script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
        env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(case/'profile'),LOCALAPPDATA=str(case/'local'))
        result=subprocess.run([str(gui),'--install',str(root),'--size',size,'--auto',str(script)],cwd=args.cwd.resolve(),
                              env=env,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120)
        (case/'output.log').write_text(result.stdout+result.stderr,encoding='utf-8')
        trace=Path(str(script)+'.log').read_text(encoding='utf-8')
        assert result.returncode==0 and 'RESULT PASS' in trace and 'FAIL ' not in trace,trace[-6000:]
        assert all(hashlib.sha256(path.read_bytes()).hexdigest()==digest for path,digest in original.items())
        with exported.open(encoding='utf-8-sig',newline='') as stream:
            exported_rows=list(csv.DictReader(stream,delimiter='\t'))
        assert len(exported_rows)==11
        by_name={row['name']:row for row in exported_rows}
        assert by_name['TEXT_HELLO']['content']=='Hello Ω 漢字'
        assert by_name['TEXT_OTHER']['content']=='Another "quoted"\tline\nSecond line Ω'
        assert json.loads(by_name['TEXT_HELLO']['tags'])==[{'position':1,'name':'emphasis'}]
        assert json.loads(by_name['TEXT_GROUP']['members'])==[10,20,999,40,80]
        assert json.loads(by_name['TEXT_NARRATORS']['narrators'])==['HERO','GUILDMASTER']
        assert by_name['TEXT_BAD']['error'] and by_name['TEXT_NARRATORS_BAD']['error']
        assert existing.read_bytes()==b'original output'
        assert not (case/'missing').exists() and not list(case.glob('forge-text-export-*'))
        print(size,'language/search/read-only/group/refusal/output-fallback checks: PASS',flush=True)
    (work/'result.json').write_text(json.dumps({'passed':True,'sizes':['1440x900','800x600'],'source_hashes_unchanged':True},indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
