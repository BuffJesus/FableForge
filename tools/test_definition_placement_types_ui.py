#!/usr/bin/env python3
"""Place definitions by their compiled class, independently of their names."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import zlib
from test_fmp_links import crc0, read_bank
from test_setup_restore_ui import level
from test_texture_lookup import texture_bank
from test_definition_edit_ui import click

ROWS = [('CREATURE', 'GUI_ROOT', 2), ('OBJECT', 'CREATURE_DECOY', 4),
        ('BUILDING', 'PLAIN_HOUSE', 4), ('CREATURE', 'CREATURE_NORMAL', 3),
        ('OBJECT', 'OBJECT_NORMAL', 4), ('BUILDING', 'BUILDING_NORMAL', 4)]


def fixture(root, schema):
    folder = root/'data/CompiledDefs'
    folder.mkdir(parents=True)
    pool, refs, table, offsets, payloads = bytearray(), {}, bytearray(), [], bytearray()
    counts = {}
    for kind, name, owner in ROWS:
        for value in (kind, name):
            if value not in refs:
                refs[value] = len(pool)+4
                pool += struct.pack('<I', crc0(value))+value.encode('ascii')+b'\0'
        table += struct.pack('<III', refs[kind], refs[name], counts.get(kind, 0))
        counts[kind] = counts.get(kind, 0)+1
        body = bytearray(b'\x01\0\x01')
        for field in schema[kind]['fields']:
            assert field['name'], 'fixture requires named field anchors'
            body += struct.pack('<I', crc0(field['name']))
            body += struct.pack('<IIIff', 0, 0, 0, 1, 0) if field['name']=='Graphic' else bytes(4)
        # DefaultOwner is retained as a tagged scalar in the reconstructed blob.
        if kind == 'CREATURE':
            body += struct.pack('<Ii', crc0('DefaultOwner'), owner)
        offsets.append(2*len(ROWS)+len(payloads))
        payloads += body
    (folder/'names.bin').write_bytes(struct.pack('<5I', 0, 0xa8e36c34, len(refs), len(pool)+4, 0)+pool)
    chunk = zlib.compress(struct.pack('<'+'H'*len(ROWS), *offsets)+payloads)
    (folder/'game.bin').write_bytes(bytes(9)+struct.pack('<I', len(ROWS))+table+
        struct.pack('<I5I', 2, 0, 0, len(ROWS), len(chunk), len(chunk))+chunk)
    levels = root/'data/Levels/FinalAlbion'
    levels.mkdir(parents=True)
    (levels/'PlacementProbe.lev').write_bytes(level(4))
    (levels/'PlacementProbe.tng').write_bytes(b'Version 2;\r\n')
    texture_bank(root/'data/graphics/pc/textures.big', [(7, 'TEXTURE_BASE', 20)])
    records = read_bank(root)
    assert [(r['definition'], r['name']) for r in records] == [(k, n) for k, n, _ in ROWS]


def inventory(root):
    return {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in root.rglob('*') if p.is_file()}


def verify_tng(path):
    text = path.read_text(encoding='ascii')
    blocks = re.findall(r'NewThing (\w+);(.*?)EndThing;', text, re.S)
    expected = ROWS+[('CREATURE', 'GUI_ROOT', 1), ('CREATURE', 'GUI_ROOT', 2)]
    assert len(blocks) == len(expected), (len(blocks), len(expected))
    for index, ((actual_type, body), (kind, name, owner)) in enumerate(zip(blocks, expected)):
        wanted = 'AICreature' if kind=='CREATURE' else 'Building' if kind=='BUILDING' else 'Object'
        assert actual_type == wanted, (name, actual_type, wanted)
        assert f'DefinitionType "{name}";' in body
        assert f'Player {owner};' in body, (name, owner)
        assert (f'ScriptName Probe{index};' if index<=len(ROWS) else 'ScriptName NULL;') in body
        for axis in 'XYZ':
            initial = re.search(r'\bInitialPos'+axis+r' ([^;]+);', body)
            position = re.search(r'\bPosition'+axis+r' ([^;]+);', body)
            assert position, (name, axis)
            if kind=='CREATURE':
                assert initial and float(initial[1])==float(position[1]), (name, axis)
            else:
                assert initial is None, (name, axis)


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gui', type=Path, default=repo/'build/FableForge.exe')
    parser.add_argument('--forge', type=Path, default=repo/'build/forge.exe')
    parser.add_argument('--work-root', type=Path, default=repo/'build')
    parser.add_argument('--prepare-only', action='store_true', help='Write and independently inspect fixtures/scripts without launching any executable')
    args = parser.parse_args()
    driver_hash = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    gui_hash = None if args.prepare_only else hashlib.sha256(args.gui.resolve(strict=True).read_bytes()).hexdigest()
    args.work_root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='placement-types-', dir=args.work_root.resolve()))
    print('Evidence retained at', work, flush=True)
    schema = json.loads((repo/'docs/re_reference/def_schema.json').read_text(encoding='utf-8'))
    cases = []
    for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
        case = work/size
        root = case/'install'
        fixture(root, schema)
        before = inventory(root)
        lines = ['wait_maps', 'wait_ready', f'set uiscale {scale}', 'select PlacementProbe',
                 'wait_loaded', 'edit 1', 'set place_owner auto', 'assert_state doc_things 0']
        for index, (kind, name, owner) in enumerate(ROWS+[('CREATURE', 'GUI_ROOT', 1)]):
            if index==len(ROWS): lines += ['set place_owner 1']
            lines += [f'place {name} Probe{index}', 'frames 2', f'assert_state selected_def {name}',
                      f'assert_state selected_player {owner}', f'assert_state doc_things {index+1}',
                      'undo', 'frames 2', f'assert_state doc_things {index}',
                      'redo', 'frames 2', f'assert_state doc_things {index+1}']
        lines += ['set place_owner auto', 'set place_def GUI_ROOT', 'edit_tab 2', 'frames 3']
        lines += click('btn_place_actor')+['assert_state doc_things 8', 'assert_state selected_player 2']
        lines += ['save_draft', 'frames 2', f'screenshot {(case/"placed.png").as_posix()}', 'quit']
        script = case/'place.txt'
        script.write_text('\n'.join(lines)+'\n', encoding='utf-8')
        if args.prepare_only:
            assert inventory(root)==before
            cases.append({'size': size, 'prepared': True})
            continue
        gui = args.gui.resolve(strict=True)
        result = subprocess.run([str(gui), '--install', str(root), '--size', size, '--auto', str(script)],
            cwd=repo, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=120,
            env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1', APPDATA=str(case/'profile'), LOCALAPPDATA=str(case/'local')))
        (case/'output.log').write_text(result.stdout+result.stderr, encoding='utf-8')
        trace = Path(str(script)+'.log').read_text(encoding='utf-8')
        assert result.returncode==0 and 'RESULT PASS' in trace and 'FAIL ' not in trace, trace[-6500:]
        verify_tng(root/'data/Levels/FinalAlbion/PlacementProbe.tng')
        # Only the owned loose TNG may change; all definitions, texture and LEV bytes stay exact.
        after = inventory(root)
        target = 'data/Levels/FinalAlbion/PlacementProbe.tng'
        assert all(after[name]==digest for name,digest in before.items() if name!=target)
        restored = subprocess.run([str(args.forge.resolve()), 'restore', '--install', str(root)], capture_output=True, text=True, timeout=60)
        (case/'restore.log').write_text(restored.stdout+restored.stderr, encoding='utf-8')
        assert restored.returncode==0, restored.stdout+restored.stderr
        expected_restored = dict(before)
        expected_restored[target+'.forge-orig'] = before[target]
        assert inventory(root)==expected_restored, 'Restore must recover all original bytes and retain only the exact first-original backup'
        cases.append({'size': size, 'placements': len(ROWS)+2, 'passed': True})
    assert hashlib.sha256(Path(__file__).read_bytes()).hexdigest()==driver_hash, 'driver changed during run'
    if gui_hash is not None:
        assert hashlib.sha256(args.gui.resolve().read_bytes()).hexdigest()==gui_hash, 'GUI changed during run'
    (work/'result.json').write_text(json.dumps({'prepared_only': args.prepare_only, 'passed': not args.prepare_only,
        'cases': cases, 'driver_sha256': driver_hash, 'gui_sha256': gui_hash}, indent=2)+'\n', encoding='utf-8')


if __name__=='__main__':
    main()
