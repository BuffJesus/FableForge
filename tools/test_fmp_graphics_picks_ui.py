#!/usr/bin/env python3
"""Exercise a named mesh conflict and saved winner in the Mods panel."""
import argparse
import os
import shutil
import subprocess
import tempfile
import zipfile
from pathlib import Path


PACKAGES = ('Half Mask (Variant 1).fmp', 'Lava JOB Mask.fmp')
ENTRY = 'MESH_HERO_JOB_MASK_01'


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--gui', type=Path, default=repo/'build/FableForge.exe')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='fmp-graphics-ui-', dir=repo/'build')).resolve()
    assert work.parent == (repo/'build').resolve()
    print('Evidence retained at', work, flush=True)
    scratch = work/'install'
    (scratch/'data/CompiledDefs').mkdir(parents=True)
    (scratch/'data/graphics/pc').mkdir(parents=True)
    for name in ('game.bin', 'names.bin', 'script.bin', 'frontend.bin'):
        shutil.copyfile(args.install/'data/CompiledDefs'/name,
                        scratch/'data/CompiledDefs'/name)
    shutil.copyfile(args.install/'data/graphics/pc/textures.big',
                    scratch/'data/graphics/pc/textures.big')
    shutil.copyfile(args.install/'data/graphics/graphics.big',
                    scratch/'data/graphics/graphics.big')
    with zipfile.ZipFile(args.archive) as archive:
        for name, short in zip(PACKAGES, ('a.fmp', 'b.fmp')):
            (work/short).write_bytes(archive.read('mods/'+name))
    script = work/'mesh_picks.txt'
    lines = [
        'wait_maps', 'wait_ready', f'set saveroot {scratch.as_posix()}',
        'mods_tab 1', 'frames 2',
        f'mod_add {(work/"a.fmp").as_posix()} {Path(PACKAGES[0]).stem}',
        f'mod_add {(work/"b.fmp").as_posix()} {Path(PACKAGES[1]).stem}',
        'frames 2', 'assert_state mods_count 2',
        'mods_conflicts', 'wait_mods', 'frames 2',
        'assert_state mods_conflicts 1', 'assert_widget mod_winner_0',
        f'mod_pick mesh:{ENTRY} {Path(PACKAGES[0]).stem}',
        'frames 2',
        f'assert_file_contains {(scratch/"forge_mods_picks.txt").as_posix()} mesh:{ENTRY}',
        'mods_conflicts', 'wait_mods', 'frames 2',
        'assert_state mods_conflicts 1',
        f'screenshot {(work/"mesh_conflicts.png").as_posix()}',
        'quit',
    ]
    script.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    result = subprocess.run([str(args.gui), '--auto', str(script), '--install', str(args.install)],
                            cwd=repo, capture_output=True, text=True, timeout=300,
                            env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1'))
    (work/'gui_output.log').write_text(result.stdout + result.stderr, encoding='utf-8')
    log = Path(str(script)+'.log')
    tail = log.read_text(encoding='utf-8', errors='replace').splitlines() if log.exists() else []
    assert result.returncode == 0 and tail and 'RESULT PASS' in tail[-1], tail[-12:]
    picks = (scratch/'forge_mods_picks.txt').read_text(encoding='utf-8')
    assert f'mesh:{ENTRY}\t{Path(PACKAGES[0]).stem}' in picks
    assert (work/'mesh_conflicts.png').is_file()
    target = scratch.resolve()
    assert target != work and target.is_relative_to(work)
    shutil.rmtree(target)
    print('Mesh conflict row and persisted winner pass in GUI:', work/'mesh_conflicts.png')


if __name__ == '__main__':
    main()
