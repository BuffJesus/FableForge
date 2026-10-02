#!/usr/bin/env python3
"""Use Dialogue's picker, character, playback and edit/export flow at normal and compact sizes."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

from test_dialogue_edit import lip_frame_count


def main():
    repo=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install',type=Path,default=Path(r'C:\Programs\Steam\steamapps\common\Fable The Lost Chapters'))
    args=parser.parse_args()
    work=Path(tempfile.mkdtemp(prefix='dialogue-workspace-',dir=repo/'build')).resolve()
    print('evidence retained at',work,flush=True)
    source=args.install/'data/lang/English/dialogue.big'
    before=lip_frame_count(source,'LIPSYNC_ENGLISH_SCRIPT',5080)
    for size,scale in [('1440x900','1'),('800x600','1.5')]:
        dest=work/size;dest.mkdir()
        script=dest/'flow.txt'
        script.write_text('\n'.join([
            'wait_maps','wait_ready','assets_tab 4',f'set uiscale {scale}','frames 5',
            f'screenshot {(dest/"empty.png").as_posix()}',
            'assert_widget tree_dialogue_lines','assert_widget dialogue_group_0',
            'dialogue_search beefy','frames 2','assert_state dialogue_search_results 1',
            'reveal dialogue_group_0','click dialogue_group_0','frames 2',
            'click dialogue_group_0','frames 2','reveal dialogue_search_result_0','frames 3',f'screenshot {(dest/"choose.png").as_posix()}',
            'click dialogue_search_result_0','frames 3','assert_state dialogue_loaded 1',
            'assert_state dialogue_bank 2','assert_state dialogue_id 5080',
            'click combo_dialogue_preset','frames 2','click dialogue_character_3','frames 3',
            'assert_state dialogue_head_ready 1','click button_dialogue_play_pause','frames 10',
            'assert_state dialogue_playing 1','click button_dialogue_play_pause',
            'click button_dialogue_play_pause','frames 2',
            'mouse_move slider_dialogue_time','mouse_down left','mouse_delta 10000 0',
            'mouse_up left','frames 5','assert_state dialogue_playing 0',
            'assert_state dialogue_scrubbed 1','click button_dialogue_play_pause',
            'frames 3','assert_state dialogue_playing 1','click button_dialogue_play_pause',
            'click checkbox_dialogue_mute','click button_dialogue_play_pause','frames 3',
            'assert_state dialogue_playing 1','mouse_move slider_dialogue_time','mouse_down left','frames 3',
            'assert_state dialogue_playing 0','mouse_up left','frames 2',
            'click button_dialogue_play_pause','frames 3','assert_state dialogue_playing 1',
            'click checkbox_dialogue_timeline','frames 3','reveal timeline_dialogue_large',
            'click timeline_dialogue_large','frames 3','assert_state dialogue_playing 0',
            'reveal checkbox_dialogue_timeline','click checkbox_dialogue_timeline',
            'reveal checkbox_dialogue_mute','click checkbox_dialogue_mute','frames 3',
            f'screenshot {(dest/"loaded.png").as_posix()}',
            'click checkbox_dialogue_timeline','frames 3','assert_widget timeline_dialogue_large',
            'reveal dialogue_timeline_panel','reveal timeline_dialogue_large',
            'click timeline_dialogue_large','frames 2','assert_state dialogue_scrubbed 1',
            'reveal checkbox_dialogue_timeline','click checkbox_dialogue_timeline',
            'reveal button_dialogue_stop','click button_dialogue_stop',
            'click button_dialogue_play_pause','frames 2','assert_state dialogue_playing 1',
            'reveal button_dialogue_edit','frames 3','click button_dialogue_edit','frames 3','assert_state dialogue_editor_open 1',
            'assert_state dialogue_playing 0','assert_widget timeline_dialogue_large',
            'reveal input_dialogue_frame','frames 3','click input_dialogue_frame',
            'key_down Ctrl','key_down A','key_up A','key_up Ctrl','input_text 10',
            'key_down Enter','key_up Enter','frames 3','assert_state dialogue_frame 10',
            'reveal button_dialogue_previous_frame','frames 3','click button_dialogue_previous_frame',
            'frames 3','assert_state dialogue_frame 9','click button_dialogue_next_frame',
            'frames 3','assert_state dialogue_frame 10','assert_state dialogue_staged 0',
            'reveal input_dialogue_frame','frames 3','click input_dialogue_frame',
            'key_down Ctrl','key_down A','key_up A','key_up Ctrl','input_text 0',
            'key_down Enter','key_up Enter','frames 3','assert_state dialogue_frame 1',
            f'screenshot {(dest/"edit.png").as_posix()}',
            'reveal button_dialogue_insert_frame','click button_dialogue_insert_frame','frames 2',
            'assert_state dialogue_staged 1',f'assert_state dialogue_frames {before+1}',
            f'dialogue_export_path {dest/"dialogue.big"}',
            'reveal input_dialogue_scratch_path','frames 3','reveal button_dialogue_export',
            'click button_dialogue_export','frames 2','assert_state dialogue_exported 1','quit','']),encoding='utf-8')
        result=subprocess.run([str(repo/'build/FableForge.exe'),'--install',str(args.install),
            '--auto',str(script),'--size',size],cwd=repo,capture_output=True,text=True,timeout=180,
            env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1'))
        (dest/'output.log').write_text(result.stdout+result.stderr,encoding='utf-8')
        log=Path(str(script)+'.log').read_text(encoding='utf-8')
        assert result.returncode==0 and 'RESULT PASS' in log,log[-2500:]
        assert lip_frame_count(dest/'dialogue.big','LIPSYNC_ENGLISH_SCRIPT',5080)==before+1
        assert lip_frame_count(source,'LIPSYNC_ENGLISH_SCRIPT',5080)==before
        print(size,'browse, all-bank search, character, playback, scrub, edit and exported frames: PASS',flush=True)


if __name__=='__main__':
    main()
