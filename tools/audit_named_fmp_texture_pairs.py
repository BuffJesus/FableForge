#!/usr/bin/env python3
"""Check original texture-provider pairs offline; retain failures without promoting dependencies."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile
import zlib
from test_fmp_texture_layers import entries


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', required=True, type=Path)
    parser.add_argument('--providers', required=True, type=Path)
    parser.add_argument('--install', required=True, type=Path)
    parser.add_argument('--work-root', required=True, type=Path)
    args = parser.parse_args()
    tool = repo/'build/forge-tools.exe'
    report = json.loads(args.providers.read_text())
    baseline_files = [args.install/p for p in ('data/CompiledDefs/game.bin',
        'data/CompiledDefs/names.bin', 'data/graphics/graphics.big', 'data/graphics/pc/textures.big')]
    inputs = [args.archive, args.providers, tool] + baseline_files
    before = {str(p): digest(p) for p in inputs}
    assert before[str(args.archive)] == report['archive_sha256']
    status = {r['file']: r['coverage_status'] for r in report['packages']}
    args.work_root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='named-fmp-texture-pairs-', dir=args.work_root))
    print('Evidence retained at', work, flush=True)
    checks = []
    with zipfile.ZipFile(args.archive) as archive:
        catalogue = {r['file']: r for r in json.loads(archive.read('catalogue.json'))}
        for row in report['missing_texture_targets']:
            candidates = [p for p in row['candidates'] if status[p['file']] == 'importable_offline']
            if not candidates:
                continue
            provider = candidates[0]
            assert provider['bank'] == row['required_bank'] == 'maintextures'
            originals = []; parsed = []
            pair = work/str(len(checks)); pair.mkdir()
            for index, member in enumerate((provider['file'], row['consumer'])):
                raw = archive.read(member)
                assert hashlib.sha256(raw).hexdigest() == catalogue[member]['sha256']
                original = pair/(str(index)+'.fmp'); original.write_bytes(raw); originals.append(original)
                if raw[:4] == b'90\0\0':
                    decoder = zlib.decompressobj()
                    raw = decoder.decompress(raw[4:], 256*1024*1024+1)
                    assert len(raw) <= 256*1024*1024 and decoder.eof
                    assert not decoder.unused_data and not decoder.unconsumed_tail
                normalized = pair/(str(index)+'-parsed.fmp'); normalized.write_bytes(raw); parsed.append(normalized)
            texture_source = entries(parsed[0], 'maintextures', {row['target']})[row['target']]
            mesh_sources = entries(parsed[1], 'graphics')
            provider_meshes = entries(parsed[0], 'graphics')
            assert row['mesh'] in mesh_sources
            orders = []
            for reverse in (False, True):
                output = pair/('reverse' if reverse else 'forward'); output.mkdir()
                sentinel = output/'sentinel'; sentinel.write_bytes(b'prior output')
                command = [str(tool), 'mods', 'merge', str(args.install), str(output),
                           '--with', *map(str, reversed(originals) if reverse else originals), '--json']
                try:
                    result = subprocess.run(command, capture_output=True, text=True, timeout=240)
                    outcome = {'reverse': reverse, 'exit_code': result.returncode,
                               'reason': result.stderr.strip()}
                    if result.returncode == 0:
                        built_textures = entries(output/'data/graphics/pc/textures.big', 'GBANK_MAIN_PC', {row['target']})
                        expected_meshes = {**mesh_sources, **provider_meshes} if reverse else {**provider_meshes, **mesh_sources}
                        built_meshes = entries(output/'data/graphics/graphics.big', 'MBANK_ALLMESHES', set(expected_meshes))
                        for expected, actual in [(texture_source, built_textures[row['target']])] + [
                                (source, built_meshes[name]) for name, source in expected_meshes.items()]:
                            assert all(expected[key] == actual[key] for key in ('id', 'type', 'header', 'payload'))
                        outcome['bank_sha256'] = {str(p.relative_to(output)): digest(p) for p in
                            (output/'data/CompiledDefs/game.bin', output/'data/CompiledDefs/names.bin',
                             output/'data/graphics/graphics.big', output/'data/graphics/pc/textures.big')}
                        outcome['mesh_entries_checked'] = len(expected_meshes)
                        outcome['overlapping_mesh_names'] = sorted(set(mesh_sources) & set(provider_meshes))
                        outcome['target_texture_id'] = texture_source['id']
                    else:
                        assert {str(p.relative_to(output)) for p in output.rglob('*') if p.is_file()} == {'sentinel'}
                    assert sentinel.read_bytes() == b'prior output'
                except subprocess.TimeoutExpired:
                    outcome = {'reverse': reverse, 'exit_code': None, 'reason': '240-second timeout; inconclusive'}
                orders.append(outcome)
                resolved = output.resolve()
                assert resolved != work.resolve() and resolved.is_relative_to(work.resolve())
                shutil.rmtree(resolved)
            checks.append({'consumer': row['consumer'], 'provider': provider['file'], 'target': row['target'],
                           'source_sha256': [catalogue[n]['sha256'] for n in (provider['file'], row['consumer'])],
                           'orders': orders, 'gameplay_tested': False})
            (work/'result.json').write_text(json.dumps({'checks': checks, 'complete': False}, indent=2))
            print(row['consumer'], [r['exit_code'] for r in orders], flush=True)
    assert before == {str(p): digest(p) for p in inputs}
    (work/'result.json').write_text(json.dumps({'checks': checks, 'complete': True,
        'input_sha256': before, 'inputs_unchanged': True, 'dependencies_declared': False,
        'gameplay_tested': False}, indent=2))
    print('Completed:', len(checks), 'original pairs; see retained per-order results')


if __name__ == '__main__':
    main()
