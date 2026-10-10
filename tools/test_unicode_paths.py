#!/usr/bin/env python3
"""Windows CLI arguments and packaged GUI helpers preserve Unicode paths."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from test_setup_restore_ui import level


def snapshot(root):
    return {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in root.rglob('*') if p.is_file()}


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gui', type=Path, default=repo/'build/FableForge.exe')
    parser.add_argument('--install', type=Path, required=True)
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('this fixture exercises Windows process argument conversion')
    # Keep a failed owned child from opening a Windows fault-report dialog.
    ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002)
    work = Path(tempfile.mkdtemp(prefix='unicode-paths-', dir=repo/'build')).resolve()
    print('Evidence retained at', work, flush=True)
    cwd = work/'unrelated working directory'
    cwd.mkdir()
    marker = '\u00e9 \u03a9 \u6f22\u5b57 %FORGE_UNICODE_TOKEN% & files'
    results = []
    sources = [args.install/'data/CompiledDefs'/name for name in ('game.bin', 'names.bin')]
    source_hashes = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
    def run_gui(case, binary, selected, commands, size='1440x900', scale=1):
        script = work/(case+'.txt')
        script.write_text('\n'.join(['wait_maps', 'wait_ready', f'set uiscale {scale}',
            'mods_tab 1', *commands, 'quit', '']), encoding='utf-8')
        result = subprocess.run([str(binary), '--install', str(selected), '--size', size,
            '--auto', str(script)], cwd=cwd, capture_output=True, timeout=90,
            env=dict(os.environ, FABLEFORGE_AUTOMATION_HIDDEN='1', FORGE_UNICODE_TOKEN='expanded',
                APPDATA=str(work/(case+'-appdata')), LOCALAPPDATA=str(work/(case+'-localappdata'))))
        output = (result.stdout+result.stderr).decode('utf-8', errors='replace')
        (work/(case+'.log')).write_text(output, encoding='utf-8')
        log_path = Path(str(script)+'.log')
        log = log_path.read_text(encoding='utf-8') if log_path.exists() else output
        results.append({'case': case, 'returncode': result.returncode,
                        'passed': result.returncode == 0 and 'RESULT PASS' in log})
        print(results[-1], flush=True)
    for label, suffix in [('ascii', 'plain files'), ('unicode', marker)]:
        root = work/('install '+suffix)
        defs = root/'data/CompiledDefs'
        defs.mkdir(parents=True)
        for name in ('game.bin', 'names.bin'):
            shutil.copyfile(args.install/'data/CompiledDefs'/name, defs/name)
        levels = root/'data/Levels/FinalAlbion'
        levels.mkdir(parents=True)
        (levels/'UnicodePath.lev').write_bytes(level(4))
        (levels/'UnicodePath.tng').write_bytes(b'Version 2;\r\n')
        before = snapshot(root)
        for name, command in [('list', ['forge.exe', 'list', '--install', str(root)]),
                              ('conflicts', ['forge-tools.exe', 'mods', 'conflicts', str(root), '--json'])]:
            binary = args.gui.resolve().with_name(command[0])
            result = subprocess.run([str(binary), *command[1:]], cwd=cwd,
                capture_output=True, timeout=45)
            output = (result.stdout+result.stderr).decode('utf-8', errors='replace')
            (work/(label+'-'+name+'.log')).write_text(output, encoding='utf-8')
            passed = result.returncode == 0 and ('UnicodePath' in output if name == 'list' else '"sources"' in output)
            results.append({'case': label+'-'+name, 'returncode': result.returncode, 'passed': passed})
            print(results[-1], flush=True)
        assert snapshot(root) == before, 'read-only commands changed the install'
        app = work/('application '+suffix)
        app.mkdir()
        for binary in ('FableForge.exe', 'forge-tools.exe', 'forge.exe'):
            shutil.copyfile(args.gui.resolve().with_name(binary), app/binary)
        schema = app/'docs/re_reference/def_schema.json'
        schema.parent.mkdir(parents=True)
        schema_source = args.gui.resolve().parent/'docs/re_reference/def_schema.json'
        if not schema_source.is_file():
            schema_source = repo/'docs/re_reference/def_schema.json'
        shutil.copyfile(schema_source, schema)
        result = subprocess.run([str(app/'forge-tools.exe'), 'terrain', 'themes',
            str(levels/'UnicodePath.lev'), '--root', str(root), '--json'],
            cwd=cwd, capture_output=True, timeout=45)
        output = (result.stdout+result.stderr).decode('utf-8', errors='replace')
        (work/(label+'-schema.log')).write_text(output, encoding='utf-8')
        results.append({'case': label+'-schema', 'returncode': result.returncode,
                        'passed': result.returncode == 0 and '"themes"' in output})
        print(results[-1], flush=True)
        # Isolate executable lookup from install argument conversion first.
        selected = work/'install plain files'
        selected_before = snapshot(selected)
        selected_mods = 0 if label == 'ascii' else 1
        commands = ['mods_conflicts', 'wait_mods',
                    f'assert_log mods conflicts: 0 conflict(s) across {selected_mods} mod(s)']
        run_gui(label+'-gui', app/'FableForge.exe', selected, commands)
        assert snapshot(selected) == selected_before
        if label == 'unicode':
            run_gui('unicode-gui-install', app/'FableForge.exe', root,
                ['mods_conflicts', 'wait_mods', 'assert_log mods conflicts: 0 conflict(s) across 0 mod(s)'])
            assert snapshot(root) == before
        if all(result['passed'] for result in results):
            pack = work/('pack '+suffix)
            pack_name = 'PathPack' if label == 'ascii' else 'PathPack \u00e9 \u03a9 \u6f22\u5b57'
            payload = Path('data/Misc')/('content '+suffix+'.txt')
            (pack/payload).parent.mkdir(parents=True)
            (pack/payload).write_bytes(b'from the owned Unicode-path pack')
            (pack/'forge_pack.json').write_text(json.dumps({'version': 1,
                'name': pack_name, 'models': [], 'groundThemes': []}), encoding='utf-8')
            pack_before = snapshot(pack)
            (root/payload).parent.mkdir(parents=True)
            (root/payload).write_bytes(b'original owned content')
            result = subprocess.run([str(app/'forge-tools.exe'), 'mods', 'add', str(root),
                str(pack), '--name', pack_name], cwd=cwd, capture_output=True, timeout=45)
            output = (result.stdout+result.stderr).decode('utf-8', errors='replace')
            (work/(label+'-add.log')).write_text(output, encoding='utf-8')
            results.append({'case': label+'-add', 'returncode': result.returncode,
                            'passed': result.returncode == 0})
            print(results[-1], flush=True)
            if result.returncode == 0:
                order = json.loads((root/'forge_mods.json').read_text(encoding='utf-8'))
                assert order['mods'][0]['name'] == pack_name
                assert Path(order['mods'][0]['source']).resolve() == pack.resolve()
                baseline = snapshot(root)
                for size, scale in [('1440x900', 1), ('800x600', 1.5)]:
                    case = label+'-'+size
                    def button(widget):
                        return [f'reveal {widget}', 'frames 3', f'reveal {widget}',
                            'frames 3', f'click {widget}', 'wait_mods', 'wait_ready']
                    run_gui(case+'-deploy', app/'FableForge.exe', root,
                        [f'set saveroot {root}\\', 'assert_state mods_count 1',
                         *button('btn_mods_conflicts'),
                         'assert_log mods conflicts: 0 conflict(s) across 1 mod(s)',
                         *button('btn_mods_deploy'), 'assert_log mods deploy: done'], size, scale)
                    assert (root/payload).read_bytes() == (pack/payload).read_bytes()
                    run_gui(case+'-undeploy', app/'FableForge.exe', root,
                        [f'set saveroot {root}\\', *button('btn_mods_undeploy'),
                         'assert_log mods undeploy: done'], size, scale)
                    assert snapshot(root) == baseline, 'undeploy did not restore the complete inventory'
                assert snapshot(pack) == pack_before, 'the source pack changed'
    assert {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in sources} == source_hashes
    (work/'result.json').write_text(json.dumps(results, indent=2)+'\n', encoding='utf-8')
    assert all(result['passed'] for result in results), results
    print('Unicode/ASCII CLI, packaged helpers and exact GUI deploy/undeploy paths: PASS')


if __name__ == '__main__':
    main()
