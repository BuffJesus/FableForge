#!/usr/bin/env python3
"""Check corrected Ground Smear, cursor, real strokes, repeat, undo and draft reopen."""
import argparse,hashlib,os,shutil,subprocess,tempfile
from pathlib import Path

def run_layout(install,repo,work):
    for size,scale in [('1440x900',1),('800x600',1.5)]:
        case=work/size;case.mkdir()
        lines=['wait_maps','wait_ready','select BanditCampResidential','wait_loaded','wait_foliage',
            f'set uiscale {scale}','edit 1','tour -1','set preview_things 0','set preview_foliage 0',
            f'set saveroot {(case/"out").as_posix()}','terrain_set_all 20','terrain_mode 6',
            'assert_state ground_smear 0','set paint_stamp 1','paint_theme 2','brush 100 20',
            'terrain_stroke 64 64 1','paint_theme 3','brush .25 20','terrain_stroke 64 64 1',
            'frames 3','snapshot_themes','snapshot_heights','snapshot_sounds','snapshot_environments','snapshot_document',
            'snapshot_passability','snapshot_shore',
            'reveal chip_ground_smear','frames 3','reveal chip_ground_smear','frames 3',
            'click chip_ground_smear','frames 3','assert_state ground_smear 1','paint_theme 0','set pen_spray 0',
            'clear_toasts','reveal chip_ground_smear','frames 3',
            f'screenshot {(case/"smear-controls.png").as_posix()}']
        for radius,count in [(.25,1),(1.5,1),(2,9)]:
            lines += [f'brush {radius} 0','camera 64.75 64.75 20 0 1.5 15',
                'clear_toasts','reveal slider_radius','frames 3','mouse_move viewport','frames 3',
                f'assert_state brush_cursor_vertices {count}',f'screenshot {(case/("radius-"+str(radius)+".png")).as_posix()}',
                'mouse_down Left','frames 5','assert_theme_blend 64 64 2 3 0 227 28 0',
                'mouse_up Left','frames 4',f'assert_theme_changes {count}','assert_heights_changed 0',
                'assert_sound_changes 0','assert_environment_changes 0','assert_document_same',
                'assert_passability_changes 0 0','assert_shore_changes 0',
                'undo','frames 3','assert_theme_changes 0']
        lines += ['brush .25 0','reveal check_pen_spray','frames 3','click check_pen_spray','frames 3',
            'mouse_move viewport','frames 3','mouse_down Left','frames 12','mouse_up Left','frames 3',
            'assert_theme_stamp 64 64 2','assert_theme_changes 1','undo','frames 3','assert_theme_changes 0',
            'reveal check_pen_spray','frames 3','click check_pen_spray','frames 3',
            'brush 2 0','terrain_stroke 64 64 0','frames 3','assert_theme_changes 9','save_draft',
            'assert_state terrain_draft_dirty 0','select LookoutPoint','wait_loaded',
            'select BanditCampResidential','wait_loaded','frames 3','assert_theme_blend 64 64 2 3 0 227 28 0',
            'assert_theme_changes 9','assert_heights_changed 0','assert_sound_changes 0','assert_environment_changes 0',
            'assert_passability_changes 0 0','assert_shore_changes 0',
            'reveal chip_ground_paint','frames 3','click chip_ground_paint','frames 3','assert_state ground_smear 0',
            'reveal check_paint_stamp','frames 3','assert_widget check_paint_stamp','quit']
        script=case/'smear.txt';script.write_text('\n'.join(lines)+'\n',encoding='utf-8')
        result=subprocess.run([str(repo/'build/FableForge.exe'),'--install',str(install),'--size',size,'--auto',str(script)],cwd=repo,
            capture_output=True,text=True,timeout=180,env=dict(os.environ,FABLEFORGE_AUTOMATION_HIDDEN='1'))
        log=Path(str(script)+'.log').read_text(encoding='utf-8');(case/'output.log').write_text(result.stdout+result.stderr,encoding='utf-8')
        assert result.returncode==0 and 'RESULT PASS' in log,log[-5000:]
        print(size,'Ground Smear control, frozen blends, repeat, undo and draft reopen: PASS',flush=True)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install',type=Path,required=True)
    args=parser.parse_args();repo=Path(__file__).resolve().parents[1]
    work=Path(tempfile.mkdtemp(prefix='ground-smear-',dir=repo/'build')).resolve()
    assert work.parent==(repo/'build').resolve()
    print('Evidence retained at',work,flush=True)
    install=work/'install'
    files=['data/Levels/'+name for name in ('FinalAlbion.wld','FinalAlbion.bwd',
        'FinalAlbion.wad','FinalAlbion_RT.stb','FinalAlbion.gtg')]
    files+=['data/CompiledDefs/game.bin','data/CompiledDefs/names.bin','data/graphics/pc/textures.big']
    for name in files:
        dest=install/name;dest.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(args.install/name,dest)
    def hashes():
        result={}
        for path in install.rglob('*'):
            if path.is_file():
                with path.open('rb') as stream:
                    result[path.relative_to(install).as_posix()]=hashlib.file_digest(stream,'sha256').hexdigest()
        return result
    for layout in ('stock','loose'):
        if layout=='loose':
            wad=install/'data/Levels/FinalAlbion.wad'
            subprocess.run([str(repo/'build/forge-tools.exe'),'wad','extract',str(wad),str(install)],
                cwd=repo,check=True,capture_output=True,timeout=180)
            renamed=wad.with_name('_FinalAlbion.wad')
            assert wad.resolve().is_relative_to(work) and renamed.resolve().is_relative_to(work)
            wad.rename(renamed)
        baseline=hashes()
        evidence=work/layout;evidence.mkdir()
        run_layout(install,repo,evidence)
        assert hashes()==baseline,'authoring saves changed the scratch game install'
        if layout=='loose':
            assert not (install/'data/Levels/FinalAlbion.wad').exists(),'loose install recreated the active WAD'
        print(layout,'game files byte-identical; draft saves stay in output root: PASS',flush=True)
if __name__=='__main__':main()
