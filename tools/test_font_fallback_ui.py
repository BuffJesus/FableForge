#!/usr/bin/env python3
"""Check optional system glyphs, font roles and atlas rebuilds without game writes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

from test_definition_edit_ui import click
from test_mod_field_details import write_bank
from test_texture_lookup import texture_bank


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gui', type=Path, default=repo/'build/FableForge.exe')
    parser.add_argument('--cwd', type=Path, default=repo)
    parser.add_argument('--work-root', type=Path, default=repo/'build')
    parser.add_argument('--require-wide', action='store_true', help='Require both CJK and emoji system fonts')
    args = parser.parse_args()
    fonts = Path(os.environ.get('WINDIR', 'C:/Windows'))/'Fonts'
    cjk = next((fonts/name for name in ('msyh.ttc', 'msgothic.ttc', 'YuGothR.ttc', 'malgun.ttf')
                if (fonts/name).is_file()), None)
    emoji = fonts/'seguiemj.ttf'
    if args.require_wide:
        assert cjk and emoji.is_file(), 'required optional language/emoji fonts are absent'
    points = ['0041']
    skipped = []
    if (fonts/'segoeui.ttf').is_file() and (fonts/'seguisb.ttf').is_file():
        points += ['00e9', '03a9']
    else:
        skipped.append('Latin accent/Greek primary-font coverage')
    if cjk:
        points += ['6f22']
    else:
        skipped.append('CJK: no selected optional language font')
    if emoji.is_file():
        points += ['1f642', '1f680']
    else:
        skipped.append('supplementary emoji: no Segoe UI Emoji')
    args.work_root.resolve().mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='font-fallback-ui-', dir=args.work_root.resolve()))
    print('Evidence retained at', work, flush=True)
    fields = [field['name'] for field in json.loads((repo/'docs/re_reference/def_schema.json').read_text())['UI']['fields']]
    cases = 0
    for size, scale, mode in ((size, scale, mode) for size, scale in (('1440x900', 1), ('800x600', 1.5))
                              for mode in ('system', 'no-fonts', 'primary-only')):
        case = work/(size+'-'+mode)
        root = case/'install'
        values = [bytes(4)]*len(fields)
        values[3] = 'caf\u00e9 \u03a9 \u6f22 \U0001f642 \U0001f680'.encode('utf-16-le')+b'\0\0'
        write_bank(root, values, definition='UI', record='UNICODE', fields=fields)
        levels = root/'data/Levels/FinalAlbion'
        levels.mkdir(parents=True)
        (levels/'unused.lev').write_bytes(b'')
        texture_bank(root/'data/graphics/pc/textures.big', [(1, 'Existing', 20)])
        original = {path: path.read_bytes() for path in root.rglob('*') if path.is_file()}
        override = None
        if mode != 'system':
            override = case/'diagnostic fonts'
            override.mkdir()
            if mode == 'primary-only':
                for name in ('segoeui.ttf', 'seguisb.ttf'):
                    if (fonts/name).is_file():
                        shutil.copyfile(fonts/name, override/name)
            current_points = ['0041']
        else:
            current_points = points
        lines = ['wait_maps', 'wait_ready']
        # Original scale, a second rebuild and return to the intended screenshot
        # scale exercise source-buffer lifetime and every font role repeatedly.
        for current_scale in (scale, 1.25, scale):
            lines += [f'set uiscale {current_scale}', 'frames 5']
            for role in ('body', 'bold', 'title', 'small'):
                lines += [f'assert_font_glyph {role} {point}' for point in current_points]
                if mode != 'system':
                    lines += [f'assert_font_glyph_missing {role} {point}' for point in ('6f22', '1f642')]
        lines += ['assets_tab 6', 'wait_definitions', 'definition_select UNICODE', 'frames 3']
        lines += click('btn_definition_edit_3')
        lines += ['reveal input_definition_edit_value', 'frames 5']
        lines += [f'screenshot {(case/"unicode-draft.png").as_posix()}', 'definition_cancel', 'quit']
        script = case/'fonts.txt'
        script.write_text('\n'.join(lines)+'\n', encoding='utf-8')
        env = dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1', APPDATA=str(case/'profile'), LOCALAPPDATA=str(case/'local'))
        env.pop('FABLEFORGE_AUTOMATION_FONT_DIR', None)
        if override:
            env['FABLEFORGE_AUTOMATION_FONT_DIR'] = str(override)
        result = subprocess.run([str(args.gui.resolve()), '--install', str(root), '--size', size, '--auto', str(script)],
            cwd=args.cwd.resolve(), env=env,
            capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=120)
        (case/'output.log').write_text(result.stdout+result.stderr, encoding='utf-8')
        trace = Path(str(script)+'.log').read_text(encoding='utf-8')
        assert result.returncode == 0 and 'RESULT PASS' in trace and 'FAIL ' not in trace, trace[-8000:]
        assert all(path.read_bytes() == data for path, data in original.items())
        assert not list(root.rglob('*.forge-orig')) and not list(root.rglob('*.forge-created'))
        cases += 1
        print(size, mode, 'font roles, Unicode glyphs and repeated atlas rebuilds: PASS', flush=True)
    for item in skipped:
        print('SKIP:', item, flush=True)
    with args.gui.resolve().open('rb') as stream:
        gui_hash = hashlib.file_digest(stream, 'sha256').hexdigest()
    (work/'result.json').write_text(json.dumps({'passed': True, 'cases': cases,
        'roles': 4, 'atlas_scales_per_case': 3, 'glyphs': points, 'skipped_glyph_groups': skipped,
        'gui_sha256': gui_hash, 'cjk_font': str(cjk) if cjk else None,
        'emoji_font': str(emoji) if emoji.is_file() else None}, indent=2)+'\n', encoding='utf-8')


if __name__ == '__main__':
    main()
