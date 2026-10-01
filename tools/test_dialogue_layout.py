#!/usr/bin/env python3
"""Capture and check Dialogue heads in owned scratch at normal and compact sizes."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "tests/ui/dialogue_head_eyes.txt"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='dialogue-layout-', dir=ROOT/'build')).resolve()
    print('Evidence retained at', work, flush=True)
    for size, scale in [('1440x900', '1'), ('1280x720', '1.5'), ('1024x600', '1.5')]:
        dest = work/size
        dest.mkdir()
        script = dest/'heads.txt'
        body = SOURCE.read_text(encoding='utf-8').replace(
            'wait_ready\n', f'wait_ready\nset uiscale {scale}\n')
        body = body.replace('build/ui/', dest.as_posix()+'/')
        script.write_text(body, encoding='utf-8')
        run = subprocess.run([str(ROOT/'build/FableForge.exe'), '--install', str(args.install),
                              '--size', size, '--auto', str(script)], cwd=ROOT,
                             capture_output=True, text=True, timeout=180,
                             env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1'))
        (dest/'output.log').write_text(run.stdout+run.stderr, encoding='utf-8')
        log = Path(str(script)+'.log').read_text(encoding='utf-8', errors='replace')
        assert run.returncode == 0 and 'RESULT PASS' in log, log[-2500:]
        rects = re.findall(r'widget_rect dialogue_head_viewport ([\d.]+) ([\d.]+) ([\d.]+) ([\d.]+)', log)
        shots = [line.split(' ', 1)[1] for line in body.splitlines() if line.startswith('screenshot ')]
        assert len(rects) == len(shots) == 8
        for rect, shot in zip(rects, shots):
            box = tuple(round(float(value)) for value in rect)
            with Image.open(shot) as image:
                assert 0 <= box[0] < box[2] <= image.width and 0 <= box[1] < box[3] <= image.height
                image.crop(box).save(Path(shot).with_suffix('.preview.png'))
        if scale == '1':
            for test in ('test_dialogue_head_pixels.py', 'test_dialogue_head_pose_pixels.py'):
                subprocess.run([sys.executable, str(ROOT/'tools'/test), '--capture-dir', str(dest)],
                               cwd=ROOT, check=True)
        print('Dialogue layout', size, 'PASS', flush=True)


if __name__ == '__main__':
    main()
