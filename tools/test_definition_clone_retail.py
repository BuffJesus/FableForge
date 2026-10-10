#!/usr/bin/env python3
"""Check donor cloning and Restore on copies of an explicitly selected retail bank."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

from test_fmp_links import read_bank, crc0


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    parser.add_argument('--forge', type=Path, default=repo/'build/forge.exe')
    parser.add_argument('--gui', type=Path, help='Drive the GUI clone controls instead of the CLI')
    parser.add_argument('--work-root', type=Path, default=repo/'build')
    parser.add_argument('--donor', default='OBJECT_QUEST_CARD_TEMPLATE')
    args = parser.parse_args()
    native = args.install.resolve()/'data/CompiledDefs'
    inputs = sorted(native.glob('*.bin'))
    assert (native/'game.bin').is_file() and (native/'names.bin').is_file()
    hashes = {str(path): digest(path) for path in inputs}
    args.work_root.resolve().mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='retail-definition-clone-', dir=args.work_root.resolve()))
    print('Evidence retained at', work, flush=True)
    source = work/'source'
    folder = source/'data/CompiledDefs'
    folder.mkdir(parents=True)
    for name in ('game.bin', 'names.bin'):
        shutil.copyfile(native/name, folder/name)
    before = read_bank(source)
    donors = [i for i, record in enumerate(before) if record['name'] == args.donor]
    assert len(donors) == 1, 'retail donor must identify one record'
    donor = donors[0]
    new_name = 'FORGE_RC2_RETAIL_CLONE_CHECK'
    assert all(record['name'] != new_name for record in before)
    original_bank = (folder/'game.bin').read_bytes()
    original_names = (folder/'names.bin').read_bytes()
    output = work/'output'
    bank = output/'data/CompiledDefs/game.bin'
    names = bank.parent/'names.bin'

    def run(label, command, env=None):
        result = subprocess.run(list(map(str, command)), cwd=work, capture_output=True,
                                text=True, encoding='utf-8', errors='replace', timeout=120, env=env)
        (work/(label+'.log')).write_text(result.stdout+result.stderr, encoding='utf-8')
        assert result.returncode == 0, result.stdout+result.stderr
        return result

    try:
        if args.gui:
            from test_definition_edit_ui import click
            # The selected native install supplies read-only map/asset inputs;
            # every authored file goes to this new owned output directory.
            script = work/'clone.txt'
            lines = ['wait_maps', 'wait_ready', f'set saveroot {output.as_posix()}',
                     'assets_tab 6', 'wait_definitions', 'frames 3',
                     'definition_select '+args.donor, 'frames 3']
            lines += click('btn_definition_clone')+['assert_state definition_cloning 1']
            lines += click('input_definition_edit_value')+['input_text '+new_name, 'frames 3']
            lines += click('btn_definition_save')+click('btn_definition_save_confirm')
            lines += ['wait_ready', 'wait_definitions', 'frames 3',
                      'assert_state definition_cloning 0',
                      'assert_state context_ready 1',
                      f'assert_state definition_selected {len(before)}',
                      'definition_select '+new_name,
                      f'screenshot {(work/"created.png").as_posix()}', 'quit']
            script.write_text('\n'.join(lines)+'\n', encoding='utf-8')
            run('clone-ui', [args.gui.resolve(), '--install', args.install.resolve(),
                '--size', '1440x900', '--auto', script], env=dict(os.environ,
                FABLEFORGE_AUTOMATION_HIDDEN='1', APPDATA=str(work/'profile'),
                LOCALAPPDATA=str(work/'local')))
            trace = Path(str(script)+'.log').read_text(encoding='utf-8')
            assert 'RESULT PASS' in trace and 'FAIL ' not in trace, trace[-7000:]
        else:
            result = run('clone', [args.tool.resolve(), 'defs', 'clone', folder/'game.bin',
                                  args.donor, new_name, '--out', bank, '--json'])
            report = json.loads(result.stdout)
            assert report['donor_index'] == donor and report['entry_index'] == len(before)
        after = read_bank(output)
        assert after[:-1] == before and len(after) == len(before)+1
        assert after[-1] == dict(before[donor], name=new_name)
        raw = bank.read_bytes()
        assert raw[:9] == original_bank[:9]
        assert raw[13:13+12*len(before)] == original_bank[13:13+12*len(before)]
        definition, name_offset, ordinal = struct.unpack_from('<III', raw, 13+12*len(before))
        assert definition == struct.unpack_from('<I', original_bank, 13+12*donor)[0]
        assert ordinal == sum(record['definition'] == before[donor]['definition'] for record in before)
        pool = names.read_bytes()
        assert pool[:8] == original_names[:8] and pool[16:len(original_names)] == original_names[16:]
        assert struct.unpack_from('<I', pool, 8)[0] == struct.unpack_from('<I', original_names, 8)[0]+1
        assert name_offset == len(original_names)-16
        assert pool[len(original_names):] == struct.pack('<I', crc0(new_name))+new_name.encode('ascii')+b'\0'
        for path in (bank, names):
            assert Path(str(path)+'.forge-created').is_file()
        run('restore', [args.forge.resolve(), 'restore', '--install', output])
        for path in (bank, names):
            assert not path.exists() and not Path(str(path)+'.forge-created').exists()
        assert (folder/'game.bin').read_bytes() == original_bank
        assert (folder/'names.bin').read_bytes() == original_names
    finally:
        assert {str(path): digest(path) for path in inputs} == hashes, 'native input changed'
    (work/'result.json').write_text(json.dumps({
        'passed': True, 'preserved_records': len(before), 'donor_index': donor,
        'new_index': len(before), 'native_hashes_unchanged': hashes,
        'tool_sha256': digest(args.tool.resolve()), 'forge_sha256': digest(args.forge.resolve()),
        'gui_sha256': digest(args.gui.resolve()) if args.gui else None
    }, indent=2)+'\n', encoding='utf-8')
    print('PASS:', len(before), 'records and identity rows preserved; exact donor/name appended; Restore passed', flush=True)


if __name__ == '__main__':
    main()
