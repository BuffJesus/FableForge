#!/usr/bin/env python3
"""GUI mod commands must use the literal selected path, including shell metacharacters."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(os.environ.get('FABLE_ROOT',
        r'C:\Programs\Steam\steamapps\common\Fable The Lost Chapters')))
    parser.add_argument('--gui', type=Path, default=repo / 'build/FableForge.exe')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='mod-gui-paths-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    root = work / 'install %FORGE_TEST_TOKEN% & content'
    defs = root / 'data/CompiledDefs'
    defs.mkdir(parents=True)
    for name in ('game.bin', 'names.bin'):
        shutil.copyfile(args.root / 'data/CompiledDefs' / name, defs / name)
    tool = args.gui.resolve().with_name('forge-tools.exe')
    for tag in ('A', 'B'):
        pack = work / ('Pack' + tag)
        (pack / 'data/Misc').mkdir(parents=True)
        (pack / 'assets').mkdir()
        (pack / 'forge_pack.json').write_text(json.dumps({'version': 1, 'name': 'Pack' + tag,
            'models': [], 'groundThemes': []}), encoding='utf-8')
        (pack / 'data/Misc/shared.txt').write_text('from pack ' + tag, encoding='utf-8')
        result = subprocess.run([str(tool), 'mods', 'add', str(root), str(pack), '--name', 'Pack' + tag],
            cwd=repo, capture_output=True, text=True, timeout=60)
        assert result.returncode == 0, result.stdout + result.stderr
    decoy = work / 'install expanded & content'
    (decoy / 'data/CompiledDefs').mkdir(parents=True)
    for name in ('game.bin', 'names.bin'):
        shutil.copyfile(root / 'data/CompiledDefs' / name, decoy / 'data/CompiledDefs' / name)
    for install in (root, decoy):
        (install / 'data/Misc').mkdir()
        (install / 'data/Misc/shared.txt').write_bytes(b'original shared file')
    picks = root / 'forge_mods_picks.txt'
    picks.write_bytes(b'file:data/misc/shared.txt\tPackA\n')
    def snapshot(install):
        return {p.relative_to(install).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in install.rglob('*') if p.is_file()}
    original, other = snapshot(root), snapshot(decoy)
    def run(label, commands):
        script = work / (label + '.txt')
        script.write_text('\n'.join(['wait_maps', 'wait_ready',
            f'set saveroot {root}\\', 'mods_tab 1', 'frames 2',
            'assert_state mods_count 2', *commands, 'quit', '']), encoding='utf-8')
        result = subprocess.run([str(args.gui.resolve()), '--auto', str(script), '--install', str(args.root)],
            cwd=repo, capture_output=True, text=True, timeout=180,
            env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1', FORGE_TEST_TOKEN='expanded'))
        (work / (label + '_output.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        log = Path(str(script) + '.log').read_text(encoding='utf-8')
        assert result.returncode == 0 and 'RESULT PASS' in log, log[-2200:]
    run('deploy', ['mods_conflicts', 'wait_mods', 'assert_state mods_conflicts 1',
        'assert_state mods_picks 1', 'mods_deploy', 'wait_mods', 'assert_log mods deploy: done',
        f'screenshot {(work / "deployed.png").as_posix()}'])
    assert (root / 'data/Misc/shared.txt').read_text() == 'from pack A'
    assert snapshot(decoy) == other
    run('undeploy', ['mods_undeploy', 'wait_mods', 'assert_log mods undeploy: done'])
    assert snapshot(root) == original
    saved = picks.read_bytes()
    picks.unlink()
    picks.mkdir()
    run('failure', ['mods_conflicts', 'wait_mods', 'assert_state mods_conflicts 0',
        'assert_log conflict choices are not a regular file', 'assert_log mods conflicts: FAILED (rc 1)'])
    picks.rmdir()
    picks.write_bytes(saved)
    assert snapshot(root) == original
    assert snapshot(decoy) == other
    print('Literal path conflicts, deploy and undeploy; exact selected/decoy preservation: PASS')


if __name__ == '__main__':
    main()
