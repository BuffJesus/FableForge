"""Test-only content overlays plus explicit full-source bank checks.

These fixtures exercise level/asset composition, never certify an installed mod.
"""
import json
from pathlib import Path
import shutil
import subprocess
from test_aeon_controller import inventory
from test_fmp_links import read_bank
from test_fmp_texture_layers import entries


def accept_stable_overhaul(tool, base, source, output, cwd, evidence):
    """Audit an original compatible whole-bank source without changing inputs."""
    base, source, output, evidence = map(Path, (base, source, output, evidence))
    before = [inventory(path) for path in (base, source)]
    proc = subprocess.run([str(tool), 'mods', 'merge', str(base), str(output),
                           '--with', str(source)], cwd=cwd, capture_output=True, text=True, timeout=300)
    evidence.mkdir(parents=True, exist_ok=True)
    (evidence/'acceptance.log').write_text(proc.stdout+proc.stderr, encoding='utf-8')
    assert proc.returncode == 0, proc.stdout+proc.stderr
    checked = audit_stable_overhaul(source, output)
    assert [inventory(path) for path in (base, source)] == before
    (evidence/'result.json').write_text(json.dumps({'full_source_accepted': True,
        **checked, 'all_inputs_preserved': True, 'gameplay_tested': False}, indent=2), encoding='utf-8')
    return proc.stdout


def audit_stable_overhaul(source, output):
    """Independently compare original numeric slots and complete text payloads."""
    source, output = map(Path, (source, output))
    counts = {}
    for bank in ('game.bin', 'script.bin', 'frontend.bin'):
        expected, actual = read_bank(source, bank), read_bank(output, bank)
        assert len(actual) >= len(expected), bank
        assert actual[:len(expected)] == expected, bank
        counts[bank] = len(expected)
    expected = entries(source/'data/lang/English/text.big', 'TEXT_ENGLISH_MAIN')
    actual = entries(output/'data/lang/English/text.big', 'TEXT_ENGLISH_MAIN')
    for symbol, original in expected.items():
        assert symbol in actual, symbol
        assert all(actual[symbol][key] == original[key] for key in ('id', 'type', 'header', 'payload')), symbol
    return {'source_bank_rows': counts, 'source_text_entries': len(expected)}


def refuse_overhaul(tool, base, source, output, cwd, evidence):
    """Check the full source refuses without changing inputs or prior output."""
    base, source, output, evidence = map(Path, (base, source, output, evidence))
    output.mkdir(parents=True, exist_ok=True)
    marker = output/'prior-result.txt'
    if not marker.exists(): marker.write_bytes(b'preserve prior output')
    before = [inventory(path) for path in (base, source, output)]
    proc = subprocess.run([str(tool), 'mods', 'merge', str(base), str(output),
                           '--with', str(source)], cwd=cwd, capture_output=True, text=True, timeout=300)
    evidence.mkdir(parents=True, exist_ok=True)
    (evidence/'refusal.log').write_text(proc.stdout+proc.stderr, encoding='utf-8')
    assert proc.returncode != 0 and 'changed or new unnamed definition entries' in proc.stderr, proc.stderr
    assert [inventory(path) for path in (base, source, output)] == before
    (evidence/'result.json').write_text(json.dumps({'full_source_refused': True,
        'all_inputs_and_prior_output_preserved': True, 'gameplay_tested': False}, indent=2), encoding='utf-8')


def content_only(source, target, *, include_graphics=True):
    """A copied test tree for the retained level/asset checks, never a mod install."""
    source, target = Path(source), Path(target)
    assert not target.exists()
    def excluded(directory, children):
        relative = Path(directory).relative_to(source)
        language = [part.lower() for part in relative.parts[:2]] == ['data', 'lang']
        return [name for name in children if name.lower() == 'compileddefs'
                or (not include_graphics and [part.lower() for part in relative.parts] == ['data']
                    and name.lower() == 'graphics')
                or (language and name.lower() == 'text.big')]
    shutil.copytree(source, target, ignore=excluded)
    assert not (target/'data/CompiledDefs').exists()
    assert not any(path.name.lower() == 'text.big' for path in (target/'data/lang').rglob('*'))
    print('CONTENT-ONLY FIXTURE: definition/language banks excluded; not a complete mod installation:', target, flush=True)
    if not include_graphics:
        assert not (target/'data/graphics').exists()
        print('LEVEL/WAD SCOPE: graphics banks excluded from this overlap fixture', flush=True)
    return target
