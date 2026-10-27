#!/usr/bin/env python3
"""Verify a real GUI Unicode field delta and Restore with read-only retail inputs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile

from test_definition_edit_ui import click
from test_fmp_links import read_bank
from test_mod_field_details import tag


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--gui', type=Path, default=repo/'build/FableForge.exe')
    parser.add_argument('--forge', type=Path, default=repo/'build/forge.exe')
    parser.add_argument('--cwd', type=Path, default=repo)
    parser.add_argument('--work-root', type=Path, default=repo/'build')
    parser.add_argument('--require-glyphs', action='store_true', help='Require installed CJK/emoji glyph coverage')
    args = parser.parse_args()
    native = args.install.resolve()
    folder = native/'data/CompiledDefs'
    inputs = sorted(folder.glob('*.bin'))
    hashes = {str(path): digest(path) for path in inputs}
    before = read_bank(native)
    selected = [i for i, record in enumerate(before)
                if record['name'] == 'PC_PHOTOJOURNAL_CAPTURE_DESCRIPTION']
    assert len(selected) == 1
    index = selected[0]
    record = before[index]
    field_tag = struct.pack('<I', tag('TextValue'))
    assert record['data'].count(field_tag) == 1
    start = record['data'].index(field_tag)+4
    end = start
    while end+2 <= len(record['data']) and record['data'][end:end+2] != b'\0\0':
        end += 2
    assert end+2 <= len(record['data'])
    end += 2
    original_bank = (folder/'game.bin').read_bytes()
    original_names = (folder/'names.bin').read_bytes()
    text = 'caf\u00e9 \u03a9 \u6f22 \U0001f642'
    expected = dict(record, data=record['data'][:start]+text.encode('utf-16-le')+b'\0\0'+record['data'][end:])
    args.work_root.resolve().mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='retail-definition-wide-ui-', dir=args.work_root.resolve()))
    print('Evidence retained at', work, flush=True)
    for size, scale in (('1440x900', 1), ('800x600', 1.5)):
        case = work/size
        case.mkdir()
        output = case/'output'
        lines = ['wait_maps', 'wait_ready', f'set uiscale {scale}',
                 f'set saveroot {output.as_posix()}', 'assets_tab 6', 'wait_definitions',
                 'definition_select '+record['name'], 'frames 3']
        if args.require_glyphs:
            for role in ('body', 'bold', 'title', 'small'):
                lines += [f'assert_font_glyph {role} {point}' for point in ('00e9', '03a9', '6f22', '1f642')]
        lines += click('btn_definition_edit_3')+click('input_definition_edit_value')
        lines += ['key_down Ctrl', 'key_down A', 'key_up A', 'key_up Ctrl',
                  'input_text '+text, 'frames 3', f'screenshot {(case/"draft.png").as_posix()}']
        lines += click('btn_definition_save')+click('btn_definition_save_confirm')
        lines += ['wait_ready', 'wait_definitions', 'frames 3',
                  'assert_state definition_editing 0', 'assert_state context_ready 1',
                  f'assert_state definition_selected {index}',
                  f'screenshot {(case/"saved.png").as_posix()}', 'quit']
        script = case/'wide.txt'
        script.write_text('\n'.join(lines)+'\n', encoding='utf-8')
        try:
            result = subprocess.run([str(args.gui.resolve()), '--install', str(native),
                '--size', size, '--auto', str(script)], cwd=args.cwd.resolve(),
                env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1',
                         APPDATA=str(case/'profile'), LOCALAPPDATA=str(case/'local')),
                capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=120)
            (case/'output.log').write_text(result.stdout+result.stderr, encoding='utf-8')
            trace = Path(str(script)+'.log').read_text(encoding='utf-8')
            assert result.returncode == 0 and 'RESULT PASS' in trace and 'FAIL ' not in trace, trace[-6000:]
            after = read_bank(output)
            assert len(after) == len(before) and after[index] == expected
            assert after[:index] == before[:index] and after[index+1:] == before[index+1:]
            bank = output/'data/CompiledDefs/game.bin'
            names = bank.parent/'names.bin'
            raw = bank.read_bytes()
            assert raw[:13+12*len(before)] == original_bank[:13+12*len(before)]
            assert names.read_bytes() == original_names
            for path in (bank, names):
                assert Path(str(path)+'.forge-created').is_file()
            restore = subprocess.run([str(args.forge.resolve()), 'restore', '--install', str(output)],
                cwd=args.cwd.resolve(), capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=120)
            (case/'restore.log').write_text(restore.stdout+restore.stderr, encoding='utf-8')
            assert restore.returncode == 0, restore.stdout+restore.stderr
            for path in (bank, names):
                assert not path.exists() and not Path(str(path)+'.forge-created').exists()
        finally:
            assert {str(path): digest(path) for path in inputs} == hashes, 'native input changed'
        print(size, len(before), 'identities, exact UTF16 delta, source preservation and Restore: PASS', flush=True)
    (work/'result.json').write_text(json.dumps({'passed': True, 'cases': 2,
        'preserved_records': len(before), 'selected_index': index,
        'gui_sha256': digest(args.gui.resolve()), 'forge_sha256': digest(args.forge.resolve()),
        'native_hashes_unchanged': hashes}, indent=2)+'\n', encoding='utf-8')


if __name__ == '__main__':
    main()
