#!/usr/bin/env python3
"""Verify finite float conflict text and exact selected bank bits."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile

from test_mod_field_details import write_bank
from test_fmp_links import read_bank
from test_mod_file_choice_contributors import snapshot

FIELDS = ('MinSecondsBetweenTeleport', 'MinSecondsBetweenWillUse')


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    parser.add_argument('--work-root', type=Path, default=repo/'build')
    parser.add_argument('--gui', type=Path)
    parser.add_argument('--install', type=Path)
    args = parser.parse_args()
    if args.gui and not args.install:
        parser.error('--gui requires --install')
    args.work_root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='mod-float-values-', dir=args.work_root)).resolve()
    assert work.parent == args.work_root.resolve()
    print('Evidence retained at', work, flush=True)
    checks = []

    def cli(label, *arguments):
        result = subprocess.run([str(args.tool.resolve()), *map(str, arguments)],
                                cwd=repo, capture_output=True, text=True, timeout=120)
        (work/(label+'.log')).write_text(result.stdout+result.stderr, encoding='utf-8')
        assert result.returncode == 0, result.stdout+result.stderr
        return json.loads(result.stdout) if '--json' in arguments else None

    for case, first, second in [('adjacent', 0x41a00000, 0x41a00001),
                                ('signed-zero', 0x80000000, 0x00000000)]:
        folder = work/case
        root, a, b = (folder/name for name in ('baseline', 'PackA', 'PackB'))
        for path, bits, other in [(root, 0x41200000, 10), (a, first, 100), (b, second, 10)]:
            write_bank(path, [struct.pack('<I', bits), struct.pack('<f', other)],
                       definition='CGuildMasterDef', record='FLOAT_REPORT_PROBE', fields=FIELDS)
        for source in (a, b):
            cli(case+'-add-'+source.name, 'mods', 'add', root, source, '--name', source.name)
        original = {str(path): snapshot(path) for path in (root, a, b)}
        picks = folder/'choices.txt'
        for route in ('merge', 'build'):
            command = ['mods', route, root, folder/route]
            if route == 'merge': command += ['--with', a, b]
            report = cli(case+'-'+route+'-order', *command, '--json')
            conflict = report['defs']['conflicts'][0]['field_conflicts'][0]
            assert conflict['field'] == FIELDS[0]
            assert conflict['type'] == 'float' and conflict['winner'] == 'PackB'
            displayed = conflict['values']
            assert [v['mod'] for v in displayed] == ['PackA', 'PackB']
            assert displayed[0]['value'] != displayed[1]['value']
            for value, bits in zip(displayed, (first, second)):
                assert struct.pack('<f', float(value['value'])) == struct.pack('<I', bits)
            for choice, expected in [('PackA', first), ('PackB', second), ('vanilla', 0x41200000)]:
                picks.write_text(conflict['choice_key']+'\t'+choice+'\n', encoding='utf-8')
                saved = picks.read_bytes()
                cli(case+'-'+route+'-'+choice, *command, '--picks', picks, '--json')
                raw = read_bank(folder/route)[0]['data']
                assert struct.unpack_from('<I', raw, 7)[0] == expected
                assert struct.unpack_from('<f', raw, 15)[0] == 100
                assert picks.read_bytes() == saved
                checks.append(case+'/'+route+'/'+choice)
        assert all(snapshot(Path(path)) == before for path, before in original.items())
        if args.gui and case == 'adjacent':
            for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
                script = folder/(size+'.txt')
                key = conflict['choice_key']
                lines = ['wait_maps', 'wait_ready', f'set saveroot {root.as_posix()}',
                         f'set uiscale {scale}', 'mods_tab 1', 'frames 2',
                         'mods_conflicts', 'wait_mods', 'frames 3',
                         'assert_state mods_field_conflicts 1',
                         f'assert_state mods_first_field_0 {FIELDS[0]}',
                         'assert_state mods_first_field_base_0 10',
                         'assert_state mods_first_field_value_0 20.000002',
                         f'mod_pick {key} PackA', 'frames 3',
                         'assert_state mods_first_field_value_0 20',
                         f'mod_pick {key} vanilla', 'frames 3',
                         'assert_state mods_first_field_value_0 10',
                         f'mod_pick {key} -', 'frames 3',
                         'assert_state mods_first_field_value_0 20.000002',
                         'reveal mod_fields_0', 'frames 3', 'reveal mod_fields_0',
                         'frames 3', 'click mod_fields_0', 'frames 3',
                         f'screenshot {(folder/(size+".png")).as_posix()}', 'quit']
                script.write_text('\n'.join(lines)+'\n', encoding='utf-8')
                result = subprocess.run([str(args.gui.resolve()), '--install', str(args.install.resolve()),
                    '--size', size, '--auto', str(script)], cwd=repo, capture_output=True,
                    text=True, timeout=240, env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1',
                        APPDATA=str(folder/(size+'-appdata')), LOCALAPPDATA=str(folder/(size+'-localappdata'))))
                (folder/(size+'.output.log')).write_text(result.stdout+result.stderr, encoding='utf-8')
                log = Path(str(script)+'.log').read_text(encoding='utf-8')
                assert result.returncode == 0 and 'RESULT PASS' in log, log[-5000:]
                checks.append('gui/'+size)
    (work/'result.json').write_text(json.dumps({'passed': True, 'checks': checks,
        'inputs_unchanged': True, 'finite_float_text_roundtrips': True}, indent=2), encoding='utf-8')
    print('PASS:', len(checks), 'exact choices; adjacent values, signed zero and inputs preserved', flush=True)


if __name__ == '__main__':
    main()
