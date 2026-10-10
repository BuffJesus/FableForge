#!/usr/bin/env python3
"""Compare whole-map Raise camera fields in independently read scratch TNGs."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile


def coord(value):
    match = re.fullmatch(r'C3DCoordF\(([^,]+),([^,]+),([^,]+)\)', value)
    assert match, value
    return tuple(float(part) for part in match.groups())


def blocks(text):
    return re.findall(r'NewThing[^;\r\n]+;.*?EndThing;', text, re.S)


def check(before, after, delta):
    a, b = blocks(before), blocks(after)
    assert len(a) == len(b)
    assert re.sub(r'NewThing[^;\r\n]+;.*?EndThing;', '', before, flags=re.S) == \
        re.sub(r'NewThing[^;\r\n]+;.*?EndThing;', '', after, flags=re.S)
    bases = keys = 0
    for original, raised in zip(a, b):
        expected = original
        physics = re.search(r'StartCTCPhysics(?:Standard|Navigator);(.*?)EndCTCPhysics(?:Standard|Navigator);', original, re.S)
        moved = physics is not None
        if moved:
            old = re.search(r'PositionZ\s+([^;]+);', physics[1])
            assert old
            expected = expected.replace(old[0], f'PositionZ {float(old[1]) + delta:.6f};', 1)
        initial = re.search(r'^InitialPosZ\s+([^;]+);', original, re.M)
        if moved and initial:
            expected = expected.replace(initial[0], f'InitialPosZ {float(initial[1]) + delta:.6f};', 1)
        for camera in re.finditer(r'Start(CTCCameraPoint\w+);(.*?)End\1;', original, re.S):
            body = camera[2]
            def flag(name):
                match = re.search(r'^' + name + r'\s+(TRUE|FALSE);', body, re.M)
                return bool(match and match[1] == 'TRUE')
            shift = (delta if flag('UsingRelativeCoords') else 0) - (delta if moved and flag('IsCoordBaseRelativeToParent') else 0)
            base = re.search(r'^CoordBase\s+([^;]+);', body, re.M)
            if base and shift:
                x, y, z = coord(base[1])
                changed = re.search(r'^CoordBase\s+([^;]+);', raised[raised.find('Start'+camera[1]):], re.M)
                assert changed and all(abs(v-w) < .0001 for v, w in zip(coord(changed[1]), (x, y, z+shift)))
                expected = expected.replace(base[0], changed[0], 1)
                bases += 1
            if camera[1] == 'CTCCameraPointScriptedSpline':
                count = int(re.search(r'^NumKeyCameras\s+(\d+);', body, re.M)[1])
                for key in re.finditer(r'^(KeyCameras\[(\d+)\]\.Position)\s+([^;]+);', body, re.M):
                    if int(key[2]) >= count:
                        continue
                    x, y, z = coord(key[3])
                    changed = re.search(r'^' + re.escape(key[1]) + r'\s+([^;]+);', raised, re.M)
                    assert changed and all(abs(v-w) < .0001 for v, w in zip(coord(changed[1]), (x, y, z+delta)))
                    expected = expected.replace(key[0], changed[0], 1)
                    keys += 1
        # Numeric physics spelling may trim zeros. Substitute only validated Z values;
        # all other rows and spacing must be identical, including camera directions/times.
        for name in ['PositionZ', 'InitialPosX', 'InitialPosY', 'InitialPosZ']:
            old = re.search(r'^' + name + r'\s+([^;]+);', expected, re.M)
            new = re.search(r'^' + name + r'\s+([^;]+);', raised, re.M)
            if old and new:
                assert abs(float(old[1]) - float(new[1])) < .0001
                expected = expected.replace(old[0], new[0], 1)
        assert expected == raised, original[:150]
    assert bases > 0 and keys > 0, (bases, keys)
    return bases, keys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    work = Path(tempfile.mkdtemp(prefix='wholemap-cameras-', dir=repo / 'build'))
    print('Evidence retained at', work, flush=True)
    contents = {}
    for name, amount in [('baseline', 0), ('raised', 3), ('lowered', -10)]:
        out = work / name
        script = work / (name + '.txt')
        lines = ['wait_maps', 'wait_ready', f'set saveroot {out.as_posix()}',
                 'select StartOakValeWest', 'wait_loaded', 'wait_foliage', 'edit 1',
                 'snapshot_document', 'snapshot_heights']
        if amount:
            lines += [f'terrain_raise_all {amount} 1', 'wait_foliage']
        lines += ['save_level', 'frames 3', 'assert_state doc_dirty 0']
        if amount:
            lines += ['undo', 'wait_foliage', 'assert_document_same', 'assert_heights_changed 0']
        lines += ['quit']
        script.write_text('\n'.join(lines) + '\n', encoding='utf-8')
        result = subprocess.run([str(repo / 'build/FableForge.exe'), '--install', str(args.install),
                                 '--auto', str(script)], cwd=repo, capture_output=True, text=True,
                                timeout=180, env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1'))
        (work / (name + '.output.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        log = Path(str(script) + '.log').read_text(encoding='utf-8')
        assert result.returncode == 0 and 'RESULT PASS' in log, log[-3000:]
        contents[name] = (out / 'data/Levels/FinalAlbion/StartOakValeWest.tng').read_bytes().decode('latin1')
    for name, amount in [('raised', 3), ('lowered', -10)]:
        bases, keys = check(contents['baseline'], contents[name], amount)
        print(name, bases, 'coordinate bases and', keys, 'spline keys; unrelated rows and undo: PASS', flush=True)


if __name__ == '__main__':
    main()
