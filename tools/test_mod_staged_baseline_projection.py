#!/usr/bin/env python3
"""Staged ordinary snapshots preserve originals while omitting unrelated trees."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
from test_mod_field_details import write_bank
from test_mod_file_choice_contributors import snapshot
from test_mod_asset_choice_contributors import bank
from test_mod_text_choice_contributors import text_bank, text_values


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    parser.add_argument('--work-root', type=Path, default=repo/'build')
    args = parser.parse_args()
    args.work_root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='mod-staged-baseline-', dir=args.work_root)).resolve()
    print('Evidence retained at', work, flush=True)
    root = work/'install'
    write_bank(root, (1, 2, 3, 4))
    text_bank(root, 'English', {'SHARED': 'original text'})

    def put(folder, relative, data):
        path = folder/relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def run(name, *arguments):
        result = subprocess.run([str(args.tool.resolve()), *map(str, arguments)],
                                capture_output=True, text=True, timeout=120)
        (work/(name+'.log')).write_text(result.stdout+'\n'+result.stderr)
        assert result.returncode == 0, (arguments, result.stderr)
        return json.loads(result.stdout) if '--json' in arguments else None

    original = b'original sound'
    put(root, 'data/Sound/touched.bin', original)
    authored_backup = 'data/graphics/authored.retail-bak'
    put(root, authored_backup, b'original authored backup')
    for relative in ('data/Sound/unused.bin', 'data/Video/unused.bin',
                     'FSE/backups/unused.bin', 'Mods/unused.bin',
                     'data/graphics/graphics.big.retail-bak',
                     'data/Levels/FinalAlbion.wad.atlas-orig',
                     'data/lang/English/Dialogue.lut'):
        put(root, relative, b'x'*(1024*1024))
    a, b = work/'PackA', work/'PackB'
    text_bank(a, 'English', {'SHARED': 'first text'})
    text_bank(b, 'English', {'SHARED': 'last text'})
    put(a, 'data/Sound/touched.bin', b'first sound')
    put(b, 'data/Sound/touched.bin', b'last sound')
    put(a, 'FSE/new.lua', b'new script')
    put(b, 'FSE/new.lua', b'last script')
    put(a, authored_backup, b'first authored backup')
    put(b, authored_backup, b'last authored backup')
    for source in (a, b):
        run('add-'+source.name, 'mods', 'add', root, source)
    picks = work/'picks.txt'
    picks.write_text('file:data/sound/touched.bin\tvanilla\nfile:fse/new.lua\tvanilla\n'
                     'file:data/graphics/authored.retail-bak\tvanilla\n'
                     'text:English|SHARED\tvanilla\n')
    initial = snapshot(root)
    sources_before = [snapshot(a), snapshot(b)]
    run('deploy', 'mods', 'deploy', root)
    installed = snapshot(root)
    assert (root/'data/Sound/touched.bin').read_bytes() == b'last sound'
    assert (root/'FSE/new.lua').read_bytes() == b'last script'
    assert text_values(root, 'English') == {'SHARED': 'last text'}
    reports = []
    try:
        for route in ('merge', 'build', 'conflicts'):
            output = work/route
            command = ['mods', route, root]
            if route != 'conflicts': command.append(output)
            if route == 'merge': command += ['--with', a, b]
            report = run(route, *command, '--picks', picks, '--json')
            footprint = report['baseline_originals']
            assert footprint['mode'] == 'ordinary_dependencies'
            assert footprint['bytes_copied'] < 1024*1024, footprint
            rows = {row['path']: row for row in report['files']['contested']}
            assert rows['data/sound/touched.bin']['winner'] == 'vanilla'
            assert not rows['data/sound/touched.bin']['choice_unavailable']
            if route != 'conflicts':
                assert (output/'data/Sound/touched.bin').read_bytes() == original
                assert not (output/'FSE/new.lua').exists()
                assert (output/authored_backup).read_bytes() == b'original authored backup'
                assert text_values(output, 'English') == {'SHARED': 'original text'}
            assert snapshot(root) == installed
            reports.append({'route': route, 'footprint': footprint})
        package = work/'Empty.fmp'
        bank(package, 'GameBINEntries', {})
        report = run('fmp', 'mods', 'merge', root, work/'fmp', '--with', a, b,
                     package, '--picks', picks, '--json')
        assert report['baseline_originals']['mode'] == 'ordinary_dependencies'
        assert report['baseline_originals']['bytes_copied'] < 1024*1024
        assert (work/'fmp/data/Sound/touched.bin').read_bytes() == original
        reports.append({'route': 'folder plus empty FMP', 'footprint': report['baseline_originals']})
        recipe = work/'Recipe'
        put(recipe, 'forge_pack.json', b'{"name":"Recipe","version":1}')
        report = run('recipe', 'mods', 'merge', root, work/'recipe', '--with', a, b,
                     recipe, '--picks', picks, '--json')
        assert report['baseline_originals']['mode'] == 'complete'
        assert report['baseline_originals']['bytes_copied'] >= 4*1024*1024
        assert (work/'recipe/data/Sound/touched.bin').read_bytes() == original
        assert snapshot(root) == installed
        reports.append({'route': 'recipe full snapshot fallback', 'footprint': report['baseline_originals']})
    finally:
        run('undeploy', 'mods', 'undeploy', root)
    assert snapshot(root) == initial
    assert [snapshot(a), snapshot(b)] == sources_before
    (work/'result.json').write_text(json.dumps({'checks': reports,
        'restored_exactly': True, 'sources_unchanged': True}, indent=2))
    print('PASS: staged merge/build/review projection, originals and absent files')


if __name__ == '__main__':
    main()
