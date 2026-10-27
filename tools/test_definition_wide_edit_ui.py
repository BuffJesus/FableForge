#!/usr/bin/env python3
"""Edit confirmed printable wide strings without discarding legacy or trailing bytes."""
import argparse,json,os,struct,subprocess,tempfile
from pathlib import Path
from test_mod_field_details import write_bank,tag
from test_texture_lookup import texture_bank
from test_fmp_links import read_bank
from test_definition_edit_ui import click

MODES=('save-long','save-short','save-empty','max127','noop','unicode','accented','mixed',
       'supplementary','unicode-max127','unicode-old-change','invalid','nonascii-old',
       'embedded-old','trailer','truncated','long-old','high-surrogate','low-surrogate',
       'utf8-boundary-old','utf8-fit-old')

def main():
 repo=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--gui',type=Path,default=repo/'build/FableForge.exe');p.add_argument('--cwd',type=Path,default=repo);p.add_argument('--work-root',type=Path,default=repo/'build');p.add_argument('--only',nargs='+',choices=MODES);a=p.parse_args()
 work=Path(tempfile.mkdtemp(prefix='definition-wide-ui-',dir=a.work_root.resolve()));print('Evidence retained at',work,flush=True);cases=0
 for size,scale in [('1440x900',1),('800x600',1.5)]:
  for mode in a.only or MODES:
   cases+=1;case=work/f'{size}-{mode}';root=case/'install';wide=lambda x:x.encode('utf-16-le')+b'\0\0';old=wide('OLD')
   if mode=='nonascii-old':old=wide('caf\u00e9')
   if mode=='unicode-old-change':old=wide('\u6f22\U0001f642')
   if mode=='high-surrogate':old=b'\0\xd8\0\0'
   if mode=='low-surrogate':old=b'\0\xdc\0\0'
   if mode=='embedded-old':old=wide('A')+wide('B')
   if mode=='trailer':old+=b'OPAQUE'
   if mode=='truncated':old=old[:-2]
   if mode=='long-old':old=wide('A'*128)
   if mode=='utf8-boundary-old':old=wide('A'*2046+'\u00e9'+'TAIL')
   if mode=='utf8-fit-old':old=wide('A'*2045+'\u00e9'+'TAIL')
   fields=[f['name'] for f in json.loads((repo/'docs/re_reference/def_schema.json').read_text())['UI']['fields']];values=[bytes(4)]*len(fields);values[3]=old
   write_bank(root,values,definition='UI',record='STRINGS',fields=fields)
   levels=root/'data/Levels/FinalAlbion';levels.mkdir(parents=True);(levels/'unused.lev').write_bytes(b'');texture_bank(root/'data/graphics/pc/textures.big',[(1,'Existing',20)])
   bank=root/'data/CompiledDefs/game.bin';names=root/'data/CompiledDefs/names.bin';before=bank.read_bytes();names_before=names.read_bytes();record=read_bank(root)[0]
   lines=['wait_maps','wait_ready',f'set uiscale {scale}','assets_tab 6','wait_definitions','frames 3','definition_select STRINGS','frames 3'];expected=None
   if mode in ('embedded-old','trailer','truncated','long-old','high-surrogate','low-surrogate','utf8-boundary-old','utf8-fit-old'):lines+=['definition_edit_refused 3']
   else:
    lines+=click('btn_definition_edit_3')+['assert_state definition_editing 1']
    if mode in ('save-long','save-short','save-empty','max127','unicode','accented','mixed','supplementary','unicode-max127','unicode-old-change'):
     lines+=click('input_definition_edit_value')+['key_down Ctrl','key_down A','key_up A','key_up Ctrl']
     text={'save-long':'A longer creature name','save-short':'A','save-empty':'','max127':'X'*127,'unicode':'\u6f22',
           'accented':'caf\u00e9','mixed':'A\u03a9\u6f22\U0001f642','supplementary':'\U0001f680',
           'unicode-max127':'X'*120+'\u6f22\U0001f642','unicode-old-change':'\u00e9\u03a9'}[mode]
     if text:lines+=['input_text '+text,'frames 3']
     else:lines+=['key_down Delete','key_up Delete','frames 3']
     expected=wide(text)
     lines+=click('btn_definition_save')+click('btn_definition_save_confirm')+['wait_ready','wait_file_job','wait_definitions','frames 3','assert_state definition_editing 0']
    elif mode=='invalid':
     lines+=['definition_value_refused '+('X'*128),'definition_value_refused '+('X'*121+'\u6f22\U0001f642'),'definition_value BAD\x7f','definition_save_refused','definition_value BAD\u0080','definition_save_refused','assert_state definition_editing 1','definition_cancel']
    else:lines+=['definition_save','wait_ready','wait_file_job','wait_definitions','frames 3','assert_state definition_editing 0']
   if mode=='nonascii-old':lines+=['reveal definition_field_3','frames 3',f'screenshot {(case/"wide-display.png").as_posix()}']
   lines+=['quit'];script=case/'strings.txt';script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
   r=subprocess.run([str(a.gui.resolve()),'--install',str(root),'--size',size,'--auto',str(script)],cwd=a.cwd.resolve(),env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(case/'profile'),LOCALAPPDATA=str(case/'local')),capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=90)
   (case/'output.log').write_text(r.stdout+r.stderr,encoding='utf-8');trace=Path(str(script)+'.log').read_text(encoding='utf-8');assert r.returncode==0 and 'RESULT PASS' in trace and 'FAIL ' not in trace,trace[-5000:]
   backup=Path(str(bank)+'.forge-orig')
   if expected is not None:
    start=record['data'].index(struct.pack('<I',tag('TextValue')))+4
    assert read_bank(root)[0]==dict(record,data=record['data'][:start]+expected+record['data'][start+len(old):]) and backup.read_bytes()==before
    assert 'click btn_definition_save_confirm' in trace
   else:assert bank.read_bytes()==before and not backup.exists()
   assert names.read_bytes()==names_before
   print(size,mode,'wide Unicode controls, guards and exact bytes: PASS',flush=True)
 (work/'result.json').write_text(json.dumps({'passed':True,'cases':cases})+'\n')
if __name__=='__main__':main()
