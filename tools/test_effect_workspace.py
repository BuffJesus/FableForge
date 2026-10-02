"""Exercise Effects controls and rendered pixels in normal and compact workspaces."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='effect-workspace-', dir=repo/'build'))
    print('Evidence retained at', work, flush=True)
    controls = ['btn_effect_play', 'btn_effect_restart', 'btn_effect_step', 'btn_effect_frame',
                'btn_effect_frame_current', 'check_effect_loop', 'drag_effect_duration',
                'combo_effect_speed', 'effect_timeline', 'effect_background_color', 'check_effect_grid']
    for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
        dest = work/size
        dest.mkdir()
        for name in ('effect_transport', 'effect_grid', 'effect_frame_current', 'effect_browser'):
            lines = []
            for line in (repo/'tests/ui'/f'{name}.txt').read_text().splitlines():
                if line == 'wait_ready':
                    lines.extend([line, f'set uiscale {scale}'])
                    continue
                if line.startswith('click '):
                    lines.extend(['reveal ' + line[6:], 'frames 3'])
                if line.startswith('dump_widget effect_preview_image'):
                    lines.extend(['reveal effect_preview_image', 'frames 3'])
                if line.startswith('screenshot '):
                    line = 'screenshot ' + (dest/Path(line[11:]).name).as_posix()
                if line == 'quit' and name == 'effect_grid':
                    for control in controls:
                        lines.extend(['reveal '+control, 'frames 3', 'dump_widget '+control])
                    lines.extend(['reveal effect_preview_image', 'frames 3',
                                  'screenshot '+(dest/'controls.png').as_posix()])
                lines.append(line)
            script = dest/(name+'.txt')
            script.write_text('\n'.join(lines)+'\n')
            result = subprocess.run([str(repo/'build/FableForge.exe'), '--install', str(args.install),
                '--auto', str(script), '--size', size], cwd=repo, capture_output=True, text=True,
                timeout=120, env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1'))
            (dest/(name+'-output.log')).write_text(result.stdout+result.stderr)
            log = Path(str(script)+'.log')
            trace = log.read_text()
            assert result.returncode == 0 and 'RESULT PASS' in trace, trace[-2400:]
            if name in ('effect_grid', 'effect_frame_current'):
                subprocess.run([sys.executable, str(repo/'tools'/f'test_{name}_pixels.py'),
                    '--capture-dir', str(dest), '--log', str(log)], check=True)
            if name == 'effect_grid':
                # Every revealed control must fit horizontally inside the preview pane.
                preview = re.findall(r'widget_rect effect_preview_image ([\d.-]+) ([\d.-]+) ([\d.-]+) ([\d.-]+)', trace)[0]
                left, top, right, bottom = map(float, preview)
                assert abs((right-left)/(bottom-top)-4/3) < .01, preview
                if size == '1440x900':
                    assert right-left < 720 and left > 100, 'Preview should be bounded and centred'
                for control in controls:
                    match = re.search(r'widget_rect '+control+r' ([\d.-]+) ([\d.-]+) ([\d.-]+) ([\d.-]+)', trace)
                    assert match, control
                    x0, y0, x1, y1 = map(float, match.groups())
                    assert left <= x0 < x1 <= right + 1, (control, (x0, x1), (left, right))
            print(size, name, 'PASS', flush=True)


if __name__ == '__main__':
    main()
