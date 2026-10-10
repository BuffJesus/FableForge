#!/usr/bin/env python3
"""Check cell-centre placement, paste and carry at normal and compact sizes."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    work = Path(tempfile.mkdtemp(prefix='placement-grid-', dir=repo / 'build'))
    print('Evidence retained at', work, flush=True)
    template = (repo / 'tests/ui/placement_grid.txt').read_text(encoding='utf-8')
    for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
        dest = work / size
        dest.mkdir()
        script = dest / 'placement.txt'
        text = template.replace('edit 1\n', f'set uiscale {scale}\nedit 1\n', 1)
        text = text.replace('build/ui/placement_grid_options.png', (dest / 'options.png').as_posix())
        script.write_text(text, encoding='utf-8')
        result = subprocess.run([str(repo / 'build/FableForge.exe'), '--install', str(args.install),
                                 '--size', size, '--auto', str(script)], cwd=repo,
                                capture_output=True, text=True, timeout=180,
                                env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1'))
        (dest / 'output.log').write_text(result.stdout + result.stderr, encoding='utf-8')
        log = Path(str(script) + '.log').read_text(encoding='utf-8')
        assert result.returncode == 0 and 'RESULT PASS' in log, log[-4000:]
        print(size, 'grid toggle, snapped ground, fixed height, pointer, paste, carry and undo: PASS', flush=True)


if __name__ == '__main__':
    main()
