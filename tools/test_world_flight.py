"""Hidden, paced flight captures and coverage comparisons. Requires Pillow/numpy.

This is a visual diagnostic, not an FPS benchmark. Suspect pixels require review:
coarse and detailed terrain silhouettes legitimately differ.
"""
import argparse
import ctypes
from ctypes import wintypes
import json
import math
import os
import re
from pathlib import Path
import subprocess
import time

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[1]


def missing_coverage(live, coarse, settled):
    covered = Image.fromarray((~coarse & ~settled).astype('uint8')*255).filter(ImageFilter.MinFilter(5))
    return live & (np.asarray(covered) != 0)


def check_detector():
    # Positive control: a deliberately missing interior patch must be detected.
    reference = np.zeros((32, 32), dtype=bool)
    reference[:4] = True  # existing sky must not be reported
    broken = reference.copy()
    broken[12:20, 12:20] = True
    if missing_coverage(reference, reference, reference).any() or missing_coverage(broken, reference, reference).sum() != 64:
        raise RuntimeError('Coverage detector positive/negative controls failed')


def analyze_sequence(out, log, box):
    records = []
    for line in log.splitlines():
        match = re.match(r'\s*[\d.]+ capture (.+)', line)
        if match:
            records.append(dict(path=Path(match[1]), state={}))
        elif records and (match := re.match(r'\s*[\d.]+\s+(world_\w+)=(.*)', line)):
            records[-1]['state'][match[1]] = match[2]
    pose_keys = ['world_eye_x', 'world_eye_y', 'world_eye_height', 'world_camera_yaw', 'world_camera_pitch']
    recent, spikes, changes = [], [], []
    for record in records:
        with Image.open(record['path']) as im:
            rgb = np.asarray(im.convert('RGB').crop(box)).astype(np.int16)
        mask = np.all(rgb == (19, 18, 26), axis=2)
        pose = tuple(record['state'].get(key) for key in pose_keys)
        if recent and pose == recent[-1][3]:
            changed = np.max(np.abs(rgb - recent[-1][1]), axis=2) > 80
            count = int(changed.sum())
            if count > 128:
                changes.append(dict(frame=record['path'].name, changed_pixels=count, state=record['state']))
        recent.append((record, rgb, mask, pose))
        if len(recent) == 3:
            count = int(missing_coverage(recent[1][2], recent[0][2], mask).sum())
            if count:
                spikes.append(dict(frame=recent[1][0]['path'].name, missing_pixels=count))
            recent.pop(0)
    report = dict(continuous_frames=len(records), transient_candidates=sorted(spikes, key=lambda s: -s['missing_pixels'])[:30],
                  stationary_changes=sorted(changes, key=lambda s: -s['changed_pixels'])[:20])
    (out / 'sequence-report.json').write_text(json.dumps(report, indent=2))
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=ROOT / 'build/FableForge.exe')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/world-flight')
    parser.add_argument('--cache', type=Path, default=ROOT / 'build/water-overview-cache')
    parser.add_argument('--route', choices=['start', 'oakvale', 'world'], default='world')
    parser.add_argument('--stress', action='store_true', help='One-map budget, long orbit distance and repeated altitude transitions')
    parser.add_argument('--continuous', action='store_true', help='Save every rendered frame during the live route')
    parser.add_argument('--focus-distance', type=float, help='Orbit distance retained during free flight; compare with a distant-focus reference')
    parser.add_argument('--assert-focus-coverage', action='store_true', help='Fail if any focus comparison loses more than 64 pixels (allow tiny silhouette shifts)')
    parser.add_argument('--coarse-only', action='store_true', help='Isolate world terrain/water visibility without object streaming')
    parser.add_argument('--memory-pressure', action='store_true', help='Simulate an exhausted GPU memory budget throughout the route (no VRAM allocation)')
    parser.add_argument('--sample-step', type=int, default=6, choices=range(1, 25))
    args = parser.parse_args()
    if args.assert_focus_coverage and args.focus_distance is None:
        parser.error('--assert-focus-coverage requires --focus-distance')
    check_detector()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    start, end = ((3500, 780), (3640, 850)) if args.route == 'start' else ((2510, 1950), (2640, 1990))
    poses = []
    for i in range(49):
        t = (i if i <= 24 else 48-i) / 24
        x, y = [a+(b-a)*t for a, b in zip(start, end)]
        yaw = math.atan2(-(end[0]-start[0]), end[1]-start[1]) + (math.pi if i > 24 else 0)
        clearance = 8+25*math.sin(math.pi*t)**2
        pitch = .28
        if args.stress:
            clearance = 8+740*math.sin(2*math.pi*t)**2
            pitch = 1.5  # keep the terrain beneath the route in view at high altitude
        poses.append(f'world_eye_ground {x:.4f} {y:.4f} {clearance:.4f} {yaw:.5f} {pitch}')
    if args.route == 'world':
        # Traverse the actual WLD layout, including separate regions and broad overview.
        waypoints = [(3550, 825, 70), (2535, 1950, 120), (1800, 2100, 350),
                     (3100, 3250, 900), (3430, 3550, 80), (3180, 4050, 200),
                     (2000, 3440, 700), (850, 4220, 120), (2600, 6100, 1800),
                     (4200, 7400, 4000), (2700, 4400, 7000), (3300, 3550, 50)]
        poses = []
        for a, b in zip(waypoints, waypoints[1:]):
            for step in range(8):
                t = step / 8
                x, y, height = [u+(v-u)*t for u, v in zip(a, b)]
                yaw = math.atan2(-(b[0]-a[0]), b[1]-a[1])
                pitch = min(1.5, max(.35, math.atan2(height, 500)))
                poses.append(f'world_pose {x:.3f} {y:.3f} {height:.3f} {yaw:.5f} {pitch:.5f}')
        x, y, height = waypoints[-1]
        poses.append(f'world_pose {x} {y} {height} 0.8 0.35')
    sampled = sorted(set(range(0, len(poses), args.sample_step)) | {len(poses)-1})
    focus_distance = args.focus_distance if args.focus_distance is not None else (8000 if args.stress or args.route == 'world' else 200)
    lines = ['wait_maps', 'wait_ready', 'set uiscale 1', 'set world_auto_detail 0',
             f'set world_detail_limit {1 if args.stress else 6}', 'world_tab 1', 'wait_world_tiles',
             'set world_3d 1', 'wait_world_tiles', 'mouse_move 20 20', 'clear_toasts',
             f'world_camera {start[0]} {start[1]} 5 0 0.3 {focus_distance}']
    phases = ['live', 'coarse', 'unculled', 'settled', 'settled_unculled']
    if args.memory_pressure:
        lines.insert(2, 'world_memory_sample 1073741824 1073741824')
    if args.coarse_only:
        phases = ['live', 'coarse', 'unculled']
    if args.focus_distance is not None:
        phases.append('wide')
    for phase in phases:
        if phase == 'wide':
            lines += [f'world_camera {start[0]} {start[1]} 5 0 0.3 8000']
        lines += [f'set world_detail {int(not args.coarse_only and phase in ["live", "settled", "settled_unculled"])}',
                  f'set world_culling {int("unculled" not in phase)}']
        if phase == 'live' and args.continuous:
            lines += [f'capture_begin {(out / "sequence").as_posix()}']
        for i in (range(len(poses)) if phase == 'live' else sampled):
            lines += [poses[i], 'frames 3']
            if phase.startswith('settled'):
                lines += ['wait_world_detail']
            if i in sampled:
                lines += [f'screenshot {(out / f"{phase}-{i:03}.png").as_posix()}', 'dump_state']
        if phase == 'live' and args.continuous:
            lines += ['capture_end']
    lines += ['quit']
    script = out / 'flight.txt'
    script.write_text('\n'.join(lines)+'\n')
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = subprocess.SW_HIDE
    env = dict(os.environ, FABLEFORGE_TILE_CACHE=str(args.cache.resolve()))
    user = ctypes.WinDLL('user32', use_last_error=True)
    user.GetForegroundWindow.restype = wintypes.HWND
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.IsWindowVisible.argtypes = [wintypes.HWND]
    callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user.EnumWindows.argtypes = [callback, wintypes.LPARAM]
    user.EnumWindows.restype = wintypes.BOOL
    visible, foreground, polls = [], [], 0
    proc = subprocess.Popen([str(args.exe.resolve()), '--auto', str(script), '--size', '1280x800',
                             '--auto-frame-ms', '33'], cwd=ROOT, env=env, startupinfo=startup,
                            creationflags=subprocess.BELOW_NORMAL_PRIORITY_CLASS | subprocess.CREATE_NO_WINDOW)
    deadline = time.monotonic()+300
    try:
        while proc.poll() is None:
            polls += 1
            def check(hwnd, _):
                pid = wintypes.DWORD()
                user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
                if pid.value == proc.pid:
                    if user.IsWindowVisible(hwnd):
                        visible.append(int(hwnd))
                    if hwnd == user.GetForegroundWindow():
                        foreground.append(int(hwnd))
                return True
            if not user.EnumWindows(callback(check), 0):
                raise ctypes.WinError(ctypes.get_last_error())
            if visible or foreground:
                raise RuntimeError('Test window visibility/focus watchdog triggered')
            if time.monotonic() > deadline:
                raise TimeoutError('Flight exceeded 300 seconds')
            time.sleep(.05)
    finally:
        if proc.poll() is None:
            proc.kill()
        proc.wait()
        (out / 'window-watch.json').write_text(json.dumps(dict(polls=polls, visible=visible, foreground=foreground, exit=proc.returncode), indent=2))
    log = Path(str(script)+'.log').read_text(errors='replace')
    if proc.returncode or 'RESULT PASS' not in log:
        raise RuntimeError(log[-3000:])
    results, thumbs = [], []
    for i in sampled:
        imgs = {p: Image.open(out / f'{p}-{i:03}.png').convert('RGB') for p in phases}
        w, h = imgs['live'].size
        box = (285, 65, w-356, h-4)  # UI scale 1, standard dock widths
        crops = {p: im.crop(box) for p, im in imgs.items()}
        clear = {p: np.all(np.asarray(im) == (19, 18, 26), axis=2) for p, im in crops.items()}
        if args.coarse_only:
            clear['settled'] = clear['coarse']
            clear['settled_unculled'] = clear['unculled']
        suspect = missing_coverage(clear['live'], clear['coarse'], clear['settled'])
        mismatch = clear['coarse'] ^ clear['unculled']
        detail_mismatch = clear['settled'] ^ clear['settled_unculled']
        results.append(dict(sample=i, suspect_pixels=int(suspect.sum()), culling_coverage_difference=int(mismatch.sum()), detail_culling_coverage_difference=int(detail_mismatch.sum()), live_clear_pixels=int(clear['live'].sum())))
        if 'wide' in clear:
            results[-1]['focus_distance_missing_pixels'] = int((clear['coarse'] & ~clear['wide']).sum())
            delta = np.abs(np.asarray(crops['coarse']).astype(np.int16) - np.asarray(crops['wide']).astype(np.int16))
            results[-1]['focus_colour_changed_pixels'] = int((delta.max(axis=2) > 40).sum())
        arr = np.array(crops['live'])
        arr[suspect] = (255, 40, 40)
        thumb = Image.fromarray(arr)
        thumb.thumbnail((400, 320))
        card = Image.new('RGB', (400, 345), '#181820')
        card.paste(thumb, (0, 25))
        ImageDraw.Draw(card).text((5, 5), f'{args.route} {i}: suspect {suspect.sum()}, cull {mismatch.sum()}', fill='white')
        thumbs.append(card)
    sheet = Image.new('RGB', (1200, 345*math.ceil(len(thumbs)/3)))
    for n, im in enumerate(thumbs):
        sheet.paste(im, ((n % 3)*400, (n // 3)*345))
    sheet.save(out / 'contact.png')
    thumbs[0].save(out / 'flight.gif', save_all=True, append_images=thumbs[1:], duration=180, loop=0)
    report = dict(route=args.route, stress=args.stress, memory_pressure=args.memory_pressure,
                  samples=results, window_polls=polls, visible_samples=len(visible), foreground_samples=len(foreground))
    if args.continuous:
        report.update(analyze_sequence(out, log, box))
    (out / 'report.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))
    if args.assert_focus_coverage and any(s.get('focus_distance_missing_pixels', 0) > 64 for s in results):
        print('FAIL: orbit focus distance changed visible world coverage')
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
