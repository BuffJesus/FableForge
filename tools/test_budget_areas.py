#!/usr/bin/env python3
"""Check native survey boundaries, captured IDs and real area selection input."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import re
from PIL import Image, ImageChops


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install',type=Path,required=True)
    args=parser.parse_args()
    repo=Path(__file__).resolve().parents[1]
    work=Path(tempfile.mkdtemp(prefix='budget-areas-',dir=repo/'build'))
    print('Evidence retained at',work,flush=True)
    for size,scale in [('1440x900',1),('800x600',1.5)]:
        dest=work/size;dest.mkdir()
        lines=['wait_maps','wait_ready','select TeleporterGreatwood','wait_loaded','wait_foliage',
            f'set uiscale {scale}','edit 1','tour -1','terrain_set_all 20','wait_foliage',
            'set place_snap_grid 0','camera 10 10 20 0 1.5 20',
            'place OBJECT_BARREL_BREAKABLE BudgetMin','camera 11 10 20 0 1.5 20',
            'place OBJECT_BARREL_BREAKABLE BudgetMid','camera 12 10 20 0 1.5 20',
            'place OBJECT_BARREL_BREAKABLE BudgetMax','wait_foliage','edit_tab 3',
            'set budget_include 4','budget_open 1','set budget_scope 3','budget_clear_areas',
            'budget_area 10 9 12 11','budget_run 1',
            'assert_budget_thing BudgetMin 1','assert_budget_thing BudgetMid 1',
            'assert_budget_thing BudgetMax 0','select_script BudgetMin','move_thing 10 0 0',
            'select_script BudgetMax','move_thing -1.5 0 0','budget_run 1',
            'assert_budget_thing BudgetMin 1','assert_budget_thing BudgetMax 0',
            'budget_area 12 11 10 9','assert_budget_thing BudgetMax 1',
            'budget_clear_areas','budget_area 12 11 10 9',
            'assert_budget_thing BudgetMin 0','assert_budget_thing BudgetMid 1',
            'assert_budget_thing BudgetMax 1','budget_area 20 10 20 10',
            'assert_budget_thing BudgetMin 0','budget_clear_areas',
            'select_script BudgetMax','move_thing 10 0 0','wait_foliage',
            'snapshot_document','snapshot_heights','camera 11 11 20 0 1.5 30',
            'snapshot_camera','clear_toasts','frames 3',
            'reveal btn_budget_pick','frames 3','click btn_budget_pick','frames 3',
            'assert_state budget_picking 1','dump_state',
            f'screenshot {dest.as_posix()}/area-clear.png','mouse_move ground 8 8','frames 3',
            'mouse_down Left','frames 3','assert_state budget_area_drag 1',
            'mouse_move ground 14 14','frames 3','mouse_up Left','frames 3',
            'assert_state budget_areas 1','assert_budget_thing BudgetMid 1',
            'assert_budget_thing BudgetMin 0','assert_budget_thing BudgetMax 0',
            'assert_document_same','assert_heights_changed 0','assert_camera_back',
            f'screenshot {dest.as_posix()}/area-overlay.png',
            'gizmo 4','frames 3','edit_tab 3','budget_open 1','frames 3',
            'reveal btn_budget_pick','frames 3','click btn_budget_pick','frames 3',
            'assert_state budget_picking 1','camera 20 10 20 0 1.5 30',
            'snapshot_camera','mouse_move ground 18 8',
            'key_down Shift','mouse_down Left','frames 3','mouse_move ground 22 12',
            'frames 3','mouse_up Left','key_up Shift','frames 3',
            'assert_state budget_areas 2','assert_budget_thing BudgetMin 1',
            'assert_budget_thing BudgetMid 1','assert_document_same','assert_heights_changed 0',
            'assert_camera_back','click btn_budget_pick_clear','frames 3',
            'assert_state budget_areas 0','assert_state budget_picking 1',
            'select_script BudgetMid','camera 11 10 20 0 1.5 20','frames 3',
            'mouse_move selected_pivot','mouse_down Left','frames 3','mouse_up Left','frames 3',
            'assert_state budget_areas 1','assert_budget_thing BudgetMid 1',
            'assert_document_same','assert_heights_changed 0',
            'click btn_budget_pick_done','frames 3','assert_state budget_picking 0',
            'assert_widget seg_budget_scope','budget_run 1',
            f'screenshot {dest.as_posix()}/area-report.png',
            'click btn_budget_clear_areas','frames 3','assert_state budget_areas 0',
            'assert_budget_thing BudgetMid 0','budget_run 0','assert_state budget_things 0',
            'budget_area 10 9 12 11','reveal btn_budget_pick','frames 3','click btn_budget_pick',
            'frames 3','assert_state budget_picking 1','mouse_move ground 11 10',
            'key_down Escape','frames 3','key_up Escape','frames 3',
            'assert_state budget_areas 0','assert_state budget_picking 0',
            'assert_budget_thing BudgetMid 0','assert_document_same','assert_heights_changed 0',
            'budget_open 1','budget_area 10 9 12 11','budget_open 0','select Greatwood_1','wait_loaded',
            'assert_state budget_areas 0','assert_state budget_picking 0','quit']
        script=dest/'areas.txt';script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
        result=subprocess.run([str(repo/'build/FableForge.exe'),'--install',str(args.install),
            '--size',size,'--auto',str(script)],cwd=repo,capture_output=True,text=True,timeout=180,
            env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1'))
        (dest/'output.log').write_text(result.stdout+result.stderr,encoding='utf-8')
        log=Path(str(script)+'.log').read_text(encoding='utf-8')
        assert result.returncode==0 and 'RESULT PASS' in log,log[-6000:]+result.stderr
        values=dict(re.findall(r'\b(viewport_\w+)=([\d.]+)',log))
        x,y,w,h=(float(values['viewport_'+key]) for key in ['x','y','width','height'])
        crop=tuple(round(v) for v in (x+w*.2,y+h*.3,x+w*.8,y+h*.8))
        before=Image.open(dest/'area-clear.png').convert('RGB').crop(crop)
        after=Image.open(dest/'area-overlay.png').convert('RGB').crop(crop)
        assert ImageChops.difference(before,after).getbbox(), 'survey overlay has no visible pixels'
        print(size,'half-open bounds, captured IDs, real drag/click/Shift, untouched edits and clear/map reset: PASS',flush=True)


if __name__=='__main__':
    main()
