#!/usr/bin/env python3
"""Capture the real Mods UI on the retained test_aeon_controller scratch root."""
import argparse
import os
import shutil
from pathlib import Path
import subprocess


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('evidence', type=Path)
    args = parser.parse_args()
    work = args.evidence.resolve()
    assert work.parent == (repo / 'build').resolve() and work.name.startswith('aeon_controller_')
    root = work / 'retail'
    assert (root / 'forge_mods.json').is_file()
    tool = repo / 'build/forge-tools.exe'
    # Start each capture from the original scratch baseline.
    restored = subprocess.run([str(tool), 'mods', 'undeploy', str(root)], cwd=repo,
                             capture_output=True, text=True, timeout=600)
    (work / 'guide-restore.log').write_text(restored.stdout + restored.stderr, encoding='utf-8')
    assert restored.returncode == 0, restored.stdout + restored.stderr
    retail = Path(r'C:\Programs\Steam\steamapps\common\Fable The Lost Chapters')
    for relative in ('data/graphics/graphics.big', 'data/graphics/pc/textures.big'):
        dest = root / relative
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(retail / relative, dest)
    shots = repo / 'docs/walkthrough/aeon-controller'
    shots.mkdir(parents=True, exist_ok=True)
    script = work / 'guide.txt'
    script.write_text('\n'.join([
        'wait_maps', 'wait_ready', 'setup 0', 'set uiscale 1', 'mods_tab 1',
        'frames 5', 'assert_state mods_count 3', 'clear_toasts',
        f'screenshot {(shots / "01-order.png").as_posix()}',
        'reveal input_mod_path', 'frames 3',
        f'screenshot {(shots / "02-add.png").as_posix()}',
        'reveal btn_mods_conflicts', 'click btn_mods_conflicts', 'wait_mods',
        'assert_state mods_conflicts 0', 'assert_state mods_missing_models 0',
        'assert_log mods conflicts: 0 conflict(s) across 3 mod(s)', 'clear_toasts',
        'reveal btn_mods_deploy', 'frames 3',
        f'screenshot {(shots / "03-check.png").as_posix()}',
        'click btn_mods_deploy', 'wait_mods', 'assert_log mods deploy: done',
        'clear_toasts', 'reveal btn_mods_deploy', 'frames 3',
        f'screenshot {(shots / "04-deploy.png").as_posix()}',
        'quit', '']), encoding='utf-8')
    result = subprocess.run([str(repo / 'build/FableForge.exe'), '--install', str(root),
        '--auto', str(script), '--size', '1440x1000'], cwd=repo,
        capture_output=True, text=True, timeout=600,
        env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1'))
    (work / 'guide-output.log').write_text(result.stdout + result.stderr, encoding='utf-8')
    log = Path(str(script) + '.log').read_text(encoding='utf-8')
    assert result.returncode == 0 and 'RESULT PASS' in log, log[-3000:]
    # The GUI deploy must install the same key files as the checked CLI build.
    for name in ('data/CompiledDefs/game.bin', 'data/Levels/FinalAlbion.wad',
                 'Mods/FableControllerSupport/FableControllerSupport.dll', 'Mods.ini'):
        assert (root / name).read_bytes() == (work / 'merged' / name).read_bytes(), name
    assert not (root / 'Mods/FableControllerSupport/Data').exists()
    print('Real GUI check/deploy and CLI output comparison: PASS', shots)


if __name__ == '__main__':
    main()
