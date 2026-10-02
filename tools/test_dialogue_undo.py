"""Exercise lip-sync undo gestures, frame/shape edits, line switching and saving."""
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

from test_dialogue_edit import read_lip_entry


def click(widget):
    # Compact panels need a second reveal after their scroll position settles.
    return [f'reveal {widget}','frames 3',f'reveal {widget}','frames 3',
            f'click {widget}','frames 3']


def shortcut(key,shift=False):
    return ['key_down Ctrl']+(['key_down Shift'] if shift else [])+[
        f'key_down {key}',f'key_up {key}']+(['key_up Shift'] if shift else [])+[
        'key_up Ctrl','frames 3']


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install',type=Path,required=True)
    args=parser.parse_args()
    repo=Path(__file__).resolve().parents[1]
    work=Path(tempfile.mkdtemp(prefix='dialogue-undo-',dir=repo/'build')).resolve()
    print('Evidence retained at',work,flush=True)
    source=args.install/'data/lang/English/dialogue.big'
    digest=hashlib.sha256(source.read_bytes()).hexdigest()
    original=read_lip_entry(source,'LIPSYNC_ENGLISH_MAIN',2)
    other=read_lip_entry(source,'LIPSYNC_ENGLISH_SCRIPT_2',3)
    count=len(original['frames'])
    for size,scale in [('1440x900',1),('800x600',1.5)]:
        dest=work/size;dest.mkdir()
        pack=dest/'LipPack';pack.mkdir()
        (pack/'forge_pack.json').write_text(json.dumps({
            'version':1,'name':'Undo test','models':[],'groundThemes':[]}),encoding='utf-8')
        commands=['wait_maps','wait_ready','assets_tab 4',f'set uiscale {scale}','frames 3']
        commands+=click('header_dialogue_lookup')+['dialogue_select 0 2']
        commands+=click('button_dialogue_load')+click('button_dialogue_edit')
        commands+=['assert_state dialogue_can_undo 0','assert_state dialogue_can_redo 0',
            'reveal slider_dialogue_key_0','frames 3','reveal slider_dialogue_key_0','frames 3',
            'mouse_move slider_dialogue_key_0','mouse_down left','frames 3',
            'mouse_delta 30 0','frames 3','mouse_delta 30 0','frames 3',
            'mouse_up left','frames 3','assert_state dialogue_staged 1']
        commands+=click('button_dialogue_undo')+[
            'assert_state dialogue_staged 0','assert_state dialogue_can_undo 0',
            'assert_state dialogue_can_redo 1']
        commands+=shortcut('Y')+['assert_state dialogue_staged 1']
        commands+=shortcut('Z')+['assert_state dialogue_staged 0']
        commands+=shortcut('Z',True)+['assert_state dialogue_staged 1',
            f'dialogue_export_path {(dest/"drag.big").as_posix()}']
        commands+=click('button_dialogue_export')+['assert_state dialogue_exported 1']
        commands+=click('button_dialogue_export')+[
            'assert_state dialogue_exported 0','assert_state dialogue_can_undo 1',
            'assert_state dialogue_staged 1']
        commands+=click('button_dialogue_insert_frame')+[
            f'assert_state dialogue_frames {count+1}','assert_state dialogue_frame 2']
        commands+=click('button_dialogue_undo')+[
            f'assert_state dialogue_frames {count}','assert_state dialogue_frame 1',
            'assert_state dialogue_staged 1']
        commands+=click('button_dialogue_redo')+[
            f'assert_state dialogue_frames {count+1}','assert_state dialogue_frame 2']
        commands+=click('combo_dialogue_add_phoneme')+click('dialogue_add_shape_AH')
        commands+=click('button_dialogue_remove_key_0')+click('button_dialogue_undo')
        commands+=['assert_widget slider_dialogue_key_0',
            f'dialogue_export_path {(dest/"shaped.big").as_posix()}']
        commands+=click('button_dialogue_export')+['assert_state dialogue_exported 1']
        commands+=click('button_dialogue_delete_frame')+[f'assert_state dialogue_frames {count}']
        commands+=click('button_dialogue_undo')+[f'assert_state dialogue_frames {count+1}']
        commands+=click('button_dialogue_reset_line')+[
            f'assert_state dialogue_frames {count}','assert_state dialogue_staged 0']
        commands+=click('button_dialogue_undo')+[
            f'assert_state dialogue_frames {count+1}','assert_state dialogue_staged 1']
        commands+=click('button_dialogue_edit')+['dialogue_select 3 3']
        commands+=click('button_dialogue_load')+click('button_dialogue_edit')
        commands+=['assert_state dialogue_can_undo 0']+click('button_dialogue_insert_frame')
        commands+=['assert_state dialogue_staged 2']+shortcut('Z')+[
            'assert_state dialogue_staged 1',f'assert_state dialogue_frames {len(other["frames"])}']
        commands+=click('button_dialogue_edit')+['dialogue_select 0 2']
        commands+=click('button_dialogue_load')+click('button_dialogue_edit')
        commands+=['assert_state dialogue_can_undo 1']+shortcut('Z')+[
            f'assert_state dialogue_frames {count+1}','assert_state dialogue_staged 1']
        commands+=shortcut('Y')+[f'pack_dest {pack.as_posix()}']
        commands+=click('button_dialogue_add_pack')+[
            'assert_state dialogue_pack_added 1','assert_state dialogue_staged 0',
            'assert_state dialogue_can_undo 0','assert_state dialogue_can_redo 0',
            f'screenshot {dest.as_posix()}/saved.png','quit','']
        script=dest/'undo.txt';script.write_text('\n'.join(commands),encoding='utf-8')
        result=subprocess.run([str(repo/'build/FableForge.exe'),'--install',str(args.install),
            '--auto',str(script),'--size',size],cwd=repo,capture_output=True,text=True,
            timeout=180,env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1'))
        (dest/'output.log').write_text(result.stdout+result.stderr,encoding='utf-8')
        log=Path(str(script)+'.log').read_text(encoding='utf-8')
        assert result.returncode==0 and 'RESULT PASS' in log,log[-4000:]
        drag=read_lip_entry(dest/'drag.big','LIPSYNC_ENGLISH_MAIN',2)
        assert drag['frames'][0][0][1]!=original['frames'][0][0][1]
        expected=copy.deepcopy(original)
        expected['frames'][0][0][1]=drag['frames'][0][0][1]
        assert drag==expected,'drag altered unrelated fields'
        shaped=read_lip_entry(dest/'shaped.big','LIPSYNC_ENGLISH_MAIN',2)
        ah=next(item['id'] for item in original['dictionary'] if item['symbol']=='AH')
        expected['frames'].insert(1,[[ah,255]])
        expected['durationBits']=shaped['durationBits']
        assert shaped==expected,'shape/frame undo changed unrelated bytes'
        recipe=json.loads((pack/'forge_pack.json').read_text(encoding='utf-8'))['lipSync']
        assert len(recipe)==1 and recipe[0]['soundId']==2
        assert {key:recipe[0][key] for key in shaped}==shaped,'saved recipe lost restored edits'
        assert hashlib.sha256(source.read_bytes()).hexdigest()==digest,'source archive changed'
        print(size,'gesture grouping, shortcuts, frame/shape/reset undo, line history and saved recipe: PASS',flush=True)


if __name__=='__main__':
    main()
