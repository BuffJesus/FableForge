#!/usr/bin/env python3
"""Check short-effect auto length, end replay and live timeline scrubbing."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import numpy as np
from test_effect_grid_pixels import captured_previews


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    work = Path(tempfile.mkdtemp(prefix='effect-timing-', dir=repo / 'build'))
    print('Evidence retained at', work, flush=True)
    for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
        dest = work / size
        dest.mkdir()
        lines = ['wait_maps', 'wait_ready', 'assets_tab 3', f'set uiscale {scale}',
                 'effect_preview_play 0', 'effect_select ACTIVATE_SKILL_01', 'frames 3',
                 'assert_state effect_preview_auto_duration 1',
                 'assert_state effect_preview_duration 0.533333',
                 'effect_preview_loop 0', 'effect_preview_advance_frames 30',
                 'assert_state effect_preview_playing 0', 'assert_state effect_preview_particles 0',
                 'assert_state effect_preview_position 0.533333',
                 'reveal effect_preview_image', 'frames 3', f'screenshot {dest.as_posix()}/finished.png',
                 'reveal btn_effect_play', 'frames 3', 'click btn_effect_play', 'frames 3',
                 'assert_state effect_preview_playing 1', 'click btn_effect_play',
                 'effect_preview_duration .51', 'effect_preview_seek 0',
                 'effect_preview_advance_frames 30', 'assert_state effect_preview_playing 0',
                 'assert_state effect_preview_position 0.510000',
                 'assert_state effect_preview_time 0.500000',
                 'assert_state effect_preview_auto_duration 0',
                 'effect_preview_seek 0', 'reveal effect_timeline', 'frames 3',
                 'mouse_move effect_timeline', 'mouse_down left', 'mouse_delta 10000 0', 'frames 3',
                 'assert_state effect_preview_position 0.510000',
                 'mouse_up left', 'frames 3', 'reveal check_effect_auto_duration', 'frames 3',
                 'click check_effect_auto_duration', 'frames 3',
                 'assert_state effect_preview_auto_duration 1',
                 'assert_state effect_preview_duration 0.533333',
                 'effect_select BRAZIERFIREFINAL', 'frames 3',
                 'assert_state effect_preview_auto_duration 1', 'assert_state effect_preview_duration 10.000000',
                 'effect_select AIR_GLOW_01', 'frames 3',
                 'assert_state effect_preview_duration 1.233333', 'effect_preview_seek .5',
                 'reveal effect_preview_image', 'mouse_move btn_effect_play', 'frames 3',
                 'dump_widget effect_preview_image', f'screenshot {dest.as_posix()}/burst.png', 'quit']
        script = dest / 'timing.txt'
        script.write_text('\n'.join(lines) + '\n', encoding='utf-8')
        result = subprocess.run([str(repo / 'build/FableForge.exe'), '--install', str(args.install),
                                 '--size', size, '--auto', str(script)], cwd=repo,
                                capture_output=True, text=True, timeout=180,
                                env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1'))
        (dest / 'output.log').write_text(result.stdout + result.stderr, encoding='utf-8')
        log = Path(str(script) + '.log').read_text(encoding='utf-8')
        assert result.returncode == 0 and 'RESULT PASS' in log, log[-4000:]
        pixels = captured_previews(dest, Path(str(script)+'.log'), ['burst.png'])[0].astype(np.int16)
        visible = int((np.max(np.abs(pixels - pixels[0, 0]), axis=2) > 3).sum())
        assert visible > 150, f'Air glow framed too far away: {visible} visible pixels'
        print(size, 'air glow visible pixels', visible, flush=True)
        print(size, 'auto lengths, completed replay, fractional end and drag-before-release seek: PASS', flush=True)


if __name__ == '__main__':
    main()
