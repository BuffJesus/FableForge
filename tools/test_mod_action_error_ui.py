#!/usr/bin/env python3
"""Mods failures remain visible beside actions, preserve files and clear on retry."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time
from PIL import Image
from test_install_switch_ui import make_install


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def snapshot(root):
    return {p.relative_to(root).as_posix(): digest(p) for p in root.rglob('*') if p.is_file()}


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--gui', type=Path, default=repo/'build/FableForge.exe')
    parser.add_argument('--mode', choices=['redeploy', 'report', 'missing', 'fmp', 'unnamed', 'sibling', 'record'], default='redeploy')
    parser.add_argument('--fmp', type=Path, help='Text-link fixture for a scratch baseline without text.big')
    args = parser.parse_args()
    if args.mode == 'fmp' and not args.fmp: parser.error('--mode fmp requires --fmp')
    deployment_case = args.mode in ('redeploy', 'fmp', 'unnamed', 'sibling', 'record')
    fmp_hash = digest(args.fmp) if args.fmp else None
    gui = args.gui.resolve()
    work = Path(tempfile.mkdtemp(prefix='mod-action-error-', dir=repo/'build')).resolve()
    print('Evidence retained at', work, flush=True)
    cwd = work/'unrelated working directory'
    cwd.mkdir()
    sources = [args.install/'data/CompiledDefs'/name for name in ('game.bin', 'names.bin')]
    source_hashes = {p: digest(p) for p in sources}
    helper = None
    if args.mode == 'report':
        source = work/'invalid-report.cpp'
        source.write_text('#include <cstdio>\n#include <cstdlib>\nint main() { const char* schema = std::getenv("FORGE_TEST_REPORT_SCHEMA"); std::puts(schema && *schema ? "{}" : "not a JSON report"); return 0; }\n', encoding='utf-8')
        cache = (repo/'build/CMakeCache.txt').read_text(encoding='utf-8')
        compiler = re.search(r'^CMAKE_CXX_COMPILER:(?:FILEPATH|STRING)=(.+)$', cache, re.M).group(1)
        helper = work/'invalid-report.exe'
        subprocess.run([compiler, '-static', str(source), '-o', str(helper)],
            cwd=cwd, check=True, capture_output=True, timeout=60)
    def button(widget):
        return [f'reveal {widget}', 'frames 3', f'reveal {widget}', 'frames 3',
                f'click {widget}', 'wait_mods', 'wait_ready', 'frames 3']
    for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
        case = work/size
        root = case/'install'
        make_install(root, 'ModAction', 4)
        for source in sources:
            target = root/source.relative_to(args.install)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
        other = case/'other-install'
        make_install(other, 'OtherAction', 4)
        other_before = snapshot(other)
        selected_gui = gui
        if not deployment_case:
            app = case/'application'
            app.mkdir()
            shutil.copyfile(gui, app/'FableForge.exe')
            if helper:
                shutil.copyfile(helper, app/'forge-tools.exe')
            schema = app/'docs/re_reference/def_schema.json'
            schema.parent.mkdir(parents=True)
            shutil.copyfile(repo/'docs/re_reference/def_schema.json', schema)
            selected_gui = app/'FableForge.exe'
        payload = Path('data/Misc/action.txt')
        if deployment_case:
            (root/payload).parent.mkdir(parents=True)
            (root/payload).write_bytes(b'original owned file')
            pack = case/'SmallFilePack'
            (pack/payload).parent.mkdir(parents=True)
            (pack/payload).write_bytes(b'deployed owned file')
            (pack/'forge_pack.json').write_text(json.dumps({'version': 1, 'name': 'SmallFilePack',
                'models': [], 'groundThemes': []}), encoding='utf-8')
            added = subprocess.run([str(gui.with_name('forge-tools.exe')), 'mods', 'add', str(root),
                str(pack), '--name', 'SmallFilePack'], cwd=cwd, capture_output=True, timeout=45)
            assert added.returncode == 0, (added.stdout+added.stderr).decode('utf-8', errors='replace')
            bad = case/('ChangedSiblingBanks' if args.mode == 'sibling' else
                        'UnnamedComponents' if args.mode == 'unnamed' else
                        'UntypedRecords.fmp' if args.mode == 'record' else
                        'UnsupportedLinks.fmp' if args.mode == 'fmp' else 'game.bin.patch')
            if args.mode == 'sibling':
                (bad/'data/CompiledDefs').mkdir(parents=True)
                for source in sources:
                    shutil.copyfile(source, bad/source.relative_to(args.install))
                shutil.copyfile(root/'data/CompiledDefs/game.bin', bad/'data/CompiledDefs/script.bin')
            elif args.mode == 'unnamed':
                from test_fmp_links import read_bank, rewrite_bank
                entries = read_bank(root)
                index = next(i for i, entry in enumerate(entries) if not entry['name'])
                entries[index] = dict(entries[index], data=entries[index]['data']+b'unsupported-unnamed-edit')
                rewrite_bank(root, bad, entries)
            elif args.mode == 'record':
                from test_fmp_record_refusal import write_fmp
                from test_fmp_links import read_bank
                entry = next(entry for entry in read_bank(root) if entry['name'])
                write_fmp(bad, [(entry['name'], entry['definition'], entry['data']),
                                ('NEW_UNTYPED_RECORD', '-', b'unsupported-untyped-record')])
            elif args.mode == 'fmp': shutil.copyfile(args.fmp, bad)
            else: bad.write_bytes(b'not a BSDIFF patch'+b'x'*64)
            bad_before = snapshot(bad) if bad.is_dir() else digest(bad)
            pack_before = snapshot(pack)
        before = snapshot(root)
        failed = case/'failed.png'
        script = case/'actions.txt'
        lines = ['wait_maps', 'wait_ready', f'set uiscale {scale}', 'mods_tab 1', 'frames 3']
        if deployment_case:
            lines += button('btn_mods_deploy')+['assert_log mods deploy: done',
                f'mod_add {bad.as_posix()} BrokenPatch', 'frames 3']
            lines += button('btn_mods_deploy')+['assert_log mods deploy: FAILED',
                'assert_log changed sibling definition banks' if args.mode == 'sibling' else
                'assert_log has no definition type' if args.mode == 'record' else
                'assert_log changed or new unnamed definition entries' if args.mode == 'unnamed' else
                'assert_log FMP text links: baseline English/text.big is missing' if args.mode == 'fmp' else 'assert_log bad magic']
            if args.mode in ('unnamed', 'sibling', 'record'):
                lines += button('btn_mods_conflicts')+['assert_log mods conflicts: FAILED',
                    'assert_log changed sibling definition banks' if args.mode == 'sibling' else
                    'assert_log has no definition type' if args.mode == 'record' else
                    'assert_log changed or new unnamed definition entries']
        else:
            lines += button('btn_mods_conflicts')
            if args.mode == 'report': lines += ['assert_log mods conflicts: FAILED']
        lines += ['assert_state mod_action_failed 1', 'mods_tab 0', 'frames 3',
            'mods_tab 1', 'assert_state mod_action_failed 1',
            f'scan_install {(root/"Data").as_posix()}', 'wait_maps', 'wait_ready',
            'assert_state mod_action_failed 1', 'clear_toasts',
            'reveal mods_action_error', 'frames 3', 'reveal mods_action_error', 'frames 3',
            'assert_widget mods_action_error', 'dump_widget mods_action_error', 'dump_log',
            'mouse_move 20 20', 'frames 3',
            f'screenshot {failed.as_posix()}', 'frames 300']
        if scale > 1:
            lines += [f'set saveroot {other.as_posix()}', 'assert_state mod_action_failed 0',
                      f'set saveroot {root.as_posix()}', 'assert_state mod_action_failed 0']
        if deployment_case:
            lines += ['mod_remove BrokenPatch', 'frames 3']+button('btn_mods_deploy')
            lines += ['assert_log mods deploy: done', 'assert_state mod_action_failed 0']
            lines += button('btn_mods_undeploy')+['assert_log mods undeploy: done']
        else:
            lines += button('btn_mods_conflicts')+['assert_log mods conflicts: 0 conflict(s) across 0 mod(s)']
        lines += ['assert_state mod_action_failed 0', f'screenshot {(case/"retried.png").as_posix()}', 'quit']
        script.write_text('\n'.join(lines)+'\n', encoding='utf-8')
        with (case/'output.log').open('w', encoding='utf-8') as output:
            process = subprocess.Popen([str(selected_gui), '--install', str(root), '--size', size,
                '--auto', str(script)], cwd=cwd, stdout=output, stderr=subprocess.STDOUT,
                env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1',
                    FORGE_TEST_REPORT_SCHEMA='1' if scale > 1 else '',
                    APPDATA=str(case/'appdata'), LOCALAPPDATA=str(case/'localappdata')))
            try:
                deadline = time.monotonic()+60
                while not failed.exists() and process.poll() is None and time.monotonic() < deadline:
                    time.sleep(0.01)
                assert failed.exists(), 'GUI did not reach failure checkpoint'
                after = snapshot(root)
                if deployment_case:
                    assert {k:v for k,v in after.items() if k != 'forge_mods.json'} == \
                           {k:v for k,v in before.items() if k != 'forge_mods.json'}
                    assert len(json.loads((root/'forge_mods.json').read_text(encoding='utf-8'))['mods']) == 2
                else:
                    assert after == before
                    shutil.copyfile(gui.with_name('forge-tools.exe'), selected_gui.with_name('forge-tools.exe'))
                rc = process.wait(timeout=90)
            finally:
                if process.poll() is None:
                    process.terminate(); process.wait(timeout=10)
        log = Path(str(script)+'.log').read_text(encoding='utf-8')
        assert snapshot(root) == before and snapshot(other) == other_before
        if deployment_case:
            assert snapshot(pack) == pack_before
            assert (snapshot(bad) if bad.is_dir() else digest(bad)) == bad_before
        assert rc == 0 and 'RESULT PASS' in log, log[-4500:]
        bounds = re.search(r'widget_rect mods_action_error ([\d.-]+) ([\d.-]+) ([\d.-]+) ([\d.-]+)', log)
        assert bounds
        left, top, right, bottom = map(float, bounds.groups())
        with Image.open(failed) as picture:
            assert 0 <= left < right <= picture.width and 0 <= top < bottom <= picture.height
        print(size, args.mode, 'persistent error, preserved files and same-process retry: PASS', flush=True)
    assert {p: digest(p) for p in sources} == source_hashes
    if args.fmp: assert digest(args.fmp) == fmp_hash
    print('Mods action error lifecycle and recovery: PASS')


if __name__ == '__main__':
    main()
