#!/usr/bin/env python3
"""Running-game guards distinguish an install from similarly named siblings."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def main():
    if os.name != 'nt':
        raise SystemExit('this process-image guard regression requires Windows')
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=repo / 'build/forge.exe')
    exe = parser.parse_args().exe.resolve()
    work = Path(tempfile.mkdtemp(prefix='install-guard-', dir=repo / 'build')).resolve()
    assert work.parent == (repo / 'build').resolve()
    print('evidence retained at', work, flush=True)
    source = work / 'hold.cpp'
    source.write_text('#include <windows.h>\n#include <cstdio>\nint main() { puts("ready"); fflush(stdout); Sleep(60000); }\n')
    cache = (repo / 'build/CMakeCache.txt').read_text()
    compiler = re.search(r'^CMAKE_CXX_COMPILER:FILEPATH=(.+)$', cache, re.MULTILINE).group(1)
    helper = work / 'hold.exe'
    subprocess.run([compiler, str(source), '-o', str(helper)], check=True, capture_output=True, timeout=60)
    root = work / 'install'
    bank = root / 'data/CompiledDefs/game.bin'
    bank.parent.mkdir(parents=True)
    backup = Path(str(bank) + '.forge-orig')
    def run(label, install):
        result = subprocess.run([str(exe), 'restore', '--install', str(install), '--forget'],
            cwd=repo, capture_output=True, text=True, timeout=30)
        (work / (label + '.log')).write_text(result.stdout + result.stderr)
        return result
    for label, folder, blocked in (('sibling', work / 'install-other', False),
            ('own', root, True), ('nested', root / 'bin', True)):
        folder.mkdir(parents=True, exist_ok=True)
        image = folder / 'Fable.exe'
        shutil.copyfile(helper, image)
        bank.write_bytes(b'edited')
        backup.write_bytes(b'original')
        process = subprocess.Popen([str(image)], cwd=folder, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            assert process.stdout.readline().strip() == 'ready', 'test process failed to start'
            result = run(label, root)
            if blocked:
                assert result.returncode != 0 and 'Fable.exe is running' in result.stdout + result.stderr
                assert bank.read_bytes() == b'edited' and backup.read_bytes() == b'original'
                # Canonicalization must also handle case, separators and a trailing separator.
                alias = str(root).upper().replace('\\', '/') + '/'
                assert run(label + '_alias', alias).returncode != 0
            else:
                assert result.returncode == 0, result.stdout + result.stderr
                assert bank.read_bytes() == b'original' and not backup.exists()
        finally:
            process.terminate()
            process.communicate(timeout=10)
        image.unlink()
    assert run('after_exit', root).returncode == 0
    assert bank.read_bytes() == b'original' and not backup.exists()
    print('Sibling allowed, own/nested processes blocked, exit retry: PASS')


if __name__ == '__main__':
    main()
