#!/usr/bin/env python3
"""Repair a missing game.bin through Setup, including a selected Data folder."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from test_setup_restore_ui import level


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gui', type=Path, default=repo/'build/FableForge.exe')
    parser.add_argument('--cwd', type=Path, default=repo)
    layout = parser.add_mutually_exclusive_group()
    layout.add_argument('--missing-levels', action='store_true',
        help='restore the only loose level instead of the definition bank')
    layout.add_argument('--wad', type=Path, help='copy and restore a missing stock WAD; source stays unchanged')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='setup-missing-bank-', dir=repo/'build')).resolve()
    print('Evidence retained at', work, flush=True)
    source = work/'hold.cpp'
    source.write_text('#include <windows.h>\n#include <cstdio>\nint main() { puts("ready"); fflush(stdout); Sleep(60000); }\n')
    cache = (repo/'build/CMakeCache.txt').read_text()
    compiler = re.search(r'^CMAKE_CXX_COMPILER:(?:FILEPATH|STRING)=(.+)$', cache, re.MULTILINE).group(1)
    helper = work/'hold.exe'
    subprocess.run([compiler, str(source), '-o', str(helper)], check=True, capture_output=True, timeout=60)
    source_digest = digest(args.wad) if args.wad else None
    for size, scale, pick_data in [('1440x900', 1, False), ('800x600', 1.5, True)]:
        case = work/size
        root = case/'install'
        bank = root/'data/CompiledDefs/game.bin'
        bank.parent.mkdir(parents=True)
        backup = Path(str(bank)+'.forge-orig')
        backup.write_bytes(b'x')
        levels = root/'data/Levels/FinalAlbion'
        levels.mkdir(parents=True)
        (levels/'MissingBank.lev').write_bytes(level(4))
        (levels/'MissingBank.tng').write_bytes(b'Version 2;\r\n')
        target, expected = bank, b'x'
        if args.missing_levels:
            bank.write_bytes(b'x')
            backup.unlink()
            target, expected = levels/'MissingBank.lev', level(4)
            target.unlink()
            backup = Path(str(target)+'.forge-orig')
            backup.write_bytes(expected)
        selected = 'MissingBank'
        if args.wad:
            bank.write_bytes(b'x')
            backup.unlink()
            for path in levels.iterdir(): path.unlink()
            target = root/'data/Levels/FinalAlbion.wad'
            backup = Path(str(target)+'.forge-orig')
            shutil.copyfile(args.wad, backup)
            selected = 'Greatwood_1'
        expected_digest = source_digest if args.wad else hashlib.sha256(expected).hexdigest()
        unchanged = {p: digest(p) for p in root.rglob('*') if p.is_file()}
        script = case/'restore.txt'
        buttons = [f'set uiscale {scale}', 'setup 1', 'frames 5', 'assert_state install_valid 0',
            'reveal btn_restore_all', 'frames 3', 'reveal btn_restore_all', 'frames 3',
            'assert_widget btn_restore_all', 'click btn_restore_all', 'frames 3',
            'reveal btn_restore_confirm', 'frames 3', 'reveal btn_restore_confirm', 'frames 3',
            'click btn_restore_confirm']
        def run(path):
            with Path(str(path)+'.output.log').open('w', encoding='utf-8') as output:
                result = subprocess.run([str(args.gui.resolve()), '--install', str(root/'data' if pick_data else root),
                    '--size', size, '--auto', str(path)], cwd=args.cwd.resolve(), stdout=output,
                    stderr=subprocess.STDOUT, timeout=90,
                    env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1',
                             APPDATA=str(case/'appdata'), LOCALAPPDATA=str(case/'localappdata')))
            log = Path(str(path)+'.log').read_text(encoding='utf-8')
            assert result.returncode == 0 and 'RESULT PASS' in log, log[-5000:]
        # This owned sleeping helper only simulates the process-image guard.
        image = root/'Fable.exe'
        shutil.copyfile(helper, image)
        guard = case/'guard.txt'
        guard.write_text('\n'.join(buttons+['frames 3', 'assert_log Fable.exe is running',
            'assert_state setup_restore_failed 1', 'assert_state install_valid 0', 'quit'])+'\n', encoding='utf-8')
        process = subprocess.Popen([str(image)], cwd=root, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            assert process.stdout.readline().strip() == 'ready'
            run(guard)
            assert not target.exists()
            assert all(digest(p) == value for p, value in unchanged.items())
        finally:
            process.terminate(); process.communicate(timeout=10)
            image.unlink()
        script.write_text('\n'.join(buttons+['wait_maps', 'wait_ready',
            'assert_state install_valid 1', 'assert_state setup_restore_failed 0',
            'assert_state backups_differ 0', f'select {selected}', 'wait_loaded',
            'assert_state preview_has_mesh 1',
            'assert_state levels_loose 0' if args.wad else 'assert_state terrain_center_height 4.000',
            'setup 1', 'frames 3',
            f'screenshot {(case/"restored.png").as_posix()}', 'quit'])+'\n', encoding='utf-8')
        run(script)
        assert digest(target) == expected_digest
        assert all(digest(p) == value for p, value in unchanged.items())
        assert {p for p in root.rglob('*') if p.is_file()} == set(unchanged) | {target}
        print(size, 'Data folder' if pick_data else 'install root',
            'missing stock WAD' if args.wad else 'missing loose level' if args.missing_levels else 'missing bank',
            'repaired: PASS', flush=True)
    if args.wad: assert digest(args.wad) == source_digest


if __name__ == '__main__':
    main()
