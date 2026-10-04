#!/usr/bin/env python3
"""Saved choices outside the conflict report stay visible and clear individually."""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import time
from test_mod_field_details import write_bank


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--gui', type=Path, default=repo/'build/FableForge.exe')
    parser.add_argument('--cwd', type=Path, default=repo)
    args = parser.parse_args()
    gui, cwd = args.gui.resolve(), args.cwd.resolve()
    work = Path(tempfile.mkdtemp(prefix='mod-unreported-choices-', dir=repo/'build')).resolve()
    print('Evidence retained at', work, flush=True)
    root = work/'install'
    write_bank(root, (1, 2, 3, 4))
    other = work/'other-install'
    write_bank(other, (1, 2, 3, 4))
    def cli(label, *arguments):
        result = subprocess.run([str(gui.with_name('forge-tools.exe')), *map(str, arguments)],
            cwd=cwd, capture_output=True, text=True, timeout=60)
        (work/(label+'.log')).write_text(result.stdout+result.stderr, encoding='utf-8')
        assert result.returncode == 0, result.stdout+result.stderr
        return result.stdout
    sources = []
    for label in ('PackA', 'PackB'):
        pack = work/label
        files = pack/'data/Misc'
        files.mkdir(parents=True)
        (files/'shared.txt').write_text(label, encoding='utf-8')
        if label == 'PackA': (files/'uncontested.txt').write_text('Independent PackA', encoding='utf-8')
        (pack/'forge_pack.json').write_text(json.dumps({'version': 1, 'name': label,
            'models': [], 'groundThemes': []}), encoding='utf-8')
        sources.append(pack)
        cli('add-'+label, 'mods', 'add', root, pack, '--name', label)
    picks = root/'forge_mods_picks.txt'
    original = (b'file:data/misc/missing-target-with-a-long-readable-name.txt\t'
                b'RemovedPack With Long Weapon And Appearance Overrides\n'
                b'file:data/misc/shared.txt\tPackA\n'
                b'file:data/misc/uncontested.txt\tvanilla\n')
    picks.write_bytes(original)
    hashes = {p: hashlib.sha256(p.read_bytes()).hexdigest() for source in (root, other, *sources)
              for p in source.rglob('*') if p.is_file() and p != picks}
    report = json.loads(cli('report', 'mods', 'conflicts', root, '--json'))
    assert len(report['files']['contested']) == 1
    def check_build(label, suppressed):
        output = work/label
        cli(label, 'mods', 'build', root, output, '--json')
        assert (output/'data/Misc/shared.txt').read_bytes() == b'PackA'
        independent = output/'data/Misc/uncontested.txt'
        if suppressed: assert not independent.exists()
        else: assert independent.read_bytes() == b'Independent PackA'
        assert not (output/'data/Misc/missing-target-with-a-long-readable-name.txt').exists()
    check_build('before', True)
    def run(case, size, lines, unlock=None):
        case.mkdir()
        script = case/'review.txt'
        script.write_text('\n'.join(lines+['quit'])+'\n', encoding='utf-8')
        with (case/'output.log').open('w',encoding='utf-8') as output:
            process = subprocess.Popen([str(gui), '--install', str(args.install), '--size', size,
                '--auto', str(script)], cwd=cwd, stdout=output, stderr=subprocess.STDOUT,
                env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1',
                         APPDATA=str(work/'appdata'), LOCALAPPDATA=str(work/'localappdata')))
            try:
                if unlock:
                    deadline=time.monotonic()+150
                    while not (case/'locked.png').exists() and process.poll() is None and time.monotonic()<deadline:
                        time.sleep(.01)
                    assert (case/'locked.png').exists(),'locked choice checkpoint missing'
                    unlock()
                rc=process.wait(timeout=180)
            finally:
                if process.poll() is None: process.terminate();process.wait(timeout=10)
        log = Path(str(script)+'.log').read_text(encoding='utf-8')
        assert rc == 0 and 'RESULT PASS' in log, log[-5000:]
        return log
    def start(scale):
        return ['wait_maps', 'wait_ready', f'set saveroot {root.as_posix()}',
                f'set uiscale {scale}', 'mods_tab 1', 'frames 2', 'mods_conflicts', 'wait_mods', 'frames 2']
    def reveal(widget): return [f'reveal {widget}', 'frames 3', f'reveal {widget}', 'frames 3']
    for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
        case = work/size
        lines = start(scale)+['assert_state mods_conflicts 1', 'assert_state mods_picks 3',
            'assert_state mods_unreported_choices 2', 'assert_state mods_unreported_matching 2']
        lines += reveal('mods_unreported_help')+['dump_widget mods_unreported_help',
            'mouse_move viewport', 'clear_toasts', f'screenshot {(case/"help.png").as_posix()}']
        lines += reveal('input_mod_conflict_search')+['click input_mod_conflict_search',
            'input_text rEmOvEdPaCk', 'frames 3', 'assert_state mods_conflicts_matching 0',
            'assert_state mods_unreported_matching 1']
        lines += reveal('mod_unreported_choice_0')+['dump_widget mod_unreported_choice_0',
            'mouse_move viewport', 'clear_toasts', f'screenshot {(case/"missing.png").as_posix()}']
        lines += reveal('mods_show_all_conflicts')+['click mods_show_all_conflicts', 'frames 3']
        lines += reveal('mods_choices_only')+['click mods_choices_only', 'frames 3',
            'assert_state mods_conflicts_matching 1', 'assert_state mods_unreported_matching 2']
        lines += reveal('mod_unreported_choice_1')+['assert_widget mod_clear_unreported_1']
        log = run(case, size, lines)
        for widget, name in [('mods_unreported_help', 'help'), ('mod_unreported_choice_0', 'missing')]:
            bounds = re.search(r'widget_rect '+widget+r' ([\d.-]+) ([\d.-]+) ([\d.-]+) ([\d.-]+)', log)
            assert bounds, widget
            left, top, right, bottom = map(float, bounds.groups())
            width, height = struct.unpack_from('>II', (case/(name+'.png')).read_bytes(), 16)
            assert 0 <= left < right <= width and 0 <= top < bottom <= height, (widget, bounds.groups())
            assert bottom-top >= 20*scale, (widget, 'long explanation did not wrap')
        assert picks.read_bytes() == original
        print(size, 'missing/uncontested choices, search and read-only preservation: PASS', flush=True)
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
        ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.CloseHandle.restype = wintypes.BOOL
    for size,scale in [('1440x900',1),('800x600',1.5)]:
        picks.write_bytes(original)
        case=work/('locked-'+size)
        handle = kernel.CreateFileW(str(picks), 0x80000000, 1, None, 3, 0x80, None)
        assert handle != wintypes.HANDLE(-1).value
        def unlock():
            nonlocal handle
            assert picks.read_bytes()==original,'failed choice changed persisted winners'
            assert not list(root.glob('.forge-mod-picks-*'))
            kernel.CloseHandle(handle);handle=None
        try:
            lines=start(scale)+reveal('mod_winner_0')+['click mod_winner_0','frames 3',
                'click mod_vanilla_0','frames 3','assert_state mods_first_winner PackA',
                'assert_state mod_choice_failed 1','clear_toasts','mods_tab 0','frames 3',
                'mods_tab 1','mods_conflicts','wait_mods','frames 3',
                'assert_state mod_choice_failed 1']
            lines += [f'set saveroot {other.as_posix()}','frames 3','assert_state mod_choice_failed 0',
                f'set saveroot {root.as_posix()}','mods_conflicts','wait_mods','frames 3',
                'assert_state mods_first_winner PackA','assert_state mod_choice_failed 0']
            lines+=reveal('mod_clear_unreported_0')+['click mod_clear_unreported_0','frames 3',
                'assert_state mods_picks 3','assert_state mods_unreported_choices 2',
                'assert_log cannot save conflict choices','clear_toasts']
            lines+=reveal('mods_choice_error')+['assert_widget mods_choice_error',
                'dump_widget mods_choice_error','mouse_move viewport',
                f'screenshot {(case/"locked.png").as_posix()}','frames 300']
            lines+=reveal('mod_clear_unreported_0')+['click mod_clear_unreported_0','frames 3',
                'assert_state mod_choice_failed 0','assert_state mods_picks 2',
                'assert_state mods_unreported_choices 1','assert_state mods_first_winner PackA']
            log=run(case,size,lines,unlock)
            rect=re.search(r'widget_rect mods_choice_error ([\d.-]+) ([\d.-]+) ([\d.-]+) ([\d.-]+)',log)
            clip=re.search(r'widget_clip_rect mods_choice_error ([\d.-]+) ([\d.-]+) ([\d.-]+) ([\d.-]+)',log)
            assert rect and clip
            left,top,right,bottom=map(float,rect.groups());cl,ct,cr,cb=map(float,clip.groups())
            assert cl<=left<right<=cr and ct<=top<bottom<=cb,(rect.groups(),clip.groups())
            print(size,'persistent choice refusal, unchanged winner and same-process retry: PASS',flush=True)
        finally:
            if handle is not None:kernel.CloseHandle(handle)
    picks.write_bytes(original)
    assert not list(root.glob('.forge-mod-picks-*'))
    lines = start(1.5)+['assert_state mods_unreported_choices 2']
    lines += reveal('mod_clear_unreported_0')+['click mod_clear_unreported_0', 'frames 3',
        'assert_state mods_unreported_choices 1', 'assert_state mods_picks 2']
    lines += reveal('mod_clear_unreported_0')+['click mod_clear_unreported_0', 'frames 3',
        'assert_state mods_unreported_choices 0', 'assert_state mods_picks 1',
        'assert_state mods_first_winner PackA']
    run(work/'clear', '800x600', lines)
    remaining = [line for line in picks.read_text().splitlines() if line and not line.startswith('#')]
    assert remaining == ['file:data/misc/shared.txt\tPackA']
    check_build('after', False)
    # Disabling the second contributor removes the conflict but retains the saved choice.
    lines = start(1.5)+['mod_enable 1 0', 'frames 2', 'mods_conflicts', 'wait_mods', 'frames 2',
        'assert_state mods_conflicts 0', 'assert_state mods_unreported_choices 1']
    lines += reveal('mod_clear_unreported_0')+['click mod_clear_unreported_0', 'frames 3',
        'assert_state mods_picks 0', 'assert_state mods_unreported_choices 0']
    run(work/'no-conflicts', '800x600', lines)
    assert not picks.exists()
    bulk = ''.join(f'file:data/misc/unreported{i:03}.txt\tRemovedPack\n' for i in range(401)).encode('ascii')
    picks.write_bytes(bulk)
    lines = start(1.5)+['mod_remove 1', 'frames 2', 'mod_remove 0', 'frames 2',
        'mods_conflicts', 'wait_mods', 'frames 2', 'assert_state mods_count 0',
        'assert_state mods_conflicts 0', 'assert_state mods_unreported_choices 401']
    run(work/'empty-order', '800x600', lines)
    empty = json.loads(cli('empty-report', 'mods', 'conflicts', root, '--json'))
    assert empty['summary']['sources'] == 0
    assert all(value == 0 for value in empty['summary'].values())
    for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
        case = work/('bulk-'+size)
        lines = start(scale)+['assert_state mods_count 0', 'assert_state mods_conflicts 0',
                             'assert_state mods_unreported_choices 401']
        lines += reveal('input_mod_conflict_search')+['click input_mod_conflict_search',
            'input_text UnRePoRtEd400', 'frames 3', 'assert_state mods_unreported_matching 1']
        lines += reveal('mod_clear_unreported_400')+['assert_widget mod_clear_unreported_400',
            'mouse_move viewport', 'clear_toasts', f'screenshot {(case/"last-choice.png").as_posix()}']
        run(case, size, lines)
        assert picks.read_bytes() == bulk
    # Only the explicitly changed mod-order file may differ from its initial state.
    assert all(hashlib.sha256(p.read_bytes()).hexdigest() == digest for p, digest in hashes.items()
               if p != root/'forge_mods.json')
    retained = {p for source in (root, other, *sources) for p in source.rglob('*') if p.is_file()
                and p not in (picks, root/'forge_mods.json')}
    assert retained == set(hashes)-{root/'forge_mods.json'}
    print('Individual clearing, expected next-build change, empty-order report and search beyond 400: PASS', flush=True)


if __name__ == '__main__':
    main()
