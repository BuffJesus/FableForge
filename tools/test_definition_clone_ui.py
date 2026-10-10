#!/usr/bin/env python3
"""Exercise clone drafts, exact donor preservation, stale guards and paired Restore."""
import argparse
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import time

from test_definition_clone import fixture
from test_definition_edit_ui import click
from test_fmp_links import read_bank, crc0
from test_texture_lookup import texture_bank

MODES = ('save', 'restore', 'numeric', 'unnamed', 'indexed-duplicate', 'folder',
         'folder-textures', 'folder-bad-textures', 'folder-texture-directory', 'pack',
         'empty', 'duplicate', 'cancel', 'confirmation-cancel', 'destination',
         'stale-bank', 'stale-names', 'pool-conflict', 'bounds', 'install-switch', 'frontend', 'script')


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gui', type=Path, default=repo/'build/FableForge.exe')
    parser.add_argument('--forge', type=Path, default=repo/'build/forge.exe')
    parser.add_argument('--cwd', type=Path, default=repo)
    parser.add_argument('--work-root', type=Path, default=repo/'build')
    parser.add_argument('--only', nargs='+', choices=MODES)
    args = parser.parse_args()
    args.work_root.resolve().mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='definition-clone-ui-', dir=args.work_root.resolve()))
    print('Evidence retained at', work, flush=True)
    cases = 0
    for size, scale in (('1440x900', 1), ('800x600', 1.5)):
        for mode in args.only or MODES:
            case = work/f'{size}-{mode}'
            root = case/'install \u03a9'
            folder = fixture(root)
            levels = root/'data/Levels/FinalAlbion'
            levels.mkdir(parents=True)
            (levels/'unused.lev').write_bytes(b'')
            textures = root/'data/graphics/pc/textures.big'
            texture_bank(textures, [(1, 'Existing', 20)])
            texture_bytes = textures.read_bytes()
            bank_name = mode+'.bin' if mode in ('frontend', 'script') else 'game.bin'
            game_bytes = (folder/'game.bin').read_bytes()
            if bank_name != 'game.bin':
                shutil.copyfile(folder/'game.bin', folder/bank_name)
            source_bank, source_names = folder/bank_name, folder/'names.bin'
            original_bank, original_names = source_bank.read_bytes(), source_names.read_bytes()
            original_records = read_bank(root, bank_name)
            destination = case/'output \u6f22'
            folder_mode = mode.startswith('folder')
            output_root = destination if folder_mode or mode in ('pack', 'pool-conflict') else root
            bank = output_root/'data/CompiledDefs'/bank_name
            names = bank.parent/'names.bin'
            pack_bytes = None
            if mode == 'pack':
                destination.mkdir(parents=True)
                pack_bytes = b'{"version":1,"name":"Cloned"}\n'
                (destination/'forge_pack.json').write_bytes(pack_bytes)
            if mode == 'pool-conflict':
                names.parent.mkdir(parents=True)
                names.write_bytes(b'INCOMPATIBLE POOL')
            output_textures = destination/'data/graphics/pc/textures.big'
            output_texture_bytes = None
            if mode == 'folder-textures':
                texture_bank(output_textures, [(2, 'OutputOnly', 20)])
                output_texture_bytes = output_textures.read_bytes()
            elif mode == 'folder-bad-textures':
                output_textures.parent.mkdir(parents=True)
                output_texture_bytes = b'INVALID OUTPUT TEXTURE BANK'
                output_textures.write_bytes(output_texture_bytes)
            elif mode == 'folder-texture-directory':
                output_textures.mkdir(parents=True)
            donor = 1 if mode == 'numeric' else 2 if mode == 'unnamed' else 3 if mode == 'indexed-duplicate' else 0
            selector = '1' if mode == 'numeric' else str(donor)
            new_name = 'GUI_CLONE_CHECK'
            accepted = folder_mode or mode in ('save', 'restore', 'numeric', 'unnamed', 'indexed-duplicate', 'pack', 'frontend', 'script')
            lines = ['wait_maps', 'wait_ready', f'set uiscale {scale}']
            if folder_mode or mode == 'pool-conflict':
                lines += [f'set saveroot {destination.as_posix()}']
            elif mode == 'pack':
                lines += [f'pack_dest {destination.as_posix()}']
            lines += ['assets_tab 6', 'wait_definitions', 'frames 3']
            if bank_name != 'game.bin':
                lines += ['definition_bank '+bank_name, 'wait_definitions', 'frames 3']
            lines += [f'definition_select {selector}', 'frames 3']
            lines += click('btn_definition_clone') + ['assert_state definition_editing 1',
                'assert_state definition_cloning 1', 'definition_bank_refused '+('script.bin' if bank_name == 'frontend.bin' else 'frontend.bin'),
                'definition_select_refused 0', 'definition_clone_refused']
            if mode != 'empty':
                value = 'DONOR' if mode == 'duplicate' else new_name
                lines += click('input_definition_edit_value') + ['input_text '+value, 'frames 3']
            barrier = case/'draft.png'
            if mode in ('stale-bank', 'stale-names'):
                lines += [f'screenshot {barrier.as_posix()}', 'frames 300']
            if mode == 'destination':
                lines += [f'set saveroot {destination.as_posix()}', 'wait_definitions', 'frames 3']
            if mode == 'bounds':
                lines += ['definition_value_refused '+('X'*128), 'definition_value_refused BAD\u00e9']
            if mode == 'install-switch':
                fixture(case/'other-install')
                lines += [f'scan_install {(case/"other-install").as_posix()}', 'frames 3',
                    'assert_log install: save or discard the current edits before changing folders',
                    'assert_state definition_cloning 1']
            if mode in ('cancel', 'bounds', 'install-switch'):
                lines += click('btn_definition_edit_cancel') + ['assert_state definition_editing 0',
                                                              'assert_state definition_cloning 0']
            elif mode == 'confirmation-cancel':
                lines += click('btn_definition_save') + ['key_down Escape', 'frames 2',
                    'key_up Escape', 'frames 3', 'assert_state definition_editing 1',
                    'assert_state definition_cloning 1', 'assert_widget btn_definition_save', 'definition_cancel']
            elif accepted:
                if mode == 'save':
                    lines += ['close', 'frames 3', 'assert_state close_prompt 1',
                        'click btn_unsaved_cancel', 'frames 3', 'assert_state definition_cloning 1',
                        'close', 'frames 3', 'click btn_unsaved_save', 'frames 3',
                        'assert_state close_prompt 0', 'assert_state assets_tab 6']
                lines += click('btn_definition_save') + [f'screenshot {(case/"confirmation.png").as_posix()}']
                lines += click('btn_definition_save_confirm') + ['wait_ready', 'wait_definitions', 'frames 3',
                    'assert_state definition_editing 0', 'assert_state definition_cloning 0',
                    'assert_state definition_selected 5', f'definition_select {new_name}',
                    f'screenshot {(case/"created.png").as_posix()}']
                if folder_mode:
                    ready = int(mode not in ('folder-bad-textures', 'folder-texture-directory'))
                    lines += [f'assert_state context_ready {ready}']
                    if not ready:
                        lines += ['assert_log Textures unavailable:']
                if mode == 'restore':
                    lines += ['restore_all', 'wait_ready', 'wait_definitions', 'frames 3',
                              'definition_select_refused '+new_name, 'definition_select 0']
            else:
                lines += click('btn_definition_save') + click('btn_definition_save_confirm') + [
                    'assert_state definition_edit_failed 1', 'assert_state definition_editing 1',
                    'assert_state definition_cloning 1', 'definition_cancel']
            lines += ['quit']
            script = case/'clone.txt'
            script.write_text('\n'.join(lines)+'\n', encoding='utf-8')
            env = dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1',
                       APPDATA=str(case/'profile'), LOCALAPPDATA=str(case/'local'))
            expected_bank, expected_names = original_bank, original_names
            with (case/'output.log').open('w', encoding='utf-8') as output:
                process = subprocess.Popen([str(args.gui.resolve()), '--install', str(root),
                    '--size', size, '--auto', str(script)], cwd=args.cwd.resolve(), env=env,
                    stdout=output, stderr=subprocess.STDOUT)
                try:
                    if mode in ('stale-bank', 'stale-names'):
                        deadline = time.monotonic()+60
                        while not barrier.exists() and process.poll() is None and time.monotonic()<deadline:
                            time.sleep(.02)
                        assert barrier.exists(), 'draft barrier not reached'
                        if mode == 'stale-bank':
                            expected_bank = bytes([original_bank[0]^1])+original_bank[1:]
                            source_bank.write_bytes(expected_bank)
                        else:
                            expected_names = bytes([original_names[0]^1])+original_names[1:]
                            source_names.write_bytes(expected_names)
                    result = process.wait(timeout=90)
                finally:
                    if process.poll() is None:
                        process.terminate()
                        process.wait(timeout=10)
            trace = Path(str(script)+'.log').read_text(encoding='utf-8')
            assert result == 0 and 'RESULT PASS' in trace and 'FAIL ' not in trace, trace[-7000:]
            assert textures.read_bytes() == texture_bytes
            if output_texture_bytes is not None:
                assert output_textures.read_bytes() == output_texture_bytes
            elif mode == 'folder-texture-directory':
                assert output_textures.is_dir()
            elif folder_mode:
                assert not output_textures.exists(), 'preview fallback must not copy textures'
            if accepted and mode != 'restore':
                after = read_bank(output_root, bank_name)
                assert len(after) == 6 and after[:5] == original_records
                assert after[5] == dict(original_records[donor], name=new_name)
                raw = bank.read_bytes()
                assert raw[:9] == original_bank[:9] and raw[13:73] == original_bank[13:73]
                assert struct.unpack_from('<I', raw, 13+12*5+8)[0] == sum(
                    entry['definition'] == original_records[donor]['definition'] for entry in original_records)
                pool = names.read_bytes()
                assert pool[:8] == original_names[:8] and pool[16:len(original_names)] == original_names[16:]
                assert pool[len(original_names):] == struct.pack('<I', crc0(new_name))+new_name.encode()+b'\0'
                if output_root == root:
                    assert Path(str(bank)+'.forge-orig').read_bytes() == original_bank
                    assert Path(str(names)+'.forge-orig').read_bytes() == original_names
                else:
                    assert source_bank.read_bytes() == original_bank and source_names.read_bytes() == original_names
                    for path in (bank, names):
                        assert Path(str(path)+'.forge-created').is_file()
                    restored = subprocess.run([str(args.forge.resolve()), 'restore', '--install', str(output_root)],
                        cwd=args.cwd.resolve(), capture_output=True, text=True, timeout=60)
                    assert restored.returncode == 0, restored.stdout+restored.stderr
                    for path in (bank, names):
                        assert not path.exists() and not Path(str(path)+'.forge-created').exists()
            else:
                assert source_bank.read_bytes() == expected_bank and source_names.read_bytes() == expected_names
                if mode != 'restore':
                    assert not list(root.rglob('*.forge-orig'))
                if mode == 'pool-conflict':
                    assert names.read_bytes() == b'INCOMPATIBLE POOL' and not bank.exists()
            if pack_bytes is not None:
                assert (destination/'forge_pack.json').read_bytes() == pack_bytes
            if bank_name != 'game.bin':
                assert (folder/'game.bin').read_bytes() == game_bytes
                assert read_bank(root) == original_records
            cases += 1
            print(size, mode, 'clone draft, preservation and recovery: PASS', flush=True)
    (work/'result.json').write_text(json.dumps({'passed': True, 'cases': cases})+'\n', encoding='utf-8')


if __name__ == '__main__':
    main()
