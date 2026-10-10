#!/usr/bin/env python3
"""Reused outputs remove only obsolete, unchanged files recorded by prior builds."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
from test_mod_field_details import write_bank
from test_mod_file_choice_contributors import snapshot
from test_mod_output_publish import locked
from test_mod_tng_choice_contributors import tng, RELATIVE
from test_mod_qst_choice_contributors import qst
from test_mod_text_choice_contributors import text_bank


RECEIPT = Path('forge_mods_build.json')


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool', type=Path, default=repo/'build/forge-tools.exe')
    parser.add_argument('--work-root', type=Path, default=repo/'build')
    parser.add_argument('--complete-data', action='store_true')
    args = parser.parse_args()
    args.work_root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='mod-build-ownership-', dir=args.work_root)).resolve()
    print('Evidence retained at', work, flush=True)
    root = work/'install'; write_bank(root, (1, 2, 3, 4)); tng(root); qst(root, False)
    text_bank(root, 'English', {'SHARED': 'original'})
    keep = work/'Keep'; (keep/'data/Misc').mkdir(parents=True)
    (keep/'data/Misc/keep.bin').write_bytes(b'still enabled')
    source = work/'Authored'; tng(source, 20); qst(source, True)
    text_bank(source, 'English', {'SHARED': 'authored'})
    (source/RELATIVE.with_name('New.tng')).write_bytes((source/RELATIVE).read_bytes())
    authored = Path('data/Misc/authored café 🐺.bin')
    (source/authored).parent.mkdir(parents=True); (source/authored).write_bytes(b'authored loose bytes')
    # Incoming source bookkeeping must never establish output ownership.
    (source/RECEIPT).write_text('untrusted source receipt must be ignored')
    (source/'data'/RECEIPT).write_bytes(b'nested authored file retains its bytes')
    checks = []; counter = 0
    def run(*arguments, good=True):
        nonlocal counter
        counter += 1
        result = subprocess.run([str(args.tool.resolve()), *map(str, arguments)],
            capture_output=True, text=True, encoding='utf-8', timeout=120)
        (work/f'command-{counter:03d}.json').write_text(json.dumps({
            'arguments': list(map(str, arguments)), 'returncode': result.returncode,
            'stdout': result.stdout, 'stderr': result.stderr}, indent=2))
        assert (result.returncode == 0) == good, (arguments, result.stdout, result.stderr)
        return result
    complete = None
    if args.complete_data:
        complete = work/'Complete'; write_bank(complete, (1, 2, 3, 4))
        for binary in ('script.bin', 'frontend.bin'):
            (complete/'data/CompiledDefs'/binary).write_bytes((complete/'data/CompiledDefs/game.bin').read_bytes())
        (complete/RECEIPT).write_text('complete source bookkeeping must also be ignored')
        (complete/'data'/RECEIPT).write_bytes(b'complete nested authored file')
        run('mods', 'add', root, complete, '--install-data')
    for pack in (keep, source): run('mods', 'add', root, pack)
    sources = [keep, source]+([complete] if complete else [])
    original_sources = {str(p): snapshot(p) for p in sources}
    def command(route, output):
        cmd = ['mods', route, root, output]
        if route == 'merge':
            order = json.loads((root/'forge_mods.json').read_text(encoding='utf-8'))['mods']
            cmd += ['--with', *[m['source'] for m in order if m['enabled']]]
            if complete: cmd += ['--install-data']
        return cmd+['--json']
    def verify_receipt(output, unowned=()):
        receipt = json.loads((output/RECEIPT).read_text(encoding='utf-8'))
        files = snapshot(output)
        expected = {name: digest for name, digest in files.items() if name != RECEIPT.as_posix() and name not in unowned}
        assert receipt['format'] == 1 and set(receipt['files']) == set(expected)
        for name, digest in expected.items():
            assert receipt['files'][name] == {'bytes': (output/name).stat().st_size, 'sha256': digest}
        return receipt
    unowned = {'sentinel': b'unrelated output', 'data/Misc/unowned.bin': b'legacy unowned output'}
    for route in ('merge', 'build'):
        run('mods', 'enable', root, 'Authored')
        output = work/route; output.mkdir()
        for name, content in unowned.items():
            path = output/name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(content)
        run(*command(route, output)); first_receipt = verify_receipt(output, unowned)
        assert (output/authored).read_bytes() == b'authored loose bytes'
        assert (output/'data/Levels/New.tng').is_file()
        assert (output/'data'/RECEIPT).read_bytes() == b'nested authored file retains its bytes'
        checks.append({'route': route, 'independent_inventory_digest_and_source_receipt_exclusion': True})
        run('mods', 'disable', root, 'Authored')
        before_inputs = {str(p): snapshot(p) for p in [root, *sources]}
        # Obsolete removals happen before these late rename failures, so rollback
        # must restore both removed files and the old receipt.
        for target in (output/'data/CompiledDefs/game.bin', output/RECEIPT):
            before = snapshot(output)
            with locked(target):
                failure = run(*command(route, output), good=False)
            assert 'cannot publish mod build' in failure.stderr
            assert snapshot(output) == before and not list(output.glob('.forge-mod-output-*'))
            checks.append({'route': route, 'locked_target': target.relative_to(output).as_posix(), 'removal_and_receipt_rollback': True})
        path = output/authored; original = path.read_bytes(); path.write_bytes(b'user edits must survive')
        before = snapshot(output); failure = run(*command(route, output), good=False)
        assert 'previously generated file changed' in failure.stderr
        assert snapshot(output) == before
        path.write_bytes(original)
        checks.append({'route': route, 'changed_obsolete_owned_file_refuses_without_writes': True})
        # Already missing owned files need no deletion and are dropped from the next receipt.
        (output/'data/Levels/New.tng').unlink()
        run(*command(route, output)); reference = work/(route+'-fresh'); run(*command(route, reference))
        receipt = verify_receipt(output, unowned)
        expected = snapshot(reference)
        expected.update({name: hashlib.sha256(content).hexdigest() for name, content in unowned.items()})
        assert snapshot(output) == expected
        assert not (output/authored).exists() and not (output/'data/Levels/New.tng').exists()
        assert not (output/'forge_mods_provenance.json').exists()
        if complete: assert (output/'data'/RECEIPT).read_bytes() == b'complete nested authored file'
        else: assert not (output/'data'/RECEIPT).exists()
        assert before_inputs == {str(p): snapshot(p) for p in [root, *sources]}
        checks.append({'route': route, 'reused_equals_fresh_plus_unowned_files': True})
        original_receipt = (output/RECEIPT).read_bytes()
        before = snapshot(output); run(*command(route, output)); assert snapshot(output) == before
        checks.append({'route': route, 'deterministic_repeat': True})
        outside = work/(route+'-outside.bin'); outside.write_bytes(b'outside must survive')
        sample = next(iter(receipt['files'].values())); sample_name = next(iter(receipt['files']))
        cases = [('invalid JSON', b'not JSON'), ('unsupported version', {'format': 99, 'files': {}}),
                 ('invalid digest', {'format': 1, 'files': {sample_name: dict(sample, sha256='bad')}}),
                 ('invalid size', {'format': 1, 'files': {sample_name: dict(sample, bytes=-1)}}),
                 ('traversal', {'format': 1, 'files': {'../'+outside.name: sample}}),
                 ('absolute', {'format': 1, 'files': {str(outside): sample}}),
                 ('backup', {'format': 1, 'files': {'protected.forgebak': sample}}),
                 ('recovery tree', {'format': 1, 'files': {'.forge-mod-output-recovery/saved.bin': sample}}),
                 ('receipt itself', {'format': 1, 'files': {RECEIPT.as_posix(): sample}}),
                 ('duplicate aliases', {'format': 1, 'files': {sample_name: sample, sample_name.swapcase(): sample}}),
                 ('trailing separator', {'format': 1, 'files': {'data/': sample}}),
                 ('alternate stream', {'format': 1, 'files': {'data/Misc/unowned.bin:stream': sample}}),
                 ('device name', {'format': 1, 'files': {'data/NUL.bin': sample}})]
        for label, broken in cases:
            (output/RECEIPT).write_bytes(broken if isinstance(broken, bytes) else json.dumps(broken).encode())
            before = snapshot(output); failure = run(*command(route, output), good=False)
            assert 'mods build receipt:' in failure.stderr
            assert snapshot(output) == before and outside.read_bytes() == b'outside must survive'
            assert before_inputs == {str(p): snapshot(p) for p in [root, *sources]}
            checks.append({'route': route, 'invalid_receipt': label, 'refused_preserved': True})
        (output/RECEIPT).write_bytes(original_receipt)
        # Real Windows junctions must not redirect receipt-owned reads or removals.
        import _winapi
        external = work/(route+'-external'); external.mkdir(); (external/'owned.bin').write_bytes(b'external file')
        link = output/'redirect'; assert not link.exists() and link.absolute().is_relative_to(work)
        _winapi.CreateJunction(str(external), str(link))
        assert link.is_junction()
        try:
            broken = copy.deepcopy(receipt)
            broken['files']['redirect/owned.bin'] = {'bytes': len(b'external file'), 'sha256': hashlib.sha256(b'external file').hexdigest()}
            (output/RECEIPT).write_text(json.dumps(broken))
            before_external = snapshot(external)
            failure = run(*command(route, output), good=False)
            assert 'mods build receipt:' in failure.stderr
            assert snapshot(external) == before_external
            checks.append({'route': route, 'junction_owned_path_refused_external_preserved': True})
        finally:
            # Remove only the verified junction itself, never its target tree.
            assert link.is_junction() and link.absolute().is_relative_to(work)
            link.rmdir(); (output/RECEIPT).write_bytes(original_receipt)
        run(*command(route, output)); verify_receipt(output, unowned)
        # A staged rebuild must use the fresh composition, including when the
        # reusable output currently belongs to a now-disabled source.
        run('mods', 'enable', root, 'Authored'); run(*command(route, output))
        run('mods', 'disable', root, 'Authored')
        stage_command = command(route, output)[:-1]+['--stage', '--json']
        before_root = snapshot(root); before_output = snapshot(output)
        with locked(output/'data/CompiledDefs/game.bin'):
            failure = run(*stage_command, good=False)
        assert 'cannot publish mod build' in failure.stderr
        assert snapshot(root) == before_root and snapshot(output) == before_output
        checks.append({'route': route, 'staged_publish_failure_preserves_install_and_output': True})
        report = json.loads(run(*stage_command).stdout)
        try:
            assert report['stage']['staged'] > 0
            assert not (root/RECEIPT).exists() and not (root/'sentinel').exists()
            assert not (root/'data/Misc/unowned.bin').exists()
            assert not (root/authored).exists() and not (root/'data/Levels/New.tng').exists()
            from test_mod_tng_choice_contributors import values
            assert values(root)[123456] == 10
            stage_plan = json.loads((root/'forge_stage_manifest.json').read_text(encoding='utf-8'))
            assert not any(row['path'] == RECEIPT.as_posix() for row in stage_plan['files'])
            assert (output/'sentinel').read_bytes() == unowned['sentinel']
            before = {str(p): snapshot(p) for p in (root, output)}
            failure = run(*stage_command, good=False)
            assert 'already staged' in failure.stderr
            assert before == {str(p): snapshot(p) for p in (root, output)}
        finally: run('mods', 'undeploy', root)
        assert snapshot(root) == before_root
        checks.append({'route': route, 'fresh_only_stage_existing_stage_refusal_and_exact_undeployment': True})
        ego_backup = root/'data/CompiledDefs/game.bin.tmp'; ego_backup.write_bytes(b'owned fake EgoCore marker')
        before = {str(p): snapshot(p) for p in (root, output)}
        failure = run(*stage_command, good=False)
        assert 'EgoCore has deployed' in failure.stderr
        assert before == {str(p): snapshot(p) for p in (root, output)}
        ego_backup.unlink()
        checks.append({'route': route, 'egocore_stage_guard_preserves_install_and_output': True})
        for invalid_output in (root, root/'output', work, keep, keep/'output', Path(str(keep).swapcase())):
            before = {str(p): snapshot(p) for p in [root, output, *sources]}
            failure = run(*command(route, invalid_output), good=False)
            assert 'Mod output must be outside' in failure.stderr
            assert before == {str(p): snapshot(p) for p in [root, output, *sources]}
            checks.append({'route': route, 'overlapping_output': str(invalid_output), 'refused_preserved': True})
    assert original_sources == {str(p): snapshot(p) for p in sources}
    (work/'result.json').write_text(json.dumps({'ok': True, 'checks': checks, 'complete_data': args.complete_data}, indent=2))
    print('PASS:', len(checks), 'build ownership/cleanup/refusal/rollback checks')


if __name__ == '__main__': main()
