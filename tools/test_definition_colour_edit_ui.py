#!/usr/bin/env python3
"""Exercise native BGRA colour drafts, real channel controls and exact no-ops."""
import argparse,json,os,struct,subprocess,tempfile
from pathlib import Path
from test_mod_field_details import write_bank
from test_texture_lookup import texture_bank
from test_fmp_links import read_bank
from test_definition_edit_ui import click

def main():
 repo=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--gui',type=Path,default=repo/'build/FableForge.exe');p.add_argument('--cwd',type=Path,default=repo);p.add_argument('--work-root',type=Path,default=repo/'build');a=p.parse_args()
 work=Path(tempfile.mkdtemp(prefix='definition-colour-ui-',dir=a.work_root.resolve()));print('Evidence retained at',work,flush=True);cases=0
 for size,scale in [('1440x900',1),('800x600',1.5)]:
  for mode in ('red','blue','alpha','picker','picker-save','noop','extreme-noop','invalid','trailer','truncated'):
   cases+=1;case=work/f'{size}-{mode}';root=case/'install';old=bytes([3,2,1,4])
   if mode=='extreme-noop':old=bytes([0,255,0,255])
   if mode=='trailer':old+=b'OPAQUE'
   if mode=='truncated':old=old[:-1]
   write_bank(root,[struct.pack('<i',42),old],definition='CPlayerDef',record='PLAYER',fields=['CharacterDef','Colour'])
   levels=root/'data/Levels/FinalAlbion';levels.mkdir(parents=True);(levels/'unused.lev').write_bytes(b'');texture_bank(root/'data/graphics/pc/textures.big',[(1,'Existing',20)])
   bank=root/'data/CompiledDefs/game.bin';names=root/'data/CompiledDefs/names.bin';before=bank.read_bytes();names_before=names.read_bytes();record=read_bank(root)[0]
   lines=['wait_maps','wait_ready',f'set uiscale {scale}','assets_tab 6','wait_definitions','frames 3','definition_select PLAYER','frames 3']
   if mode in ('trailer','truncated'):lines+=['definition_edit_refused 1']
   else:
    lines+=click('btn_definition_edit_1')+['assert_state definition_editing 1']
    if mode in ('red','blue','alpha'):
     channel={'red':0,'blue':2,'alpha':3}[mode];value=128 if mode=='alpha' else 255
     lines+=click(f'input_definition_colour_{channel}')+['key_down Ctrl','key_down A','key_up A','key_up Ctrl',f'input_text {value}','frames 3']
     if size=='800x600' and mode=='red':lines+=[f'screenshot {(case/"draft.png").as_posix()}']
     lines+=click('btn_definition_save')+click('btn_definition_save_confirm')+['wait_ready','wait_file_job','wait_definitions','frames 3','assert_state definition_editing 0']
    elif mode=='picker-save':
     x,y=(1170,480) if size=='1440x900' else (400,200)
     lines+=click('input_definition_colour_picker')+['assert_widget definition_colour_popup',f'mouse_move {x} {y}','frames 3','mouse_down left','frames 3','mouse_up left','frames 3','key_down Escape','key_up Escape','frames 3']
     lines+=click('btn_definition_save')+click('btn_definition_save_confirm')+['wait_ready','wait_file_job','wait_definitions','frames 3','assert_state definition_editing 0']
    elif mode=='picker':
     lines+=click('input_definition_colour_picker')+['assert_widget definition_colour_popup',f'screenshot {(case/"picker.png").as_posix()}','key_down Escape','key_up Escape','frames 3','assert_state definition_editing 1','assert_state close_prompt 0','definition_cancel']
    elif mode=='invalid':
     for value in ('-1,2,3,4','256,2,3,4','1,2','1junk,2,3,4','1,2,3,256'):
      lines+=['definition_value '+value,'definition_save_refused','assert_state definition_editing 1']
     lines+=['definition_cancel']
    else:lines+=['definition_save','wait_ready','wait_file_job','wait_definitions','frames 3','assert_state definition_editing 0']
   lines+=['quit'];script=case/'colours.txt';script.write_text('\n'.join(lines)+'\n')
   r=subprocess.run([str(a.gui.resolve()),'--install',str(root),'--size',size,'--auto',str(script)],cwd=a.cwd.resolve(),env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1',APPDATA=str(case/'profile'),LOCALAPPDATA=str(case/'local')),capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=90)
   (case/'output.log').write_text(r.stdout+r.stderr,encoding='utf-8');trace=Path(str(script)+'.log').read_text();assert r.returncode==0 and 'RESULT PASS' in trace and 'FAIL ' not in trace,trace[-5000:]
   backup=Path(str(bank)+'.forge-orig')
   if mode in ('red','blue','alpha'):
    expected=bytearray(record['data']);expected[-4+{'red':2,'blue':0,'alpha':3}[mode]]=128 if mode=='alpha' else 255
    assert read_bank(root)[0]==dict(record,data=bytes(expected)) and backup.read_bytes()==before
    assert 'click btn_definition_save_confirm' in trace
   elif mode=='picker-save':
    actual=read_bank(root)[0];assert actual['data'][:-4]==record['data'][:-4] and actual['data'][-4:]!=old
    b,g,r,alpha=actual['data'][-4:];assert b>g>r and alpha==4 and backup.read_bytes()==before
    assert 'mouse_down left' in trace and 'click btn_definition_save_confirm' in trace
   else:assert bank.read_bytes()==before and not backup.exists()
   assert names.read_bytes()==names_before
   if mode=='picker':assert 'ok   assert_widget definition_colour_popup' in trace and (case/'picker.png').exists()
   print(size,mode,'colour controls, BGRA and exact bytes: PASS',flush=True)
 (work/'result.json').write_text(json.dumps({'passed':True,'cases':cases})+'\n')
if __name__=='__main__':main()
